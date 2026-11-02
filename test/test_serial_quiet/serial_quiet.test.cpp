// The touch bar's per-RDY path must not write to the USB serial port: in streaming mode (menus, apps) it runs dozens
// of times a second and fills HWCDC's TX ring, so app uploads over the cable got no READY and lost bytes (device,
// 2026-11-02: "Queue value updates..." flood, TAPP ERR short). Debug lines there go under IQS323_DEBUG_STREAM.
#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

// Returns the body of `void IQS323::<name>(void)` with lines under #ifdef IQS323_DEBUG_STREAM removed
static bool body_without_debug(const char *name, char *out, size_t cap) {
  FILE *f = fopen("lib/IQS323/IQS323.cpp", "r");
  if (!f) return false;
  char sig[96], line[512];
  snprintf(sig, sizeof sig, "void IQS323::%s(void)", name);
  bool in = false, debug = false;
  int depth = 0;
  size_t n = 0;
  out[0] = 0;
  while (fgets(line, sizeof line, f)) {
    if (!in) {
      if (strstr(line, sig)) in = true;
      continue;
    }
    if (strstr(line, "#ifdef IQS323_DEBUG_STREAM")) debug = true;
    else if (debug && strstr(line, "#endif")) debug = false;
    else if (!debug && n + strlen(line) < cap) n += (size_t)snprintf(out + n, cap - n, "%s", line);
    for (char *c = line; *c; c++) depth += *c == '{' ? 1 : *c == '}' ? -1 : 0;
    if (depth == 0 && strchr(line, '}')) break;
  }
  fclose(f);
  return in;
}

void test_queue_value_updates_is_quiet() {
  static char body[16384];
  TEST_ASSERT_TRUE_MESSAGE(body_without_debug("queueValueUpdates", body, sizeof body), "function not found");
  TEST_ASSERT_NULL_MESSAGE(strstr(body, "Serial."), "Serial output in the per-RDY path");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_queue_value_updates_is_quiet);
  return UNITY_END();
}
