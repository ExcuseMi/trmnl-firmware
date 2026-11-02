// The idle limit of apps (app_idle.h): texts, which limit counts, the timer, the hold, the warning, no resume
#include <app_idle.h>
#include <app_resume.h>
#include <unity.h>

#include <string.h>

#include "memory_persistence.h"

#define MIN(n) ((uint32_t)(n) * 60000u)

void setUp() {}
void tearDown() {}

void test_texts() {
  TEST_ASSERT_EQUAL(0, app_idle_parse("off"));
  TEST_ASSERT_EQUAL(5, app_idle_parse("5"));
  TEST_ASSERT_EQUAL(10, app_idle_parse("10"));
  TEST_ASSERT_EQUAL(60, app_idle_parse("60"));
  const char *bad[] = {"", "default", "7", "-5", "05", "600", "Off", "5 ", nullptr};
  for (const char *b : bad) TEST_ASSERT_EQUAL_MESSAGE(-1, app_idle_parse(b), b ? b : "null");
}

// The app's own choice beats the device's; "default", nothing or rubbish falls to the device; rubbish there to 10
void test_override_precedence() {
  TEST_ASSERT_EQUAL(30, app_idle_minutes("10", "30"));
  TEST_ASSERT_EQUAL(0, app_idle_minutes("10", "off"));
  TEST_ASSERT_EQUAL(10, app_idle_minutes("10", "default"));
  TEST_ASSERT_EQUAL(15, app_idle_minutes("15", ""));
  TEST_ASSERT_EQUAL(15, app_idle_minutes("15", nullptr));
  TEST_ASSERT_EQUAL(15, app_idle_minutes("15", "x"));
  TEST_ASSERT_EQUAL(5, app_idle_minutes("off", "5")); // the device is off, this app is not
  TEST_ASSERT_EQUAL(0, app_idle_minutes("off", "default"));
  TEST_ASSERT_EQUAL(APP_IDLE_DEFAULT_MIN, app_idle_minutes("", ""));
  TEST_ASSERT_EQUAL(APP_IDLE_DEFAULT_MIN, app_idle_minutes(nullptr, nullptr));
}

void test_app_key() {
  char a[APP_IDLE_KEY_MAX + 1], b[APP_IDLE_KEY_MAX + 1];
  TEST_ASSERT_TRUE(app_idle_app_key("writer", a, sizeof(a)));
  TEST_ASSERT_TRUE(app_idle_app_key("fpv", b, sizeof(b)));
  TEST_ASSERT_EQUAL(10, strlen(a));
  TEST_ASSERT_EQUAL_INT8_ARRAY("i_", a, 2);
  TEST_ASSERT_NOT_EQUAL(0, strcmp(a, b));
  TEST_ASSERT_FALSE(app_idle_app_key("", a, sizeof(a)));
  TEST_ASSERT_FALSE(app_idle_app_key(nullptr, a, sizeof(a)));
  TEST_ASSERT_FALSE(app_idle_app_key("writer", a, 5)); // too small
}

void test_runs_until_the_limit() {
  app_idle_t t;
  app_idle_begin(&t, 1000, 5);
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, 1000 + MIN(5) - APP_IDLE_WARN_MS - 1));
  TEST_ASSERT_EQUAL(APP_IDLE_WARN, app_idle_poll(&t, 1000 + MIN(5) - APP_IDLE_WARN_MS));
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, 1000 + MIN(5) - 1)); // the warning only once
  TEST_ASSERT_EQUAL(APP_IDLE_EXPIRED, app_idle_poll(&t, 1000 + MIN(5)));
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, 1000 + MIN(6))); // and the end only once
}

void test_off_never_ends() {
  app_idle_t t;
  app_idle_begin(&t, 0, 0);
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, MIN(600)));
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, 0xfffffff0u));
}

void test_a_sign_of_a_person_restarts_the_stretch() {
  app_idle_t t;
  app_idle_begin(&t, 0, 5);
  TEST_ASSERT_EQUAL(APP_IDLE_WARN, app_idle_poll(&t, MIN(5) - APP_IDLE_WARN_MS));
  TEST_ASSERT_TRUE(app_idle_warning(&t));
  app_idle_activity(&t, MIN(4) + 45000u); // a touch while the warning is up
  TEST_ASSERT_FALSE(app_idle_warning(&t));
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, MIN(5)));
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, MIN(4) + 45000u + MIN(5) - APP_IDLE_WARN_MS - 1));
  TEST_ASSERT_EQUAL(APP_IDLE_WARN, app_idle_poll(&t, MIN(4) + 45000u + MIN(5) - APP_IDLE_WARN_MS)); // warns again
  TEST_ASSERT_EQUAL(APP_IDLE_EXPIRED, app_idle_poll(&t, MIN(4) + 45000u + MIN(5)));
}

// Once the exit is asked a late touch must not take it back (the runner already sent APP_EVENT_EXIT)
void test_late_touch_after_the_end() {
  app_idle_t t;
  app_idle_begin(&t, 0, 5);
  TEST_ASSERT_EQUAL(APP_IDLE_EXPIRED, app_idle_poll(&t, MIN(5)));
  app_idle_activity(&t, MIN(5) + 10);
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, MIN(20)));
}

void test_hold() {
  app_idle_t t;
  app_idle_begin(&t, 0, 5);
  app_idle_hold(&t, true, 1000);
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, MIN(60))); // held for an hour: nothing
  app_idle_hold(&t, false, MIN(60)); // letting go: a full stretch from now
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, MIN(60) + MIN(5) - APP_IDLE_WARN_MS - 1));
  TEST_ASSERT_EQUAL(APP_IDLE_EXPIRED, app_idle_poll(&t, MIN(60) + MIN(5)));
}

void test_hold_while_the_warning_is_up() {
  app_idle_t t;
  app_idle_begin(&t, 0, 5);
  TEST_ASSERT_EQUAL(APP_IDLE_WARN, app_idle_poll(&t, MIN(5) - APP_IDLE_WARN_MS));
  app_idle_hold(&t, true, MIN(5) - 1000);
  TEST_ASSERT_FALSE(app_idle_warning(&t)); // the toast goes away
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, MIN(9)));
}

void test_clock_wrap() {
  app_idle_t t;
  uint32_t start = 0xffffffffu - 1000; // the 32-bit millisecond clock wraps during the stretch
  app_idle_begin(&t, start, 5);
  TEST_ASSERT_EQUAL(APP_IDLE_NONE, app_idle_poll(&t, start + 1000));
  TEST_ASSERT_EQUAL(APP_IDLE_WARN, app_idle_poll(&t, start + MIN(5) - APP_IDLE_WARN_MS));
  TEST_ASSERT_EQUAL(APP_IDLE_EXPIRED, app_idle_poll(&t, start + MIN(5)));
}

// A timed-out app is quit, not interrupted: the runner clears the resume mark the way a long press does
// (app_resume_ended), so the next wake resumes nothing
void test_no_resume_after_a_timeout() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/writer.elf");
  app_resume_started(p);
  app_resume_input(p);
  app_idle_t t;
  app_idle_begin(&t, 0, 5);
  if (app_idle_poll(&t, MIN(5)) == APP_IDLE_EXPIRED) app_idle_close(p); // what the runner does
  char path[64];
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_texts);
  RUN_TEST(test_override_precedence);
  RUN_TEST(test_app_key);
  RUN_TEST(test_runs_until_the_limit);
  RUN_TEST(test_off_never_ends);
  RUN_TEST(test_a_sign_of_a_person_restarts_the_stretch);
  RUN_TEST(test_late_touch_after_the_end);
  RUN_TEST(test_hold);
  RUN_TEST(test_hold_while_the_warning_is_up);
  RUN_TEST(test_clock_wrap);
  RUN_TEST(test_no_resume_after_a_timeout);
  return UNITY_END();
}
