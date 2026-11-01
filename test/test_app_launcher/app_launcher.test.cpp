// The Apps menu's pure parts: ELF name and icon (app_meta), the grid (app_grid), most-recently-used order and
// removing an app (app_remove). Set LAUNCHER_PBM_DIR to also write the grid screens as .pbm files to look at.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "../test_app_elf_check/hello_elf.h"
#include "../test_webshare/memfs.h"
#include "icon_elf.h"
#include <app_elf_check.h>
#include <app_grid.h>
#include <app_meta.h>
#include <app_remove.h>

void setUp(void) {}
void tearDown(void) {}

typedef std::vector<uint8_t> bytes_t;

static int read_mem(void *ctx, uint32_t off, void *buf, size_t len) {
  const bytes_t &b = *(const bytes_t *)ctx;
  if ((size_t)off + len > b.size()) return -1;
  memcpy(buf, &b[off], len);
  return 0;
}

static app_meta_t meta_of(const bytes_t &b, const char *fallback = "my_app") {
  app_meta_t m;
  app_meta_read(read_mem, (void *)&b, (uint32_t)b.size(), fallback, &m);
  return m;
}

static void put32(bytes_t &b, size_t off, uint32_t v) { memcpy(&b[off], &v, 4); }
static void put16(bytes_t &b, size_t off, uint16_t v) { memcpy(&b[off], &v, 2); }

// A minimal ELF: header, sections [null, .shstrtab, .trmnl_app_info, .trmnl_app_icon] (each optional), contents after
struct elf_opts {
  const char *name = "Sketch";
  uint32_t magic = APP_INFO_MAGIC;
  bool info = true;
  int icon_size = -1; // -1: no icon section
  uint32_t icon_type = 1;
  uint32_t shoff_extra = 0; // added to e_shoff (pointing past the file)
};

static bytes_t make_elf(const elf_opts &o) {
  std::string names = std::string("\0", 1) + ".shstrtab" + std::string("\0", 1);
  size_t info_name = names.size();
  names += APP_INFO_SECTION + std::string("\0", 1);
  size_t icon_name = names.size();
  names += APP_ICON_SECTION + std::string("\0", 1);
  const int shnum = 4;
  bytes_t b(52, 0);
  memcpy(&b[0], "\x7f" "ELF\x01\x01\x01", 7);
  size_t names_off = b.size();
  b.insert(b.end(), names.begin(), names.end());
  while (b.size() % 4) b.push_back(0);
  size_t info_off = b.size();
  app_info_t info = {};
  info.magic = o.magic;
  info.api_version = 1;
  strncpy(info.name, o.name, sizeof(info.name) - 1);
  b.resize(b.size() + sizeof(info));
  memcpy(&b[info_off], &info, sizeof(info));
  size_t icon_off = b.size();
  if (o.icon_size > 0) {
    b.resize(b.size() + (size_t)o.icon_size);
    for (int i = 0; i < o.icon_size; i++) b[icon_off + (size_t)i] = (uint8_t)(i * 7 + 1);
  }
  size_t sh = b.size();
  b.resize(sh + shnum * 40, 0);
  auto sec = [&](int i, size_t name, uint32_t type, size_t off, size_t size) {
    put32(b, sh + i * 40 + 0, (uint32_t)name);
    put32(b, sh + i * 40 + 4, type);
    put32(b, sh + i * 40 + 16, (uint32_t)off);
    put32(b, sh + i * 40 + 20, (uint32_t)size);
  };
  sec(1, 1, 3, names_off, names.size());
  if (o.info) sec(2, info_name, 1, info_off, sizeof(info));
  if (o.icon_size >= 0) sec(3, icon_name, o.icon_type, icon_off, (size_t)o.icon_size);
  put32(b, 32, (uint32_t)sh + o.shoff_extra);
  put16(b, 46, 40);
  put16(b, 48, shnum);
  put16(b, 50, 1);
  return b;
}

void test_name_and_icon_are_read() {
  elf_opts o;
  o.icon_size = APP_ICON_BYTES;
  app_meta_t m = meta_of(make_elf(o));
  TEST_ASSERT_EQUAL_STRING("Sketch", m.name);
  TEST_ASSERT_TRUE(m.has_icon);
  TEST_ASSERT_EQUAL_UINT8(1, m.icon[0]);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(511 * 7 + 1), m.icon[511]);
}

void test_real_app_has_name_and_no_icon() {
  bytes_t b(hello_elf, hello_elf + sizeof(hello_elf));
  app_meta_t m = meta_of(b, "hello");
  TEST_ASSERT_EQUAL_STRING("Hello", m.name);
  TEST_ASSERT_FALSE(m.has_icon);
}

void test_real_built_app_with_icon() { // an ELF from apps/build.sh with APP_ICON: loader checks pass, icon found
  bytes_t b(icon_elf, icon_elf + sizeof(icon_elf));
  app_info_t info;
  TEST_ASSERT_EQUAL(APP_ELF_OK, app_elf_check(b.data(), b.size(), 8u << 20, &info));
  app_meta_t m = meta_of(b, "iconapp");
  TEST_ASSERT_EQUAL_STRING("Icon Test", m.name);
  TEST_ASSERT_TRUE(m.has_icon);
  TEST_ASSERT_EQUAL_UINT8(0xFF, m.icon[0]);           // white corner
  TEST_ASSERT_EQUAL_UINT8(0x00, m.icon[32 * 8 + 3]);  // black inside the circle
}

void test_bad_icon_sections_are_ignored() {
  for (int size : {0, 1, 511, 513, 4096}) {
    elf_opts o;
    o.icon_size = size;
    app_meta_t m = meta_of(make_elf(o));
    TEST_ASSERT_FALSE_MESSAGE(m.has_icon, "wrong size");
    TEST_ASSERT_EQUAL_STRING("Sketch", m.name); // the name is not hurt
  }
  elf_opts o;
  o.icon_size = APP_ICON_BYTES;
  o.icon_type = 8; // NOBITS: no bytes in the file
  TEST_ASSERT_FALSE(meta_of(make_elf(o)).has_icon);
  // truncated: the icon section lies past the end of the file
  o.icon_type = 1;
  bytes_t b = make_elf(o);
  size_t sh = b.size() - 4 * 40;
  put32(b, sh + 3 * 40 + 16, (uint32_t)b.size() - 100);
  TEST_ASSERT_FALSE(meta_of(b).has_icon);
}

void test_bad_elf_falls_back_to_the_file_name() {
  bytes_t junk(200, 0xAB);
  TEST_ASSERT_EQUAL_STRING("My app", meta_of(junk).name);
  TEST_ASSERT_EQUAL_STRING("My app", meta_of(bytes_t()).name);
  elf_opts o;
  o.magic = 0;
  TEST_ASSERT_EQUAL_STRING("My app", meta_of(make_elf(o)).name);
  o = elf_opts();
  o.info = false;
  TEST_ASSERT_EQUAL_STRING("My app", meta_of(make_elf(o)).name);
  o = elf_opts();
  o.name = "";
  TEST_ASSERT_EQUAL_STRING("My app", meta_of(make_elf(o)).name);
  o = elf_opts();
  o.shoff_extra = 100000; // section table past the end
  TEST_ASSERT_EQUAL_STRING("My app", meta_of(make_elf(o)).name);
  o = elf_opts();
  o.name = "A\tB\x01 ";
  TEST_ASSERT_EQUAL_STRING("AB", meta_of(make_elf(o)).name);
}

void test_letter_for_generic_icon() {
  TEST_ASSERT_EQUAL_CHAR('S', app_meta_letter("sketch"));
  TEST_ASSERT_EQUAL_CHAR('G', app_meta_letter("  - Game Boy"));
  TEST_ASSERT_EQUAL_CHAR('?', app_meta_letter("--"));
  TEST_ASSERT_EQUAL_CHAR('?', app_meta_letter(""));
}

void test_grid_in_four_orientations() {
  struct { int w, h, edge, cols, rows; } cases[] = {
      {1872, 1404, APP_EDGE_BOTTOM, 4, 2}, {1404, 1872, APP_EDGE_LEFT, 2, 4},
      {1872, 1404, APP_EDGE_TOP, 4, 2},    {1404, 1872, APP_EDGE_RIGHT, 2, 4},
  };
  for (auto &c : cases) {
    app_grid_t g;
    TEST_ASSERT_TRUE(app_grid_layout(c.w, c.h, c.edge, &g));
    TEST_ASSERT_EQUAL(c.cols, g.cols);
    TEST_ASSERT_EQUAL(c.rows, g.rows);
    TEST_ASSERT_EQUAL(8, g.per_page);
    for (int s = 0; s < g.per_page; s++) {
      app_rect_t t = app_grid_tile(&g, s);
      TEST_ASSERT_TRUE(t.w >= 300 && t.h >= 200);
      TEST_ASSERT_TRUE(t.x >= 0 && t.y >= 0 && t.x + t.w <= c.w && t.y + t.h <= c.h);
      // clear of the touch bar's strip
      if (c.edge == APP_EDGE_BOTTOM) TEST_ASSERT_TRUE(t.y + t.h <= c.h - APP_TOUCHBAR_DEPTH);
      if (c.edge == APP_EDGE_TOP) TEST_ASSERT_TRUE(t.y >= APP_TOUCHBAR_DEPTH);
      if (c.edge == APP_EDGE_LEFT) TEST_ASSERT_TRUE(t.x >= APP_TOUCHBAR_DEPTH);
      if (c.edge == APP_EDGE_RIGHT) TEST_ASSERT_TRUE(t.x + t.w <= c.w - APP_TOUCHBAR_DEPTH);
      for (int u = s + 1; u < g.per_page; u++) { // no two tiles overlap
        app_rect_t v = app_grid_tile(&g, u);
        TEST_ASSERT_FALSE(t.x < v.x + v.w && v.x < t.x + t.w && t.y < v.y + v.h && v.y < t.y + t.h);
      }
    }
  }
  app_grid_t g;
  TEST_ASSERT_FALSE(app_grid_layout(200, 150, APP_EDGE_BOTTOM, &g)); // too small
  TEST_ASSERT_FALSE(app_grid_layout(1872, 1404, 9, &g));
}

void test_pages_and_moves() {
  app_grid_t g;
  TEST_ASSERT_TRUE(app_grid_layout(1872, 1404, APP_EDGE_BOTTOM, &g));
  TEST_ASSERT_EQUAL(1, app_grid_pages(&g, 0));
  TEST_ASSERT_EQUAL(1, app_grid_pages(&g, 8));
  TEST_ASSERT_EQUAL(2, app_grid_pages(&g, 9));
  TEST_ASSERT_EQUAL(3, app_grid_pages(&g, 17));
  TEST_ASSERT_EQUAL(1, app_grid_move(5, 0, 1));
  TEST_ASSERT_EQUAL(4, app_grid_move(5, 0, -1)); // wraps
  TEST_ASSERT_EQUAL(0, app_grid_move(5, 4, 1));
  TEST_ASSERT_EQUAL(0, app_grid_move(1, 0, 1));
}

void test_mru_order() {
  const char names[4][32] = {"alpha", "beta", "gamma", "delta"};
  int o[4];
  app_mru_order(names, 4, "", o);
  TEST_ASSERT_EQUAL_INT_ARRAY(((int[]){0, 1, 2, 3}), o, 4);
  app_mru_order(names, 4, "gamma,alpha,gone,gamma", o); // not installed and doubles are skipped
  TEST_ASSERT_EQUAL_INT_ARRAY(((int[]){2, 0, 1, 3}), o, 4);
  app_mru_order(names, 4, "alph,alpha2", o); // a prefix is not a match
  TEST_ASSERT_EQUAL_INT_ARRAY(((int[]){0, 1, 2, 3}), o, 4);
}

void test_mru_touch_moves_to_front_and_is_bounded() {
  char mru[APP_MRU_MAX] = "";
  app_mru_touch(mru, sizeof(mru), "a");
  app_mru_touch(mru, sizeof(mru), "b");
  app_mru_touch(mru, sizeof(mru), "c");
  TEST_ASSERT_EQUAL_STRING("c,b,a", mru);
  app_mru_touch(mru, sizeof(mru), "a");
  TEST_ASSERT_EQUAL_STRING("a,c,b", mru);
  char small[12] = "";
  for (const char *n : {"alpha", "beta", "gamma"}) app_mru_touch(small, sizeof(small), n);
  TEST_ASSERT_EQUAL_STRING("gamma,beta", small); // the oldest that does not fit is dropped
  app_mru_touch(small, sizeof(small), "waytoolonganame");
  TEST_ASSERT_EQUAL_STRING("", small);
}

static void install(MemFs &m, const char *name) {
  m.put(std::string("apps/") + name + ".elf", "ELF");
  m.put(std::string("apps/") + name + ".elf.bak", "OLD");
  m.nodes["appdata"] = {true, ""};
  m.nodes[std::string("appdata/") + name] = {true, ""};
  m.nodes[std::string("appdata/") + name + "/saves"] = {true, ""};
  m.put(std::string("appdata/") + name + "/saves/one.sav", "S");
  m.put(std::string("appdata/") + name + "/config.json", "{}");
}

void test_remove_keeps_saved_files() {
  MemFs m;
  install(m, "game");
  install(m, "other");
  TEST_ASSERT_EQUAL(APP_REMOVE_OK, app_remove(&m.fs, "apps", "appdata", "game", "", false, true));
  TEST_ASSERT_FALSE(m.has("apps/game.elf"));
  TEST_ASSERT_FALSE(m.has("apps/game.elf.bak"));
  TEST_ASSERT_TRUE(m.has("appdata/game/saves/one.sav")); // user data stays
  TEST_ASSERT_TRUE(m.has("appdata/game/config.json"));
  TEST_ASSERT_TRUE(m.has("apps/other.elf"));
  TEST_ASSERT_TRUE(m.has("appdata/other/saves/one.sav"));
}

void test_remove_with_files_takes_only_that_apps_folder() {
  MemFs m;
  install(m, "game");
  install(m, "gamer"); // a name that starts the same
  TEST_ASSERT_EQUAL(APP_REMOVE_OK, app_remove(&m.fs, "apps", "appdata", "game", "", true, true));
  TEST_ASSERT_FALSE(m.has("appdata/game"));
  TEST_ASSERT_FALSE(m.has("appdata/game/saves/one.sav"));
  TEST_ASSERT_TRUE(m.has("appdata/gamer/saves/one.sav"));
  TEST_ASSERT_TRUE(m.has("apps/gamer.elf"));
}

void test_remove_needs_confirmation_and_refuses_the_active_app() {
  MemFs m;
  install(m, "game");
  TEST_ASSERT_EQUAL(APP_REMOVE_NOT_CONFIRMED, app_remove(&m.fs, "apps", "appdata", "game", "", false, false));
  TEST_ASSERT_EQUAL(APP_REMOVE_NOT_CONFIRMED, app_remove(&m.fs, "apps", "appdata", "game", "", true, false));
  TEST_ASSERT_EQUAL(APP_REMOVE_ACTIVE, app_remove(&m.fs, "apps", "appdata", "game", "game", true, true));
  TEST_ASSERT_TRUE(m.has("apps/game.elf"));
  TEST_ASSERT_TRUE(m.has("appdata/game/saves/one.sav"));
  TEST_ASSERT_EQUAL(APP_REMOVE_BAD_NAME, app_remove(&m.fs, "apps", "appdata", "../x", "", true, true));
  TEST_ASSERT_EQUAL(APP_REMOVE_BAD_NAME, app_remove(&m.fs, "apps", "appdata", "", "", true, true));
  TEST_ASSERT_EQUAL(APP_REMOVE_NOT_INSTALLED, app_remove(&m.fs, "apps", "appdata", "nothere", "", true, true));
  TEST_ASSERT_TRUE(m.has("appdata/game/saves/one.sav"));
}

static MemFs *failing;
void test_remove_reports_a_file_that_stays() {
  MemFs m;
  install(m, "game");
  failing = &m;
  m.fs.remove = [](void *c, const char *p) -> int {
    if (strcmp(p, "appdata/game/config.json") == 0) return -1; // a read-only file
    return failing->nodes.erase(p) ? 0 : -1;
  };
  TEST_ASSERT_EQUAL(APP_REMOVE_FAILED, app_remove(&m.fs, "apps", "appdata", "game", "", true, true));
  TEST_ASSERT_TRUE(m.has("appdata/game/config.json")); // said so, not hidden
}

// ---- looking at the screens: a pure layout render (tiles, icons, letters as blocks), only when asked ----
static void render(const char *dir, const char *file, int w, int h, int edge, int count) {
  app_grid_t g;
  TEST_ASSERT_TRUE(app_grid_layout(w, h, edge, &g));
  std::vector<uint8_t> px((size_t)w * h, 0); // 1 = black
  auto rect = [&](app_rect_t r, int th) {
    for (int y = r.y; y < r.y + r.h; y++)
      for (int x = r.x; x < r.x + r.w; x++)
        if (x - r.x < th || r.x + r.w - 1 - x < th || y - r.y < th || r.y + r.h - 1 - y < th) px[(size_t)y * w + x] = 1;
  };
  for (int s = 0; s < g.per_page && s <= count; s++) {
    app_rect_t t = app_grid_tile(&g, s);
    rect(t, s == 1 ? 8 : 3);
    for (int y = 0; y < 64 * 2; y++) // an icon, drawn 2x, centered above the middle
      for (int x = 0; x < 64 * 2; x++)
        if (((x / 2 / 8 + y / 2 / 8) & 1) == 0) px[(size_t)(t.y + 30 + y) * w + t.x + t.w / 2 - 64 + x] = 1;
  }
  std::string path = std::string(dir) + "/" + file;
  FILE *f = fopen(path.c_str(), "wb");
  fprintf(f, "P4\n%d %d\n", w, h);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x += 8) {
      uint8_t byte = 0;
      for (int k = 0; k < 8 && x + k < w; k++) byte |= (uint8_t)(px[(size_t)y * w + x + k] << (7 - k));
      fputc(byte, f);
    }
  }
  fclose(f);
}

void test_render_if_asked() {
  const char *dir = getenv("LAUNCHER_PBM_DIR");
  if (!dir) TEST_IGNORE_MESSAGE("LAUNCHER_PBM_DIR not set");
  render(dir, "land-bottom.pbm", 1872, 1404, APP_EDGE_BOTTOM, 7);
  render(dir, "port-left.pbm", 1404, 1872, APP_EDGE_LEFT, 5);
  render(dir, "land-top.pbm", 1872, 1404, APP_EDGE_TOP, 7);
  render(dir, "port-right.pbm", 1404, 1872, APP_EDGE_RIGHT, 7);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_name_and_icon_are_read);
  RUN_TEST(test_real_app_has_name_and_no_icon);
  RUN_TEST(test_real_built_app_with_icon);
  RUN_TEST(test_bad_icon_sections_are_ignored);
  RUN_TEST(test_bad_elf_falls_back_to_the_file_name);
  RUN_TEST(test_letter_for_generic_icon);
  RUN_TEST(test_grid_in_four_orientations);
  RUN_TEST(test_pages_and_moves);
  RUN_TEST(test_mru_order);
  RUN_TEST(test_mru_touch_moves_to_front_and_is_bounded);
  RUN_TEST(test_remove_keeps_saved_files);
  RUN_TEST(test_remove_with_files_takes_only_that_apps_folder);
  RUN_TEST(test_remove_needs_confirmation_and_refuses_the_active_app);
  RUN_TEST(test_remove_reports_a_file_that_stays);
  RUN_TEST(test_render_if_asked);
  return UNITY_END();
}
