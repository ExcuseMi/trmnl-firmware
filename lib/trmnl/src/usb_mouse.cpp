#include "usb_mouse.h"

#include <string.h>

#define UNIT       16 // the cursor's fixed point
#define MAX_FP     (65535 * UNIT)
#define BASE_GAIN  128 // 1/16 of a unit factor: 8 pen units per count
#define ACCEL_FROM 6 // counts per report where the gain starts to rise
#define ACCEL_STEP 4 // gain (1/16) per count above that
#define ACCEL_MAX  30 // counts above ACCEL_FROM that still add: the gain tops out at 1.9 times

void usb_mouse_boot_layout(usb_mouse_layout_t *out) {
  memset(out, 0, sizeof(*out));
  out->btn_bit = 0;
  out->btn_count = 3;
  out->x_bit = 8;
  out->y_bit = 16;
  out->x_size = out->y_size = 8;
  out->report_bits = 24;
}

// ---- the report descriptor ----

typedef struct {
  uint16_t page;
  uint32_t rcount, rsize;
  uint8_t id;
  int32_t lmin;
  uint16_t usages[16];
  uint8_t nusages;
  int32_t umin, umax;
  bool has_range;
} gstate_t;

static int32_t sext(uint32_t v, unsigned size) {
  if (size == 0 || size >= 32) return (int32_t)v;
  return (v & (1u << (size - 1))) ? (int32_t)(v | (~0u << size)) : (int32_t)v;
}

// bits `n` (1..16) at bit offset `off` of d, little endian; the caller checked the length
static uint32_t get_bits(const uint8_t *d, unsigned off, unsigned n) {
  uint32_t v = 0;
  for (unsigned i = 0; i < n; i++) v |= (uint32_t)((d[(off + i) >> 3] >> ((off + i) & 7)) & 1u) << i;
  return v;
}

// The usage of field `i` of an Input item (local usages: a list or a range; the last one repeats)
static uint16_t usage_of(const gstate_t *g, uint32_t i) {
  if (g->has_range) return (uint16_t)(g->umin + (int32_t)i > g->umax ? g->umax : g->umin + (int32_t)i);
  if (!g->nusages) return 0;
  return g->usages[i < g->nusages ? i : g->nusages - 1u];
}

bool usb_mouse_map_parse(const uint8_t *map, size_t len, usb_mouse_layout_t *out) {
  if (!map || len == 0 || len > USB_MOUSE_MAP_MAX) return false;
  usb_mouse_layout_t l;
  memset(&l, 0, sizeof(l));
  l.btn_bit = l.x_bit = l.y_bit = -1;
  gstate_t g;
  memset(&g, 0, sizeof(g));
  int depth = 0, mouse_depth = 0; // mouse_depth: depth of the mouse's Application collection, 0 outside
  bool done = false;
  uint32_t bitpos = 0;
  size_t i = 0;
  while (i < len) {
    uint8_t b = map[i++];
    if (b == 0xFE) { // long item: skip
      if (i >= len) return false;
      size_t n = map[i];
      if (i + 2 + n > len) return false;
      i += 2 + n;
      continue;
    }
    unsigned size = b & 3u;
    if (size == 3) size = 4;
    unsigned type = (b >> 2) & 3u, tag = b >> 4;
    if (i + size > len) return false;
    uint32_t u = 0;
    for (unsigned k = 0; k < size; k++) u |= (uint32_t)map[i + k] << (8 * k);
    i += size;
    int32_t s = sext(u, size * 8);
    if (type == 1) { // global
      if (tag == 0) g.page = (uint16_t)u;
      else if (tag == 1) g.lmin = s;
      else if (tag == 7) g.rsize = u;
      else if (tag == 8) {
        g.id = (uint8_t)u;
        if (mouse_depth) bitpos = 0;
      } else if (tag == 9) g.rcount = u;
    } else if (type == 2) { // local
      if (tag == 0) {
        if (g.nusages < 16) g.usages[g.nusages++] = (uint16_t)u;
      } else if (tag == 1) {
        g.umin = (int32_t)u;
        g.has_range = true;
      } else if (tag == 2) {
        g.umax = (int32_t)u;
        g.has_range = true;
      }
    } else if (type == 0) { // main
      if (tag == 0xA) { // collection
        if (u == 1 && !mouse_depth && !done && g.page == 1 && g.nusages && g.usages[0] == 2) {
          mouse_depth = depth + 1;
          bitpos = 0;
          l.report_id = 0;
        }
        depth++;
      } else if (tag == 0xC) {
        if (depth > 0) depth--;
        if (mouse_depth && depth < mouse_depth) {
          mouse_depth = 0;
          done = true;
        }
      } else if (tag == 8 && mouse_depth) { // input
        bool constant = u & 1u, relative = (u & 4u) != 0;
        if (g.id && !l.report_id) l.report_id = g.id;
        unsigned base = l.report_id ? 8u : 0u;
        if (!constant && g.rsize >= 1 && g.rsize <= 16) {
          for (uint32_t f = 0; f < g.rcount && f < 64; f++) {
            uint16_t us = usage_of(&g, f);
            unsigned off = base + bitpos + f * g.rsize;
            if (g.page == 9 && f == 0 && l.btn_bit < 0) {
              l.btn_bit = (int16_t)off;
              l.btn_count = (uint8_t)(g.rcount < 8 ? g.rcount : 8);
            } else if (g.page == 1 && relative && g.rsize <= 16 && g.lmin < 0) {
              if (us == 0x30 && l.x_bit < 0) {
                l.x_bit = (int16_t)off;
                l.x_size = (uint8_t)g.rsize;
              } else if (us == 0x31 && l.y_bit < 0) {
                l.y_bit = (int16_t)off;
                l.y_size = (uint8_t)g.rsize;
              }
            }
          }
        }
        bitpos += g.rcount * g.rsize;
        if (bitpos > 0xFFFF - 8) return false;
        l.report_bits = (uint16_t)(bitpos + (l.report_id ? 8u : 0u));
      }
      if (tag == 8 || tag == 9 || tag == 0xB || tag == 0xA) { // a main item clears the local items
        g.nusages = 0;
        g.has_range = false;
      }
    }
  }
  if (depth != 0 || l.x_bit < 0 || l.y_bit < 0) return false;
  if (l.btn_bit < 0) l.btn_count = 0;
  *out = l;
  return true;
}

bool usb_mouse_report(const usb_mouse_layout_t *l, const uint8_t *d, size_t len, usb_mouse_move_t *out) {
  if (!l || !d || !out || l->x_bit < 0 || l->y_bit < 0) return false;
  if (l->report_id && (len == 0 || d[0] != l->report_id)) return false;
  size_t bits = l->report_bits ? l->report_bits : 24u;
  if (len * 8u < bits) return false;
  memset(out, 0, sizeof(*out));
  if (l->btn_bit >= 0) {
    unsigned n = l->btn_count < 3 ? l->btn_count : 3u;
    out->buttons = (uint8_t)get_bits(d, (unsigned)l->btn_bit, n);
  }
  out->dx = sext(get_bits(d, (unsigned)l->x_bit, l->x_size), l->x_size);
  out->dy = sext(get_bits(d, (unsigned)l->y_bit, l->y_size), l->y_size);
  return true;
}

// ---- the cursor ----

void usb_mouse_init(usb_mouse_t *m, uint32_t w, uint32_t h) {
  memset(m, 0, sizeof(*m));
  m->w = w ? w : 1;
  m->h = h ? h : m->w;
  m->x = m->y = 32768 * UNIT;
}

static void fill(const usb_mouse_t *m, uint8_t flags, app_pen_event_t *out) {
  memset(out, 0, sizeof(*out));
  out->type = APP_EVENT_PEN;
  out->flags = flags;
  out->changed = (uint8_t)(flags ^ m->flags);
  out->x = (uint16_t)(m->x / UNIT);
  out->y = (uint16_t)(m->y / UNIT);
  out->pressure = (flags & APP_PEN_TIP) ? USB_MOUSE_PRESSURE : 0;
}

int usb_mouse_enter(usb_mouse_t *m, app_pen_event_t *out) {
  uint8_t f = APP_PEN_IN_RANGE;
  fill(m, f, out);
  m->flags = f;
  return 1;
}

int usb_mouse_leave(usb_mouse_t *m, app_pen_event_t *out) {
  if (!m->flags) return 0;
  fill(m, 0, out);
  m->flags = 0;
  return 1;
}

static int32_t clamp_fp(int64_t v) { return v < 0 ? 0 : v > MAX_FP ? MAX_FP : (int32_t)v; }

int usb_mouse_move(usb_mouse_t *m, const usb_mouse_move_t *mv, app_pen_event_t *out) {
  if (!(m->flags & APP_PEN_IN_RANGE)) return 0;
  int64_t ax = mv->dx < 0 ? -(int64_t)mv->dx : mv->dx, ay = mv->dy < 0 ? -(int64_t)mv->dy : mv->dy;
  int64_t speed = ax > ay ? ax : ay;
  int64_t extra = speed > ACCEL_FROM ? speed - ACCEL_FROM : 0;
  if (extra > ACCEL_MAX) extra = ACCEL_MAX;
  int64_t gain = BASE_GAIN + extra * ACCEL_STEP; // pen units = counts * gain / 16; the cursor is in 1/16 units
  int32_t nx = clamp_fp((int64_t)m->x + (int64_t)mv->dx * gain);
  // y: the same distance on screen, so scaled by h / w
  int32_t ny = clamp_fp((int64_t)m->y + (int64_t)mv->dy * gain * (int64_t)m->h / (int64_t)m->w);
  uint8_t f = APP_PEN_IN_RANGE;
  if (mv->buttons & 1) f |= APP_PEN_TIP;
  if (mv->buttons & 2) f |= APP_PEN_BARREL;
  if (mv->buttons & 4) f |= APP_PEN_BARREL2;
  if (nx == m->x && ny == m->y && f == m->flags) return 0;
  m->x = nx;
  m->y = ny;
  fill(m, f, out);
  m->flags = f;
  return 1;
}

void usb_mouse_info(uint16_t vid, uint16_t pid, uint32_t width_um, uint32_t height_um, app_pen_info_t *out) {
  memset(out, 0, sizeof(*out));
  out->vid = vid;
  out->pid = pid;
  out->mode = APP_PEN_MODE_MOUSE;
  out->key_count = 0;
  out->width_um = width_um;
  out->height_um = height_um;
}
