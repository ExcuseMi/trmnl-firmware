// The X's page beyond installing (docs/apps.md "The X's page"): each app's settings form while the app does not run,
// the device settings, printers, the Bluetooth keyboard, the USB devices seen and this X's facts. Added to the apps
// profile's routes when the device gives ws_env_t.portal. Transport-free (ws_http.h); every value is checked (the
// app's declaration, x_devset.h) before it is written; a secret never goes back to the phone.
#include <stdio.h>
#include <string.h>

#include "app_decl_store.h"
#include "app_idle.h"
#include "x_devset.h"
#include "ws_page_json.h"

static const ws_portal_env_t *P(void) { return WP.env->portal; }

// The app named in ?app=, a valid name; false (and the answer sent) otherwise
static bool app_param(WsRequest &r, char *app, size_t cap) {
  char path[64];
  app[0] = 0;
  r.param("app", false, app, cap);
  if (!ads_path(app, path, sizeof(path))) { // the same name rule as the declaration file
    ws_send_error(r, 400, "no such app");
    return false;
  }
  return true;
}

struct app_kv {
  const char *app;
};
static int kv_get(void *c, const char *k, char *b, size_t n) { return P()->app_get(((app_kv *)c)->app, k, b, n); }
static int kv_set(void *c, const char *k, const char *v, size_t n) { return P()->app_set(((app_kv *)c)->app, k, v, n); }
static int kv_remove(void *c, const char *k) { return P()->app_remove(((app_kv *)c)->app, k); }

// The app's form, from its stored declaration; nullptr (answer sent) when none
static asf_form_t *app_form(WsRequest &r, const char *app) {
  asf_form_t *f = (asf_form_t *)WP.env->big_alloc(sizeof(asf_form_t));
  if (!f) return ws_send_error(r, 500, "no memory"), nullptr;
  if (!P()->decl_load(app, f) || f->n == 0) {
    WP.env->big_free(f);
    ws_send_error(r, 404, "this app has no settings yet (start it once)");
    return nullptr;
  }
  return f;
}

static void handle_app_settings_get(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  char app[32];
  if (!app_param(r, app, sizeof(app))) return;
  asf_form_t *f = app_form(r, app);
  if (!f) return;
  app_kv c = {app};
  const ws_kv_t kv = {&c, kv_get, kv_set, kv_remove};
  ws_settings_send_form(r, f, &kv);
  WP.env->big_free(f);
}

static void handle_app_settings_set(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  char app[32];
  if (!app_param(r, app, sizeof(app))) return;
  asf_form_t *f = app_form(r, app);
  if (!f) return;
  app_kv c = {app};
  const ws_kv_t kv = {&c, kv_get, kv_set, kv_remove};
  int changed = 0;
  bool ok = ws_settings_apply(r, f, &kv, &changed);
  WP.env->big_free(f);
  if (!ok) return; // the answer is sent
  WP.env->log("webshare: %s settings saved, %d changed", app, changed);
  if (changed) P()->app_changed(app, changed);
  JsonDocument d;
  d["ok"] = true;
  d["changed"] = changed;
  ws_send_doc(r, 200, d);
}

// The app's display name and its 64x64 icon as hex (MSB first, rows of 8 bytes), "" when it has none
static void handle_app_info(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  char app[32];
  if (!app_param(r, app, sizeof(app))) return;
  app_meta_t *m = (app_meta_t *)WP.env->big_alloc(sizeof(app_meta_t));
  if (!m) return ws_send_error(r, 500, "no memory");
  if (!P()->app_meta(app, m)) {
    WP.env->big_free(m);
    return ws_send_error(r, 404, "no such app");
  }
  static const char DIGITS[] = "0123456789abcdef";
  char *hex = (char *)WP.env->big_alloc(APP_ICON_BYTES * 2 + 1);
  if (!hex) {
    WP.env->big_free(m);
    return ws_send_error(r, 500, "no memory");
  }
  size_t k = 0;
  for (size_t i = 0; m->has_icon && i < APP_ICON_BYTES; i++) {
    hex[k++] = DIGITS[m->icon[i] >> 4];
    hex[k++] = DIGITS[m->icon[i] & 15];
  }
  hex[k] = 0;
  JsonDocument d;
  d["name"] = m->name;
  d["icon"] = hex;
  ws_send_doc(r, 200, d);
  WP.env->big_free(hex);
  WP.env->big_free(m);
}

static void handle_x(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  ws_x_info_t x = {};
  P()->x_info(&x);
  JsonDocument d;
  d["name"] = x.name;
  d["version"] = x.version;
  d["wifi"] = x.wifi;
  d["wifi5"] = x.wifi_5ghz;
  d["ap"] = WP.ap;
  ws_send_doc(r, 200, d);
}

static void handle_devices(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  JsonDocument d;
  JsonArray a = d["settings"].to<JsonArray>();
  int n = 0;
  const xds_key_t *keys = xds_keys(&n);
  char v[XDS_VALUE_MAX + 1];
  for (int i = 0; i < n; i++) {
    JsonObject o = a.add<JsonObject>();
    o["k"] = keys[i].key;
    o["l"] = keys[i].label;
    o["t"] = !strcmp(keys[i].key, "printer") ? "printer" : keys[i].choices ? "choice" : "number";
    if (keys[i].choices) o["choices"] = keys[i].choices;
    else o["min"] = keys[i].min, o["max"] = keys[i].max;
    int got = P()->dev_get(keys[i].key, v, XDS_VALUE_MAX);
    if (got > 0) v[got] = '\0';
    o["v"] = got > 0 ? v : keys[i].def;
  }
  char name[48];
  if (P()->ble_bond(name, sizeof(name))) d["ble"] = name[0] ? name : "a keyboard";
  else d["ble"] = nullptr;
  usb_seen_t usb = {};
  P()->usb_list(&usb);
  JsonArray u = d["usb"].to<JsonArray>();
  for (int i = 0; i < usb.n && i < USB_SEEN_MAX; i++) {
    char id[10];
    snprintf(id, sizeof(id), "%04x:%04x", usb.it[i].vid, usb.it[i].pid);
    JsonObject o = u.add<JsonObject>();
    o["id"] = id;
    o["kind"] = usb.it[i].kind;
  }
  ws_send_doc(r, 200, d);
}

static void handle_device_set(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  char key[XDS_KEY_MAX + 2] = "", v[XDS_VALUE_MAX + 2] = "", why[64];
  r.param("k", true, key, sizeof(key));
  if (!r.param("v", true, v, sizeof(v))) return ws_send_error(r, 400, "no value");
  size_t len = strlen(v);
  int rc = xds_check(key, v, len, why, sizeof(why));
  if (rc == APP_ENOENT) return ws_send_error(r, 404, "no such setting");
  if (rc) return ws_send_error(r, 400, why);
  if (P()->dev_set(key, v, len) != 0) return ws_send_error(r, 500, "could not save (flash full?)");
  WP.env->log("webshare: device setting %s saved", key); // never the value: a printer's address is the user's
  ws_send_json(r, 200, "{\"ok\":true}");
}

// An app's own idle limit (Apps > Configure): a device setting under a key made of the app's name, so the app
// never sees or changes it. GET: {"v": a limit}; POST v=
static void handle_app_idle_get(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  char app[32], key[APP_IDLE_KEY_MAX + 1], v[16];
  if (!app_param(r, app, sizeof(app)) || !app_idle_app_key(app, key, sizeof(key))) return;
  int got = P()->dev_get(key, v, sizeof(v) - 1);
  if (got > 0) v[got] = '\0';
  JsonDocument d;
  d["v"] = got > 0 ? v : "default";
  ws_send_doc(r, 200, d);
}

static void handle_app_idle_set(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  char app[32], key[APP_IDLE_KEY_MAX + 1], v[16] = "", why[64];
  if (!app_param(r, app, sizeof(app)) || !app_idle_app_key(app, key, sizeof(key))) return;
  if (!r.param("v", true, v, sizeof(v))) return ws_send_error(r, 400, "no value");
  size_t len = strlen(v);
  if (xds_check(key, v, len, why, sizeof(why))) return ws_send_error(r, 400, why[0] ? why : "not a valid limit");
  if (P()->dev_set(key, v, len) != 0) return ws_send_error(r, 500, "could not save (flash full?)");
  WP.env->log("webshare: %s idle limit saved", app);
  ws_send_json(r, 200, "{\"ok\":true}");
}

static void handle_printers_find(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  nb_list_t *l = (nb_list_t *)WP.env->big_alloc(sizeof(nb_list_t));
  if (!l) return ws_send_error(r, 500, "no memory");
  memset(l, 0, sizeof(*l));
  int n = P()->printers_find(l);
  if (n < 0) {
    WP.env->big_free(l);
    if (n == APP_EBUSY) return ws_send_error(r, 409, "the X is busy: close other tabs of this page and try again");
    return ws_send_error(r, n == APP_ENONET ? 503 : 500, n == APP_ENONET ? "no network" : "the search failed");
  }
  JsonDocument d;
  JsonArray a = d["printers"].to<JsonArray>();
  for (int i = 0; i < l->n && i < NB_MAX; i++) {
    JsonObject o = a.add<JsonObject>();
    o["name"] = l->item[i].name;
    o["host"] = l->item[i].host;
    o["port"] = l->item[i].port;
    o["path"] = l->item[i].path;
  }
  ws_send_doc(r, 200, d); // before the free: the strings are pointers into the list
  WP.env->big_free(l);
}

static void handle_printer_test(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  char v[XDS_VALUE_MAX + 1];
  if (P()->dev_get("printer", v, XDS_VALUE_MAX) <= 0) return ws_send_error(r, 409, "choose a default printer first");
  int rc = P()->printer_test();
  if (rc) return ws_send_error(r, 500, "the X cannot print now");
  ws_send_json(r, 200, "{\"ok\":true}");
}

static void handle_ble_forget(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  P()->ble_forget();
  WP.env->log("webshare: Bluetooth keyboard forgotten");
  ws_send_json(r, 200, "{\"ok\":true}");
}

const ws_route_t WS_ROUTES_PORTAL[] = {
    {WS_GET, "/api/x", handle_x, nullptr, nullptr},
    {WS_GET, "/api/appinfo", handle_app_info, nullptr, nullptr},
    {WS_GET, "/api/appsettings", handle_app_settings_get, nullptr, nullptr},
    {WS_POST, "/api/appsettings", handle_app_settings_set, nullptr, nullptr},
    {WS_GET, "/api/appidle", handle_app_idle_get, nullptr, nullptr},
    {WS_POST, "/api/appidle", handle_app_idle_set, nullptr, nullptr},
    {WS_GET, "/api/devices", handle_devices, nullptr, nullptr},
    {WS_POST, "/api/device", handle_device_set, nullptr, nullptr},
    {WS_POST, "/api/printers", handle_printers_find, nullptr, nullptr},
    {WS_POST, "/api/printertest", handle_printer_test, nullptr, nullptr},
    {WS_POST, "/api/bleforget", handle_ble_forget, nullptr, nullptr},
};
const int WS_ROUTES_PORTAL_N = (int)(sizeof(WS_ROUTES_PORTAL) / sizeof(WS_ROUTES_PORTAL[0]));
