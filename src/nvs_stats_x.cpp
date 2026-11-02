#ifdef BOARD_TRMNL_X
#include "nvs_stats_x.h"

#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <nvs_write.h>
#include <serial_out.h>
#include <string.h>

#include "evlog.h"
#include "serial_out_hw.h"

#define NS_MAX 48

static const char *type_str(nvs_type_t t) {
  switch (t) {
  case NVS_TYPE_U8: return "u8";
  case NVS_TYPE_I8: return "i8";
  case NVS_TYPE_U16: return "u16";
  case NVS_TYPE_I16: return "i16";
  case NVS_TYPE_U32: return "u32";
  case NVS_TYPE_I32: return "i32";
  case NVS_TYPE_U64: return "u64";
  case NVS_TYPE_I64: return "i64";
  case NVS_TYPE_STR: return "str";
  case NVS_TYPE_BLOB: return "blob";
  default: return "?";
  }
}

// The bytes a string or blob holds (read through a read-only handle), 0 for numbers
static size_t value_size(const nvs_entry_info_t &e) {
  if (e.type != NVS_TYPE_STR && e.type != NVS_TYPE_BLOB) return 0;
  nvs_handle_t h;
  if (nvs_open(e.namespace_name, NVS_READONLY, &h) != ESP_OK) return 0;
  size_t n = 0;
  if (e.type == NVS_TYPE_STR) nvs_get_str(h, e.key, nullptr, &n);
  else nvs_get_blob(h, e.key, nullptr, &n);
  nvs_close(h);
  return n;
}

bool nvs_stats_serial_command(const char *line) {
  if (strcmp(line, "TAPP NVS") != 0) return false;
  nvs_stats_t st;
  if (nvs_get_stats(NULL, &st) == ESP_OK)
    serial_out_printf("TAPP NVS used %u free %u total %u namespaces %u failed_writes %u\n", (unsigned)st.used_entries,
                      (unsigned)st.free_entries, (unsigned)st.total_entries, (unsigned)st.namespace_count,
                      nvs_write_failures());
  static char names[NS_MAX][16];
  static unsigned counts[NS_MAX];
  int n = 0;
  nvs_iterator_t it = nullptr;
  esp_err_t rc = nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_ANY, &it);
  while (rc == ESP_OK) {
    nvs_entry_info_t e;
    nvs_entry_info(it, &e);
    int i = 0;
    while (i < n && strcmp(names[i], e.namespace_name) != 0) i++;
    if (i == n && n < NS_MAX) {
      strncpy(names[n], e.namespace_name, sizeof(names[0]) - 1);
      names[n][sizeof(names[0]) - 1] = '\0';
      counts[n++] = 0;
    }
    if (i < n) counts[i]++;
    serial_out_printf("TAPP NVS key %s %s %s %u\n", e.namespace_name, e.key, type_str(e.type), (unsigned)value_size(e));
    rc = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  for (int i = 0; i < n; i++) serial_out_printf("TAPP NVS ns %s %u\n", names[i], counts[i]);
  serial_out_printf("TAPP NVS END\n");
  return true;
}

void nvs_stats_boot_log(void) {
  if (esp_reset_reason() == ESP_RST_DEEPSLEEP) return; // not one line per wake
  nvs_stats_t st;
  if (nvs_get_stats(NULL, &st) != ESP_OK) return;
  evlog_add("NVS: %u used, %u free of %u entries, %u namespaces", (unsigned)st.used_entries, (unsigned)st.free_entries,
            (unsigned)st.total_entries, (unsigned)st.namespace_count);
}
#endif // BOARD_TRMNL_X
