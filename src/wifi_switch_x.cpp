// The phone page's Wi-Fi Join on the X (wifi_switch.h): after the page has closed, the new network on the S3
// (2.4 GHz) or the modem (5 GHz); saved only once joined, else the saved network again
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <WiFi.h>
#include <WifiCaptive.h>
#include <globals.h>
#include <modem.h>
#include <trmnl_log.h>
#include <wifi_network.h>

#include "evlog.h"
#include "wifi_switch_x.h"

static WifiCredentials creds(const wsw_join_t *j) {
  WifiCredentials c;
  c.ssid = j->ssid;
  c.pswd = j->pass;
  c.is5GHz = j->is5;
  return c;
}

static bool join(void *, const wsw_join_t *j) {
  if (j->is5) return g_modem && g_modem->connectToNetwork(j->ssid, j->pass, getWifiClientHostname()) && g_modem->isJoined();
  WiFi.disconnect();
  return WifiCaptivePortal.connect(creds(j)) == WL_CONNECTED;
}

static void save(void *, const wsw_join_t *j) {
  WifiCredentials c = creds(j);
  WifiCaptivePortal.saveJoined(c);
  c.pswd = String(); // the String's copy of the password leaves RAM with it
}

static bool rejoin_old(void *) {
  WiFi.disconnect();
  return connectWithSavedCredentials();
}

wsw_result_t wifi_switch_x(wsw_join_t *j) {
  static const wsw_ops_t OPS = {join, save, rejoin_old, nullptr};
  char ssid[sizeof(j->ssid)];
  snprintf(ssid, sizeof(ssid), "%s", j->ssid);
  uint32_t t0 = millis();
  wsw_result_t r = wsw_switch(j, &OPS); // wipes the password
  evlog_add("Wi-Fi: switch to %s from the phone page: %s after %lu ms", ssid,
            r == WSW_JOINED ? "joined, saved" : r == WSW_KEPT_OLD ? "failed, the saved network again" : "failed, no network",
            (unsigned long)(millis() - t0));
  return r;
}

#endif // BOARD_TRMNL_X
