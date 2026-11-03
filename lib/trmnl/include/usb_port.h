#pragma once

#include <stdbool.h>
#include <stdint.h>

// ---- who owns the PHY ----

// The S3 has one USB PHY. Its role: the USB serial/JTAG (serial log, flashing; the default and what every
// switch-back returns to), the OTG controller as USB host (keyboards), or the OTG controller as TinyUSB device
// (the USB drive). One role at a time: a feature takes the PHY and gives it back; src/usb_port_role.cpp applies it.
typedef enum { USB_ROLE_SERIAL, USB_ROLE_HOST, USB_ROLE_DRIVE } usb_role_t;

typedef struct {
  usb_role_t role;
} usb_roles_t;

void usb_roles_init(usb_roles_t *r);

// True when `want` (HOST or DRIVE) may have the PHY now: it is free (SERIAL) or already `want`'s
bool usb_role_take(usb_roles_t *r, usb_role_t want);

// True when `had` held the PHY: it is SERIAL again and the caller switches back. False: someone else holds it
// (or nobody): nothing to switch, so one feature never pulls the PHY from under another
bool usb_role_give(usb_roles_t *r, usb_role_t had);

const char *usb_role_str(usb_role_t role);

// ---- keyboard host mode ----

// Who has the X's USB-C port (docs/apps.md "Keyboards"): device mode (charging, the serial port, flashing) is the
// default; host mode (the 5 V boost on, a USB keyboard on an OTG adapter) only while an app wants USB keys AND
// the charger has seen no input power for USB_PORT_CLEAR_MS AND a keyboard enumerates. Pure logic: the caller
// polls every USB_PORT_POLL_MS and applies the result (boost and PHY).
//
// Host mode starts as a probe: without a keyboard after USB_PORT_PROBE_MS it is left and tried again after
// USB_PORT_RETRY_MS. It is left in the same step on input power (or an unreadable charger), on the keyboard's
// removal, or when the app no longer wants keys. A restart starts in device mode (usb_port_init).
//
// Outside power (a powered hub or a Y/OTG adapter with power in feeds VBUS, or a computer): the boost never goes on
// (never 5 V against an outside supply). An app that wants USB host gets one try with the boost OFF after
// USB_PORT_CLEAR_MS of outside power: host mode while a device is there; none within USB_PORT_OUTSIDE_PROBE_MS (a
// computer) or the device removed: device mode (serial, flashing) and no new try until the outside power goes away or
// the app asks again. `boost` says which host mode it is.

#define USB_PORT_POLL_MS  100
// config/apps.json: USB_PORT_CLEAR_MS no input power this long before the boost may go on, USB_PORT_PROBE_MS host
// mode without a keyboard: back to device mode, USB_PORT_RETRY_MS then this long in device mode before the next probe,
// USB_PORT_OUTSIDE_PROBE_MS host mode with outside power without a device: back to serial
#include <apps_config.h>

typedef enum { USB_POWER_NO, USB_POWER_YES, USB_POWER_UNKNOWN } usb_power_t;

typedef enum { USB_PORT_DEVICE, USB_PORT_HOST } usb_port_mode_t;

typedef enum {
  USB_WHY_NOT_WANTED, // no app wants USB keys
  USB_WHY_INPUT_POWER, // a charger or computer powers the port
  USB_WHY_POWER_UNKNOWN, // the charger could not be read: never boost blind
  USB_WHY_WAIT_CLEAR, // no input power, not yet for USB_PORT_CLEAR_MS
  USB_WHY_PROBING, // host mode, waiting for a keyboard
  USB_WHY_KEYBOARD, // host mode, a keyboard is there
  USB_WHY_NO_KEYBOARD, // the probe found none, or it was removed: waiting USB_PORT_RETRY_MS
  USB_WHY_REJOIN, // host mode, the device was removed: it enumerates again when back; the stack is not stopped and
                  // started for it while the app wants the port (a stop with the hub mid-change asserted in ext_hub.c)
  USB_WHY_PORT_TAKEN, // another role (the USB drive) has the PHY
  USB_WHY_OUTSIDE_PROBING, // host mode, boost off, outside power: waiting for a device (a hub's or an adapter's)
  USB_WHY_OUTSIDE_DEVICE, // host mode, boost off, outside power: a device is there
  USB_WHY_OUTSIDE_NONE, // the outside power try found none or the device left: serial until the power goes away
} usb_port_why_t;

typedef struct {
  usb_port_mode_t mode;
  usb_port_why_t why;
  bool clear; // no input power since clear_since
  uint32_t clear_since;
  uint32_t host_since; // when host mode began
  uint32_t gone_since; // USB_WHY_REJOIN: when the device was removed
  uint32_t retry_at; // device mode until then after a probe without a keyboard
  bool retry_wait;
  bool boost; // host mode with the X's boost on; false: host mode on outside power (or device mode)
  bool powered; // outside power (or unreadable) since powered_since
  uint32_t powered_since;
  bool outside_tried; // the one try on this outside power is used
} usb_port_t;

typedef struct {
  bool wanted; // an app asked for USB keys
  usb_power_t power; // the charger's input power good
  bool keyboard; // a HID keyboard (or the wanted pen tablet) is enumerated (host mode only)
  bool port_taken; // the PHY is in another role than serial or host (the USB drive shows)
} usb_port_input_t;

void usb_port_init(usb_port_t *p);

// One poll at `now_ms`; returns the mode the port must be in now. Host mode means: PHY to USB-OTG, the boost on only
// when `p->boost`.
usb_port_mode_t usb_port_step(usb_port_t *p, uint32_t now_ms, const usb_port_input_t *in);

const char *usb_port_why_str(usb_port_why_t why);

// ---- the charger reading ----

// The charger's power-good pin sits on the IO expander behind the panel's lock (app_host.panel). Each poll tries the
// lock without waiting: `got` true with the pin's level in `input_power`, false when the lock was busy. A busy lock
// (a refresh: full or gray, seconds) keeps the last reading and the next free poll reads again; on 97fff83 it read as
// unknown after 1 s and host mode dropped in every long refresh (the lost first key). Only a lock held this long, a
// hang rather than a refresh, counts as unknown (never boost blind):
#define USB_POWER_STALE_MS 30000

typedef struct {
  usb_power_t last;
  uint32_t at; // when `last` was read
} usb_power_reading_t;

void usb_power_reading_init(usb_power_reading_t *r);

// One poll at `now_ms`; returns what usb_port_input_t.power gets
usb_power_t usb_power_update(usb_power_reading_t *r, uint32_t now_ms, bool got, bool input_power);

// How long this poll waits for the lock: 0 while the reading is fresh, USB_POWER_LOCK_WAIT_MS once it is
// USB_POWER_WAIT_AFTER_MS old. Back-to-back ink updates while drawing (one after another for minutes) left no free
// moment for a poll that never waits: the reading went stale after 30 s, "charger unreadable" stopped host mode and
// switched the boost off under the tablet (fw-latest29, 2026-11-03). A waiting take gets the lock between two updates
// (the mutex's priority inheritance hurries the holder; the port task outranks the ink task).
#define USB_POWER_WAIT_AFTER_MS 5000
#define USB_POWER_LOCK_WAIT_MS 1500
uint32_t usb_power_lock_wait_ms(const usb_power_reading_t *r, uint32_t now_ms);

// ---- the boost's switch-on ----

// The boost pin is on the expander too, behind the same lock. One 200 ms try on entering host mode failed in a
// refresh (2026-11-01, "boost not switched on (expander busy)") and a tablet plugged after it got no power until it
// was replugged. In host mode the caller tries again while the boost is off: every USB_BOOST_FAST_MS for the first
// USB_BOOST_FAST_TRIES tries (about 1 s), then every USB_BOOST_SLOW_MS for as long as host mode lasts.
#define USB_BOOST_FAST_MS    250
#define USB_BOOST_FAST_TRIES 4
#define USB_BOOST_SLOW_MS    2000

typedef enum {
  USB_BOOST_LOG_NONE,
  USB_BOOST_LOG_FAILED, // the fast tries all failed (logged once)
  USB_BOOST_LOG_ON_LATE, // on, after at least one failed try
} usb_boost_log_t;

typedef struct {
  bool on;
  uint8_t tries; // failed so far
  uint32_t next_at;
} usb_boost_t;

void usb_boost_init(usb_boost_t *b, uint32_t now_ms); // host mode began: the first try is due now
bool usb_boost_due(const usb_boost_t *b, uint32_t now_ms); // off and a try is due
// A try's result at `now_ms`: what to log
usb_boost_log_t usb_boost_result(usb_boost_t *b, uint32_t now_ms, bool ok);

// ---- leaving host mode: the order of the steps ----

// The stack powers its root port off itself (usb_host_lib_set_root_port_power): every device and the external hub are
// gone through the host library's own path, then freed; the boost goes off last. With the boost off first (as
// before 2026-11-02) the hub's interrupt transfer failed while it was being freed, the hub was released twice and
// ESP-IDF's ext_hub.c asserted (waiting_release): IllegalInstruction when Sketch handed over to Print, and on "back
// to serial". usb_host_device_free_all needs every client deregistered first.
typedef enum {
  USB_STOP_CLOSE_HANDLES, // our HID interfaces closed
  USB_STOP_PORT_OFF, // the root port off: the devices and hubs gone through the stack, VBUS still on
  USB_STOP_CLIENTS_OFF, // the HID driver (once its devices are gone) and the device log deregister
  USB_STOP_WAIT_GONE, // until the library lists no device (USB_STOP_GONE_MS at most): the hub freed by its own path
  USB_STOP_FREE_DEVICES, // usb_host_device_free_all, until ALL_FREE
  USB_STOP_HOST_OFF, // usb_host_uninstall, the PHY given back
  USB_STOP_BOOST_OFF, // VBUS off
} usb_stop_step_t;
#define USB_STOP_STEPS 7
// free_all marks every hub still listed for release a second time (ext_hub_mark_all_free) while the root port's
// off already released it through ext_hub_dev_gone: with a hub whose children are still being freed that is the
// double release of 2026-11-02 (fw-usb25, #176: IllegalInstruction at ext_hub.c:509 on "no device, back to serial")
#define USB_STOP_GONE_MS 500
extern const usb_stop_step_t usb_stop_plan[USB_STOP_STEPS];
