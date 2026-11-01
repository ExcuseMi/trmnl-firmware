// The settings declaration file of each app (app_decl_store.h): text in, text out, every field through asf_declare.
#include "app_decl_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADS_HEAD "x-settings "

size_t ads_encode(const asf_form_t *form, char *out, size_t cap) {
  if (!form || !out || cap == 0) return 0;
  size_t at = 0;
  int n = snprintf(out, cap, ADS_HEAD "%d\n", ADS_FORMAT);
  if (n < 0 || (size_t)n >= cap) return 0;
  at = (size_t)n;
  for (int i = 0; i < form->n && i < ASF_FIELDS_MAX; i++) {
    const asf_field_t *f = &form->f[i];
    n = snprintf(out + at, cap - at, "%s\t%u\t%ld\t%ld\t%s\t%s\n", f->key, (unsigned)f->kind, (long)f->min,
                 (long)f->max, f->label, f->choices);
    if (n < 0 || (size_t)n >= cap - at) return 0;
    at += (size_t)n;
  }
  return at;
}

// One tab-separated column of [*at, end) into out (NUL-terminated); false when it does not fit
static bool column(const char *buf, size_t end, size_t *at, char *out, size_t cap, bool last) {
  size_t s = *at, e = s;
  while (e < end && buf[e] != '\t') e++;
  if (last ? e != end : e == end) return false;
  if (e - s >= cap) return false;
  memcpy(out, buf + s, e - s);
  out[e - s] = '\0';
  *at = e + 1;
  return true;
}

static bool number(const char *s, long *v) {
  if (!s[0]) return false;
  char *end = nullptr;
  *v = strtol(s, &end, 10);
  return end && !*end;
}

int ads_decode(const char *buf, size_t len, asf_form_t *out, asf_form_t *scratch) {
  if (!out || !scratch) return APP_EINVAL;
  memset(out, 0, sizeof(*out));
  if (!buf) return APP_EINVAL;
  const size_t hl = strlen(ADS_HEAD);
  if (len < hl + 2 || memcmp(buf, ADS_HEAD, hl)) return APP_EINVAL;
  const char *nl = (const char *)memchr(buf, '\n', len);
  if (!nl) return APP_EINVAL;
  char ver[8];
  size_t vl = (size_t)(nl - buf) - hl;
  if (vl == 0 || vl >= sizeof(ver)) return APP_EINVAL;
  memcpy(ver, buf + hl, vl);
  ver[vl] = '\0';
  long v = 0;
  if (!number(ver, &v)) return APP_EINVAL;
  if (v != ADS_FORMAT) return APP_ENOTSUP;
  memset(scratch, 0, sizeof(*scratch));
  app_setting_decl_t decls[ASF_FIELDS_MAX];
  size_t at = (size_t)(nl - buf) + 1;
  int n = 0;
  while (at < len) {
    const char *e = (const char *)memchr(buf + at, '\n', len - at);
    if (!e || n >= ASF_FIELDS_MAX) return APP_EINVAL; // a cut last line, or too many
    size_t end = (size_t)(e - buf);
    asf_field_t *f = &scratch->f[n];
    char kind[8], mn[16], mx[16];
    if (!column(buf, end, &at, f->key, sizeof(f->key), false) || !column(buf, end, &at, kind, sizeof(kind), false) ||
        !column(buf, end, &at, mn, sizeof(mn), false) || !column(buf, end, &at, mx, sizeof(mx), false) ||
        !column(buf, end, &at, f->label, sizeof(f->label), false) ||
        !column(buf, end, &at, f->choices, sizeof(f->choices), true)) {
      return APP_EINVAL;
    }
    long k = 0, lo = 0, hi = 0;
    if (!number(kind, &k) || !number(mn, &lo) || !number(mx, &hi)) return APP_EINVAL;
    if (k < 0 || k > 255 || lo < INT32_MIN || lo > INT32_MAX || hi < INT32_MIN || hi > INT32_MAX) return APP_EINVAL;
    decls[n] = {f->key, f->label, k == APP_SETTING_CHOICE ? f->choices : nullptr, (int32_t)lo, (int32_t)hi, (uint32_t)k};
    n++;
    at = end + 1;
  }
  int rc = asf_declare(out, decls, (uint32_t)n);
  if (rc) memset(out, 0, sizeof(*out));
  return rc;
}

bool ads_path(const char *app, char *out, size_t cap) {
  size_t n = app ? strnlen(app, 25) : 0;
  if (n == 0 || n > 24 || !out) return false;
  for (size_t i = 0; i < n; i++) {
    char c = app[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  }
  int w = snprintf(out, cap, ADS_DIR "/%s.txt", app);
  return w > 0 && (size_t)w < cap;
}
