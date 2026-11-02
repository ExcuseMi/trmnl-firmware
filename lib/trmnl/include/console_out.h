#pragma once
// Log text to the X's USB serial port that never makes the caller wait on a PC that does not read (owner
// 2026-11-02: the event log filled with "Serial: output stalled ... FIFO full" while the tablet held host mode or the
// PC keeper was closed, and HWCDC's write() waited up to 100 ms per call each time the watchdog kicked it awake).
// Every log write goes into a small buffer that keeps the newest bytes (the oldest go and are counted) and is moved
// into HWCDC's TX ring only as far as the ring has room now, whole lines first. A writer with a reader there waits
// for room CONSOLE_OUT_WAIT_MS at most, and not at all once the port counts as stalled (until the buffer drained);
// with no reader (host mode, no cable) it never waits. While an upload or a cable command holds the port
// (serial_hold.h) nothing is moved: its replies and ACKs (serial_out.h) have the ring to themselves. Pure: the port is
// serial_out.h's table of functions; the caller serializes calls (one lock).
#include <serial_out.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONSOLE_OUT_BYTES   2048 // the newest log text kept while nobody reads
#define CONSOLE_OUT_WAIT_MS 20   // a reading PC empties HWCDC's 4 KB ring in a few ms
#define CONSOLE_OUT_LINE_MAX 256 // a longer log line may go into the ring in parts

typedef struct {
  uint8_t buf[CONSOLE_OUT_BYTES];
  size_t len;
  bool stalled;     // a writer waited CONSOLE_OUT_WAIT_MS for nothing: no more waits until the buffer drained
  uint32_t dropped; // bytes dropped (oldest first) since the last console_out_dropped_take
} console_out_t;

void console_out_init(console_out_t *c);

// Log text: kept (the oldest pending bytes make room), then moved as far as the ring takes it. `reader`: the port
// is in serial mode with a host on the bus; `held`: an upload or cable command holds the port. Returns `n`
size_t console_out_write(console_out_t *c, const serial_out_port_t *p, bool reader, bool held, const uint8_t *b,
                         size_t n);

// Moves what waits, never waits itself (the serial watchdog's poll): true when nothing waits any more
bool console_out_pump(console_out_t *c, const serial_out_port_t *p, bool reader, bool held);

// Bytes dropped while nobody read, once the buffer drained (0 while text still waits or nothing was lost); resets
uint32_t console_out_dropped_take(console_out_t *c);
