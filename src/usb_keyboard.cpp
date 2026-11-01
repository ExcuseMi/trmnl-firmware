// A USB keyboard for apps on the TRMNL X: the X as USB host (ESP-IDF usb + espressif/usb_host_hid, boot protocol),
// powered through the charger's 5 V boost (IOX pin 1, OTG). When the port may be a host is usb_port.h's call;
// this file applies it. Device mode (charging, the USB serial port, flashing) is the default and comes back on
// app exit, input power, keyboard removal, a restart or deep sleep.
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <epd_driver.h>
#include <esp_system.h>
#include <hid_keys.h>
#include <trmnl_log.h>
#include <usb/hid_host.h>
#include <usb/usb_host.h>
#include <usb_mouse.h>
#include <usb_probe.h>

#include "app_host.h"
#include "evlog.h"
#include "keyboards.h"
#include "usb_dev_log.h"
#include "usb_pen_x.h"
#include "usb_port_role.h"

#define IOX_CHG_PG       0 // input, low: the charger has input power (VBUS from a charger or computer)
#define IOX_OTG_EN       1 // output, high: the charger's boost drives VBUS
#define STOP_DEADLINE_MS 1000 // the USB stack must be down by then, else a restart
#define BOOST_OFF_WAIT_MS 8000 // the boost goes off after the refresh holding the expander (seconds), else a restart

static TaskHandle_t port_task;
static volatile bool wanted, pen_wanted, stop_task; // wanted: USB keys; pen_wanted: the pen (usb_pen_x.h)
static volatile bool keyboard_open;
static QueueHandle_t conn_q, report_q;
static hid_host_device_handle_t open_dev;
static hid_keys_t keys;

// A non-keyboard HID interface: opened in report protocol. An unknown one is only logged (usb_probe.h); a pen tablet's
// (usb_pen.h role STANDARD or FULL) reports go to usb_pen_x.h
#define PROBE_SLOTS 4
typedef struct {
  hid_host_device_handle_t h; // nullptr: free
  uint8_t iface;
  usb_pen_iface_t pen; // USB_PEN_IFACE_NONE: the probe or a mouse
  bool mouse; // a mouse: its reports draw as the pen (usb_pen_x.h)
  usb_mouse_layout_t ml; // the mouse's report layout
  usb_probe_t pr;
} probe_slot_t;
static uint8_t pen_mode_logged; // APP_PEN_MODE_* already in the event log for the tablet now on
static probe_slot_t probes[PROBE_SLOTS];

static bool mouse_open(void) {
  for (const probe_slot_t &ps : probes) {
    if (ps.h && ps.mouse) return true;
  }
  return false;
}

typedef struct {
  uint8_t len;
  uint8_t data[USB_PROBE_REPORT_BYTES];
  bool gone;
  uint8_t probe; // 0: the keyboard, else probe slot + 1
  uint32_t seq; // probe: which report of the interface
} kbd_report_t;

// ---- the expander, shared with the panel driver ----

static bool expander_lock(uint32_t ms) { return !app_host.panel || xSemaphoreTake(app_host.panel, pdMS_TO_TICKS(ms)); }
static void expander_unlock(void) {
  if (app_host.panel) xSemaphoreGive(app_host.panel);
}

static usb_power_t read_power(void) {
  static usb_power_reading_t reading = {USB_POWER_UNKNOWN, 0};
  bool got = expander_lock(0), input_power = false;
  if (got) {
    bbep.ioPinMode(IOX_CHG_PG, INPUT);
    input_power = bbep.ioRead(IOX_CHG_PG) == 0;
    expander_unlock();
  }
  return usb_power_update(&reading, millis(), got, input_power);
}

static bool boost(bool on, uint32_t wait_ms) {
  if (!expander_lock(wait_ms)) return false;
  bbep.ioPinMode(IOX_OTG_EN, OUTPUT);
  bbep.ioWrite(IOX_OTG_EN, on ? HIGH : LOW);
  expander_unlock();
  return true;
}

// ---- USB host: install, run, stop ----

static void iface_cb(hid_host_device_handle_t h, const hid_host_interface_event_t ev, void *arg) {
  probe_slot_t *ps = (probe_slot_t *)arg; // nullptr: the keyboard
  kbd_report_t r = {};
  r.probe = ps ? (uint8_t)(ps - probes + 1) : 0;
  switch (ev) {
  case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
    uint8_t d[64];
    size_t n = 0;
    if (hid_host_device_get_raw_input_report_data(h, d, sizeof(d), &n) != ESP_OK) break;
    bool pen = ps && ps->pen != USB_PEN_IFACE_NONE;
    if (ps && ps->mouse) {
      usb_pen_x_mouse_report(&ps->ml, d, n);
      pen = true; // counted and logged like a tablet's
    }
    if (ps && ps->pen != USB_PEN_IFACE_NONE) usb_pen_x_report(ps->pen, d, n); // parsed here: 200 reports a second, not through the port task
    r.len = (uint8_t)(n < sizeof(r.data) ? n : sizeof(r.data));
    memcpy(r.data, d, r.len);
    if (ps) {
      r.seq = ps->pr.total + 1;
      // counted; past the first ones not queued (a tablet's first ones are logged too: which interface sends what)
      if (!usb_probe_report_upto(&ps->pr, pen ? USB_PROBE_PEN_REPORTS : USB_PROBE_FULL_REPORTS)) break;
    }
    xQueueSend(report_q, &r, 0); // a full queue drops it; never wait in the driver's task
    break;
  }
  case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
    hid_host_device_close(h);
    if (ps) {
      if (ps->h == h) ps->h = nullptr;
    } else {
      if (open_dev == h) open_dev = nullptr;
      keyboard_open = false;
    }
    r.gone = true;
    xQueueSend(report_q, &r, 0);
    break;
  default:
    break;
  }
}

static void driver_cb(hid_host_device_handle_t h, const hid_host_driver_event_t ev, void *arg) {
  (void)arg;
  if (ev == HID_HOST_DRIVER_EVENT_CONNECTED) xQueueSend(conn_q, &h, 0);
}

static volatile TaskHandle_t lib_task;
static volatile bool lib_stop;

// The USB host library's events; on lib_stop frees the devices, uninstalls the library and ends
static void lib_loop(void *arg) {
  (void)arg;
  bool freeing = false;
  while (true) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(50), &flags);
    if (freeing && (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)) break;
    if (lib_stop && !freeing) {
      if (usb_host_device_free_all() == ESP_OK) break; // none left to free
      freeing = true; // ESP_ERR_NOT_FINISHED: wait for ALL_FREE
    }
  }
  usb_host_uninstall();
  lib_task = nullptr;
  vTaskDelete(nullptr);
}

static bool host_start(void) {
  if (!usb_port_take(USB_ROLE_HOST)) return false; // the PHY: usb_port_role.cpp, not the host library
  const usb_host_config_t hc = {.skip_phy_setup = true, .intr_flags = ESP_INTR_FLAG_LEVEL1};
  esp_err_t rc = usb_host_install(&hc);
  if (rc != ESP_OK) {
    Log_error("USB keys: host install failed: %s", esp_err_to_name(rc));
    usb_port_give(USB_ROLE_HOST);
    return false;
  }
  lib_stop = false;
  if (xTaskCreatePinnedToCore(lib_loop, "usb_lib", 4096, nullptr, 10, (TaskHandle_t *)&lib_task, 0) != pdPASS) {
    usb_host_uninstall();
    usb_port_give(USB_ROLE_HOST);
    return false;
  }
  usb_dev_log_start(); // before any device enumerates (the root port's debounce is 250 ms)
  hid_host_driver_config_t dc = {};
  dc.create_background_task = true;
  dc.task_priority = 5;
  dc.stack_size = 4096;
  dc.core_id = 0;
  dc.callback = driver_cb;
  rc = hid_host_install(&dc);
  if (rc != ESP_OK) Log_error("USB keys: HID driver install failed: %s", esp_err_to_name(rc));
  return true;
}

// A tablet interface's report count at removal: which interface sent at all
static void log_pen_gone(const probe_slot_t &ps) {
  evlog_add("Pen i%u gone after %lu reports", ps.iface, (unsigned long)ps.pr.total);
}

// Stops the USB stack; false if it did not stop by `deadline`
static bool host_stop(uint32_t deadline) {
  if (open_dev) {
    hid_host_device_close(open_dev);
    open_dev = nullptr;
  }
  keyboard_open = false;
  bool had_pen = false;
  bool had_mouse = mouse_open();
  for (probe_slot_t &ps : probes) {
    if (ps.h) hid_host_device_close(ps.h);
    if (ps.h && ps.pen != USB_PEN_IFACE_NONE) {
      had_pen = true;
      log_pen_gone(ps);
    }
    ps.h = nullptr;
  }
  if (had_pen) usb_pen_x_detach();
  if (had_mouse) usb_pen_x_mouse_detach();
  while (hid_host_uninstall() != ESP_OK) { // a device still listed: its removal (boost off) takes a moment
    if ((int32_t)(millis() - deadline) >= 0) return false;
    delay(20);
  }
  usb_dev_log_stop(); // every client off before the library's uninstall
  lib_stop = true;
  while (lib_task) {
    if ((int32_t)(millis() - deadline) >= 0) return false;
    delay(10);
  }
  usb_port_give(USB_ROLE_HOST);
  return true;
}

// Logs the device and the interface with its report descriptor in hex (the probe: what does an unknown device send?)
static void log_interface(hid_host_device_handle_t h, const hid_host_dev_params_t &p) {
  hid_host_dev_info_t info;
  bool got = hid_host_get_device_info(h, &info) == ESP_OK;
  evlog_add("HID i%u sc%u p%u %04x:%04x", p.iface_num, p.sub_class, p.proto, got ? info.VID : 0, got ? info.PID : 0);
  size_t len = 0;
  const uint8_t *d = hid_host_get_report_descriptor(h, &len);
  if (!d || !len) {
    evlog_add("HID i%u: no report descriptor", p.iface_num);
    return;
  }
  size_t lines = usb_probe_desc_lines(len);
  for (size_t l = 0; l < lines; l++) {
    size_t off = 0, n = usb_probe_desc_line(len, l, &off);
    char hex[USB_PROBE_HEX_CAP];
    usb_probe_hex(hex, sizeof(hex), d + off, n);
    evlog_add("HID i%u d%u/%u @%u: %s", p.iface_num, (unsigned)(l + 1), (unsigned)lines, (unsigned)off, hex);
  }
  if (len > USB_PROBE_DESC_MAX) evlog_add("HID i%u descriptor %u bytes, first %u shown", p.iface_num, (unsigned)len, USB_PROBE_DESC_MAX);
}

static bool pen_open(void) {
  for (const probe_slot_t &ps : probes) {
    if (ps.h && ps.pen != USB_PEN_IFACE_NONE) return true;
  }
  return false;
}

// A connected HID interface: a pen tablet's (usb_pen.h) to the pen or to nobody, never to the keyboard; a boot keyboard
// is opened as the keyboard (none yet); any other interface is opened in report protocol for the probe. Every
// interface opened is logged.
static void open_interface(hid_host_device_handle_t h) {
  hid_host_dev_params_t p;
  if (hid_host_device_get_params(h, &p) != ESP_OK) return;
  hid_host_dev_info_t info;
  bool got_info = hid_host_get_device_info(h, &info) == ESP_OK;
  usb_pen_iface_t pen = got_info ? usb_pen_iface_role(info.VID, info.PID, p.iface_num) : USB_PEN_IFACE_NONE;
  if (pen == USB_PEN_IFACE_IGNORE) {
    evlog_add("Pen %04x:%04x i%u: not used", info.VID, info.PID, p.iface_num);
    return;
  }
  bool as_kbd = pen == USB_PEN_IFACE_NONE && p.sub_class == HID_SUBCLASS_BOOT_INTERFACE &&
                p.proto == HID_PROTOCOL_KEYBOARD && !open_dev;
  int switched = -1; // full mode: the interrupt OUT transfer's result, before the HID driver claims the interface
  if (pen == USB_PEN_IFACE_FULL) switched = usb_pen_x_full_mode(p.addr, p.iface_num);
  probe_slot_t *ps = nullptr;
  if (!as_kbd) {
    for (probe_slot_t &s : probes) {
      if (!s.h) {
        ps = &s;
        break;
      }
    }
    if (!ps) {
      evlog_add("HID i%u: no free probe slot", p.iface_num);
      return;
    }
  }
  const hid_host_device_config_t dc = {.callback = iface_cb, .callback_arg = ps};
  esp_err_t rc = hid_host_device_open(h, &dc);
  if (rc != ESP_OK) {
    evlog_add("HID i%u: open failed: %s", p.iface_num, esp_err_to_name(rc));
    return;
  }
  log_interface(h, p);
  if (as_kbd) {
    open_dev = h;
    hid_class_request_set_protocol(h, HID_REPORT_PROTOCOL_BOOT); // many start in it already: not fatal
  } else {
    ps->iface = p.iface_num;
    ps->pen = pen;
    ps->mouse = false;
    if (pen == USB_PEN_IFACE_NONE) { // a mouse: a boot one in boot protocol, else one whose descriptor holds a mouse
      size_t dlen = 0;
      const uint8_t *desc = hid_host_get_report_descriptor(h, &dlen);
      if (p.sub_class == HID_SUBCLASS_BOOT_INTERFACE && p.proto == HID_PROTOCOL_MOUSE) {
        usb_mouse_boot_layout(&ps->ml);
        ps->mouse = true;
      } else if (desc && usb_mouse_map_parse(desc, dlen, &ps->ml)) {
        ps->mouse = true;
      }
    }
    usb_probe_init(&ps->pr, millis());
    hid_class_request_set_protocol(h, ps->mouse && ps->ml.report_id == 0 && p.sub_class == HID_SUBCLASS_BOOT_INTERFACE
                                          ? HID_REPORT_PROTOCOL_BOOT
                                          : HID_REPORT_PROTOCOL_REPORT); // not fatal
    if (ps->mouse) {
      evlog_add("Mouse %04x:%04x i%u: %s", got_info ? info.VID : 0, got_info ? info.PID : 0, p.iface_num,
                p.sub_class == HID_SUBCLASS_BOOT_INTERFACE ? "boot protocol" : "report protocol");
      usb_pen_x_mouse_attach(got_info ? info.VID : 0, got_info ? info.PID : 0);
    }
    if (pen != USB_PEN_IFACE_NONE) {
      if (!pen_open()) pen_mode_logged = APP_PEN_MODE_NONE;
      usb_pen_x_attach(info.VID, info.PID);
    }
    ps->h = h;
  }
  if (pen == USB_PEN_IFACE_FULL) {
    if (switched != ESP_OK) { // once more as SET_REPORT (output, report 02); no string descriptors (they stalled it)
      uint8_t cmd[USB_PEN_CMD_LEN];
      memcpy(cmd, usb_pen_full_mode_cmd, sizeof(cmd));
      esp_err_t rc2 = hid_class_request_set_report(h, HID_REPORT_TYPE_OUTPUT, cmd[0], cmd, sizeof(cmd));
      evlog_add("Pen %04x:%04x: full mode by OUT failed (%s), SET_REPORT %s", info.VID, info.PID,
                esp_err_to_name((esp_err_t)switched), esp_err_to_name(rc2));
    } else {
      evlog_add("Pen %04x:%04x: full mode asked", info.VID, info.PID);
    }
  }
  hid_class_request_set_idle(h, 0, 0);
  rc = hid_host_device_start(h);
  if (rc != ESP_OK) {
    evlog_add("HID i%u: start failed: %s", p.iface_num, esp_err_to_name(rc));
    hid_host_device_close(h);
    if (ps) {
      ps->h = nullptr;
      if (pen != USB_PEN_IFACE_NONE && !pen_open()) usb_pen_x_detach();
      if (ps->mouse) {
        ps->mouse = false;
        if (!mouse_open()) usb_pen_x_mouse_detach();
      }
    } else {
      open_dev = nullptr;
    }
    return;
  }
  if (!as_kbd) return;
  hid_keys_init(&keys);
  keyboard_open = true;
  Log_event("USB keys: keyboard connected");
}

static void drain_reports(void) {
  kbd_report_t r;
  app_event_t ev[HID_KEYS_MAX_EVENTS];
  for (probe_slot_t &ps : probes) {
    uint32_t n;
    if (ps.h && ps.pen == USB_PEN_IFACE_NONE && !ps.mouse && usb_probe_summary(&ps.pr, millis(), &n)) // a tablet's: at removal
      evlog_add("HID i%u: %lu reports", ps.iface, (unsigned long)n);
  }
  uint8_t mode = usb_pen_x_mode();
  if (mode != pen_mode_logged && pen_open()) { // the mode the tablet's data came in
    pen_mode_logged = mode;
    app_pen_info_t info;
    usb_pen_x_info(&info);
    evlog_add("Pen %04x:%04x: %s mode", info.vid, info.pid, mode == APP_PEN_MODE_FULL ? "full" : "standard");
  }
  while (xQueueReceive(report_q, &r, 0) == pdTRUE) {
    if (r.probe) {
      probe_slot_t &ps = probes[r.probe - 1];
      if (r.gone && ps.pen != USB_PEN_IFACE_NONE) {
        log_pen_gone(ps);
        if (!pen_open()) usb_pen_x_detach(); // its last interface
      } else if (r.gone && ps.mouse) {
        evlog_add("Mouse i%u gone after %lu reports", ps.iface, (unsigned long)ps.pr.total);
        ps.mouse = false;
        if (!mouse_open()) usb_pen_x_mouse_detach();
      } else if (r.gone) {
        evlog_add("HID i%u gone after %lu reports", ps.iface, (unsigned long)ps.pr.total);
      } else {
        char hex[USB_PROBE_REPORT_BYTES * 3];
        usb_probe_hex(hex, sizeof(hex), r.data, r.len);
        evlog_add("HID i%u #%lu: %s", ps.iface, (unsigned long)r.seq, hex);
      }
      continue;
    }
    if (r.gone) {
      keyboards_post(ev, hid_keys_release_all(&keys, ev)); // no key stays down
      Log_event("USB keys: keyboard removed");
      keyboards_post_change();
      continue;
    }
    keyboards_post(ev, hid_keys_report(&keys, r.data, r.len, ev));
  }
}

// ---- the port task ----

static void port_loop(void *arg) {
  (void)arg;
  usb_port_t port;
  usb_port_init(&port);
  usb_port_mode_t applied = USB_PORT_DEVICE;
  usb_port_why_t why = port.why;
  bool was_open = false;
  usb_boost_t bst;
  usb_boost_init(&bst, millis());
  bst.on = true; // device mode: nothing to switch on
  usb_power_t last_power = USB_POWER_UNKNOWN;
  while (true) {
    hid_host_device_handle_t h;
    if (xQueueReceive(conn_q, &h, pdMS_TO_TICKS(USB_PORT_POLL_MS)) == pdTRUE && applied == USB_PORT_HOST) {
      open_interface(h);
    }
    drain_reports();
    if (applied == USB_PORT_HOST) {
      char vbus[48];
      snprintf(vbus, sizeof(vbus), "boost %s, input power %s", bst.on && port.boost ? "on" : "off",
               last_power == USB_POWER_YES ? "yes" : last_power == USB_POWER_NO ? "no" : "unknown");
      usb_dev_log_poll(vbus);
    }
    if (keyboard_open != was_open) {
      was_open = keyboard_open;
      if (was_open) keyboards_post_change();
    }
    last_power = read_power();
    usb_port_input_t in = {(wanted || pen_wanted) && !stop_task, last_power,
                           keyboard_open || (pen_wanted && (pen_open() || mouse_open())), usb_port_role() == USB_ROLE_DRIVE};
    usb_port_mode_t mode = usb_port_step(&port, millis(), &in);
    if (port.why != why) {
      why = port.why;
      Log_info("USB keys: %s", usb_port_why_str(why));
      // not every probe (one per retry_ms without a keyboard): an unreadable charger and the outside power steps
      if (why == USB_WHY_POWER_UNKNOWN || why == USB_WHY_OUTSIDE_PROBING || why == USB_WHY_OUTSIDE_DEVICE ||
          why == USB_WHY_OUTSIDE_NONE)
        evlog_add("USB keys: %s", usb_port_why_str(why));
    }
    if (mode == USB_PORT_HOST && applied == USB_PORT_DEVICE) {
      if (host_start()) {
        applied = USB_PORT_HOST;
        usb_boost_init(&bst, millis());
        bst.on = !port.boost; // outside power: the boost stays off, nothing to switch on (usb_port.h)
      }
    } else if (mode == USB_PORT_DEVICE && applied == USB_PORT_HOST) {
      if (was_open) evlog_add("USB keys: host mode dropped with a keyboard on: %s", usb_port_why_str(why));
      if (!boost(false, BOOST_OFF_WAIT_MS)) {
        Log_error("USB keys: boost could not be switched off - restarting");
        evlog_add("USB keys: boost could not be switched off, restarting");
        evlog_flush(false);
        esp_restart(); // the restart's handler and the boot switch it off
      }
      if (!host_stop(millis() + STOP_DEADLINE_MS)) {
        Log_error("USB keys: USB host did not stop within %d ms - restarting", STOP_DEADLINE_MS);
        evlog_add("USB keys: USB host did not stop within %d ms, restarting", STOP_DEADLINE_MS);
        evlog_flush(false);
        esp_restart();
      }
      drain_reports();
      if (was_open) {
        app_event_t ev[HID_KEYS_MAX_EVENTS];
        keyboards_post(ev, hid_keys_release_all(&keys, ev));
        was_open = false;
        keyboards_post_change();
      }
      applied = USB_PORT_DEVICE;
    }
    if (applied == USB_PORT_HOST && port.boost && usb_boost_due(&bst, millis())) { // retried while off (usb_port.h)
      usb_boost_log_t l = usb_boost_result(&bst, millis(), boost(true, 200));
      if (l == USB_BOOST_LOG_FAILED) {
        Log_error("USB keys: boost not switched on (expander busy), trying every %d ms", USB_BOOST_SLOW_MS);
        evlog_add("USB keys: boost not switched on after %d tries (expander busy), trying on", USB_BOOST_FAST_TRIES);
      } else if (l == USB_BOOST_LOG_ON_LATE) {
        evlog_add("USB keys: boost on after %u failed tries", bst.tries);
      }
    }
    if (stop_task && applied == USB_PORT_DEVICE) break;
  }
  port_task = nullptr;
  vTaskDelete(nullptr);
}

// Starts the port task when keys or the pen are wanted
static int port_start(void) {
  if (!(wanted || pen_wanted) || port_task) return 0;
  if (!conn_q) conn_q = xQueueCreate(4, sizeof(hid_host_device_handle_t));
  if (!report_q) report_q = xQueueCreate(48, sizeof(kbd_report_t));
  if (!conn_q || !report_q) return APP_ENOMEM;
  stop_task = false;
  if (xTaskCreatePinnedToCore(port_loop, "usb_port", 4096, nullptr, 3, &port_task, 0) != pdPASS) return APP_ENOMEM;
  return 0;
}

int usb_keyboard_enable(bool on) {
  // A restart: usb_port_role.cpp's handler gives the PHY back (no I2C there: the boot switches the boost off)
  wanted = on;
  return port_start();
}

int usb_keyboard_pen(bool on) {
  pen_wanted = on;
  return port_start();
}

bool usb_keyboard_connected(void) { return keyboard_open; }

void usb_keyboard_end(void) {
  wanted = false;
  pen_wanted = false;
  stop_task = true;
  uint32_t start = millis();
  while (port_task && millis() - start < 3 * STOP_DEADLINE_MS) delay(10);
  if (port_task) {
    Log_error("USB keys: port task did not end - restarting");
    evlog_add("USB keys: port task did not end, restarting");
    evlog_flush(false);
    esp_restart();
  }
}

#endif // BOARD_TRMNL_X
