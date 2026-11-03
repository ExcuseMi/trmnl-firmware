#include <sleep_touch.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_a_ready_touch_bar_sleeps_the_whole_interval() {
  TEST_ASSERT_EQUAL_UINT32(3600, sleep_touch_seconds(3600, true));
  TEST_ASSERT_EQUAL_UINT32(30, sleep_touch_seconds(30, true));
}

// Owner's X 2026-11-03: asleep about an hour and the touch bar did not wake it
void test_a_failed_touch_prep_never_sleeps_the_interval_blind() {
  TEST_ASSERT_EQUAL_UINT32(SLEEP_TOUCH_FAILED_S, sleep_touch_seconds(3600, false));
  TEST_ASSERT_EQUAL_UINT32(30, sleep_touch_seconds(30, false)); // shorter already: kept
  TEST_ASSERT_EQUAL_UINT32(0, sleep_touch_seconds(0, false));
}

void test_the_event_log_line_says_seconds_wakes_and_prep() {
  char l[128];
  sleep_touch_line(l, sizeof(l), 900, 1, true);
  TEST_ASSERT_EQUAL_STRING("Sleep: 900 s, wake by timer and touch, touch prep ok", l);
  sleep_touch_line(l, sizeof(l), 900, 2, true);
  TEST_ASSERT_EQUAL_STRING("Sleep: 900 s, wake by timer and touch, touch prep ok on the retry", l);
  sleep_touch_line(l, sizeof(l), 3600, 2, false);
  TEST_ASSERT_EQUAL_STRING("Sleep: 60 s (not 3600, touch prep failed twice), wake by timer and touch", l);
  TEST_ASSERT_TRUE(sleep_touch_line(l, sizeof(l), 4294967295u, 2, false) < 100); // within EVLOG_TEXT_MAX
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_ready_touch_bar_sleeps_the_whole_interval);
  RUN_TEST(test_a_failed_touch_prep_never_sleeps_the_interval_blind);
  RUN_TEST(test_the_event_log_line_says_seconds_wakes_and_prep);
  return UNITY_END();
}
