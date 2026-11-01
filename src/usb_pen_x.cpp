// A USB pen tablet for apps (usb_pen_x.h): the X's side of lib/trmnl usb_pen.h
#ifdef BOARD_TRMNL_X

#include "usb_dev_log.h"
#include "usb_pen_x.h"
#include "x_devset_x.h"

#include <Arduino.h>
#include <string.h>
#include <usb/usb_helpers.h>
#include <usb/usb_host.h>

#include "app_host.h"
#include "app_idle_x.h"
#include "evlog.h"

int usb_keyboard_pen(bool on); // usb_keyboard.cpp: the port in host mode for the pen as for keys

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static usb_pen_t pen; // under mux
static usb_pen_queue_t queue; // under mux
static usb_cursor_t cursor; // under mux: who owns the cursor (lib/trmnl usb_cursor.h)
static bool present; // under mux: a tablet is attached
static bool mouse_present; // under mux
static uint16_t mouse_vid, mouse_pid; // under mux
static bool cursor_ready; // under mux: cursor initialised
// the X's panel (docs/spec/usb-display.md): the mouse moves the same distance on screen in x and y; micrometres only
// give pen_info the aspect
#define PANEL_W    1872
#define PANEL_H    1404
#define PANEL_UM_W 209664
#define PANEL_UM_H 157248
static volatile bool wanted;
static void flush(const usb_cursor_out_t &o); // under mux: the cursor's events into the queue

static volatile bool token_pending; // an APP_EVENT_PEN token sits in the app's queue

// Puts the token in the app's queue unless one is there (any task)
static void post_token(void) {
  if (token_pending || !app_host.events) return;
  app_event_t tok = {};
  tok.type = APP_EVENT_PEN;
  token_pending = true;
  if (xQueueSend(app_host.events, &tok, 0) != pdTRUE) token_pending = false; // full: the next report tries again
}

static void cursor_check(void) { // under mux
  if (!cursor_ready) {
    usb_cursor_init(&cursor, PANEL_W, PANEL_H);
    cursor_ready = true;
  }
}

static void flush(const usb_cursor_out_t &o) {
  if (!wanted) return;
  for (uint8_t i = 0; i < o.n; i++) usb_pen_queue_push(&queue, &o.ev[i]);
}

int usb_pen_x_enable(bool on) {
  usb_cursor_out_t o;
  taskENTER_CRITICAL(&mux);
  cursor_check();
  usb_pen_queue_init(&queue);
  usb_cursor_resync(&cursor, &o); // a mouse already there (the pen not in range): in range at once
  wanted = on;
  flush(o);
  taskEXIT_CRITICAL(&mux);
  if (on && o.n) post_token();
  return usb_keyboard_pen(on);
}

bool usb_pen_x_wanted(void) { return wanted; }

int usb_pen_x_info(app_pen_info_t *out) {
  if (!out) return APP_EINVAL;
  taskENTER_CRITICAL(&mux);
  if (present || !mouse_present) usb_pen_info(present ? &pen : nullptr, out);
  else usb_mouse_info(mouse_vid, mouse_pid, PANEL_UM_W, PANEL_UM_H, out);
  if (mouse_present) out->flags |= APP_PEN_INFO_MOUSE;
  out->dropped = queue.dropped;
  taskEXIT_CRITICAL(&mux);
  return 0;
}

bool usb_pen_x_take(app_event_t *ev) {
  app_any_event_t u = {};
  taskENTER_CRITICAL(&mux);
  bool got = usb_pen_queue_take(&queue, &u.pen);
  bool more = queue.count != 0;
  taskEXIT_CRITICAL(&mux);
  if (got) *ev = u.ev;
  if (more) post_token(); // sleep_ms and a waiting input_poll wake for the rest
  return got;
}

void usb_pen_x_token_taken(void) { token_pending = false; }

bool usb_pen_x_waiting(void) {
  taskENTER_CRITICAL(&mux);
  bool w = queue.count != 0;
  taskEXIT_CRITICAL(&mux);
  return w;
}

void usb_pen_x_attach(uint16_t vid, uint16_t pid) {
  usb_dev_log_kind(vid, pid, "pen tablet");
  taskENTER_CRITICAL(&mux);
  cursor_check();
  if (!present || pen.vid != vid || pen.pid != pid) usb_pen_init(&pen, vid, pid);
  present = true;
  usb_cursor_tablet_attach(&cursor); // the mouse keeps the cursor until the pen comes in range
  taskEXIT_CRITICAL(&mux);
}

void usb_pen_x_detach(void) {
  usb_cursor_out_t o;
  taskENTER_CRITICAL(&mux);
  cursor_check();
  usb_pen_t was = pen;
  bool had = present;
  present = false;
  usb_cursor_tablet_detach(&cursor, &o); // the pen leaves; a mouse goes on from its place
  bool keys = had && pen.keys; // nothing stays down
  if (had) flush(o);
  if (keys) {
    app_pen_event_t up = {};
    up.type = APP_EVENT_PEN_KEY;
    up.x = pen.x;
    up.y = pen.y;
    up.changed = pen.keys;
    if (wanted) usb_pen_queue_push(&queue, &up);
  }
  taskEXIT_CRITICAL(&mux);
  if (((had && o.n) || keys) && wanted) post_token();
  // the largest raw values seen: checks the assumed full-mode maxima (usb_pen.h)
  if (had)
    evlog_add("Pen %04x:%04x gone, %lu pen frames, raw max x %u y %u p %u", was.vid, was.pid,
              (unsigned long)was.pen_frames, was.raw_max_x, was.raw_max_y, was.raw_max_p);
}

void usb_pen_x_report(usb_pen_iface_t role, const uint8_t *d, size_t len) {
  app_idle_x_note(); // any report, hover and tablet buttons too, is a person (also when the app asked for none)
  app_pen_event_t e;
  usb_cursor_out_t o = {};
  taskENTER_CRITICAL(&mux);
  cursor_check();
  int n = usb_pen_report(&pen, role, d, len, &e);
  if (n && e.type == APP_EVENT_PEN) usb_cursor_pen(&cursor, &e, &o); // pen events may be held back for the mouse
  else if (n) {
    o.ev[0] = e; // tablet buttons pass as they are
    o.n = 1;
  }
  flush(o);
  taskEXIT_CRITICAL(&mux);
  if (o.n && wanted) {
    app_host.got_input = true;
    post_token();
  }
}

void usb_pen_x_mouse_speed_reload(void) {
  char speed[8] = "";
  if (x_devset_get("mouse_speed", speed, sizeof(speed)) < 0) speed[0] = '\0'; // NVS: outside the critical section
  uint8_t q = usb_mouse_speed(speed);
  taskENTER_CRITICAL(&mux);
  cursor_check();
  usb_mouse_set_speed(&cursor.cur, q);
  taskEXIT_CRITICAL(&mux);
}

void usb_pen_x_mouse_attach(uint16_t vid, uint16_t pid) {
  usb_dev_log_kind(vid, pid, "mouse");
  usb_pen_x_mouse_speed_reload();
  usb_cursor_out_t o;
  taskENTER_CRITICAL(&mux);
  cursor_check();
  mouse_present = true;
  mouse_vid = vid;
  mouse_pid = pid;
  usb_cursor_mouse_attach(&cursor, &o);
  flush(o);
  taskEXIT_CRITICAL(&mux);
  if (o.n && wanted) post_token();
}

void usb_pen_x_mouse_detach(void) {
  usb_cursor_out_t o;
  taskENTER_CRITICAL(&mux);
  cursor_check();
  mouse_present = false;
  usb_cursor_mouse_detach(&cursor, &o); // nothing stays down
  flush(o);
  taskEXIT_CRITICAL(&mux);
  if (o.n && wanted) post_token();
}

void usb_pen_x_mouse_report(const usb_mouse_layout_t *l, const uint8_t *d, size_t len) {
  usb_mouse_move_t mv;
  app_idle_x_note(); // a mouse report is a person, also for an app that did not ask for the mouse
  if (!wanted || !usb_mouse_report(l, d, len, &mv)) return; // no mouse events for an app that did not ask
  usb_cursor_out_t o;
  taskENTER_CRITICAL(&mux);
  cursor_check();
  usb_cursor_mouse_move(&cursor, &mv, &o); // ignored while the pen is in range
  flush(o);
  taskEXIT_CRITICAL(&mux);
  if (o.n && wanted) {
    app_host.got_input = true;
    post_token();
  }
}

uint8_t usb_pen_x_mode(void) {
  taskENTER_CRITICAL(&mux);
  uint8_t m = present ? pen.mode : (uint8_t)APP_PEN_MODE_NONE;
  taskEXIT_CRITICAL(&mux);
  return m;
}

// ---- the full-mode switch: our own USB host client, for the one interrupt OUT transfer ----

static volatile bool out_done;
static void out_cb(usb_transfer_t *t) {
  (void)t;
  out_done = true;
}
static void client_cb(const usb_host_client_event_msg_t *msg, void *arg) {
  (void)msg;
  (void)arg;
}

// The interrupt OUT endpoint of interface `iface`, or 0
static uint8_t out_endpoint(usb_device_handle_t dev, uint8_t iface) {
  const usb_config_desc_t *cfg = nullptr;
  if (usb_host_get_active_config_descriptor(dev, &cfg) != ESP_OK || !cfg) return 0;
  int off = 0;
  const usb_intf_desc_t *in = usb_parse_interface_descriptor(cfg, iface, 0, &off);
  if (!in) return 0;
  for (int i = 0; i < in->bNumEndpoints; i++) {
    int o = off;
    const usb_ep_desc_t *ep = usb_parse_endpoint_descriptor_by_index(in, i, cfg->wTotalLength, &o);
    if (ep && !(ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) &&
        (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT)
      return ep->bEndpointAddress;
  }
  return 0;
}

static void pump(usb_host_client_handle_t client, uint32_t until_ms) {
  while (!out_done && (int32_t)(millis() - until_ms) < 0) usb_host_client_handle_events(client, pdMS_TO_TICKS(10));
}

int usb_pen_x_full_mode(uint8_t addr, uint8_t iface) {
  usb_host_client_config_t cc = {};
  cc.is_synchronous = false;
  cc.max_num_event_msg = 4;
  cc.async.client_event_callback = client_cb;
  usb_host_client_handle_t client = nullptr;
  esp_err_t rc = usb_host_client_register(&cc, &client);
  if (rc != ESP_OK) return rc;
  usb_device_handle_t dev = nullptr;
  usb_transfer_t *t = nullptr;
  bool claimed = false;
  uint8_t ep = 0;
  rc = usb_host_device_open(client, addr, &dev);
  if (rc == ESP_OK) {
    ep = out_endpoint(dev, iface);
    if (!ep) rc = ESP_ERR_NOT_FOUND;
  }
  if (rc == ESP_OK) {
    rc = usb_host_interface_claim(client, dev, iface, 0);
    claimed = rc == ESP_OK;
  }
  if (rc == ESP_OK) rc = usb_host_transfer_alloc(USB_PEN_CMD_LEN, 0, &t);
  if (rc == ESP_OK) {
    memcpy(t->data_buffer, usb_pen_full_mode_cmd, USB_PEN_CMD_LEN);
    t->num_bytes = USB_PEN_CMD_LEN;
    t->device_handle = dev;
    t->bEndpointAddress = ep;
    t->callback = out_cb;
    t->context = nullptr;
    t->timeout_ms = 500;
    out_done = false;
    rc = usb_host_transfer_submit(t);
    if (rc == ESP_OK) {
      uint32_t start = millis();
      pump(client, start + 500);
      if (!out_done) { // the tablet did not take it: cancel the transfer before freeing it
        usb_host_endpoint_halt(dev, ep);
        usb_host_endpoint_flush(dev, ep);
        pump(client, start + 1000);
        usb_host_endpoint_clear(dev, ep);
      }
      if (!out_done) t = nullptr; // still in flight: leaked rather than freed under the USB stack
      rc = out_done && t && t->status == USB_TRANSFER_STATUS_COMPLETED ? ESP_OK : ESP_ERR_TIMEOUT;
    }
  }
  if (t) usb_host_transfer_free(t);
  if (claimed) usb_host_interface_release(client, dev, iface);
  if (dev) usb_host_device_close(client, dev);
  uint32_t start = millis();
  while (usb_host_client_deregister(client) != ESP_OK && millis() - start < 500)
    usb_host_client_handle_events(client, pdMS_TO_TICKS(10));
  return rc;
}

#endif // BOARD_TRMNL_X
