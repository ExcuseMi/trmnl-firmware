#include <unity.h>

#include <fs_space.h>

// Owner's X 2026-11-02: "10.1 of 10.1 MB used, 0 free" with 5.4 MB of files; uploads refused for no space

struct Fake {
  uint64_t readings[8];
  int n, at, waits;
};

static uint64_t used(void *c) {
  Fake *f = (Fake *)c;
  return f->readings[f->at < f->n ? f->at++ : f->n - 1];
}
static void wait_ms(void *c, uint32_t) { ((Fake *)c)->waits++; }

static const uint64_t TOTAL = 10133504;

void test_a_clamped_full_reading_is_read_again() {
  Fake f = {{TOTAL, 5472256}, 2, 0, 0};
  fs_space_port_t p = {&f, used, wait_ms};
  bool unsure = true;
  uint64_t u = 0;
  TEST_ASSERT_EQUAL_UINT64(TOTAL - 5472256, fs_space_free(&p, TOTAL, &u, &unsure));
  TEST_ASSERT_EQUAL_UINT64(5472256, u);
  TEST_ASSERT_FALSE(unsure);
  TEST_ASSERT_EQUAL(1, f.waits);
}

void test_full_every_time_is_zero_and_unsure() {
  Fake f = {{TOTAL}, 1, 0, 0};
  fs_space_port_t p = {&f, used, wait_ms};
  bool unsure = false;
  TEST_ASSERT_EQUAL_UINT64(0, fs_space_free(&p, TOTAL, nullptr, &unsure));
  TEST_ASSERT_TRUE(unsure);
  TEST_ASSERT_EQUAL(FS_SPACE_TRIES - 1, f.waits); // read FS_SPACE_TRIES times
}

void test_a_good_reading_is_used_at_once() {
  Fake f = {{100}, 1, 0, 0};
  fs_space_port_t p = {&f, used, wait_ms};
  TEST_ASSERT_EQUAL_UINT64(TOTAL - 100, fs_space_free(&p, TOTAL, nullptr, nullptr));
  TEST_ASSERT_EQUAL(0, f.waits);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_clamped_full_reading_is_read_again);
  RUN_TEST(test_full_every_time_is_zero_and_unsure);
  RUN_TEST(test_a_good_reading_is_used_at_once);
  return UNITY_END();
}
