#pragma once

// The X's deep sleep and its touch bar wake (src/sleep_session.cpp), pure. The touch controller (IQS323) must be put
// in its event mode before the sleep, or a touch does not wake the X (owner's X 2026-11-03: about an hour asleep, the
// touch bar did not wake it, only the timer did). A failed preparation is tried once more; still failed, the X sleeps
// only SLEEP_TOUCH_FAILED_S, so it is never blind for a whole refresh interval.

#include <stddef.h>
#include <stdint.h>

#define SLEEP_TOUCH_FAILED_S 60 // the longest sleep without a prepared touch wake

// The sleep's seconds: the refresh interval with the touch bar ready, else at most SLEEP_TOUCH_FAILED_S
uint32_t sleep_touch_seconds(uint32_t interval_s, bool touch_ready);

// The event log line: "Sleep: 900 s, wake by timer and touch, touch prep ok" (`tries` 1 or 2: the preparations run);
// failed: "Sleep: 60 s (not 900, touch prep failed twice), wake by timer and touch". Its length as snprintf's
int sleep_touch_line(char *out, size_t cap, uint32_t interval_s, int tries, bool touch_ready);
