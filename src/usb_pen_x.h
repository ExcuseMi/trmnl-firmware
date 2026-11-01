#pragma once

// A USB pen tablet for apps (app_api.h pen_enable, docs/apps.md "Pen tablet"), the X's side: the full-mode switch, the
// tablet's state and the merging queue (lib/trmnl usb_pen.h) behind a lock, and the wake-up token in the app's event
// queue. usb_keyboard.cpp opens the tablet's interfaces and hands their reports here.

#include <app_api.h>
#include <stddef.h>
#include <stdint.h>
#include <usb_mouse.h>
#include <usb_pen.h>

#ifdef BOARD_TRMNL_X

// ---- the app's calls ----

int usb_pen_x_enable(bool on); // pen_enable: 0 or APP_ENOMEM
int usb_pen_x_info(app_pen_info_t *out); // pen_info
bool usb_pen_x_wanted(void);

// input_poll: the oldest pen event into *ev (true), false when none waits. An APP_EVENT_PEN that input_poll takes
// from the app's queue is only the token that pen data waits: it calls usb_pen_x_token_taken, then usb_pen_x_take
bool usb_pen_x_take(app_event_t *ev);
void usb_pen_x_token_taken(void);
bool usb_pen_x_waiting(void); // sleep_ms: pen data waits

// ---- for usb_keyboard.cpp ----

// A tablet interface came (the same vid:pid keeps the state of its other interface) or the tablet went
void usb_pen_x_attach(uint16_t vid, uint16_t pid);
void usb_pen_x_detach(void);

// One input report (the HID driver's task): parsed and queued for the app when it wants the pen
void usb_pen_x_report(usb_pen_iface_t role, const uint8_t *d, size_t len);

// The full-mode command to interface `iface` of the device at `addr` as an interrupt OUT transfer (what Linux does),
// before the HID driver claims the interface. 0, or an esp_err_t (then the caller tries SET_REPORT once)
int usb_pen_x_full_mode(uint8_t addr, uint8_t iface);

// A USB mouse (lib/trmnl usb_mouse.h) draws as a pen in mode APP_PEN_MODE_MOUSE while the app wants the pen. A tablet
// wins: while one is attached the mouse's reports are dropped, and pen_info says tablet. When the tablet goes, a mouse
// still attached comes back in range. Its interface layout comes with each report
void usb_pen_x_mouse_attach(uint16_t vid, uint16_t pid);
void usb_pen_x_mouse_detach(void);
void usb_pen_x_mouse_report(const usb_mouse_layout_t *l, const uint8_t *d, size_t len);

// The mode of the reports taken so far (APP_PEN_MODE_*), for the connect log
uint8_t usb_pen_x_mode(void);

#endif
