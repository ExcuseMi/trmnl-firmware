// The X's device settings in NVS (x_devset_x.h)
#ifdef BOARD_TRMNL_X

#include <LittleFS.h>
#include <Preferences.h>
#include <app_api.h>
#include <string.h>
#include <trmnl_log.h>
#include <x_devset.h>

#include "usb_pen_x.h"
#include "x_devset_x.h"

#define NS "xdev"
#define OLD_PRINTER_FILE "/appdata/trmnl-print/printer.cfg" // the Print app's file before API 2.11

// The Print app's old file into NVS, once, when it holds a printer the checks accept
static void move_old_printer(Preferences &p) {
  File f = LittleFS.open(OLD_PRINTER_FILE, "r");
  if (!f) return;
  char buf[XDS_VALUE_MAX];
  size_t n = f.size() <= sizeof(buf) ? f.read((uint8_t *)buf, f.size()) : 0;
  f.close();
  if (n && xds_check("printer", buf, n, nullptr, 0) == 0 && p.putBytes("printer", buf, n) == n) {
    Log_info("Device settings: the default printer moved from the Print app's file");
  }
}

int x_devset_get(const char *key, char *buf, size_t cap) {
  const xds_key_t *k = xds_find(key);
  if (!k) return APP_ENOENT;
  if (!buf || cap == 0) return APP_EINVAL;
  Preferences p;
  size_t n = 0;
  if (p.begin(NS, false)) {
    if (!strcmp(key, "printer") && !p.isKey("printer")) move_old_printer(p);
    n = p.isKey(key) ? p.getBytesLength(key) : 0;
    if (n >= cap) n = cap; // too small: refused below
    else if (n) n = p.getBytes(key, buf, n);
    p.end();
  }
  if (n >= cap) return APP_EINVAL;
  if (!n) {
    if (!k->def[0]) return APP_ENOENT; // no printer yet
    if (strlen(k->def) >= cap) return APP_EINVAL;
    n = strlen(k->def);
    memcpy(buf, k->def, n);
  }
  buf[n] = '\0';
  return (int)n;
}

int x_devset_set(const char *key, const char *v, size_t len) {
  int rc = xds_check(key, v, len, nullptr, 0);
  if (rc) return rc;
  Preferences p;
  if (!p.begin(NS, false)) return APP_EIO;
  if (len == 0) {
    if (p.isKey(key)) p.remove(key);
    rc = 0;
  } else {
    rc = p.putBytes(key, v, len) == len ? 0 : APP_EIO;
  }
  p.end();
  if (rc == 0 && !strcmp(key, "mouse_speed")) usb_pen_x_mouse_speed_reload(); // a mouse plugged in now follows at once
  return rc;
}

#endif // BOARD_TRMNL_X
