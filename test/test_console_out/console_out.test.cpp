#include <console_out.h>
#include <serial_out.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

// A fake HWCDC TX ring of `cap` bytes; the host takes `drain` bytes per ms (0: nobody reads, FIFO full)
typedef struct {
  char ring[512], got[65536];
  size_t cap, len, got_len, drain;
  uint32_t t;
} fake_t;

static fake_t f;
static console_out_t c;

static int room(void *k) { return (int)(((fake_t *)k)->cap - ((fake_t *)k)->len); }
static size_t write_(void *v, const uint8_t *b, size_t n) {
  fake_t *k = (fake_t *)v;
  if (n > k->cap - k->len) n = k->cap - k->len;
  memcpy(k->ring + k->len, b, n);
  k->len += n;
  return n;
}
static uint32_t now_ms(void *k) { return ((fake_t *)k)->t; }
static void wait_ms(void *v, uint32_t ms) {
  fake_t *k = (fake_t *)v;
  for (uint32_t m = 0; m < ms; m++) {
    k->t++;
    size_t d = k->drain < k->len ? k->drain : k->len;
    memcpy(k->got + k->got_len, k->ring, d);
    k->got_len += d;
    memmove(k->ring, k->ring + d, k->len - d);
    k->len -= d;
  }
}
static const serial_out_port_t port = {room, write_, now_ms, wait_ms, &f};

void setUp() {
  memset(&f, 0, sizeof(f));
  f.cap = 256;
  console_out_init(&c);
}
void tearDown() {}

static uint32_t log_line(int i, bool reader, bool held) {
  char line[64];
  int n = snprintf(line, sizeof line, "log line %04d with some text after it\n", i);
  uint32_t t0 = f.t;
  TEST_ASSERT_EQUAL(n, console_out_write(&c, &port, reader, held, (const uint8_t *)line, (size_t)n));
  return f.t - t0;
}

// A host that reads: every byte arrives, in order, nothing dropped
void test_reading_host_gets_everything() {
  f.drain = 8;
  for (int i = 0; i < 200; i++) log_line(i, true, false);
  while (!console_out_pump(&c, &port, true, false)) wait_ms(&f, 1);
  wait_ms(&f, 100);
  TEST_ASSERT_EQUAL(0, console_out_dropped_take(&c));
  f.got[f.got_len] = 0;
  TEST_ASSERT_EQUAL(200 * 38, f.got_len);
  TEST_ASSERT_EQUAL_STRING_LEN("log line 0000", f.got, 13);
  TEST_ASSERT_NOT_NULL(strstr(f.got, "log line 0199 with some text after it\n"));
}

// owner 2026-11-02: a PC on the bus that takes nothing (FIFO full). Writers wait at most the limit once, then not at
// all; the newest text is kept and the loss counted; once the PC reads again the newest lines arrive
void test_stalled_tx_never_blocks_keeps_newest_counts_drops() {
  f.drain = 0;
  uint32_t longest = 0, total = 0;
  for (int i = 0; i < 1000; i++) {
    uint32_t w = log_line(i, true, false);
    if (w > longest) longest = w;
    total += w;
  }
  TEST_ASSERT_LESS_OR_EQUAL(CONSOLE_OUT_WAIT_MS, longest);
  TEST_ASSERT_LESS_OR_EQUAL(CONSOLE_OUT_WAIT_MS, total); // one wait for the whole stall
  TEST_ASSERT_EQUAL(0, console_out_dropped_take(&c)); // not reported while text still waits
  f.drain = 16; // the PC reads again
  for (int i = 0; i < 1000 && !console_out_pump(&c, &port, true, false); i++) wait_ms(&f, 1);
  wait_ms(&f, 100);
  uint32_t dropped = console_out_dropped_take(&c);
  TEST_ASSERT_GREATER_THAN(0, dropped);
  TEST_ASSERT_EQUAL(1000u * 38u, f.got_len + dropped); // every byte arrived or was counted
  f.got[f.got_len] = 0;
  const char *last = "log line 0999 with some text after it\n";
  TEST_ASSERT_EQUAL_STRING(last, f.got + f.got_len - strlen(last)); // the newest is the last out
  TEST_ASSERT_EQUAL(0, console_out_dropped_take(&c)); // reported once
}

// Host mode (the tablet) or no cable: no reader, the writer never waits and the newest text waits for one
void test_no_reader_never_waits() {
  f.drain = 0;
  for (int i = 0; i < 500; i++) TEST_ASSERT_EQUAL(0, log_line(i, false, false));
  TEST_ASSERT_EQUAL(0, f.len); // nothing went into the ring
  TEST_ASSERT_EQUAL(CONSOLE_OUT_BYTES, c.len);
  TEST_ASSERT_EQUAL(500u * 38u - CONSOLE_OUT_BYTES, c.dropped);
}

// fd9d941: an upload holds the port. Log text that waited from before is not moved meanwhile; the ACK has the
// ring to itself and goes out whole
void test_upload_ack_still_goes_out() {
  f.drain = 0;
  for (int i = 0; i < 100; i++) log_line(i, true, false); // stalled with text waiting
  f.drain = 16;
  wait_ms(&f, 50); // the PC reads again: the ring empties
  size_t before = f.got_len;
  const char *ack = "TAPP ACK 28672\n";
  TEST_ASSERT_TRUE(console_out_pump(&c, &port, true, true) == false); // held: nothing moved
  TEST_ASSERT_EQUAL(0, f.len);
  TEST_ASSERT_EQUAL(strlen(ack), serial_out_write(&port, (const uint8_t *)ack, strlen(ack), SERIAL_OUT_STALL_MS));
  TEST_ASSERT_TRUE(console_out_pump(&c, &port, true, true) == false);
  wait_ms(&f, 10);
  TEST_ASSERT_EQUAL(strlen(ack), f.got_len - before);
  TEST_ASSERT_EQUAL_STRING_LEN(ack, f.got + before, strlen(ack));
}

// Moves stop at a line end when the rest does not fit: a reply written in between never lands inside a log line
void test_moves_whole_lines() {
  f.drain = 0;
  f.cap = 50; // room for one 38-byte line, not two
  log_line(1, true, false);
  log_line(2, true, false);
  TEST_ASSERT_EQUAL(38, f.len);
  TEST_ASSERT_EQUAL('\n', f.ring[37]);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_reading_host_gets_everything);
  RUN_TEST(test_stalled_tx_never_blocks_keeps_newest_counts_drops);
  RUN_TEST(test_no_reader_never_waits);
  RUN_TEST(test_upload_ack_still_goes_out);
  RUN_TEST(test_moves_whole_lines);
  return UNITY_END();
}
