#include "usb_probe.h"

size_t usb_probe_hex(char *out, size_t cap, const uint8_t *d, size_t n) {
  static const char hx[] = "0123456789abcdef";
  if (!out || cap == 0) return 0;
  size_t shown = 0, w = 0;
  while (shown < n && d && w + (shown ? 3 : 2) < cap) {
    if (shown) out[w++] = ' ';
    out[w++] = hx[d[shown] >> 4];
    out[w++] = hx[d[shown] & 15];
    shown++;
  }
  out[w] = 0;
  return shown;
}

size_t usb_probe_desc_lines(size_t len) {
  if (len > USB_PROBE_DESC_MAX) len = USB_PROBE_DESC_MAX;
  return (len + USB_PROBE_DESC_LINE - 1) / USB_PROBE_DESC_LINE;
}

size_t usb_probe_desc_line(size_t len, size_t line, size_t *off) {
  if (len > USB_PROBE_DESC_MAX) len = USB_PROBE_DESC_MAX;
  size_t start = line * USB_PROBE_DESC_LINE;
  if (line >= usb_probe_desc_lines(len)) return 0;
  *off = start;
  size_t left = len - start;
  return left < USB_PROBE_DESC_LINE ? left : USB_PROBE_DESC_LINE;
}

void usb_probe_init(usb_probe_t *p, uint32_t now) {
  p->total = 0;
  p->summed = 0;
  p->summary_ms = now;
}

bool usb_probe_report(usb_probe_t *p) {
  uint32_t t = p->total;
  if (t != 0xFFFFFFFFu) p->total = t + 1;
  return t < USB_PROBE_FULL_REPORTS;
}

bool usb_probe_summary(usb_probe_t *p, uint32_t now, uint32_t *count) {
  uint32_t t = p->total;
  if (t <= USB_PROBE_FULL_REPORTS || t == p->summed) return false;
  if ((uint32_t)(now - p->summary_ms) < USB_PROBE_SUMMARY_MS) return false;
  p->summed = t;
  p->summary_ms = now;
  *count = t;
  return true;
}
