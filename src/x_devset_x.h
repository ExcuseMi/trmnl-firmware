#pragma once

// The X's device settings (x_devset.h) in NVS (namespace "xdev"): the phone page writes them, apps read them with
// device_setting_get (API 2.11). The default printer is moved once from the Print app's old file
// (/appdata/trmnl-print/printer.cfg) when nothing is stored yet.

#include <stddef.h>

// The value (or the key's default) into buf, NUL-terminated: its length; APP_ENOENT for an unknown key or no printer;
// APP_EINVAL (buf NULL, cap too small)
int x_devset_get(const char *key, char *buf, size_t cap);
// Checked by xds_check first: 0, APP_ENOENT (unknown key), APP_EINVAL, APP_EIO. len 0 removes (the default again)
int x_devset_set(const char *key, const char *v, size_t len);
