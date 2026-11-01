// The idle limit of an app (app_idle.h)
#include "app_idle.h"

#include <app_resume.h>
#include <stdio.h>
#include <string.h>

int app_idle_parse(const char *text) {
  if (!text) return -1;
  if (!strcmp(text, "off")) return 0;
  static const char *const OK[] = {"5", "10", "15", "30", "60"};
  for (const char *m : OK) {
    if (!strcmp(text, m)) return m[1] ? (m[0] - '0') * 10 + (m[1] - '0') : m[0] - '0';
  }
  return -1;
}

int app_idle_minutes(const char *device, const char *override_text) {
  int o = app_idle_parse(override_text);
  if (o >= 0) return o;
  int d = app_idle_parse(device);
  return d >= 0 ? d : APP_IDLE_DEFAULT_MIN;
}

bool app_idle_app_key(const char *app, char *out, size_t cap) {
  if (!app || !app[0] || !out || cap < APP_IDLE_KEY_MAX + 1) return false;
  uint32_t h = 2166136261u;
  for (const char *c = app; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
  snprintf(out, cap, "i_%08lx", (unsigned long)h);
  return true;
}

void app_idle_begin(app_idle_t *t, uint32_t now_ms, int minutes) {
  memset(t, 0, sizeof(*t));
  t->limit_ms = minutes > 0 ? (uint32_t)minutes * 60000u : 0;
  t->last_ms = now_ms;
}

void app_idle_activity(app_idle_t *t, uint32_t now_ms) {
  if (t->expired) return; // the exit was asked: a late touch does not take it back
  t->last_ms = now_ms;
  t->warned = false;
}

void app_idle_hold(app_idle_t *t, bool on, uint32_t now_ms) {
  if (t->hold && !on) {
    t->last_ms = now_ms;
    t->warned = false;
  }
  if (on) t->warned = false; // the warning goes with it
  t->hold = on;
}

app_idle_event_t app_idle_poll(app_idle_t *t, uint32_t now_ms) {
  if (!t->limit_ms || t->hold || t->expired) return APP_IDLE_NONE;
  uint32_t idle = now_ms - t->last_ms; // unsigned: right across the clock's wrap
  if (idle >= t->limit_ms) {
    t->expired = true;
    return APP_IDLE_EXPIRED;
  }
  if (!t->warned && idle + APP_IDLE_WARN_MS >= t->limit_ms) {
    t->warned = true;
    return APP_IDLE_WARN;
  }
  return APP_IDLE_NONE;
}

bool app_idle_warning(const app_idle_t *t) { return t->warned && !t->expired; }

void app_idle_close(Persistence &p) { app_resume_ended(p); }
