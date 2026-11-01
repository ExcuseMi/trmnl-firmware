#pragma once

#include <stdint.h>

// The USB devices the X has seen since it last started (the X's page, Devices): each VID:PID once, newest first, at
// most USB_SEEN_MAX (the oldest goes). The firmware keeps the list in RTC memory; usb_dev_log adds each device that
// enumerates and the keyboard, mouse and pen code name what they recognised. Pure.

#define USB_SEEN_MAX 8
#define USB_SEEN_KIND_MAX 12

typedef struct {
  uint16_t vid, pid;
  uint8_t cls; // bDeviceClass
  char kind[USB_SEEN_KIND_MAX]; // "keyboard", "mouse", "pen tablet", "hub", "device"
} usb_seen_item_t;

typedef struct {
  uint32_t magic; // USB_SEEN_MAGIC once initialised (RTC memory is random after power on)
  int n;
  usb_seen_item_t it[USB_SEEN_MAX];
} usb_seen_t;

#define USB_SEEN_MAGIC 0x55534231u

// An empty list unless it already holds a valid one
void usb_seen_init(usb_seen_t *l);
// Adds or refreshes vid:pid at the front. kind nullptr or "": "hub" for class 9, else keeps a known kind or "device".
// A named kind replaces "device" (the keyboard code knows more than the enumeration)
void usb_seen_add(usb_seen_t *l, uint16_t vid, uint16_t pid, uint8_t cls, const char *kind);
