#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A multipart/form-data body (curl -F file=@app.elf) as it arrives in pieces, a boundary split anywhere. Pure; the
// modem path of the Wi-Fi page feeds it (AsyncWebServer parses its own).

#define WS_MP_BOUNDARY_MAX 70 // RFC 2046
#define WS_MP_NAME_MAX 64
#define WS_MP_FILENAME_MAX 96

typedef struct {
  void (*on_part)(void *ctx, const char *name, const char *filename); // filename "" for a plain field
  void (*on_data)(void *ctx, const uint8_t *data, size_t len);
  void (*on_part_end)(void *ctx);
  void *ctx;
} ws_mp_sink_t;

typedef struct {
  char delim[WS_MP_BOUNDARY_MAX + 5]; // "\r\n--" + boundary
  size_t dlen, match;
  int state;
  bool in_part;
  char hdr[512];
  size_t hdr_len;
  char name[WS_MP_NAME_MAX], filename[WS_MP_FILENAME_MAX];
  uint8_t out[256];
  size_t out_len;
} ws_mp_t;

// False when `content_type` is not multipart/form-data with a usable boundary
bool ws_mp_init(ws_mp_t *m, const char *content_type);
void ws_mp_feed(ws_mp_t *m, const uint8_t *data, size_t len, const ws_mp_sink_t *sink);
bool ws_mp_done(const ws_mp_t *m); // the closing boundary was seen
bool ws_mp_failed(const ws_mp_t *m); // a part's headers were bad
