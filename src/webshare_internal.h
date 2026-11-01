#pragma once

// Shared by the Wi-Fi page's firmware files: webshare.cpp (start, stop, files), webshare_async.cpp (AsyncWebServer
// on the S3's 2.4 GHz radio), webshare_modem.cpp (the ESP32-C5 modem's server on 5 GHz), webshare_env.cpp (what the
// handlers need from the device). The handlers themselves: lib/trmnl/ws_page_*.
#ifdef BOARD_TRMNL_X

#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ws_page.h>

#include "webshare.h"

// The page's state as the device sees it; WP (ws_page.h) holds the handlers' part
struct webshare_state_t {
  bool watch_alive;
  bool modem; // served through the modem (a saved 5 GHz network), not AsyncWebServer
  bool connected_before; // the X was on a network before the page started (then stop leaves it on)
  app_webshare_mode_t mode;
  SemaphoreHandle_t lock; // start/stop and the upload state; handlers run in the transport's task
  char network[33], address[40], ip[16], url[32];
};
extern webshare_state_t S;

extern const ws_env_t WEBSHARE_ENV; // webshare_env.cpp
extern const ws_portal_env_t WEBSHARE_PORTAL; // webshare_portal.cpp

// AsyncWebServer (webshare_async.cpp): the page's routes, and the access point's captive checks when `ap`
void webshare_async_routes(AsyncWebServer &server, bool ap, const char *ap_url);

// The modem's server (webshare_modem.cpp). Starts CIPMUX/CIPSERVER and announces `host` by mDNS if the AT firmware
// can; fills `ip` and `mdns_ok`. Its task runs until WP.running goes false, then closes everything itself.
int webshare_modem_start(const char *host, char *ip, size_t ip_len, bool *mdns_ok);
bool webshare_modem_alive(void); // the task has not finished its cleanup yet
// A link id the page's server does not hold (for the printer search beside it), -1 when the phone holds all. Only
// from the page's own task (a handler)
int webshare_modem_free_link(void);

// The page's Wi-Fi and Bluetooth (webshare_radio.cpp), for WEBSHARE_PORTAL
int webshare_radio_scan(wsw_list_t *out);
int webshare_radio_join(const wsw_join_t *j);
int webshare_radio_ble(ws_ble_do_t what, int index);
void webshare_radio_ble_info(ws_ble_info_t *out);

// Starts the page in a profile (webshare.cpp): the public starts are this
int webshare_start_profile(app_webshare_mode_t requested, int profile);

const char *webshare_files_page(void);
const char *webshare_install_page(void);
const char *webshare_settings_page(void);

#endif
