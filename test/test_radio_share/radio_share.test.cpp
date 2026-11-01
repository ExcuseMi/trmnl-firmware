#include <radio_share.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

// Wi-Fi joins while the X reconnects a sleeping keyboard: no BLE scan may hold the radio more than 60 % of the time
void test_connecting_a_keyboard_leaves_wifi_its_share() {
  TEST_ASSERT_LESS_OR_EQUAL(60, ble_share_duty(ble_share_connect()));
  TEST_ASSERT_TRUE(ble_share_connect().window < ble_share_connect().itvl);
}

// App start: the bonded keyboard is looked for with the whole radio for 5 s, then shared again
void test_first_5_s_after_start_full_duty_then_shared() {
  TEST_ASSERT_EQUAL(100, ble_share_duty(ble_share_connect_at(0)));
  TEST_ASSERT_EQUAL(100, ble_share_duty(ble_share_connect_at(4999)));
  TEST_ASSERT_LESS_OR_EQUAL(60, ble_share_duty(ble_share_connect_at(5000)));
  TEST_ASSERT_LESS_OR_EQUAL(60, ble_share_duty(ble_share_connect_at(600000)));
}

void test_listing_keyboards_leaves_wifi_its_share() {
  TEST_ASSERT_LESS_OR_EQUAL(60, ble_share_duty(ble_share_list()));
}

void test_nimble_default_connect_scan_is_all_ble() {
  ble_share_scan_t dflt = {0x10, 0x10}; // ble_gap_conn_params_dflt
  TEST_ASSERT_EQUAL(100, ble_share_duty(dflt));
}

void test_join_runs_with_modem_sleep_while_ble_is_on() {
  TEST_ASSERT_TRUE(net_up_modem_sleep(true));
  TEST_ASSERT_FALSE(net_up_modem_sleep(false)); // TRMNL's own connect: sleep off
}

void test_steps() {
  TEST_ASSERT_EQUAL(NET_UP_ALREADY, net_up_step({true, false, false}));
  TEST_ASSERT_EQUAL(NET_UP_NO_SAVED, net_up_step({false, false, false}));
  TEST_ASSERT_EQUAL(NET_UP_5GHZ, net_up_step({false, true, true}));
  TEST_ASSERT_EQUAL(NET_UP_JOIN, net_up_step({false, true, false}));
}

void test_log_line_up() {
  char b[160];
  net_up_report_t r = {NET_UP_JOIN, true, 3200, 3, -61, true, true, 70000};
  net_up_log_line(b, sizeof(b), &r);
  TEST_ASSERT_EQUAL_STRING("Net: up in 3200 ms (joined, rssi -61, ble on, keyboard connected)", b);
}

void test_log_line_failed_names_reason_status_rssi_ble_and_ram() {
  char b[200];
  net_up_report_t r = {NET_UP_JOIN, false, 61000, 6, 0, true, false, 41000};
  net_up_log_line(b, sizeof(b), &r);
  TEST_ASSERT_EQUAL_STRING(
    "Net: failed: join failed after 61000 ms, wl_status 6, rssi 0, ble on, no keyboard, internal RAM 41000 free", b);
  r.step = NET_UP_5GHZ;
  r.ble_on = false;
  net_up_log_line(b, sizeof(b), &r);
  TEST_ASSERT_NOT_NULL(strstr(b, "5 GHz"));
  TEST_ASSERT_NOT_NULL(strstr(b, "ble off"));
}

void test_log_line_truncates_safely() {
  char b[16];
  net_up_report_t r = {NET_UP_NO_SAVED, false, 1, 0, 0, false, false, 0};
  TEST_ASSERT_EQUAL(15, net_up_log_line(b, sizeof(b), &r));
  TEST_ASSERT_EQUAL(15, strlen(b));
}

// The owner's X (2026-11-01): "Xena" saved as 5 GHz, TRMNL's refresh worked over the modem, apps saw "No Wi-Fi network"
void test_saved_5ghz_with_modem_goes_through_the_modem() {
  TEST_ASSERT_EQUAL(NET_UP_MODEM, net_up_step({false, true, true, true}));
  TEST_ASSERT_EQUAL(NET_UP_5GHZ, net_up_step({false, true, true, false})); // modem missing, or the Wi-Fi page
  TEST_ASSERT_EQUAL(NET_UP_JOIN, net_up_step({false, true, false, true})); // 2.4 GHz: the S3 path, modem or not
  TEST_ASSERT_EQUAL(NET_UP_ALREADY, net_up_step({true, true, false, true}));
  TEST_ASSERT_EQUAL(NET_UP_NO_SAVED, net_up_step({false, false, false, true}));
}

void test_log_line_names_the_modem_path() {
  char b[160];
  net_up_report_t r = {NET_UP_MODEM, true, 4100, 0, -58, false, false, 90000, true};
  net_up_log_line(b, sizeof(b), &r);
  TEST_ASSERT_EQUAL_STRING("Net: up in 4100 ms (modem 5 GHz, joined, rssi -58, ble off)", b);
  r.modem_joined = false;
  net_up_log_line(b, sizeof(b), &r);
  TEST_ASSERT_NOT_NULL(strstr(b, "modem 5 GHz, already"));
  r.up = false;
  net_up_log_line(b, sizeof(b), &r);
  TEST_ASSERT_NOT_NULL(strstr(b, "Net: failed: modem could not join"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_connecting_a_keyboard_leaves_wifi_its_share);
  RUN_TEST(test_first_5_s_after_start_full_duty_then_shared);
  RUN_TEST(test_listing_keyboards_leaves_wifi_its_share);
  RUN_TEST(test_nimble_default_connect_scan_is_all_ble);
  RUN_TEST(test_join_runs_with_modem_sleep_while_ble_is_on);
  RUN_TEST(test_steps);
  RUN_TEST(test_log_line_up);
  RUN_TEST(test_log_line_failed_names_reason_status_rssi_ble_and_ram);
  RUN_TEST(test_log_line_truncates_safely);
  RUN_TEST(test_saved_5ghz_with_modem_goes_through_the_modem);
  RUN_TEST(test_log_line_names_the_modem_path);
  return UNITY_END();
}
