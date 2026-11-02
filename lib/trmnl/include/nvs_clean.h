#pragma once
// The one NVS cleanup the owner agreed to on 2026-11-02 (the X's NVS full, every new write failing): exactly these
// keys and this namespace, nothing else. Wi-Fi ("wificaptive", "nvs.net80211"), Bluetooth and calibration are never
// on it. Run by "TAPP NVS CLEAN" (src/nvs_stats_x.cpp); each removal is logged.
#include <stddef.h>

typedef struct {
  const char *ns;  // the namespace
  const char *key; // the key, or nullptr: the whole namespace
} nvs_clean_item_t;

// The list; *n gets its length
const nvs_clean_item_t *nvs_clean_list(size_t *n);

// Never cleaned, whatever the list says (a second check before each removal)
bool nvs_clean_protected(const char *ns);
