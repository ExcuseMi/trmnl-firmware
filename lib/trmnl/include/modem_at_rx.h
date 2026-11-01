#pragma once

#include <stddef.h>
#include <stdint.h>

// Splits what the ESP32-C5 modem (ESP-AT) sends during a TCP/SSL link into lines ("OK", "SEND OK", "CLOSED",
// ...), the ">" prompt of AT+CIPSEND and the payload of "+IPD,<len>:<data>" frames (AT+CIPDINFO=0, active receive).
// A frame may arrive at any point, also between a command and its answer. Pure: lib/trmnl_x/src/modem_tcp.cpp feeds it.

#define MODEM_AT_LINE_MAX 128 // longer lines are cut (kept: the first 127 characters)

typedef struct {
  void (*on_line)(void *ctx, const char *line); // without CR/LF; empty lines are not passed
  void (*on_prompt)(void *ctx); // ">" at the start of a line
  void (*on_payload)(void *ctx, const uint8_t *data, size_t len);
  void *ctx;
} modem_at_sink_t;

typedef struct {
  char line[MODEM_AT_LINE_MAX];
  size_t line_len;
  uint32_t payload_left; // > 0: inside a +IPD frame
  int bad_frames; // "+IPD," with no valid length (counted, the line is then passed as a line)
} modem_at_rx_t;

void modem_at_rx_init(modem_at_rx_t *rx);
void modem_at_rx_feed(modem_at_rx_t *rx, const uint8_t *data, size_t len, const modem_at_sink_t *sink);
