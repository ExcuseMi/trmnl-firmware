#include <string.h>
#include <unity.h>
#include <webshare_host.h>

static const uint8_t MAC[6] = {0x10, 0x20, 0x30, 0xAB, 0xCD, 0xEF};

void setUp(void) {}
void tearDown(void) {}

void test_device_id_kept_host_lowercase() {
  char s[WS_HOST_MAX + 1], h[WS_HOST_MAX + 1];
  ws_host_names("TRMNL-X-BJRLAT", "TRMNL-X", MAC, s, h);
  TEST_ASSERT_EQUAL_STRING("TRMNL-X-BJRLAT", s);
  TEST_ASSERT_EQUAL_STRING("trmnl-x-bjrlat", h);
}

void test_prefix_only_gets_mac_suffix() {
  char s[WS_HOST_MAX + 1], h[WS_HOST_MAX + 1];
  ws_host_names("TRMNL-X", "TRMNL-X", MAC, s, h);
  TEST_ASSERT_EQUAL_STRING("TRMNL-X-ABCDEF", s);
  TEST_ASSERT_EQUAL_STRING("trmnl-x-abcdef", h);
  ws_host_names("", "TRMNL-X", MAC, s, h);
  TEST_ASSERT_EQUAL_STRING("TRMNL-X-ABCDEF", s);
}

void test_bad_characters_and_length() {
  char s[WS_HOST_MAX + 1], h[WS_HOST_MAX + 1];
  ws_host_names("my x.local!_", "TRMNL-X", MAC, s, h);
  TEST_ASSERT_EQUAL_STRING("my-xlocal", s);
  ws_host_names("---", "TRMNL-X", MAC, s, h);
  TEST_ASSERT_EQUAL_STRING("TRMNL-X-ABCDEF", s);
  ws_host_names("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-ABCDEFGH", "TRMNL-X", MAC, s, h);
  TEST_ASSERT_EQUAL_UINT(WS_HOST_MAX, strlen(s));
  TEST_ASSERT_EQUAL_UINT(WS_HOST_MAX, strlen(h));
  for (const char *p = h; *p; p++) TEST_ASSERT_TRUE((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-');
  TEST_ASSERT_NOT_EQUAL('-', h[strlen(h) - 1]);
}

void test_long_prefix_with_suffix_fits() {
  char s[WS_HOST_MAX + 1], h[WS_HOST_MAX + 1];
  const char *fb = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345678";
  ws_host_names("", fb, MAC, s, h);
  TEST_ASSERT_TRUE(strlen(s) <= WS_HOST_MAX);
  TEST_ASSERT_EQUAL_STRING("-ABCDEF", s + strlen(s) - 7);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_device_id_kept_host_lowercase);
  RUN_TEST(test_prefix_only_gets_mac_suffix);
  RUN_TEST(test_bad_characters_and_length);
  RUN_TEST(test_long_prefix_with_suffix_fits);
  return UNITY_END();
}
