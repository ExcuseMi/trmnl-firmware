// A failed NVS write is reported (nvs_write.h; owner's X 2026-11-02, a resume flag that never cleared)
#include <nvs_write.h>
#include <string.h>
#include <unity.h>

static char last_key[32];
static size_t last_got, last_want;
static int reports;
static void report(const char *key, size_t got, size_t want) {
  strncpy(last_key, key, sizeof(last_key) - 1);
  last_got = got;
  last_want = want;
  reports++;
}

void setUp() { reports = 0; }
void tearDown() {}

void test_a_write_that_took_is_quiet() {
  TEST_ASSERT_TRUE(nvs_write_check("app_resume", 1, 1, report));
  TEST_ASSERT_TRUE(nvs_write_check("app_last", 23, 23, report));
  TEST_ASSERT_EQUAL(0, reports);
}

void test_a_write_that_wrote_nothing_is_reported_and_counted() {
  unsigned before = nvs_write_failures();
  TEST_ASSERT_FALSE(nvs_write_check("app_resume", 0, 1, report)); // putBool on a full NVS
  TEST_ASSERT_EQUAL(1, reports);
  TEST_ASSERT_EQUAL_STRING("app_resume", last_key);
  TEST_ASSERT_EQUAL(0, last_got);
  TEST_ASSERT_EQUAL(1, last_want);
  TEST_ASSERT_FALSE(nvs_write_check(nullptr, 3, 23, nullptr)); // no reporter: still counted, no crash
  TEST_ASSERT_EQUAL_UINT(before + 2, nvs_write_failures());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_write_that_took_is_quiet);
  RUN_TEST(test_a_write_that_wrote_nothing_is_reported_and_counted);
  return UNITY_END();
}
