#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// An HTTP/1.1 request as it arrives in pieces (the modem's +IPD frames, split anywhere): request line, headers, then
// a body by Content-Length or chunked transfer coding. Pure; ws_msrv.cpp feeds it one link's bytes.

#define WS_HP_TARGET_MAX 512 // path and query
#define WS_HP_LINE_MAX 1024 // one header line; a longer one is skipped (the request line: 414)
#define WS_HP_HEADERS_MAX 2048 // all kept headers; headers past it are skipped
#define WS_HP_HEADS_MAX 64 // header lines; more: 431

typedef enum {
  WS_HP_REQLINE,
  WS_HP_HEADERS,
  WS_HP_BODY,
  WS_HP_CHUNK_SIZE,
  WS_HP_CHUNK_DATA,
  WS_HP_CHUNK_END,
  WS_HP_TRAILER,
  WS_HP_DONE,
  WS_HP_ERROR,
} ws_hp_state_t;

typedef struct {
  void (*on_head)(void *ctx); // request line and headers complete (the body follows)
  void (*on_body)(void *ctx, const uint8_t *data, size_t len);
  void (*on_done)(void *ctx); // the whole request is in
  void *ctx;
} ws_hp_sink_t;

typedef struct {
  ws_hp_state_t state;
  int error; // WS_HP_ERROR: the status to answer (400, 414, 431, 501)
  int method; // WS_GET / WS_POST (ws_http.h), 0 other
  char target[WS_HP_TARGET_MAX];
  size_t path_len; // target[0..path_len) is the path, a query follows after '?'
  char headers[WS_HP_HEADERS_MAX]; // "name\0value\0" pairs
  size_t headers_len;
  int heads;
  long long content_length; // -1: none
  bool chunked;
  unsigned long long body_left, chunk_left, body_seen;
  char line[WS_HP_LINE_MAX];
  size_t line_len;
  bool line_over;
} ws_hp_t;

void ws_hp_init(ws_hp_t *p);
// Feeds bytes; returns how many it took (fewer only once DONE or ERROR: the rest is not this request's)
size_t ws_hp_feed(ws_hp_t *p, const uint8_t *data, size_t len, const ws_hp_sink_t *sink);
const char *ws_hp_header(const ws_hp_t *p, const char *name); // case does not matter; nullptr when absent
const char *ws_hp_query(const ws_hp_t *p); // after '?', "" when none
