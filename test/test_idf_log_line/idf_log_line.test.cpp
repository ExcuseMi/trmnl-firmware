// ESP-IDF's error and warning lines into the event log (idf_log_line.h): which lines, how cut, how many a minute
#include <idf_log_line.h>
#include <string.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_error_and_warning_lines_are_kept_without_the_timestamp() {
  char out[IDF_LOG_LINE_MAX + 8];
  TEST_ASSERT_TRUE(idf_log_line_keep("E (12345) EXT_HUB: [1] Interrupt transfer failed, status 4\n", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("E EXT_HUB: [1] Interrupt transfer failed, status 4", out);
  TEST_ASSERT_TRUE(idf_log_line_keep("W (7) HUB: Port 3 disconnected\r\n", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("W HUB: Port 3 disconnected", out);
}

void test_other_levels_and_noise_are_not_kept() {
  char out[IDF_LOG_LINE_MAX + 8];
  TEST_ASSERT_FALSE(idf_log_line_keep("I (12) wifi: connected\n", out, sizeof(out)));
  TEST_ASSERT_FALSE(idf_log_line_keep("D (12) x: y\n", out, sizeof(out)));
  TEST_ASSERT_FALSE(idf_log_line_keep("Error: plain text\n", out, sizeof(out)));
  TEST_ASSERT_FALSE(idf_log_line_keep("E (12 no closing\n", out, sizeof(out)));
  TEST_ASSERT_FALSE(idf_log_line_keep("E (12) \n", out, sizeof(out)));
  TEST_ASSERT_FALSE(idf_log_line_keep(nullptr, out, sizeof(out)));
  TEST_ASSERT_FALSE(idf_log_line_keep("E (1) a: b\n", nullptr, 0));
}

void test_a_long_line_is_cut_to_the_record() {
  char line[300] = "E (1) T: ";
  memset(line + 9, 'x', 280);
  line[289] = '\n';
  line[290] = '\0';
  char out[IDF_LOG_LINE_MAX + 8];
  memset(out, 'Z', sizeof(out));
  TEST_ASSERT_TRUE(idf_log_line_keep(line, out, sizeof(out)));
  TEST_ASSERT_EQUAL(IDF_LOG_LINE_MAX, (int)strlen(out));
  TEST_ASSERT_EQUAL('Z', out[IDF_LOG_LINE_MAX + 1]); // nothing past the terminator
  char small[10];
  TEST_ASSERT_TRUE(idf_log_line_keep(line, small, sizeof(small)));
  TEST_ASSERT_EQUAL(9, (int)strlen(small));
}

void test_at_most_the_limit_a_minute_then_counted() {
  idf_log_rate_t r = {};
  unsigned before = 99;
  for (int i = 0; i < IDF_LOG_LINES_PER_MIN; i++) TEST_ASSERT_TRUE(idf_log_rate_allow(&r, 1000 + i, &before));
  TEST_ASSERT_EQUAL(0, before);
  TEST_ASSERT_FALSE(idf_log_rate_allow(&r, 2000, &before));
  TEST_ASSERT_FALSE(idf_log_rate_allow(&r, 3000, &before));
  TEST_ASSERT_EQUAL(0, before); // the minute goes on: nothing to say yet
  TEST_ASSERT_TRUE(idf_log_rate_allow(&r, 1000 + 60000, &before));
  TEST_ASSERT_EQUAL(2, before); // said once, with the next kept line
  TEST_ASSERT_TRUE(idf_log_rate_allow(&r, 1000 + 60001, &before));
  TEST_ASSERT_EQUAL(0, before);
}

void test_the_clock_wrap_starts_a_new_minute() {
  idf_log_rate_t r = {};
  TEST_ASSERT_TRUE(idf_log_rate_allow(&r, 0xFFFFFF00u, nullptr));
  for (int i = 1; i < IDF_LOG_LINES_PER_MIN; i++) idf_log_rate_allow(&r, 0xFFFFFF00u + i, nullptr);
  TEST_ASSERT_FALSE(idf_log_rate_allow(&r, 0xFFFFFFF0u, nullptr));
  TEST_ASSERT_TRUE(idf_log_rate_allow(&r, 60000u, nullptr)); // 60.3 s later across the wrap
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_error_and_warning_lines_are_kept_without_the_timestamp);
  RUN_TEST(test_other_levels_and_noise_are_not_kept);
  RUN_TEST(test_a_long_line_is_cut_to_the_record);
  RUN_TEST(test_at_most_the_limit_a_minute_then_counted);
  RUN_TEST(test_the_clock_wrap_starts_a_new_minute);
  return UNITY_END();
}
