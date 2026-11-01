#include "ws_http_parse.h"

#include <string.h>

#include "ws_http.h"

void ws_hp_init(ws_hp_t *p) {
  memset(p, 0, sizeof(*p));
  p->content_length = -1;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static bool same_ci(const char *a, const char *b) {
  while (*a && *b && lower(*a) == lower(*b)) a++, b++;
  return *a == *b;
}

const char *ws_hp_header(const ws_hp_t *p, const char *name) {
  for (size_t i = 0; i < p->headers_len;) {
    const char *n = p->headers + i;
    const char *v = n + strlen(n) + 1;
    if (same_ci(n, name)) return v;
    i = (size_t)(v - p->headers) + strlen(v) + 1;
  }
  return nullptr;
}

const char *ws_hp_query(const ws_hp_t *p) {
  return p->path_len < sizeof(p->target) - 1 && p->target[p->path_len + 1] ? p->target + p->path_len + 1 : "";
}

static void fail(ws_hp_t *p, int code) {
  p->state = WS_HP_ERROR;
  p->error = code;
}

static void request_line(ws_hp_t *p, const char *l) {
  const char *sp1 = strchr(l, ' ');
  const char *sp2 = sp1 ? strchr(sp1 + 1, ' ') : nullptr;
  if (!sp1 || !sp2 || strncmp(sp2 + 1, "HTTP/1.", 7) != 0 || sp1[1] != '/') return fail(p, 400);
  size_t mlen = (size_t)(sp1 - l);
  p->method = mlen == 3 && !memcmp(l, "GET", 3) ? WS_GET : mlen == 4 && !memcmp(l, "POST", 4) ? WS_POST : 0;
  if (!p->method) return fail(p, 501);
  size_t tlen = (size_t)(sp2 - sp1 - 1);
  if (tlen + 2 > sizeof(p->target)) return fail(p, 414);
  memcpy(p->target, sp1 + 1, tlen);
  p->target[tlen] = '\0';
  p->target[tlen + 1] = '\0';
  char *q = strchr(p->target, '?');
  if (q) *q = '\0';
  p->path_len = strlen(p->target);
  p->state = WS_HP_HEADERS;
}

static bool parse_ull(const char *s, int base, unsigned long long *out) {
  unsigned long long v = 0;
  int digits = 0;
  while (*s == ' ' || *s == '\t') s++;
  for (; *s; s++) {
    int d = *s >= '0' && *s <= '9' ? *s - '0' : base == 16 && lower(*s) >= 'a' && lower(*s) <= 'f' ? lower(*s) - 'a' + 10 : -1;
    if (d < 0) break;
    if (v > (0xffffffffffffull - (unsigned)d) / (unsigned)base) return false; // far past any limit
    v = v * (unsigned)base + (unsigned)d;
    digits++;
  }
  while (*s == ' ' || *s == '\t') s++;
  if (base == 16 && *s == ';') s = ""; // chunk extensions are ignored
  *out = v;
  return digits > 0 && *s == '\0';
}

static void header_line(ws_hp_t *p, const char *l, const ws_hp_sink_t *sink) {
  if (!l[0]) { // end of the headers
    p->state = WS_HP_DONE;
    if (p->chunked) p->state = WS_HP_CHUNK_SIZE;
    else if (p->content_length > 0) {
      p->state = WS_HP_BODY;
      p->body_left = (unsigned long long)p->content_length;
    }
    if (sink->on_head) sink->on_head(sink->ctx);
    if (p->state == WS_HP_DONE && sink->on_done) sink->on_done(sink->ctx);
    return;
  }
  if (++p->heads > WS_HP_HEADS_MAX) return fail(p, 431);
  const char *colon = strchr(l, ':');
  if (!colon || colon == l) return fail(p, 400);
  size_t nlen = (size_t)(colon - l);
  const char *v = colon + 1;
  while (*v == ' ' || *v == '\t') v++;
  size_t vlen = strlen(v);
  while (vlen && (v[vlen - 1] == ' ' || v[vlen - 1] == '\t')) vlen--;
  char name[40];
  if (nlen < sizeof(name)) {
    memcpy(name, l, nlen);
    name[nlen] = '\0';
    char val[24];
    size_t c = vlen < sizeof(val) - 1 ? vlen : sizeof(val) - 1;
    memcpy(val, v, c);
    val[c] = '\0';
    if (same_ci(name, "Content-Length")) {
      unsigned long long n;
      if (vlen >= sizeof(val) || !parse_ull(val, 10, &n)) return fail(p, 400);
      if (p->content_length >= 0 && (unsigned long long)p->content_length != n) return fail(p, 400);
      p->content_length = (long long)n;
    } else if (same_ci(name, "Transfer-Encoding")) {
      if (!same_ci(val, "chunked")) return fail(p, 501);
      p->chunked = true;
    }
  }
  if (p->chunked && p->content_length >= 0) return fail(p, 400); // both: refused (request smuggling)
  if (p->headers_len + nlen + vlen + 2 > sizeof(p->headers)) return; // no room: skipped
  memcpy(p->headers + p->headers_len, l, nlen);
  p->headers[p->headers_len + nlen] = '\0';
  p->headers_len += nlen + 1;
  memcpy(p->headers + p->headers_len, v, vlen);
  p->headers[p->headers_len + vlen] = '\0';
  p->headers_len += vlen + 1;
}

static void line_done(ws_hp_t *p, const ws_hp_sink_t *sink) {
  size_t n = p->line_len;
  bool over = p->line_over;
  p->line_len = 0;
  p->line_over = false;
  if (n && p->line[n - 1] == '\r') n--;
  p->line[n] = '\0';
  switch (p->state) {
  case WS_HP_REQLINE:
    if (over) return fail(p, 414);
    if (!n) return; // a stray CRLF before a request is allowed
    return request_line(p, p->line);
  case WS_HP_HEADERS:
    if (over) return; // a header too long to keep: skipped
    return header_line(p, p->line, sink);
  case WS_HP_CHUNK_SIZE: {
    unsigned long long size;
    if (over || !parse_ull(p->line, 16, &size)) return fail(p, 400);
    p->chunk_left = size;
    p->state = size ? WS_HP_CHUNK_DATA : WS_HP_TRAILER;
    return;
  }
  case WS_HP_CHUNK_END:
    if (n) return fail(p, 400);
    p->state = WS_HP_CHUNK_SIZE;
    return;
  case WS_HP_TRAILER:
    if (n) return; // trailer fields are ignored
    p->state = WS_HP_DONE;
    if (sink->on_done) sink->on_done(sink->ctx);
    return;
  default: return;
  }
}

size_t ws_hp_feed(ws_hp_t *p, const uint8_t *data, size_t len, const ws_hp_sink_t *sink) {
  size_t i = 0;
  while (i < len && p->state != WS_HP_DONE && p->state != WS_HP_ERROR) {
    if (p->state == WS_HP_BODY || p->state == WS_HP_CHUNK_DATA) {
      unsigned long long *left = p->state == WS_HP_BODY ? &p->body_left : &p->chunk_left;
      size_t n = len - i;
      if (n > *left) n = (size_t)*left;
      if (sink->on_body) sink->on_body(sink->ctx, data + i, n);
      *left -= n;
      p->body_seen += n;
      i += n;
      if (*left == 0) {
        if (p->state == WS_HP_CHUNK_DATA) p->state = WS_HP_CHUNK_END;
        else {
          p->state = WS_HP_DONE;
          if (sink->on_done) sink->on_done(sink->ctx);
        }
      }
      continue;
    }
    char c = (char)data[i++];
    if (c == '\n') {
      line_done(p, sink);
      continue;
    }
    if (p->line_len < sizeof(p->line) - 1) p->line[p->line_len++] = c;
    else p->line_over = true;
  }
  return i;
}
