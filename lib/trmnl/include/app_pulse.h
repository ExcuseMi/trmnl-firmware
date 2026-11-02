// What a running app's time goes to, as event log lines every APP_PULSE_MS (pure part; src/app_pulse_x.cpp reads the
// counters and FreeRTOS' run time). Owner's X 2026-11-02: drawing lagged and nothing said where the time went.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define APP_PULSE_MS 20000
#define APP_PULSE_TASKS 24 // more tasks than this: the rest is not shown
#define APP_PULSE_NAME 16 // configMAX_TASK_NAME_LEN

typedef struct {
  char name[APP_PULSE_NAME];
  uint32_t run; // run time in the window, any unit
} app_pulse_task_t;

typedef struct {
  uint32_t pen_reports; // from the tablet
  uint32_t pen_taken; // by the app
  uint32_t pen_dropped; // the queue's count since pen_enable
  uint32_t refreshes; // screen_refresh calls that touched the panel in the app's call
  uint32_t refresh_ms; // their time, in all
  uint32_t refresh_max_ms;
  uint32_t lock_wait_max_ms; // the longest wait for the panel lock in those calls
  uint32_t ink_runs; // updates the ink task ran
  uint32_t ink_requests; // APP_REFRESH_INK calls (more than runs: the queue merged them while the panel was busy)
  uint32_t ink_ms;
} app_pulse_counts_t;

// "pen 3920/3918 drop 0, refresh 61 avg 142 max 410 ms, lock 12, ink 7/9 avg 90": the window's reports/taken, the
// queue's dropped count, the refreshes with their average and longest time, the longest wait for the panel lock (ms),
// the ink task's runs/requests and average; false when nothing happened in the window (no line then)
bool app_pulse_pen_line(const app_pulse_counts_t *c, char *out, size_t cap);

// "cpu % app 19 ink 15 iqs323_task 3 usb_lib 2 IDLE1 26 IDLE0 22": the four busiest tasks by share of `total` (the
// window's wall time: each core's tasks add up to 100), then the idle tasks. `t` is reordered. False when total is 0.
bool app_pulse_cpu_line(app_pulse_task_t *t, int n, uint32_t total, char *out, size_t cap);
