// The Wi-Fi page on AsyncWebServer (the S3's own 2.4 GHz radio: the home network or the X's access point): each
// route of lib/trmnl/ws_page_* registered as it is, the request wrapped as a WsRequest (ws_http.h).
#ifdef BOARD_TRMNL_X

#include <captive_ap.h>
#include <string.h>

#include "webshare_internal.h"

class AsyncReq : public WsRequest {
  AsyncWebServerRequest *r;

public:
  explicit AsyncReq(AsyncWebServerRequest *req) : r(req) {}
  int method() override { return r->method() == HTTP_POST ? WS_POST : r->method() == HTTP_GET ? WS_GET : 0; }
  const char *path() override { return r->url().c_str(); }
  const void *id() override { return r; }
  const char *header(const char *name) override { return r->hasHeader(name) ? r->header(name).c_str() : nullptr; }
  bool param(const char *name, bool post, char *out, size_t cap) override {
    const AsyncWebParameter *p = r->getParam(name, post);
    if (!p) return false;
    snprintf(out, cap, "%s", p->value().c_str());
    return true;
  }
  size_t content_length() override { return r->contentLength(); }
  void *&temp() override { return r->_tempObject; }
  void on_disconnect(void (*fn)(void *), void *ctx) override {
    r->onDisconnect([fn, ctx]() { fn(ctx); });
  }
  static void add(AsyncWebServerResponse *res, const ws_header_t *h, int nh) {
    for (int i = 0; i < nh; i++) res->addHeader(h[i].name, h[i].value);
  }
  void send(int code, const char *type, const char *body, size_t len, const ws_header_t *h, int nh) override {
    (void)len; // bodies are text: `body` ends at len
    AsyncWebServerResponse *res = r->beginResponse(code, type ? type : "", body);
    add(res, h, nh);
    r->send(res);
  }
  void send_stream(int code, const char *type, long long len, ws_fill_fn fill, void *ctx, const ws_header_t *h,
                   int nh) override {
    AsyncWebServerRequest *req = r;
    auto filler = [fill, ctx, req](uint8_t *buf, size_t max, size_t) -> size_t {
      int n = fill(ctx, buf, max);
      if (n < 0) { // cut the connection so the phone sees a failed download
        req->client()->abort();
        return 0;
      }
      return (size_t)n;
    };
    AsyncWebServerResponse *res =
        len < 0 ? r->beginChunkedResponse(type, filler) : r->beginResponse(type, (size_t)len, filler);
    res->setCode(code);
    add(res, h, nh);
    r->send(res);
  }
};

static WebRequestMethod async_method(int m) { return m == WS_POST ? HTTP_POST : HTTP_GET; }

void webshare_async_routes(AsyncWebServer &server, bool ap, const char *ap_url) {
  if (ap) captive_ap_routes(server, ap_url);
  const ws_route_t *routes;
  int n = ws_page_routes(&routes);
  for (int i = 0; i < n; i++) {
    const ws_route_t *rt = &routes[i];
    ArRequestHandlerFunction done = [rt](AsyncWebServerRequest *r) {
      AsyncReq a(r);
      rt->done(a);
    };
    ArUploadHandlerFunction file = nullptr;
    ArBodyHandlerFunction body = nullptr;
    if (rt->file) {
      file = [rt](AsyncWebServerRequest *r, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
        AsyncReq a(r);
        rt->file(a, filename.c_str(), index, data, len, final);
      };
    }
    if (rt->body) {
      body = [rt](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
        AsyncReq a(r);
        rt->body(a, data, len, index, total);
      };
    }
    server.on(rt->path, async_method(rt->method), done, file, body);
  }
  server.onNotFound([](AsyncWebServerRequest *r) {
    AsyncReq a(r);
    ws_page_not_found(a);
  });
}

#endif // BOARD_TRMNL_X
