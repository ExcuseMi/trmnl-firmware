#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The Wi-Fi page's server on the ESP32-C5 modem (a saved 5 GHz network): ESP-AT runs the TCP server (AT+CIPMUX=1,
// AT+CIPSERVER=1,80) and reports "<link>,CONNECT", "+IPD,<link>,<len>:<data>" and "<link>,CLOSED"; this side parses
// each link's HTTP request, runs the page's handler (ws_page.h) and answers through the transport (AT+CIPSEND=<link>).
// One request per connection (Connection: close). Pure: src/webshare_modem.cpp turns AT traffic into these calls.

#define WS_MSRV_LINKS 5 // ESP-AT's AT+CIPSERVERMAXCONN limit, link ids 0..4
#define WS_MSRV_CHUNK 4096 // bytes per AT+CIPSEND and per upload write
#define WS_MSRV_FORM_MAX 16384 // a urlencoded body (the settings form); more: 413
#define WS_MSRV_IDLE_MS 20000 // a request that stops arriving is cut

typedef struct {
  // Sends `len` (<= WS_MSRV_CHUNK) bytes on `link`; false: the link is gone. May call ws_msrv_data/closed for other
  // links (and this one) while it waits.
  bool (*send)(void *ctx, int link, const uint8_t *data, size_t len);
  void (*close)(void *ctx, int link);
  uint32_t (*now)(void *ctx);
  void *ctx;
} ws_mtransport_t;

class WsLink;
typedef struct {
  const ws_mtransport_t *t;
  WsLink *links[WS_MSRV_LINKS];
  uint32_t refused; // link ids out of range, or no memory
} ws_msrv_t;

void ws_msrv_init(ws_msrv_t *s, const ws_mtransport_t *t);
void ws_msrv_connect(ws_msrv_t *s, int link);
void ws_msrv_closed(ws_msrv_t *s, int link); // the phone closed it (or the modem did)
void ws_msrv_data(ws_msrv_t *s, int link, const uint8_t *data, size_t len);
// Runs the handlers of complete requests and sends one chunk of each answer; cuts requests idle too long.
// True when a link has work left (call again soon).
bool ws_msrv_poll(ws_msrv_t *s);
int ws_msrv_open_links(const ws_msrv_t *s);
void ws_msrv_end(ws_msrv_t *s); // every link closed and freed (stop)
