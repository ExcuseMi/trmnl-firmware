#pragma once

#include <stdbool.h>
#include <stddef.h>

// The X's own device settings, one owner for what several apps share (docs/apps.md "Device settings"): the default
// printer, the pen tablet's area, the mouse speed, the screen clean-up. The phone page writes them (the X's page,
// Devices), apps read them with device_setting_get (API 2.11). Pure: the keys, their defaults and the value checks;
// the firmware keeps the values in NVS (namespace "xdev").

#define XDS_KEY_MAX 15
#define XDS_VALUE_MAX 512 // the printer's text

typedef struct {
  const char *key;
  const char *label; // the page's
  const char *def; // the value when none is stored ("" for the printer: none)
  const char *choices; // "a|b" for a choice, nullptr otherwise
  int min, max; // a number's range (choices nullptr and the printer aside)
} xds_key_t;

// The keys, in the page's order; n set to their count
const xds_key_t *xds_keys(int *n);
// The key's entry, or nullptr for an unknown key
const xds_key_t *xds_find(const char *key);
// 0, APP_ENOENT (unknown key) or APP_EINVAL with `why` (the value is never copied into it). printer: key=value lines
// as the Print app's printer.cfg (name, host, port, path, format), host required (letters, digits, '.', '-', ':'),
// port 1..65535, path of [A-Za-z0-9/._-] without "..", format pwg|pcl, name printable; an empty value clears it
int xds_check(const char *key, const char *value, size_t len, char *why, size_t why_len);
