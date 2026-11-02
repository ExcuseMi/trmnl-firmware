#include "nvs_write.h"

static unsigned failures;

bool nvs_write_check(const char *key, size_t got, size_t want, nvs_write_report_t report) {
  if (got == want) return true;
  failures++;
  if (report) report(key ? key : "?", got, want);
  return false;
}

unsigned nvs_write_failures(void) { return failures; }
