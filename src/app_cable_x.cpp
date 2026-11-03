// Updating apps over the USB cable without the Receive over USB screen (app_cable_x.h)
#ifdef BOARD_TRMNL_X

#include "app_cable_x.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <app_cable.h>
#include <app_elf_check.h>
#include <app_launch.h>
#include <app_remove.h>
#include <app_sleep_gate.h>
#include <serial_hold.h>
#include <apps.h>
#include <esp_heap_caps.h>
#include <filesystem.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>
#include <trmnl_log.h>

#include "app_host.h"
#include "app_receive.h"
#include "app_storage_x.h"
#include "menu_apps.h"
#include "webshare.h"
#include <apps_config.h>
#include "nvs_stats_x.h"
#include "evlog.h"
#include "usb_port_role.h"
#include "task_watch_x.h"

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
  serial_hold_set(true); // no log text between the replies (serial_hold.h)
  char reason[112] = "";
  app_upload_header_t h;
  app_upload_result_t r = pending ? app_upload_parse(header, UINT64_MAX, &h) // the waiting file is not stale
                                  : apps_parse_header(header, &h, reason, sizeof(reason));
  usb_role_t role = usb_port_role();
  app_cable_header_plan_t plan = app_cable_header_plan(pending, r);
  if (plan == APP_CABLE_HDR_KEEP_WAITING) { // good or bad, this header never touches the waiting file
    if (r == APP_UPLOAD_OK) apps_reply("ERR busy: an update waits for its app to exit");
    else fail(r, reason);
    goto done;
  }
  if (plan == APP_CABLE_HDR_RECEIVE && role != USB_ROLE_SERIAL) {
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
  serial_hold_set(false);
  if (unsigned d = serial_hold_dropped_take()) Log_info("Serial: %u log writes held back during the upload", d);
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

static char run_next[32], run_arg[APP_LAUNCH_ARG_MAX + 1]; // "TAPP RUN <app> [arg]": started once the running app has exited
static volatile bool run_pending;

static bool run_command(const char *line) {
  char name[sizeof(run_next)];
  if (strncmp(line, "TAPP RUN", 8) != 0) return false;
  char elf[sizeof(name) + 16];
  if (!app_cable_run_name(line, name, sizeof(name))) apps_reply("ERR run: not an app name");
  else if (snprintf(elf, sizeof(elf), APPS_DIR "/%s.elf", name), !LittleFS.exists(elf)) apps_reply("ERR run: %s is not installed", name);
  else if (!app_host.name[0]) apps_reply("ERR run: no app runs (start one on the X first)");
  else {
    snprintf(run_next, sizeof(run_next), "%s", name);
    const char *arg = app_cable_run_arg(line);
    snprintf(run_arg, sizeof(run_arg), "%.*s", (int)strcspn(arg, "\r\n"), arg);
    run_pending = true;
    evlog_add("Cable: run %s asked, %s is asked to exit", name, app_host.name);
    apps_reply("RUN OK %s", name);
  }
  return true;
}

bool app_cable_x_run_take(char *name, size_t n, char *arg, size_t arg_n) {
  if (!run_pending) return false;
  run_pending = false;
  snprintf(name, n, "%s", run_next);
  snprintf(arg, arg_n, "%s", run_arg);
  return true;
}

static const char *rm_reason(app_remove_t r) {
  switch (r) {
  case APP_REMOVE_BAD_NAME: return "not an app name";
  case APP_REMOVE_ACTIVE: return "it is running or due to resume (end it first)";
  case APP_REMOVE_NOT_INSTALLED: return "not installed";
  case APP_REMOVE_FAILED: return "a file could not be removed";
  default: return "refused";
  }
}

// "TAPP RM <app> [data]" (Apps > Remove an app over the cable): the .elf and .bak; "data" also its /appdata files
static bool rm_command(const char *line) {
  if (strncmp(line, "TAPP RM", 7) != 0) return false;
  char name[32], active[32];
  bool data = false;
  if (!app_cable_rm_parse(line, name, sizeof(name), &data)) {
    apps_reply("ERR rm: not an app name (TAPP RM <app> [data])");
    return true;
  }
  menu_apps_active(active, sizeof(active));
  app_remove_t r = app_remove(webshare_fs_root(), APPS_DIR_REL, APPS_DATA_DIR_REL, name, active, data, true);
  evlog_add("Cable: remove %s%s: %s", name, data ? " with its data" : "", r == APP_REMOVE_OK ? "done" : rm_reason(r));
  if (r == APP_REMOVE_OK) apps_reply("RM OK %s%s", name, data ? " data" : "");
  else apps_reply("ERR rm %s: %s", name, rm_reason(r));
  return true;
}

bool app_cable_x_command(const char *line) {
  return evlog_serial_command(line) || app_storage_serial_command(line) || nvs_stats_serial_command(line) ||
         run_command(line) || rm_command(line) || task_watch_x_command(line);
}

// Serial was begun by bootEarly before this task starts (test_serial_begin): a begin() here raced it on the other core
// and installed a second HWCDC interrupt
static void cable_task(void *) {
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
      app_cable_rx_note(millis()); // a host on the cable: no deep sleep under it (app_cable_sleep_hold)
      if (app_cable_line_feed(&line, (unsigned char)Serial.read(), &header)) upload(header);
      else if (const char *cmd = app_cable_line_command(&line)) app_cable_x_command(cmd);
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
