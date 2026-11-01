// The X's page, Wi-Fi and Bluetooth (lib/trmnl/ws_page_radio.cpp): the device side. Scan (the S3 on 2.4 GHz plus
// the modem's dual-band scan when its UART is free; through the server's framing when the modem serves the page),
// the network to try once the page has closed (apps_install_session runs wifi_switch.h), keyboard pairing with the
// BLE keyboard code. Radio rules: pairing is refused while ESP-NOW runs or an app's keyboard holds the radio; on the
// modem path Bluetooth has the S3's radio alone, on 2.4 GHz and the access point it shares it (coexistence).
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <WiFi.h>
#include <app_api.h>
#include <globals.h>
#include <modem.h>
#include <string.h>

#include "ble_keyboard.h"
#include "espnow_app.h"
#include "webshare_internal.h"

static wsw_join_t pending_join; // the page asked; taken by the session once the page has closed
static volatile bool join_pending;
static bool pairing; // the page started the radio (and stops it)

static void add_modem(const std::vector<Modem::ModemNetwork> &nets, wsw_list_t *out) {
  for (const auto &n : nets) wsw_list_add(out, n.ssid.c_str(), (int8_t)n.rssi, n.open, n.is5GHz, n.enterprise);
}

int webshare_radio_scan(wsw_list_t *out) {
  if (S.modem) { // the page's task owns the UART: the scan goes through the server's framing
    std::vector<Modem::ModemNetwork> nets;
    if (!g_modem || !g_modem->serverScan(nets)) return APP_EIO;
    add_modem(nets, out);
    return out->n;
  }
  int n = WiFi.scanNetworks(false, false); // blocks a few seconds; the web server's task waits
  for (int i = 0; i < n; i++) {
    wifi_auth_mode_t a = WiFi.encryptionType(i);
    wsw_list_add(out, WiFi.SSID(i).c_str(), (int8_t)WiFi.RSSI(i), a == WIFI_AUTH_OPEN, false, a == WIFI_AUTH_WPA2_ENTERPRISE);
  }
  WiFi.scanDelete();
  if (g_modem && g_modem->isInitialized()) add_modem(g_modem->scanNetworks(), out); // its UART is free: 5 GHz too
  return n < 0 && out->n == 0 ? APP_EIO : out->n;
}

int webshare_radio_join(const wsw_join_t *j) {
  if (j->is5 && !g_modem) return APP_ENOTSUP;
  pending_join = *j;
  join_pending = true;
  return 0;
}

bool webshare_portal_take_wifi(wsw_join_t *out) {
  if (!join_pending) return false;
  *out = pending_join;
  memset(&pending_join, 0, sizeof(pending_join));
  join_pending = false;
  return true;
}

int webshare_radio_ble(ws_ble_do_t what, int i) {
  if (what == WS_BLE_STOP) {
    if (pairing) ble_keyboard_end();
    pairing = false;
    return 0;
  }
  if (what == WS_BLE_PICK) {
    ble_pair_t p;
    ble_keyboard_pair_state(&p);
    if (!pairing || p.state != BLE_PAIR_LIST || i < 0 || i >= p.found_count) return APP_EINVAL;
    ble_keyboard_pick(i);
    return 0;
  }
  if (pairing) return 0; // already listing
  if (espnow_app_running() || ble_keyboard_radio_on()) return APP_EBUSY;
  int rc = ble_keyboard_start(true);
  pairing = rc == 0;
  return rc;
}

void webshare_radio_ble_info(ws_ble_info_t *b) {
  ble_pair_t p;
  ble_keyboard_pair_state(&p);
  memset(b, 0, sizeof(*b));
  if (!pairing) return;
  switch (p.state) {
  case BLE_PAIR_LIST: b->state = WS_BLE_LIST; break;
  case BLE_PAIR_CONNECTING: b->state = WS_BLE_CONNECTING; break;
  case BLE_PAIR_PASSKEY: b->state = WS_BLE_PASSKEY; break;
  case BLE_PAIR_CONNECTED: b->state = WS_BLE_PAIRED; break;
  default: b->state = WS_BLE_OFF; break;
  }
  for (int i = 0; i < p.found_count && i < WS_BLE_FOUND_MAX; i++) {
    snprintf(b->found[i].name, sizeof(b->found[i].name), "%s", p.found[i].name);
    b->found[i].rssi = p.found[i].rssi;
  }
  b->n = p.found_count < WS_BLE_FOUND_MAX ? p.found_count : WS_BLE_FOUND_MAX;
  b->passkey = p.passkey;
  if (p.message) snprintf(b->message, sizeof(b->message), "%s", p.message);
  if (b->state == WS_BLE_PAIRED) snprintf(b->paired, sizeof(b->paired), "%s", p.target.name);
}

void webshare_radio_end(void) { webshare_radio_ble(WS_BLE_STOP, 0); }

#endif // BOARD_TRMNL_X
