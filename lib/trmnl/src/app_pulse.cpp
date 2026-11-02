// A running app's pulse lines (app_pulse.h)
#include "app_pulse.h"

#include <stdio.h>
#include <string.h>

bool app_pulse_pen_line(const app_pulse_counts_t *c, char *out, size_t cap) {
  if (!c || !out || !cap) return false;
  if (!c->pen_reports && !c->pen_taken && !c->refreshes && !c->ink_runs && !c->ink_requests) return false;
  unsigned avg = c->refreshes ? (unsigned)(c->refresh_ms / c->refreshes) : 0;
  unsigned ink_avg = c->ink_runs ? (unsigned)(c->ink_ms / c->ink_runs) : 0;
  snprintf(out, cap, "pen %lu/%lu drop %lu, refresh %lu avg %u max %lu ms, lock %lu, ink %lu/%lu avg %u",
           (unsigned long)c->pen_reports, (unsigned long)c->pen_taken, (unsigned long)c->pen_dropped,
           (unsigned long)c->refreshes, avg, (unsigned long)c->refresh_max_ms, (unsigned long)c->lock_wait_max_ms,
           (unsigned long)c->ink_runs, (unsigned long)c->ink_requests, ink_avg);
  return true;
}

static bool idle_task(const app_pulse_task_t *t) { return strncmp(t->name, "IDLE", 4) == 0; }

static unsigned share(uint32_t run, uint32_t total) { return (unsigned)(((uint64_t)run * 200u + total / 2) / total); }

bool app_pulse_cpu_line(app_pulse_task_t *t, int n, uint32_t total, char *out, size_t cap) {
  if (!t || n <= 0 || !total || !out || !cap) return false;
  // the busiest first (insertion sort: n is small)
  for (int i = 1; i < n; i++) {
    app_pulse_task_t k = t[i];
    int j = i - 1;
    while (j >= 0 && t[j].run < k.run) {
      t[j + 1] = t[j];
      j--;
    }
    t[j + 1] = k;
  }
  size_t len = (size_t)snprintf(out, cap, "cpu %%");
  int shown = 0;
  for (int i = 0; i < n && shown < 4 && len < cap; i++) {
    if (idle_task(&t[i]) || !t[i].run) continue;
    len += (size_t)snprintf(out + len, cap - len, " %s %u", t[i].name, share(t[i].run, total));
    shown++;
  }
  for (int i = 0; i < n && len < cap; i++) {
    if (!idle_task(&t[i])) continue;
    len += (size_t)snprintf(out + len, cap - len, " %s %u", t[i].name, share(t[i].run, total));
  }
  return true;
}
