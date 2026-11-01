// What enumerated (usb_dev_log.h): an own USB host client opens each new device a moment for its descriptors
#ifdef BOARD_TRMNL_X

#include "usb_dev_log.h"

#include <Arduino.h>
#include <string.h>
#include <usb/usb_host.h>
#include <usb/usb_types_ch11.h>
#include <usb_probe.h>
#include <usb_seen.h>

#include "evlog.h"

#define PENDING 4
#define HUB_DESC_LEN 16 // the fixed 7 bytes and the port bitmaps of a hub up to 15 ports

static usb_host_client_handle_t client;
static RTC_DATA_ATTR usb_seen_t seen; // since the X last started (deep sleep keeps it), for the X's page

void usb_dev_log_kind(uint16_t vid, uint16_t pid, const char *kind) { usb_seen_add(&seen, vid, pid, 0, kind); }

void usb_dev_log_seen(usb_seen_t *out) {
  usb_seen_init(&seen);
  *out = seen;
}
static uint8_t pending[PENDING], n_pending;
static volatile bool ctrl_done;

static void client_cb(const usb_host_client_event_msg_t *msg, void *arg) {
  (void)arg;
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    if (n_pending < PENDING) pending[n_pending++] = msg->new_dev.address;
    else evlog_add("USB dev %u: not logged (busy)", msg->new_dev.address);
  }
}

static void ctrl_cb(usb_transfer_t *t) {
  (void)t;
  ctrl_done = true;
}

// The hub's port count by GET_DESCRIPTOR (hub class), -1 when it did not answer within 500 ms
static int hub_ports(usb_device_handle_t dev) {
  usb_transfer_t *t = nullptr;
  if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + HUB_DESC_LEN, 0, &t) != ESP_OK) return -1;
  usb_setup_packet_t *s = (usb_setup_packet_t *)t->data_buffer;
  s->bmRequestType = USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_HUB;
  s->bRequest = USB_B_REQUEST_GET_DESCRIPTOR;
  s->wValue = (USB_CLASS_DESCRIPTOR_TYPE_HUB << 8);
  s->wIndex = 0;
  s->wLength = HUB_DESC_LEN;
  t->num_bytes = sizeof(usb_setup_packet_t) + HUB_DESC_LEN;
  t->device_handle = dev;
  t->bEndpointAddress = 0;
  t->callback = ctrl_cb;
  t->context = nullptr;
  t->timeout_ms = 500;
  ctrl_done = false;
  int ports = -1;
  if (usb_host_transfer_submit_control(client, t) == ESP_OK) {
    uint32_t start = millis();
    while (!ctrl_done && millis() - start < 600) usb_host_client_handle_events(client, pdMS_TO_TICKS(10));
    if (!ctrl_done) return -1; // still in flight: leaked rather than freed under the USB stack
    if (t->status == USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes > (int)sizeof(usb_setup_packet_t))
      ports = usb_probe_hub_ports(t->data_buffer + sizeof(usb_setup_packet_t),
                                  (size_t)t->actual_num_bytes - sizeof(usb_setup_packet_t));
  }
  usb_host_transfer_free(t);
  return ports;
}

static void log_device(uint8_t addr, const char *vbus) {
  usb_device_handle_t dev = nullptr;
  esp_err_t rc = usb_host_device_open(client, addr, &dev);
  if (rc != ESP_OK) {
    evlog_add("USB dev %u: open failed: %s", addr, esp_err_to_name(rc));
    return;
  }
  const usb_device_desc_t *dd = nullptr;
  const usb_config_desc_t *cd = nullptr;
  usb_device_info_t info = {};
  bool got_dd = usb_host_get_device_descriptor(dev, &dd) == ESP_OK && dd;
  bool got_cd = usb_host_get_active_config_descriptor(dev, &cd) == ESP_OK && cd;
  bool got_info = usb_host_device_info(dev, &info) == ESP_OK;
  char where[24] = "on the X";
  if (got_info && info.parent.dev_hdl) snprintf(where, sizeof(where), "on hub port %u", info.parent.port_num);
  evlog_add("USB dev %04x:%04x class %u %s: %u mA asked, %s", got_dd ? dd->idVendor : 0, got_dd ? dd->idProduct : 0,
            got_dd ? dd->bDeviceClass : 0, where, got_cd ? usb_probe_max_ma(cd->bMaxPower) : 0, vbus);
  if (got_dd) usb_seen_add(&seen, dd->idVendor, dd->idProduct, dd->bDeviceClass, nullptr);
  if (got_dd && dd->bDeviceClass == USB_CLASS_HUB) {
    int ports = hub_ports(dev);
    if (ports >= 0) evlog_add("USB hub found, %d ports", ports);
    else evlog_add("USB hub found, port count unread");
  }
  usb_host_device_close(client, dev);
}

bool usb_dev_log_start(void) {
  if (client) return true;
  usb_host_client_config_t cc = {};
  cc.is_synchronous = false;
  cc.max_num_event_msg = 8;
  cc.async.client_event_callback = client_cb;
  n_pending = 0;
  esp_err_t rc = usb_host_client_register(&cc, &client);
  if (rc != ESP_OK) {
    client = nullptr;
    evlog_add("USB dev log: client failed: %s", esp_err_to_name(rc));
    return false;
  }
  return true;
}

void usb_dev_log_poll(const char *vbus) {
  if (!client) return;
  usb_host_client_handle_events(client, 0);
  while (n_pending) {
    uint8_t addr = pending[0];
    memmove(pending, pending + 1, --n_pending);
    log_device(addr, vbus);
  }
}

void usb_dev_log_stop(void) {
  if (!client) return;
  uint32_t start = millis();
  while (usb_host_client_deregister(client) != ESP_OK && millis() - start < 500)
    usb_host_client_handle_events(client, pdMS_TO_TICKS(10));
  client = nullptr;
  n_pending = 0;
}

#endif // BOARD_TRMNL_X
