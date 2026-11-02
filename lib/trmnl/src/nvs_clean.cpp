#include "nvs_clean.h"

#include <string.h>

static const nvs_clean_item_t items[] = {
    {"data", "log_0"}, {"data", "log_1"}, {"data", "log_2"}, {"data", "log_3"},
    {"data", "log_4"}, {"data", "log_5"}, {"data", "log_6"},
    {"app84cc956a", nullptr}, // the "writer" app's settings; the app is not installed
};

const nvs_clean_item_t *nvs_clean_list(size_t *n) {
  if (n) *n = sizeof(items) / sizeof(items[0]);
  return items;
}

bool nvs_clean_protected(const char *ns) {
  static const char *keep[] = {"wificaptive", "nvs.net80211", "phy", "nimble_bond", "ble_kbd", "qa", "app8c09e2df"};
  if (!ns) return true;
  for (const char *k : keep)
    if (!strcmp(ns, k)) return true;
  return false;
}
