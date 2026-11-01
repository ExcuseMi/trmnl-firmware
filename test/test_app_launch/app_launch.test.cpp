#include <app_api.h>
#include <app_launch.h>
#include <string.h>
#include <unity.h>

static app_launch_t rec;
static char app[32], arg[200];

void setUp(void) { memset(&rec, 0, sizeof(rec)); }
void tearDown(void) {}

void test_a_launch_is_recorded_and_taken_once() {
  TEST_ASSERT_EQUAL(0, app_launch_set(&rec, "trmnl-print", "trmnl-sketch:sketch/sk0001.png", true));
  TEST_ASSERT_TRUE(app_launch_take(&rec, app, sizeof(app), arg, sizeof(arg)));
  TEST_ASSERT_EQUAL_STRING("trmnl-print", app);
  TEST_ASSERT_EQUAL_STRING("trmnl-sketch:sketch/sk0001.png", arg);
  TEST_ASSERT_FALSE(app_launch_take(&rec, app, sizeof(app), arg, sizeof(arg)));
}

void test_bad_names_and_arguments_are_refused_and_record_nothing() {
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_set(&rec, "../x", "", true));
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_set(&rec, "a/b", "", true));
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_set(&rec, "", "", true));
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_set(&rec, nullptr, "", true));
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_set(&rec, "x", nullptr, true));
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_set(nullptr, "x", "", true));
  char big[130];
  memset(big, 'a', 129);
  big[129] = 0;
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_set(&rec, "x", big, true));
  TEST_ASSERT_FALSE(rec.pending);
}

void test_the_argument_limit_is_128_bytes() {
  char big[130];
  memset(big, 'a', 128);
  big[128] = 0;
  TEST_ASSERT_EQUAL(0, app_launch_set(&rec, "x", big, true));
  TEST_ASSERT_TRUE(app_launch_take(&rec, app, sizeof(app), arg, sizeof(arg)));
  TEST_ASSERT_EQUAL(128, strlen(arg));
}

void test_a_missing_app_is_enoent_and_keeps_an_earlier_launch() {
  TEST_ASSERT_EQUAL(0, app_launch_set(&rec, "a", "1", true));
  TEST_ASSERT_EQUAL(APP_ENOENT, app_launch_set(&rec, "b", "2", false));
  TEST_ASSERT_TRUE(app_launch_take(&rec, app, sizeof(app), arg, sizeof(arg)));
  TEST_ASSERT_EQUAL_STRING("a", app);
}

void test_take_into_small_buffers_keeps_the_launch() {
  TEST_ASSERT_EQUAL(0, app_launch_set(&rec, "abcdef", "12345", true));
  char tiny[4];
  TEST_ASSERT_FALSE(app_launch_take(&rec, tiny, sizeof(tiny), arg, sizeof(arg)));
  TEST_ASSERT_TRUE(rec.pending);
}

void test_arg_copy_gives_full_length_and_empty_default() {
  char out[8];
  TEST_ASSERT_EQUAL(11, app_arg_copy("hello world", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("hello w", out);
  TEST_ASSERT_EQUAL(0, app_arg_copy(nullptr, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_EQUAL(APP_EINVAL, app_arg_copy("x", nullptr, 4));
  TEST_ASSERT_EQUAL(APP_EINVAL, app_arg_copy("x", out, 0));
}

// API 2.12 device_page_open: the phone page opens next at a section; app "" is the page
void test_the_page_opens_next_at_a_section() {
  TEST_ASSERT_EQUAL(0, app_launch_page(&rec, "configure", "trmnl-print"));
  TEST_ASSERT_TRUE(app_launch_take(&rec, app, sizeof(app), arg, sizeof(arg)));
  TEST_ASSERT_EQUAL_STRING("", app);
  TEST_ASSERT_EQUAL_STRING("configure=trmnl-print", arg);
  TEST_ASSERT_EQUAL(0, app_launch_page(&rec, "devices", nullptr));
  TEST_ASSERT_TRUE(app_launch_take(&rec, app, sizeof(app), arg, sizeof(arg)));
  TEST_ASSERT_EQUAL_STRING("devices", arg);
  TEST_ASSERT_EQUAL(0, app_launch_set(&rec, "x", "a", true));
  TEST_ASSERT_EQUAL(0, app_launch_page(&rec, "this-x", nullptr)); // replaces the app launch
  TEST_ASSERT_TRUE(app_launch_take(&rec, app, sizeof(app), arg, sizeof(arg)));
  TEST_ASSERT_EQUAL_STRING("", app);
}

void test_a_bad_page_section_records_nothing() {
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_page(&rec, "wifi", "x"));
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_page(&rec, "", "x"));
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_page(&rec, nullptr, "x"));
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_page(&rec, "configure", nullptr));
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_page(&rec, "configure", "../x")); // the fragment holds only a valid name
  TEST_ASSERT_EQUAL(APP_EINVAL, app_launch_page(nullptr, "apps", nullptr));
  TEST_ASSERT_FALSE(rec.pending);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_launch_is_recorded_and_taken_once);
  RUN_TEST(test_bad_names_and_arguments_are_refused_and_record_nothing);
  RUN_TEST(test_the_argument_limit_is_128_bytes);
  RUN_TEST(test_a_missing_app_is_enoent_and_keeps_an_earlier_launch);
  RUN_TEST(test_take_into_small_buffers_keeps_the_launch);
  RUN_TEST(test_arg_copy_gives_full_length_and_empty_default);
  RUN_TEST(test_the_page_opens_next_at_a_section);
  RUN_TEST(test_a_bad_page_section_records_nothing);
  return UNITY_END();
}
