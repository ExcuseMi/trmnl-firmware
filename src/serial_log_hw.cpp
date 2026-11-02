#ifdef BOARD_TRMNL_X
#include "serial_log_hw.h"

#include <Arduino.h>
#include <console_out.h>
#include <driver/usb_serial_jtag.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <idf_log_line.h>
#include <serial_hold.h>
#include <stdio.h>

#include "evlog.h"
#include "usb_port_role.h"

// Log text goes through console_out.h: never waits on a PC that does not read, keeps the newest, counts the rest
static console_out_t console; // under lock
static SemaphoreHandle_t lock;
static volatile uint32_t busy_dropped; // log writes that found the lock taken past the wait limit, or re-entered

static int room(void *) { return Serial.availableForWrite(); }
static size_t put(void *, const uint8_t *b, size_t n) { return Serial.write(b, n); }
static uint32_t now(void *) { return millis(); }
static void wait(void *, uint32_t ms) { delay(ms); }
static const serial_out_port_t port = {room, put, now, wait, nullptr};

// A PC can take the text: the PHY is the serial port's (not the keyboard's or tablet's host mode) and SOF comes
static bool reader(void) { return usb_port_role() == USB_ROLE_SERIAL && usb_serial_jtag_is_connected(); }

static size_t to_console(void *, const uint8_t *b, size_t n) {
  if (!lock) return Serial.write(b, n); // before serial_log_out set it up
  if (xSemaphoreGetMutexHolder(lock) == xTaskGetCurrentTaskHandle() ||
      xSemaphoreTake(lock, pdMS_TO_TICKS(CONSOLE_OUT_WAIT_MS)) != pdTRUE) {
    busy_dropped = busy_dropped + (uint32_t)n;
    return n;
  }
  console_out_write(&console, &port, reader(), serial_hold_on(), b, n);
  xSemaphoreGive(lock);
  return n;
}

class HeldSerial : public Print {
public:
  size_t write(uint8_t c) override { return serial_hold_log(&c, 1, to_console, nullptr); }
  size_t write(const uint8_t *b, size_t n) override { return serial_hold_log(b, n, to_console, nullptr); }
};

static HeldSerial held;

Print &serial_log_out(void) {
  if (!lock) {
    console_out_init(&console);
    lock = xSemaphoreCreateMutex();
  }
  return held;
}

void serial_log_pump(void) {
  if (!lock || xSemaphoreTake(lock, 0) != pdTRUE) return;
  bool drained = console_out_pump(&console, &port, reader(), serial_hold_on());
  uint32_t d = console_out_dropped_take(&console);
  xSemaphoreGive(lock);
  if (drained && (d || busy_dropped)) { // reported once a reader took everything that waited
    uint32_t b = busy_dropped;
    busy_dropped = 0;
    d += b;
    evlog_add("console: %lu bytes dropped while nobody read", (unsigned long)d);
    char line[64];
    int n = snprintf(line, sizeof line, "console: %lu bytes dropped while nobody read\r\n", (unsigned long)d);
    to_console(nullptr, (const uint8_t *)line, (size_t)n);
  }
}

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
