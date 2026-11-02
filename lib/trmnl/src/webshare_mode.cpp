#include <stdio.h>
#include "webshare_mode.h"

bool ws_pick_mode(app_webshare_mode_t requested, bool home_wifi_up, app_webshare_mode_t *picked) {
  switch (requested) {
  case APP_WEBSHARE_AUTO:
    *picked = home_wifi_up ? APP_WEBSHARE_HOME : APP_WEBSHARE_AP;
    return true;
  case APP_WEBSHARE_HOME:
    *picked = APP_WEBSHARE_HOME;
    return home_wifi_up;
  case APP_WEBSHARE_AP:
    *picked = APP_WEBSHARE_AP;
    return true;
  }
  return false;
}

bool ws_release_wifi_on_stop(app_webshare_mode_t mode, bool connected_before_start) {
  return mode == APP_WEBSHARE_HOME && !connected_before_start;
}

const char *ws_refusal_reason(int rc) {
  switch (rc) {
  case APP_EINVAL: return "bad mode or folder";
  case APP_ENOENT: return "the app has no settings form (its settings_declare was refused or never called)";
  case APP_ENOMEM: return "out of memory";
  case APP_ENONET: return "no network (no saved Wi-Fi in reach, or the modem refused the server)";
  case APP_EBUSY: return "busy (ESP-NOW holds the radio, or the last page through the modem is still closing)";
  }
  return "unexpected error";
}

bool ws_open_address(char *out, size_t n, app_webshare_mode_t mode, const char *ip, const char *ap_url) {
  if (!out || !n) return false;
  out[0] = '\0';
  const char *src = mode == APP_WEBSHARE_AP ? ap_url : ip;
  if (!src || !src[0]) return false;
  int w = mode == APP_WEBSHARE_AP ? snprintf(out, n, "%s", src) : snprintf(out, n, "http://%s", src);
  if (w < 0 || (size_t)w >= n) {
    out[0] = '\0';
    return false;
  }
  return true;
}
