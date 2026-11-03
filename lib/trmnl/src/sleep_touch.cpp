#include "sleep_touch.h"

#include <stdio.h>

uint32_t sleep_touch_seconds(uint32_t interval_s, bool touch_ready) {
  if (touch_ready || interval_s <= SLEEP_TOUCH_FAILED_S) return interval_s;
  return SLEEP_TOUCH_FAILED_S;
}

int sleep_touch_line(char *out, size_t cap, uint32_t interval_s, int tries, bool touch_ready) {
  uint32_t s = sleep_touch_seconds(interval_s, touch_ready);
  if (touch_ready) {
    return snprintf(out, cap, "Sleep: %lu s, wake by timer and touch, touch prep ok%s", (unsigned long)s,
                    tries > 1 ? " on the retry" : "");
  }
  return snprintf(out, cap, "Sleep: %lu s (not %lu, touch prep failed %s), wake by timer and touch", (unsigned long)s,
                  (unsigned long)interval_s, tries > 1 ? "twice" : "once");
}
