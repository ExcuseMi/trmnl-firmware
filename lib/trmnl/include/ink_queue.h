#pragma once

// The X's app refreshes (screen_refresh, docs/apps.md "Fast ink"): what each kind does, and the queue of
// APP_REFRESH_INK requests a firmware task runs while the app goes on drawing (src/display_ink.cpp). Pure: no locks,
// the caller holds one.
//
// Hand-over rules (why no copy of the framebuffer is needed): FastEPD's partialUpdate reads each framebuffer byte once,
// at its start, and stores it as the panel's state (its previous buffer), so a byte the app draws during an update is
// either in that update or shows as a difference in the next one. What the queue must keep: a request that arrives
// while one runs waits as pending even when its rows are inside the running ones (the app may have drawn them after
// the driver read them), and FULL, PARTIAL and a mode change wait until the queue is idle.

#include <app_api.h>
#include <stdint.h>

// Passes for APP_REFRESH_INK (white<->black only; PARTIAL uses DISPLAY_PARTIAL_PASSES). Each pass, and the driver's
// neutral scan after them, scans all rows of the panel: ~51 ms each on the X, whatever the rows asked for
#ifndef APP_INK_PASSES
#define APP_INK_PASSES 3 // owner 2026-11-02: same as PARTIAL (2 was undone)
#endif
#define DISPLAY_PARTIAL_PASSES 3 // src/display.cpp setPasses(3, 3)

enum ink_plan_kind_t {
  INK_PLAN_QUEUE, // run by the firmware's task, the call returns at once
  INK_PLAN_PARTIAL, // partialUpdate now, blocking
  INK_PLAN_FULL, // fullUpdate now, blocking
  INK_PLAN_BAD, // not a refresh kind: APP_EINVAL
};

struct ink_plan_t {
  ink_plan_kind_t kind;
  int passes; // for QUEUE and PARTIAL
  bool wait_idle; // the queue must be empty and idle before it runs
};

// One screen_refresh(how) in a 1-bit (one_bit) or gray screen
ink_plan_t ink_plan(int how, bool one_bit);

struct ink_queue_t {
  bool pending; // rows wait
  int16_t y0, y1; // the pending rows, merged: the first and last
  bool running; // the task updates the panel now
  int error; // 0, or APP_EIO: an update failed since the app last asked
};

void ink_queue_init(ink_queue_t *q);
// The app asks for rows y0..y1 (checked by the caller). Returns true when the task must be woken (nothing waited)
bool ink_queue_add(ink_queue_t *q, int y0, int y1);
// The task takes the pending rows to run them: true with *y0, *y1, false when none wait
bool ink_queue_take(ink_queue_t *q, int *y0, int *y1);
// The task finished the rows it took
void ink_queue_done(ink_queue_t *q, bool ok);
// Nothing waits and nothing runs: the panel shows every request
bool ink_queue_idle(const ink_queue_t *q);
// The error of a finished update (0 or APP_EIO), once
int ink_queue_take_error(ink_queue_t *q);
