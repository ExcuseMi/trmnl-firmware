#pragma once

// A USB mouse as a pen for apps (app_api.h pen_enable, docs/apps.md "Pen tablet", mode APP_PEN_MODE_MOUSE), the pure
// part: the mouse's report layout (boot protocol or read from a report descriptor), a report into motion and
// buttons, and the cursor that motion moves. src/usb_pen_x.cpp does the USB side. I/O free.
//
// Left button = tip (pressure USB_MOUSE_PRESSURE), right = APP_PEN_BARREL (Sketch: eraser), middle = APP_PEN_BARREL2,
// the wheel is ignored. The cursor is in range from usb_mouse_enter to usb_mouse_leave (a mouse is there).

#include <app_api.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_MOUSE_PRESSURE 32768
#define USB_MOUSE_MAP_MAX  512 // longest report descriptor looked at

typedef struct {
  uint8_t report_id; // 0: no report ID byte
  int16_t btn_bit; // bit offset of the buttons (after the ID byte, if any), -1 none
  uint8_t btn_count; // buttons in that field (only the first 3 are used)
  int16_t x_bit, y_bit; // bit offsets of relative X and Y
  uint8_t x_size, y_size; // their sizes in bits, 1..16, signed
  uint16_t report_bits; // length of the report in bits, with the ID byte
} usb_mouse_layout_t;

// The boot protocol mouse report: buttons, X, Y (i8), an optional wheel
void usb_mouse_boot_layout(usb_mouse_layout_t *out);

// The first mouse (Generic Desktop / Mouse, relative X and Y) of a report descriptor; false if it has none, or the
// descriptor is cut off or too long
bool usb_mouse_map_parse(const uint8_t *map, size_t len, usb_mouse_layout_t *out);

typedef enum { USB_MOUSE_PICK_NONE, USB_MOUSE_PICK_BOOT, USB_MOUSE_PICK_REPORT } usb_mouse_pick_t;

// Which layout an interface's reports have, and so which protocol to set: a descriptor that holds a mouse wins
// (REPORT, the protocol a device starts in and the one its descriptor describes); else a boot mouse interface
// (boot_iface: subclass boot, protocol mouse) takes the boot layout (BOOT). NONE: not a mouse. *out filled unless NONE
usb_mouse_pick_t usb_mouse_pick(const uint8_t *map, size_t len, bool boot_iface, usb_mouse_layout_t *out);

typedef struct {
  uint8_t buttons; // bit 0 left, 1 right, 2 middle
  int32_t dx, dy;
} usb_mouse_move_t;

// One input report (with its report ID byte if the layout has one) into *out. False: another report ID or shorter than
// the layout says
bool usb_mouse_report(const usb_mouse_layout_t *l, const uint8_t *d, size_t len, usb_mouse_move_t *out);

// ---- the cursor ----

typedef struct {
  int32_t x, y; // 1/16 of a pen unit (0..65535 * 16)
  uint32_t w, h; // the screen's shape (pixels); the same movement on screen in both directions
  uint8_t flags; // APP_PEN_* last sent
  uint8_t quarters; // the speed: gain in quarters of normal (USB_MOUSE_SLOW .. USB_MOUSE_FAST)
} usb_mouse_t;

#define USB_MOUSE_SLOW   2 // half
#define USB_MOUSE_NORMAL 4
#define USB_MOUSE_FAST   7 // 1.75 times

// At the centre, out of range, normal speed. w and h: the aspect of the screen (0: square)
void usb_mouse_init(usb_mouse_t *m, uint32_t w, uint32_t h);
// The device setting mouse_speed (x_devset.h: slow|normal|fast) as quarters; anything else (none stored): normal
uint8_t usb_mouse_speed(const char *setting);
// Scales every later move; a value outside SLOW..FAST is taken as normal
void usb_mouse_set_speed(usb_mouse_t *m, uint8_t quarters);

// The mouse came: in range at the cursor. 1 with *out filled
int usb_mouse_enter(usb_mouse_t *m, app_pen_event_t *out);
// The mouse went: nothing stays down, out of range. 1 with *out filled, 0 if nothing was on
int usb_mouse_leave(usb_mouse_t *m, app_pen_event_t *out);
// Moves the cursor (8 units per count at normal speed, up to twice that for fast motion, clamped to the edges) and takes the buttons.
// 1 with *out filled, 0 when nothing changed (no motion left after clamping, same buttons). Only while in range
int usb_mouse_move(usb_mouse_t *m, const usb_mouse_move_t *mv, app_pen_event_t *out);

// pen_info's fields for a mouse (width_um, height_um: the screen's aspect)
void usb_mouse_info(uint16_t vid, uint16_t pid, uint32_t width_um, uint32_t height_um, app_pen_info_t *out);
