#pragma once
#ifdef BOARD_TRMNL_X

// Updating apps over the USB cable while the X is awake (lib/trmnl/include/app_cable.h, docs/apps.md "Update over the
// cable"). Not running: a task reads the port. An app running: the app runner's supervise loop hands the header to
// app_cable_x_header (the app keeps the port's keys otherwise).

// Starts the task for the times no app runs; call once at boot
void app_cable_x_start(void);

// A "TAPP PUT ..." header line seen while an app runs: receives the file here (the caller is blocked until it ends)
void app_cable_x_header(const char *header);

// A serial command line that is not a header ("TAPP EVLOG", "TAPP LS", "TAPP DF"): answered; false if unknown
bool app_cable_x_command(const char *line);

// True when the app running now must exit so its new file can replace it (the same app was uploaded); the caller posts
// APP_EVENT_EXIT once
bool app_cable_x_exit_wanted(void);

// The app ended (or did not exit in time): installs the waiting file. True when the new file is in place; `path` is the
// app to start again
bool app_cable_x_finish(char *path, unsigned len);

#endif
