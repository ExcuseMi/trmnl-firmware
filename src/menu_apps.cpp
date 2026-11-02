#ifdef BOARD_TRMNL_X

#include "menu_apps.h"

#include <app_grid.h>
#include <LittleFS.h>
#include <app_path.h>
#include <app_remove.h>
#include <app_space.h>
#include <app_resume.h>
#include <globals.h>
#include <touchbar_layout.h>
#include <epd_driver.h>
#include <hints.h>
#include <trmnl_log.h>

#include <stdio.h>
#include <string.h>

#include "fonts.h"
#include "webshare.h"
#include "app_storage_x.h"

#define TILE_BORDER     3
#define TILE_SELECTED   10 // total border of the selected tile

static int bar_edge(void) {
  // the touch bar's edge in the screen's current orientation (the hint pills use the same)
  return touchbar_orient_of_rotation(bbep.getRotation());
}

static void tile_border(const app_rect_t &t, int thickness, uint8_t color) {
  bbep.fillRect(t.x, t.y, t.w, thickness, color);
  bbep.fillRect(t.x, t.y + t.h - thickness, t.w, thickness, color);
  bbep.fillRect(t.x, t.y, thickness, t.h, color);
  bbep.fillRect(t.x + t.w - thickness, t.y, thickness, t.h, color);
}

static void draw_centered(const char *text, int cx, int baseline, int max_w) {
  char fit[40];
  snprintf(fit, sizeof(fit), "%s", text);
  BB_RECT r;
  bbep.setCursor(0, baseline);
  bbep.getStringBox(fit, &r);
  for (size_t n = strlen(fit); r.w > max_w && n > 2; n--) { // too wide: cut with ".."
    fit[n - 1] = '\0';
    if (n > 3) memcpy(fit + n - 3, "..", 3);
    bbep.getStringBox(fit, &r);
  }
  bbep.drawString(fit, cx - r.w / 2, baseline);
}

// The 64x64 icon drawn `scale` times as large at (x, y); 1 bits are white, runs of black are one rectangle
static void draw_icon(const uint8_t *bits, int x, int y, int scale) {
  for (int row = 0; row < APP_ICON_SIZE; row++) {
    int run = -1;
    for (int col = 0; col <= APP_ICON_SIZE; col++) {
      bool black = col < APP_ICON_SIZE && !(bits[row * (APP_ICON_SIZE / 8) + col / 8] & (0x80 >> (col % 8)));
      if (black && run < 0) run = col;
      if (!black && run >= 0) {
        bbep.fillRect(x + run * scale, y + row * scale, (col - run) * scale, scale, BBEP_BLACK);
        run = -1;
      }
    }
  }
}

// An app without an icon: a frame with the first letter of its name
static void draw_generic_icon(const char *name, int x, int y, int scale) {
  const int s = APP_ICON_SIZE * scale;
  const app_rect_t box = {(int16_t)x, (int16_t)y, (int16_t)s, (int16_t)s};
  tile_border(box, 2 * scale, BBEP_BLACK);
  char letter[2] = {app_meta_letter(name), 0};
  bbep.setFont(Roboto_Black_24);
  draw_centered(letter, x + s / 2, y + s / 2 + 10, s);
}

static void draw_back_icon(int x, int y, int scale) {
  const int s = APP_ICON_SIZE * scale;
  hints_fill_triangle(x + s / 2, y + s / 2, s / 2, 1, BBEP_BLACK); // the grid has no left triangle: an up arrow
}

// One tile: icon above the name; `selected` thickens the border
static void draw_tile(const app_rect_t &t, const app_meta_t *m, bool selected) {
  const int scale = max(1, min(3, min((t.w - 40) / APP_ICON_SIZE, (t.h - 110) / APP_ICON_SIZE))); // 192 px at most
  const int ix = t.x + (t.w - APP_ICON_SIZE * scale) / 2, iy = t.y + 24;
  bbep.fillRect(t.x, t.y, t.w, t.h, BBEP_WHITE);
  if (!m) {
    draw_back_icon(ix, iy, scale);
  } else if (m->has_icon) {
    draw_icon(m->icon, ix, iy, scale);
  } else {
    draw_generic_icon(m->name, ix, iy, scale);
  }
  bbep.setFont(Inter_18);
  bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);
  draw_centered(m ? m->name : "Back", t.x + t.w / 2, iy + APP_ICON_SIZE * scale + 56, t.w - 20);
  tile_border(t, TILE_BORDER, BBEP_BLACK);
  if (selected) tile_border({(int16_t)(t.x + TILE_BORDER), (int16_t)(t.y + TILE_BORDER), (int16_t)(t.w - 2 * TILE_BORDER),
                             (int16_t)(t.h - 2 * TILE_BORDER)}, TILE_SELECTED - TILE_BORDER, BBEP_BLACK);
}

static void draw_page(const app_grid_t &g, const char *title, const app_meta_t *metas, const int *order, int n,
                      int page, int sel, int pages) {
  static const menu_hint_t hints[MENU_HINT_SLOTS] = {{"Left", MENU_HINT_ICON_NONE},
                                                     {"Select", MENU_HINT_ICON_NONE},
                                                     {"Right", MENU_HINT_ICON_NONE}};
  bbep.setMode(BB_MODE_1BPP);
  bbep.fillScreen(BBEP_WHITE);
  bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);
  bbep.setFont(Roboto_Black_24);
  bbep.drawString(title, g.area.x, 70);
  if (pages > 1) { // one square per page, the current one filled
    for (int p = 0; p < pages; p++) {
      const app_rect_t sq = {(int16_t)(g.area.x + g.area.w - (pages - p) * 36), 46, 24, 24};
      if (p == page) bbep.fillRect(sq.x, sq.y, sq.w, sq.h, BBEP_BLACK);
      else tile_border(sq, 3, BBEP_BLACK);
    }
  }
  for (int s = 0; s < g.per_page; s++) {
    const int i = page * g.per_page + s; // i == n: the Back tile
    if (i > n) break;
    draw_tile(app_grid_tile(&g, s), i < n ? &metas[order[i]] : nullptr, i == sel);
  }
  for (int i = 0; i < MENU_HINT_SLOTS; i++) hints_draw(hints[i].label, HINTS_ICON_NONE, i, HINTS_OUTLINED);
}

int menu_apps_pick(const menu_controls_t &controls, const char *title, const app_meta_t *metas, const int *order, int n,
                   unsigned long timeout_ms) {
  app_grid_t g;
  if (!app_grid_layout(bbep.width(), bbep.height(), bar_edge(), &g)) return MENU_APPS_BACK;
  const int count = n + 1; // and Back
  const int pages = app_grid_pages(&g, count);
  int sel = 0, page = 0;
  draw_page(g, title, metas, order, n, page, sel, pages);
  bbep.fullUpdate(CLEAR_FAST, false);
  unsigned long last = millis();
  while (millis() - last < timeout_ms) {
    delay(20);
    menu_input_t in = controls.read();
    if (in == menu_input_t::NONE) continue;
    last = millis();
    if (in == menu_input_t::SELECT) return sel == n ? MENU_APPS_BACK : sel;
    const int next = app_grid_move(count, sel, in == menu_input_t::UP ? -1 : 1);
    if (next == sel) continue;
    if (next / g.per_page != page) { // another page: a full refresh
      page = next / g.per_page;
      sel = next;
      draw_page(g, title, metas, order, n, page, sel, pages);
      bbep.fullUpdate(CLEAR_FAST, false);
      continue;
    }
    const int old = sel;
    sel = next;
    for (int pass = 0; pass < 2; pass++) { // the old tile loses its heavy border, the new one gains it
      const int i = pass ? sel : old;
      draw_tile(app_grid_tile(&g, i % g.per_page), i < n ? &metas[order[i]] : nullptr, pass == 1);
    }
    bbep.partialUpdate(false);
  }
  return MENU_APPS_TIMEOUT;
}

// ---- reading the apps ----

static int read_file(void *ctx, uint32_t off, void *buf, size_t len) {
  File *f = (File *)ctx;
  return f->seek(off) && f->read((uint8_t *)buf, len) == len ? 0 : -1;
}

bool menu_apps_meta(const char *name, app_meta_t *out) {
  char path[APPS_PATH_MAX];
  snprintf(path, sizeof(path), APPS_DIR "/%s.elf", name);
  File f = LittleFS.open(path, "r");
  if (!f) {
    app_meta_read(nullptr, nullptr, 0, name, out); // the name from the file's name
    return false;
  }
  app_meta_read(read_file, &f, (uint32_t)f.size(), name, out);
  f.close();
  return true;
}

void menu_apps_load(app_meta_t *metas, const char names[][32], int n) {
  for (int i = 0; i < n; i++) menu_apps_meta(names[i], &metas[i]);
}

int menu_apps_open(const menu_controls_t &controls, const app_meta_t *metas, const char names[][32], int n) {
  int order[APPS_MAX];
  char mru[APP_MRU_MAX];
  snprintf(mru, sizeof(mru), "%s", preferences.getString(APP_MRU_KEY, "").c_str());
  app_mru_order(names, n, mru, order);
  int pos = menu_apps_pick(controls, n ? "Open an app" : "No apps installed", metas, order, n, 30000);
  if (pos < 0) return pos;
  app_mru_touch(mru, sizeof(mru), names[order[pos]]);
  preferences.putString(APP_MRU_KEY, mru);
  return order[pos];
}

// The app that is running or due to resume: it can not be removed
static void active_app(char *name, size_t len) {
  name[0] = '\0';
  char path[APPS_PATH_MAX];
  snprintf(path, sizeof(path), "%s", preferences.getString(APP_RESUME_PATH_KEY, "").c_str());
  if (preferences.getBool(APP_RESUME_FLAG_KEY, false) && !app_path_name(path, name, len)) name[0] = '\0';
}

int menu_apps_remove(const menu_controls_t &controls, menu_apps_choose_t choose, const app_meta_t *metas,
                     const char names[][32], int n) {
  int order[APPS_MAX];
  for (int i = 0; i < n; i++) order[i] = i;
  const int pos = menu_apps_pick(controls, n ? "Remove an app" : "No apps installed", metas, order, n, 30000);
  if (pos < 0) return pos;
  const char *file = names[pos];
  const char *shown = metas[pos].name;
  char line[2][96];
  const char *body[2] = {line[0], line[1]};
  const char *labels[3] = {"Remove", "Remove with its files", "Back"};
  snprintf(line[0], sizeof(line[0]), "Remove %s? Its saved files stay.", shown);
  int c = choose(labels, 3, body, 1, 2);
  if (c < 0) return MENU_APPS_TIMEOUT;
  if (c == 2) return MENU_APPS_BACK;
  const bool with_files = c == 1;
  if (with_files) {
    const char *yes[2] = {"Yes, delete everything", "Back"};
    snprintf(line[0], sizeof(line[0]), "Delete %s and all its saved files?", shown);
    snprintf(line[1], sizeof(line[1]), "Erases %s/%s. This can not be undone.", APPS_DATA_DIR, file);
    c = choose(yes, 2, body, 2, 1);
    if (c < 0) return MENU_APPS_TIMEOUT;
    if (c != 0) return MENU_APPS_BACK;
  }
  char active[32];
  active_app(active, sizeof(active));
  const app_remove_t r = app_remove(webshare_fs_root(), APPS_DIR_REL, APPS_DATA_DIR_REL, file, active, with_files, true);
  Log_info("Apps: remove %s%s: %d", file, with_files ? " with its files" : "", (int)r);
  snprintf(line[0], sizeof(line[0]), r == APP_REMOVE_OK ? "Removed %s." : r == APP_REMOVE_ACTIVE
           ? "%s is running; end it first." : "Could not remove %s.", shown);
  const char *ok[1] = {"Back"};
  choose(ok, 1, body, 1, 0);
  return MENU_APPS_BACK;
}

// "Storage": the used and free bytes, each app with its .bak copy and saved files, and "Free space" (app_space.h)
int menu_apps_storage(menu_apps_choose_t choose) {
  static app_space_app_t apps[APPS_MAX];
  static char lines[APPS_MAX + 2][96];
  const char *body[APPS_MAX + 2];
  while (true) {
    int n = app_space_apps(webshare_fs_root(), apps, APPS_MAX), shown = 0;
    app_storage_line(lines[shown], sizeof(lines[0]));
    body[shown] = lines[shown];
    shown++;
    uint64_t baks = 0;
    for (int i = 0; i < n && shown < 12; i++) {
      char e[16], d[16];
      app_space_size_text(apps[i].elf + apps[i].bak, e, sizeof(e));
      app_space_size_text(apps[i].data, d, sizeof(d));
      snprintf(lines[shown], sizeof(lines[0]), "%s\t%s app, %s saved", apps[i].name, e, d);
      body[shown] = lines[shown];
      shown++;
      baks += apps[i].bak;
    }
    char b[16];
    app_space_size_text(baks, b, sizeof(b));
    snprintf(lines[shown], sizeof(lines[0]), "Free space removes old copies of apps (%s) and leftovers.", b);
    body[shown] = lines[shown];
    shown++;
    const char *labels[2] = {"Free space", "Back"};
    int c = choose(labels, 2, body, shown, 1);
    if (c < 0) return MENU_APPS_TIMEOUT;
    if (c != 0) return MENU_APPS_BACK;
    app_storage_free_space(); // the screen again, with the new numbers
  }
}

#endif // BOARD_TRMNL_X
