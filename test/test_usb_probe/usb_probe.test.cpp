#include <string.h>
#include <unity.h>
#include <usb_probe.h>

void setUp() {}
void tearDown() {}

void test_hex_formats_and_stops_at_whole_bytes() {
  uint8_t d[] = {0x01, 0xab, 0xff};
  char out[USB_PROBE_HEX_CAP];
  TEST_ASSERT_EQUAL(3, usb_probe_hex(out, sizeof(out), d, 3));
  TEST_ASSERT_EQUAL_STRING("01 ab ff", out);
  char small[6]; // "01 ab" fits, a third byte does not
  TEST_ASSERT_EQUAL(2, usb_probe_hex(small, sizeof(small), d, 3));
  TEST_ASSERT_EQUAL_STRING("01 ab", small);
  char one[2];
  TEST_ASSERT_EQUAL(0, usb_probe_hex(one, sizeof(one), d, 3)); // not even one byte: empty, NUL ended
  TEST_ASSERT_EQUAL_STRING("", one);
  TEST_ASSERT_EQUAL(0, usb_probe_hex(nullptr, 0, d, 3));
  TEST_ASSERT_EQUAL(0, usb_probe_hex(out, sizeof(out), nullptr, 3));
}

void test_a_full_line_fits_the_hex_cap() {
  uint8_t d[USB_PROBE_DESC_LINE];
  memset(d, 0xee, sizeof(d));
  char out[USB_PROBE_HEX_CAP];
  TEST_ASSERT_EQUAL(USB_PROBE_DESC_LINE, usb_probe_hex(out, sizeof(out), d, sizeof(d)));
}

void test_descriptor_lines_cover_it_once_and_are_bounded() {
  size_t off = 99;
  TEST_ASSERT_EQUAL(0, usb_probe_desc_lines(0));
  TEST_ASSERT_EQUAL(0, usb_probe_desc_line(0, 0, &off));
  TEST_ASSERT_EQUAL(1, usb_probe_desc_lines(1));
  TEST_ASSERT_EQUAL(1, usb_probe_desc_line(1, 0, &off));
  size_t len = USB_PROBE_DESC_LINE * 2 + 5;
  TEST_ASSERT_EQUAL(3, usb_probe_desc_lines(len));
  size_t total = 0;
  for (size_t l = 0; l < 3; l++) {
    size_t n = usb_probe_desc_line(len, l, &off);
    TEST_ASSERT_EQUAL(total, off);
    total += n;
  }
  TEST_ASSERT_EQUAL(len, total);
  TEST_ASSERT_EQUAL(0, usb_probe_desc_line(len, 3, &off));
  // a huge descriptor: cut at the maximum
  total = 0;
  size_t lines = usb_probe_desc_lines(100000);
  for (size_t l = 0; l < lines; l++) total += usb_probe_desc_line(100000, l, &off);
  TEST_ASSERT_EQUAL(USB_PROBE_DESC_MAX, total);
  TEST_ASSERT_EQUAL(0, usb_probe_desc_line(100000, lines, &off));
}

void test_first_reports_in_full_then_counted_every_two_seconds() {
  usb_probe_t p;
  usb_probe_init(&p, 1000);
  uint32_t count = 0;
  for (int i = 0; i < USB_PROBE_FULL_REPORTS; i++) {
    TEST_ASSERT_TRUE(usb_probe_report(&p));
    TEST_ASSERT_FALSE(usb_probe_summary(&p, 1000 + 5000, &count)); // none while only full ones came
  }
  TEST_ASSERT_FALSE(usb_probe_report(&p)); // the 41st
  TEST_ASSERT_FALSE(usb_probe_summary(&p, 1000 + USB_PROBE_SUMMARY_MS - 1, &count)); // too early
  TEST_ASSERT_TRUE(usb_probe_summary(&p, 1000 + USB_PROBE_SUMMARY_MS, &count));
  TEST_ASSERT_EQUAL(USB_PROBE_FULL_REPORTS + 1, count);
  // nothing new: silent, however long
  TEST_ASSERT_FALSE(usb_probe_summary(&p, 1000 + 10 * USB_PROBE_SUMMARY_MS, &count));
  // a flood: still one line per period
  for (int i = 0; i < 5000; i++) usb_probe_report(&p);
  TEST_ASSERT_TRUE(usb_probe_summary(&p, 1000 + 11 * USB_PROBE_SUMMARY_MS, &count));
  TEST_ASSERT_EQUAL(USB_PROBE_FULL_REPORTS + 5001, count);
  TEST_ASSERT_FALSE(usb_probe_summary(&p, 1000 + 11 * USB_PROBE_SUMMARY_MS + 10, &count));
}

void test_counter_does_not_wrap() {
  usb_probe_t p;
  usb_probe_init(&p, 0);
  p.total = 0xFFFFFFFFu;
  TEST_ASSERT_FALSE(usb_probe_report(&p));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, p.total);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_hex_formats_and_stops_at_whole_bytes);
  RUN_TEST(test_a_full_line_fits_the_hex_cap);
  RUN_TEST(test_descriptor_lines_cover_it_once_and_are_bounded);
  RUN_TEST(test_first_reports_in_full_then_counted_every_two_seconds);
  RUN_TEST(test_counter_does_not_wrap);
  return UNITY_END();
}
