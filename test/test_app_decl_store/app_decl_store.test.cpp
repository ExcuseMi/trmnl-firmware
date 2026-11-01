// The settings declaration file of each app (app_decl_store.h)
#include <app_decl_store.h>
#include <string.h>
#include <unity.h>

#include <string>

static asf_form_t A, B, S;

static void declare_three() {
  static const app_setting_decl_t d[] = {
      {"city", "City", nullptr, 1, 40, APP_SETTING_TEXT},
      {"token", "API token", nullptr, 0, 64, APP_SETTING_SECRET},
      {"units", "Units", "metric|imperial", 0, 0, APP_SETTING_CHOICE},
      {"days", "Days shown", nullptr, 1, 7, APP_SETTING_NUMBER},
  };
  TEST_ASSERT_EQUAL(0, asf_declare(&A, d, 4));
}

static std::string encode(const asf_form_t *f) {
  static char buf[ADS_FILE_MAX];
  size_t n = ads_encode(f, buf, sizeof(buf));
  return std::string(buf, n);
}

void test_write_read_back() {
  declare_three();
  std::string t = encode(&A);
  TEST_ASSERT_TRUE(t.rfind("x-settings 1\n", 0) == 0);
  TEST_ASSERT_EQUAL(0, ads_decode(t.data(), t.size(), &B, &S));
  TEST_ASSERT_EQUAL(4, B.n);
  TEST_ASSERT_EQUAL_MEMORY(&A, &B, sizeof(A)); // the same form, byte for byte
  TEST_ASSERT_EQUAL_STRING("metric|imperial", B.f[2].choices);
  TEST_ASSERT_EQUAL(7, B.f[3].max);
}

void test_empty_form_and_small_buffer() {
  asf_form_t none = {};
  std::string t = encode(&none);
  TEST_ASSERT_EQUAL(0, ads_decode(t.data(), t.size(), &B, &S));
  TEST_ASSERT_EQUAL(0, B.n);
  declare_three();
  char small[40];
  TEST_ASSERT_EQUAL(0, (int)ads_encode(&A, small, sizeof(small))); // never a cut file
}

void test_bad_files_are_refused() {
  declare_three();
  std::string t = encode(&A);
  const std::string bad[] = {
      "",
      "x-settings\n",
      "y-settings 1\n",
      t.substr(0, t.size() - 1), // the last line cut
      t.substr(0, t.size() - 10),
      "x-settings 1\ncity\t0\t1\t40\tCity\n", // a column missing
      "x-settings 1\ncity\t0\t1\t40\tCity\t\textra\n",
      "x-settings 1\ncity\tz\t1\t40\tCity\t\n", // not a number
      "x-settings 1\nci ty\t0\t1\t40\tCity\t\n", // asf_declare: a bad key
      "x-settings 1\ncity\t0\t1\t400\tCity\t\n", // over setting_set's limit
      "x-settings 1\ncity\t9\t1\t40\tCity\t\n", // an unknown kind
      "x-settings 1\nu\t3\t0\t0\tUnits\ta||b\n", // an empty choice
      "x-settings 1\ncity\t0\t1\t40\tCity\t\ncity\t0\t1\t40\tCity\t\n", // the key twice
      "x-settings 1\ncity\t0\t1\t99999999999\tCity\t\n",
  };
  for (const std::string &b : bad) {
    memset(&B, 0x5a, sizeof(B));
    TEST_ASSERT_EQUAL_MESSAGE(APP_EINVAL, ads_decode(b.data(), b.size(), &B, &S), b.c_str());
    TEST_ASSERT_EQUAL(0, B.n); // nothing half kept
  }
  std::string many = "x-settings 1\n";
  for (int i = 0; i < ASF_FIELDS_MAX + 1; i++) many += "k" + std::to_string(i) + "\t0\t1\t4\tL\t\n";
  TEST_ASSERT_EQUAL(APP_EINVAL, ads_decode(many.data(), many.size(), &B, &S));
  TEST_ASSERT_EQUAL(APP_EINVAL, ads_decode(nullptr, 3, &B, &S));
}

void test_other_format_version() {
  declare_three();
  std::string t = encode(&A);
  t.replace(0, 12, "x-settings 2");
  TEST_ASSERT_EQUAL(APP_ENOTSUP, ads_decode(t.data(), t.size(), &B, &S)); // the app declares again
  TEST_ASSERT_EQUAL(0, B.n);
}

void test_redeclared_form_replaces_the_file() {
  declare_three();
  std::string old_text = encode(&A);
  static const app_setting_decl_t d2[] = {{"city", "Town", nullptr, 1, 40, APP_SETTING_TEXT}};
  TEST_ASSERT_EQUAL(0, asf_declare(&A, d2, 1)); // the updated app declares a different form
  std::string t = encode(&A);
  TEST_ASSERT_TRUE(t != old_text); // the firmware writes only when the text differs
  TEST_ASSERT_EQUAL(0, ads_decode(t.data(), t.size(), &B, &S));
  TEST_ASSERT_EQUAL(1, B.n);
  TEST_ASSERT_EQUAL_STRING("Town", B.f[0].label);
}

void test_paths() {
  char p[64];
  TEST_ASSERT_TRUE(ads_path("trmnl-print", p, sizeof(p)));
  TEST_ASSERT_EQUAL_STRING("/xset/decl/trmnl-print.txt", p);
  TEST_ASSERT_FALSE(ads_path("../x", p, sizeof(p)));
  TEST_ASSERT_FALSE(ads_path("a/b", p, sizeof(p)));
  TEST_ASSERT_FALSE(ads_path("", p, sizeof(p)));
  TEST_ASSERT_FALSE(ads_path("abcdefghijklmnopqrstuvwxy", p, sizeof(p))); // 25
  TEST_ASSERT_FALSE(ads_path(nullptr, p, sizeof(p)));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_write_read_back);
  RUN_TEST(test_empty_form_and_small_buffer);
  RUN_TEST(test_bad_files_are_refused);
  RUN_TEST(test_other_format_version);
  RUN_TEST(test_redeclared_form_replaces_the_file);
  RUN_TEST(test_paths);
  return UNITY_END();
}
