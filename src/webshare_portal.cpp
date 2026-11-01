// The X's page (docs/apps.md "The X's page"): what lib/trmnl/ws_page_portal.cpp needs from the device. Every app's
// settings and declaration (app_settings_store), the device settings (x_devset_x), printers (net_browse), the Bluetooth
// keyboard, the USB devices seen and the X's own facts.
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <WifiCaptive.h>
#include <apps.h>
#include <config.h>
#include <string.h>

#include "app_net_browse.h"
#include "app_settings_store.h"
#include "ble_keyboard.h"
#include "menu_apps.h"
#include "usb_dev_log.h"
#include "webshare.h"
#include "webshare_internal.h"
#include "x_devset_x.h"

#define PRINT_APP "trmnl-print" // its "test" start argument prints a test page on the default printer
#define PRINTERS_MODEM_MS 15000 // the whole /24 at nb_probe_ms each

static volatile bool test_pending;

static bool app_meta(const char *app, app_meta_t *out) { return menu_apps_meta(app, out); }
static int app_get(const char *app, const char *k, char *b, size_t n) { return app_store_get(app, k, b, n); }
static int app_set(const char *app, const char *k, const char *v, size_t n) { return app_store_set(app, k, v, n); }

static int dev_get(const char *key, char *buf, size_t cap) {
  int n = x_devset_get(key, buf, cap + 1); // the handler's buffer has room for the NUL (XDS_VALUE_MAX + 1)
  return n == APP_ENOENT ? 0 : n;
}

// 2.4 GHz: mDNS from the S3. Through the modem (it serves this page): the /24 probe on a link beside the server
// (Modem::serverProbeOpen), from this handler, which runs in the page's task (the UART's owner)
static int printers_find(nb_list_t *out) {
  if (S.modem) {
    int link = webshare_modem_free_link();
    if (link < 0) return APP_EBUSY; // the phone holds every link: try again
    nb_list_init(out);
    return app_net_browse_modem_beside(link, PRINTERS_MODEM_MS, out);
  }
  if (WiFi.status() != WL_CONNECTED) return APP_ENONET;
  nb_list_init(out);
  return app_net_browse_s3("_ipp", "_tcp", 3000, out); // short: the web server's task waits for it
}

static int printer_test(void) {
  char path[48];
  snprintf(path, sizeof(path), APPS_DIR "/" PRINT_APP ".elf");
  if (!LittleFS.exists(path)) return APP_ENOENT;
  test_pending = true;
  return 0;
}

bool webshare_portal_take_test(void) {
  bool t = test_pending;
  test_pending = false;
  return t;
}

static bool ble_bond(char *name, size_t cap) {
  ble_pair_peer_t peer;
  if (!ble_keyboard_bond(&peer)) return false;
  snprintf(name, cap, "%s", peer.name);
  return true;
}

static void x_info(ws_x_info_t *x) {
  snprintf(x->name, sizeof(x->name), "%s", webshare_device_name());
  snprintf(x->version, sizeof(x->version), "%s", FW_VERSION_STRING);
  if (WifiCaptivePortal.isSaved()) {
    WifiCredentials c = WifiCaptivePortal.getLastCredentials();
    snprintf(x->wifi, sizeof(x->wifi), "%s", c.ssid.c_str());
    x->wifi_5ghz = c.is5GHz;
  }
}

const ws_portal_env_t WEBSHARE_PORTAL = {
    app_meta,      app_store_decl_load, app_get,         app_set,     app_store_remove, app_store_changed, dev_get,
    x_devset_set,  printers_find,       printer_test,    ble_bond,    ble_keyboard_forget, usb_dev_log_seen,
    x_info,        webshare_radio_scan, webshare_radio_join, webshare_radio_ble, webshare_radio_ble_info,
};

#endif // BOARD_TRMNL_X
