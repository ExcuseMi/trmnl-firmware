#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <persistence_interface.h>

// An app left alone goes back to TRMNL (docs/apps.md "Idle"). Pure: the setting texts, which limit counts (the app's
// own override or the device's), the timer with its 30 s warning and the hold (app_idle_hold, API 2.13). The runner
// feeds it the clock and every sign of a person (touch, key, pen, mouse) and reads its verdict.

#define APP_IDLE_DEFAULT_MIN 10 // the device setting's default (x_devset.h "app_idle_min")
#define APP_IDLE_WARN_MS 30000u // the warning comes this long before the end
#define APP_IDLE_KEY_MAX 15

// A setting text to minutes: "off" -> 0, "5".."60" (5, 10, 15, 30, 60) -> that; anything else -> -1
int app_idle_parse(const char *text);
// The minutes that count for an app: its override ("default", "" or not valid: the device's) over the device setting
// (not valid: APP_IDLE_DEFAULT_MIN). 0 is off
int app_idle_minutes(const char *device, const char *override_text);
// The device-setting key that holds an app's override ("i_" and 8 hex of a hash of the name): false for an empty name
bool app_idle_app_key(const char *app, char *out, size_t cap);

typedef enum {
  APP_IDLE_NONE, // nothing to do
  APP_IDLE_WARN, // the warning time begins (returned once per idle stretch)
  APP_IDLE_EXPIRED, // the limit is reached (returned once)
} app_idle_event_t;

typedef struct {
  uint32_t limit_ms; // 0: off
  uint32_t last_ms; // the last sign of a person (or of the hold ending)
  bool hold, warned, expired;
} app_idle_t;

void app_idle_begin(app_idle_t *t, uint32_t now_ms, int minutes);
void app_idle_activity(app_idle_t *t, uint32_t now_ms);
// While held the timer does not run; letting go starts a full idle stretch
void app_idle_hold(app_idle_t *t, bool on, uint32_t now_ms);
app_idle_event_t app_idle_poll(app_idle_t *t, uint32_t now_ms);
// True while the warning is up (WARN returned, no sign of a person since)
bool app_idle_warning(const app_idle_t *t);

// The limit was reached and the app is asked to exit: a timed-out app is quit, not interrupted, so the next wake does
// not resume it (as after a long press)
void app_idle_close(Persistence &p);
