#include <unity.h>
#include <usb_cursor.h>

static usb_cursor_t c;
static usb_cursor_out_t o;

void setUp() { usb_cursor_init(&c, 1872, 1404); }
void tearDown() {}

static app_pen_event_t pen(uint8_t flags, uint16_t x, uint16_t y) {
  app_pen_event_t e = {};
  e.type = APP_EVENT_PEN;
  e.flags = flags;
  e.x = x;
  e.y = y;
  return e;
}
static usb_mouse_move_t mv(uint8_t b, int dx, int dy) {
  usb_mouse_move_t r = {b, dx, dy};
  return r;
}
static void both_attached() {
  usb_cursor_tablet_attach(&c);
  usb_cursor_mouse_attach(&c, &o);
}
static void pen_at(uint8_t f, uint16_t x, uint16_t y) {
  app_pen_event_t e = pen(f, x, y);
  usb_cursor_pen(&c, &e, &o);
}

void test_pen_in_range_owns_the_cursor_and_mouse_is_ignored() {
  both_attached();
  pen_at(APP_PEN_IN_RANGE, 1000, 2000);
  TEST_ASSERT_EQUAL(USB_CURSOR_PEN, usb_cursor_owner(&c));
  usb_mouse_move_t m = mv(1, 50, 50);
  usb_cursor_mouse_move(&c, &m, &o);
  TEST_ASSERT_EQUAL(0, o.n); // no jump, no click
  pen_at(APP_PEN_IN_RANGE | APP_PEN_TIP, 1100, 2000); // the tip too
  TEST_ASSERT_EQUAL(1, o.n);
  TEST_ASSERT_EQUAL(1100, o.ev[0].x);
}

void test_mouse_starts_at_the_last_pen_position() {
  both_attached();
  pen_at(APP_PEN_IN_RANGE, 1000, 2000);
  pen_at(0, 7, 9); // leaves; the leave frame's own place is not trusted
  TEST_ASSERT_EQUAL(1, o.n);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE, o.ev[0].flags); // the mouse is there: still in range, no gap
  TEST_ASSERT_EQUAL(1000, o.ev[0].x);
  TEST_ASSERT_EQUAL(2000, o.ev[0].y);
  usb_mouse_move_t m = mv(0, 1, 0);
  usb_cursor_mouse_move(&c, &m, &o);
  TEST_ASSERT_EQUAL(1, o.n);
  TEST_ASSERT_EQUAL(1008, o.ev[0].x); // 8 units per count from there
  TEST_ASSERT_EQUAL(2000, o.ev[0].y);
}

void test_mouse_buttons_work_when_the_pen_is_out() {
  both_attached();
  usb_mouse_move_t m = mv(1 | 2 | 4, 0, 0);
  usb_cursor_mouse_move(&c, &m, &o);
  TEST_ASSERT_EQUAL(1, o.n);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE | APP_PEN_TIP | APP_PEN_BARREL | APP_PEN_BARREL2, o.ev[0].flags);
  TEST_ASSERT_EQUAL(APP_PEN_TIP | APP_PEN_BARREL | APP_PEN_BARREL2, o.ev[0].changed);
}

void test_pen_returning_releases_a_held_mouse_button_first() {
  both_attached();
  usb_mouse_move_t m = mv(1, 40, 0);
  usb_cursor_mouse_move(&c, &m, &o);
  uint16_t at = o.ev[0].x;
  pen_at(APP_PEN_IN_RANGE, 5, 6);
  TEST_ASSERT_EQUAL(2, o.n);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE, o.ev[0].flags); // tip up at the mouse's place
  TEST_ASSERT_EQUAL(APP_PEN_TIP, o.ev[0].changed);
  TEST_ASSERT_EQUAL(at, o.ev[0].x);
  TEST_ASSERT_EQUAL(5, o.ev[1].x); // then the pen
  TEST_ASSERT_EQUAL(0, o.ev[1].changed); // the app already saw in range: one stream
}

void test_a_button_held_through_the_pen_stays_ignored_until_released() {
  both_attached();
  pen_at(APP_PEN_IN_RANGE, 100, 100);
  usb_mouse_move_t m = mv(1, 0, 0);
  usb_cursor_mouse_move(&c, &m, &o); // held while the pen is in range
  pen_at(0, 100, 100);
  m = mv(1, 3, 0); // pen gone, button still down: moves, no tip
  usb_cursor_mouse_move(&c, &m, &o);
  TEST_ASSERT_EQUAL(1, o.n);
  TEST_ASSERT_EQUAL(0, o.ev[0].flags & APP_PEN_TIP);
  m = mv(0, 0, 0);
  usb_cursor_mouse_move(&c, &m, &o); // released
  m = mv(1, 0, 0);
  usb_cursor_mouse_move(&c, &m, &o); // pressed again: counts
  TEST_ASSERT_EQUAL(1, o.n);
  TEST_ASSERT_TRUE(o.ev[0].flags & APP_PEN_TIP);
}

void test_stale_out_of_range_frames_do_not_move_the_mouse_cursor() {
  both_attached();
  pen_at(APP_PEN_IN_RANGE, 100, 100);
  pen_at(0, 100, 100);
  usb_mouse_move_t m = mv(0, 20, 0);
  usb_cursor_mouse_move(&c, &m, &o);
  uint16_t x = o.ev[0].x;
  pen_at(0, 100, 100); // the tablet repeats its out of range frame
  TEST_ASSERT_EQUAL(0, o.n);
  m = mv(0, 1, 0);
  usb_cursor_mouse_move(&c, &m, &o);
  TEST_ASSERT_TRUE(o.ev[0].x > x);
}

void test_without_a_mouse_the_pen_leaves_as_before() {
  usb_cursor_tablet_attach(&c);
  pen_at(APP_PEN_IN_RANGE, 10, 20);
  pen_at(0, 10, 20);
  TEST_ASSERT_EQUAL(1, o.n);
  TEST_ASSERT_EQUAL(0, o.ev[0].flags);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE, o.ev[0].changed);
}

void test_unplugging_the_mouse_leaves_the_tablet_working() {
  both_attached();
  usb_cursor_mouse_detach(&c, &o);
  TEST_ASSERT_EQUAL(1, o.n); // the mouse's cursor leaves
  TEST_ASSERT_EQUAL(0, o.ev[0].flags);
  pen_at(APP_PEN_IN_RANGE, 1, 2);
  TEST_ASSERT_EQUAL(1, o.n);
  usb_cursor_mouse_detach(&c, &o); // pen owns it: no event for a mouse the app does not see
  TEST_ASSERT_EQUAL(0, o.n);
}

void test_unplugging_the_tablet_leaves_the_mouse_working_from_the_pen_place() {
  both_attached();
  pen_at(APP_PEN_IN_RANGE | APP_PEN_TIP, 300, 400);
  usb_cursor_tablet_detach(&c, &o);
  TEST_ASSERT_EQUAL(1, o.n);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE, o.ev[0].flags); // tip up, still in range
  TEST_ASSERT_EQUAL(APP_PEN_TIP, o.ev[0].changed);
  TEST_ASSERT_EQUAL(300, o.ev[0].x);
  usb_mouse_move_t m = mv(0, 1, 0);
  usb_cursor_mouse_move(&c, &m, &o);
  TEST_ASSERT_EQUAL(308, o.ev[0].x);
}

void test_unplugging_the_tablet_with_no_mouse_leaves_range() {
  usb_cursor_tablet_attach(&c);
  pen_at(APP_PEN_IN_RANGE, 3, 4);
  usb_cursor_tablet_detach(&c, &o);
  TEST_ASSERT_EQUAL(1, o.n);
  TEST_ASSERT_EQUAL(0, o.ev[0].flags);
}

void test_mouse_alone_hovers_at_the_centre_and_a_late_pen_takes_over() {
  usb_cursor_mouse_attach(&c, &o);
  TEST_ASSERT_EQUAL(1, o.n);
  TEST_ASSERT_EQUAL(32768, o.ev[0].x);
  usb_cursor_tablet_attach(&c); // a tablet alone shows nothing until its pen comes
  usb_cursor_mouse_attach(&c, &o);
  usb_mouse_move_t m = mv(0, 0, 0);
  usb_cursor_mouse_move(&c, &m, &o);
  pen_at(APP_PEN_IN_RANGE, 9, 9);
  TEST_ASSERT_EQUAL(USB_CURSOR_PEN, usb_cursor_owner(&c));
}

void test_mouse_attach_while_the_pen_owns_the_cursor_shows_nothing() {
  usb_cursor_tablet_attach(&c);
  pen_at(APP_PEN_IN_RANGE, 50, 60);
  usb_cursor_mouse_attach(&c, &o);
  TEST_ASSERT_EQUAL(0, o.n);
  pen_at(0, 50, 60);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE, o.ev[0].flags);
}

void test_resync_starts_the_app_afresh() {
  both_attached();
  usb_cursor_resync(&c, &o);
  TEST_ASSERT_EQUAL(1, o.n);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE, o.ev[0].changed);
  pen_at(APP_PEN_IN_RANGE, 1, 1);
  usb_cursor_resync(&c, &o);
  TEST_ASSERT_EQUAL(0, o.n); // the pen owns it
}

void test_key_events_are_not_for_the_cursor() {
  both_attached();
  app_pen_event_t k = pen(0, 0, 0);
  k.type = APP_EVENT_PEN_KEY;
  usb_cursor_pen(&c, &k, &o);
  TEST_ASSERT_EQUAL(0, o.n);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pen_in_range_owns_the_cursor_and_mouse_is_ignored);
  RUN_TEST(test_mouse_starts_at_the_last_pen_position);
  RUN_TEST(test_mouse_buttons_work_when_the_pen_is_out);
  RUN_TEST(test_pen_returning_releases_a_held_mouse_button_first);
  RUN_TEST(test_a_button_held_through_the_pen_stays_ignored_until_released);
  RUN_TEST(test_stale_out_of_range_frames_do_not_move_the_mouse_cursor);
  RUN_TEST(test_without_a_mouse_the_pen_leaves_as_before);
  RUN_TEST(test_unplugging_the_mouse_leaves_the_tablet_working);
  RUN_TEST(test_unplugging_the_tablet_leaves_the_mouse_working_from_the_pen_place);
  RUN_TEST(test_unplugging_the_tablet_with_no_mouse_leaves_range);
  RUN_TEST(test_mouse_alone_hovers_at_the_centre_and_a_late_pen_takes_over);
  RUN_TEST(test_mouse_attach_while_the_pen_owns_the_cursor_shows_nothing);
  RUN_TEST(test_resync_starts_the_app_afresh);
  RUN_TEST(test_key_events_are_not_for_the_cursor);
  return UNITY_END();
}
