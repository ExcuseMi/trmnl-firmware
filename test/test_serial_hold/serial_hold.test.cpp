// Log text never lands between upload replies (serial_hold.h; owner's X 2026-11-02, ACK mixed with log text)
#include <serial_hold.h>
#include <serial_out.h>
#include <string.h>
#include <unity.h>

static char out[512];
static size_t out_len;
static size_t sink(void *, const uint8_t *b, size_t n) {
  memcpy(out + out_len, b, n);
  out_len += n;
  return n;
}
static int room(void *) { return 256; }
static uint32_t t;
static uint32_t now_ms(void *) { return t; }
static void wait_ms(void *, uint32_t ms) { t += ms; }
static const serial_out_port_t port = {room, sink, now_ms, wait_ms, nullptr};

static void log_line(const char *s) { serial_hold_log((const uint8_t *)s, strlen(s), sink, nullptr); }
static void reply(const char *s) { serial_out_write(&port, (const uint8_t *)s, strlen(s), 500); }

void setUp() {
  out_len = 0;
  memset(out, 0, sizeof(out));
  serial_hold_dropped_take();
}
void tearDown() {}

void test_log_mid_upload_stays_out_of_the_replies() {
  log_line("before\n");
  serial_hold_set(true);
  reply("TAPP READY\n");
  log_line("src/serial_watch.cpp [9]: Serial: output resumed\n");
  reply("TAPP ACK 64\n");
  log_line("x\n");
  reply("TAPP OK a.elf\n");
  serial_hold_set(false);
  log_line("after\n");
  TEST_ASSERT_EQUAL_STRING("before\nTAPP READY\nTAPP ACK 64\nTAPP OK a.elf\nafter\n", out);
  TEST_ASSERT_EQUAL_UINT(2, serial_hold_dropped_take());
  TEST_ASSERT_EQUAL_UINT(0, serial_hold_dropped_take());
}

void test_hold_nests_and_never_goes_negative() {
  serial_hold_set(true);
  serial_hold_set(true);
  serial_hold_set(false);
  TEST_ASSERT_TRUE(serial_hold_on());
  serial_hold_set(false);
  serial_hold_set(false); // one end too many: still off, not stuck held
  TEST_ASSERT_FALSE(serial_hold_on());
  serial_hold_set(true);
  TEST_ASSERT_TRUE(serial_hold_on());
  serial_hold_set(false);
  TEST_ASSERT_FALSE(serial_hold_on());
}

void test_rx_waits_while_the_ack_is_still_queued() {
  // 6 s without a byte, but the ACK left the ring only 1 s ago: the host had no chance to answer
  TEST_ASSERT_FALSE(serial_hold_rx_gave_up(16000, 10000, 15000, 5000, 30000));
  // 6 s without a byte and the ring long empty: given up
  TEST_ASSERT_TRUE(serial_hold_rx_gave_up(16000, 10000, 10500, 5000, 30000));
  // a ring stuck forever still ends the upload
  TEST_ASSERT_TRUE(serial_hold_rx_gave_up(40000, 10000, 40000, 5000, 30000));
  // millis() wrapping
  TEST_ASSERT_FALSE(serial_hold_rx_gave_up(1000, 0xFFFFF000u, 500, 5000, 30000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_log_mid_upload_stays_out_of_the_replies);
  RUN_TEST(test_hold_nests_and_never_goes_negative);
  RUN_TEST(test_rx_waits_while_the_ack_is_still_queued);
  return UNITY_END();
}
