// The X's ink task (display_ink.h)
#ifdef BOARD_TRMNL_X

#include "display_ink.h"

#include <Arduino.h>
#include <epd_driver.h>
#include <ink_queue.h>
#include <trmnl_log.h>

#include "app_host.h"

static portMUX_TYPE ink_mux = portMUX_INITIALIZER_UNLOCKED;
static ink_queue_t ink_q; // zero: idle
static TaskHandle_t ink_task_handle = nullptr;

#define INK_TASK_STACK 6144
#define INK_TASK_CORE 0 // the app's task runs on core 1: it draws on while the panel scans

// One update of rows y0..y1 with the ink passes, under the panel lock (the IO expander's other user waits)
static bool ink_run(int y0, int y1) {
  SemaphoreHandle_t panel = app_host.panel;
  if (!panel) return true; // no app: nothing to show (the runner waited for the queue before it ended)
  xSemaphoreTake(panel, portMAX_DELAY);
  bool ok = true;
  if (!app_host.screen_held) { // the pairing screen is up; it puts the app's pixels back when done
    bbep.setPasses(APP_INK_PASSES, DISPLAY_PARTIAL_PASSES);
    ok = bbep.partialUpdate(true, y0, y1) == BBEP_SUCCESS;
    bbep.setPasses(DISPLAY_PARTIAL_PASSES, DISPLAY_PARTIAL_PASSES); // as src/display.cpp sets it
  }
  xSemaphoreGive(panel);
  if (!ok) Log_error("ink update of rows %d..%d failed", y0, y1);
  return ok;
}

static void ink_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    int y0, y1;
    for (;;) {
      taskENTER_CRITICAL(&ink_mux);
      bool got = ink_queue_take(&ink_q, &y0, &y1);
      taskEXIT_CRITICAL(&ink_mux);
      if (!got) break;
      bool ok = ink_run(y0, y1);
      taskENTER_CRITICAL(&ink_mux);
      ink_queue_done(&ink_q, ok);
      taskEXIT_CRITICAL(&ink_mux);
    }
  }
}

int display_ink_request(int y0, int y1) {
  if (!ink_task_handle &&
      xTaskCreatePinnedToCore(ink_task, "ink", INK_TASK_STACK, nullptr, 1, &ink_task_handle, INK_TASK_CORE) != pdPASS) {
    ink_task_handle = nullptr;
    Log_error("ink: no task (out of memory), updating in the app's call");
    return ink_run(y0, y1) ? 0 : APP_EIO;
  }
  taskENTER_CRITICAL(&ink_mux);
  int err = ink_queue_take_error(&ink_q);
  bool wake = ink_queue_add(&ink_q, y0, y1);
  taskEXIT_CRITICAL(&ink_mux);
  if (wake) xTaskNotifyGive(ink_task_handle);
  return err;
}

int display_ink_busy(void) {
  taskENTER_CRITICAL(&ink_mux);
  bool idle = ink_queue_idle(&ink_q);
  taskEXIT_CRITICAL(&ink_mux);
  return idle ? 0 : 1;
}

int display_ink_wait_idle(uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (display_ink_busy()) {
    if (millis() - start >= timeout_ms) {
      Log_error("ink: still busy after %lu ms", (unsigned long)timeout_ms);
      return APP_EIO;
    }
    vTaskDelay(1);
  }
  return 0;
}

#endif // BOARD_TRMNL_X
