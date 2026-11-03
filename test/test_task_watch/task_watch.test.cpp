#include <stdio.h>
#include <string.h>
#include <task_watch.h>
#include <unity.h>

static task_watch_t w;
static int loop_task, port_task, other_task; // their addresses stand for task handles
static char name[16];

void setUp(void) { memset(&w, 0, sizeof(w)); }
void tearDown(void) {}

// A task never added is not fed: the device then never calls the watchdog for it (ESP-IDF logs "task not found")
void test_an_unwatched_task_is_not_fed() {
  TEST_ASSERT_EQUAL(TASK_WATCH_NOT_WATCHED, task_watch_feed(&w, &loop_task, 10));
  TEST_ASSERT_TRUE(task_watch_add(&w, &loop_task, "loop", 0));
  TEST_ASSERT_EQUAL(TASK_WATCH_FED, task_watch_feed(&w, &loop_task, 10));
  TEST_ASSERT_EQUAL(TASK_WATCH_NOT_WATCHED, task_watch_feed(&w, &other_task, 10));
}

// The loop starts an app many times in one wake: adding it again keeps one entry
void test_adding_the_same_task_twice_keeps_one_entry() {
  TEST_ASSERT_TRUE(task_watch_add(&w, &loop_task, "loop", 0));
  TEST_ASSERT_TRUE(task_watch_add(&w, &loop_task, "loop", 5));
  TEST_ASSERT_EQUAL_STRING("loop", task_watch_name(&w, &loop_task));
  task_watch_remove(&w, &loop_task);
  TEST_ASSERT_NULL(task_watch_name(&w, &loop_task));
  TEST_ASSERT_EQUAL(TASK_WATCH_NOT_WATCHED, task_watch_feed(&w, &loop_task, 10));
  TEST_ASSERT_NULL(task_watch_stalest(&w, 10, nullptr));
}

void test_a_full_table_and_bad_names_are_refused() {
  static int t[TASK_WATCH_MAX + 1];
  char n[8];
  for (int i = 0; i < TASK_WATCH_MAX; i++) {
    snprintf(n, sizeof(n), "t%d", i);
    TEST_ASSERT_TRUE(task_watch_add(&w, &t[i], n, 0));
  }
  TEST_ASSERT_FALSE(task_watch_add(&w, &t[TASK_WATCH_MAX], "last", 0));
  setUp();
  TEST_ASSERT_FALSE(task_watch_add(&w, &loop_task, "", 0));
  TEST_ASSERT_FALSE(task_watch_add(&w, &loop_task, "a_name_too_long", 0));
  TEST_ASSERT_FALSE(task_watch_add(&w, nullptr, "loop", 0));
  TEST_ASSERT_TRUE(task_watch_add(&w, &loop_task, "loop", 0));
  TEST_ASSERT_FALSE(task_watch_add(&w, &port_task, "loop", 0)); // a hang command must name one task
}

// "TAPP HANG loop": the loop blocks at its next feed, and is not fed from then on
void test_the_hang_command_stops_the_feeding_of_that_task_only() {
  task_watch_add(&w, &loop_task, "loop", 0);
  task_watch_add(&w, &port_task, "usb_port", 0);
  TEST_ASSERT_EQUAL(TASK_WATCH_CMD_HANG, task_watch_command(&w, "TAPP HANG loop", name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("loop", name);
  TEST_ASSERT_EQUAL(TASK_WATCH_HANG, task_watch_feed(&w, &loop_task, 100));
  TEST_ASSERT_EQUAL(TASK_WATCH_HANG, task_watch_feed(&w, &loop_task, 200));
  TEST_ASSERT_EQUAL(TASK_WATCH_FED, task_watch_feed(&w, &port_task, 200));
  uint32_t age = 0;
  TEST_ASSERT_EQUAL_STRING("loop", task_watch_stalest(&w, 31000, &age));
  TEST_ASSERT_EQUAL_UINT32(31000, age); // its last feed was the add at 0
}

// A hang nothing would restart (the task not watched now) is refused, and other lines are not this command
void test_a_hang_of_an_unwatched_task_is_refused() {
  TEST_ASSERT_EQUAL(TASK_WATCH_CMD_NOT_WATCHED, task_watch_command(&w, "TAPP HANG loop", name, sizeof(name)));
  task_watch_add(&w, &loop_task, "loop", 0);
  TEST_ASSERT_EQUAL(TASK_WATCH_CMD_NOT_WATCHED, task_watch_command(&w, "TAPP HANG", name, sizeof(name)));
  TEST_ASSERT_EQUAL(TASK_WATCH_CMD_NOT_WATCHED, task_watch_command(&w, "TAPP HANG ", name, sizeof(name)));
  TEST_ASSERT_EQUAL(TASK_WATCH_CMD_NOT_WATCHED, task_watch_command(&w, "TAPP HANG loop now", name, sizeof(name)));
  TEST_ASSERT_EQUAL(TASK_WATCH_CMD_NOT_WATCHED, task_watch_command(&w, "TAPP HANG usb", name, sizeof(name)));
  TEST_ASSERT_EQUAL(TASK_WATCH_CMD_NONE, task_watch_command(&w, "TAPP EVLOG", name, sizeof(name)));
  TEST_ASSERT_EQUAL(TASK_WATCH_CMD_NONE, task_watch_command(&w, "TAPP HANGUP loop", name, sizeof(name)));
  TEST_ASSERT_EQUAL(TASK_WATCH_CMD_NONE, task_watch_command(&w, "TKEY TAPP HANG loop", name, sizeof(name)));
  TEST_ASSERT_EQUAL(TASK_WATCH_FED, task_watch_feed(&w, &loop_task, 10));
}

// A task that ends takes its hang with it: a new task under the same name is fed
void test_removing_a_task_drops_its_hang() {
  task_watch_add(&w, &port_task, "usb_port", 0);
  task_watch_command(&w, "TAPP HANG usb_port", name, sizeof(name));
  task_watch_remove(&w, &port_task);
  TEST_ASSERT_TRUE(task_watch_add(&w, &other_task, "usb_port", 50));
  TEST_ASSERT_EQUAL(TASK_WATCH_FED, task_watch_feed(&w, &other_task, 60));
}

// The note names the task fed longest ago, across millis() wrapping
void test_the_stalest_task_is_named() {
  task_watch_add(&w, &loop_task, "loop", 0xFFFFFF00u);
  task_watch_add(&w, &port_task, "usb_port", 0xFFFFFF00u);
  task_watch_feed(&w, &port_task, 0x100u);
  uint32_t age = 0;
  TEST_ASSERT_EQUAL_STRING("loop", task_watch_stalest(&w, 0x200u, &age));
  TEST_ASSERT_EQUAL_UINT32(0x300u, age);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_an_unwatched_task_is_not_fed);
  RUN_TEST(test_adding_the_same_task_twice_keeps_one_entry);
  RUN_TEST(test_a_full_table_and_bad_names_are_refused);
  RUN_TEST(test_the_hang_command_stops_the_feeding_of_that_task_only);
  RUN_TEST(test_a_hang_of_an_unwatched_task_is_refused);
  RUN_TEST(test_removing_a_task_drops_its_hang);
  RUN_TEST(test_the_stalest_task_is_named);
  return UNITY_END();
}
