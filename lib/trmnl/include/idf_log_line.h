// ESP-IDF's own error and warning lines, kept in the event log (pure part; src/serial_log_hw.cpp hooks the vprintf).
// While the X is a USB host its serial port has no reader: what the USB stack says about a hub port or a failing
// transfer is lost unless it goes into the event log too (owner's X 2026-11-02: the tablet left mid-drawing, no reason).
#pragma once

#include <stddef.h>
#include <stdint.h>

#define IDF_LOG_LINE_MAX 80 // the event log's record holds 100; "idf: " and a count go in front
#define IDF_LOG_LINES_PER_MIN 20 // more in a minute are counted, not kept

// `line` as esp_log printed it ("E (12345) TAG: text\n", colours off). True for an E or W line: `out` gets
// "E TAG: text" without the timestamp and newline, cut at IDF_LOG_LINE_MAX. Other levels and malformed lines: false.
bool idf_log_line_keep(const char *line, char *out, size_t cap);

typedef struct {
  uint32_t minute_at; // ms: the start of the current minute
  unsigned kept; // in it
  unsigned dropped; // in it, past the limit
} idf_log_rate_t;

// Whether a kept line may go into the event log now; `dropped_before` gets how many were held back in the previous
// minute (0 when none or when the minute goes on), to be said once
bool idf_log_rate_allow(idf_log_rate_t *r, uint32_t now_ms, unsigned *dropped_before);
