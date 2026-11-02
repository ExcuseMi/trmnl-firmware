// A running app's pulse in the event log (app_pulse_x.h)
#ifdef BOARD_TRMNL_X

#include "app_pulse_x.h"

#include <Arduino.h>
#include <app_pulse.h>
#include <esp_heap_caps.h>
#include <evlog_ring.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "evlog.h"
#include "usb_pen_x.h"

static volatile uint32_t pen_reports, pen_taken, refreshes, refresh_ms, refresh_max, lock_wait_max, ink_runs, ink_requests,
    ink_ms;
static uint32_t window_at;
static bool running;

// the run time of every task at the window's start, by name (a task that comes or goes mid-window counts from 0)
static TaskStatus_t *status; // APP_PULSE_TASKS, allocated once
static app_pulse_task_t base[APP_PULSE_TASKS];
static int base_n;
static uint32_t base_total;

static void snapshot(app_pulse_task_t *out, int *n, uint32_t *total) {
  *n = 0;
  *total = 0;
  if (!status) status = (TaskStatus_t *)heap_caps_malloc(sizeof(TaskStatus_t) * APP_PULSE_TASKS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!status) return;
  configRUN_TIME_COUNTER_TYPE run_total = 0;
  UBaseType_t got = uxTaskGetSystemState(status, APP_PULSE_TASKS, &run_total);
  for (UBaseType_t i = 0; i < got; i++) {
    strncpy(out[*n].name, status[i].pcTaskName ? status[i].pcTaskName : "?", APP_PULSE_NAME - 1);
    out[*n].name[APP_PULSE_NAME - 1] = '\0';
    out[*n].run = (uint32_t)status[i].ulRunTimeCounter;
    (*n)++;
  }
  *total = (uint32_t)run_total;
}

static void window_start(void) {
  pen_reports = 0;
  pen_taken = 0;
  refreshes = 0;
  refresh_ms = 0;
  refresh_max = 0;
  lock_wait_max = 0;
  ink_runs = 0;
  ink_requests = 0;
  ink_ms = 0;
  window_at = millis();
  snapshot(base, &base_n, &base_total);
}

static void window_lines(void) {
  app_pen_info_t info = {};
  usb_pen_x_info(&info); // the queue's dropped count since pen_enable
  app_pulse_counts_t c = {pen_reports,   pen_taken, info.dropped, refreshes, refresh_ms, refresh_max, lock_wait_max,
                          ink_runs,      ink_requests, ink_ms};
  char line[EVLOG_TEXT_MAX + 1];
  if (!app_pulse_pen_line(&c, line, sizeof(line))) return; // nothing happened: no lines
  evlog_add("%s", line);
  app_pulse_task_t now[APP_PULSE_TASKS];
  int n;
  uint32_t total;
  snapshot(now, &n, &total);
  if (!n || !base_n) return;
  for (int i = 0; i < n; i++) { // the window's run time: now minus the base of the same name
    for (int j = 0; j < base_n; j++) {
      if (strcmp(now[i].name, base[j].name) == 0) {
        // a task of that name made again in the window (the USB host's on a restart of host mode) counts from 0
        if (now[i].run >= base[j].run) now[i].run -= base[j].run;
        break;
      }
    }
  }
  uint32_t window_total = total - base_total;
  if (app_pulse_cpu_line(now, n, window_total, line, sizeof(line))) evlog_add("%s", line);
}

void app_pulse_x_begin(void) {
  running = true;
  window_start();
}

void app_pulse_x_poll(void) {
  if (!running || millis() - window_at < APP_PULSE_MS) return;
  window_lines();
  window_start();
}

void app_pulse_x_end(void) {
  if (!running) return;
  running = false;
  window_lines();
}

void app_pulse_x_pen_report(void) { pen_reports = pen_reports + 1; }
void app_pulse_x_pen_taken(void) { pen_taken = pen_taken + 1; }

void app_pulse_x_refresh(uint32_t lock_wait_ms, uint32_t total_ms) {
  refreshes = refreshes + 1;
  refresh_ms = refresh_ms + total_ms;
  if (total_ms > refresh_max) refresh_max = total_ms;
  if (lock_wait_ms > lock_wait_max) lock_wait_max = lock_wait_ms;
}

void app_pulse_x_ink(uint32_t ms) {
  ink_runs = ink_runs + 1;
  ink_ms = ink_ms + ms;
}

void app_pulse_x_ink_request(void) { ink_requests = ink_requests + 1; }

#endif // BOARD_TRMNL_X
