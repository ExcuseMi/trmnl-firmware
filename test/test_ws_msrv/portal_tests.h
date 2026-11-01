#pragma once

// The X's page (the apps profile with ws_env_t.portal): other apps' settings forms, device settings, printers, the
// Bluetooth keyboard, USB devices and this X, through the modem server with a fake device
#include <unity.h>

#include <app_decl_store.h>

#include "harness.h"

struct PortalFake {
  std::map<std::string, std::string> decl_files; // app -> its declaration file's text
  std::map<std::string, std::string> app_settings; // "app/key" -> value
  std::map<std::string, int> changed; // app -> values changed
  std::map<std::string, std::string> dev;
  nb_list_t found = {};
  int find_rc = 0, tests = 0;
  std::string ble = "K380";
  bool forgot = false;
  bool dev_set_fails = false;
  // Wi-Fi and Bluetooth (radio_tests.h)
  wsw_list_t nets = {};
  int scan_rc = 0, join_rc = 0, joins = 0;
  wsw_join_t joined = {};
  int ble_rc = 0, ble_last_do = -1, ble_last_i = -2;
  ws_ble_info_t ble_info = {};
  std::string log;
};
static PortalFake PF;

static bool p_decl(const char *app, asf_form_t *out) {
  if (!PF.decl_files.count(app)) return false;
  static asf_form_t scratch;
  const std::string &t = PF.decl_files[app];
  return ads_decode(t.data(), t.size(), out, &scratch) == 0;
}
static bool p_meta(const char *app, app_meta_t *m) {
  if (!PF.decl_files.count(app) && strcmp(app, "plain")) return false;
  memset(m, 0, sizeof(*m));
  snprintf(m->name, sizeof(m->name), "%s app", app);
  m->has_icon = !strcmp(app, "weather");
  if (m->has_icon) m->icon[0] = 0xa5;
  return true;
}
static int p_get(const char *app, const char *k, char *buf, size_t cap) {
  std::string id = std::string(app) + "/" + k;
  if (!PF.app_settings.count(id)) return -1;
  std::string v = PF.app_settings[id];
  size_t n = v.size() < cap ? v.size() : cap;
  memcpy(buf, v.data(), n);
  return (int)n;
}
static int p_set(const char *app, const char *k, const char *v, size_t n) {
  return (PF.app_settings[std::string(app) + "/" + k] = std::string(v, n)), 0;
}
static int p_remove(const char *app, const char *k) { return PF.app_settings.erase(std::string(app) + "/" + k), 0; }
static void p_changed(const char *app, int n) { PF.changed[app] += n; }
static int p_dev_get(const char *k, char *buf, size_t cap) {
  if (!PF.dev.count(k)) return 0;
  std::string v = PF.dev[k];
  size_t n = v.size() < cap ? v.size() : cap;
  memcpy(buf, v.data(), n);
  return (int)n;
}
static int p_dev_set(const char *k, const char *v, size_t n) {
  if (PF.dev_set_fails) return -1;
  if (n) PF.dev[k] = std::string(v, n);
  else PF.dev.erase(k);
  return 0;
}
static int p_find(nb_list_t *out) { return PF.find_rc ? PF.find_rc : ((*out = PF.found), PF.found.n); }
static int p_test(void) { return PF.tests++, 0; }
static bool p_ble(char *name, size_t cap) { return snprintf(name, cap, "%s", PF.ble.c_str()), !PF.ble.empty(); }
static void p_forget(void) { PF.forgot = true, PF.ble.clear(); }
static void p_usb(usb_seen_t *l) {
  usb_seen_init(l);
  usb_seen_add(l, 0x05e3, 0x0610, 9, nullptr);
  usb_seen_add(l, 0x256c, 0x006d, 0, "pen tablet");
}
static void p_x(ws_x_info_t *x) {
  snprintf(x->name, sizeof(x->name), "TRMNL-X-BJRLAT");
  snprintf(x->version, sizeof(x->version), "1.7.3");
  snprintf(x->wifi, sizeof(x->wifi), "Home");
}

static int p_scan(wsw_list_t *out) { return PF.scan_rc ? PF.scan_rc : ((*out = PF.nets), PF.nets.n); }
static int p_join(const wsw_join_t *j) { return PF.join_rc ? PF.join_rc : ((PF.joined = *j), PF.joins++, 0); }
static int p_ble_pair(ws_ble_do_t w, int i) { return PF.ble_rc ? PF.ble_rc : ((PF.ble_last_do = w), (PF.ble_last_i = i), 0); }
static void p_ble_info(ws_ble_info_t *b) { *b = PF.ble_info; }
static void p_log(const char *fmt, ...) {
  char b[256];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof(b), fmt, a);
  va_end(a);
  PF.log += std::string(b) + "\n";
}

static const ws_portal_env_t PORTAL = {p_meta,    p_decl, p_get,  p_set,     p_remove, p_changed, p_dev_get,
                                       p_dev_set, p_find, p_test, p_ble,     p_forget, p_usb,     p_x,
                                       p_scan,    p_join, p_ble_pair, p_ble_info};
static ws_env_t ENV_P;

static std::string start_portal() {
  start(WS_PROFILE_INSTALL);
  PF = PortalFake();
  ENV_P = ENV;
  ENV_P.portal = &PORTAL;
  ENV_P.log = p_log;
  ws_page_start(WS_PROFILE_INSTALL, false, &ENV_P, &APP_FS->fs, &ROOT_FS->fs);
  WP.running = true;
  return login();
}

static void declare_weather() {
  static const app_setting_decl_t d[] = {{"city", "City", nullptr, 1, 40, APP_SETTING_TEXT},
                                         {"token", "API token", nullptr, 0, 64, APP_SETTING_SECRET},
                                         {"units", "Units", "metric|imperial", 0, 0, APP_SETTING_CHOICE}};
  asf_form_t f = {};
  TEST_ASSERT_EQUAL(0, asf_declare(&f, d, 3));
  static char buf[ADS_FILE_MAX];
  PF.decl_files["weather"] = std::string(buf, ads_encode(&f, buf, sizeof(buf)));
}

static void test_portal_needs_login_and_the_portal_env() {
  start(WS_PROFILE_INSTALL); // without ws_env_t.portal: the apps list only
  std::string c = login();
  TEST_ASSERT_EQUAL(404, status(get("/api/devices", c)));
  c = start_portal();
  TEST_ASSERT_EQUAL(401, status(get("/api/devices")));
  TEST_ASSERT_EQUAL(401, status(get("/api/appsettings?app=weather")));
  TEST_ASSERT_EQUAL(401, status(post("/api/device", "k=pen_area&v=keep", c, "application/x-www-form-urlencoded", 1460, false)));
  TEST_ASSERT_EQUAL(0u, PF.dev.count("pen_area"));
  std::string r = get("/api/x", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE_MESSAGE(body(r).find("\"name\":\"TRMNL-X-BJRLAT\"") != std::string::npos, body(r).c_str());
  TEST_ASSERT_TRUE(body(r).find("\"wifi\":\"Home\"") != std::string::npos);
}

static void test_portal_other_apps_settings_checked() {
  std::string c = start_portal();
  declare_weather();
  PF.app_settings["weather/token"] = "s3cret";
  std::string r = get("/api/appsettings?app=weather", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE_MESSAGE(body(r).find("\"k\":\"units\"") != std::string::npos, body(r).c_str());
  TEST_ASSERT_TRUE(body(r).find("s3cret") == std::string::npos); // a secret never goes back
  TEST_ASSERT_EQUAL(404, status(get("/api/appsettings?app=sketch", c))); // never declared: start it once
  TEST_ASSERT_EQUAL(400, status(get("/api/appsettings?app=..%2Fx", c)));
  r = post("/api/appsettings?app=weather", "v_city=Gent&v_units=kelvin", c);
  TEST_ASSERT_EQUAL(400, status(r)); // one bad value writes nothing
  TEST_ASSERT_EQUAL(0u, PF.app_settings.count("weather/city"));
  r = post("/api/appsettings?app=weather", "v_city=Gent&v_units=imperial", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_EQUAL_STRING("Gent", PF.app_settings["weather/city"].c_str());
  TEST_ASSERT_EQUAL(2, PF.changed["weather"]); // the app hears it when it runs
  TEST_ASSERT_EQUAL(0u, F.settings.count("city")); // not the running app's store
  PF.decl_files["weather"].replace(0, 12, "x-settings 9"); // other firmware wrote it
  TEST_ASSERT_EQUAL(404, status(get("/api/appsettings?app=weather", c)));
  r = get("/api/appinfo?app=weather", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE_MESSAGE(body(r).find("\"icon\":\"a5000000") != std::string::npos, body(r).c_str());
  TEST_ASSERT_TRUE(body(get("/api/appinfo?app=plain", c)).find("\"icon\":\"\"") != std::string::npos);
  TEST_ASSERT_EQUAL(404, status(get("/api/appinfo?app=none", c)));
}

static void test_portal_device_settings_and_printer() {
  std::string c = start_portal();
  std::string r = get("/api/devices", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE_MESSAGE(body(r).find("\"k\":\"pen_area\"") != std::string::npos, body(r).c_str());
  TEST_ASSERT_TRUE(body(r).find("\"v\":\"fill\"") != std::string::npos); // the default
  TEST_ASSERT_TRUE(body(r).find("\"kind\":\"pen tablet\"") != std::string::npos);
  TEST_ASSERT_TRUE(body(r).find("\"ble\":\"K380\"") != std::string::npos);
  TEST_ASSERT_EQUAL(400, status(post("/api/device", "k=pen_area&v=stretch", c)));
  TEST_ASSERT_EQUAL(404, status(post("/api/device", "k=wifi&v=x", c)));
  TEST_ASSERT_EQUAL(400, status(post("/api/device", "k=printer&v=host%3D1.2.3.4%0Aport%3D0", c)));
  TEST_ASSERT_EQUAL(0u, PF.dev.size());
  TEST_ASSERT_EQUAL(200, status(post("/api/device", "k=pen_area&v=keep", c)));
  TEST_ASSERT_EQUAL_STRING("keep", PF.dev["pen_area"].c_str());
  TEST_ASSERT_EQUAL(409, status(post("/api/printertest", "", c))); // no default yet
  TEST_ASSERT_EQUAL(200, status(post("/api/device", "k=printer&v=name%3DBrother%0Ahost%3D192.168.1.50%0Aport%3D631", c)));
  TEST_ASSERT_EQUAL(200, status(post("/api/printertest", "", c)));
  TEST_ASSERT_EQUAL(1, PF.tests);
  PF.dev_set_fails = true; // flash full
  TEST_ASSERT_EQUAL(500, status(post("/api/device", "k=mouse_speed&v=fast", c)));
}

static void test_portal_printers_and_bluetooth() {
  std::string c = start_portal();
  nb_list_add(&PF.found, "Brother MFC", "192.168.1.50", 631, "ipp/print");
  std::string r = post("/api/printers", "", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE_MESSAGE(body(r).find("\"host\":\"192.168.1.50\"") != std::string::npos, body(r).c_str());
  PF.find_rc = APP_ENONET;
  TEST_ASSERT_EQUAL(503, status(post("/api/printers", "", c)));
  PF.find_rc = APP_EBUSY; // the modem path: the phone holds every link
  TEST_ASSERT_EQUAL(409, status(post("/api/printers", "", c)));
  TEST_ASSERT_EQUAL(200, status(post("/api/bleforget", "", c)));
  TEST_ASSERT_TRUE(PF.forgot);
  TEST_ASSERT_TRUE(body(get("/api/devices", c)).find("\"ble\":null") != std::string::npos);
}

#include "radio_tests.h"

void run_portal_tests() {
  RUN_TEST(test_portal_needs_login_and_the_portal_env);
  RUN_TEST(test_portal_other_apps_settings_checked);
  RUN_TEST(test_portal_device_settings_and_printer);
  RUN_TEST(test_portal_printers_and_bluetooth);
  run_radio_tests();
}
