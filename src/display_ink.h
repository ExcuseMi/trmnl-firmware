#pragma once

// APP_REFRESH_INK on the X (docs/apps.md "Fast ink"): a firmware task runs the app's ink updates while the app goes on
// drawing. The rules are lib/trmnl's ink_queue; this file holds the task, the lock and the driver calls.

#include <stdint.h>

// Queues rows y0..y1 (checked) for an ink update; returns at once with 0, or the error of an earlier update that failed
// (APP_EIO). Without its task (no memory) the update runs here, blocking
int display_ink_request(int y0, int y1);
// 1 while updates are queued or running, 0 when the panel shows all of them
int display_ink_busy(void);
// Waits until no update is queued or running (for FULL, PARTIAL, a mode change, the app's end): 0, or APP_EIO when it
// did not get there in timeout_ms (the caller goes on: the panel lock still orders it after the running one)
int display_ink_wait_idle(uint32_t timeout_ms);

#define DISPLAY_INK_WAIT_MS 3000 // a few updates of ~150 ms; more means the task is stuck
