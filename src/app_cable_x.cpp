// Updating apps over the USB cable without the Receive over USB screen (app_cable_x.h)
#ifdef BOARD_TRMNL_X

#include "app_cable_x.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <app_cable.h>
#include <app_elf_check.h>
#include <app_sleep_gate.h>
#include <apps.h>
#include <esp_heap_caps.h>
#include <filesystem.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>
#include <trmnl_log.h>

#include "app_receive.h"
#include "evlog.h"
#include "usb_port_role.h"

#define TASK_POLL_MS 20
#define RECEIVE_SCREEN "Receive over USB" // the menu screen reads the port itself

static SemaphoreHandle_t lock; // one upload at a time
static bool pending;           // a received, checked file waits for the running app to exit
static app_upload_header_t pending_h;

static void fail(app_upload_result_t r, const char *reason) {
  Log_info("Apps: cable upload refused: %s%s%s", app_upload_result_str(r), reason[0] ? ": " : "", reason);
  evlog_add("Cable: upload refused: %s%s%s", app_upload_result_str(r), reason[0] ? " " : "", reason);
  apps_reply("ERR %s%s%s", app_upload_result_str(r), reason[0] ? ": " : "", reason);
}

// Checks the file in APP_UPLOAD_TMP as the install will, without touching the installed app
static app_upload_result_t check_tmp(char *reason, size_t reason_len) {
  uint8_t *buf = nullptr;
  size_t len = filesystem_read_and_allocate(APP_UPLOAD_TMP, &buf);
  if (!len || !buf) {
    free(buf);
    return APP_UPLOAD_NO_SPACE;
  }
  app_info_t info;
  size_t free_mem = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  app_elf_result_t c = app_elf_check(buf, len, free_mem, &info);
  free(buf);
  if (c == APP_ELF_OK) return APP_UPLOAD_OK;
  snprintf(reason, reason_len, "%s", app_elf_result_str(c));
  return APP_UPLOAD_REFUSED;
}

static void upload(const char *header) {
  if (!lock) lock = xSemaphoreCreateMutex();
  if (!lock || xSemaphoreTake(lock, 0) != pdTRUE) {
    apps_reply("ERR busy: another upload is under way");
    return;
  }
  app_cable_busy_set(true);
  char reason[112] = "";
  app_upload_header_t h;
  uint64_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  app_upload_result_t r = app_upload_parse(header, used < total ? total - used : 0, &h);
  if (r == APP_UPLOAD_BAD_HEADER) app_upload_got_text(header, 32, reason, sizeof(reason));
  usb_role_t role = usb_port_role();
  if (r == APP_UPLOAD_OK && pending) {
    apps_reply("ERR busy: an update waits for its app to exit");
    r = APP_UPLOAD_WRITE_FAILED;
    goto done;
  }
  if (r == APP_UPLOAD_OK && role != USB_ROLE_SERIAL) {
    Log_info("Apps: cable upload refused, %s", app_cable_refusal(role));
    evlog_add("Cable: upload refused, %s", app_cable_refusal(role));
    apps_reply("ERR %s", app_cable_refusal(role));
    goto done;
  }
  if (r == APP_UPLOAD_OK) {
    if (!LittleFS.exists(APPS_DIR)) LittleFS.mkdir(APPS_DIR);
    evlog_add("Cable: receiving %s, %lu bytes", h.path, (unsigned long)h.size);
    r = apps_receive_tmp(h);
  }
  if (r == APP_UPLOAD_OK) r = check_tmp(reason, sizeof(reason));
  if (r != APP_UPLOAD_OK) {
    LittleFS.remove(APP_UPLOAD_TMP);
    fail(r, reason);
    goto done;
  }
  {
    // decided now, not at the header: an app may have started or ended while the bytes came
    const char *running = app_sleep_gate_app();
    app_cable_action_t a = app_cable_plan(usb_port_role(), running, h.path);
    if (a == APP_CABLE_REFUSE_PORT) {
      LittleFS.remove(APP_UPLOAD_TMP);
      fail(APP_UPLOAD_WRITE_FAILED, app_cable_refusal(usb_port_role()));
    } else if (a == APP_CABLE_REPLACE_AND_RESTART) {
      pending_h = h;
      pending = true;
      evlog_add("Cable: %s runs - asking it to exit for the update", running);
      Log_event("Apps: cable update of the running %s - asking it to exit", running);
    } else {
      r = apps_install_tmp(h, reason, sizeof(reason));
      if (r != APP_UPLOAD_OK) {
        fail(r, reason);
      } else {
        evlog_add("Cable: installed %s%s%s", h.path, running ? ", kept running: " : "", running ? running : "");
        Log_event("Apps: cable installed %s%s%s", h.path, running ? ", still running: " : "", running ? running : "");
        apps_reply("OK %s", h.path + sizeof(APPS_DIR));
      }
    }
  }
done:
  app_cable_busy_set(false);
  xSemaphoreGive(lock);
}

void app_cable_x_header(const char *header) { upload(header); }

bool app_cable_x_exit_wanted(void) {
  static bool asked;
  if (!pending) {
    asked = false;
    return false;
  }
  if (asked) return false;
  asked = true;
  return true;
}

bool app_cable_x_finish(char *path, unsigned len) {
  if (!pending) return false;
  pending = false;
  char reason[112] = "";
  app_upload_result_t r = apps_install_tmp(pending_h, reason, sizeof(reason));
  if (r != APP_UPLOAD_OK) {
    fail(r, reason); // the old file stays
    return false;
  }
  evlog_add("Cable: replaced %s", pending_h.path);
  Log_event("Apps: cable replaced %s, starting it again", pending_h.path);
  apps_reply("OK %s", pending_h.path + sizeof(APPS_DIR));
  snprintf(path, len, "%s", pending_h.path);
  return true;
}

static void cable_task(void *) {
  Serial.begin(115200);
  app_cable_line_t line;
  app_cable_line_init(&line);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(TASK_POLL_MS));
    const char *screen = app_sleep_gate_screen_name();
    if (app_sleep_gate_app() || (screen && strcmp(screen, RECEIVE_SCREEN) == 0) || usb_port_role() != USB_ROLE_SERIAL) {
      app_cable_line_init(&line);
      continue;
    }
    while (Serial.available() > 0) {
      const char *header;
      if (app_cable_line_feed(&line, (unsigned char)Serial.read(), &header)) upload(header);
    }
  }
}

void app_cable_x_start(void) {
  static TaskHandle_t task;
  if (task) return;
  if (xTaskCreatePinnedToCore(cable_task, "cable", 6144, nullptr, 1, &task, 0) != pdPASS) {
    Log_error("Apps: cable update task not started (out of memory)");
    task = nullptr;
  }
}

#endif // BOARD_TRMNL_X
