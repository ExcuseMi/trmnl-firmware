// The X's page, Wi-Fi and Bluetooth (docs/apps.md "The X's page"): scan and join another network (This X), pair a
// keyboard (Devices). Part of the portal routes (ws_portal.h); transport-free. A password never goes back to the
// phone and never into the log; the join itself runs after the page has closed (wifi_switch.h).
#include <stdio.h>
#include <string.h>

#include "ws_page_json.h"

static const ws_portal_env_t *P(void) { return WP.env->portal; }
static wsw_list_t last_scan; // what Join is checked against

void ws_page_radio_reset(void) { wsw_list_init(&last_scan); }

static void handle_wifi_scan(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  wsw_list_init(&last_scan);
  int n = P()->wifi_scan(&last_scan);
  if (n < 0) return ws_send_error(r, n == APP_EBUSY ? 409 : 500, n == APP_EBUSY ? "the X's radio is busy" : "the scan failed");
  JsonDocument d;
  JsonArray a = d["nets"].to<JsonArray>();
  for (int i = 0; i < last_scan.n && i < WSW_LIST_MAX; i++) {
    const wsw_net_t &w = last_scan.it[i];
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = w.ssid;
    o["rssi"] = w.rssi;
    o["open"] = w.open;
    o["band"] = w.is5 ? "5" : "2.4";
    if (w.enterprise) o["ent"] = true;
  }
  ws_send_doc(r, 200, d);
}

static void handle_wifi_join(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  char ssid[WSW_SSID_MAX + 2] = "", pass[WSW_PASS_MAX + 2] = "", why[64];
  if (!r.param("s", true, ssid, sizeof(ssid))) return ws_send_error(r, 400, "no network");
  r.param("p", true, pass, sizeof(pass));
  wsw_join_t j;
  int rc = wsw_check(&last_scan, ssid, pass, &j, why, sizeof(why));
  memset(pass, 0, sizeof(pass));
  if (rc) return ws_send_error(r, rc == APP_ENOENT ? 404 : 400, why);
  rc = P()->wifi_join(&j);
  memset(&j, 0, sizeof(j));
  if (rc) return ws_send_error(r, 500, "the X cannot switch now");
  WP.env->log("webshare: Wi-Fi switch to %s asked", ssid); // the name only
  ws_send_json(r, 200, "{\"ok\":true}");
}

static const char *const BLE_STATE[] = {"off", "list", "connecting", "passkey", "paired"};

static void handle_ble_get(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  ws_ble_info_t b = {};
  P()->ble_info(&b);
  JsonDocument d;
  d["state"] = (unsigned)b.state <= WS_BLE_PAIRED ? BLE_STATE[b.state] : "off";
  JsonArray a = d["found"].to<JsonArray>();
  for (int i = 0; i < b.n && i < WS_BLE_FOUND_MAX; i++) {
    JsonObject o = a.add<JsonObject>();
    b.found[i].name[sizeof(b.found[i].name) - 1] = '\0';
    o["name"] = b.found[i].name;
    o["rssi"] = b.found[i].rssi;
  }
  if (b.state == WS_BLE_PASSKEY) {
    char k[8];
    snprintf(k, sizeof(k), "%06lu", (unsigned long)(b.passkey % 1000000));
    d["passkey"] = k;
  }
  b.message[sizeof(b.message) - 1] = b.paired[sizeof(b.paired) - 1] = '\0';
  d["message"] = b.message;
  if (b.state == WS_BLE_PAIRED) d["paired"] = b.paired;
  ws_send_doc(r, 200, d);
}

static void handle_ble_do(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  char what[8] = "", idx[4] = "";
  r.param("do", true, what, sizeof(what));
  ws_ble_do_t w = !strcmp(what, "start") ? WS_BLE_START : !strcmp(what, "pick") ? WS_BLE_PICK : WS_BLE_STOP;
  if (w == WS_BLE_STOP && strcmp(what, "stop")) return ws_send_error(r, 400, "start, pick or stop");
  int i = -1;
  if (w == WS_BLE_PICK) {
    r.param("i", true, idx, sizeof(idx));
    if (idx[0] < '0' || idx[0] > '9' || idx[1]) return ws_send_error(r, 400, "which keyboard?");
    i = idx[0] - '0';
    if (i >= WS_BLE_FOUND_MAX) return ws_send_error(r, 400, "which keyboard?");
  }
  int rc = P()->ble_pair(w, i);
  if (rc == APP_EBUSY) return ws_send_error(r, 409, "Bluetooth is busy (ESP-NOW or an app's keyboard): stop it first");
  if (rc == APP_EINVAL) return ws_send_error(r, 404, "that keyboard is gone: scan again");
  if (rc) return ws_send_error(r, 500, "Bluetooth did not start (memory?)");
  if (w != WS_BLE_PICK) WP.env->log("webshare: Bluetooth pairing %s", what);
  ws_send_json(r, 200, "{\"ok\":true}");
}

const ws_route_t WS_ROUTES_RADIO[] = {
    {WS_POST, "/api/wifiscan", handle_wifi_scan, nullptr, nullptr},
    {WS_POST, "/api/wifijoin", handle_wifi_join, nullptr, nullptr},
    {WS_GET, "/api/blepair", handle_ble_get, nullptr, nullptr},
    {WS_POST, "/api/blepair", handle_ble_do, nullptr, nullptr},
};
const int WS_ROUTES_RADIO_N = (int)(sizeof(WS_ROUTES_RADIO) / sizeof(WS_ROUTES_RADIO[0]));
