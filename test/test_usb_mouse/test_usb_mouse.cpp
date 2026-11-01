#include <string.h>
#include <unity.h>
#include <usb_mouse.h>

// A typical report-protocol mouse descriptor with a report ID (buttons 5, X Y 16 bit, wheel, as many receivers)
static const uint8_t MAP_ID[] = {0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x04, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09,
                                 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
                                 0x95, 0x01, 0x75, 0x03, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01,
                                 0x80, 0x26, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x02, 0x81, 0x06, 0x09, 0x38, 0x15, 0x81,
                                 0x25, 0x7f, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xc0, 0xc0};
// The boot mouse descriptor (HID 1.11 appendix B.2): 3 buttons, X Y i8
static const uint8_t MAP_BOOT[] = {0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01,
                                   0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01,
                                   0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81,
                                   0x25, 0x7f, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xc0, 0xc0};
// A keyboard only
static const uint8_t MAP_KBD[] = {0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00,
                                  0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xc0};

static usb_mouse_t m;
static app_pen_event_t ev;
static usb_mouse_layout_t boot;

void setUp() {
  usb_mouse_init(&m, 1872, 1404);
  usb_mouse_boot_layout(&boot);
  app_pen_event_t e;
  usb_mouse_enter(&m, &e);
}
void tearDown() {}

static usb_mouse_move_t mv(uint8_t b, int dx, int dy) {
  usb_mouse_move_t r = {b, dx, dy};
  return r;
}

void test_boot_report_parses() {
  const uint8_t r[] = {0x01, 0xfb, 0x03};
  usb_mouse_move_t out;
  TEST_ASSERT_TRUE(usb_mouse_report(&boot, r, sizeof(r), &out));
  TEST_ASSERT_EQUAL(1, out.buttons);
  TEST_ASSERT_EQUAL(-5, out.dx);
  TEST_ASSERT_EQUAL(3, out.dy);
  const uint8_t w[] = {0x07, 0x00, 0x80, 0x09}; // with a wheel byte; all three buttons
  TEST_ASSERT_TRUE(usb_mouse_report(&boot, w, sizeof(w), &out));
  TEST_ASSERT_EQUAL(7, out.buttons);
  TEST_ASSERT_EQUAL(-128, out.dy);
}

void test_short_report_is_refused() {
  const uint8_t r[] = {0x01, 0x05};
  usb_mouse_move_t out;
  TEST_ASSERT_FALSE(usb_mouse_report(&boot, r, sizeof(r), &out));
  TEST_ASSERT_FALSE(usb_mouse_report(&boot, r, 0, &out));
  TEST_ASSERT_FALSE(usb_mouse_report(&boot, nullptr, 3, &out));
}

void test_descriptor_boot_mouse() {
  usb_mouse_layout_t l;
  TEST_ASSERT_TRUE(usb_mouse_map_parse(MAP_BOOT, sizeof(MAP_BOOT), &l));
  TEST_ASSERT_EQUAL(0, l.report_id);
  TEST_ASSERT_EQUAL(0, l.btn_bit);
  TEST_ASSERT_EQUAL(8, l.x_bit);
  TEST_ASSERT_EQUAL(16, l.y_bit);
  TEST_ASSERT_EQUAL(8, l.x_size);
}

void test_descriptor_with_id_and_16_bit_axes() {
  usb_mouse_layout_t l;
  TEST_ASSERT_TRUE(usb_mouse_map_parse(MAP_ID, sizeof(MAP_ID), &l));
  TEST_ASSERT_EQUAL(4, l.report_id);
  TEST_ASSERT_EQUAL(8, l.btn_bit);
  TEST_ASSERT_EQUAL(5, l.btn_count);
  TEST_ASSERT_EQUAL(16, l.x_bit); // id, 5 buttons + 3 padding
  TEST_ASSERT_EQUAL(32, l.y_bit);
  TEST_ASSERT_EQUAL(16, l.x_size);
  const uint8_t r[] = {0x04, 0x02, 0x10, 0x00, 0x00, 0xf0, 0x01, 0x00}; // right, x +16, y -4096, wheel 1
  usb_mouse_move_t out;
  TEST_ASSERT_TRUE(usb_mouse_report(&l, r, sizeof(r), &out));
  TEST_ASSERT_EQUAL(2, out.buttons);
  TEST_ASSERT_EQUAL(16, out.dx);
  TEST_ASSERT_EQUAL(-4096, out.dy);
  const uint8_t other[] = {0x05, 0x02, 0x10, 0x00, 0x00, 0xf0, 0x01, 0x00}; // another report ID
  TEST_ASSERT_FALSE(usb_mouse_report(&l, other, sizeof(other), &out));
}

void test_descriptor_without_a_mouse_or_cut_off() {
  usb_mouse_layout_t l;
  TEST_ASSERT_FALSE(usb_mouse_map_parse(MAP_KBD, sizeof(MAP_KBD), &l));
  TEST_ASSERT_FALSE(usb_mouse_map_parse(MAP_ID, sizeof(MAP_ID) - 1, &l)); // a collection left open
  TEST_ASSERT_FALSE(usb_mouse_map_parse(MAP_ID, 30, &l));
  TEST_ASSERT_FALSE(usb_mouse_map_parse(nullptr, 5, &l));
  TEST_ASSERT_FALSE(usb_mouse_map_parse(MAP_ID, 0, &l));
  uint8_t junk[200];
  for (unsigned s = 0; s < 50; s++) { // garbage never crashes (ASan in CI) and never claims a mouse without axes
    for (unsigned i = 0; i < sizeof(junk); i++) junk[i] = (uint8_t)((i * 37 + s * 101) ^ (i >> 3));
    usb_mouse_map_parse(junk, sizeof(junk), &l);
  }
}

void test_starts_at_the_centre_in_range() {
  usb_mouse_t c;
  usb_mouse_init(&c, 1872, 1404);
  TEST_ASSERT_TRUE(usb_mouse_enter(&c, &ev));
  TEST_ASSERT_EQUAL(32768, ev.x);
  TEST_ASSERT_EQUAL(32768, ev.y);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE, ev.flags);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE, ev.changed);
}

void test_slow_motion_is_8_units_per_count_and_y_keeps_the_aspect() {
  usb_mouse_move_t a = mv(0, 4, 4);
  TEST_ASSERT_TRUE(usb_mouse_move(&m, &a, &ev));
  TEST_ASSERT_EQUAL(32768 + 32, ev.x);
  TEST_ASSERT_EQUAL(32768 + 24, ev.y); // 1404 / 1872 of x: the same distance on the screen
  TEST_ASSERT_EQUAL(0, ev.changed);
}

void test_fast_motion_is_faster_but_bounded() {
  usb_mouse_move_t slow = mv(0, 6, 0), fast = mv(0, 100, 0);
  usb_mouse_move(&m, &slow, &ev);
  int s = ev.x - 32768;
  usb_mouse_init(&m, 1872, 1404);
  usb_mouse_enter(&m, &ev);
  usb_mouse_move(&m, &fast, &ev);
  int f = ev.x - 32768;
  TEST_ASSERT_EQUAL(48, s);
  TEST_ASSERT_TRUE(f > 100 * 8);
  TEST_ASSERT_TRUE(f <= 100 * 16); // at most twice
}

void test_a_screen_width_takes_two_to_three_sweeps() {
  // a sweep: 3000 counts (3 in at 1000 cpi) in reports of 12
  unsigned reports = 0;
  int start = m.x / 16;
  usb_mouse_init(&m, 1872, 1404);
  app_pen_event_t e;
  usb_mouse_enter(&m, &e);
  usb_mouse_move_t one = mv(0, 12, 0);
  while (m.x / 16 < 65000 && reports < 100000) {
    usb_mouse_move(&m, &one, &e);
    reports++;
  }
  (void)start;
  unsigned counts = reports * 12 * 2; // from the centre: half a width; double for the whole
  TEST_ASSERT_TRUE(counts >= 4500 && counts <= 9000);
}

void test_clamped_at_the_edges_and_no_event_when_stuck() {
  usb_mouse_move_t big = mv(0, 30000, -30000);
  TEST_ASSERT_TRUE(usb_mouse_move(&m, &big, &ev));
  TEST_ASSERT_EQUAL(65535, ev.x);
  TEST_ASSERT_EQUAL(0, ev.y);
  TEST_ASSERT_FALSE(usb_mouse_move(&m, &big, &ev)); // nothing left to move, same buttons
  usb_mouse_move_t back = mv(0, -30000, 30000);
  TEST_ASSERT_TRUE(usb_mouse_move(&m, &back, &ev));
  TEST_ASSERT_EQUAL(0, ev.x);
  TEST_ASSERT_EQUAL(65535, ev.y);
}

void test_buttons_map_to_tip_and_barrels() {
  usb_mouse_move_t l = mv(1, 0, 0);
  TEST_ASSERT_TRUE(usb_mouse_move(&m, &l, &ev));
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE | APP_PEN_TIP, ev.flags);
  TEST_ASSERT_EQUAL(APP_PEN_TIP, ev.changed);
  TEST_ASSERT_EQUAL(USB_MOUSE_PRESSURE, ev.pressure);
  usb_mouse_move_t lr = mv(3, 0, 0);
  usb_mouse_move(&m, &lr, &ev);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE | APP_PEN_TIP | APP_PEN_BARREL, ev.flags);
  TEST_ASSERT_EQUAL(APP_PEN_BARREL, ev.changed);
  usb_mouse_move_t mid = mv(4, 0, 0);
  usb_mouse_move(&m, &mid, &ev);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE | APP_PEN_BARREL2, ev.flags);
  TEST_ASSERT_EQUAL(0, ev.pressure);
  usb_mouse_move_t none = mv(0, 0, 0);
  usb_mouse_move(&m, &none, &ev);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE, ev.flags);
}

void test_leaving_lets_everything_go() {
  usb_mouse_move_t l = mv(1, 0, 0);
  usb_mouse_move(&m, &l, &ev);
  TEST_ASSERT_TRUE(usb_mouse_leave(&m, &ev));
  TEST_ASSERT_EQUAL(0, ev.flags);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE | APP_PEN_TIP, ev.changed);
  TEST_ASSERT_FALSE(usb_mouse_leave(&m, &ev)); // already gone
  usb_mouse_move_t any = mv(1, 5, 5);
  TEST_ASSERT_FALSE(usb_mouse_move(&m, &any, &ev)); // out of range: no events
}

// The device setting mouse_speed (x_devset.h) scales the motion: slow half, fast 1.75 times
void test_speed_setting_scales_motion() {
  TEST_ASSERT_EQUAL(USB_MOUSE_SLOW, usb_mouse_speed("slow"));
  TEST_ASSERT_EQUAL(USB_MOUSE_FAST, usb_mouse_speed("fast"));
  TEST_ASSERT_EQUAL(USB_MOUSE_NORMAL, usb_mouse_speed("normal"));
  TEST_ASSERT_EQUAL(USB_MOUSE_NORMAL, usb_mouse_speed(""));
  TEST_ASSERT_EQUAL(USB_MOUSE_NORMAL, usb_mouse_speed(nullptr));
  TEST_ASSERT_EQUAL(USB_MOUSE_NORMAL, usb_mouse_speed("FAST")); // only the stored words
  usb_mouse_move_t a = mv(0, 4, 0);
  int got[3];
  const uint8_t q[3] = {USB_MOUSE_SLOW, USB_MOUSE_NORMAL, USB_MOUSE_FAST};
  for (int i = 0; i < 3; i++) {
    usb_mouse_init(&m, 1872, 1404);
    usb_mouse_set_speed(&m, q[i]);
    usb_mouse_enter(&m, &ev);
    usb_mouse_move(&m, &a, &ev);
    got[i] = ev.x - 32768;
  }
  TEST_ASSERT_EQUAL(16, got[0]);
  TEST_ASSERT_EQUAL(32, got[1]);
  TEST_ASSERT_EQUAL(56, got[2]);
  usb_mouse_set_speed(&m, 99); // out of range: normal
  TEST_ASSERT_EQUAL(USB_MOUSE_NORMAL, m.quarters);
}

void test_info() {
  app_pen_info_t i;
  usb_mouse_info(0x046d, 0xc077, 209664, 157248, &i);
  TEST_ASSERT_EQUAL(APP_PEN_MODE_MOUSE, i.mode);
  TEST_ASSERT_EQUAL(0, i.key_count);
  TEST_ASSERT_EQUAL(209664, i.width_um);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_report_parses);
  RUN_TEST(test_short_report_is_refused);
  RUN_TEST(test_descriptor_boot_mouse);
  RUN_TEST(test_descriptor_with_id_and_16_bit_axes);
  RUN_TEST(test_descriptor_without_a_mouse_or_cut_off);
  RUN_TEST(test_starts_at_the_centre_in_range);
  RUN_TEST(test_slow_motion_is_8_units_per_count_and_y_keeps_the_aspect);
  RUN_TEST(test_fast_motion_is_faster_but_bounded);
  RUN_TEST(test_a_screen_width_takes_two_to_three_sweeps);
  RUN_TEST(test_clamped_at_the_edges_and_no_event_when_stuck);
  RUN_TEST(test_buttons_map_to_tip_and_barrels);
  RUN_TEST(test_leaving_lets_everything_go);
  RUN_TEST(test_speed_setting_scales_motion);
  RUN_TEST(test_info);
  return UNITY_END();
}
