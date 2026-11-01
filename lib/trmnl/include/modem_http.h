#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// An app's http_request over the ESP32-C5 modem (TRMNL X on a 5 GHz network, docs/apps.md "net"). ESP-AT's own
// HTTP commands cannot carry it: AT+HTTPCLIENT takes a body only inside a 256-byte command, AT+HTTPCPOST/PUT keep
// the whole body in the modem's heap and print no status code. So the S3 speaks HTTP/1.1 itself over a TCP or SSL
// link (AT+CIPSTART, AT+CIPSEND of at most MODEM_SEND_MAX bytes each). Pure parts: src/app_net_modem.cpp runs them.

#define MODEM_SEND_MAX 8192 // ESP-AT's AT+CIPSEND limit in normal transmission mode
#define MODEM_HOST_MAX 128
#define MODEM_HEAD_MAX 2048 // request line and headers

typedef struct {
  bool tls; // https
  char host[MODEM_HOST_MAX];
  uint16_t port;
  const char *path; // into the URL: "/..." or "" (then "/" is sent)
} modem_url_t;

// http:// or https://, host (no user info), optional port, path and query. False for anything else.
bool modem_url_parse(const char *url, modem_url_t *out);

// Request line and headers: Host, User-Agent, Connection: close, Content-Length (with a body or for POST/PUT/PATCH),
// then the app's "Name: value" headers (skipped like on the S3 path: no colon, name of 64 or more; also skipped:
// CR/LF inside, and Host, Content-Length, Connection, which the request sets). Returns its length, 0 if it does not
// fit in `len` (the caller answers APP_EINVAL).
size_t modem_http_head(char *buf, size_t len, const char *method, const modem_url_t *u, const char *const *headers,
                       size_t body_len);

// Bytes of the next AT+CIPSEND once `sent` of `total` are out (0: done)
size_t modem_send_chunk(size_t sent, size_t total);

typedef enum {
  MODEM_RESP_STATUS,
  MODEM_RESP_HEADERS,
  MODEM_RESP_BODY, // Content-Length or until the server closes
  MODEM_RESP_CHUNK_SIZE,
  MODEM_RESP_CHUNK_DATA,
  MODEM_RESP_CHUNK_END, // the CRLF after a chunk's data
  MODEM_RESP_TRAILER,
  MODEM_RESP_DONE,
  MODEM_RESP_BAD,
} modem_resp_state_t;

// The response, parsed as it arrives; the body goes straight into the app's buffer
typedef struct {
  modem_resp_state_t state;
  int status;
  bool chunked, head_only;
  long long content_length; // -1: not given
  unsigned long long body_seen; // body bytes so far (also past the buffer)
  unsigned long long chunk_left;
  uint8_t *buf;
  size_t buf_len, got;
  bool truncated;
  char line[512];
  size_t line_len;
} modem_resp_t;

void modem_resp_init(modem_resp_t *r, void *buf, size_t buf_len, bool head_only);
void modem_resp_feed(modem_resp_t *r, const uint8_t *data, size_t len);
// The server closed the link: a body without length ends here. True when the response is complete.
bool modem_resp_closed(modem_resp_t *r);
// Nothing more to read: complete, or the buffer is full and the rest only sets `truncated`
bool modem_resp_enough(const modem_resp_t *r);

typedef enum {
  MODEM_HTTP_OK,
  MODEM_HTTP_NO_MODEM, // modem missing or not joined
  MODEM_HTTP_BAD_URL,
  MODEM_HTTP_HEAD_TOO_BIG,
  MODEM_HTTP_CONNECT_FAILED, // DNS, refused, TLS handshake (CIPSTART answered ERROR)
  MODEM_HTTP_SEND_FAILED, // CIPSEND refused, SEND FAIL or no answer
  MODEM_HTTP_TIMEOUT, // no response, or it stopped arriving
  MODEM_HTTP_BAD_RESPONSE, // no status line before the link closed, or not HTTP
} modem_http_fail_t;

// The app-facing code, as on the S3 path: 0, APP_ENONET, APP_EINVAL, APP_ETIMEDOUT (DNS, TLS, transport)
int modem_http_app_error(modem_http_fail_t f);
const char *modem_http_fail_name(modem_http_fail_t f);
