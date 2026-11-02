#pragma once
// Log text on the X's USB serial port is held back while a cable upload runs (header to its last reply): an ACK
// line mixed with log text, or queued behind it, made every upload fail (owner's X 2026-11-02: "TAPP ACK 28672:
// src/l_wacpp ]: Seriumedr..." and 13x "TAPP ERR short"). Log lines then are dropped (the event log keeps its
// own copy) and counted. Pure: the writer is a function.
#include <stddef.h>
#include <stdint.h>

typedef size_t (*serial_hold_write_t)(void *ctx, const uint8_t *b, size_t n);

// An upload starts (true) or ends (false); nests (the menu receiver inside the cable task)
void serial_hold_set(bool on);
bool serial_hold_on(void);

// Log text: passed to `write` unless held; returns `n` either way (a held write is not an error for the logger)
size_t serial_hold_log(const uint8_t *b, size_t n, serial_hold_write_t write, void *ctx);

// Log writes dropped since the last call (for one line after the upload)
unsigned serial_hold_dropped_take(void);

// The upload's receive loop gives up after `idle_ms` without a byte from the host, but not while its own reply
// still waits in the TX ring (the host cannot answer an ACK it has not got: owner's X 2026-11-02, TX stalls of
// 2 to 6 s ended uploads as "short"). A stall longer than `stall_max_ms` since the last byte ends it anyway.
bool serial_hold_rx_gave_up(uint32_t now, uint32_t last_rx, uint32_t last_tx_waiting, uint32_t idle_ms,
                            uint32_t stall_max_ms);
