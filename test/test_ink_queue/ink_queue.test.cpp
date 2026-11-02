#include <ink_queue.h>
#include <unity.h>

static ink_queue_t q;

void setUp() { ink_queue_init(&q); }
void tearDown() {}

// ---- pass choice ----

// Owner 2026-11-02: "undo the 2 passes": ink keeps PARTIAL's pass count, only the waiting is gone
void test_ink_is_queued_with_the_same_passes_as_partial() {
  ink_plan_t ink = ink_plan(APP_REFRESH_INK, true), partial = ink_plan(APP_REFRESH_PARTIAL, true);
  TEST_ASSERT_EQUAL(INK_PLAN_QUEUE, ink.kind);
  TEST_ASSERT_FALSE(ink.wait_idle); // returns at once
  TEST_ASSERT_EQUAL(partial.passes, ink.passes);
}

void test_partial_and_full_wait_for_the_queue_and_block() {
  ink_plan_t partial = ink_plan(APP_REFRESH_PARTIAL, true), full = ink_plan(APP_REFRESH_FULL, true);
  TEST_ASSERT_EQUAL(INK_PLAN_PARTIAL, partial.kind);
  TEST_ASSERT_EQUAL(DISPLAY_PARTIAL_PASSES, partial.passes);
  TEST_ASSERT_TRUE(partial.wait_idle);
  TEST_ASSERT_EQUAL(INK_PLAN_FULL, full.kind);
  TEST_ASSERT_TRUE(full.wait_idle);
  TEST_ASSERT_TRUE(ink_plan(APP_REFRESH_FULL, false).wait_idle);
}

void test_a_gray_screen_gets_a_full_refresh_for_ink() {
  ink_plan_t p = ink_plan(APP_REFRESH_INK, false);
  TEST_ASSERT_EQUAL(INK_PLAN_FULL, p.kind);
  TEST_ASSERT_TRUE(p.wait_idle);
  TEST_ASSERT_EQUAL(INK_PLAN_FULL, ink_plan(APP_REFRESH_PARTIAL, false).kind);
}

void test_an_unknown_kind_is_refused() {
  TEST_ASSERT_EQUAL(INK_PLAN_BAD, ink_plan(APP_REFRESH_INK + 1, true).kind);
  TEST_ASSERT_EQUAL(INK_PLAN_BAD, ink_plan(-1, true).kind);
}

// ---- row merging ----

void test_requests_waiting_merge_to_one_range() {
  TEST_ASSERT_TRUE(ink_queue_add(&q, 100, 120)); // wakes the task
  TEST_ASSERT_FALSE(ink_queue_add(&q, 10, 20)); // already woken
  TEST_ASSERT_FALSE(ink_queue_add(&q, 110, 300));
  int y0 = -1, y1 = -1;
  TEST_ASSERT_TRUE(ink_queue_take(&q, &y0, &y1));
  TEST_ASSERT_EQUAL(10, y0);
  TEST_ASSERT_EQUAL(300, y1);
  TEST_ASSERT_FALSE(ink_queue_take(&q, &y0, &y1)); // one run for all three
}

void test_a_request_while_one_runs_runs_next_alone() {
  int y0, y1;
  ink_queue_add(&q, 10, 20);
  ink_queue_take(&q, &y0, &y1);
  TEST_ASSERT_TRUE(ink_queue_add(&q, 500, 510)); // the task wakes again for it
  ink_queue_done(&q, true);
  TEST_ASSERT_TRUE(ink_queue_take(&q, &y0, &y1));
  TEST_ASSERT_EQUAL(500, y0); // not merged with the rows that ran
  TEST_ASSERT_EQUAL(510, y1);
}

// ---- hand-over: the app draws on while an update runs ----

void test_rows_inside_the_running_ones_run_again() {
  int y0, y1;
  ink_queue_add(&q, 0, 1403);
  ink_queue_take(&q, &y0, &y1);
  // the driver read the framebuffer at the start; the app drew in rows it already read
  ink_queue_add(&q, 700, 710);
  ink_queue_done(&q, true);
  TEST_ASSERT_TRUE(ink_queue_take(&q, &y0, &y1));
  TEST_ASSERT_EQUAL(700, y0);
  TEST_ASSERT_EQUAL(710, y1);
}

// ---- FULL waits for INK ----

void test_not_idle_until_every_request_ran() {
  int y0, y1;
  TEST_ASSERT_TRUE(ink_queue_idle(&q));
  ink_queue_add(&q, 1, 2);
  TEST_ASSERT_FALSE(ink_queue_idle(&q)); // waiting
  ink_queue_take(&q, &y0, &y1);
  TEST_ASSERT_FALSE(ink_queue_idle(&q)); // running
  ink_queue_add(&q, 3, 4);
  ink_queue_done(&q, true);
  TEST_ASSERT_FALSE(ink_queue_idle(&q)); // the next one waits
  ink_queue_take(&q, &y0, &y1);
  ink_queue_done(&q, true);
  TEST_ASSERT_TRUE(ink_queue_idle(&q));
}

// ---- the way it breaks: a failed update ----

void test_a_failed_update_is_reported_once() {
  int y0, y1;
  ink_queue_add(&q, 1, 2);
  ink_queue_take(&q, &y0, &y1);
  ink_queue_done(&q, false);
  TEST_ASSERT_TRUE(ink_queue_idle(&q)); // a FULL after it does not hang
  TEST_ASSERT_EQUAL(APP_EIO, ink_queue_take_error(&q));
  TEST_ASSERT_EQUAL(0, ink_queue_take_error(&q));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_ink_is_queued_with_the_same_passes_as_partial);
  RUN_TEST(test_partial_and_full_wait_for_the_queue_and_block);
  RUN_TEST(test_a_gray_screen_gets_a_full_refresh_for_ink);
  RUN_TEST(test_an_unknown_kind_is_refused);
  RUN_TEST(test_requests_waiting_merge_to_one_range);
  RUN_TEST(test_a_request_while_one_runs_runs_next_alone);
  RUN_TEST(test_rows_inside_the_running_ones_run_again);
  RUN_TEST(test_not_idle_until_every_request_ran);
  RUN_TEST(test_a_failed_update_is_reported_once);
  return UNITY_END();
}
