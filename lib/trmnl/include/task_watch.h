#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Which tasks the X's task watchdog watches (src/task_watch_x.cpp, ESP-IDF's TWDT with a panic): a watched task feeds
// once per turn of its loop; a turn that blocks longer than TASK_WATCH_TIMEOUT_S (a lock or queue wait that never
// ends, a spin) restarts the X. Owner's X 2026-11-03: frozen after a long Sketch session, the port left in host mode,
// no restart by itself. Pure bookkeeping here; the TWDT calls are the device's.
//
// The timeout, against the longest legal block between two feeds (read 2026-11-03, no device measurement):
//   loop task, after an app: the last image decoded and a full refresh (a few s), the ink wait (DISPLAY_INK_WAIT_MS
//   3 s), the USB host stop (STOP_DEADLINE_MS 1 s); a cable upload feeds per block, its install copies one file
//   usb_port turn: host stop 1 s + charger lock wait USB_POWER_LOCK_WAIT_MS 1.5 s + boost 0.2 s + host start
//   the idle tasks (watched by ESP-IDF itself, the same timeout): a CPU-bound step on the loop's core (PNG decode,
//   ELF relocation) must not starve IDLE1 that long
// About 10 s at most: 30 s leaves three times that, and a frozen X is back within half a minute.
#define TASK_WATCH_TIMEOUT_S 30 // = CONFIG_ESP_TASK_WDT_TIMEOUT_S in sdkconfigs/sdkconfig.TRMNL_X (checked there)
#define TASK_WATCH_MAX 4
#define TASK_WATCH_NAME_MAX 11

typedef struct {
  const void *task; // nullptr: free
  char name[TASK_WATCH_NAME_MAX + 1];
  uint32_t fed_ms;
  bool hang; // "TAPP HANG <name>": block at the next feed
} task_watch_entry_t;

typedef struct {
  task_watch_entry_t e[TASK_WATCH_MAX];
} task_watch_t;

// `task` watched as `name` (fed now); false when the table is full, the name is empty, too long or taken by another
// task. The same task again: true, the name kept
bool task_watch_add(task_watch_t *w, const void *task, const char *name, uint32_t now_ms);

// `task` no longer watched (before it ends: ESP-IDF's watchdog would wait for a deleted task); a hang asked for it goes
void task_watch_remove(task_watch_t *w, const void *task);

typedef enum {
  TASK_WATCH_NOT_WATCHED, // nothing to feed: the caller does not touch the watchdog
  TASK_WATCH_FED,
  TASK_WATCH_HANG, // the test hang asked for this task: the caller blocks for good (and is not fed)
} task_watch_feed_t;

task_watch_feed_t task_watch_feed(task_watch_t *w, const void *task, uint32_t now_ms);

typedef enum {
  TASK_WATCH_CMD_NONE,        // not "TAPP HANG ..."
  TASK_WATCH_CMD_HANG,        // asked: the task blocks at its next feed
  TASK_WATCH_CMD_NOT_WATCHED, // no watched task by that name now: nothing happens (a hang that nothing restarts)
} task_watch_cmd_t;

// "TAPP HANG <name>" (serial only, a test of the watchdog on the device); the name to `name` (may be cut short)
task_watch_cmd_t task_watch_command(task_watch_t *w, const char *line, char *name, size_t name_len);

// The name `task` is watched as, nullptr when it is not
const char *task_watch_name(const task_watch_t *w, const void *task);

// The watched task fed longest ago and how long ago (for the watchdog's note); nullptr when none is watched
const char *task_watch_stalest(const task_watch_t *w, uint32_t now_ms, uint32_t *age_ms);
