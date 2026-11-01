#pragma once

// Every USB device that enumerates in host mode, in the event log (TRMNL X): VID:PID, class, the hub port it is on,
// the power its configuration asks for (bMaxPower) and how VBUS was fed then; a hub with its port count. Its own USB
// host client, pumped from the port task (usb_keyboard.cpp); started after usb_host_install, stopped before the
// library's uninstall.

#include <stdbool.h>

bool usb_dev_log_start(void);
// Handles the client's events and logs new devices; `vbus` says how VBUS is fed ("boost on", "outside power yes")
void usb_dev_log_poll(const char *vbus);
void usb_dev_log_stop(void);
