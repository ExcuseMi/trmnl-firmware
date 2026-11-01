#pragma once

// The X's page, Wi-Fi and Bluetooth (ws_page_radio.cpp): scan, join (checked against the scan, the password never
// back or in the log), keyboard pairing; included by portal_tests.h

static const char *const FORM = "application/x-www-form-urlencoded";

static void test_radio_wifi_scan_and_join() {
  std::string c = start_portal();
  TEST_ASSERT_EQUAL(401, status(post("/api/wifiscan", "", "", FORM, 1460, false)));
  wsw_list_add(&PF.nets, "Home5", -50, false, true, false);
  wsw_list_add(&PF.nets, "Cafe", -70, true, false, false);
  // Join before a scan: refused, nothing recorded
  TEST_ASSERT_EQUAL(404, status(post("/api/wifijoin", "s=Home5&p=password1", c)));
  std::string r = post("/api/wifiscan", "", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE_MESSAGE(body(r).find("\"ssid\":\"Home5\",\"rssi\":-50,\"open\":false,\"band\":\"5\"") != std::string::npos,
                           body(r).c_str());
  TEST_ASSERT_EQUAL(400, status(post("/api/wifijoin", "s=Home5&p=short", c)));
  TEST_ASSERT_EQUAL(404, status(post("/api/wifijoin", "s=Other&p=password1", c)));
  TEST_ASSERT_EQUAL(0, PF.joins);
  r = post("/api/wifijoin", "s=Home5&p=s3cret-pass", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_EQUAL(1, PF.joins);
  TEST_ASSERT_TRUE(PF.joined.is5); // the band from the scan
  TEST_ASSERT_EQUAL_STRING("s3cret-pass", PF.joined.pass);
  TEST_ASSERT_TRUE(body(r).find("s3cret") == std::string::npos);
  TEST_ASSERT_TRUE_MESSAGE(PF.log.find("s3cret") == std::string::npos, PF.log.c_str()); // never logged
  TEST_ASSERT_TRUE(PF.log.find("Home5") != std::string::npos);
  PF.join_rc = APP_EBUSY;
  TEST_ASSERT_EQUAL(500, status(post("/api/wifijoin", "s=Cafe", c)));
  PF.scan_rc = APP_EBUSY;
  TEST_ASSERT_EQUAL(409, status(post("/api/wifiscan", "", c)));
}

static void test_radio_ble_pairing() {
  std::string c = start_portal();
  TEST_ASSERT_EQUAL(401, status(post("/api/blepair", "do=start", "", FORM, 1460, false)));
  TEST_ASSERT_EQUAL(200, status(post("/api/blepair", "do=start", c)));
  TEST_ASSERT_EQUAL(WS_BLE_START, PF.ble_last_do);
  PF.ble_info.state = WS_BLE_LIST;
  PF.ble_info.n = 2;
  snprintf(PF.ble_info.found[0].name, 24, "K380");
  PF.ble_info.found[0].rssi = -60;
  snprintf(PF.ble_info.found[1].name, 24, "POP Keys");
  std::string r = get("/api/blepair", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE_MESSAGE(body(r).find("\"state\":\"list\"") != std::string::npos, body(r).c_str());
  TEST_ASSERT_TRUE(body(r).find("\"name\":\"POP Keys\"") != std::string::npos);
  TEST_ASSERT_TRUE(body(r).find("passkey") == std::string::npos);
  TEST_ASSERT_EQUAL(400, status(post("/api/blepair", "do=pick&i=x", c)));
  TEST_ASSERT_EQUAL(400, status(post("/api/blepair", "do=pick&i=12", c)));
  TEST_ASSERT_EQUAL(400, status(post("/api/blepair", "do=dance", c)));
  TEST_ASSERT_EQUAL(200, status(post("/api/blepair", "do=pick&i=1", c)));
  TEST_ASSERT_EQUAL(1, PF.ble_last_i);
  PF.ble_info.state = WS_BLE_PASSKEY;
  PF.ble_info.passkey = 4521;
  TEST_ASSERT_TRUE_MESSAGE(body(get("/api/blepair", c)).find("\"passkey\":\"004521\"") != std::string::npos,
                           body(get("/api/blepair", c)).c_str());
  PF.ble_info.state = WS_BLE_PAIRED;
  snprintf(PF.ble_info.paired, 24, "POP Keys");
  TEST_ASSERT_TRUE(body(get("/api/blepair", c)).find("\"paired\":\"POP Keys\"") != std::string::npos);
  PF.ble_rc = APP_EBUSY; // ESP-NOW or an app holds the radio
  r = post("/api/blepair", "do=start", c);
  TEST_ASSERT_EQUAL(409, status(r));
  TEST_ASSERT_TRUE(body(r).find("ESP-NOW") != std::string::npos);
  PF.ble_rc = APP_EINVAL;
  TEST_ASSERT_EQUAL(404, status(post("/api/blepair", "do=pick&i=5", c)));
  PF.ble_rc = 0;
  TEST_ASSERT_EQUAL(200, status(post("/api/blepair", "do=stop", c)));
  TEST_ASSERT_EQUAL(WS_BLE_STOP, PF.ble_last_do);
  // the last route of the profile answers: the route table did not cut it off
  TEST_ASSERT_EQUAL(200, status(get("/api/blepair", c)));
}

static void run_radio_tests() {
  RUN_TEST(test_radio_wifi_scan_and_join);
  RUN_TEST(test_radio_ble_pairing);
}
