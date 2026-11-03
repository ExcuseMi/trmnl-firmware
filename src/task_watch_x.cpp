// The task watchdog's device side (task_watch_x.h): ESP-IDF's TWDT and the watched table (task_watch.h)
#ifdef BOARD_TRMNL_X

#include "task_watch_x.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <task_watch.h>
#include <string.h>
#include <trmnl_log.h>

#include "app_receive.h"
#include "evlog.h"
#include "evlog_crash.h"

static_assert(CONFIG_ESP_TASK_WDT_TIMEOUT_S == TASK_WATCH_TIMEOUT_S, "sdkconfig.TRMNL_X: the timeout task_watch.h argues for");
#ifndef CONFIG_ESP_TASK_WDT_PANIC
#error "sdkconfig.TRMNL_X: CONFIG_ESP_TASK_WDT_PANIC off, a hung task would only print (owner's X 2026-11-03)"
#endif

static task_watch_t watched;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void) { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

bool task_watch_x_begin(const char *name) {
  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&mux);
  bool known = task_watch_feed(&watched, self, now_ms()) != TASK_WATCH_NOT_WATCHED;
  portEXIT_CRITICAL(&mux);
  if (known) return true;
  esp_err_t rc = esp_task_wdt_add(nullptr);
  if (rc != ESP_OK) {
    Log_error("Watchdog: %s not watched (%s)", name, esp_err_to_name(rc));
    return false;
  }
  portENTER_CRITICAL(&mux);
  bool ok = task_watch_add(&watched, self, name, now_ms());
  portEXIT_CRITICAL(&mux);
  if (!ok) { // the table is full or the name taken: not watched, rather than watched and never fed
    esp_task_wdt_delete(nullptr);
    Log_error("Watchdog: %s not watched (table full or name taken)", name);
    return false;
  }
  Log_info("Watchdog: %s watched, %d s", name, TASK_WATCH_TIMEOUT_S);
  return true;
}

static void hang_for_good(void) {
  Log_event("Watchdog: test hang (TAPP HANG), the task watchdog restarts the X in %d s", TASK_WATCH_TIMEOUT_S);
  evlog_add("Watchdog: test hang (TAPP HANG), blocked for good");
  SemaphoreHandle_t never = xSemaphoreCreateBinary(); // a lock nobody gives, as in a real hang
  while (true) {
    if (never) xSemaphoreTake(never, portMAX_DELAY);
    else vTaskDelay(portMAX_DELAY);
  }
}

void task_watch_x_feed(void) {
  portENTER_CRITICAL(&mux);
  task_watch_feed_t f = task_watch_feed(&watched, xTaskGetCurrentTaskHandle(), now_ms());
  portEXIT_CRITICAL(&mux);
  if (f == TASK_WATCH_FED) esp_task_wdt_reset();
  else if (f == TASK_WATCH_HANG) hang_for_good();
}

void task_watch_x_end(void) {
  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&mux);
  bool known = task_watch_feed(&watched, self, now_ms()) != TASK_WATCH_NOT_WATCHED;
  task_watch_remove(&watched, self);
  portEXIT_CRITICAL(&mux);
  if (known) esp_task_wdt_delete(nullptr);
}

void task_watch_x_pause(task_watch_x_pause_t *p) {
  p->name[0] = '\0';
  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&mux);
  const char *name = task_watch_name(&watched, self);
  if (name) {
    strncpy(p->name, name, sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';
  }
  portEXIT_CRITICAL(&mux);
  if (p->name[0]) task_watch_x_end();
}

void task_watch_x_resume(const task_watch_x_pause_t *p) {
  if (p->name[0]) task_watch_x_begin(p->name);
}

bool task_watch_x_command(const char *line) {
  char name[TASK_WATCH_NAME_MAX + 2];
  portENTER_CRITICAL(&mux);
  task_watch_cmd_t c = task_watch_command(&watched, line, name, sizeof(name));
  portEXIT_CRITICAL(&mux);
  if (c == TASK_WATCH_CMD_NONE) return false;
  if (c == TASK_WATCH_CMD_HANG) {
    evlog_add("Watchdog: test hang of %s asked over the cable", name);
    evlog_flush(false); // on file before the hang: the restart's record has only the last few events
    apps_reply("HANG OK %s: blocks at its next turn, restart in %d s", name, TASK_WATCH_TIMEOUT_S);
  } else {
    apps_reply("ERR hang: \"%s\" is not watched now (loop: while an app runs; usb_port, usb_lib: in host mode)", name);
  }
  return true;
}

// ESP-IDF calls this from the watchdog's interrupt just before the panic: the task that stopped feeding, for the log
extern "C" void esp_task_wdt_isr_user_handler(void) {
  portENTER_CRITICAL_ISR(&mux);
  uint32_t age = 0;
  const char *name = task_watch_stalest(&watched, (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS), &age);
  // one of ours fed within half the timeout did not trip it: a spin starved IDLE0 or IDLE1 (watched by ESP-IDF)
  if (name && age >= TASK_WATCH_TIMEOUT_S * 1000u / 2) evlog_crash_watchdog(name, age);
  else evlog_crash_watchdog("an idle task", 0);
  portEXIT_CRITICAL_ISR(&mux);
}

#endif // BOARD_TRMNL_X
