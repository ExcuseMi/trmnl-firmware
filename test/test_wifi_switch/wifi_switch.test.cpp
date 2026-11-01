#include <app_api.h>
#include <string.h>
#include <unity.h>
#include <wifi_switch.h>

#include <string>

static wsw_list_t l;
static wsw_join_t j;
static char why[64];

void setUp(void) {
  wsw_list_init(&l);
  wsw_list_add(&l, "Home", -60, false, false, false);
  wsw_list_add(&l, "Home5", -50, false, true, false);
  wsw_list_add(&l, "Cafe", -70, true, false, false);
  wsw_list_add(&l, "Office", -65, false, false, true);
}
void tearDown(void) {}

void test_list_is_sorted_and_keeps_the_stronger_of_a_name_and_band() {
  TEST_ASSERT_EQUAL(4, l.n);
  TEST_ASSERT_EQUAL_STRING("Home5", l.it[0].ssid);
  TEST_ASSERT_TRUE(wsw_list_add(&l, "Home", -40, false, false, false)); // stronger: moves up
  TEST_ASSERT_FALSE(wsw_list_add(&l, "Home", -80, false, false, false)); // weaker: dropped
  TEST_ASSERT_EQUAL(4, l.n);
  TEST_ASSERT_EQUAL_STRING("Home", l.it[0].ssid);
  TEST_ASSERT_EQUAL(-40, l.it[0].rssi);
  TEST_ASSERT_TRUE(wsw_list_add(&l, "Home", -55, false, true, false)); // the same name on 5 GHz: its own entry
  TEST_ASSERT_EQUAL(5, l.n);
  TEST_ASSERT_FALSE(wsw_list_add(&l, "", -10, true, false, false));
  TEST_ASSERT_FALSE(wsw_list_add(&l, std::string(33, 'a').c_str(), -10, true, false, false));
  for (int i = 0; i < 30; i++) wsw_list_add(&l, ("n" + std::to_string(i)).c_str(), (int8_t)(-90 + i), true, false, false);
  TEST_ASSERT_EQUAL(WSW_LIST_MAX, l.n); // full: the weakest fall off
  for (int i = 1; i < l.n; i++) TEST_ASSERT_TRUE(l.it[i - 1].rssi >= l.it[i].rssi);
}

void test_check_takes_band_and_security_from_the_scan() {
  TEST_ASSERT_EQUAL(0, wsw_check(&l, "Home5", "password1", &j, why, sizeof(why)));
  TEST_ASSERT_TRUE(j.is5);
  TEST_ASSERT_EQUAL_STRING("password1", j.pass);
  TEST_ASSERT_EQUAL(0, wsw_check(&l, "Cafe", "", &j, why, sizeof(why)));
  TEST_ASSERT_FALSE(j.is5);
  TEST_ASSERT_EQUAL(APP_ENOENT, wsw_check(&l, "Elsewhere", "password1", &j, why, sizeof(why)));
  TEST_ASSERT_EQUAL(APP_EINVAL, wsw_check(&l, "Office", "password1", &j, why, sizeof(why))); // enterprise
}

void test_check_refuses_bad_passwords_without_echoing_them() {
  TEST_ASSERT_EQUAL(APP_EINVAL, wsw_check(&l, "Home", "short7!", &j, why, sizeof(why)));
  TEST_ASSERT_TRUE(strstr(why, "short7") == nullptr);
  TEST_ASSERT_EQUAL(APP_EINVAL, wsw_check(&l, "Home", std::string(65, 'a').c_str(), &j, why, sizeof(why)));
  TEST_ASSERT_EQUAL(APP_EINVAL, wsw_check(&l, "Home", std::string(64, 'z').c_str(), &j, why, sizeof(why)));
  TEST_ASSERT_EQUAL(0, wsw_check(&l, "Home", std::string(64, 'a').c_str(), &j, why, sizeof(why))); // a hex key
  TEST_ASSERT_EQUAL(APP_EINVAL, wsw_check(&l, "Home", "pass\nword1", &j, why, sizeof(why)));
  TEST_ASSERT_EQUAL(APP_EINVAL, wsw_check(&l, "Cafe", "something", &j, why, sizeof(why))); // open: none
}

struct Ops {
  bool join_ok = false, old_ok = true;
  int joins = 0, saves = 0, rejoins = 0;
  std::string seen_pass;
};
static bool o_join(void *c, const wsw_join_t *j) {
  Ops *o = (Ops *)c;
  o->joins++;
  o->seen_pass = j->pass;
  return o->join_ok;
}
static void o_save(void *c, const wsw_join_t *) { ((Ops *)c)->saves++; }
static bool o_old(void *c) { return ((Ops *)c)->rejoins++, ((Ops *)c)->old_ok; }

void test_a_failed_join_keeps_the_old_network() {
  Ops o;
  wsw_ops_t ops = {o_join, o_save, o_old, &o};
  wsw_check(&l, "Home", "password1", &j, why, sizeof(why));
  TEST_ASSERT_EQUAL(WSW_KEPT_OLD, wsw_switch(&j, &ops));
  TEST_ASSERT_EQUAL(0, o.saves); // never saved
  TEST_ASSERT_EQUAL(1, o.rejoins);
  o.old_ok = false;
  wsw_check(&l, "Home", "password1", &j, why, sizeof(why));
  TEST_ASSERT_EQUAL(WSW_NONE, wsw_switch(&j, &ops));
  TEST_ASSERT_EQUAL(0, o.saves);
}

void test_a_good_join_is_saved_and_the_password_wiped() {
  Ops o;
  o.join_ok = true;
  wsw_ops_t ops = {o_join, o_save, o_old, &o};
  wsw_check(&l, "Home5", "password1", &j, why, sizeof(why));
  TEST_ASSERT_EQUAL(WSW_JOINED, wsw_switch(&j, &ops));
  TEST_ASSERT_EQUAL(1, o.saves);
  TEST_ASSERT_EQUAL(0, o.rejoins);
  TEST_ASSERT_EQUAL_STRING("password1", o.seen_pass.c_str());
  for (size_t i = 0; i < sizeof(j.pass); i++) TEST_ASSERT_EQUAL(0, j.pass[i]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_list_is_sorted_and_keeps_the_stronger_of_a_name_and_band);
  RUN_TEST(test_check_takes_band_and_security_from_the_scan);
  RUN_TEST(test_check_refuses_bad_passwords_without_echoing_them);
  RUN_TEST(test_a_failed_join_keeps_the_old_network);
  RUN_TEST(test_a_good_join_is_saved_and_the_password_wiped);
  return UNITY_END();
}
