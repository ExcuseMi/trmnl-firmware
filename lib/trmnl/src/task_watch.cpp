#include "task_watch.h"

#include <stdio.h>
#include <string.h>

#define HANG_CMD "TAPP HANG"

static task_watch_entry_t *find(task_watch_t *w, const void *task) {
  for (int i = 0; i < TASK_WATCH_MAX; i++)
    if (w->e[i].task && w->e[i].task == task) return &w->e[i];
  return nullptr;
}

static task_watch_entry_t *find_name(task_watch_t *w, const char *name) {
  for (int i = 0; i < TASK_WATCH_MAX; i++)
    if (w->e[i].task && strcmp(w->e[i].name, name) == 0) return &w->e[i];
  return nullptr;
}

bool task_watch_add(task_watch_t *w, const void *task, const char *name, uint32_t now_ms) {
  if (!task || !name) return false;
  size_t n = strlen(name);
  if (n == 0 || n > TASK_WATCH_NAME_MAX) return false;
  if (task_watch_entry_t *e = find(w, task)) {
    e->fed_ms = now_ms;
    return true;
  }
  if (find_name(w, name)) return false;
  for (int i = 0; i < TASK_WATCH_MAX; i++) {
    task_watch_entry_t *e = &w->e[i];
    if (e->task) continue;
    memset(e, 0, sizeof(*e));
    memcpy(e->name, name, n + 1);
    e->fed_ms = now_ms;
    e->task = task;
    return true;
  }
  return false;
}

void task_watch_remove(task_watch_t *w, const void *task) {
  if (task_watch_entry_t *e = find(w, task)) memset(e, 0, sizeof(*e));
}

task_watch_feed_t task_watch_feed(task_watch_t *w, const void *task, uint32_t now_ms) {
  task_watch_entry_t *e = find(w, task);
  if (!e) return TASK_WATCH_NOT_WATCHED;
  if (e->hang) return TASK_WATCH_HANG;
  e->fed_ms = now_ms;
  return TASK_WATCH_FED;
}

task_watch_cmd_t task_watch_command(task_watch_t *w, const char *line, char *name, size_t name_len) {
  if (name_len) name[0] = '\0';
  if (!line || strncmp(line, HANG_CMD, sizeof(HANG_CMD) - 1) != 0) return TASK_WATCH_CMD_NONE;
  const char *arg = line + sizeof(HANG_CMD) - 1;
  if (*arg != ' ' && *arg != '\0') return TASK_WATCH_CMD_NONE; // "TAPP HANGUP" is another word
  if (*arg == ' ') arg++;
  size_t n = strcspn(arg, " \r\n");
  if (name_len) snprintf(name, name_len, "%.*s", (int)n, arg);
  if (n == 0 || n > TASK_WATCH_NAME_MAX || arg[n] != '\0') return TASK_WATCH_CMD_NOT_WATCHED;
  task_watch_entry_t *e = find_name(w, arg);
  if (!e) return TASK_WATCH_CMD_NOT_WATCHED;
  e->hang = true;
  return TASK_WATCH_CMD_HANG;
}

const char *task_watch_name(const task_watch_t *w, const void *task) {
  task_watch_entry_t *e = find(const_cast<task_watch_t *>(w), task);
  return e ? e->name : nullptr;
}

const char *task_watch_stalest(const task_watch_t *w, uint32_t now_ms, uint32_t *age_ms) {
  const task_watch_entry_t *worst = nullptr;
  uint32_t worst_age = 0;
  for (int i = 0; i < TASK_WATCH_MAX; i++) {
    const task_watch_entry_t *e = &w->e[i];
    if (!e->task) continue;
    uint32_t age = now_ms - e->fed_ms;
    if (!worst || age > worst_age) {
      worst = e;
      worst_age = age;
    }
  }
  if (age_ms) *age_ms = worst_age;
  return worst ? worst->name : nullptr;
}
