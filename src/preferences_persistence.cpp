
#include <Arduino.h>
#include <ArduinoLog.h>
#include <Preferences.h>
#include <nvs.h>
#include <preferences_persistence.h>
#include <nvs_write.h>
#include <string.h>
#include <trmnl_log.h>
#ifdef BOARD_TRMNL_X
#include "evlog.h"
#endif

// Every failed write in the log and the event log (nvs_write.h)
static void report(const char *key, size_t got, size_t want) {
  // not Log_error: it stores the line in this same NVS (a restart loop on fw-nvs22, 2026-11-02)
  Serial.printf("NVS: write of %s failed (%u of %u bytes)\n", key, (unsigned)got, (unsigned)want);
#ifdef BOARD_TRMNL_X
  evlog_add("NVS: write of %s failed (%u of %u bytes)", key, (unsigned)got, (unsigned)want);
#endif
}

PreferencesPersistence::PreferencesPersistence(Preferences &preferences) : _preferences(preferences) {}

bool PreferencesPersistence::recordExists(const char *key) { return _preferences.isKey(key); }

String PreferencesPersistence::readString(const char *key, const String defaultValue) {
  return _preferences.getString(key, defaultValue);
}

uint32_t PreferencesPersistence::readUint(const char *key, const uint32_t defaultValue) {
  return _preferences.getUInt(key, defaultValue);
}

size_t PreferencesPersistence::writeUint(const char *key, const uint32_t value) {
  size_t n = _preferences.putUInt(key, value);
  nvs_write_check(key, n, sizeof(uint32_t), report);
  return n;
}

size_t PreferencesPersistence::writeString(const char *key, const char *value) {
  size_t n = _preferences.putString(key, value);
  if (value && value[0]) nvs_write_check(key, n, strlen(value), report); // "" returns 0 also when it worked
  return n;
}

uint8_t PreferencesPersistence::readUChar(const char *key, const uint8_t defaultValue) {
  return _preferences.getUChar(key, defaultValue);
}

size_t PreferencesPersistence::writeUChar(const char *key, const uint8_t value) {
  size_t n = _preferences.putUChar(key, value);
  nvs_write_check(key, n, 1, report);
  return n;
}

bool PreferencesPersistence::readBool(const char *key, const bool defaultValue) {
  return _preferences.getBool(key, defaultValue);
}

size_t PreferencesPersistence::writeBool(const char *key, const bool value) {
  size_t n = _preferences.putBool(key, value);
  nvs_write_check(key, n, 1, report);
  return n;
}

bool PreferencesPersistence::clear() { return _preferences.clear(); }

bool PreferencesPersistence::remove(const char *key) { return _preferences.remove(key); }

void log_nvs_usage() {
  nvs_stats_t nv;
  esp_err_t ret = nvs_get_stats(NULL, &nv);
  if (ret == ESP_OK) {
    float percent = (float)nv.used_entries / (float)nv.total_entries * 100.0f;
    char percent_str[16];
    dtostrf(percent, 0, 2, percent_str); // 2 decimal places
    Log_info("NVS Usage: %d/%d entries (%s%%)", nv.used_entries, nv.total_entries, percent_str);
  } else {
    Log_error("Failed to get NVS stats: %s", esp_err_to_name(ret));
  }
}
