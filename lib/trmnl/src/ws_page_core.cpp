// The Wi-Fi page's shared part: state, login (code and one-time QR link), the page itself, the keyboard layout,
// the touch buttons and the route table of each profile. Transport-free (ws_http.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kbd_layout.h"
#include "ws_page_json.h"

ws_page_t WP;

static const ws_header_t NO_STORE = {"Cache-Control", "no-store"};

void ws_send_json(WsRequest &r, int code, const char *body) {
  r.send(code, "application/json", body, strlen(body), &NO_STORE, 1);
}

void ws_send_doc(WsRequest &r, int code, const JsonDocument &d) {
  size_t n = measureJson(d);
  char *s = (char *)malloc(n + 1);
  if (!s) return ws_send_json(r, 503, "{\"error\":\"out of memory\"}");
  serializeJson(d, s, n + 1);
  r.send(code, "application/json", s, n, &NO_STORE, 1);
  free(s);
}

void ws_send_error(WsRequest &r, int code, const char *what) {
  JsonDocument d;
  d["error"] = what;
  ws_send_doc(r, code, d);
}

int ws_http_code(ws_result_t rc) {
  switch (rc) {
  case WS_OK: return 200;
  case WS_E_NAME: case WS_E_SHORT: return 400;
  case WS_E_NOT_FOUND: return 404;
  case WS_E_EXISTS: case WS_E_KEEP_FAILED: return 409;
  case WS_E_TOO_BIG: return 413;
  case WS_E_BUSY: return 503;
  case WS_E_NO_SPACE: return 507;
  default: return 500;
  }
}

bool ws_session_cookie(WsRequest &r, char *out, size_t cap) {
  out[0] = '\0';
  const char *c = r.header("Cookie");
  const char *at = c ? strstr(c, "wsid=") : nullptr;
  if (!at) return false;
  snprintf(out, cap, "%.*s", WS_TOKEN_LEN, at + 5);
  return true;
}

static bool session_ok(WsRequest &r) {
  char sid[WS_TOKEN_LEN + 1];
  ws_session_cookie(r, sid, sizeof(sid));
  return ws_auth_session_ok(&WP.auth, sid);
}

// A request with a good session (and, for anything that changes data, the header only our page sends: another
// site's form cannot add it). Refreshes the idle timer.
bool ws_page_guard(WsRequest &r, bool changes, bool reply) {
  if (!WP.running || !session_ok(r) || (changes && !r.header("X-Writer"))) {
    if (reply) ws_send_error(r, 401, "type the code first");
    return false;
  }
  WP.last_request = WP.env->now();
  return true;
}

bool ws_page_authorized(WsRequest &r, bool changes) {
  return WP.profile == WS_PROFILE_INSTALL ? ws_install_allowed(r) : ws_page_guard(r, changes, false);
}

bool ws_install_allowed(WsRequest &r) {
  if (!WP.running) return false;
  char cookie[WS_TOKEN_LEN + 1];
  ws_session_cookie(r, cookie, sizeof(cookie));
  // A cookie works for our own page only (the header another site's form cannot add)
  if (cookie[0] && r.header("X-Writer") && ws_auth_session_ok(&WP.auth, cookie)) {
    WP.last_request = WP.env->now();
    return true;
  }
  char t[WS_LOGIN_TOKEN_LEN + 2] = "", code[WS_PIN_LEN + 2] = "";
  r.param("t", false, t, sizeof(t));
  r.param("code", false, code, sizeof(code));
  uint32_t rnd[2] = {WP.env->random(), WP.env->random()};
  char sid[WS_TOKEN_LEN + 1];
  bool ok = ws_auth_request(&WP.auth, nullptr, t, code, WP.env->now(), rnd, sid);
  if (ok) WP.last_request = WP.env->now();
  return ok;
}

static void send_cookie(WsRequest &r, int code, const char *sid, const char *location) {
  char cookie[WS_TOKEN_LEN + 48];
  snprintf(cookie, sizeof(cookie), "wsid=%s; Path=/; HttpOnly; SameSite=Strict", sid);
  ws_header_t h[3] = {{"Set-Cookie", cookie}, NO_STORE, {"Location", location}};
  if (location) r.send(code, nullptr, "", 0, h, 3);
  else r.send(code, "application/json", "{\"ok\":true}", 11, h, 2);
}

static void handle_login(WsRequest &r) {
  if (!WP.running || !r.header("X-Writer")) return ws_send_error(r, 401, "not available");
  WP.last_request = WP.env->now();
  char pin[16] = "";
  r.param("pin", true, pin, sizeof(pin));
  uint32_t rnd[2] = {WP.env->random(), WP.env->random()};
  char token[WS_TOKEN_LEN + 1];
  ws_auth_result_t res = ws_auth_login(&WP.auth, pin, WP.env->now(), rnd, token);
  if (res == WS_AUTH_OK) send_cookie(r, 200, token, nullptr);
  else if (res == WS_AUTH_WRONG) ws_send_error(r, 403, "wrong code");
  else if (res == WS_AUTH_LOCKED) ws_send_error(r, 429, "too many wrong codes: wait a minute or more");
  else ws_send_error(r, 423, "too many wrong codes: switched off, start it again on the X");
}

static void handle_root(WsRequest &r) {
  char t[WS_LOGIN_TOKEN_LEN + 2];
  if (r.param("t", false, t, sizeof(t))) { // the one-time link from the X's QR: log in, then drop it from the address
    uint32_t rnd[2] = {WP.env->random(), WP.env->random()};
    char sid[WS_TOKEN_LEN + 1];
    WP.last_request = WP.env->now();
    if (ws_auth_token_login(&WP.auth, t, WP.env->now(), rnd, sid) == WS_AUTH_OK) return send_cookie(r, 302, sid, "/");
  }
  const char *page = WP.env->page(WP.profile);
  r.send(200, "text/html; charset=utf-8", page, strlen(page), &NO_STORE, 1);
}

// ---- keyboard layout (every profile) ----

static void handle_kbd_js(WsRequest &r) {
  r.send(200, "text/javascript; charset=utf-8", WP.env->kbd_js, strlen(WP.env->kbd_js), &NO_STORE, 1);
}

static void handle_kbd_get(WsRequest &r) {
  if (!ws_page_authorized(r, false)) return ws_send_error(r, 401, "type the code first");
  JsonDocument d;
  d["cur"] = kbd_layout_code(WP.env->kbd_current());
  JsonArray a = d["layouts"].to<JsonArray>();
  for (int i = 0; i < KBD_LAYOUT_COUNT; i++) {
    JsonObject o = a.add<JsonObject>();
    o["c"] = kbd_layout_code(i);
    o["n"] = kbd_layout_name(i);
  }
  ws_send_doc(r, 200, d);
}

static void handle_kbd_set(WsRequest &r) {
  if (!ws_page_authorized(r, true)) return ws_send_error(r, 401, "type the code first");
  char c[16] = "";
  r.param("c", true, c, sizeof(c));
  int l = kbd_layout_from_code(c);
  if (l < 0) return ws_send_error(r, 400, "unknown keyboard layout");
  if (!WP.env->kbd_store(l)) return ws_send_error(r, 500, "could not save the layout");
  if (WP.env->evlog) WP.env->evlog("Keyboard layout: %s", kbd_layout_name(l));
  ws_send_json(r, 200, "{\"ok\":true}");
}

// A button of the page: left, middle, right, long or stop (= long press); the touch bar's path (touch_inject.h)
void ws_handle_touch(WsRequest &r) {
  if (!ws_page_authorized(r, true)) return ws_send_error(r, 401, "type the code first");
  char b[16] = "";
  r.param("b", true, b, sizeof(b));
  if (!WP.env->touch(b)) return ws_send_error(r, 400, "unknown button or queue full");
  WP.env->log("webshare: page button %s", b);
  ws_send_json(r, 200, "{\"ok\":true}");
}

void ws_page_not_found(WsRequest &r) {
  if (WP.ap) {
    ws_header_t loc = {"Location", WP.ap_url};
    r.send(302, nullptr, "", 0, &loc, 1);
  } else {
    r.send(404, nullptr, "", 0, nullptr, 0);
  }
}

// ---- routes, start, watcher ----

static const ws_route_t COMMON[] = {
    {WS_GET, "/", handle_root, nullptr, nullptr},
    {WS_POST, "/api/login", handle_login, nullptr, nullptr},
    {WS_GET, "/kbd.js", handle_kbd_js, nullptr, nullptr},
    {WS_GET, "/api/kbd", handle_kbd_get, nullptr, nullptr},
    {WS_POST, "/api/kbd", handle_kbd_set, nullptr, nullptr},
};
#define COMMON_N ((int)(sizeof(COMMON) / sizeof(COMMON[0])))
static ws_route_t routes[COMMON_N + 12];
static int routes_n;

int ws_page_routes(const ws_route_t **out) {
  *out = routes;
  return routes_n;
}

void ws_page_start(int profile, bool ap, const ws_env_t *env, const ws_fs_t *fs_app, const ws_fs_t *fs_root) {
  ws_files_cfg_t files_cfg = WP.files_cfg; // set by the files start before
  const char *ap_url = WP.ap_url, *host = WP.host;
  memset(&WP, 0, sizeof(WP));
  WP.files_cfg = files_cfg;
  WP.ap_url = ap_url;
  WP.host = host;
  WP.profile = profile;
  WP.ap = ap;
  WP.env = env;
  WP.fs_app = fs_app;
  WP.fs_root = fs_root;
  ws_auth_init(&WP.auth, env->random());
  uint32_t tok[4] = {env->random(), env->random(), env->random(), env->random()};
  ws_auth_new_token(&WP.auth, tok, env->now());
  WP.last_request = env->now();
  const ws_route_t *p = profile == WS_PROFILE_INSTALL  ? WS_ROUTES_INSTALL
                        : profile == WS_PROFILE_FILES    ? WS_ROUTES_FILES
                        : profile == WS_PROFILE_SETTINGS ? WS_ROUTES_SETTINGS
                                                         : WS_ROUTES_WRITER;
  int pn = profile == WS_PROFILE_INSTALL  ? WS_ROUTES_INSTALL_N
           : profile == WS_PROFILE_FILES    ? WS_ROUTES_FILES_N
           : profile == WS_PROFILE_SETTINGS ? WS_ROUTES_SETTINGS_N
                                            : WS_ROUTES_WRITER_N;
  routes_n = 0;
  for (int i = 0; i < COMMON_N; i++) routes[routes_n++] = COMMON[i];
  for (int i = 0; i < pn && routes_n < (int)(sizeof(routes) / sizeof(routes[0])); i++) routes[routes_n++] = p[i];
}

void ws_page_drop_upload(void) {
  if (!WP.upload_owner) return;
  ws_upload_abort(&WP.upload);
  WP.upload_owner = nullptr;
}

bool ws_page_tick(uint32_t now, bool *idle) {
  if (WP.running && WP.upload_owner && now - WP.upload_last_chunk > WS_UPLOAD_STALL_MS) {
    WP.env->log("webshare: upload stalled, dropped");
    ws_page_drop_upload();
  }
  // never while an upload runs: it would end as "no connection" in the browser
  *idle = !WP.upload_owner && ws_idle_expired(WP.last_request, now);
  return WP.running && (*idle || WP.auth.fails >= WS_MAX_FAILS);
}
