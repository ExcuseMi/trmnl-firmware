#ifdef BOARD_TRMNL_X
#include "serial_log_hw.h"

#include <Arduino.h>
#include <esp_log.h>
#include <serial_hold.h>
#include <stdio.h>

static size_t to_serial(void *, const uint8_t *b, size_t n) { return Serial.write(b, n); }

class HeldSerial : public Print {
public:
  size_t write(uint8_t c) override { return serial_hold_log(&c, 1, to_serial, nullptr); }
  size_t write(const uint8_t *b, size_t n) override { return serial_hold_log(b, n, to_serial, nullptr); }
};

static HeldSerial held;

Print &serial_log_out(void) { return held; }

static vprintf_like_t idf_prev;

static int idf_vprintf(const char *fmt, va_list ap) {
  if (serial_hold_on()) return 0;
  return idf_prev ? idf_prev(fmt, ap) : vprintf(fmt, ap);
}

void serial_log_hold_idf(void) {
  if (!idf_prev) idf_prev = esp_log_set_vprintf(idf_vprintf);
}
#endif // BOARD_TRMNL_X
