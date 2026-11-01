#pragma once

// Who owns the cursor when a pen tablet and a USB mouse are both attached (docs/apps.md "Pen tablet"), the pure part;
// src/usb_pen_x.cpp does the USB side. I/O free.
//
// - The tablet's pen in range (hover or tip) owns the cursor; the mouse's reports are ignored meanwhile.
// - The pen out of range: the mouse drives, starting at the pen's last place; its buttons held while the pen was
//   in range stay ignored until released.
// - The pen coming back takes over at once; a mouse button down then is released first (an event at the mouse's place).
// - One stream: every event handed out has `changed` against the flags last handed out, so the app sees one cursor.
// Tablet buttons (APP_EVENT_PEN_KEY) do not pass here.

#include <app_api.h>
#include <stdbool.h>
#include <stdint.h>
#include <usb_mouse.h>

typedef struct {
  app_pen_event_t ev[2];
  uint8_t n; // events to queue, in order
} usb_cursor_out_t;

typedef struct {
  bool tablet, mouse; // attached
  bool pen_range; // the tablet's pen owns the cursor
  uint8_t mask; // mouse buttons ignored until released
  uint8_t last; // APP_PEN_* last handed out
  uint16_t x, y; // where the cursor was last handed out
  usb_mouse_t cur; // the mouse's own cursor
} usb_cursor_t;

typedef enum { USB_CURSOR_NONE, USB_CURSOR_PEN, USB_CURSOR_MOUSE } usb_cursor_owner_t;

void usb_cursor_init(usb_cursor_t *c, uint32_t w, uint32_t h); // w, h: the screen's shape for the mouse
usb_cursor_owner_t usb_cursor_owner(const usb_cursor_t *c);

void usb_cursor_tablet_attach(usb_cursor_t *c);
void usb_cursor_tablet_detach(usb_cursor_t *c, usb_cursor_out_t *out); // the pen leaves range if it was in
void usb_cursor_mouse_attach(usb_cursor_t *c, usb_cursor_out_t *out); // hovers at the cursor unless the pen owns it
void usb_cursor_mouse_detach(usb_cursor_t *c, usb_cursor_out_t *out); // nothing stays down

// A pen event (type APP_EVENT_PEN) from the tablet; an out-of-range one while it was already out is dropped
void usb_cursor_pen(usb_cursor_t *c, const app_pen_event_t *e, usb_cursor_out_t *out);
// A mouse report; ignored (n 0) while the pen owns the cursor
void usb_cursor_mouse_move(usb_cursor_t *c, const usb_mouse_move_t *mv, usb_cursor_out_t *out);
// The app asked for the pen (again): forget what it saw; a mouse not shadowed hovers
void usb_cursor_resync(usb_cursor_t *c, usb_cursor_out_t *out);
