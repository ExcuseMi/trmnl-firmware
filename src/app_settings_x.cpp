// The running app's settings form (app_settings_x.h)
#ifdef BOARD_TRMNL_X

#include <esp_heap_caps.h>
#include <string.h>

#include "app_host.h"
#include "app_settings_store.h"
#include "app_settings_x.h"
#include "evlog.h"

static asf_form_t *form; // PSRAM, allocated at the first declare and kept (no free while the page may read)
static SemaphoreHandle_t lock;

struct form_lock {
  form_lock() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~form_lock() { xSemaphoreGive(lock); }
};

static bool ready(void) {
  if (!lock) lock = xSemaphoreCreateMutex();
  if (lock && !form) {
    form = (asf_form_t *)heap_caps_calloc(1, sizeof(asf_form_t), MALLOC_CAP_SPIRAM);
    if (!form) form = (asf_form_t *)calloc(1, sizeof(asf_form_t));
  }
  return lock && form;
}

int app_settings_x_declare(const app_setting_decl_t *decls, uint32_t n) {
  if (!ready()) return APP_ENOMEM;
  int bad = n <= ASF_FIELDS_MAX ? asf_bad_field(decls, n) : (int)n;
  if (bad >= 0) { // the app's Settings on the phone would not start: say which field (2026-11-02)
    const char *key = decls && (uint32_t)bad < n && decls[bad].key ? decls[bad].key : "";
    evlog_add("Settings: %s declared a field the X refuses (#%d %.15s; label 1..%d bytes, key 1..%d)", app_host.name, bad,
              key, ASF_LABEL_MAX, ASF_KEY_MAX);
    return APP_EINVAL;
  }
  int rc;
  {
    form_lock l;
    rc = asf_declare(form, decls, n);
  }
  if (rc == 0) app_store_decl_save(app_host.name, form); // the phone page shows it while the app does not run
  return rc;
}

bool app_settings_x_declared(void) {
  if (!lock || !form) return false;
  form_lock l;
  return form->n > 0;
}

bool app_settings_x_copy(asf_form_t *out) {
  if (!out || !lock || !form) return false;
  form_lock l;
  memcpy(out, form, sizeof(*out));
  return out->n > 0;
}

void app_settings_x_changed(int n) {
  if (n <= 0 || !app_host.events) return;
  app_event_t ev = {};
  ev.type = APP_EVENT_SETTINGS;
  ev.key = (uint16_t)n;
  xQueueSend(app_host.events, &ev, 0);
}

void app_settings_x_end(void) {
  if (!lock || !form) return;
  form_lock l;
  memset(form, 0, sizeof(*form));
}

#endif // BOARD_TRMNL_X
