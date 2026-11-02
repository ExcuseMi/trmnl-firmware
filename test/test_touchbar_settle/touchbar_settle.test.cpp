#include <touchbar_settle.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

static const uint8_t LEFT = 1, MID = 2, RIGHT = 4;

// The boot after a restart (owner 2026-11-02): the bar reports touches on its own right after the ATI. None reaches
// the firmware while it settles, the task gets one second ATI, and the bar acts again after it.
void test_boot_ghosts_never_pass_while_settling() {
  touchbar_settle_t s;
  touchbar_settle_begin(&s, 1000);
  const uint8_t ghosts[] = {MID, 0, MID | RIGHT, LEFT, 0, MID, RIGHT, 0, MID, LEFT};
  uint32_t t = 1000;
  for (uint8_t g : ghosts) {
    t += 140;
    TEST_ASSERT_EQUAL_HEX8(0, touchbar_settle_filter(&s, t, g, false));
  }
  TEST_ASSERT_TRUE(t - 1000 >= TOUCHBAR_SETTLE_REATI_MS - 140);
  touchbar_settle_tick(&s, 1000 + TOUCHBAR_SETTLE_REATI_MS);
  TEST_ASSERT_TRUE(touchbar_settle_take_reati(&s));
  TEST_ASSERT_FALSE(touchbar_settle_take_reati(&s)); // once
  // the chip's ATI: reads show it active, no touch passes
  TEST_ASSERT_EQUAL_HEX8(0, touchbar_settle_filter(&s, 1000 + 1600, MID, true));
  // done: the bar opens; the middle still down then waits for its release
  TEST_ASSERT_EQUAL_HEX8(0, touchbar_settle_filter(&s, 1000 + 1800, MID, false));
  TEST_ASSERT_EQUAL_HEX8(0, touchbar_settle_filter(&s, 1000 + 1850, MID, false));
  TEST_ASSERT_EQUAL_HEX8(0, touchbar_settle_filter(&s, 1000 + 1900, 0, false));
  TEST_ASSERT_EQUAL_HEX8(MID, touchbar_settle_filter(&s, 1000 + 2000, MID, false));
}

// Hard limit: with no read at all (event mode, nobody touches) or an ATI that never reports done, the bar opens
// TOUCHBAR_SETTLE_MAX_MS after the init.
void test_bar_never_shut_longer_than_the_limit() {
  touchbar_settle_t s;
  touchbar_settle_begin(&s, 0);
  touchbar_settle_tick(&s, TOUCHBAR_SETTLE_REATI_MS);
  TEST_ASSERT_TRUE(touchbar_settle_take_reati(&s));
  TEST_ASSERT_EQUAL_HEX8(0, touchbar_settle_filter(&s, 2500, LEFT, true));
  TEST_ASSERT_EQUAL_HEX8(0, touchbar_settle_filter(&s, 2900, 0, true));
  touchbar_settle_tick(&s, TOUCHBAR_SETTLE_MAX_MS);
  TEST_ASSERT_FALSE(s.active);
  TEST_ASSERT_EQUAL_HEX8(RIGHT, touchbar_settle_filter(&s, TOUCHBAR_SETTLE_MAX_MS + 10, RIGHT, false));
}

// A touch held across the opening (also a real finger): it never acts, the next press does
void test_touch_held_at_opening_needs_a_release() {
  touchbar_settle_t s;
  touchbar_settle_begin(&s, 0);
  TEST_ASSERT_EQUAL_HEX8(0, touchbar_settle_filter(&s, 2900, LEFT | MID, false));
  TEST_ASSERT_EQUAL_HEX8(0, touchbar_settle_filter(&s, 3100, LEFT | MID, false));
  TEST_ASSERT_EQUAL_HEX8(0, touchbar_settle_filter(&s, 3200, MID, false)); // left released, middle still down
  TEST_ASSERT_EQUAL_HEX8(LEFT, touchbar_settle_filter(&s, 3300, LEFT | MID, false));
  TEST_ASSERT_EQUAL_HEX8(LEFT, touchbar_settle_filter(&s, 3400, LEFT, false));
  TEST_ASSERT_EQUAL_HEX8(MID, touchbar_settle_filter(&s, 3500, MID, false));
}

// Without a full init (a wake from deep sleep: the chip kept its baseline) every touch passes at once
void test_no_init_no_settling() {
  touchbar_settle_t s = {};
  touchbar_settle_tick(&s, 10);
  TEST_ASSERT_FALSE(touchbar_settle_take_reati(&s));
  TEST_ASSERT_EQUAL_HEX8(MID, touchbar_settle_filter(&s, 20, MID, false));
}

// millis() wraps: the limits still hold
void test_wrap() {
  touchbar_settle_t s;
  touchbar_settle_begin(&s, 0xFFFFFF00u);
  TEST_ASSERT_EQUAL_HEX8(0, touchbar_settle_filter(&s, 0x00000100u, MID, false));
  touchbar_settle_tick(&s, 0xFFFFFF00u + TOUCHBAR_SETTLE_MAX_MS);
  TEST_ASSERT_FALSE(s.active);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_ghosts_never_pass_while_settling);
  RUN_TEST(test_bar_never_shut_longer_than_the_limit);
  RUN_TEST(test_touch_held_at_opening_needs_a_release);
  RUN_TEST(test_no_init_no_settling);
  RUN_TEST(test_wrap);
  return UNITY_END();
}
