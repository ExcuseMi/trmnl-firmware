// The X's USB serial (Arduino HWCDC) is begun once, on the main task, before any task that uses it starts.
// HWCDC::begin allocates its interrupt only when none exists yet; two begin() calls at the same time on two cores both
// see none and install two ISRs, one per core. Both then read the RX FIFO and write the TX FIFO: every other received
// byte was lost and the next doubled ("TAPP DF" arrived as "AAPPDD"), console lines lost characters, no TAPP command
// or upload header was understood (owner's X 2026-11-02, fw-all9/10: the cable task's Serial.begin on core 0 raced
// bootEarly's on core 1). Rule checked here: the cable task never calls Serial.begin, and it is started in bootEarly
// after Serial.begin, not before bl_init.
#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

static bool read_file(const char *path, char *out, size_t cap) {
  FILE *f = fopen(path, "r");
  if (!f) return false;
  size_t n = fread(out, 1, cap - 1, f);
  out[n] = 0;
  fclose(f);
  return true;
}

static char buf[65536];

void test_cable_task_never_begins_serial() {
  TEST_ASSERT_TRUE_MESSAGE(read_file("src/app_cable_x.cpp", buf, sizeof buf), "src/app_cable_x.cpp not found");
  TEST_ASSERT_NULL_MESSAGE(strstr(buf, "Serial.begin"), "Serial.begin in the cable task (second HWCDC ISR)");
}

void test_cable_task_starts_after_serial_begin() {
  TEST_ASSERT_TRUE(read_file("src/main.cpp", buf, sizeof buf));
  TEST_ASSERT_NULL_MESSAGE(strstr(buf, "app_cable_x_start()"), "cable task started in setup, before bootEarly");
  TEST_ASSERT_TRUE(read_file("src/wake_init.cpp", buf, sizeof buf));
  const char *ring = strstr(buf, "Serial.setTxBufferSize(SERIAL_TX_RING_BYTES)");
  TEST_ASSERT_NOT_NULL_MESSAGE(ring, "the X's Serial.begin in bootEarly not found");
  const char *begin = strstr(ring, "Serial.begin(");
  const char *start = strstr(buf, "app_cable_x_start()");
  TEST_ASSERT_NOT_NULL(begin);
  TEST_ASSERT_NOT_NULL_MESSAGE(start, "app_cable_x_start() not in bootEarly");
  TEST_ASSERT_TRUE_MESSAGE(start > begin, "app_cable_x_start() before Serial.begin");
}

// HWCDC's flush() discards the whole TX ring when the host took nothing for 100 ms (serial_out.h); over usbip that
// happens, and the upload's "TAPP ACK" went with it: uploads stopped at "no reply (wanted 'ACK 9920')" (owner's X
// 2026-11-02, fw-all11 test). Replies go through serial_out_printf.
void test_upload_replies_never_flush() {
  TEST_ASSERT_TRUE(read_file("src/app_receive.cpp", buf, sizeof buf));
  TEST_ASSERT_NULL_MESSAGE(strstr(buf, "Serial.flush()"), "Serial.flush() in the upload replies");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cable_task_never_begins_serial);
  RUN_TEST(test_cable_task_starts_after_serial_begin);
  RUN_TEST(test_upload_replies_never_flush);
  return UNITY_END();
}
