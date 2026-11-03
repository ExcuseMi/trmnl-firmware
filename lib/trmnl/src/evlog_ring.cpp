#include "evlog_ring.h"

#include <app_clock.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <trmnl_crc32.h>

static_assert(sizeof(evlog_hdr_t) == 32, "the image header is part of the saved file");
static_assert(EVLOG_BYTES >= sizeof(evlog_hdr_t) + EVLOG_KEEP_BYTES + 4 * (EVLOG_REC_HEAD + EVLOG_TEXT_MAX),
              "event_log.bytes holds the kept slots and at least 4 full records");
static_assert(EVLOG_KEEP < 256, "kept counts are bytes");

#define V1_HDR 28u // "ELG1": magic, cap, head, used, last, boot, crc

typedef struct {
  uint32_t ms, wall;
  uint16_t boot;
  uint8_t repeat, len;
} rec_t;

static void get(const evlog_ring_t *r, uint32_t off, void *dst, uint32_t n) {
  uint8_t *d = (uint8_t *)dst;
  for (uint32_t i = 0; i < n; i++) d[i] = r->ring[(off + i) % r->h.cap];
}

static void put(evlog_ring_t *r, uint32_t off, const void *src, uint32_t n) {
  const uint8_t *s = (const uint8_t *)src;
  for (uint32_t i = 0; i < n; i++) r->ring[(off + i) % r->h.cap] = s[i];
}

static rec_t unpack(const uint8_t *b) {
  rec_t x;
  x.ms = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
  x.wall = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
  x.boot = (uint16_t)(b[8] | b[9] << 8);
  x.repeat = b[10];
  x.len = b[11];
  return x;
}

static void pack(uint8_t *b, const rec_t &x) {
  const uint8_t v[EVLOG_REC_HEAD] = {(uint8_t)x.ms, (uint8_t)(x.ms >> 8), (uint8_t)(x.ms >> 16), (uint8_t)(x.ms >> 24),
                                     (uint8_t)x.wall, (uint8_t)(x.wall >> 8), (uint8_t)(x.wall >> 16),
                                     (uint8_t)(x.wall >> 24), (uint8_t)x.boot, (uint8_t)(x.boot >> 8), x.repeat, x.len};
  memcpy(b, v, sizeof(v));
}

static rec_t rec_at(const evlog_ring_t *r, uint32_t off) {
  uint8_t b[EVLOG_REC_HEAD];
  get(r, off, b, sizeof(b));
  return unpack(b);
}

static void rec_put(evlog_ring_t *r, uint32_t off, const rec_t &x) {
  uint8_t b[EVLOG_REC_HEAD];
  pack(b, x);
  put(r, off, b, sizeof(b));
}

// ---- the kept slots: data[0, EVLOG_KEEP_BYTES), a ring of EVLOG_KEEP ----

static uint32_t kept_n(const evlog_ring_t *r) { return r->h.kept & 0xFFu; }
static uint32_t kept_next(const evlog_ring_t *r) { return (r->h.kept >> 8) & 0xFFu; }

// Kept record `i` (0 the oldest of those in use)
static uint8_t *kept_slot(const evlog_ring_t *r, uint32_t i) {
  uint32_t at = (kept_next(r) + EVLOG_KEEP - kept_n(r) + i) % EVLOG_KEEP;
  return r->data + at * EVLOG_KEEP_SLOT;
}

static bool kept_ok(const evlog_ring_t *r) {
  if (kept_n(r) > EVLOG_KEEP || kept_next(r) >= EVLOG_KEEP || (r->h.kept >> 16)) return false;
  for (uint32_t i = 0; i < kept_n(r); i++) {
    rec_t x = unpack(kept_slot(r, i));
    if (x.len > EVLOG_TEXT_MAX || x.repeat == 0) return false;
  }
  return true;
}

static void keep(evlog_ring_t *r, const rec_t &x, const char *text) {
  uint32_t n = kept_n(r), next = kept_next(r);
  if (n) { // the same text again in the same boot: counted up, as the ring does
    uint8_t *last = kept_slot(r, n - 1);
    rec_t newest = unpack(last);
    if (newest.boot == x.boot && newest.len == x.len && newest.repeat < 255 &&
        memcmp(last + EVLOG_REC_HEAD, text, x.len) == 0) {
      rec_t y = x;
      y.repeat = (uint8_t)(newest.repeat + 1);
      pack(last, y);
      return;
    }
  }
  uint8_t *slot = r->data + next * EVLOG_KEEP_SLOT;
  memset(slot, 0, EVLOG_KEEP_SLOT);
  pack(slot, x);
  memcpy(slot + EVLOG_REC_HEAD, text, x.len);
  if (n < EVLOG_KEEP) n++;
  r->h.kept = n | ((next + 1) % EVLOG_KEEP) << 8;
}

static void empty(evlog_ring_t *r) {
  r->h.head = r->h.used = r->h.last = 0;
}

void evlog_ring_init(evlog_ring_t *r, uint8_t *data) {
  memset(r, 0, sizeof(*r));
  r->data = data;
  r->ring = data + EVLOG_KEEP_BYTES;
  r->h.magic = EVLOG_MAGIC;
  r->h.cap = EVLOG_RING_BYTES;
  memset(data, 0, EVLOG_DATA_BYTES);
}

static uint32_t image_crc(const evlog_hdr_t *h, const uint8_t *data) {
  evlog_hdr_t c = *h;
  c.crc = 0;
  return trmnl_crc32(trmnl_crc32(0, &c, sizeof(c)), data, EVLOG_DATA_BYTES);
}

// The records of `h` over `data` are whole and end exactly at `used`, the newest at `last`
static bool records_ok(const evlog_ring_t *r) {
  uint32_t off = r->h.head, left = r->h.used, newest = r->h.head;
  while (left) {
    if (left < EVLOG_REC_HEAD) return false;
    rec_t x = rec_at(r, off);
    uint32_t n = EVLOG_REC_HEAD + x.len;
    if (x.len > EVLOG_TEXT_MAX || x.repeat == 0 || n > left) return false;
    newest = off;
    off = (off + n) % r->h.cap;
    left -= n;
  }
  return r->h.used == 0 || newest == r->h.last;
}

static bool is_kept_text(const char *t, uint8_t len) {
  return (len >= 6 && memcmp(t, "boot: ", 6) == 0) || (len >= 7 && memcmp(t, "crash: ", 7) == 0);
}

// An "ELG1" image (no kept slots): its records into the empty ring `r`, the oldest go when they do not fit; its
// reset and crash lines kept. False when it is not a whole one
static bool load_v1(evlog_ring_t *r, const uint8_t *src) {
  uint32_t f[7];
  memcpy(f, src, sizeof(f));
  const uint32_t cap = EVLOG_BYTES - V1_HDR;
  if (f[1] != cap || f[2] >= cap || f[3] > cap || f[4] >= cap) return false;
  uint32_t crc = f[6];
  f[6] = 0;
  if (crc != trmnl_crc32(trmnl_crc32(0, f, sizeof(f)), src + V1_HDR, cap)) return false;
  evlog_ring_t old; // a read-only view of the old records
  memset(&old, 0, sizeof(old));
  old.ring = (uint8_t *)(uintptr_t)(src + V1_HDR);
  old.h.cap = cap;
  old.h.head = f[2];
  old.h.used = f[3];
  old.h.last = f[4];
  if (!records_ok(&old)) return false;
  r->h.boot = f[5];
  uint32_t off = old.h.head, left = old.h.used;
  while (left) {
    rec_t x = rec_at(&old, off);
    char t[EVLOG_TEXT_MAX];
    get(&old, off + EVLOG_REC_HEAD, t, x.len);
    uint32_t need = EVLOG_REC_HEAD + x.len;
    while (r->h.cap - r->h.used < need) { // the oldest go
      uint32_t n = EVLOG_REC_HEAD + rec_at(r, r->h.head).len;
      r->h.head = (r->h.head + n) % r->h.cap;
      r->h.used -= n;
    }
    uint32_t at = (r->h.head + r->h.used) % r->h.cap;
    rec_put(r, at, x);
    put(r, at + EVLOG_REC_HEAD, t, x.len);
    r->h.last = at;
    r->h.used += need;
    if (is_kept_text(t, x.len)) keep(r, x, t);
    off = (off + need) % cap;
    left -= need;
  }
  return true;
}

bool evlog_ring_load(evlog_ring_t *r, const void *img, size_t len) {
  evlog_hdr_t h;
  const uint8_t *src = (const uint8_t *)img;
  bool ok = img && len == EVLOG_BYTES;
  uint32_t magic = 0;
  if (ok) memcpy(&magic, src, sizeof(magic));
  if (ok && magic == EVLOG_MAGIC_V1) {
    uint32_t boot = r->h.boot;
    evlog_ring_init(r, r->data);
    r->h.boot = boot;
    ok = load_v1(r, src);
    if (ok) {
      r->dirty = true; // written back in this version
      return true;
    }
  } else if (ok) {
    memcpy(&h, src, sizeof(h));
    ok = h.magic == EVLOG_MAGIC && h.cap == EVLOG_RING_BYTES && h.head < h.cap && h.used <= h.cap &&
         h.last < h.cap && h.crc == image_crc(&h, src + sizeof(h));
    if (ok) {
      memcpy(r->data, src + sizeof(h), EVLOG_DATA_BYTES);
      r->h = h;
      ok = records_ok(r) && kept_ok(r);
    }
  }
  if (!ok) {
    uint32_t boot = r->h.boot;
    evlog_ring_init(r, r->data);
    r->h.boot = boot;
  }
  r->dirty = false;
  return ok;
}

size_t evlog_ring_save(const evlog_ring_t *r, void *out, size_t cap) {
  if (!out || cap < EVLOG_BYTES) return 0;
  evlog_hdr_t h = r->h;
  h.crc = image_crc(&h, r->data);
  memcpy(out, &h, sizeof(h));
  memcpy((uint8_t *)out + sizeof(h), r->data, EVLOG_DATA_BYTES);
  return EVLOG_BYTES;
}

void evlog_ring_new_boot(evlog_ring_t *r) { r->h.boot = (uint16_t)(r->h.boot + 1); }

static bool same_text(const evlog_ring_t *r, uint32_t off, const char *text, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (r->ring[(off + EVLOG_REC_HEAD + i) % r->h.cap] != (uint8_t)text[i]) return false;
  }
  return true;
}

void evlog_ring_add(evlog_ring_t *r, uint32_t ms, int64_t wall, const char *text) {
  evlog_ring_add_boot(r, (uint16_t)r->h.boot, ms, wall, text);
}

// The record for `text` (control characters as spaces, cut) into `t`
static rec_t make_rec(uint16_t boot, uint32_t ms, int64_t wall, const char *text, char *t) {
  uint8_t len = 0;
  for (const char *c = text ? text : ""; *c && len < EVLOG_TEXT_MAX; c++) {
    t[len++] = (unsigned char)*c < 0x20 || *c == 0x7F ? ' ' : *c;
  }
  rec_t x = {ms, wall >= APP_CLOCK_SET_AFTER && wall <= 0xFFFFFFFFll ? (uint32_t)wall : 0u, boot, 1, len};
  return x;
}

void evlog_ring_add_boot(evlog_ring_t *r, uint16_t boot, uint32_t ms, int64_t wall, const char *text) {
  char t[EVLOG_TEXT_MAX];
  rec_t x = make_rec(boot, ms, wall, text, t);
  uint8_t len = x.len;
  r->dirty = true;
  if (r->h.used) {
    rec_t newest = rec_at(r, r->h.last);
    if (newest.boot == x.boot && newest.len == len && newest.repeat < 255 && same_text(r, r->h.last, t, len)) {
      x.repeat = (uint8_t)(newest.repeat + 1);
      rec_put(r, r->h.last, x);
      return;
    }
  }
  uint32_t need = EVLOG_REC_HEAD + len;
  while (r->h.cap - r->h.used < need) { // the oldest go
    uint32_t n = EVLOG_REC_HEAD + rec_at(r, r->h.head).len;
    r->h.head = (r->h.head + n) % r->h.cap;
    r->h.used -= n;
  }
  if (!r->h.used) empty(r);
  uint32_t at = (r->h.head + r->h.used) % r->h.cap;
  rec_put(r, at, x);
  put(r, at + EVLOG_REC_HEAD, t, len);
  r->h.last = at;
  r->h.used += need;
}

void evlog_ring_add_kept(evlog_ring_t *r, uint16_t boot, uint32_t ms, int64_t wall, const char *text) {
  evlog_ring_add_boot(r, boot, ms, wall, text);
  char t[EVLOG_TEXT_MAX];
  rec_t x = make_rec(boot, ms, wall, text, t);
  keep(r, x, t);
}

void evlog_ring_clear(evlog_ring_t *r) {
  empty(r);
  r->h.kept = 0;
  r->dirty = true;
}

// Walks the records: `off` of the next one, `left` bytes after it
typedef struct {
  uint32_t off, left;
} walk_t;

static bool next(const evlog_ring_t *r, walk_t *w, uint32_t *at) {
  if (!w->left) return false;
  uint32_t n = EVLOG_REC_HEAD + rec_at(r, w->off).len;
  *at = w->off;
  w->off = (w->off + n) % r->h.cap;
  w->left -= n;
  return true;
}

static int ring_count(const evlog_ring_t *r) {
  walk_t w = {r->h.head, r->h.used};
  uint32_t at;
  int n = 0;
  while (next(r, &w, &at)) n++;
  return n;
}

// The kept records the ring no longer holds: those before its oldest record (boots counted back from the current one,
// then the uptime); at the oldest's own time, those none of the ring's records of that time has the text of
static int kept_shown(const evlog_ring_t *r) {
  uint32_t n = kept_n(r);
  if (!r->h.used) return (int)n;
  rec_t head = rec_at(r, r->h.head);
  uint16_t now = (uint16_t)r->h.boot, head_age = (uint16_t)(now - head.boot);
  uint32_t i = 0;
  for (; i < n; i++) {
    const uint8_t *s = kept_slot(r, i);
    rec_t k = unpack(s);
    uint16_t age = (uint16_t)(now - k.boot);
    bool older = age > head_age || (age == head_age && k.ms < head.ms);
    if (age == head_age && k.ms == head.ms) {
      walk_t w = {r->h.head, r->h.used};
      uint32_t at;
      bool found = false;
      while (!found && next(r, &w, &at)) {
        rec_t x = rec_at(r, at);
        if (x.boot != k.boot || x.ms != k.ms) break;
        found = x.len == k.len && same_text(r, at, (const char *)s + EVLOG_REC_HEAD, k.len);
      }
      older = !found;
    }
    if (!older) break;
  }
  return (int)i;
}

int evlog_ring_count(const evlog_ring_t *r) { return kept_shown(r) + ring_count(r); }

static int format(const rec_t &x, const char *text, char *out, size_t cap) {
  char when[24] = "-";
  if (x.wall) {
    time_t t = (time_t)x.wall;
    struct tm tm;
    if (gmtime_r(&t, &tm)) strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%SZ", &tm);
  }
  char rep[8] = "";
  if (x.repeat > 1) snprintf(rep, sizeof(rep), " (x%u)", (unsigned)x.repeat);
  return snprintf(out, cap, "#%u %lu.%03lus %s %s%s", (unsigned)x.boot, (unsigned long)(x.ms / 1000),
                  (unsigned long)(x.ms % 1000), when, text, rep);
}

int evlog_ring_line(const evlog_ring_t *r, int i, char *out, size_t cap) {
  if (i < 0) return -1;
  char text[EVLOG_TEXT_MAX + 1];
  int ks = kept_shown(r);
  if (i < ks) {
    const uint8_t *s = kept_slot(r, (uint32_t)i);
    rec_t x = unpack(s);
    memcpy(text, s + EVLOG_REC_HEAD, x.len);
    text[x.len] = '\0';
    return format(x, text, out, cap);
  }
  walk_t w = {r->h.head, r->h.used};
  uint32_t at;
  for (int k = ks; next(r, &w, &at); k++) {
    if (k == i) {
      rec_t x = rec_at(r, at);
      get(r, at + EVLOG_REC_HEAD, text, x.len);
      text[x.len] = '\0';
      return format(x, text, out, cap);
    }
  }
  return -1;
}

int evlog_ring_tail(const evlog_ring_t *r, uint32_t n, char *buf, size_t len) {
  if (!buf || !len) return -1;
  buf[0] = '\0';
  int count = evlog_ring_count(r);
  int first = (uint32_t)count > n ? count - (int)n : 0;
  char line[EVLOG_LINE_MAX];
  // from the newest back: the lines that fit with their '\n' and the NUL
  size_t total = 1;
  int from = count;
  while (from > first) {
    int l = evlog_ring_line(r, from - 1, line, sizeof(line));
    if (l < 0 || total + (size_t)l + 1 > len) break;
    total += (size_t)l + 1;
    from--;
  }
  size_t pos = 0;
  for (int i = from; i < count; i++) {
    int l = evlog_ring_line(r, i, line, sizeof(line));
    memcpy(buf + pos, line, (size_t)l);
    pos += (size_t)l;
    buf[pos++] = '\n';
  }
  buf[pos] = '\0';
  return count - from;
}

bool evlog_ring_flush_due(const evlog_ring_t *r, uint32_t now) {
  return r->dirty && (!r->flushed_once || (uint32_t)(now - r->flushed_at) >= EVLOG_FLUSH_GAP_MS);
}

void evlog_ring_flushed(evlog_ring_t *r, uint32_t now) {
  r->dirty = false;
  r->flushed_once = true;
  r->flushed_at = now;
}
