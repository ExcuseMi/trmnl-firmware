#ifdef BOARD_TRMNL_X
#include "serial_log_hw.h"

#include <Arduino.h>
#include <esp_log.h>
#include <idf_log_line.h>
#include <serial_hold.h>
#include <stdio.h>

#include "evlog.h"

static size_t to_serial(void *, const uint8_t *b, size_t n) { return Serial.write(b, n); }

class HeldSerial : public Print {
public:
  size_t write(uint8_t c) override { return serial_hold_log(&c, 1, to_serial, nullptr); }
  size_t write(const uint8_t *b, size_t n) override { return serial_hold_log(b, n, to_serial, nullptr); }
};

static HeldSerial held;

Print &serial_log_out(void) { return held; }

static vprintf_like_t idf_prev;
static idf_log_rate_t rate;

// ESP-IDF's own E and W lines also go into the event log (idf_log_line.h): in USB host mode the serial port has no
// reader, and what the USB stack says about a hub port or a failed transfer is the only trace of a device that left
static void keep_idf_line(const char *fmt, va_list ap) {
  char line[160], kept[IDF_LOG_LINE_MAX + 1];
  va_list copy;
  va_copy(copy, ap);
  vsnprintf(line, sizeof(line), fmt, copy);
  va_end(copy);
  if (!idf_log_line_keep(line, kept, sizeof(kept))) return;
  unsigned held_back = 0;
  if (!idf_log_rate_allow(&rate, millis(), &held_back)) return;
  if (held_back) evlog_add("idf: %u more lines in the last minute not kept", held_back);
  evlog_add("idf: %s", kept);
}

static int idf_vprintf(const char *fmt, va_list ap) {
  keep_idf_line(fmt, ap);
  if (serial_hold_on()) return 0;
  return idf_prev ? idf_prev(fmt, ap) : vprintf(fmt, ap);
}

void serial_log_hold_idf(void) {
  if (!idf_prev) idf_prev = esp_log_set_vprintf(idf_vprintf);
}
#endif // BOARD_TRMNL_X
