#include <string.h>
#include <unity.h>

#include <string>

#include "../test_webshare/memfs.h"
#include <app_space.h>
#include <app_upload.h>

// The owner's X (2026-11-02): "TAPP ERR no space" for a 130 KB app after apps were removed; a stale upload temp
// file and .bak copies (the old FPV .bak alone 1 MB) held the room

static const std::string KB(size_t n) { return std::string(n * 1024, 'x'); }

static void setup(MemFs &m, uint64_t capacity) {
  m.capacity = capacity;
  m.nodes["apps"] = MemFs::Node{true, ""};
  m.nodes["appdata"] = MemFs::Node{true, ""};
}

void test_a_stale_upload_temp_counts_as_free() {
  MemFs m;
  setup(m, 1024 * 1024);
  m.put("apps/.upload.tmp", KB(900));
  TEST_ASSERT_EQUAL_UINT64(1024 * 1024, app_space_free(&m.fs));
}

void test_short_space_drops_bak_copies_but_not_apps() {
  MemFs m;
  setup(m, 1300 * 1024);
  m.put("apps/fpv.elf", KB(100));
  m.put("apps/fpv.elf.bak", KB(1000));
  m.put("apps/sketch.elf", KB(130));
  int dropped = -1;
  uint64_t free_bytes = app_space_make_room(&m.fs, 130 * 1024, &dropped);
  TEST_ASSERT_EQUAL(1, dropped);
  TEST_ASSERT_FALSE(m.has("apps/fpv.elf.bak"));
  TEST_ASSERT_TRUE(m.has("apps/fpv.elf"));
  TEST_ASSERT_TRUE(m.has("apps/sketch.elf")); // the app being replaced stays until its new version is complete
  TEST_ASSERT_TRUE(free_bytes >= 130 * 1024 + APP_UPLOAD_MARGIN);
}

void test_enough_space_keeps_the_bak() {
  MemFs m;
  setup(m, 8 * 1024 * 1024);
  m.put("apps/fpv.elf.bak", KB(1000));
  int dropped = -1;
  app_space_make_room(&m.fs, 130 * 1024, &dropped);
  TEST_ASSERT_EQUAL(0, dropped);
  TEST_ASSERT_TRUE(m.has("apps/fpv.elf.bak"));
}

void test_still_short_says_free_and_needed() {
  MemFs m;
  setup(m, 200 * 1024);
  m.put("appdata/gameboy/rom.gb", KB(150));
  uint64_t free_bytes = app_space_make_room(&m.fs, 130 * 1024, nullptr);
  char why[64];
  app_space_no_room_text(free_bytes, 130 * 1024, why, sizeof(why));
  TEST_ASSERT_EQUAL_STRING("50 KB free, 146 KB needed", why);
  TEST_ASSERT_TRUE(m.has("appdata/gameboy/rom.gb")); // saves are never touched
}

void test_free_space_removes_baks_and_the_temp_only() {
  MemFs m;
  setup(m, 8 * 1024 * 1024);
  m.put("apps/a.elf", KB(10));
  m.put("apps/a.elf.bak", KB(20));
  m.put("apps/b.elf.bak", KB(30));
  m.put("apps/.upload.tmp", KB(5));
  m.put("appdata/a/save.bin", KB(7));
  int removed = 0;
  TEST_ASSERT_EQUAL_UINT64(55 * 1024, app_space_clean(&m.fs, &removed));
  TEST_ASSERT_EQUAL(3, removed);
  TEST_ASSERT_TRUE(m.has("apps/a.elf"));
  TEST_ASSERT_TRUE(m.has("appdata/a/save.bin"));
  TEST_ASSERT_EQUAL(0, (int)app_space_clean(&m.fs, &removed)); // nothing left: no failure
}

void test_per_app_sizes_include_bak_and_data() {
  MemFs m;
  setup(m, 8 * 1024 * 1024);
  m.put("apps/gameboy.elf", KB(400));
  m.put("apps/gameboy.elf.bak", KB(390));
  m.put("apps/notes.txt", KB(1));
  m.nodes["appdata/gameboy"] = MemFs::Node{true, ""};
  m.nodes["appdata/gameboy/roms"] = MemFs::Node{true, ""};
  m.put("appdata/gameboy/roms/a.gb", KB(1024));
  m.put("appdata/gameboy/a.sav", KB(32));
  app_space_app_t apps[4];
  TEST_ASSERT_EQUAL(1, app_space_apps(&m.fs, apps, 4));
  TEST_ASSERT_EQUAL_STRING("gameboy", apps[0].name);
  TEST_ASSERT_EQUAL_UINT32(400 * 1024, apps[0].elf);
  TEST_ASSERT_EQUAL_UINT32(390 * 1024, apps[0].bak);
  TEST_ASSERT_EQUAL_UINT64(1056 * 1024, apps[0].data);
  char line[160];
  app_space_apps_line(&m.fs, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING_LEN("apps: gameboy 400 KB +bak 390 KB +data 1.0 MB; ", line, 47);
}

static bool count_cb(const char *path, uint64_t, bool, void *ctx) {
  std::string *s = (std::string *)ctx;
  *s += path;
  *s += ";";
  return true;
}

void test_walk_lists_every_file_at_any_depth() {
  MemFs m;
  setup(m, 8 * 1024 * 1024);
  m.nodes["appdata/x"] = MemFs::Node{true, ""};
  m.put("appdata/x/y", "1");
  m.put("evlog.bin", "22");
  std::string seen;
  app_space_walk(&m.fs, "", count_cb, &seen);
  TEST_ASSERT_TRUE(seen.find("appdata/x/y;") != std::string::npos);
  TEST_ASSERT_TRUE(seen.find("evlog.bin;") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT64(3, app_space_dir_bytes(&m.fs, ""));
}

void test_size_text() {
  char t[16];
  app_space_size_text(512, t, sizeof(t));
  TEST_ASSERT_EQUAL_STRING("512 B", t);
  app_space_size_text(130 * 1024, t, sizeof(t));
  TEST_ASSERT_EQUAL_STRING("130 KB", t);
  app_space_size_text(9900 * 1024, t, sizeof(t));
  TEST_ASSERT_EQUAL_STRING("9.7 MB", t);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_stale_upload_temp_counts_as_free);
  RUN_TEST(test_short_space_drops_bak_copies_but_not_apps);
  RUN_TEST(test_enough_space_keeps_the_bak);
  RUN_TEST(test_still_short_says_free_and_needed);
  RUN_TEST(test_free_space_removes_baks_and_the_temp_only);
  RUN_TEST(test_per_app_sizes_include_bak_and_data);
  RUN_TEST(test_walk_lists_every_file_at_any_depth);
  RUN_TEST(test_size_text);
  return UNITY_END();
}
