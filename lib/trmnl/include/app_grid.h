#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <app_api.h>

// The Apps menu's "Open an app" grid (pure parts): the tile layout, which tile a touch-bar tap moves to, and the
// most-recently-used order. Drawing is in src/menu_apps.cpp.

#define APP_GRID_TITLE_H 120 // band at the top: title and page indicator
#define APP_GRID_MARGIN 40
#define APP_GRID_GAP 24

typedef struct {
  int cols, rows, per_page;
  app_rect_t area; // where the tiles lie (inside the screen, clear of the title band and the touch bar's hints)
} app_grid_t;

// `w`, `h`: the screen in the current orientation. Landscape (w >= h) is 4 x 2 tiles, portrait 2 x 4. `bar_edge`:
// APP_EDGE_* the touch bar lies along; its APP_TOUCHBAR_DEPTH strip is kept free for the hint pills. False if the
// screen is too small for 8 tiles of at least 100 pixels.
bool app_grid_layout(int w, int h, int bar_edge, app_grid_t *g);

// Tile `slot` (0 .. per_page - 1) of a page
app_rect_t app_grid_tile(const app_grid_t *g, int slot);

// Pages for `count` tiles
int app_grid_pages(const app_grid_t *g, int count);

// The tile after `sel` moving `dir` (-1 left, +1 right) over `count` tiles in reading order, wrapping around
int app_grid_move(int count, int sel, int dir);

// ---- most recently used ----
#define APP_MRU_KEY "app_mru"
#define APP_MRU_MAX 160 // bytes of the stored list "name,name,..." (NVS string)

// Puts `order[0..n)` (indexes into names[]) in the order: apps in `mru` first, in the list's order; the others after
// them in their given order (the caller sorts by name). Names in `mru` that are not installed are skipped.
void app_mru_order(const char names[][32], int n, const char *mru, int *order);

// Moves `name` to the front of `mru` (capacity `cap`, at most APP_MRU_MAX), dropping the oldest names that do not fit
void app_mru_touch(char *mru, size_t cap, const char *name);
