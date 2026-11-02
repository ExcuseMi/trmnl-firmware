// The NVS cleanup is exactly what the owner agreed to (2026-11-02), and never Wi-Fi, Bluetooth, calibration or Sketch
#include <nvs_clean.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_the_list_is_the_agreed_one() {
  size_t n = 0;
  const nvs_clean_item_t *l = nvs_clean_list(&n);
  TEST_ASSERT_EQUAL(8, n);
  for (int i = 0; i < 7; i++) {
    char want[8] = "log_0";
    want[4] = (char)('0' + i);
    TEST_ASSERT_EQUAL_STRING("data", l[i].ns);
    TEST_ASSERT_EQUAL_STRING(want, l[i].key);
  }
  TEST_ASSERT_EQUAL_STRING("app84cc956a", l[7].ns);
  TEST_ASSERT_NULL(l[7].key);
}

void test_nothing_on_it_is_protected_and_the_protected_stay() {
  size_t n = 0;
  const nvs_clean_item_t *l = nvs_clean_list(&n);
  for (size_t i = 0; i < n; i++) TEST_ASSERT_FALSE(nvs_clean_protected(l[i].ns));
  const char *keep[] = {"wificaptive", "nvs.net80211", "phy", "nimble_bond", "ble_kbd", "app8c09e2df"};
  for (const char *k : keep) TEST_ASSERT_TRUE_MESSAGE(nvs_clean_protected(k), k);
  TEST_ASSERT_TRUE(nvs_clean_protected(nullptr));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_list_is_the_agreed_one);
  RUN_TEST(test_nothing_on_it_is_protected_and_the_protected_stay);
  return UNITY_END();
}
