#include "usb_cursor.h"

#include <string.h>

#define UNIT    16 // usb_mouse.h's fixed point
#define BUTTONS (APP_PEN_TIP | APP_PEN_BARREL | APP_PEN_BARREL2)

void usb_cursor_init(usb_cursor_t *c, uint32_t w, uint32_t h) {
  memset(c, 0, sizeof(*c));
  usb_mouse_init(&c->cur, w, h);
  c->cur.flags = 0;
  c->x = c->y = (uint16_t)(c->cur.x / UNIT);
}

usb_cursor_owner_t usb_cursor_owner(const usb_cursor_t *c) {
  return c->pen_range ? USB_CURSOR_PEN : c->mouse ? USB_CURSOR_MOUSE : USB_CURSOR_NONE;
}

// Hands `e` out: `changed` against what the app saw last
static void emit(usb_cursor_t *c, usb_cursor_out_t *out, app_pen_event_t e) {
  e.changed = (uint8_t)(e.flags ^ c->last);
  c->last = e.flags;
  c->x = e.x;
  c->y = e.y;
  out->ev[out->n++] = e;
}

static void place(usb_cursor_t *c, uint16_t x, uint16_t y) {
  c->cur.x = (int32_t)x * UNIT;
  c->cur.y = (int32_t)y * UNIT;
}

static app_pen_event_t at_cursor(const usb_cursor_t *c, uint8_t flags) {
  app_pen_event_t e = {};
  e.type = APP_EVENT_PEN;
  e.flags = flags;
  e.x = c->x;
  e.y = c->y;
  return e;
}

void usb_cursor_tablet_attach(usb_cursor_t *c) { c->tablet = true; }

// The pen is not in range any more: the mouse (if any) hovers where the pen was, else the cursor leaves
static void pen_gone(usb_cursor_t *c, usb_cursor_out_t *out) {
  c->pen_range = false;
  place(c, c->x, c->y);
  c->cur.flags = c->mouse ? APP_PEN_IN_RANGE : 0;
  emit(c, out, at_cursor(c, c->mouse ? APP_PEN_IN_RANGE : 0));
}

void usb_cursor_tablet_detach(usb_cursor_t *c, usb_cursor_out_t *out) {
  out->n = 0;
  c->tablet = false;
  if (c->pen_range) pen_gone(c, out);
}

void usb_cursor_mouse_attach(usb_cursor_t *c, usb_cursor_out_t *out) {
  out->n = 0;
  c->mouse = true;
  c->mask = 0;
  app_pen_event_t e;
  usb_mouse_enter(&c->cur, &e); // sets the mouse's own range flag
  if (!c->pen_range) emit(c, out, e);
}

void usb_cursor_mouse_detach(usb_cursor_t *c, usb_cursor_out_t *out) {
  out->n = 0;
  c->mouse = false;
  c->mask = 0;
  c->cur.flags = 0;
  if (!c->pen_range && (c->last & APP_PEN_IN_RANGE)) emit(c, out, at_cursor(c, 0));
}

void usb_cursor_pen(usb_cursor_t *c, const app_pen_event_t *e, usb_cursor_out_t *out) {
  out->n = 0;
  if (e->type != APP_EVENT_PEN) return;
  if (e->flags & APP_PEN_IN_RANGE) {
    if (!c->pen_range && (c->last & BUTTONS)) emit(c, out, at_cursor(c, APP_PEN_IN_RANGE)); // the mouse lets go
    c->pen_range = true;
    c->cur.flags = APP_PEN_IN_RANGE;
    place(c, e->x, e->y);
    emit(c, out, *e);
    return;
  }
  if (!c->pen_range) return; // out again while out: the mouse keeps the cursor
  if (c->mouse) { // leaving: the mouse goes on from the last place the pen was in range
    pen_gone(c, out);
    return;
  }
  c->pen_range = false;
  c->cur.flags = 0;
  place(c, e->x, e->y);
  emit(c, out, *e);
}

void usb_cursor_mouse_move(usb_cursor_t *c, const usb_mouse_move_t *mv, usb_cursor_out_t *out) {
  out->n = 0;
  if (!c->mouse) return;
  if (c->pen_range) {
    c->mask = mv->buttons & 7u; // held now: stays ignored after the pen leaves
    return;
  }
  usb_mouse_move_t m = *mv;
  c->mask &= m.buttons;
  m.buttons &= (uint8_t)~c->mask;
  app_pen_event_t e;
  if (usb_mouse_move(&c->cur, &m, &e)) emit(c, out, e);
}

void usb_cursor_resync(usb_cursor_t *c, usb_cursor_out_t *out) {
  out->n = 0;
  c->last = 0;
  if (c->mouse && !c->pen_range) {
    app_pen_event_t e;
    c->cur.flags = 0;
    usb_mouse_enter(&c->cur, &e);
    emit(c, out, e);
  }
}
