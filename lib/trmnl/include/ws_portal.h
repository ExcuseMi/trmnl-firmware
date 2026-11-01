#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_meta.h"
#include "app_settings_form.h"
#include "net_browse.h"
#include "usb_seen.h"
#include "wifi_switch.h"

// What the X's page (the apps profile, docs/apps.md "The X's page") needs from the device beyond ws_env_t: every app's
// stored settings declaration and its settings by name, the device settings (x_devset.h), printers, the Bluetooth
// keyboard, the USB devices seen and the X's own facts. nullptr in ws_env_t: the page has the apps list only.

typedef struct {
  char name[32]; // the device's name (webshare_host.h), "TRMNL-X-BJRLAT"
  char version[32]; // firmware version
  char wifi[33]; // the saved network, "" when none
  bool wifi_5ghz;
} ws_x_info_t;

// A keyboard pairing started from the page (Devices > Bluetooth), as the page sees it
#define WS_BLE_FOUND_MAX 6
typedef enum { WS_BLE_OFF, WS_BLE_LIST, WS_BLE_CONNECTING, WS_BLE_PASSKEY, WS_BLE_PAIRED } ws_ble_state_t;
typedef struct {
  ws_ble_state_t state;
  int n; // keyboards found
  struct {
    char name[24];
    int8_t rssi;
  } found[WS_BLE_FOUND_MAX];
  uint32_t passkey; // WS_BLE_PASSKEY: the 6 digits the X shows
  char message[64]; // why the last try failed, "" when none
  char paired[24]; // WS_BLE_PAIRED: its name
} ws_ble_info_t;
typedef enum { WS_BLE_START, WS_BLE_PICK, WS_BLE_STOP } ws_ble_do_t;

typedef struct {
  // ---- apps (by the name of their file in /apps without ".elf") ----
  bool (*app_meta)(const char *app, app_meta_t *out); // display name and icon from the ELF (app_meta.h); false: no such app
  bool (*decl_load)(const char *app, asf_form_t *out); // its stored declaration (app_decl_store.h); false: none yet
  int (*app_get)(const char *app, const char *key, char *buf, size_t cap); // bytes, < 0 when not set
  int (*app_set)(const char *app, const char *key, const char *v, size_t len); // 0 or < 0
  int (*app_remove)(const char *app, const char *key);
  void (*app_changed)(const char *app, int n); // n values changed: the app hears it when it runs
  // ---- devices ----
  int (*dev_get)(const char *key, char *buf, size_t cap); // bytes (0: the default), < 0 on error
  int (*dev_set)(const char *key, const char *v, size_t len); // len 0 removes; checked by xds_check before
  int (*printers_find)(nb_list_t *out); // count or APP_E*
  int (*printer_test)(void); // the X prints a test page on the default printer when the page closes; 0 or APP_E*
  bool (*ble_bond)(char *name, size_t cap); // the paired keyboard's name; false: none
  void (*ble_forget)(void);
  void (*usb_list)(usb_seen_t *out); // USB devices seen since the X last started
  // ---- this X ----
  void (*x_info)(ws_x_info_t *out);
  // ---- Wi-Fi and Bluetooth (ws_page_radio.cpp) ----
  int (*wifi_scan)(wsw_list_t *out); // networks in reach (both bands when the modem scans): count or APP_E*
  // Records the network to try once the page has closed (wifi_switch.h); 0 or APP_E*. The password is never kept
  // beyond that try and never logged
  int (*wifi_join)(const wsw_join_t *j);
  // Pairing from the page: 0, APP_EBUSY (the radio is taken: ESP-NOW, or an app's keyboard), APP_EINVAL (no such
  // keyboard), APP_ENOMEM
  int (*ble_pair)(ws_ble_do_t what, int index);
  void (*ble_info)(ws_ble_info_t *out);
} ws_portal_env_t;
