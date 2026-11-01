#pragma once

// The running app's idle limit on the device (app_idle.h has the rules): the limit is read from the device settings
// when the app starts, every sign of a person is noted from whichever task sees it, the supervisor polls. 30 s before
// the end a small toast is drawn along the bottom edge over the app (only a 1 bit screen; the rows under it are kept
// and put back by the next sign of a person).

#include <app_idle.h>

void app_idle_x_begin(const char *app); // the limit for this app, the timer from now
void app_idle_x_note(void); // a touch, key, pen or mouse report: any task
void app_idle_x_hold(bool on); // app_idle_hold of the API
// The supervisor's poll: draws or removes the toast; APP_IDLE_EXPIRED once when the app is to be asked to exit
app_idle_event_t app_idle_x_poll(void);
int app_idle_x_minutes(void); // the limit in force (0: off)
void app_idle_x_end(void); // the app ended: no toast stays
