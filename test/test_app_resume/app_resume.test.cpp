#include <app_resume.h>
#include <memory_persistence.h>
#include <unity.h>

static char path[64];

void test_nothing_to_resume_at_first() {
  MemoryPersistence p;
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
}

void test_resumes_an_app_that_started() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_input(p);
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_GO, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
  TEST_ASSERT_EQUAL_STRING("/apps/hello.elf", path);
}

// Taking it clears it: the resumed app has to start again before the next restart resumes it
void test_a_crash_while_starting_ends_the_resuming() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_input(p);
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_GO, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
  app_resume_starting(p, path);
  app_resume_input(p);
  // ... crashes before APP_RESUME_STARTED_MS, the device restarts
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
}

void test_an_app_that_never_started_is_not_resumed() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
}

void test_a_panic_or_watchdog_reset_ends_the_resuming() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_input(p);
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_CRASH, path, sizeof(path)));
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
}

// The task watchdog's restart (task_watch.h) counts as a crash: the app that hung is not reopened into the same hang
void test_a_task_watchdog_reset_is_a_crash() {
  TEST_ASSERT_EQUAL(APP_RESET_CRASH, app_reset_of(APP_RST_TASK_WDT));
  TEST_ASSERT_EQUAL(APP_RESET_CRASH, app_reset_of(APP_RST_INT_WDT));
  TEST_ASSERT_EQUAL(APP_RESET_CRASH, app_reset_of(APP_RST_WDT));
  TEST_ASSERT_EQUAL(APP_RESET_CRASH, app_reset_of(APP_RST_PANIC));
  TEST_ASSERT_EQUAL(APP_RESET_SLEEP_WAKE, app_reset_of(APP_RST_DEEPSLEEP));
  TEST_ASSERT_EQUAL(APP_RESET_RESTART, app_reset_of(1)); // power on
  TEST_ASSERT_EQUAL(APP_RESET_RESTART, app_reset_of(3)); // esp_restart
  MemoryPersistence p;
  app_resume_starting(p, "/apps/trmnl-sketch.elf");
  app_resume_input(p);
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, app_reset_of(APP_RST_TASK_WDT), path, sizeof(path)));
}

// Owner 2026-11-02: a restart (power-on, esptool, watchdog reset) goes home; only a sleep wake resumes
void test_a_restart_goes_home_and_clears_the_resume() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/trmnl-sketch.elf");
  app_resume_input(p);
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_RESTART, path, sizeof(path)));
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
}

// Owner 2026-11-02: a wake resumes only an app still running when the X slept (#93 to #94: Sketch ended by a long
// press came back after the wake; the flag was set again after the long press)
void test_a_wake_never_brings_back_an_app_that_was_not_running_at_sleep() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/trmnl-sketch.elf");
  app_resume_input(p);
  app_resume_started(p); // left set, as on the X
  app_resume_sleeping(p, nullptr);
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
  app_resume_starting(p, "/apps/trmnl-sketch.elf");
  app_resume_input(p);
  app_resume_started(p);
  app_resume_sleeping(p, "trmnl-sketch"); // still running when it slept
  TEST_ASSERT_EQUAL(APP_RESUME_GO, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
}

void test_an_ended_app_is_not_resumed() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_input(p);
  app_resume_started(p);
  app_resume_ended(p);
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
}

void test_a_path_too_long_for_the_buffer_is_not_resumed() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/hello.elf");
  app_resume_input(p);
  app_resume_started(p);
  char small[8];
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_SLEEP_WAKE, small, sizeof(small)));
}

// Started, ran past APP_RESUME_STARTED_MS, but no touch or key ever reached it (a dead touch bar): not resumed,
// named so the screen can say so; the next restart does not try again
void test_an_app_no_input_reached_is_not_resumed() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/writer.elf");
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_NO_INPUT, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
  TEST_ASSERT_EQUAL_STRING("/apps/writer.elf", path);
  TEST_ASSERT_EQUAL(APP_RESUME_NONE, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
}

// The input mark belongs to one run: a new start clears it
void test_input_of_an_earlier_run_does_not_count() {
  MemoryPersistence p;
  app_resume_starting(p, "/apps/writer.elf");
  app_resume_input(p);
  app_resume_ended(p);
  app_resume_starting(p, "/apps/writer.elf");
  app_resume_started(p);
  TEST_ASSERT_EQUAL(APP_RESUME_NO_INPUT, app_resume_take(p, APP_RESET_SLEEP_WAKE, path, sizeof(path)));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_nothing_to_resume_at_first);
  RUN_TEST(test_resumes_an_app_that_started);
  RUN_TEST(test_a_crash_while_starting_ends_the_resuming);
  RUN_TEST(test_an_app_that_never_started_is_not_resumed);
  RUN_TEST(test_a_panic_or_watchdog_reset_ends_the_resuming);
  RUN_TEST(test_a_task_watchdog_reset_is_a_crash);
  RUN_TEST(test_a_restart_goes_home_and_clears_the_resume);
  RUN_TEST(test_a_wake_never_brings_back_an_app_that_was_not_running_at_sleep);
  RUN_TEST(test_an_ended_app_is_not_resumed);
  RUN_TEST(test_a_path_too_long_for_the_buffer_is_not_resumed);
  RUN_TEST(test_an_app_no_input_reached_is_not_resumed);
  RUN_TEST(test_input_of_an_earlier_run_does_not_count);
  return UNITY_END();
}
