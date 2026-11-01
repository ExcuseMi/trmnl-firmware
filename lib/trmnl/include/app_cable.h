#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <app_upload.h>
#include <usb_port.h>

// Installing an app over the USB cable while the X is awake, without the Receive over USB menu (docs/apps.md "Update
// over the cable"). The protocol is app_upload.h's; this is the decision part, I/O free. The firmware glue is
// src/app_cable.cpp (not running) and the app runner's supervise loop (an app runs).

typedef enum {
  APP_CABLE_INSTALL_NOW,           // no app runs: replace the file at once
  APP_CABLE_INSTALL_KEEP_RUNNING,  // another app runs: it keeps running, the new file is installed
  APP_CABLE_REPLACE_AND_RESTART,   // the same app runs: ask it to exit (saves), replace, start it again
  APP_CABLE_REFUSE_PORT,           // the PHY is in USB host or drive mode (keyboard, pen, drive): TAPP ERR
} app_cable_action_t;

// What to do with a received and checked upload of `path` (/apps/<name>.elf). `running`: the app running now or nullptr
app_cable_action_t app_cable_plan(usb_role_t role, const char *running, const char *path);

// The reason after "TAPP ERR " for APP_CABLE_REFUSE_PORT
const char *app_cable_refusal(usb_role_t role);

// A line of serial bytes, any length: keeps the last APP_UPLOAD_LINE_MAX - 1 bytes (a header may follow noise). The
// app_serial.h line is shorter than a header, so the cable watches the same bytes with this.
typedef struct {
  char line[APP_UPLOAD_LINE_MAX];
  unsigned len;
} app_cable_line_t;

void app_cable_line_init(app_cable_line_t *l);

// One byte. True when it ended a line that holds "TAPP PUT ": *header is its "TAPP PUT ..." part (valid until the next feed)
bool app_cable_line_feed(app_cable_line_t *l, unsigned char b, const char **header);

// Bytes of an upload may reach the app's key reader or the menu: while an upload is received nothing else reads the port
void app_cable_busy_set(bool busy);
bool app_cable_busy(void);
