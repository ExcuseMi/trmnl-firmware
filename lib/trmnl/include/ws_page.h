#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_settings_form.h"
#include "webshare_auth.h"
#include "webshare_files.h"
#include "webshare_store.h"
#include "ws_http.h"

// The Wi-Fi page's handlers, the same for each transport (ws_http.h): login, the one-time QR link, the keyboard
// layout, and the routes of the four profiles (writer, apps, files, settings). What only the device has (pages,
// clock, random numbers, the settings store, the keyboard, the touch bar, PSRAM, the log) comes through ws_env_t;
// the files through ws_fs_t. The firmware: src/webshare*.cpp; host tests: test/test_ws_page.

enum { WS_PROFILE_WRITER, WS_PROFILE_INSTALL, WS_PROFILE_FILES, WS_PROFILE_SETTINGS };

typedef struct {
  uint32_t (*now)(void);
  uint32_t (*random)(void);
  void (*lock)(void); // the upload state; handlers run in the transport's task, the watcher in its own
  void (*unlock)(void);
  const char *(*page)(int profile); // the profile's HTML
  const char *kbd_js;
  void (*log)(const char *fmt, ...);
  void (*evlog)(const char *fmt, ...); // the device's event log (may be nullptr)
  // the keyboard layout of every profile
  int (*kbd_current)(void);
  bool (*kbd_store)(int layout);
  bool (*touch)(const char *button); // a button of the page into the touch bar's path
  // the settings profile
  bool (*settings_copy)(asf_form_t *out);
  int (*setting_get)(const char *key, char *buf, size_t cap);
  int (*setting_set)(const char *key, const char *v, size_t len);
  int (*setting_remove)(const char *key);
  void (*settings_changed)(int count);
  // the apps profile: big buffers (PSRAM), the note of a start that failed
  void *(*big_alloc)(size_t n);
  void (*big_free)(void *p);
  size_t (*big_largest)(void);
  bool (*note_get)(char *out, size_t cap);
  void (*note_remove)(void);
} ws_env_t;

typedef struct {
  bool running;
  int profile;
  bool ap; // on the X's own access point (unknown paths redirect to it)
  const char *ap_url, *host; // "http://4.3.2.1"; the name in the zip's file name
  ws_auth_t auth;
  uint32_t last_request;
  ws_upload_t upload;
  const void *upload_owner; // the request that began `upload`
  uint32_t upload_last_chunk;
  bool zip_busy;
  const ws_fs_t *fs_app, *fs_root; // the app's folder; the file system root (apps profile)
  ws_files_cfg_t files_cfg;
  const ws_env_t *env;
} ws_page_t;

extern ws_page_t WP; // one page at a time

// Resets the state for a start: new code, new one-time token
void ws_page_start(int profile, bool ap, const ws_env_t *env, const ws_fs_t *fs_app, const ws_fs_t *fs_root);
// The watcher's step every few seconds: drops a stalled upload; true when the page should switch off (idle or
// too many wrong codes). Call under the lock.
bool ws_page_tick(uint32_t now, bool *idle);
void ws_page_drop_upload(void); // a half upload's temporary file goes (stop); under the lock

// The routes of the running profile (the transport adds its own: the access point's captive checks)
int ws_page_routes(const ws_route_t **out);
void ws_page_not_found(WsRequest &r);

// ---- for the handler files (ws_page_*.cpp) ----

typedef struct { // per upload request (WsRequest::temp)
  ws_result_t rc;
  bool began, done, unauth;
  ws_put_t how;
  char final_path[WS_PATH_MAX + 1];
  char reason[64];
  char installed[48];
} ws_up_req_t;

void ws_send_json(WsRequest &r, int code, const char *body);
void ws_send_error(WsRequest &r, int code, const char *what);
int ws_http_code(ws_result_t rc);
bool ws_session_cookie(WsRequest &r, char *out, size_t cap);
bool ws_page_guard(WsRequest &r, bool changes, bool reply);
bool ws_page_authorized(WsRequest &r, bool changes);
bool ws_install_allowed(WsRequest &r);
void ws_feed_upload(WsRequest &r, const char *name, const uint8_t *data, size_t len, size_t index, size_t total,
                    bool last, bool exact);
void ws_handle_touch(WsRequest &r);

extern const ws_route_t WS_ROUTES_WRITER[], WS_ROUTES_INSTALL[], WS_ROUTES_FILES[], WS_ROUTES_SETTINGS[];
extern const int WS_ROUTES_WRITER_N, WS_ROUTES_INSTALL_N, WS_ROUTES_FILES_N, WS_ROUTES_SETTINGS_N;
