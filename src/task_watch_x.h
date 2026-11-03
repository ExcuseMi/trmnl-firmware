#pragma once

// The X's task watchdog (lib/trmnl/include/task_watch.h): ESP-IDF's TWDT restarts the X when a watched task stops
// feeding for TASK_WATCH_TIMEOUT_S (sdkconfig.TRMNL_X: CONFIG_ESP_TASK_WDT_PANIC). After the restart the port is
// serial again (usb_port_role.cpp, before setup), the event log says "boot: task watchdog" and names the task, and
// the app is not resumed (app_reset_of: a crash).
// Watched: the loop task from the first app start to the sleep or restart (app_runner.cpp; the TRMNL wake before it
// is not: its downloads and updates would need feeds of their own), the USB port task and the USB host library task
// while they run (usb_keyboard.cpp). The idle tasks are ESP-IDF's own.
#ifdef BOARD_TRMNL_X

// The calling task watched as `name` from now on (again: nothing changes); false if the watchdog refused it
bool task_watch_x_begin(const char *name);

// One turn of the calling task's loop; nothing for a task not watched. After "TAPP HANG <its name>" it never returns
void task_watch_x_feed(void);

// The calling task no longer watched (before it ends)
void task_watch_x_end(void);

// A legal step longer than the timeout and not a loop to feed in (the USB drive's import, a Wi-Fi join): the calling
// task is not watched during it, and watched again after when it was before (name "" when it was not)
typedef struct {
  char name[16];
} task_watch_x_pause_t;
void task_watch_x_pause(task_watch_x_pause_t *p);
void task_watch_x_resume(const task_watch_x_pause_t *p);

// "TAPP HANG <task>" over the cable (only the serial command paths call this): answered; false for another line
bool task_watch_x_command(const char *line);

#endif
