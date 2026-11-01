// The settings profile of the Wi-Fi page (API 2.6): the running app's settings form. Transport-free (ws_http.h);
// the form's rules are app_settings_form. No value of a field is ever logged; a secret is never sent back.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ws_page_json.h"

static const char *kind_name(uint8_t k) {
  return k == APP_SETTING_SECRET ? "secret" : k == APP_SETTING_NUMBER ? "number" : k == APP_SETTING_CHOICE ? "choice" : "text";
}

// The form, copied for this request (the app may declare again meanwhile)
static asf_form_t *form_copy(void) {
  asf_form_t *f = (asf_form_t *)WP.env->big_alloc(sizeof(asf_form_t));
  if (f && !WP.env->settings_copy(f)) f->n = 0;
  return f;
}

static void handle_settings_get(WsRequest &r) {
  if (!ws_page_authorized(r, false)) return ws_send_error(r, 401, "type the code first");
  asf_form_t *form = form_copy();
  if (!form) return ws_send_error(r, 500, "no memory");
  JsonDocument d;
  JsonArray a = d["fields"].to<JsonArray>();
  for (int i = 0; i < form->n; i++) {
    const asf_field_t *f = &form->f[i];
    JsonObject o = a.add<JsonObject>();
    o["k"] = f->key;
    o["l"] = f->label;
    o["t"] = kind_name(f->kind);
    o["min"] = f->min;
    o["max"] = f->max;
    if (f->kind == APP_SETTING_CHOICE) {
      JsonArray c = o["choices"].to<JsonArray>();
      char one[ASF_CHOICES_BYTES + 1];
      for (int k = 0; asf_choice(f, k, one, sizeof(one)); k++) c.add(one);
    }
    char stored[ASF_VALUE_MAX + 1], shown[ASF_VALUE_MAX + 1];
    int n = WP.env->setting_get(f->key, stored, ASF_VALUE_MAX);
    bool set = false;
    asf_shown(f, stored, n > 0 ? (size_t)n : 0, shown, sizeof(shown), &set); // a secret: "" always
    if (f->kind != APP_SETTING_SECRET) o["v"] = shown;
    o["set"] = set;
  }
  ws_send_doc(r, 200, d); // before the free: ArduinoJson keeps the keys and labels as pointers into the form
  WP.env->big_free(form);
}

// Every value checked first, then written: one bad value writes nothing
static void handle_settings_set(WsRequest &r) {
  if (!ws_page_authorized(r, true)) return ws_send_error(r, 401, "type the code first");
  asf_form_t *form = form_copy();
  if (!form) return ws_send_error(r, 500, "no memory");
  char why[ASF_WHY_MAX], msg[ASF_LABEL_MAX + ASF_WHY_MAX + 4], key[ASF_KEY_MAX + 3], v[ASF_VALUE_MAX + 2];
  for (int i = 0; i < form->n; i++) {
    const asf_field_t *f = &form->f[i];
    snprintf(key, sizeof(key), "v_%s", f->key);
    if (!r.param(key, true, v, sizeof(v))) continue; // not sent: unchanged (an empty secret field)
    if (asf_check(f, v, strlen(v), why, sizeof(why)) != 0) {
      snprintf(msg, sizeof(msg), "%s: %s", f->label, why);
      WP.env->big_free(form);
      return ws_send_error(r, 400, msg);
    }
  }
  int changed = 0, failed = 0;
  for (int i = 0; i < form->n; i++) {
    const asf_field_t *f = &form->f[i];
    snprintf(key, sizeof(key), "v_%s", f->key);
    if (!r.param(key, true, v, sizeof(v))) continue;
    size_t vlen = strlen(v);
    char stored[ASF_VALUE_MAX + 1];
    int n = WP.env->setting_get(f->key, stored, ASF_VALUE_MAX);
    if (n < 0) n = 0;
    if ((size_t)n == vlen && !memcmp(stored, v, vlen)) continue;
    int rc = vlen ? WP.env->setting_set(f->key, v, vlen) : WP.env->setting_remove(f->key);
    if (rc == 0) changed++;
    else failed++;
  }
  WP.env->big_free(form);
  WP.env->log("webshare: settings saved, %d changed, %d failed", changed, failed);
  if (changed) WP.env->settings_changed(changed);
  if (failed) return ws_send_error(r, 500, "could not save every value (flash full?)");
  JsonDocument d;
  d["ok"] = true;
  d["changed"] = changed;
  ws_send_doc(r, 200, d);
}

const ws_route_t WS_ROUTES_SETTINGS[] = {
    {WS_POST, "/api/touch", ws_handle_touch, nullptr, nullptr},
    {WS_GET, "/api/settings", handle_settings_get, nullptr, nullptr},
    {WS_POST, "/api/settings", handle_settings_set, nullptr, nullptr},
};
const int WS_ROUTES_SETTINGS_N = (int)(sizeof(WS_ROUTES_SETTINGS) / sizeof(WS_ROUTES_SETTINGS[0]));
