// The X's storage for apps (app_storage_x.h)
#ifdef BOARD_TRMNL_X

#include "app_storage_x.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <app_space.h>
#include <fs_space.h>
#include <esp_system.h>
#include <string.h>

#include <stdio.h>
#include <trmnl_log.h>

#include "evlog.h"
#include "serial_out_hw.h"
#include "webshare.h" // webshare_fs_root

static uint64_t used_now(void *) { return LittleFS.usedBytes(); }
static void wait_now(void *, uint32_t ms) { delay(ms); }

uint64_t app_storage_fs_free(uint64_t *used, bool *unsure) {
  static const fs_space_port_t port = {nullptr, used_now, wait_now};
  bool u = false;
  uint64_t f = fs_space_free(&port, LittleFS.totalBytes(), used, &u);
  if (u) Log_info("Storage: LittleFS reads full every time (%u tries)", (unsigned)FS_SPACE_TRIES);
  if (unsure) *unsure = u;
  return f;
}

static bool ls_cb(const char *path, uint64_t size, bool is_dir, void *) {
  return serial_out_printf("TAPP LS %10llu %s%s\n", (unsigned long long)size, path, is_dir ? "/" : "");
}

bool app_storage_serial_command(const char *line) {
  if (strcmp(line, "TAPP DF") == 0) {
    uint64_t total = LittleFS.totalBytes(), used = 0;
    bool unsure = false;
    uint64_t free_b = app_storage_fs_free(&used, &unsure);
    serial_out_printf("TAPP DF total %llu used %llu free %llu%s\n", (unsigned long long)total,
                      (unsigned long long)used, (unsigned long long)free_b, unsure ? " unsure" : "");
    return true;
  }
  if (strcmp(line, "TAPP LS") == 0) {
    app_space_walk(webshare_fs_root(), "", ls_cb, nullptr); // read only
    serial_out_printf("TAPP LS END\n");
    return true;
  }
  return false;
}

void app_storage_boot_log(void) {
  if (esp_reset_reason() == ESP_RST_DEEPSLEEP) return; // not one line per wake
  char line[200];
  app_space_apps_line(webshare_fs_root(), line, sizeof(line));
  evlog_add("%s", line);
}

void app_storage_line(char *out, size_t len) {
  char u[16], f[16];
  uint64_t used = 0;
  app_storage_fs_free(&used, nullptr);
  app_space_size_text(used, u, sizeof(u));
  app_space_size_text(app_space_free(webshare_fs_root()), f, sizeof(f));
  snprintf(out, len, "Storage: %s used, %s free", u, f);
}

uint64_t app_storage_free_space(void) {
  int removed = 0;
  uint64_t freed = app_space_clean(webshare_fs_root(), &removed);
  char f[16];
  app_space_size_text(freed, f, sizeof(f));
  Log_event("Apps: Free space removed %d files, %s", removed, f);
  return freed;
}

#endif // BOARD_TRMNL_X
