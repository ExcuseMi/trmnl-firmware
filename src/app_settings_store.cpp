// Every app's settings and settings declaration by name (app_settings_store.h)
#ifdef BOARD_TRMNL_X

#include <LittleFS.h>
#include <Preferences.h>
#include <app_decl_store.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <trmnl_log.h>

#include "app_host.h"
#include "app_settings_store.h"
#include "app_settings_x.h"

#define APP_SETTING_MAX 256
#define CHANGED_NS "xchg" // key: the app's namespace name, value: values changed while it did not run

// The app's NVS namespace: "app" and a hash of its name (namespaces are at most 15 characters)
static void settings_namespace(const char *app, char *out, size_t len) {
  uint32_t h = 2166136261u;
  for (const char *c = app; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
  snprintf(out, len, "app%08lx", (unsigned long)h);
}

static bool key_ok(const char *key) { return key && key[0] && strlen(key) <= 15; }
static bool app_ok(const char *app) { return app && app[0]; }

int app_store_get(const char *app, const char *key, void *buf, size_t len) {
  if (!app_ok(app) || !key_ok(key) || (!buf && len)) return APP_EINVAL;
  char ns[16];
  settings_namespace(app, ns, sizeof(ns));
  Preferences p;
  if (!p.begin(ns, true)) return APP_ENOENT; // nothing saved yet
  int rc = APP_ENOENT;
  size_t stored = p.isKey(key) ? p.getBytesLength(key) : 0;
  if (stored > 0) rc = stored > len ? APP_EINVAL : (int)p.getBytes(key, buf, len);
  p.end();
  return rc;
}

int app_store_set(const char *app, const char *key, const void *buf, size_t len) {
  if (!app_ok(app) || !key_ok(key) || !buf || len == 0 || len > APP_SETTING_MAX) return APP_EINVAL;
  char ns[16];
  settings_namespace(app, ns, sizeof(ns));
  Preferences p;
  if (!p.begin(ns, false)) return APP_EIO;
  int rc = p.putBytes(key, buf, len) == len ? 0 : APP_EIO;
  p.end();
  return rc;
}

int app_store_remove(const char *app, const char *key) {
  if (!app_ok(app) || !key_ok(key)) return APP_EINVAL;
  char ns[16];
  settings_namespace(app, ns, sizeof(ns));
  Preferences p;
  if (!p.begin(ns, false)) return APP_EIO;
  if (p.isKey(key)) p.remove(key);
  p.end();
  return 0;
}

static char *read_all(const char *path, size_t *len) {
  File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory() || f.size() > ADS_FILE_MAX) return nullptr;
  char *buf = (char *)heap_caps_malloc(ADS_FILE_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf) *len = f.read((uint8_t *)buf, f.size());
  f.close();
  return buf;
}

void app_store_decl_save(const char *app, const asf_form_t *form) {
  char path[64];
  if (!ads_path(app, path, sizeof(path))) return;
  char *text = (char *)heap_caps_malloc(ADS_FILE_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!text) return;
  size_t n = ads_encode(form, text, ADS_FILE_MAX), old_len = 0;
  char *old = read_all(path, &old_len);
  bool same = old && old_len == n && !memcmp(old, text, n);
  heap_caps_free(old);
  if (n && !same) {
    if (!LittleFS.exists("/xset")) LittleFS.mkdir("/xset");
    if (!LittleFS.exists(ADS_DIR)) LittleFS.mkdir(ADS_DIR);
    char tmp[72];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    File f = LittleFS.open(tmp, "w");
    bool ok = f && f.write((const uint8_t *)text, n) == n;
    if (f) f.close();
    ok = ok && (!LittleFS.exists(path) || LittleFS.remove(path)) && LittleFS.rename(tmp, path);
    if (!ok) {
      LittleFS.remove(tmp);
      Log_error("Apps: %s: settings form not kept for the phone page (flash full?)", app);
    }
  }
  heap_caps_free(text);
}

bool app_store_decl_load(const char *app, asf_form_t *out) {
  char path[64];
  size_t len = 0;
  if (!out || !ads_path(app, path, sizeof(path))) return false;
  char *text = read_all(path, &len);
  if (!text) return false;
  asf_form_t *scratch = (asf_form_t *)heap_caps_malloc(sizeof(asf_form_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  int rc = scratch ? ads_decode(text, len, out, scratch) : APP_ENOMEM;
  heap_caps_free(scratch);
  heap_caps_free(text);
  if (rc) Log_info("Apps: %s: stored settings form not usable (%d), start the app once", app, rc);
  return rc == 0;
}

void app_store_changed(const char *app, int n) {
  if (n <= 0 || !app_ok(app)) return;
  if (app_host.events && !strcmp(app_host.name, app)) return app_settings_x_changed(n); // it runs (its own page)
  char ns[16];
  settings_namespace(app, ns, sizeof(ns));
  Preferences p;
  if (!p.begin(CHANGED_NS, false)) return;
  p.putInt(ns, p.getInt(ns, 0) + n);
  p.end();
}

void app_store_started(const char *app) {
  char ns[16];
  settings_namespace(app, ns, sizeof(ns));
  Preferences p;
  if (!p.begin(CHANGED_NS, false)) return;
  int n = p.isKey(ns) ? p.getInt(ns, 0) : 0;
  if (n > 0) p.remove(ns);
  p.end();
  if (n > 0) app_settings_x_changed(n);
}

#endif // BOARD_TRMNL_X
