#include "usb_pen.h"

#include <string.h>

const uint8_t usb_pen_full_mode_cmd[USB_PEN_CMD_LEN] = {0x02, 0xb0, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

#define FULL_ID       0x02
#define FULL_KEYS     0xf0 // byte 1 of a button frame
#define FULL_PEN      0x80 // byte 1 of a pen frame has it
#define FULL_NOT_PEN  0x10 // with FULL_PEN: not a pen frame (f0 buttons; b0, x 4: the tablet's answer to the switch)
#define FULL_LEAVING  0x40 // with FULL_PEN: the pen left
#define FULL_IN_RANGE 0x20
#define STD_ID        0x07
#define STD_IN_RANGE  0x20
#define PEN_BYTES     10 // id, flags, x, y, pressure, tilt x, tilt y

usb_pen_iface_t usb_pen_iface_role(uint16_t vid, uint16_t pid, uint8_t iface) {
  if (vid != USB_PEN_VID_XPPEN) return USB_PEN_IFACE_NONE;
  if (pid == USB_PEN_PID_DECO01V2 && iface == USB_PEN_FULL_IFACE) return USB_PEN_IFACE_FULL;
  if (iface == USB_PEN_STD_IFACE) return USB_PEN_IFACE_STANDARD;
  return USB_PEN_IFACE_IGNORE;
}

void usb_pen_init(usb_pen_t *p, uint16_t vid, uint16_t pid) {
  memset(p, 0, sizeof(*p));
  p->vid = vid;
  p->pid = pid;
}

uint16_t usb_pen_scale(uint32_t raw, uint32_t max) {
  if (max == 0) return 0;
  if (raw >= max) return 65535;
  return (uint16_t)((uint64_t)raw * 65535u / max);
}

static uint16_t u16le(const uint8_t *d) { return (uint16_t)(d[0] | (d[1] << 8)); }

static void note_max(uint16_t *m, uint16_t v) {
  if (v > *m) *m = v;
}

// The pen part of a full or standard report: x, y, pressure at bytes 2..7, tilt at 8 and 9
static int pen_event(usb_pen_t *p, const uint8_t *d, uint8_t flags, uint32_t mx, uint32_t my, uint32_t mp,
                     app_pen_event_t *out) {
  uint16_t rx = u16le(d + 2), ry = u16le(d + 4), rp = u16le(d + 6);
  if (p->pen_frames != 0xFFFFFFFFu) p->pen_frames++;
  note_max(&p->raw_max_x, rx);
  note_max(&p->raw_max_y, ry);
  note_max(&p->raw_max_p, rp);
  memset(out, 0, sizeof(*out));
  out->type = APP_EVENT_PEN;
  out->flags = flags;
  out->changed = (uint8_t)(flags ^ p->flags);
  out->x = usb_pen_scale(rx, mx);
  out->y = usb_pen_scale(ry, my);
  out->pressure = (flags & APP_PEN_TIP) ? usb_pen_scale(rp, mp) : 0;
  out->tilt_x = (int8_t)d[8];
  out->tilt_y = (int8_t)d[9];
  out->keys = p->keys;
  p->flags = flags;
  p->x = out->x;
  p->y = out->y;
  p->pressure = out->pressure;
  p->tilt_x = out->tilt_x;
  p->tilt_y = out->tilt_y;
  return 1;
}

// Byte 1's pen bits, the same in both modes: 0x01 tip, 0x02 lower button, 0x04 upper button
static uint8_t pen_flags(uint8_t b, bool in_range) {
  if (!in_range) return 0;
  uint8_t f = APP_PEN_IN_RANGE;
  if (b & 0x01) f |= APP_PEN_TIP;
  if (b & 0x02) f |= APP_PEN_BARREL;
  if (b & 0x04) f |= APP_PEN_BARREL2;
  return f;
}

// `keys_iface`: from the full-mode interface, the only one whose button frames count
static int full_report(usb_pen_t *p, const uint8_t *d, size_t len, bool keys_iface, app_pen_event_t *out) {
  if (len < 3 || d[0] != FULL_ID) return 0;
  uint8_t b = d[1];
  if (b == FULL_KEYS) {
    if (!keys_iface) return 0;
    p->mode = APP_PEN_MODE_FULL;
    uint8_t changed = (uint8_t)(d[2] ^ p->keys);
    if (!changed) return 0;
    p->keys = d[2];
    memset(out, 0, sizeof(*out));
    out->type = APP_EVENT_PEN_KEY;
    out->flags = p->flags;
    out->x = p->x;
    out->y = p->y;
    out->pressure = p->pressure;
    out->tilt_x = p->tilt_x;
    out->tilt_y = p->tilt_y;
    out->keys = p->keys;
    out->changed = changed;
    return 1;
  }
  if (!(b & FULL_PEN) || (b & FULL_NOT_PEN) || len < PEN_BYTES) return 0;
  p->mode = APP_PEN_MODE_FULL;
  p->full_pen = true;
  bool in_range = !(b & FULL_LEAVING) && (b & FULL_IN_RANGE);
  return pen_event(p, d, pen_flags(b, in_range), USB_PEN_DECO_MAX_X, USB_PEN_DECO_MAX_Y, USB_PEN_DECO_MAX_P, out);
}

static int std_report(usb_pen_t *p, const uint8_t *d, size_t len, app_pen_event_t *out) {
  if (p->full_pen) return 0; // full-mode pen frames come: one motion is never sent twice
  if (len < PEN_BYTES || d[0] != STD_ID) return 0;
  if (p->mode == APP_PEN_MODE_NONE) p->mode = APP_PEN_MODE_STANDARD; // full stays: its buttons still come
  return pen_event(p, d, pen_flags(d[1], d[1] & STD_IN_RANGE), USB_PEN_STD_MAX_XY, USB_PEN_STD_MAX_XY,
                   USB_PEN_STD_MAX_P, out);
}

int usb_pen_report(usb_pen_t *p, usb_pen_iface_t role, const uint8_t *d, size_t len, app_pen_event_t *out) {
  if (!p || !d || !out) return 0;
  if (role != USB_PEN_IFACE_FULL && role != USB_PEN_IFACE_STANDARD) return 0;
  if (len && d[0] == FULL_ID && p->pid == USB_PEN_PID_DECO01V2)
    return full_report(p, d, len, role == USB_PEN_IFACE_FULL, out);
  if (len && d[0] == STD_ID && role == USB_PEN_IFACE_STANDARD) return std_report(p, d, len, out);
  return 0;
}

void usb_pen_info(const usb_pen_t *p, app_pen_info_t *out) {
  memset(out, 0, sizeof(*out));
  if (!p || !p->vid) return;
  out->vid = p->vid;
  out->pid = p->pid;
  out->mode = p->mode;
  out->key_count = p->mode == APP_PEN_MODE_FULL ? 8 : 0;
  if (p->pid == USB_PEN_PID_DECO01V2) {
    out->width_um = USB_PEN_DECO_WIDTH_UM;
    out->height_um = USB_PEN_DECO_HEIGHT_UM;
  }
}

// ---- the queue ----

void usb_pen_queue_init(usb_pen_queue_t *q) { memset(q, 0, sizeof(*q)); }

static bool is_move(const app_pen_event_t *e) { return e->type == APP_EVENT_PEN && e->changed == 0; }

static app_pen_event_t *at(usb_pen_queue_t *q, unsigned i) { return &q->ev[(q->head + i) % USB_PEN_QUEUE]; }

void usb_pen_queue_push(usb_pen_queue_t *q, const app_pen_event_t *e) {
  if (q->count == USB_PEN_QUEUE) {
    q->dropped++;
    if (is_move(e)) {
      if (is_move(at(q, q->count - 1u))) *at(q, q->count - 1u) = *e; // full: the latest place only
      return;
    }
    unsigned i = 0;
    while (i < q->count && !is_move(at(q, i))) i++;
    if (i == q->count) return; // only changes unread: the app stopped reading
    for (; i + 1u < q->count; i++) *at(q, i) = *at(q, i + 1u); // the oldest waiting move goes
    q->count--;
  }
  *at(q, q->count) = *e;
  q->count++;
}

bool usb_pen_queue_take(usb_pen_queue_t *q, app_pen_event_t *out) {
  if (!q->count) return false;
  *out = q->ev[q->head];
  q->head = (uint16_t)((q->head + 1u) % USB_PEN_QUEUE);
  q->count--;
  return true;
}
