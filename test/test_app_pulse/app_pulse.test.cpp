// A running app's pulse lines (app_pulse.h): the pen and refresh counts, the busiest tasks, within the event log's record
#include <app_pulse.h>
#include <evlog_ring.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_pen_line_has_the_counts_and_averages() {
  app_pulse_counts_t c = {3920, 3918, 2, 61, 61 * 142, 410, 12, 7, 9, 7 * 90};
  char out[160];
  TEST_ASSERT_TRUE(app_pulse_pen_line(&c, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("pen 3920/3918 drop 2, refresh 61 avg 142 max 410 ms, lock 12, ink 7/9 avg 90", out);
  TEST_ASSERT_TRUE(strlen(out) <= EVLOG_TEXT_MAX);
}

void test_pen_line_at_its_widest_fits_the_record() {
  // a 20 s window: 200 reports a second at most, refreshes of seconds at most, the dropped count since pen_enable
  app_pulse_counts_t c = {99999, 99999, 999999, 9999, 9999u * 9999u, 99999, 99999, 9999, 9999, 9999u * 9999u};
  char out[200];
  TEST_ASSERT_TRUE(app_pulse_pen_line(&c, out, sizeof(out)));
  TEST_ASSERT_TRUE(strlen(out) <= EVLOG_TEXT_MAX);
}

void test_no_pen_line_for_an_idle_window() {
  app_pulse_counts_t c = {};
  char out[160] = "x";
  TEST_ASSERT_FALSE(app_pulse_pen_line(&c, out, sizeof(out)));
  TEST_ASSERT_FALSE(app_pulse_pen_line(nullptr, out, sizeof(out)));
}

void test_cpu_line_shows_the_four_busiest_then_the_idle_tasks() {
  app_pulse_task_t t[7] = {{"IDLE0", 440}, {"app", 380}, {"usb_lib", 30}, {"IDLE1", 510}, {"ink", 290}, {"iqs323_task", 60}, {"cable", 0}};
  char out[160];
  TEST_ASSERT_TRUE(app_pulse_cpu_line(t, 7, 2000, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("cpu % app 19 ink 15 iqs323_task 3 usb_lib 2 IDLE1 26 IDLE0 22", out);
  TEST_ASSERT_TRUE(strlen(out) <= EVLOG_TEXT_MAX);
}

void test_cpu_line_with_many_long_names_fits_the_record() {
  app_pulse_task_t t[APP_PULSE_TASKS];
  for (int i = 0; i < APP_PULSE_TASKS; i++) {
    snprintf(t[i].name, sizeof(t[i].name), "task_name_%05d", i);
    t[i].run = 1000;
  }
  snprintf(t[0].name, sizeof(t[0].name), "IDLE0");
  snprintf(t[1].name, sizeof(t[1].name), "IDLE1");
  char out[200];
  TEST_ASSERT_TRUE(app_pulse_cpu_line(t, APP_PULSE_TASKS, 1000u * APP_PULSE_TASKS, out, sizeof(out)));
  TEST_ASSERT_TRUE(strlen(out) <= EVLOG_TEXT_MAX);
  TEST_ASSERT_NOT_NULL(strstr(out, "IDLE0 4"));
}

void test_cpu_line_without_run_time_gives_nothing() {
  app_pulse_task_t t[1] = {{"app", 5}};
  char out[160];
  TEST_ASSERT_FALSE(app_pulse_cpu_line(t, 1, 0, out, sizeof(out)));
  TEST_ASSERT_FALSE(app_pulse_cpu_line(nullptr, 1, 10, out, sizeof(out)));
  TEST_ASSERT_FALSE(app_pulse_cpu_line(t, 0, 10, out, sizeof(out)));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_pen_line_has_the_counts_and_averages);
  RUN_TEST(test_pen_line_at_its_widest_fits_the_record);
  RUN_TEST(test_no_pen_line_for_an_idle_window);
  RUN_TEST(test_cpu_line_shows_the_four_busiest_then_the_idle_tasks);
  RUN_TEST(test_cpu_line_with_many_long_names_fits_the_record);
  RUN_TEST(test_cpu_line_without_run_time_gives_nothing);
  return UNITY_END();
}
