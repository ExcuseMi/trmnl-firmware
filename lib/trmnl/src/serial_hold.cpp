#include "serial_hold.h"

static volatile int depth;
static volatile unsigned dropped;

void serial_hold_set(bool on) {
  if (on) depth = depth + 1;
  else if (depth > 0) depth = depth - 1;
}

bool serial_hold_on(void) { return depth > 0; }

size_t serial_hold_log(const uint8_t *b, size_t n, serial_hold_write_t write, void *ctx) {
  if (depth > 0) {
    if (n) dropped = dropped + 1;
    return n;
  }
  return write ? write(ctx, b, n) : n;
}

unsigned serial_hold_dropped_take(void) {
  unsigned d = dropped;
  dropped = 0;
  return d;
}

bool serial_hold_rx_gave_up(uint32_t now, uint32_t last_rx, uint32_t last_tx_waiting, uint32_t idle_ms,
                            uint32_t stall_max_ms) {
  if (now - last_rx >= stall_max_ms) return true;
  uint32_t since = now - last_rx;
  uint32_t since_tx = now - last_tx_waiting;
  if (since_tx < since) since = since_tx; // the reply left the ring later than the last byte came
  return since >= idle_ms;
}
