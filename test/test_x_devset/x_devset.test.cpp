// The device settings' checks (x_devset.h) and the USB devices seen (usb_seen.h)
#include <app_api.h>
#include <app_idle.h>
#include <string.h>
#include <unity.h>
#include <usb_seen.h>
#include <x_devset.h>

#include <string>

static int check(const char *k, const std::string &v) {
  char why[64];
  return xds_check(k, v.data(), v.size(), why, sizeof(why));
}

void test_printer_values() {
  TEST_ASSERT_EQUAL(0, check("printer", "name=Brother MFC\nhost=192.168.1.50\nport=631\npath=ipp/print\nformat=pwg\n"));
  TEST_ASSERT_EQUAL(0, check("printer", "host=printer.local"));
  TEST_ASSERT_EQUAL(0, check("printer", "")); // clears it
  const char *bad[] = {"name=x", "host=", "host=a b", "host=1.2.3.4\nport=0", "host=1.2.3.4\nport=70000",
                       "host=1.2.3.4\npath=../etc", "host=1.2.3.4\nformat=ps", "host=1.2.3.4\nevil=1",
                       "host=1.2.3.4\njunk", "host=1.2.3.4\nname=a\x01"};
  for (const char *b : bad) TEST_ASSERT_EQUAL_MESSAGE(APP_EINVAL, check("printer", b), b);
  TEST_ASSERT_EQUAL(APP_EINVAL, check("printer", "host=1.2.3.4\nname=" + std::string(600, 'a')));
}

void test_choices_numbers_and_unknown_keys() {
  TEST_ASSERT_EQUAL(0, check("pen_area", "keep"));
  TEST_ASSERT_EQUAL(0, check("pen_area", "fill"));
  TEST_ASSERT_EQUAL(APP_EINVAL, check("pen_area", "fil"));
  TEST_ASSERT_EQUAL(APP_EINVAL, check("pen_area", ""));
  TEST_ASSERT_EQUAL(0, check("mouse_speed", "fast"));
  TEST_ASSERT_EQUAL(0, check("cleanup_s", "0"));
  TEST_ASSERT_EQUAL(0, check("cleanup_s", "600"));
  TEST_ASSERT_EQUAL(APP_EINVAL, check("cleanup_s", "601"));
  TEST_ASSERT_EQUAL(APP_EINVAL, check("cleanup_s", "-1"));
  TEST_ASSERT_EQUAL(APP_EINVAL, check("cleanup_s", "5s"));
  TEST_ASSERT_EQUAL(0, check("app_idle_min", "off"));
  TEST_ASSERT_EQUAL(0, check("app_idle_min", "60"));
  TEST_ASSERT_EQUAL(APP_EINVAL, check("app_idle_min", "7"));
  TEST_ASSERT_EQUAL(APP_EINVAL, check("app_idle_min", "default")); // only an app's own choice has it
  TEST_ASSERT_EQUAL(APP_ENOENT, check("wifi_pass", "x"));
  TEST_ASSERT_EQUAL(APP_ENOENT, xds_check(nullptr, "x", 1, nullptr, 0));
  int n = 0;
  const xds_key_t *k = xds_keys(&n);
  TEST_ASSERT_EQUAL(5, n);
  for (int i = 0; i < n; i++) { // every default passes its own check
    if (k[i].def[0]) TEST_ASSERT_EQUAL_MESSAGE(0, check(k[i].key, k[i].def), k[i].key);
  }
}

// An app's own idle limit sits under "i_" + 8 hex: found by its shape, never listed, "default" allowed
void test_app_idle_override_keys() {
  char key[16];
  TEST_ASSERT_TRUE(app_idle_app_key("writer", key, sizeof(key)));
  TEST_ASSERT_NOT_NULL(xds_find(key));
  TEST_ASSERT_EQUAL_STRING("default", xds_find(key)->def);
  for (const char *v : {"default", "off", "5", "10", "15", "30", "60"}) TEST_ASSERT_EQUAL_MESSAGE(0, check(key, v), v);
  TEST_ASSERT_EQUAL(APP_EINVAL, check(key, "7"));
  TEST_ASSERT_EQUAL(APP_EINVAL, check(key, ""));
  TEST_ASSERT_NULL(xds_find("i_"));
  TEST_ASSERT_NULL(xds_find("i_1234567")); // 7 hex
  TEST_ASSERT_NULL(xds_find("i_123456789")); // 9 hex
  TEST_ASSERT_NULL(xds_find("i_1234567G"));
  TEST_ASSERT_NULL(xds_find("I_12345678"));
  int n = 0;
  xds_keys(&n);
  TEST_ASSERT_EQUAL(5, n); // not listed
}

void test_usb_seen_list() {
  usb_seen_t l;
  memset(&l, 0xa5, sizeof(l)); // RTC memory after power on
  usb_seen_init(&l);
  TEST_ASSERT_EQUAL(0, l.n);
  usb_seen_add(&l, 0x05e3, 0x0610, 9, nullptr);
  usb_seen_add(&l, 0x046d, 0xc31c, 0, nullptr);
  usb_seen_add(&l, 0x046d, 0xc31c, 0, "keyboard"); // the keyboard code names it
  TEST_ASSERT_EQUAL(2, l.n);
  TEST_ASSERT_EQUAL_STRING("keyboard", l.it[0].kind);
  TEST_ASSERT_EQUAL_STRING("hub", l.it[1].kind);
  usb_seen_add(&l, 0x046d, 0xc31c, 0, nullptr); // enumerates again: the name stays
  TEST_ASSERT_EQUAL_STRING("keyboard", l.it[0].kind);
  usb_seen_add(&l, 0x05e3, 0x0610, 9, nullptr); // back to the front
  TEST_ASSERT_EQUAL(0x05e3, l.it[0].vid);
  for (int i = 0; i < USB_SEEN_MAX + 3; i++) usb_seen_add(&l, 0x1000, (uint16_t)i, 0, "mouse");
  TEST_ASSERT_EQUAL(USB_SEEN_MAX, l.n);
  TEST_ASSERT_EQUAL(USB_SEEN_MAX + 2, l.it[0].pid);
  usb_seen_init(&l); // a valid list stays
  TEST_ASSERT_EQUAL(USB_SEEN_MAX, l.n);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_printer_values);
  RUN_TEST(test_choices_numbers_and_unknown_keys);
  RUN_TEST(test_app_idle_override_keys);
  RUN_TEST(test_usb_seen_list);
  return UNITY_END();
}
