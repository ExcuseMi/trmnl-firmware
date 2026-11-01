#include <app_api.h>
#include <ctype.h>
#include <modem_http.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool starts_nocase(const char *s, const char *prefix) {
  for (; *prefix; s++, prefix++)
    if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
  return true;
}

bool modem_url_parse(const char *url, modem_url_t *out) {
  if (!url || !out) return false;
  memset(out, 0, sizeof(*out));
  const char *p;
  if (starts_nocase(url, "https://")) {
    out->tls = true;
    out->port = 443;
    p = url + 8;
  } else if (starts_nocase(url, "http://")) {
    out->port = 80;
    p = url + 7;
  } else {
    return false;
  }
  size_t host_len = strcspn(p, ":/?#");
  if (host_len == 0 || host_len >= MODEM_HOST_MAX) return false;
  for (size_t i = 0; i < host_len; i++) {
    unsigned char c = (unsigned char)p[i];
    if (c <= ' ' || c == '@' || c == '"' || c == '\\' || c >= 0x7f) return false;
  }
  memcpy(out->host, p, host_len);
  out->host[host_len] = '\0';
  p += host_len;
  if (*p == ':') {
    p++;
    unsigned long port = 0;
    const char *d = p;
    while (*p >= '0' && *p <= '9') {
      port = port * 10 + (unsigned long)(*p - '0');
      if (port > 65535) return false;
      p++;
    }
    if (p == d || port == 0 || (*p && *p != '/' && *p != '?' && *p != '#')) return false;
    out->port = (uint16_t)port;
  }
  out->path = p;
  for (const char *c = p; *c; c++)
    if ((unsigned char)*c <= ' ' || (unsigned char)*c == 0x7f) return false; // no space, CR or LF in the request line
  return true;
}

// The headers the request sets itself
static bool reserved(const char *name, size_t n) {
  static const char *const own[] = {"host", "content-length", "connection"};
  for (const char *o : own)
    if (strlen(o) == n && starts_nocase(name, o)) return true;
  return false;
}

typedef struct {
  char *buf;
  size_t len, at;
  bool over;
} out_t;

static void put(out_t *o, const char *s, size_t n) {
  if (o->over || n >= o->len - o->at) {
    o->over = true;
    return;
  }
  memcpy(o->buf + o->at, s, n);
  o->at += n;
  o->buf[o->at] = '\0';
}

static void puts_(out_t *o, const char *s) { put(o, s, strlen(s)); }

size_t modem_http_head(char *buf, size_t len, const char *method, const modem_url_t *u, const char *const *headers,
                       size_t body_len) {
  if (!buf || len == 0 || !u) return 0;
  out_t o = {buf, len, 0, false};
  const char *m = method && method[0] ? method : "GET";
  char tmp[48];
  puts_(&o, m);
  puts_(&o, " ");
  const char *path = u->path ? u->path : "";
  if (path[0] != '/') puts_(&o, "/"); // "" or "?q" without a path
  put(&o, path, strcspn(path, "#")); // a fragment is not sent
  puts_(&o, " HTTP/1.1\r\nHost: ");
  puts_(&o, u->host);
  if (u->port != (u->tls ? 443 : 80)) {
    snprintf(tmp, sizeof(tmp), ":%u", (unsigned)u->port);
    puts_(&o, tmp);
  }
  puts_(&o, "\r\nUser-Agent: TRMNL-X\r\nConnection: close\r\n");
  bool length = body_len > 0 || strcmp(m, "POST") == 0 || strcmp(m, "PUT") == 0 || strcmp(m, "PATCH") == 0;
  if (length) {
    snprintf(tmp, sizeof(tmp), "Content-Length: %lu\r\n", (unsigned long)body_len);
    puts_(&o, tmp);
  }
  for (const char *const *h = headers; h && *h; h++) {
    const char *colon = strchr(*h, ':');
    if (!colon || colon == *h || colon - *h >= 64) continue;
    if (strpbrk(*h, "\r\n")) continue;
    if (reserved(*h, (size_t)(colon - *h))) continue;
    puts_(&o, *h);
    puts_(&o, "\r\n");
  }
  puts_(&o, "\r\n");
  return o.over ? 0 : o.at;
}

size_t modem_send_chunk(size_t sent, size_t total) {
  if (sent >= total) return 0;
  size_t left = total - sent;
  return left < MODEM_SEND_MAX ? left : MODEM_SEND_MAX;
}

void modem_resp_init(modem_resp_t *r, void *buf, size_t buf_len, bool head_only) {
  memset(r, 0, sizeof(*r));
  r->state = MODEM_RESP_STATUS;
  r->content_length = -1;
  r->buf = (uint8_t *)buf;
  r->buf_len = buf ? buf_len : 0;
  r->head_only = head_only;
}

static void body_bytes(modem_resp_t *r, const uint8_t *d, size_t n) {
  r->body_seen += n;
  size_t room = r->buf_len - r->got;
  size_t take = n < room ? n : room;
  if (take) memcpy(r->buf + r->got, d, take);
  r->got += take;
  if (take < n) r->truncated = true;
}

// After the blank line that ends the headers
static void headers_done(modem_resp_t *r) {
  if (r->status >= 100 && r->status < 200) { // 1xx: the real response follows
    r->state = MODEM_RESP_STATUS;
    r->chunked = false;
    r->content_length = -1;
    return;
  }
  if (r->head_only || r->status == 204 || r->status == 304) r->state = MODEM_RESP_DONE;
  else if (r->chunked) r->state = MODEM_RESP_CHUNK_SIZE;
  else if (r->content_length == 0) r->state = MODEM_RESP_DONE;
  else r->state = MODEM_RESP_BODY;
}

static void header_line(modem_resp_t *r, const char *l) {
  if (starts_nocase(l, "content-length:")) {
    const char *v = l + 15;
    while (*v == ' ' || *v == '\t') v++;
    char *end;
    long long n = strtoll(v, &end, 10);
    if (end != v && n >= 0) r->content_length = n;
  } else if (starts_nocase(l, "transfer-encoding:")) {
    const char *v = l + 18;
    while (*v == ' ' || *v == '\t') v++;
    if (starts_nocase(v, "chunked")) r->chunked = true;
  }
}

// One CRLF-ended line in a state that reads lines
static void line_done(modem_resp_t *r) {
  size_t n = r->line_len < sizeof(r->line) ? r->line_len : sizeof(r->line) - 1;
  if (n > 0 && r->line[n - 1] == '\r') n--;
  r->line[n] = '\0';
  r->line_len = 0;
  const char *l = r->line;
  switch (r->state) {
  case MODEM_RESP_STATUS: {
    int major = 0, minor = 0, code = 0;
    if (sscanf(l, "HTTP/%d.%d %d", &major, &minor, &code) != 3 || code < 100 || code > 999) {
      r->state = MODEM_RESP_BAD;
      return;
    }
    r->status = code;
    r->state = MODEM_RESP_HEADERS;
    return;
  }
  case MODEM_RESP_HEADERS:
    if (n == 0) headers_done(r);
    else header_line(r, l);
    return;
  case MODEM_RESP_CHUNK_SIZE: {
    char *end;
    unsigned long long size = strtoull(l, &end, 16);
    if (end == l) {
      r->state = MODEM_RESP_BAD;
      return;
    }
    r->chunk_left = size;
    r->state = size ? MODEM_RESP_CHUNK_DATA : MODEM_RESP_TRAILER;
    return;
  }
  case MODEM_RESP_CHUNK_END:
    r->state = n == 0 ? MODEM_RESP_CHUNK_SIZE : MODEM_RESP_BAD;
    return;
  case MODEM_RESP_TRAILER:
    if (n == 0) r->state = MODEM_RESP_DONE;
    return;
  default: return;
  }
}

void modem_resp_feed(modem_resp_t *r, const uint8_t *data, size_t len) {
  size_t i = 0;
  while (i < len && r->state != MODEM_RESP_DONE && r->state != MODEM_RESP_BAD) {
    if (r->state == MODEM_RESP_BODY) {
      size_t n = len - i;
      if (r->content_length >= 0) {
        unsigned long long left = (unsigned long long)r->content_length - r->body_seen;
        if (n > left) n = (size_t)left;
      }
      body_bytes(r, data + i, n);
      i += n;
      if (r->content_length >= 0 && r->body_seen >= (unsigned long long)r->content_length) r->state = MODEM_RESP_DONE;
      continue;
    }
    if (r->state == MODEM_RESP_CHUNK_DATA) {
      size_t n = len - i;
      if (n > r->chunk_left) n = (size_t)r->chunk_left;
      body_bytes(r, data + i, n);
      i += n;
      r->chunk_left -= n;
      if (r->chunk_left == 0) r->state = MODEM_RESP_CHUNK_END;
      continue;
    }
    char c = (char)data[i++];
    if (c == '\n') {
      line_done(r);
      continue;
    }
    if (r->line_len < sizeof(r->line) - 1) r->line[r->line_len] = c;
    r->line_len++;
    if (r->line_len > sizeof(r->line) - 1) r->line_len = sizeof(r->line) - 1; // cut, still one line
  }
}

bool modem_resp_closed(modem_resp_t *r) {
  if (r->state == MODEM_RESP_BODY && r->content_length < 0) r->state = MODEM_RESP_DONE;
  return r->state == MODEM_RESP_DONE;
}

bool modem_resp_enough(const modem_resp_t *r) {
  if (r->state == MODEM_RESP_DONE) return true;
  bool in_body = r->state == MODEM_RESP_BODY || r->state == MODEM_RESP_CHUNK_SIZE ||
                 r->state == MODEM_RESP_CHUNK_DATA || r->state == MODEM_RESP_CHUNK_END;
  return in_body && r->truncated;
}

int modem_http_app_error(modem_http_fail_t f) {
  switch (f) {
  case MODEM_HTTP_OK: return 0;
  case MODEM_HTTP_NO_MODEM: return APP_ENONET;
  case MODEM_HTTP_BAD_URL:
  case MODEM_HTTP_HEAD_TOO_BIG: return APP_EINVAL;
  case MODEM_HTTP_CONNECT_FAILED:
  case MODEM_HTTP_SEND_FAILED:
  case MODEM_HTTP_TIMEOUT:
  case MODEM_HTTP_BAD_RESPONSE: return APP_ETIMEDOUT;
  }
  return APP_ETIMEDOUT;
}

const char *modem_http_fail_name(modem_http_fail_t f) {
  switch (f) {
  case MODEM_HTTP_OK: return "ok";
  case MODEM_HTTP_NO_MODEM: return "modem not joined";
  case MODEM_HTTP_BAD_URL: return "bad URL";
  case MODEM_HTTP_HEAD_TOO_BIG: return "headers too long";
  case MODEM_HTTP_CONNECT_FAILED: return "connect failed (DNS, refused or TLS)";
  case MODEM_HTTP_SEND_FAILED: return "send failed";
  case MODEM_HTTP_TIMEOUT: return "timed out";
  case MODEM_HTTP_BAD_RESPONSE: return "bad or no response";
  }
  return "?";
}
