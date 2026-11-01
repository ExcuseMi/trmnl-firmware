#include "ws_multipart.h"

#include <string.h>

enum { MP_DATA, MP_AFTER, MP_AFTER2, MP_HEADERS, MP_DONE, MP_BAD };

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static const char *find_ci(const char *s, const char *what) {
  size_t n = strlen(what);
  for (; *s; s++) {
    size_t k = 0;
    while (k < n && s[k] && lower(s[k]) == lower(what[k])) k++;
    if (k == n) return s;
  }
  return nullptr;
}

// `key="value"` (or key=value) after `at` into out
static void attr(const char *line, const char *key, char *out, size_t cap) {
  out[0] = '\0';
  for (const char *p = line; (p = find_ci(p, key)) != nullptr; p++) {
    char before = p == line ? ';' : p[-1];
    if (before != ' ' && before != ';' && before != '\t') continue;
    const char *v = p + strlen(key);
    size_t o = 0;
    if (*v == '"') {
      for (v++; *v && *v != '"' && o + 1 < cap; v++) out[o++] = *v;
    } else {
      for (; *v && *v != ';' && *v != ' ' && o + 1 < cap; v++) out[o++] = *v;
    }
    out[o] = '\0';
    return;
  }
}

bool ws_mp_init(ws_mp_t *m, const char *ct) {
  memset(m, 0, sizeof(*m));
  if (!ct || !find_ci(ct, "multipart/form-data")) return false;
  char b[WS_MP_BOUNDARY_MAX + 2];
  attr(ct, "boundary=", b, sizeof(b));
  size_t n = strlen(b);
  if (n == 0 || n > WS_MP_BOUNDARY_MAX) return false;
  memcpy(m->delim, "\r\n--", 4);
  memcpy(m->delim + 4, b, n + 1);
  m->dlen = n + 4;
  m->match = 2; // the body starts at a boundary: as if a CRLF came before it
  m->state = MP_DATA;
  return true;
}

static void flush(ws_mp_t *m, const ws_mp_sink_t *s) {
  if (m->out_len && m->in_part && s->on_data) s->on_data(s->ctx, m->out, m->out_len);
  m->out_len = 0;
}

static void emit(ws_mp_t *m, const uint8_t *d, size_t n, const ws_mp_sink_t *s) {
  if (!m->in_part) return; // the preamble
  for (size_t i = 0; i < n; i++) {
    if (m->out_len == sizeof(m->out)) flush(m, s);
    m->out[m->out_len++] = d[i];
  }
}

static void header_done(ws_mp_t *m, const ws_mp_sink_t *s) {
  size_t n = m->hdr_len;
  if (n && m->hdr[n - 1] == '\r') n--;
  m->hdr[n] = '\0';
  m->hdr_len = 0;
  if (n == 0) { // the part's body follows
    m->state = MP_DATA;
    m->in_part = true;
    if (s->on_part) s->on_part(s->ctx, m->name, m->filename);
    return;
  }
  if (find_ci(m->hdr, "content-disposition:") == m->hdr) {
    attr(m->hdr, "name=", m->name, sizeof(m->name));
    attr(m->hdr, "filename=", m->filename, sizeof(m->filename));
  }
}

void ws_mp_feed(ws_mp_t *m, const uint8_t *data, size_t len, const ws_mp_sink_t *s) {
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    switch (m->state) {
    case MP_DATA:
      if (c == m->delim[m->match]) {
        if (++m->match == m->dlen) {
          flush(m, s);
          if (m->in_part && s->on_part_end) s->on_part_end(s->ctx);
          m->in_part = false;
          m->match = 0;
          m->state = MP_AFTER;
        }
        break;
      }
      // the bytes matched so far were data; '\r' starts the delimiter only, so a new match can start only here
      emit(m, (const uint8_t *)m->delim, m->match, s);
      m->match = 0;
      if (c == m->delim[0]) m->match = 1;
      else emit(m, (const uint8_t *)&c, 1, s);
      break;
    case MP_AFTER: // "--" closes the body, CRLF starts the next part's headers
      if (c == '-') m->state = MP_AFTER2;
      else if (c == '\n') {
        m->state = MP_HEADERS;
        m->name[0] = m->filename[0] = '\0';
      } else if (c != '\r' && c != ' ' && c != '\t') m->state = MP_BAD;
      break;
    case MP_AFTER2:
      m->state = c == '-' ? MP_DONE : MP_BAD;
      break;
    case MP_HEADERS:
      if (c == '\n') header_done(m, s);
      else if (m->hdr_len < sizeof(m->hdr) - 1) m->hdr[m->hdr_len++] = c;
      break;
    default: return; // done (the epilogue is ignored) or bad
    }
  }
  flush(m, s);
}

bool ws_mp_done(const ws_mp_t *m) { return m->state == MP_DONE; }
bool ws_mp_failed(const ws_mp_t *m) { return m->state == MP_BAD; }
