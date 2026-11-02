#include <unity.h>

#include <string>

#include "../test_webshare/memfs.h"
#include <fs_replace.h>

// Owner's X 2026-11-02: the event log, rewritten with "w" on a full disk, came back empty (the log was lost)

static const std::string OLD(100, 'o');
static const std::string NEW(200, 'n');

void test_a_full_disk_keeps_the_old_file_and_no_temp() {
  MemFs m;
  m.put("evlog.bin", OLD);
  m.capacity = OLD.size() + 50; // room for less than the new bytes
  TEST_ASSERT_TRUE(fs_replace(&m.fs, "evlog.bin", "evlog.tmp", NEW.data(), NEW.size()) < 0);
  TEST_ASSERT_TRUE(m.get("evlog.bin") == OLD);
  TEST_ASSERT_FALSE(m.has("evlog.tmp"));
}

void test_a_short_write_hidden_at_close_keeps_the_old_file() {
  MemFs m;
  m.put("evlog.bin", OLD);
  m.close_loses_last_byte = true;
  TEST_ASSERT_TRUE(fs_replace(&m.fs, "evlog.bin", "evlog.tmp", NEW.data(), NEW.size()) < 0);
  TEST_ASSERT_TRUE(m.get("evlog.bin") == OLD);
  TEST_ASSERT_FALSE(m.has("evlog.tmp"));
}

void test_a_failed_rename_keeps_the_old_file_and_drops_the_temp() {
  MemFs m;
  m.put("evlog.bin", OLD);
  m.fail_rename = true;
  TEST_ASSERT_TRUE(fs_replace(&m.fs, "evlog.bin", "evlog.tmp", NEW.data(), NEW.size()) < 0);
  TEST_ASSERT_TRUE(m.get("evlog.bin") == OLD);
  TEST_ASSERT_FALSE(m.has("evlog.tmp"));
}

void test_room_replaces_the_file() {
  MemFs m;
  m.put("evlog.bin", OLD);
  TEST_ASSERT_EQUAL(0, fs_replace(&m.fs, "evlog.bin", "evlog.tmp", NEW.data(), NEW.size()));
  TEST_ASSERT_TRUE(m.get("evlog.bin") == NEW);
  TEST_ASSERT_FALSE(m.has("evlog.tmp"));
}

void test_a_first_write_creates_the_file() {
  MemFs m;
  TEST_ASSERT_EQUAL(0, fs_replace(&m.fs, "evlog.bin", "evlog.tmp", NEW.data(), NEW.size()));
  TEST_ASSERT_TRUE(m.get("evlog.bin") == NEW);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_full_disk_keeps_the_old_file_and_no_temp);
  RUN_TEST(test_a_short_write_hidden_at_close_keeps_the_old_file);
  RUN_TEST(test_a_failed_rename_keeps_the_old_file_and_drops_the_temp);
  RUN_TEST(test_room_replaces_the_file);
  RUN_TEST(test_a_first_write_creates_the_file);
  return UNITY_END();
}
