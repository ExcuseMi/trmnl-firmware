#include <modem_at_rx.h>
#include <string.h>

static const char IPD[] = "+IPD,";
#define IPD_LEN 5
#define IPD_LEN_MAX 1000000u // ESP-AT sends at most a few kB per frame; more is a framing error

void modem_at_rx_init(modem_at_rx_t *rx) {
  memset(rx, 0, sizeof(*rx));
  rx->link = -1;
}

static void emit_line(modem_at_rx_t *rx, const modem_at_sink_t *sink) {
  size_t n = rx->line_len;
  if (n > MODEM_AT_LINE_MAX - 1) n = MODEM_AT_LINE_MAX - 1;
  while (n > 0 && rx->line[n - 1] == '\r') n--;
  rx->line[n] = '\0';
  rx->line_len = 0;
  if (n > 0 && sink->on_line) sink->on_line(sink->ctx, rx->line);
}

// "+IPD,<digits>" or "+IPD,<link>,<digits>" in the line so far: the frame's length (and `link`, -1 when none),
// or 0 when it is not one
static uint32_t ipd_length(const modem_at_rx_t *rx, int *link) {
  if (rx->line_len <= IPD_LEN || rx->line_len >= MODEM_AT_LINE_MAX || memcmp(rx->line, IPD, IPD_LEN) != 0) return 0;
  uint32_t v = 0;
  int digits = 0;
  *link = -1;
  for (size_t i = IPD_LEN; i < rx->line_len; i++) {
    char c = rx->line[i];
    if (c == ',' && *link < 0 && digits > 0 && digits <= 2) {
      *link = (int)v;
      v = 0;
      digits = 0;
      continue;
    }
    if (c < '0' || c > '9') return 0;
    v = v * 10u + (uint32_t)(c - '0');
    digits++;
    if (v > IPD_LEN_MAX) return 0;
  }
  return digits ? v : 0;
}

void modem_at_rx_feed(modem_at_rx_t *rx, const uint8_t *data, size_t len, const modem_at_sink_t *sink) {
  size_t i = 0;
  while (i < len) {
    if (rx->payload_left > 0) {
      size_t n = len - i;
      if (n > rx->payload_left) n = rx->payload_left;
      if (sink->on_payload) sink->on_payload(sink->ctx, data + i, n);
      rx->payload_left -= (uint32_t)n;
      i += n;
      continue;
    }
    char c = (char)data[i++];
    if (c == '\n') {
      emit_line(rx, sink);
      continue;
    }
    if (c == '>' && rx->line_len == 0) {
      if (sink->on_prompt) sink->on_prompt(sink->ctx);
      continue;
    }
    if (c == ':' && rx->line_len > IPD_LEN && rx->line_len < MODEM_AT_LINE_MAX &&
        memcmp(rx->line, IPD, IPD_LEN) == 0) {
      int link;
      uint32_t n = ipd_length(rx, &link);
      if (n > 0) {
        rx->payload_left = n;
        rx->link = link;
        rx->line_len = 0;
        continue;
      }
      rx->bad_frames++;
    }
    if (rx->line_len < MODEM_AT_LINE_MAX) rx->line[rx->line_len] = c;
    rx->line_len++; // counted past the end: the line is cut, not merged with the next
    if (rx->line_len > MODEM_AT_LINE_MAX) rx->line_len = MODEM_AT_LINE_MAX;
  }
}
