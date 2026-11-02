#include <touchbar_settle.h>

void touchbar_settle_begin(touchbar_settle_t *s, uint32_t now_ms) {
  *s = {true, now_ms, false, false, 0, 0};
}

static void open_bar(touchbar_settle_t *s) {
  s->active = false;
  s->reati_due = false;
  s->blocked = s->last; // a touch that began while settling never acts; its channel waits for a release
}

void touchbar_settle_tick(touchbar_settle_t *s, uint32_t now_ms) {
  if (!s->active) return;
  uint32_t elapsed = now_ms - s->since_ms;
  if (elapsed >= TOUCHBAR_SETTLE_MAX_MS) {
    open_bar(s);
    return;
  }
  if (!s->reati_sent && elapsed >= TOUCHBAR_SETTLE_REATI_MS) s->reati_due = true;
}

uint8_t touchbar_settle_filter(touchbar_settle_t *s, uint32_t now_ms, uint8_t touch, bool ati_active) {
  s->last = touch;
  if (s->active) {
    touchbar_settle_tick(s, now_ms);
    if (s->active && s->reati_sent && !ati_active &&
        now_ms - s->since_ms >= TOUCHBAR_SETTLE_REATI_MS + TOUCHBAR_SETTLE_DONE_MS)
      open_bar(s);
    if (s->active) return 0;
  }
  s->blocked &= touch;
  return touch & (uint8_t)~s->blocked;
}

bool touchbar_settle_take_reati(touchbar_settle_t *s) {
  if (!s->reati_due) return false;
  s->reati_due = false;
  s->reati_sent = true;
  return true;
}
