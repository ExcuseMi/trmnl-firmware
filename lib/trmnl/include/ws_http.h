#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// One HTTP request of the Wi-Fi page as the page's handlers (ws_page.h) see it, whatever carries it: AsyncWebServer
// on the S3's own 2.4 GHz radio (src/webshare_async.cpp) or the ESP32-C5 modem's TCP server on a 5 GHz network
// (ws_msrv.h). Each handler answers once with send or send_stream.

enum { WS_GET = 1, WS_POST = 2 };

typedef struct {
  const char *name, *value;
} ws_header_t;

// Fills up to `cap` bytes of a streamed body: the count, 0 at the end, < 0 to cut the connection (the phone then
// sees a failed download, never a short file as complete)
typedef int (*ws_fill_fn)(void *ctx, uint8_t *buf, size_t cap);

class WsRequest {
public:
  virtual ~WsRequest() {}
  virtual int method() = 0;
  virtual const char *path() = 0;
  virtual const void *id() = 0; // the same for every call about one request (upload ownership)
  virtual const char *header(const char *name) = 0; // nullptr when absent; case does not matter
  // A query (`post` false) or form body (`post` true) parameter, decoded, cut to `cap` - 1 bytes; false when absent
  virtual bool param(const char *name, bool post, char *out, size_t cap) = 0;
  virtual size_t content_length() = 0;
  virtual void *&temp() = 0; // per-request state, freed with free() when the request ends
  // Called once when the connection ends (done or cut); one per request
  virtual void on_disconnect(void (*fn)(void *ctx), void *ctx) = 0;
  // `body` is copied. `type` nullptr: no body (len 0).
  virtual void send(int code, const char *type, const char *body, size_t len, const ws_header_t *h, int nh) = 0;
  // `len` < 0: the length is unknown (chunked or until the connection closes)
  virtual void send_stream(int code, const char *type, long long len, ws_fill_fn fill, void *ctx, const ws_header_t *h,
                           int nh) = 0;
};

// A route of the page. `done` answers once the request (and its body) arrived. `body`: raw body pieces (any content
// type but a form); `file`: one file part of a multipart/form-data body. Both get the pieces in order.
typedef struct {
  int method;
  const char *path;
  void (*done)(WsRequest &r);
  void (*body)(WsRequest &r, const uint8_t *data, size_t len, size_t index, size_t total);
  void (*file)(WsRequest &r, const char *filename, size_t index, const uint8_t *data, size_t len, bool final);
} ws_route_t;

// The route for a request as AsyncWebServer picks it: the first of the method whose path is the request's or a
// prefix of it followed by "/"; nullptr: not found
const ws_route_t *ws_route_find(const ws_route_t *routes, int n, int method, const char *path);

// Decodes a "%41+b" form or query value into `out` (cut to `cap` - 1 bytes)
void ws_url_decode(const char *in, size_t len, char *out, size_t cap);
// Looks `name` up in "a=1&b=2" (a query or a urlencoded body of `len` bytes)
bool ws_form_param(const char *qs, size_t len, const char *name, char *out, size_t cap);
