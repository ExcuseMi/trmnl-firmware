#include "nvs_write.h"

static unsigned failures;
static volatile bool reporting; // a report that writes (the logger storing its line) never reports again

bool nvs_write_check(const char *key, size_t got, size_t want, nvs_write_report_t report) {
  if (got == want) return true;
  failures++;
  if (report && !reporting) {
    reporting = true;
    report(key ? key : "?", got, want);
    reporting = false;
  }
  return false;
}

unsigned nvs_write_failures(void) { return failures; }
