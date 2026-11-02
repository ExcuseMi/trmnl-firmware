#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

// A header, parsed as `parsed`, while an update may wait in APP_UPLOAD_TMP for its app to exit (`pending`)
typedef enum {
  APP_CABLE_HDR_RECEIVE,       // nothing waits, the header is good: receive into APP_UPLOAD_TMP
  APP_CABLE_HDR_REFUSE,        // nothing waits, the header is bad: TAPP ERR (a temp file there is stale)
  APP_CABLE_HDR_KEEP_WAITING,  // an update waits: TAPP ERR, good header or bad, and its file is NOT touched
} app_cable_header_plan_t;

// Owner's X 2026-11-02: a bad header while an update waited removed the waiting file (the update was lost)
app_cable_header_plan_t app_cable_header_plan(bool pending, app_upload_result_t parsed);

// The reason after "TAPP ERR " for APP_CABLE_REFUSE_PORT
const char *app_cable_refusal(usb_role_t role);

// A line of serial bytes, any length: keeps the last APP_UPLOAD_LINE_MAX - 1 bytes (a header may follow noise). The
// app_serial.h line is shorter than a header, so the cable watches the same bytes with this.
typedef struct {
  char line[APP_UPLOAD_LINE_MAX];
  unsigned len;
  bool ended; // the last feed ended a line
} app_cable_line_t;

void app_cable_line_init(app_cable_line_t *l);

// One byte. True when it ended a line that holds "TAPP PUT ": *header is its "TAPP PUT ..." part (valid until the next feed)
bool app_cable_line_feed(app_cable_line_t *l, unsigned char b, const char **header);

// After a feed that returned false: the "TAPP ..." part of the line it ended when that is not a "TAPP PUT" (a command:
// "TAPP EVLOG", "TAPP LS", "TAPP DF"), else nullptr. Valid until the next feed
const char *app_cable_line_command(const app_cable_line_t *l);

// Bytes of an upload may reach the app's key reader or the menu: while an upload is received nothing else reads the port
void app_cable_busy_set(bool busy);
bool app_cable_busy(void);

// Deep sleep waits for the cable (goToSleep): while an upload is received (it ends by itself: APP_UPLOAD_IDLE_MS
// without a byte), and APP_CABLE_QUIET_MS after the host's last byte (between two files, a header sent again, the
// host's wake loop). Owner's X 2026-11-02: a 1 MB app over usbip took longer than the old 60 s hold and the X slept
// mid-file; the next file's header came after the busy flag dropped and found it asleep. A host that never stops
// sending is held APP_CABLE_HOLD_MAX_MS at most.
#define APP_CABLE_QUIET_MS 15000u
#define APP_CABLE_HOLD_MAX_MS (10u * 60u * 1000u)

// A byte came from the host at `now_ms`
void app_cable_rx_note(uint32_t now_ms);
// Whether a sleep asked at `asked_ms` still waits at `now_ms` (pure over the busy flag and the last host byte)
bool app_cable_sleep_hold(uint32_t asked_ms, uint32_t now_ms);
