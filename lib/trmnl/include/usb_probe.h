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
