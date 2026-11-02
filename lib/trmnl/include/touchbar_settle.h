#pragma once

#include <stdbool.h>
#include <stdint.h>

// The touch bar right after a full IQS323 init (a restart, or a reinit after the chip reset): its ATI ran while the
// boot was still powering the display and the IO expander, and the bar fired on its own for about a minute (owner
// 2026-11-02). Until it has settled, no touch reaches the firmware; at TOUCHBAR_SETTLE_REATI_MS the task runs the ATI
// again on the quiet board, and the bar acts again once that ATI is done, at the latest TOUCHBAR_SETTLE_MAX_MS after
// the init. A channel touched when the bar opens acts only after its release. Pure: lib/trmnl_x/src/iqs323_task.cpp.

#define TOUCHBAR_SETTLE_REATI_MS 1500 // the second ATI, after the boot work around the first one
#define TOUCHBAR_SETTLE_DONE_MS 200   // a read without ATI this long after the second ATI opens the bar
#define TOUCHBAR_SETTLE_MAX_MS 3000   // hard limit: the bar is never shut longer after an init

typedef struct {
  bool active;      // settling: no touch passes
  uint32_t since_ms; // the init
  bool reati_due;   // the task should run the ATI now (touchbar_settle_take_reati)
  bool reati_sent;
  uint8_t last;     // touch bits of the last read
  uint8_t blocked;  // channels down when the bar opened: they pass after a read without them
} touchbar_settle_t;

// A full init (with its ATI) has just finished at `now_ms`.
void touchbar_settle_begin(touchbar_settle_t *s, uint32_t now_ms);
// Called each task loop, with or without a read: the time limits.
void touchbar_settle_tick(touchbar_settle_t *s, uint32_t now_ms);
// One read: `touch` bit n is channel n in touch, `ati_active` the chip's ATI flag. Returns the bits the firmware sees.
uint8_t touchbar_settle_filter(touchbar_settle_t *s, uint32_t now_ms, uint8_t touch, bool ati_active);
// True once when the second ATI is due.
bool touchbar_settle_take_reati(touchbar_settle_t *s);
