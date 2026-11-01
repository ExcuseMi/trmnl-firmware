#include <string.h>
#include <unity.h>
#include <usb_pen.h>

// Reports from the owner's XP-Pen Deco 01 V2 (scratchpad pen-fullmode.txt, hidraw2, full mode, 2026-11-01)
static const uint8_t HOVER[] = {0x02, 0xa0, 0xb8, 0x02, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t TIP[] = {0x02, 0xa1, 0x9b, 0x02, 0x73, 0x04, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x00};
static const uint8_t TILTED[] = {0x02, 0xa0, 0xb8, 0x56, 0x7d, 0x2e, 0x00, 0x00, 0xfa, 0xea, 0x00, 0x00};
static const uint8_t LEAVE[] = {0x02, 0xc0, 0xb8, 0x56, 0x7d, 0x2e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t KEYS_UP[] = {0x02, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
// Standard mode (interface 1, report 07): no raw line was captured; built from the measured layout (memory note):
// tip, lower button, in range, x 32767, y 0, pressure 8191, tilt 5 -3
static const uint8_t STD[] = {0x07, 0x23, 0xff, 0x7f, 0x00, 0x00, 0xff, 0x1f, 0x05, 0xfd};

static usb_pen_t pen;
static app_pen_event_t ev;

void setUp() { usb_pen_init(&pen, USB_PEN_VID_XPPEN, USB_PEN_PID_DECO01V2); }
void tearDown() {}

static int full(const uint8_t *d, size_t n) { return usb_pen_report(&pen, USB_PEN_IFACE_FULL, d, n, &ev); }

void test_roles_never_give_a_tablet_to_the_keyboard() {
  TEST_ASSERT_EQUAL(USB_PEN_IFACE_FULL, usb_pen_iface_role(0x28bd, 0x0905, 2));
  TEST_ASSERT_EQUAL(USB_PEN_IFACE_STANDARD, usb_pen_iface_role(0x28bd, 0x0905, 1));
  TEST_ASSERT_EQUAL(USB_PEN_IFACE_IGNORE, usb_pen_iface_role(0x28bd, 0x0905, 0)); // its keyboard interface
  TEST_ASSERT_EQUAL(USB_PEN_IFACE_STANDARD, usb_pen_iface_role(0x28bd, 0x1234, 1)); // another XP-Pen: no full mode
  TEST_ASSERT_EQUAL(USB_PEN_IFACE_IGNORE, usb_pen_iface_role(0x28bd, 0x1234, 2));
  TEST_ASSERT_EQUAL(USB_PEN_IFACE_NONE, usb_pen_iface_role(0x046d, 0x0905, 2)); // not a tablet
}

void test_full_mode_command_is_the_measured_one() {
  const uint8_t want[] = {0x02, 0xb0, 0x04, 0, 0, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_MEMORY(want, usb_pen_full_mode_cmd, sizeof(want));
}

void test_hover_then_tip_down_then_leave() {
  TEST_ASSERT_EQUAL(1, full(HOVER, sizeof(HOVER)));
  TEST_ASSERT_EQUAL(APP_EVENT_PEN, ev.type);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE, ev.flags);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE, ev.changed);
  TEST_ASSERT_EQUAL(usb_pen_scale(0x02b8, USB_PEN_DECO_MAX_X), ev.x);
  TEST_ASSERT_EQUAL(usb_pen_scale(0x0110, USB_PEN_DECO_MAX_Y), ev.y);
  TEST_ASSERT_EQUAL(0, ev.pressure);
  TEST_ASSERT_EQUAL(APP_PEN_MODE_FULL, pen.mode);

  TEST_ASSERT_EQUAL(1, full(HOVER, sizeof(HOVER)));
  TEST_ASSERT_EQUAL(0, ev.changed); // a move

  TEST_ASSERT_EQUAL(1, full(TIP, sizeof(TIP)));
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE | APP_PEN_TIP, ev.flags);
  TEST_ASSERT_EQUAL(APP_PEN_TIP, ev.changed);
  TEST_ASSERT_EQUAL(usb_pen_scale(0x03aa, USB_PEN_DECO_MAX_P), ev.pressure);
  TEST_ASSERT_TRUE(ev.pressure > 0);

  TEST_ASSERT_EQUAL(1, full(LEAVE, sizeof(LEAVE)));
  TEST_ASSERT_EQUAL(0, ev.flags);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE | APP_PEN_TIP, ev.changed);
  TEST_ASSERT_EQUAL(0, ev.pressure);
  TEST_ASSERT_EQUAL_HEX16(0x56b8, pen.raw_max_x); // the largest raw x seen
}

void test_tilt_is_signed_degrees() {
  TEST_ASSERT_EQUAL(1, full(TILTED, sizeof(TILTED)));
  TEST_ASSERT_EQUAL(-6, ev.tilt_x);
  TEST_ASSERT_EQUAL(-22, ev.tilt_y);
}

void test_pen_buttons_in_full_mode_use_the_standard_bits() { // unproven on the tablet: not in the capture
  uint8_t r[12];
  memcpy(r, TIP, sizeof(r));
  r[1] = 0xa1 | 0x02;
  TEST_ASSERT_EQUAL(1, full(r, sizeof(r)));
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE | APP_PEN_TIP | APP_PEN_BARREL, ev.flags);
  r[1] = 0xa0 | 0x04;
  TEST_ASSERT_EQUAL(1, full(r, sizeof(r)));
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE | APP_PEN_BARREL2, ev.flags);
  TEST_ASSERT_EQUAL(APP_PEN_TIP | APP_PEN_BARREL | APP_PEN_BARREL2, ev.changed);
}

void test_each_tablet_button_down_and_up() {
  for (int b = 0; b < 8; b++) {
    uint8_t r[12];
    memcpy(r, KEYS_UP, sizeof(r));
    r[2] = (uint8_t)(1u << b); // 02 f0 01 .. 02 f0 80, as captured
    TEST_ASSERT_EQUAL(1, full(r, sizeof(r)));
    TEST_ASSERT_EQUAL(APP_EVENT_PEN_KEY, ev.type);
    TEST_ASSERT_EQUAL(1u << b, ev.keys);
    TEST_ASSERT_EQUAL(1u << b, ev.changed);
    TEST_ASSERT_EQUAL(1, full(KEYS_UP, sizeof(KEYS_UP)));
    TEST_ASSERT_EQUAL(0, ev.keys);
    TEST_ASSERT_EQUAL(1u << b, ev.changed);
  }
  TEST_ASSERT_EQUAL(0, full(KEYS_UP, sizeof(KEYS_UP))); // nothing changed: no event
}

void test_pen_events_carry_the_buttons_held() {
  const uint8_t two[] = {0x02, 0xf0, 0x02, 0};
  TEST_ASSERT_EQUAL(1, full(two, sizeof(two)));
  TEST_ASSERT_EQUAL(1, full(HOVER, sizeof(HOVER)));
  TEST_ASSERT_EQUAL(0x02, ev.keys);
}

void test_standard_report_07() {
  usb_pen_init(&pen, USB_PEN_VID_XPPEN, USB_PEN_PID_DECO01V2);
  TEST_ASSERT_EQUAL(1, usb_pen_report(&pen, USB_PEN_IFACE_STANDARD, STD, sizeof(STD), &ev));
  TEST_ASSERT_EQUAL(APP_PEN_MODE_STANDARD, pen.mode);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE | APP_PEN_TIP | APP_PEN_BARREL, ev.flags);
  TEST_ASSERT_EQUAL(65535, ev.x);
  TEST_ASSERT_EQUAL(0, ev.y);
  TEST_ASSERT_EQUAL(65535, ev.pressure);
  TEST_ASSERT_EQUAL(5, ev.tilt_x);
  TEST_ASSERT_EQUAL(-3, ev.tilt_y);
  uint8_t out[10];
  memcpy(out, STD, sizeof(out));
  out[1] = 0x00; // out of range
  TEST_ASSERT_EQUAL(1, usb_pen_report(&pen, USB_PEN_IFACE_STANDARD, out, sizeof(out), &ev));
  TEST_ASSERT_EQUAL(0, ev.flags);
  app_pen_info_t info;
  usb_pen_info(&pen, &info);
  TEST_ASSERT_EQUAL(0, info.key_count);
}

void test_standard_reports_ignored_once_full_mode_runs() {
  TEST_ASSERT_EQUAL(1, full(HOVER, sizeof(HOVER)));
  TEST_ASSERT_EQUAL(0, usb_pen_report(&pen, USB_PEN_IFACE_STANDARD, STD, sizeof(STD), &ev));
  TEST_ASSERT_EQUAL(APP_PEN_MODE_FULL, pen.mode);
  TEST_ASSERT_EQUAL(1u, pen.pen_frames); // one motion, one event
}

// The X, 2026-11-01: after the switch the tablet answered 02 b0 04.. and sent button frames on interface 2, but the
// pen never drew ("raw max x 4": the answer was taken as a pen frame, then every report 07 dropped as "full mode runs")
static const uint8_t ANSWER[] = {0x02, 0xb0, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t KEY1[] = {0x02, 0xf0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void test_the_switch_answer_is_not_a_pen_frame() {
  TEST_ASSERT_EQUAL(0, full(ANSWER, sizeof(ANSWER)));
  TEST_ASSERT_EQUAL(0, pen.raw_max_x);
  TEST_ASSERT_EQUAL(0u, pen.pen_frames);
  TEST_ASSERT_FALSE(pen.full_pen);
}

void test_pen_on_interface_1_while_buttons_come_on_2() {
  TEST_ASSERT_EQUAL(0, full(ANSWER, sizeof(ANSWER)));
  TEST_ASSERT_EQUAL(1, full(KEY1, sizeof(KEY1)));
  TEST_ASSERT_EQUAL(APP_EVENT_PEN_KEY, ev.type);
  TEST_ASSERT_EQUAL(1, full(KEYS_UP, sizeof(KEYS_UP)));
  TEST_ASSERT_EQUAL(0u, pen.pen_frames); // button frames are not counted as pen frames
  TEST_ASSERT_EQUAL(0, pen.raw_max_x);
  TEST_ASSERT_EQUAL(1, usb_pen_report(&pen, USB_PEN_IFACE_STANDARD, STD, sizeof(STD), &ev));
  TEST_ASSERT_EQUAL(APP_EVENT_PEN, ev.type);
  TEST_ASSERT_EQUAL(APP_PEN_IN_RANGE | APP_PEN_TIP | APP_PEN_BARREL, ev.flags);
  TEST_ASSERT_EQUAL(65535, ev.x); // the standard maxima
  TEST_ASSERT_EQUAL(APP_PEN_MODE_FULL, pen.mode); // the buttons still count
  TEST_ASSERT_EQUAL(1u, pen.pen_frames);
  TEST_ASSERT_EQUAL_HEX16(0x7fff, pen.raw_max_x);
  // full-mode pen frames start: from then on the standard ones are dropped, never one motion twice
  TEST_ASSERT_EQUAL(1, full(HOVER, sizeof(HOVER)));
  TEST_ASSERT_EQUAL(0, usb_pen_report(&pen, USB_PEN_IFACE_STANDARD, STD, sizeof(STD), &ev));
  TEST_ASSERT_EQUAL(2u, pen.pen_frames);
}

void test_full_pen_frames_taken_from_interface_1_buttons_not() {
  TEST_ASSERT_EQUAL(1, usb_pen_report(&pen, USB_PEN_IFACE_STANDARD, HOVER, sizeof(HOVER), &ev));
  TEST_ASSERT_EQUAL(usb_pen_scale(0x02b8, USB_PEN_DECO_MAX_X), ev.x);
  TEST_ASSERT_EQUAL(0, usb_pen_report(&pen, USB_PEN_IFACE_STANDARD, KEY1, sizeof(KEY1), &ev));
  TEST_ASSERT_EQUAL(0, pen.keys);
  usb_pen_init(&pen, USB_PEN_VID_XPPEN, 0x1234); // another XP-Pen: report 02 is not known there
  TEST_ASSERT_EQUAL(0, usb_pen_report(&pen, USB_PEN_IFACE_STANDARD, HOVER, sizeof(HOVER), &ev));
}

void test_bad_reports_give_nothing() {
  TEST_ASSERT_EQUAL(0, full(HOVER, 9)); // short pen frame
  TEST_ASSERT_EQUAL(0, full(KEYS_UP, 2)); // short button frame
  TEST_ASSERT_EQUAL(0, full(HOVER, 0));
  TEST_ASSERT_EQUAL(0, usb_pen_report(&pen, USB_PEN_IFACE_FULL, nullptr, 12, &ev));
  TEST_ASSERT_EQUAL(0, usb_pen_report(&pen, USB_PEN_IFACE_FULL, HOVER, sizeof(HOVER), nullptr));
  uint8_t r[12];
  memcpy(r, HOVER, sizeof(r));
  r[0] = 0x07; // wrong id on the full interface
  TEST_ASSERT_EQUAL(0, full(r, sizeof(r)));
  r[0] = 0x02;
  r[1] = 0x1f; // garbage: no pen bit
  TEST_ASSERT_EQUAL(0, full(r, sizeof(r)));
  TEST_ASSERT_EQUAL(0, usb_pen_report(&pen, USB_PEN_IFACE_STANDARD, KEYS_UP, sizeof(KEYS_UP), &ev)); // keys on 1
  TEST_ASSERT_EQUAL(0, usb_pen_report(&pen, USB_PEN_IFACE_STANDARD, STD, 9, &ev)); // short 07
  TEST_ASSERT_EQUAL(0, usb_pen_report(&pen, USB_PEN_IFACE_IGNORE, HOVER, sizeof(HOVER), &ev));
  TEST_ASSERT_EQUAL(0, usb_pen_report(&pen, USB_PEN_IFACE_NONE, STD, sizeof(STD), &ev));
  TEST_ASSERT_EQUAL(APP_PEN_MODE_NONE, pen.mode);
}

void test_scaling_ends() {
  TEST_ASSERT_EQUAL(0, usb_pen_scale(0, 50800));
  TEST_ASSERT_EQUAL(65535, usb_pen_scale(50800, 50800));
  TEST_ASSERT_EQUAL(65535, usb_pen_scale(65535, 50800)); // past the assumed maximum: clamped
  TEST_ASSERT_EQUAL(32767, usb_pen_scale(16383, 32766)); // the middle
  TEST_ASSERT_EQUAL(65532, usb_pen_scale(32766, 32767)); // one below the end
  TEST_ASSERT_EQUAL(0, usb_pen_scale(5, 0));
  uint8_t r[12] = {0x02, 0xa0, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0}; // raw 65535: over the maxima
  TEST_ASSERT_EQUAL(1, full(r, sizeof(r)));
  TEST_ASSERT_EQUAL(65535, ev.x);
  TEST_ASSERT_EQUAL(65535, ev.y);
}

void test_info() {
  app_pen_info_t info;
  usb_pen_info(&pen, &info);
  TEST_ASSERT_EQUAL_HEX16(0x28bd, info.vid);
  TEST_ASSERT_EQUAL(APP_PEN_MODE_NONE, info.mode);
  TEST_ASSERT_EQUAL(USB_PEN_DECO_WIDTH_UM, info.width_um);
  full(HOVER, sizeof(HOVER));
  usb_pen_info(&pen, &info);
  TEST_ASSERT_EQUAL(8, info.key_count);
  usb_pen_t none;
  usb_pen_init(&none, 0, 0);
  usb_pen_info(&none, &info);
  TEST_ASSERT_EQUAL(0, info.vid);
  TEST_ASSERT_EQUAL(0, info.width_um);
}

// ---- the queue ----

static app_pen_event_t mk(uint8_t type, uint8_t changed, uint16_t x) {
  app_pen_event_t e = {};
  e.type = type;
  e.changed = changed;
  e.x = x;
  return e;
}

void test_moves_merge_into_the_latest() {
  usb_pen_queue_t q;
  usb_pen_queue_init(&q);
  for (uint16_t i = 1; i <= 200; i++) { // a second of hover the app did not read
    app_pen_event_t e = mk(APP_EVENT_PEN, 0, i);
    usb_pen_queue_push(&q, &e);
  }
  TEST_ASSERT_EQUAL(1, q.count);
  app_pen_event_t got;
  TEST_ASSERT_TRUE(usb_pen_queue_take(&q, &got));
  TEST_ASSERT_EQUAL(200, got.x);
  TEST_ASSERT_FALSE(usb_pen_queue_take(&q, &got));
  TEST_ASSERT_EQUAL(0, q.dropped);
}

void test_changes_keep_their_order_and_are_never_merged() {
  usb_pen_queue_t q;
  usb_pen_queue_init(&q);
  app_pen_event_t seq[] = {mk(APP_EVENT_PEN, 0, 1), mk(APP_EVENT_PEN, APP_PEN_TIP, 2), mk(APP_EVENT_PEN, 0, 3),
                           mk(APP_EVENT_PEN, 0, 4), mk(APP_EVENT_PEN_KEY, 1, 5), mk(APP_EVENT_PEN_KEY, 1, 6),
                           mk(APP_EVENT_PEN, APP_PEN_TIP, 7)};
  for (auto &e : seq) usb_pen_queue_push(&q, &e);
  const uint16_t want[] = {1, 2, 4, 5, 6, 7};
  app_pen_event_t got;
  for (uint16_t w : want) {
    TEST_ASSERT_TRUE(usb_pen_queue_take(&q, &got));
    TEST_ASSERT_EQUAL(w, got.x);
  }
  TEST_ASSERT_FALSE(usb_pen_queue_take(&q, &got));
}

void test_full_queue_drops_a_move_never_a_change() {
  usb_pen_queue_t q;
  usb_pen_queue_init(&q);
  for (uint16_t i = 0; i < USB_PEN_QUEUE; i++) { // move, change, move, change ...
    app_pen_event_t e = mk(APP_EVENT_PEN, (i & 1) ? APP_PEN_TIP : 0, i);
    usb_pen_queue_push(&q, &e);
  }
  TEST_ASSERT_EQUAL(USB_PEN_QUEUE, q.count);
  app_pen_event_t c = mk(APP_EVENT_PEN_KEY, 1, 100);
  usb_pen_queue_push(&q, &c); // room made by the oldest move (x 0)
  TEST_ASSERT_EQUAL(1, q.dropped);
  app_pen_event_t m = mk(APP_EVENT_PEN, 0, 101);
  usb_pen_queue_push(&q, &m); // full, the end is a change: this move goes
  TEST_ASSERT_EQUAL(2, q.dropped);
  app_pen_event_t got;
  TEST_ASSERT_TRUE(usb_pen_queue_take(&q, &got));
  TEST_ASSERT_EQUAL(1, got.x);
  uint16_t last = 0;
  int changes = 1; // x 1 above
  while (usb_pen_queue_take(&q, &got)) {
    last = got.x;
    if (got.changed) changes++;
  }
  TEST_ASSERT_EQUAL(100, last);
  TEST_ASSERT_EQUAL(USB_PEN_QUEUE / 2 + 1, changes);
}

void test_only_changes_unread_drops_and_counts() {
  usb_pen_queue_t q;
  usb_pen_queue_init(&q);
  for (uint16_t i = 0; i < USB_PEN_QUEUE + 3; i++) {
    app_pen_event_t e = mk(APP_EVENT_PEN_KEY, 1, i);
    usb_pen_queue_push(&q, &e);
  }
  TEST_ASSERT_EQUAL(USB_PEN_QUEUE, q.count);
  TEST_ASSERT_EQUAL(3, q.dropped);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_roles_never_give_a_tablet_to_the_keyboard);
  RUN_TEST(test_full_mode_command_is_the_measured_one);
  RUN_TEST(test_hover_then_tip_down_then_leave);
  RUN_TEST(test_tilt_is_signed_degrees);
  RUN_TEST(test_pen_buttons_in_full_mode_use_the_standard_bits);
  RUN_TEST(test_each_tablet_button_down_and_up);
  RUN_TEST(test_pen_events_carry_the_buttons_held);
  RUN_TEST(test_standard_report_07);
  RUN_TEST(test_standard_reports_ignored_once_full_mode_runs);
  RUN_TEST(test_the_switch_answer_is_not_a_pen_frame);
  RUN_TEST(test_pen_on_interface_1_while_buttons_come_on_2);
  RUN_TEST(test_full_pen_frames_taken_from_interface_1_buttons_not);
  RUN_TEST(test_bad_reports_give_nothing);
  RUN_TEST(test_scaling_ends);
  RUN_TEST(test_info);
  RUN_TEST(test_moves_merge_into_the_latest);
  RUN_TEST(test_changes_keep_their_order_and_are_never_merged);
  RUN_TEST(test_full_queue_drops_a_move_never_a_change);
  RUN_TEST(test_only_changes_unread_drops_and_counts);
  return UNITY_END();
}
