#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <app_api.h>

// Which network the Wi-Fi page uses. AUTO (the default) is the home network when a saved one is reachable, else
// the own access point. HOME refuses to fall back (false: no network); AP is always the access point.
// `picked` is APP_WEBSHARE_HOME or APP_WEBSHARE_AP.
bool ws_pick_mode(app_webshare_mode_t requested, bool home_wifi_up, app_webshare_mode_t *picked);

// Whether stopping the page drops the home Wi-Fi: only when the page joined it itself (it was not connected before),
// so the radio is free again for ESP-NOW or a Bluetooth keyboard (device 2026-10-31: after the FPV app's settings
// page the X stayed on the home network and ESP-NOW was refused). A network an app joined with net_up stays.
bool ws_release_wifi_on_stop(app_webshare_mode_t mode, bool connected_before_start);

// Why a start was refused, for the event log: one text per code (unknown codes share the last one)
const char *ws_refusal_reason(int rc);

// What the X shows to open (app_webshare_status_t.address): on the home network "http://<ip>", never the .local name
// alone (owner 2026-11-02: "http://trmnl-x-....local" did not open on the phone, which has no mDNS); the access point
// keeps its fixed address. False when it does not fit or `ip` is empty.
bool ws_open_address(char *out, size_t n, app_webshare_mode_t mode, const char *ip, const char *ap_url);
