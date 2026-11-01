// One HTTP request per modem link (ws_msrv.h): parse, run the page's handler, answer in chunks, close.
#include "ws_msrv.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ws_http_parse.h"
#include "ws_multipart.h"
#include "ws_page.h"

enum link_state { L_RECV, L_READY, L_SEND, L_DONE };
enum body_mode { B_NONE, B_DISCARD, B_FORM, B_RAW, B_MULTIPART };

static const char *reason(int code) {
  switch (code) {
  case 200: return "OK";
  case 302: return "Found";
  case 400: return "Bad Request";
  case 401: return "Unauthorized";
  case 403: return "Forbidden";
  case 404: return "Not Found";
  case 408: return "Request Timeout";
  case 409: return "Conflict";
  case 411: return "Length Required";
  case 413: return "Payload Too Large";
  case 414: return "URI Too Long";
  case 423: return "Locked";
  case 429: return "Too Many Requests";
  case 431: return "Request Header Fields Too Large";
  case 501: return "Not Implemented";
  case 503: return "Service Unavailable";
  case 507: return "Insufficient Storage";
  default: return code < 400 ? "OK" : "Error";
  }
}

class WsLink : public WsRequest {
public:
  ws_msrv_t *srv;
  int link;
  link_state state = L_RECV;
  bool busy = false, gone = false;
  int error = 0; // answer this status without a handler
  uint32_t last;
  ws_hp_t hp;
  const ws_route_t *route = nullptr;
  body_mode mode = B_NONE;
  ws_mp_t mp;
  char *form = nullptr;
  size_t form_len = 0;
  uint8_t *stage = nullptr; // body pieces gathered to WS_MSRV_CHUNK, then the answer's stream chunks
  size_t stage_len = 0, index = 0;
  char filename[WS_MP_FILENAME_MAX];
  bool in_file = false;
  void *temp_ = nullptr;
  void (*disc)(void *) = nullptr;
  void *disc_ctx = nullptr;
  // the answer
  bool answered = false;
  char *out = nullptr; // head (and a body from send)
  size_t out_len = 0, out_pos = 0;
  ws_fill_fn fill = nullptr;
  void *fill_ctx = nullptr;
  long long stream_len = -1, streamed = 0;

  WsLink(ws_msrv_t *s, int l) : srv(s), link(l) {
    ws_hp_init(&hp);
    last = now();
  }
  ~WsLink() {
    if (disc) disc(disc_ctx);
    free(temp_);
    free(form);
    free(stage);
    free(out);
  }
  uint32_t now() { return srv->t->now(srv->t->ctx); }

  // ---- WsRequest ----
  int method() override { return hp.method; }
  const char *path() override { return hp.target; }
  const void *id() override { return this; }
  const char *header(const char *name) override { return ws_hp_header(&hp, name); }
  bool param(const char *name, bool post, char *o, size_t cap) override {
    if (post) return ws_form_param(form, form_len, name, o, cap);
    const char *q = ws_hp_query(&hp);
    return ws_form_param(q, strlen(q), name, o, cap);
  }
  size_t content_length() override { return hp.content_length > 0 ? (size_t)hp.content_length : 0; }
  void *&temp() override { return temp_; }
  void on_disconnect(void (*fn)(void *), void *ctx) override {
    disc = fn;
    disc_ctx = ctx;
  }

  bool head(int code, const char *type, long long len, const ws_header_t *h, int nh, size_t body_room) {
    size_t cap = 160 + body_room + (type ? strlen(type) : 0);
    for (int i = 0; i < nh; i++) cap += strlen(h[i].name) + strlen(h[i].value) + 4;
    out = (char *)malloc(cap);
    if (!out) return false;
    int n = snprintf(out, cap, "HTTP/1.1 %d %s\r\n", code, reason(code));
    if (type) n += snprintf(out + n, cap - n, "Content-Type: %s\r\n", type);
    if (len >= 0) n += snprintf(out + n, cap - n, "Content-Length: %lld\r\n", len);
    for (int i = 0; i < nh; i++) n += snprintf(out + n, cap - n, "%s: %s\r\n", h[i].name, h[i].value);
    n += snprintf(out + n, cap - n, "Connection: close\r\n\r\n");
    out_len = (size_t)n;
    answered = true;
    return true;
  }
  void send(int code, const char *type, const char *body, size_t len, const ws_header_t *h, int nh) override {
    if (answered) return;
    if (!head(code, type, (long long)len, h, nh, len)) return;
    memcpy(out + out_len, body, len);
    out_len += len;
  }
  void send_stream(int code, const char *type, long long len, ws_fill_fn f, void *ctx, const ws_header_t *h,
                   int nh) override {
    if (answered) return;
    free(stage);
    stage = (uint8_t *)malloc(WS_MSRV_CHUNK);
    if (!stage || !head(code, type, len, h, nh, 0)) return;
    fill = f;
    fill_ctx = ctx;
    stream_len = len;
  }

  // ---- receiving ----
  void flush_body(bool final) {
    if (!route) return;
    if (mode == B_RAW && (stage_len || final)) {
      if (stage_len) route->body(*this, stage, stage_len, index, content_length());
      index += stage_len;
    } else if (mode == B_MULTIPART && in_file) {
      route->file(*this, filename, index, stage, stage_len, final);
      index += stage_len;
    }
    stage_len = 0;
  }
  void gather(const uint8_t *d, size_t n) {
    while (n) {
      size_t c = WS_MSRV_CHUNK - stage_len < n ? WS_MSRV_CHUNK - stage_len : n;
      memcpy(stage + stage_len, d, c);
      stage_len += c;
      d += c;
      n -= c;
      if (stage_len == WS_MSRV_CHUNK) flush_body(false);
    }
  }
  void fail(int code) {
    if (state != L_RECV) return;
    error = code;
    state = L_READY;
  }
  static void on_head(void *ctx) {
    WsLink *l = (WsLink *)ctx;
    const ws_route_t *rt;
    int n = ws_page_routes(&rt);
    l->route = ws_route_find(rt, n, l->hp.method, l->hp.target);
    bool body = l->hp.chunked || l->hp.content_length > 0;
    const char *ct = l->header("Content-Type");
    bool is_form = ct && strncmp(ct, "application/x-www-form-urlencoded", 33) == 0;
    if (!body) l->mode = B_NONE;
    else if (!l->route) l->mode = B_DISCARD;
    else if (l->route->file && ws_mp_init(&l->mp, ct)) l->mode = B_MULTIPART;
    else if (l->route->body && !is_form) l->mode = B_RAW;
    else l->mode = B_FORM;
    if (l->mode == B_RAW && l->hp.chunked) return l->fail(411); // an upload's size is checked before it starts
    if (l->mode == B_FORM && l->hp.content_length > WS_MSRV_FORM_MAX) return l->fail(413);
    if (l->mode == B_RAW || l->mode == B_MULTIPART) {
      l->stage = (uint8_t *)malloc(WS_MSRV_CHUNK);
      if (!l->stage) return l->fail(503);
    }
  }
  static void on_body(void *ctx, const uint8_t *d, size_t n) {
    WsLink *l = (WsLink *)ctx;
    if (l->state != L_RECV) return;
    if (l->mode == B_RAW) l->gather(d, n);
    else if (l->mode == B_MULTIPART) {
      static const ws_mp_sink_t mps = {on_part, on_part_data, on_part_end, nullptr};
      ws_mp_sink_t s = mps;
      s.ctx = l;
      ws_mp_feed(&l->mp, d, n, &s);
    } else if (l->mode == B_FORM) {
      if (l->form_len + n > WS_MSRV_FORM_MAX) return l->fail(413);
      char *f = (char *)realloc(l->form, l->form_len + n + 1);
      if (!f) return l->fail(503);
      l->form = f;
      memcpy(f + l->form_len, d, n);
      l->form_len += n;
      f[l->form_len] = '\0';
    }
  }
  static void on_part(void *ctx, const char *, const char *file) {
    WsLink *l = (WsLink *)ctx;
    l->in_file = file[0] != '\0';
    snprintf(l->filename, sizeof(l->filename), "%s", file);
    l->index = 0;
    l->stage_len = 0;
  }
  static void on_part_data(void *ctx, const uint8_t *d, size_t n) {
    WsLink *l = (WsLink *)ctx;
    if (l->in_file) l->gather(d, n);
  }
  static void on_part_end(void *ctx) {
    WsLink *l = (WsLink *)ctx;
    if (l->in_file) l->flush_body(true);
    l->in_file = false;
  }
  static void on_done(void *ctx) {
    WsLink *l = (WsLink *)ctx;
    if (l->state != L_RECV) return;
    if (l->mode == B_RAW) l->flush_body(true);
    if (l->mode == B_MULTIPART && (!ws_mp_done(&l->mp) || ws_mp_failed(&l->mp))) {
      if (l->in_file) l->flush_body(true); // cut short: the upload sees the end and checks the size
      l->in_file = false;
    }
    l->state = L_READY;
  }
  void feed(const uint8_t *d, size_t n) {
    if (state != L_RECV) return; // one request per connection: what follows is dropped
    last = now();
    static const ws_hp_sink_t hs = {on_head, on_body, on_done, nullptr};
    ws_hp_sink_t s = hs;
    s.ctx = this;
    ws_hp_feed(&hp, d, n, &s);
    if (hp.state == WS_HP_ERROR) fail(hp.error);
  }

  // ---- answering; false when the link is finished ----
  bool step() {
    if (state == L_RECV) {
      if (now() - last >= WS_MSRV_IDLE_MS) return false; // stopped arriving
      return true;
    }
    if (state == L_READY) {
      if (error) {
        char body[48];
        int n = snprintf(body, sizeof(body), "{\"error\":\"%s\"}", reason(error));
        send(error, "application/json", body, (size_t)n, nullptr, 0);
      } else if (route) {
        route->done(*this);
      } else {
        ws_page_not_found(*this);
      }
      if (!answered) send(500, nullptr, "", 0, nullptr, 0);
      if (!answered) return false; // no memory for the answer
      state = L_SEND;
      return true;
    }
    if (state != L_SEND) return false;
    const ws_mtransport_t *t = srv->t;
    if (out_pos < out_len) {
      size_t n = out_len - out_pos < WS_MSRV_CHUNK ? out_len - out_pos : WS_MSRV_CHUNK;
      if (!t->send(t->ctx, link, (const uint8_t *)out + out_pos, n)) return false;
      out_pos += n;
      return out_pos < out_len || fill ? true : (state = L_DONE, false);
    }
    if (!fill) return false;
    size_t cap = WS_MSRV_CHUNK;
    if (stream_len >= 0 && stream_len - streamed < (long long)cap) cap = (size_t)(stream_len - streamed);
    int n = cap ? fill(fill_ctx, stage, cap) : 0;
    if (n < 0) return false; // cut: the phone sees a failed download
    if (n == 0) {
      if (stream_len >= 0 && streamed < stream_len) return false; // shorter than announced: cut, never complete
      state = L_DONE;
      return false;
    }
    if (!t->send(t->ctx, link, stage, (size_t)n)) return false;
    streamed += n;
    return true;
  }
};

void ws_msrv_init(ws_msrv_t *s, const ws_mtransport_t *t) {
  memset(s, 0, sizeof(*s));
  s->t = t;
}

static void drop(ws_msrv_t *s, int link, bool close) {
  WsLink *l = s->links[link];
  if (!l) return;
  s->links[link] = nullptr;
  if (close) s->t->close(s->t->ctx, link);
  delete l; // calls the request's on_disconnect (an upload's temporary file goes)
}

void ws_msrv_connect(ws_msrv_t *s, int link) {
  if (link < 0 || link >= WS_MSRV_LINKS) {
    s->refused++;
    if (link >= 0) s->t->close(s->t->ctx, link);
    return;
  }
  if (s->links[link]) { // a CLOSED we missed: the old request is over
    if (s->links[link]->busy) {
      s->links[link]->gone = true;
      s->refused++;
      return;
    }
    drop(s, link, false);
  }
  s->links[link] = new (std::nothrow) WsLink(s, link);
  if (!s->links[link]) {
    s->refused++;
    s->t->close(s->t->ctx, link);
  }
}

void ws_msrv_closed(ws_msrv_t *s, int link) {
  if (link < 0 || link >= WS_MSRV_LINKS || !s->links[link]) return;
  if (s->links[link]->busy) s->links[link]->gone = true; // freed when the step returns
  else drop(s, link, false);
}

void ws_msrv_data(ws_msrv_t *s, int link, const uint8_t *data, size_t len) {
  if (link < 0 || link >= WS_MSRV_LINKS || !s->links[link] || s->links[link]->gone) return;
  s->links[link]->feed(data, len);
}

bool ws_msrv_poll(ws_msrv_t *s) {
  bool more = false;
  for (int i = 0; i < WS_MSRV_LINKS; i++) {
    WsLink *l = s->links[i];
    if (!l || l->busy) continue;
    l->busy = true;
    bool go_on = !l->gone && l->step();
    l->busy = false;
    if (l->gone) drop(s, i, false);
    else if (!go_on) drop(s, i, true); // answered (Connection: close), cut or idle
    else if (l->state != L_RECV) more = true;
  }
  return more;
}

int ws_msrv_open_links(const ws_msrv_t *s) {
  int n = 0;
  for (int i = 0; i < WS_MSRV_LINKS; i++) n += s->links[i] != nullptr;
  return n;
}

void ws_msrv_end(ws_msrv_t *s) {
  for (int i = 0; i < WS_MSRV_LINKS; i++) {
    if (s->links[i] && !s->links[i]->busy) drop(s, i, true);
  }
}
