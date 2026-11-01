// A USB pen tablet for apps (usb_pen_x.h): the X's side of lib/trmnl usb_pen.h
#ifdef BOARD_TRMNL_X

#include "usb_pen_x.h"

#include <Arduino.h>
#include <string.h>
#include <usb/usb_helpers.h>
#include <usb/usb_host.h>

#include "app_host.h"
#include "evlog.h"

int usb_keyboard_pen(bool on); // usb_keyboard.cpp: the port in host mode for the pen as for keys

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static usb_pen_t pen; // under mux
static usb_pen_queue_t queue; // under mux
static bool present; // under mux: a tablet is attached
static volatile bool wanted;
static volatile bool token_pending; // an APP_EVENT_PEN token sits in the app's queue

// Puts the token in the app's queue unless one is there (any task)
static void post_token(void) {
  if (token_pending || !app_host.events) return;
  app_event_t tok = {};
  tok.type = APP_EVENT_PEN;
  token_pending = true;
  if (xQueueSend(app_host.events, &tok, 0) != pdTRUE) token_pending = false; // full: the next report tries again
}

int usb_pen_x_enable(bool on) {
  taskENTER_CRITICAL(&mux);
  usb_pen_queue_init(&queue);
  taskEXIT_CRITICAL(&mux);
  wanted = on;
  return usb_keyboard_pen(on);
}

bool usb_pen_x_wanted(void) { return wanted; }

int usb_pen_x_info(app_pen_info_t *out) {
  if (!out) return APP_EINVAL;
  taskENTER_CRITICAL(&mux);
  usb_pen_info(present ? &pen : nullptr, out);
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
  taskENTER_CRITICAL(&mux);
  if (!present || pen.vid != vid || pen.pid != pid) usb_pen_init(&pen, vid, pid);
  present = true;
  taskEXIT_CRITICAL(&mux);
}

void usb_pen_x_detach(void) {
  taskENTER_CRITICAL(&mux);
  usb_pen_t was = pen;
  bool had = present;
  present = false;
  app_pen_event_t up = {};
  bool post = had && (pen.flags || pen.keys); // nothing stays down
  if (post) {
    up.type = APP_EVENT_PEN;
    up.x = pen.x;
    up.y = pen.y;
    up.changed = pen.flags;
    if (wanted) usb_pen_queue_push(&queue, &up);
    if (pen.keys) {
      up.type = APP_EVENT_PEN_KEY;
      up.changed = pen.keys;
      if (wanted) usb_pen_queue_push(&queue, &up);
    }
  }
  taskEXIT_CRITICAL(&mux);
  if (post && wanted) post_token();
  // the largest raw values seen: checks the assumed full-mode maxima (usb_pen.h)
  if (had) evlog_add("Pen %04x:%04x gone, raw max x %u y %u p %u", was.vid, was.pid, was.raw_max_x, was.raw_max_y, was.raw_max_p);
}

void usb_pen_x_report(usb_pen_iface_t role, const uint8_t *d, size_t len) {
  app_pen_event_t e;
  taskENTER_CRITICAL(&mux);
  int n = usb_pen_report(&pen, role, d, len, &e);
  if (n && wanted) usb_pen_queue_push(&queue, &e);
  taskEXIT_CRITICAL(&mux);
  if (n && wanted) {
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
