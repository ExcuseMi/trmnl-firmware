#include "ink_queue.h"

ink_plan_t ink_plan(int how, bool one_bit) {
  switch (how) {
  case APP_REFRESH_INK:
    if (one_bit) return {INK_PLAN_QUEUE, APP_INK_PASSES, false};
    return {INK_PLAN_FULL, 0, true}; // as PARTIAL: a gray screen gets a full one
  case APP_REFRESH_PARTIAL:
    if (one_bit) return {INK_PLAN_PARTIAL, DISPLAY_PARTIAL_PASSES, true};
    return {INK_PLAN_FULL, 0, true};
  case APP_REFRESH_FULL: return {INK_PLAN_FULL, 0, true};
  default: return {INK_PLAN_BAD, 0, false};
  }
}

void ink_queue_init(ink_queue_t *q) { *q = ink_queue_t{false, 0, 0, false, 0}; }

bool ink_queue_add(ink_queue_t *q, int y0, int y1) {
  if (q->pending) {
    if (y0 < q->y0) q->y0 = (int16_t)y0;
    if (y1 > q->y1) q->y1 = (int16_t)y1;
    return false;
  }
  q->pending = true;
  q->y0 = (int16_t)y0;
  q->y1 = (int16_t)y1;
  return true;
}

bool ink_queue_take(ink_queue_t *q, int *y0, int *y1) {
  if (!q->pending) return false;
  *y0 = q->y0;
  *y1 = q->y1;
  q->pending = false;
  q->running = true;
  return true;
}

void ink_queue_done(ink_queue_t *q, bool ok) {
  q->running = false;
  if (!ok) q->error = APP_EIO;
}

bool ink_queue_idle(const ink_queue_t *q) { return !q->pending && !q->running; }

int ink_queue_take_error(ink_queue_t *q) {
  int e = q->error;
  q->error = 0;
  return e;
}
