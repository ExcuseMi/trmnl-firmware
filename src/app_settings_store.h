#pragma once

// Every app's settings by its name (NVS namespace "app" + a hash of the name, setting_get/set in the API table and
// the X's page), its stored settings declaration (app_decl_store.h, LittleFS /xset/decl) and the note that the page
// changed values while the app did not run (APP_EVENT_SETTINGS at its next start).

#include <app_settings_form.h>
#include <stddef.h>

int app_store_get(const char *app, const char *key, void *buf, size_t len); // bytes, APP_ENOENT, APP_EINVAL
int app_store_set(const char *app, const char *key, const void *buf, size_t len); // 0, APP_EINVAL, APP_EIO
int app_store_remove(const char *app, const char *key);
// The app declared `form`: the file is written when its text differs (a failed write is logged, the form stays)
void app_store_decl_save(const char *app, const asf_form_t *form);
bool app_store_decl_load(const char *app, asf_form_t *out); // false: none, damaged or another format
// n values changed on the page: the running app hears it now, another app at its next start
void app_store_changed(const char *app, int n);
void app_store_started(const char *app); // the runner, once the event queue exists
