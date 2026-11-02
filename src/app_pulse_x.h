// A running app's pulse in the event log (app_pulse.h): every APP_PULSE_MS with input or refreshes in the window, a
// "pen ..." line (reports, what the app took, refresh times, the panel lock's longest wait, the ink task's runs) and a
// "cpu % ..." line (the busiest tasks and the idle tasks, from FreeRTOS' run time). The counters are cheap (no lock).
#pragma once

#include <stdint.h>

#ifdef BOARD_TRMNL_X
void app_pulse_x_begin(void); // the app starts: counters and the run time base
void app_pulse_x_poll(void); // from the supervise loop (every APP_POLL_MS); writes the lines when the window is over
void app_pulse_x_end(void); // the app ended: the last window if it has anything
void app_pulse_x_pen_report(void);
void app_pulse_x_pen_taken(void);
void app_pulse_x_refresh(uint32_t lock_wait_ms, uint32_t total_ms); // a screen_refresh in the app's call
void app_pulse_x_ink(uint32_t ms); // one update of the ink task
void app_pulse_x_ink_request(void); // one APP_REFRESH_INK call (the queue merges them: runs <= requests)
#else
static inline void app_pulse_x_begin(void) {}
static inline void app_pulse_x_poll(void) {}
static inline void app_pulse_x_end(void) {}
static inline void app_pulse_x_pen_report(void) {}
static inline void app_pulse_x_pen_taken(void) {}
static inline void app_pulse_x_refresh(uint32_t, uint32_t) {}
static inline void app_pulse_x_ink(uint32_t) {}
static inline void app_pulse_x_ink_request(void) {}
#endif
