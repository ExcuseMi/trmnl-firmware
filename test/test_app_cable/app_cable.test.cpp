#include <app_cable.h>
#include <string.h>
#include <unity.h>

void setUp() { app_cable_busy_set(false); }
void tearDown() {}

void test_plan_not_running_installs_at_once() {
  TEST_ASSERT_EQUAL(APP_CABLE_INSTALL_NOW, app_cable_plan(USB_ROLE_SERIAL, nullptr, "/apps/hello.elf"));
  TEST_ASSERT_EQUAL(APP_CABLE_INSTALL_NOW, app_cable_plan(USB_ROLE_SERIAL, "", "/apps/hello.elf"));
}

void test_plan_same_app_running_restarts_it() {
  TEST_ASSERT_EQUAL(APP_CABLE_REPLACE_AND_RESTART, app_cable_plan(USB_ROLE_SERIAL, "hello", "/apps/hello.elf"));
}

void test_plan_other_app_running_keeps_it() {
  TEST_ASSERT_EQUAL(APP_CABLE_INSTALL_KEEP_RUNNING, app_cable_plan(USB_ROLE_SERIAL, "hello2", "/apps/hello.elf"));
  TEST_ASSERT_EQUAL(APP_CABLE_INSTALL_KEEP_RUNNING, app_cable_plan(USB_ROLE_SERIAL, "hell", "/apps/hello.elf"));
}

void test_plan_host_mode_and_drive_get_an_error() {
  TEST_ASSERT_EQUAL(APP_CABLE_REFUSE_PORT, app_cable_plan(USB_ROLE_HOST, nullptr, "/apps/hello.elf"));
  TEST_ASSERT_EQUAL(APP_CABLE_REFUSE_PORT, app_cable_plan(USB_ROLE_HOST, "hello", "/apps/hello.elf"));
  TEST_ASSERT_EQUAL(APP_CABLE_REFUSE_PORT, app_cable_plan(USB_ROLE_DRIVE, nullptr, "/apps/hello.elf"));
  TEST_ASSERT_NOT_NULL(strstr(app_cable_refusal(USB_ROLE_HOST), "host mode"));
  TEST_ASSERT_NOT_NULL(strstr(app_cable_refusal(USB_ROLE_DRIVE), "drive"));
}

// Owner's X 2026-11-02: a bad header while an update waited for its app to exit removed the waiting file
void test_header_while_an_update_waits_never_touches_its_file() {
  TEST_ASSERT_EQUAL(APP_CABLE_HDR_KEEP_WAITING, app_cable_header_plan(true, APP_UPLOAD_BAD_HEADER));
  TEST_ASSERT_EQUAL(APP_CABLE_HDR_KEEP_WAITING, app_cable_header_plan(true, APP_UPLOAD_NO_SPACE));
  TEST_ASSERT_EQUAL(APP_CABLE_HDR_KEEP_WAITING, app_cable_header_plan(true, APP_UPLOAD_OK));
}

void test_header_with_nothing_waiting() {
  TEST_ASSERT_EQUAL(APP_CABLE_HDR_RECEIVE, app_cable_header_plan(false, APP_UPLOAD_OK));
  TEST_ASSERT_EQUAL(APP_CABLE_HDR_REFUSE, app_cable_header_plan(false, APP_UPLOAD_BAD_HEADER));
}

static const char *feed(app_cable_line_t *l, const char *s) {
  const char *h = nullptr;
  for (; *s; s++)
    if (app_cable_line_feed(l, (unsigned char)*s, &h)) return h;
  return nullptr;
}

void test_header_of_full_length_is_seen_after_noise() {
  app_cable_line_t l;
  app_cable_line_init(&l);
  // longer than the 32 byte line of app_serial.h: the name alone is 24 characters
  const char *h = feed(&l, "boot noise\r\nxx TAPP PUT abcdefghijklmnopqrstuvwx.elf 123456 0a1b2c3d\r\n");
  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_EQUAL_STRING("TAPP PUT abcdefghijklmnopqrstuvwx.elf 123456 0a1b2c3d", h);
}

void test_other_lines_and_overlong_noise_are_not_headers() {
  app_cable_line_t l;
  app_cable_line_init(&l);
  TEST_ASSERT_NULL(feed(&l, "TAPP EVLOG\n"));
  TEST_ASSERT_NULL(feed(&l, "TKEY hello\n"));
  char noise[300];
  memset(noise, 'x', sizeof(noise) - 2);
  noise[sizeof(noise) - 2] = '\n';
  noise[sizeof(noise) - 1] = '\0';
  TEST_ASSERT_NULL(feed(&l, noise));
  TEST_ASSERT_NOT_NULL(feed(&l, "TAPP PUT a.elf 1 0\n")); // still works after the flood
}

void test_busy_flag() {
  TEST_ASSERT_FALSE(app_cable_busy());
  app_cable_busy_set(true);
  TEST_ASSERT_TRUE(app_cable_busy());
}

void test_commands_other_than_put_are_seen() {
  app_cable_line_t l;
  app_cable_line_init(&l);
  const char *cmd = nullptr;
  const char *put;
  for (const char *c = "noise TAPP LS\r\n"; *c; c++) {
    if (app_cable_line_feed(&l, (unsigned char)*c, &put)) TEST_FAIL_MESSAGE("not a header");
    if (app_cable_line_command(&l)) cmd = app_cable_line_command(&l);
  }
  TEST_ASSERT_EQUAL_STRING("TAPP LS", cmd);
  TEST_ASSERT_FALSE(app_cable_line_feed(&l, 'x', &put));
  TEST_ASSERT_NULL(app_cable_line_command(&l));
  for (const char *c = "TAPP PUT a.elf 1 0\n"; *c; c++) app_cable_line_feed(&l, (unsigned char)*c, &put);
  TEST_ASSERT_NULL(app_cable_line_command(&l)); // a header is not a command
}

// Owner's X 2026-11-02: a 1 MB app over usbip took longer than the 60 s hold and the X slept mid-file; between two
// files the busy flag was down and the next header found the X asleep
void test_sleep_waits_for_a_long_upload_and_between_files() {
  app_cable_busy_set(false);
  TEST_ASSERT_FALSE(app_cable_sleep_hold(0, 0)); // no host ever: sleep at once
  app_cable_busy_set(true);
  TEST_ASSERT_TRUE(app_cable_sleep_hold(1000, 1000 + 120000)); // two minutes into one file: still held
  app_cable_rx_note(200000);
  app_cable_busy_set(false); // one file done, the next header not here yet
  TEST_ASSERT_TRUE(app_cable_sleep_hold(1000, 200000 + APP_CABLE_QUIET_MS - 1));
  TEST_ASSERT_FALSE(app_cable_sleep_hold(1000, 200000 + APP_CABLE_QUIET_MS)); // the host went quiet: sleep
  app_cable_busy_set(true);
  TEST_ASSERT_FALSE(app_cable_sleep_hold(1000, 1000 + APP_CABLE_HOLD_MAX_MS)); // never forever
  app_cable_busy_set(false);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_commands_other_than_put_are_seen);
  RUN_TEST(test_sleep_waits_for_a_long_upload_and_between_files);
  RUN_TEST(test_plan_not_running_installs_at_once);
  RUN_TEST(test_plan_same_app_running_restarts_it);
  RUN_TEST(test_plan_other_app_running_keeps_it);
  RUN_TEST(test_plan_host_mode_and_drive_get_an_error);
  RUN_TEST(test_header_of_full_length_is_seen_after_noise);
  RUN_TEST(test_other_lines_and_overlong_noise_are_not_headers);
  RUN_TEST(test_busy_flag);
  RUN_TEST(test_header_while_an_update_waits_never_touches_its_file);
  RUN_TEST(test_header_with_nothing_waiting);
  return UNITY_END();
}
