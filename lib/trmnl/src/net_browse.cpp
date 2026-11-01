#include "net_browse.h"

#include <app_api.h>
#include <stdio.h>
#include <string.h>

static bool host_ok(const char *h) {
  if (!h || !h[0] || strlen(h) >= NB_HOST_MAX) return false;
  for (; *h; h++) {
    char c = *h;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == ':' ||
              c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

static void clean_name(const char *s, char *out, size_t n) {
  size_t o = 0;
  for (; s && *s && o + 1 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x20 || c == 0x7f) continue;
    if (o == 0 && c == ' ') continue;
    out[o++] = (char)c;
  }
  while (o > 0 && out[o - 1] == ' ') o--;
  out[o] = 0;
}

void nb_list_init(nb_list_t *l) { memset(l, 0, sizeof(*l)); }

bool nb_rp_clean(const char *rp, char *out, size_t n) {
  if (n) out[0] = 0;
  if (!rp || !n) return false;
  while (*rp == '/') rp++;
  size_t len = strlen(rp);
  if (len == 0 || len >= NB_PATH_MAX || len >= n) return false;
  for (size_t i = 0; i < len; i++) {
    char c = rp[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
              c == '~' || c == '/' || c == '-';
    if (!ok) return false;
    if (c == '.' && rp[i + 1] == '.') return false;
  }
  memcpy(out, rp, len + 1);
  return true;
}

int nb_list_add(nb_list_t *l, const char *name, const char *host, uint16_t port, const char *rp) {
  if (!l || !host_ok(host) || port == 0) return APP_EINVAL;
  char nm[NB_NAME_MAX], path[NB_PATH_MAX];
  clean_name(name, nm, sizeof(nm));
  nb_rp_clean(rp, path, sizeof(path));
  for (int i = 0; i < l->n; i++) {
    nb_item_t *it = &l->item[i];
    if (it->port == port && strcmp(it->host, host) == 0) {
      if (!it->name[0]) memcpy(it->name, nm, sizeof(nm));
      if (!it->path[0]) memcpy(it->path, path, sizeof(path));
      return 0;
    }
  }
  if (l->n >= NB_MAX) return APP_ENOMEM;
  nb_item_t *it = &l->item[l->n++];
  memcpy(it->name, nm, sizeof(nm));
  snprintf(it->host, sizeof(it->host), "%s", host);
  it->port = port;
  memcpy(it->path, path, sizeof(path));
  return 1;
}

bool nb_walk_next(uint32_t self, int *step, uint32_t *out) {
  if (!step || !out) return false;
  uint32_t base = self & 0xffffff00u;
  uint32_t last = self & 0xffu;
  while (*step < 2 * 254) {
    int s = (*step)++;
    int d = s / 2 + 1;
    int host = (s % 2 == 0) ? (int)last + d : (int)last - d;
    if (host < 1 || host > 254 || (uint32_t)host == last) continue;
    *out = base | (uint32_t)host;
    return true;
  }
  return false;
}

uint32_t nb_probe_ms(uint32_t budget_ms) {
  uint32_t ms = budget_ms / 253 * 4 / 5; // a fifth of the time for the AT round trips and the IPP exchange of hits
  return ms < NB_PROBE_MIN_MS ? NB_PROBE_MIN_MS : ms > NB_PROBE_MAX_MS ? NB_PROBE_MAX_MS : ms;
}

size_t nb_cipstart(char *out, size_t n, int link, const char *host, uint16_t port, uint32_t timeout_ms) {
  uint32_t ip;
  if (!out || !n || !host || !nb_ip_parse(host, &ip) || port == 0 || link > 31) return 0;
  int w = link < 0 ? snprintf(out, n, "AT+CIPSTART=\"TCP\",\"%s\",%u,,,%lu", host, (unsigned)port, (unsigned long)timeout_ms)
                   : snprintf(out, n, "AT+CIPSTART=%d,\"TCP\",\"%s\",%u,,,%lu", link, host, (unsigned)port,
                              (unsigned long)timeout_ms);
  return w > 0 && (size_t)w < n ? (size_t)w : 0;
}

int nb_free_link(uint32_t used, int links) {
  for (int i = links - 1; i >= 0; i--)
    if (!(used & (1u << i))) return i;
  return -1;
}

void nb_ip_str(uint32_t ip, char *out, size_t n) {
  snprintf(out, n, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 255), (unsigned)((ip >> 8) & 255),
           (unsigned)(ip & 255));
}

bool nb_ip_parse(const char *s, uint32_t *ip) {
  if (!s || !ip) return false;
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) {
    if (*s < '0' || *s > '9') return false;
    unsigned o = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
      o = o * 10 + (unsigned)(*s++ - '0');
      if (++digits > 3 || o > 255) return false;
    }
    v = (v << 8) | o;
    if (i < 3) {
      if (*s++ != '.') return false;
    }
  }
  if (*s) return false;
  *ip = v;
  return true;
}

// ---- IPP ----

typedef struct {
  uint8_t *p;
  size_t cap, n;
  bool bad;
} wr_t;

static void wput(wr_t *w, const void *b, size_t n) {
  if (w->bad || w->n + n > w->cap) {
    w->bad = true;
    return;
  }
  memcpy(w->p + w->n, b, n);
  w->n += n;
}
static void put8(wr_t *w, int v) {
  uint8_t b = (uint8_t)v;
  wput(w, &b, 1);
}
static void put16(wr_t *w, int v) {
  put8(w, v >> 8);
  put8(w, v);
}
static void attr(wr_t *w, int tag, const char *name, const char *val) {
  size_t ln = strlen(name), lv = strlen(val);
  if (ln > 255 || lv > 255) {
    w->bad = true;
    return;
  }
  put8(w, tag);
  put16(w, (int)ln);
  wput(w, name, ln);
  put16(w, (int)lv);
  wput(w, val, lv);
}

size_t nb_ipp_query(uint8_t *out, size_t cap, const char *host, uint16_t port, const char *path) {
  char p[NB_PATH_MAX];
  if (!out || !host_ok(host) || port == 0 || !nb_rp_clean(path, p, sizeof(p))) return 0;
  char uri[160];
  if (snprintf(uri, sizeof(uri), "ipp://%s:%u/%s", host, (unsigned)port, p) >= (int)sizeof(uri)) return 0;
  wr_t w = {out, cap, 0, false};
  put16(&w, 0x0101);
  put16(&w, 0x000B); // Get-Printer-Attributes
  put16(&w, 0);
  put16(&w, 1);
  put8(&w, 0x01);
  attr(&w, 0x47, "attributes-charset", "utf-8");
  attr(&w, 0x48, "attributes-natural-language", "en");
  attr(&w, 0x45, "printer-uri", uri);
  attr(&w, 0x44, "requested-attributes", "printer-make-and-model");
  put8(&w, 0x03);
  return w.bad ? 0 : w.n;
}

size_t nb_ipp_http_head(char *out, size_t cap, const char *host, uint16_t port, const char *path, size_t body_len) {
  char p[NB_PATH_MAX];
  if (!out || !host_ok(host) || port == 0 || !nb_rp_clean(path, p, sizeof(p))) return 0;
  int n = snprintf(out, cap, "POST /%s HTTP/1.1\r\nHost: %s:%u\r\nContent-Type: application/ipp\r\nContent-Length: %u\r\n"
                             "Connection: close\r\n\r\n",
                   p, host, (unsigned)port, (unsigned)body_len);
  return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

size_t nb_http_body_at(const uint8_t *r, size_t len) {
  for (size_t i = 0; r && i + 4 <= len; i++)
    if (r[i] == '\r' && r[i + 1] == '\n' && r[i + 2] == '\r' && r[i + 3] == '\n') return i + 4;
  return 0;
}

bool nb_ipp_model(const uint8_t *r, size_t len, char *out, size_t n) {
  if (n) out[0] = 0;
  if (!r || !n || len < 8 || r[0] != 1) return false;
  if (((r[2] << 8) | r[3]) >= 0x0100) return false; // not a success status
  size_t i = 8;
  static const char want[] = "printer-make-and-model";
  while (i < len) {
    int tag = r[i++];
    if (tag == 0x03) return false; // end of attributes
    if (tag <= 0x05) continue;     // a group delimiter
    if (i + 2 > len) return false;
    size_t ln = (size_t)((r[i] << 8) | r[i + 1]);
    i += 2;
    if (i + ln + 2 > len) return false;
    const uint8_t *name = r + i;
    i += ln;
    size_t lv = (size_t)((r[i] << 8) | r[i + 1]);
    i += 2;
    if (i + lv > len) return false;
    if (ln == sizeof(want) - 1 && memcmp(name, want, ln) == 0 && (tag == 0x41 || tag == 0x42 || tag == 0x44)) {
      char tmp[NB_NAME_MAX * 2];
      size_t c = lv < sizeof(tmp) - 1 ? lv : sizeof(tmp) - 1;
      memcpy(tmp, r + i, c);
      tmp[c] = 0;
      clean_name(tmp, out, n);
      return out[0] != 0;
    }
    i += lv;
  }
  return false;
}

bool nb_service_split(const char *svc, char *service, char *proto, size_t n) {
  if (n < 17 || !svc || !service || !proto) return false;
  service[0] = proto[0] = 0;
  const char *dot = strchr(svc, '.');
  if (svc[0] != '_' || !dot || dot - svc < 2 || dot - svc > 16) return false;
  for (const char *c = svc + 1; c < dot; c++)
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) return false;
  if (strcmp(dot + 1, "_tcp") != 0 && strcmp(dot + 1, "_udp") != 0) return false;
  memcpy(service, svc, (size_t)(dot - svc));
  service[dot - svc] = 0;
  memcpy(proto, dot + 1, 5);
  return true;
}
