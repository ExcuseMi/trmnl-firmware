#pragma once

// The event log's ring (src/evlog.cpp keeps it on the X, docs/apps.md "Event log"), I/O free: recent device events
// (keyboards, apps, network) as short text lines with the uptime, the wall clock when set and a boot number. Its image
// (header + data, EVLOG_BYTES in all) is what goes to the flash file; a damaged image loads as an empty log.
// Kept records (evlog_ring_add_kept: a boot's reset reason, a crash) also go to EVLOG_KEEP slots of their own that
// ordinary records never push out; shown before the ring once the ring has dropped them (owner's X 2026-11-03: tablet
// reconnects pushed the crash line out within minutes).
// Image versions: "ELG1" (no kept slots, 28 byte header) still loads, its records moved into the new layout; "ELG2" now.

#include <apps_config.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EVLOG_MAGIC 0x32474C45u // "ELG2"
#define EVLOG_MAGIC_V1 0x31474C45u // "ELG1": loads, moved into this layout
#define EVLOG_TEXT_MAX 100 // longer event text is cut
#define EVLOG_REC_HEAD 12 // per record: uptime ms, wall s, boot, repeat, text length
#define EVLOG_LINE_MAX (EVLOG_TEXT_MAX + 64) // a formatted line with its NUL fits
#define EVLOG_KEEP 8 // kept records (resets, crashes)
#define EVLOG_KEEP_SLOT (EVLOG_REC_HEAD + EVLOG_TEXT_MAX)
#define EVLOG_KEEP_BYTES (EVLOG_KEEP * EVLOG_KEEP_SLOT)

typedef struct {
  uint32_t magic; // EVLOG_MAGIC
  uint32_t cap; // ring bytes (the data after the kept slots)
  uint32_t head; // offset of the oldest record
  uint32_t used; // bytes of records
  uint32_t last; // offset of the newest record (when used)
  uint32_t boot; // counted up at each start (evlog_ring_new_boot)
  uint32_t kept; // kept slots in use (bits 0-7) and the next one written (bits 8-15)
  uint32_t crc; // trmnl_crc32 over this header (crc 0) and all data
} evlog_hdr_t;

#define EVLOG_DATA_BYTES (EVLOG_BYTES - sizeof(evlog_hdr_t)) // the kept slots, then the ring
#define EVLOG_RING_BYTES (EVLOG_DATA_BYTES - EVLOG_KEEP_BYTES)

typedef struct {
  evlog_hdr_t h;
  uint8_t *data; // EVLOG_DATA_BYTES, the caller's
  uint8_t *ring; // data + EVLOG_KEEP_BYTES: the ring's records
  bool dirty; // changed since the last flush
  bool flushed_once;
  uint32_t flushed_at;
} evlog_ring_t;

// Empty, boot 0, over the caller's `data` of EVLOG_DATA_BYTES
void evlog_ring_init(evlog_ring_t *r, uint8_t *data);

// From a saved image of EVLOG_BYTES (this version or "ELG1"); false (and the ring empty) when it is not one: wrong
// size, magic, CRC or records
bool evlog_ring_load(evlog_ring_t *r, const void *img, size_t len);

// The image into `out` (EVLOG_BYTES); returns its size, 0 when `cap` is too small
size_t evlog_ring_save(const evlog_ring_t *r, void *out, size_t cap);

// A new start: the boot number goes up (the records stay)
void evlog_ring_new_boot(evlog_ring_t *r);

// One event: `text` (control characters become spaces, cut at EVLOG_TEXT_MAX) at uptime `ms` and Unix `wall` (0 or a
// time before the clock counts as set: unknown). The same text as the newest record of this boot only counts up its
// repeat (to 255). The oldest records go when there is no room.
void evlog_ring_add(evlog_ring_t *r, uint32_t ms, int64_t wall, const char *text);

// The same for an earlier boot's event (kept over a crash in RTC memory, src/evlog_crash.cpp): `boot` instead of the
// ring's own
void evlog_ring_add_boot(evlog_ring_t *r, uint16_t boot, uint32_t ms, int64_t wall, const char *text);

// The same, and kept: also in a kept slot (the oldest of EVLOG_KEEP goes), never pushed out by ordinary records
void evlog_ring_add_kept(evlog_ring_t *r, uint16_t boot, uint32_t ms, int64_t wall, const char *text);

void evlog_ring_clear(evlog_ring_t *r); // no records, none kept; the boot number stays

// The lines: kept records older than the ring's oldest (the ring dropped them), then the ring's records
int evlog_ring_count(const evlog_ring_t *r);

// Line `i` (0 the oldest) as "#<boot> <s>.<ms>s <YYYY-MM-DD HH:MM:SSZ or -> <text>[ (xN)]"; its length, -1 if none
int evlog_ring_line(const evlog_ring_t *r, int i, char *out, size_t cap);

// The last `n` records, oldest first, each ending in '\n', NUL-terminated in `buf`; fewer when they do not fit (the
// oldest of them go). Returns the lines written, -1 when buf is NULL or len 0
int evlog_ring_tail(const evlog_ring_t *r, uint32_t n, char *buf, size_t len);

// Flash writes only on events and at most every EVLOG_FLUSH_GAP_MS: true when a write is due at `now`
bool evlog_ring_flush_due(const evlog_ring_t *r, uint32_t now);
void evlog_ring_flushed(evlog_ring_t *r, uint32_t now);
