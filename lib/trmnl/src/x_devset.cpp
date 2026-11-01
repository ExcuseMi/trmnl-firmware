// The device settings' keys and checks (x_devset.h).
#include "x_devset.h"

#include <app_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const xds_key_t KEYS[] = {
    {"printer", "Default printer", "", nullptr, 0, 0},
    {"pen_area", "Pen tablet area", "fill", "fill|keep", 0, 0},
    {"mouse_speed", "Mouse speed", "normal", "slow|normal|fast", 0, 0},
    {"cleanup_s", "Clean-up flash after (seconds idle, 0 off)", "5", nullptr, 0, 600},
    {"app_idle_min", "Close an app after (minutes without input)", "10", "off|5|10|15|30|60", 0, 0},
};
// An app's own limit, key "i_" + 8 hex (app_idle.h): not listed, found by its shape
static const xds_key_t APP_IDLE = {"i_", "Close this app after", "default", "default|off|5|10|15|30|60", 0, 0};

static bool app_idle_key(const char *key) {
  if (!key || strlen(key) != 10 || key[0] != 'i' || key[1] != '_') return false;
  for (int i = 2; i < 10; i++) {
    if (!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f'))) return false;
  }
  return true;
}
#define N_KEYS ((int)(sizeof(KEYS) / sizeof(KEYS[0])))

const xds_key_t *xds_keys(int *n) {
  if (n) *n = N_KEYS;
  return KEYS;
}

const xds_key_t *xds_find(const char *key) {
  if (app_idle_key(key)) return &APP_IDLE;
  for (int i = 0; key && i < N_KEYS; i++) {
    if (!strcmp(KEYS[i].key, key)) return &KEYS[i];
  }
  return nullptr;
}

static int fail(char *why, size_t cap, const char *s) {
  if (why && cap) snprintf(why, cap, "%s", s);
  return APP_EINVAL;
}

static bool one_of(const char *choices, const char *v, size_t len) {
  for (const char *c = choices; *c;) {
    const char *bar = strchr(c, '|');
    size_t n = bar ? (size_t)(bar - c) : strlen(c);
    if (n == len && !memcmp(c, v, len)) return true;
    if (!bar) break;
    c = bar + 1;
  }
  return false;
}

static bool whole(const char *v, size_t len, long lo, long hi) {
  if (len == 0 || len > 6) return false;
  long x = 0;
  for (size_t i = 0; i < len; i++) {
    if (v[i] < '0' || v[i] > '9') return false;
    x = x * 10 + (v[i] - '0');
  }
  return x >= lo && x <= hi;
}

static bool chars_ok(const char *v, size_t len, const char *extra) {
  for (size_t i = 0; i < len; i++) {
    char c = v[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(extra, c))) return false;
  }
  return true;
}

static int check_printer(const char *v, size_t len, char *why, size_t cap) {
  bool host = false;
  for (size_t i = 0; i < len;) {
    size_t e = i;
    while (e < len && v[e] != '\n') e++;
    size_t n = e - i;
    const char *l = v + i;
    if (n && l[n - 1] == '\r') n--;
    const char *eq = n ? (const char *)memchr(l, '=', n) : nullptr;
    if (n && !eq) return fail(why, cap, "a line without key=value");
    if (eq) {
      size_t kl = (size_t)(eq - l), vl = n - kl - 1;
      const char *x = eq + 1;
      if (kl == 4 && !memcmp(l, "host", 4)) {
        if (vl == 0 || vl > 63 || !chars_ok(x, vl, ".-:")) return fail(why, cap, "not a host name or address");
        host = true;
      } else if (kl == 4 && !memcmp(l, "port", 4)) {
        if (!whole(x, vl, 1, 65535)) return fail(why, cap, "port not 1..65535");
      } else if (kl == 4 && !memcmp(l, "path", 4)) {
        if (vl > 47 || !chars_ok(x, vl, "/._-")) return fail(why, cap, "not a printer path");
        for (size_t k = 0; k + 1 < vl; k++) {
          if (x[k] == '.' && x[k + 1] == '.') return fail(why, cap, "not a printer path");
        }
      } else if (kl == 6 && !memcmp(l, "format", 6)) {
        if (!one_of("pwg|pcl", x, vl)) return fail(why, cap, "format not pwg or pcl");
      } else if (kl == 4 && !memcmp(l, "name", 4)) {
        if (vl > 63) return fail(why, cap, "name too long (at most 63)");
        for (size_t k = 0; k < vl; k++) {
          if ((unsigned char)x[k] < 0x20 || x[k] == 0x7f) return fail(why, cap, "name with control characters");
        }
      } else {
        return fail(why, cap, "unknown printer key");
      }
    }
    i = e + 1;
  }
  return host ? 0 : fail(why, cap, "no host");
}

int xds_check(const char *key, const char *value, size_t len, char *why, size_t why_len) {
  if (why && why_len) why[0] = '\0';
  const xds_key_t *k = xds_find(key);
  if (!k) return APP_ENOENT;
  if (!value && len) return fail(why, why_len, "no value");
  if (len > XDS_VALUE_MAX) return fail(why, why_len, "too long");
  if (!strcmp(k->key, "printer")) return len == 0 ? 0 : check_printer(value, len, why, why_len);
  if (k->choices) return one_of(k->choices, value, len) ? 0 : fail(why, why_len, "not one of the choices");
  return whole(value, len, k->min, k->max) ? 0 : fail(why, why_len, "not a whole number in range");
}
