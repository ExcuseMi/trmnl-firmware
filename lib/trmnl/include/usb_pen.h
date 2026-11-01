#pragma once

// A USB pen tablet for apps (app_api.h pen_enable, docs/apps.md "Pen tablet"), the pure part: which interface of a
// tablet does what, its reports into app_pen_event_t, and the queue that merges moves the app has not read yet.
// src/usb_pen_x.cpp does the USB side. I/O free.
//
// XP-Pen Deco 01 V2 (28bd:0905), measured on the owner's tablet (2026-11-01):
// - standard mode (no setup): interface 1, report 07, 10 bytes: [07][flags][x u16][y u16][pressure u16][tilt x i8]
//   [tilt y i8], little endian, x and y 0..32767, pressure 0..8191; flags 0x01 tip, 0x02 lower button, 0x04 upper
//   button, 0x20 in range. The tablet's 8 buttons send nothing.
// - full mode: usb_pen_full_mode_cmd as an output report to interface 2 (Linux hid-uclogic, ugee v2). Then interface
//   2 sends report 02, 12 bytes: pen [02][a0 hover | a1 tip | c0 leaving][x u16][y u16][pressure u16][tilt x i8]
//   [tilt y i8][0 0], buttons [02][f0][mask: bit 0..7 one button each, 0 released][0..].
//   The pen buttons in full mode (0x02, 0x04 of byte 1, as in standard mode and as Linux reads them) are not in the
//   capture: unproven. Full-mode maxima are not known (Linux reads them from string descriptor 200, which stalled
//   this tablet): USB_PEN_DECO_* below are the maker's area (10 x 6.25 in at 5080 lines per inch, 8192 levels).
//   Seen in a short test: x 493..24150, y 150..14909, pressure up to 3918. Unproven.

#include <app_api.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_PEN_VID_XPPEN    0x28bd
#define USB_PEN_PID_DECO01V2 0x0905

// Full mode maxima measured on the owner's tablet (device 2026-11-01: raw max x 25400 y 15405 p 8191); the maker's
// 5080 lpi figure (50800 x 31750) left three quarters of the screen out of reach. 15875 = 6.25 in at 2540 lpi
#define USB_PEN_DECO_MAX_X     25400
#define USB_PEN_DECO_MAX_Y     15875
#define USB_PEN_DECO_MAX_P     8191
#define USB_PEN_DECO_WIDTH_UM  254000
#define USB_PEN_DECO_HEIGHT_UM 158750
#define USB_PEN_STD_MAX_XY     32767 // standard report 07
#define USB_PEN_STD_MAX_P      8191

#define USB_PEN_FULL_IFACE 2
#define USB_PEN_STD_IFACE  1
#define USB_PEN_CMD_LEN    10
extern const uint8_t usb_pen_full_mode_cmd[USB_PEN_CMD_LEN]; // 02 b0 04 00 00 00 00 00 00 00

typedef enum {
  USB_PEN_IFACE_NONE, // not a tablet: the keyboard or the probe as before
  USB_PEN_IFACE_IGNORE, // a tablet's other interface (its keyboard or mouse): opened by nobody, never typed keys
  USB_PEN_IFACE_STANDARD, // report 07
  USB_PEN_IFACE_FULL, // the full-mode switch, then report 02
} usb_pen_iface_t;

// Interface `iface` of device vid:pid. Every XP-Pen (28bd) interface is a tablet's; full mode only on the Deco 01 V2
// (the one with known data), standard mode on interface 1 of other XP-Pen models
usb_pen_iface_t usb_pen_iface_role(uint16_t vid, uint16_t pid, uint8_t iface);

typedef struct {
  uint16_t vid, pid;
  uint8_t mode; // APP_PEN_MODE_* of the reports taken: NONE until the first one; FULL from the first full-mode one
  uint8_t flags; // APP_PEN_* last sent
  uint8_t keys; // tablet buttons last sent
  int8_t tilt_x, tilt_y;
  uint16_t x, y, pressure; // scaled, last sent
  uint16_t raw_max_x, raw_max_y, raw_max_p; // the largest raw values of pen frames (logged at removal)
  uint32_t pen_frames; // pen frames taken, either mode (logged at removal)
  bool full_pen; // a full-mode pen frame came: standard reports are ignored from then on
} usb_pen_t;

void usb_pen_init(usb_pen_t *p, uint16_t vid, uint16_t pid);

// One input report (with its report ID byte) from an interface with `role` (STANDARD or FULL): 1 with *out filled, 0
// for none (not a pen or button report, too short, a button report that changes nothing or not from the FULL interface,
// a standard report once a full-mode pen frame came). On the X (2026-11-01) the button frames came on interface 2
// after the switch but no pen frame did, so a pen staying on interface 1 (report 07) is taken until full-mode pen
// frames come; full-mode pen frames (02, Deco 01 V2 only) count from either interface, report 07 only from interface
// 1. Byte 1 b0 (x 4: the tablet's answer to the switch, seen as "raw max x 4") and f0 are not pen frames
int usb_pen_report(usb_pen_t *p, usb_pen_iface_t role, const uint8_t *d, size_t len, app_pen_event_t *out);

// `raw` of 0..`max` to 0..65535 (0 and max are the ends; over max is 65535; max 0 gives 0)
uint16_t usb_pen_scale(uint32_t raw, uint32_t max);

// pen_info's fields for this tablet (vid 0: none)
void usb_pen_info(const usb_pen_t *p, app_pen_info_t *out);

// ---- the queue to the app ----

// Holds the pen events the app has not taken, every move in order (device 2026-11-01: merging unread moves into the
// latest turned curves into straight lines while the app waited on a panel refresh). 512 events = 2.5 s at 200 Hz.
// Only when full: a move onto a move at the end replaces it; a change (tip, range, a pen or tablet button) is never
// merged, a waiting move makes room for it, and only with USB_PEN_QUEUE changes unread is one dropped. Every drop is
// counted
#define USB_PEN_QUEUE 512

typedef struct {
  app_pen_event_t ev[USB_PEN_QUEUE];
  uint16_t head, count;
  uint32_t dropped;
} usb_pen_queue_t;

void usb_pen_queue_init(usb_pen_queue_t *q);
void usb_pen_queue_push(usb_pen_queue_t *q, const app_pen_event_t *e);
// The oldest: true with *out filled, false when empty
bool usb_pen_queue_take(usb_pen_queue_t *q, app_pen_event_t *out);
