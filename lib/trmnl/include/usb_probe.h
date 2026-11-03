#pragma once

// The USB HID probe's pure part (src/usb_keyboard.cpp logs what an unknown USB device sends, e.g. a pen tablet):
// hex text for the event log in bounded lines, and the rate limit for input reports. I/O free.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_PROBE_DESC_MAX     320 // report descriptor bytes logged per interface (the rest is counted, not shown)
#define USB_PROBE_DESC_LINE    24 // descriptor bytes per event line
#define USB_PROBE_REPORT_BYTES 28 // bytes of one input report shown (one event line is 100 characters)
#define USB_PROBE_FULL_REPORTS 40 // reports per interface logged in full
#define USB_PROBE_PEN_REPORTS  20 // reports per pen tablet interface logged in full (then counted, logged at removal)
#define USB_PROBE_SUMMARY_MS   2000 // then one count line this often
#define USB_PROBE_HEX_CAP      (USB_PROBE_DESC_LINE * 3 + 1) // enough for a descriptor line or a report

// "0a 1b 2c" for the first `n` bytes that fit in `cap` (whole bytes, NUL ended); returns how many are shown
size_t usb_probe_hex(char *out, size_t cap, const uint8_t *d, size_t n);

// Descriptor of `len` bytes: the lines to log (cut at USB_PROBE_DESC_MAX) and the bytes `line` (0 based) holds at
// `*off`; 0 past the last line
size_t usb_probe_desc_lines(size_t len);
size_t usb_probe_desc_line(size_t len, size_t line, size_t *off);

typedef struct {
  volatile uint32_t total; // reports seen (counted in the USB task)
  uint32_t summed; // total at the last count line
  uint32_t summary_ms; // when the last count line was due
} usb_probe_t;

void usb_probe_init(usb_probe_t *p, uint32_t now);

// One report arrived: true while it is among the first USB_PROBE_FULL_REPORTS (log it in full)
bool usb_probe_report(usb_probe_t *p);
// The same with `first` reports logged in full
bool usb_probe_report_upto(usb_probe_t *p, uint32_t first);

// Polled: true when a count line is due (past the full ones, new reports, USB_PROBE_SUMMARY_MS since the last);
// `*count` is the total
bool usb_probe_summary(usb_probe_t *p, uint32_t now, uint32_t *count);

// ---- once per boot: a reconnect does not dump again (owner's X 2026-11-03: each tablet reconnect wrote about 25
// lines, descriptor and first reports, and pushed the crash line out of the event log) ----

#define USB_PROBE_ONCE_MAX 16 // interfaces remembered; more: dumped every time

typedef struct {
  uint16_t vid[USB_PROBE_ONCE_MAX], pid[USB_PROBE_ONCE_MAX];
  uint8_t iface[USB_PROBE_ONCE_MAX];
  uint8_t n;
} usb_probe_once_t;

// Interface `iface` of vid:pid connected: true when it was not seen since `t` was zeroed (dump its descriptor and its
// first reports), false for a reconnect; true when the table is full
bool usb_probe_once(usb_probe_once_t *t, uint16_t vid, uint16_t pid, uint8_t iface);

// The reports logged in full: `pen` (a tablet or mouse) USB_PROBE_PEN_REPORTS, else USB_PROBE_FULL_REPORTS; none when
// the interface was dumped before (`first` false)
uint32_t usb_probe_first_reports(bool first, bool pen);

// ---- a device's power and a hub's ports (src/usb_dev_log.cpp logs them when a device enumerates) ----

// A configuration's bMaxPower (2 mA units at full speed) in mA
uint16_t usb_probe_max_ma(uint8_t bMaxPower);

// A hub class descriptor (bDescriptorType 0x29) of `len` bytes: its bNbrPorts; -1 when `d` is not a whole one
int usb_probe_hub_ports(const uint8_t *d, size_t len);
