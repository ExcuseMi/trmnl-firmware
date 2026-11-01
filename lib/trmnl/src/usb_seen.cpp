// The USB devices seen since the X started (usb_seen.h).
#include "usb_seen.h"

#include <stdio.h>
#include <string.h>

void usb_seen_init(usb_seen_t *l) {
  if (l->magic == USB_SEEN_MAGIC && l->n >= 0 && l->n <= USB_SEEN_MAX) return;
  memset(l, 0, sizeof(*l));
  l->magic = USB_SEEN_MAGIC;
}

void usb_seen_add(usb_seen_t *l, uint16_t vid, uint16_t pid, uint8_t cls, const char *kind) {
  usb_seen_init(l);
  usb_seen_item_t it = {vid, pid, cls, ""};
  int at = -1;
  for (int i = 0; i < l->n; i++) {
    if (l->it[i].vid == vid && l->it[i].pid == pid) at = i;
  }
  if (at >= 0) {
    it = l->it[at];
    if (cls) it.cls = cls;
    memmove(&l->it[1], &l->it[0], (size_t)at * sizeof(it));
  } else {
    if (l->n < USB_SEEN_MAX) l->n++;
    memmove(&l->it[1], &l->it[0], (size_t)(l->n - 1) * sizeof(it));
  }
  if (kind && kind[0]) snprintf(it.kind, sizeof(it.kind), "%s", kind);
  else if (!it.kind[0] || !strcmp(it.kind, "device")) snprintf(it.kind, sizeof(it.kind), "%s", it.cls == 9 ? "hub" : "device");
  l->it[0] = it;
}
