#pragma once

#include <persistence_interface.h>

// Which app to resume after a restart (docs/apps.md), kept in NVS. An app is only resumed once it
// ran long enough to count as started (app_resume_started): starting it clears the flag, so an app
// that crashes while starting is not started again, and the device goes back to TRMNL.

#define APP_RESUME_PATH_KEY "app_last"
#define APP_RESUME_FLAG_KEY "app_resume"
#define APP_RESUME_INPUT_KEY "app_input" // a touch or key reached the app since it started

typedef enum {
  APP_RESUME_NONE, // nothing due (or a crash ended it)
  APP_RESUME_GO, // resume the app in `path`
  APP_RESUME_NO_INPUT, // due, but no input reached the app before the restart: not resumed, say so (the user may
                       // have had no way to leave it: a dead touch bar must not trap them in a resume loop)
} app_resume_t;

// How the X came up: only a wake from deep sleep resumes the app (owner 2026-11-02: "it just restarts back into the
// sketch app, it shouldn't do that automatically"); a power-on, esptool or watchdog reset goes to the home screen
typedef enum {
  APP_RESET_SLEEP_WAKE, // ESP_RST_DEEPSLEEP
  APP_RESET_RESTART,    // power-on, software, external or USB reset
  APP_RESET_CRASH,      // panic or a watchdog
} app_reset_t;

// The app to resume, if one is due: copies its path to `path` (also for APP_RESUME_NO_INPUT, to name it) and clears
// the flag (a crash before app_resume_started() then ends the resuming). Any `reset` but APP_RESET_SLEEP_WAKE: nothing
// is resumed and the flag is cleared.
app_resume_t app_resume_take(Persistence &p, app_reset_t reset, char *path, size_t path_len);

// A touch or key reached the running app (written once per run)
void app_resume_input(Persistence &p);

// An app is being started (from the menu or resumed): remembered, not yet due for resuming
void app_resume_starting(Persistence &p, const char *path);

// The app has run for APP_RESUME_STARTED_MS: resume it after a restart
void app_resume_started(Persistence &p);

// The X goes to deep sleep with `running` (nullptr: none) on it. Only an app still running then is resumed on the
// wake (owner 2026-11-02); one the person ended (long press, quit, idle close) never comes back: no app, flag cleared.
// On the X the sleep gate refuses sleep under a running app, so today every sleep clears it (2026-11-02, #93 to #94:
// Sketch ended by a long press came back after the wake).
void app_resume_sleeping(Persistence &p, const char *running);

// The app ended (returned, failed or was ended by the user): nothing to resume
void app_resume_ended(Persistence &p);

#define APP_RESUME_STARTED_MS 5000
