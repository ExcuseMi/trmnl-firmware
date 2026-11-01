// What the Wi-Fi page's handlers (lib/trmnl/ws_page_*) need from the device, for either transport: clock, random
// numbers, the lock, the pages, the log, the keyboard layout, the touch bar, the app's settings, PSRAM and the
// note of an app start that failed.
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <Preferences.h>
#include <app_install.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <globals.h>
#include <preferences_persistence.h>
#include <stdarg.h>
#include <touch_inject.h>
#include <trmnl_log.h>

#include "app_host.h"
#include "app_settings_x.h"
#include "evlog.h"
#include "kbd_layout_x.h"
#include "webshare_internal.h"
#include "webshare_kbd_js.h"
#include "webshare_page.h"

static uint32_t now_ms(void) { return millis(); }
static uint32_t rnd(void) { return esp_random(); }
static void lock(void) { xSemaphoreTake(S.lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(S.lock); }

static const char *page(int profile) {
  return profile == WS_PROFILE_INSTALL    ? webshare_install_page()
         : profile == WS_PROFILE_FILES    ? webshare_files_page()
         : profile == WS_PROFILE_SETTINGS ? webshare_settings_page()
                                          : WEBSHARE_PAGE;
}

static void log_line(const char *fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Log_info("%s", buf);
}

static void ev_line(const char *fmt, ...) {
  char buf[120];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  evlog_add("%s", buf);
}

static int setting_get(const char *key, char *buf, size_t cap) { return app_host_setting_get(key, buf, cap); }
static int setting_set(const char *key, const char *v, size_t len) { return app_host_setting_set(key, v, len); }

static void *big_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static void big_free(void *p) { heap_caps_free(p); }
static size_t big_largest(void) { return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

static bool note_get(char *out, size_t cap) {
  PreferencesPersistence p(preferences);
  String note = p.readString(APPI_NOTE_KEY, "");
  snprintf(out, cap, "%s", note.c_str());
  return note.length() > 0;
}

static void note_remove(void) {
  PreferencesPersistence p(preferences);
  p.remove(APPI_NOTE_KEY);
}

const ws_env_t WEBSHARE_ENV = {
    now_ms,      rnd,         lock,        unlock,          page,      WEBSHARE_KBD_JS, log_line,
    ev_line,     kbd_layout_x_current,     kbd_layout_x_store,     touch_inject_button,  app_settings_x_copy,
    setting_get, setting_set, app_host_setting_remove, app_settings_x_changed, big_alloc, big_free,
    big_largest, note_get,    note_remove,
};

#endif // BOARD_TRMNL_X
