#include <app_api.h>
#include <app_path.h>
#include <string.h>
#include <unity.h>

static char out[96];

static bool join(const char *rel) { return app_path_join("/appdata/hello", rel, out, sizeof(out)); }

void test_joins_relative_paths() {
  TEST_ASSERT_TRUE(join("notes.txt"));
  TEST_ASSERT_EQUAL_STRING("/appdata/hello/notes.txt", out);
  TEST_ASSERT_TRUE(join("a/b.txt"));
  TEST_ASSERT_EQUAL_STRING("/appdata/hello/a/b.txt", out);
  TEST_ASSERT_TRUE(join(""));
  TEST_ASSERT_EQUAL_STRING("/appdata/hello", out);
  TEST_ASSERT_TRUE(join(".hidden"));
}

void test_refuses_paths_out_of_the_folder() {
  TEST_ASSERT_FALSE(join(".."));
  TEST_ASSERT_FALSE(join("../other/x"));
  TEST_ASSERT_FALSE(join("a/../../x"));
  TEST_ASSERT_FALSE(join("/etc"));
  TEST_ASSERT_FALSE(join("a\\..\\b"));
  TEST_ASSERT_FALSE(join("a//b"));
  TEST_ASSERT_FALSE(join("./a"));
  TEST_ASSERT_FALSE(join("a/"));
  TEST_ASSERT_FALSE(app_path_join("/appdata/hello", nullptr, out, sizeof(out)));
}

void test_refuses_long_paths() {
  char rel[80];
  memset(rel, 'a', sizeof(rel) - 1);
  rel[sizeof(rel) - 1] = '\0';
  TEST_ASSERT_FALSE(join(rel));
  char small[20];
  TEST_ASSERT_FALSE(app_path_join("/appdata/hello", "notes.txt", small, sizeof(small)));
}

void test_app_name_from_elf_path() {
  char name[32];
  TEST_ASSERT_TRUE(app_path_name("/apps/hello.elf", name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("hello", name);
  TEST_ASSERT_TRUE(app_path_name("/apps/my-app_2.elf", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/apps/.elf", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/apps/hello.bin", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/apps/a/b.elf", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/other/hello.elf", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/apps/../x.elf", name, sizeof(name)));
  TEST_ASSERT_FALSE(app_path_name("/apps/aaaaaaaaaaaaaaaaaaaaaaaaa.elf", name, sizeof(name)));
}

static bool join_app(const char *app, const char *rel) {
  return app_path_join_app("/appdata", app, rel, out, sizeof(out));
}

void test_other_app_file_is_readable_by_path() {
  TEST_ASSERT_TRUE(join_app("print", "documents/a.txt"));
  TEST_ASSERT_EQUAL_STRING("/appdata/print/documents/a.txt", out);
  TEST_ASSERT_TRUE(join_app("my-app_2", ""));
  TEST_ASSERT_EQUAL_STRING("/appdata/my-app_2", out);
}

void test_other_app_path_cannot_escape_appdata() {
  TEST_ASSERT_FALSE(join_app("print", ".."));
  TEST_ASSERT_FALSE(join_app("print", "../writer/x"));
  TEST_ASSERT_FALSE(join_app("print", "a/../../x"));
  TEST_ASSERT_FALSE(join_app("print", "/etc/passwd"));
  TEST_ASSERT_FALSE(join_app("print", "a\\b"));
  TEST_ASSERT_FALSE(join_app("print", nullptr));
}

void test_other_app_name_must_be_a_plain_name() {
  TEST_ASSERT_FALSE(join_app("", "x"));
  TEST_ASSERT_FALSE(join_app("..", "x"));
  TEST_ASSERT_FALSE(join_app(".", "x"));
  TEST_ASSERT_FALSE(join_app("a/b", "x"));
  TEST_ASSERT_FALSE(join_app("/print", "x"));
  TEST_ASSERT_FALSE(join_app("../apps", "x"));
  TEST_ASSERT_FALSE(join_app("pr int", "x"));
  TEST_ASSERT_FALSE(join_app("aaaaaaaaaaaaaaaaaaaaaaaaa", "x")); // 25
  TEST_ASSERT_FALSE(join_app(nullptr, "x"));
  TEST_ASSERT_FALSE(app_path_join_app(nullptr, "print", "x", out, sizeof(out)));
  TEST_ASSERT_FALSE(app_path_join_app("/appdata", "print", "notes.txt", out, 10));
}

void test_write_to_a_read_only_handle_is_refused() {
  TEST_ASSERT_EQUAL_INT(APP_EPERM, app_file_write_check(true));
  TEST_ASSERT_EQUAL_INT(0, app_file_write_check(false));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_joins_relative_paths);
  RUN_TEST(test_refuses_paths_out_of_the_folder);
  RUN_TEST(test_refuses_long_paths);
  RUN_TEST(test_app_name_from_elf_path);
  RUN_TEST(test_other_app_file_is_readable_by_path);
  RUN_TEST(test_other_app_path_cannot_escape_appdata);
  RUN_TEST(test_other_app_name_must_be_a_plain_name);
  RUN_TEST(test_write_to_a_read_only_handle_is_refused);
  return UNITY_END();
}
