#include "app_grid.h"

#include <stdio.h>
#include <string.h>

bool app_grid_layout(int w, int h, int bar_edge, app_grid_t *g) {
  if (!g || w <= 0 || h <= 0) return false;
  const bool landscape = w >= h;
  g->cols = landscape ? 4 : 2;
  g->rows = landscape ? 2 : 4;
  g->per_page = g->cols * g->rows;
  int x0 = APP_GRID_MARGIN, y0 = APP_GRID_TITLE_H, x1 = w - APP_GRID_MARGIN, y1 = h - APP_GRID_MARGIN;
  switch (bar_edge) {
  case APP_EDGE_BOTTOM: y1 = h - APP_TOUCHBAR_DEPTH; break;
  case APP_EDGE_TOP: y0 = APP_TOUCHBAR_DEPTH + APP_GRID_TITLE_H / 2; break;
  case APP_EDGE_LEFT: x0 = APP_TOUCHBAR_DEPTH; break;
  case APP_EDGE_RIGHT: x1 = w - APP_TOUCHBAR_DEPTH; break;
  default: return false;
  }
  g->area = {(int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
  const int tw = (g->area.w - (g->cols - 1) * APP_GRID_GAP) / g->cols;
  const int th = (g->area.h - (g->rows - 1) * APP_GRID_GAP) / g->rows;
  return x1 > x0 && y1 > y0 && tw >= 100 && th >= 100;
}

app_rect_t app_grid_tile(const app_grid_t *g, int slot) {
  const int tw = (g->area.w - (g->cols - 1) * APP_GRID_GAP) / g->cols;
  const int th = (g->area.h - (g->rows - 1) * APP_GRID_GAP) / g->rows;
  const int c = slot % g->cols, r = slot / g->cols;
  return {(int16_t)(g->area.x + c * (tw + APP_GRID_GAP)), (int16_t)(g->area.y + r * (th + APP_GRID_GAP)), (int16_t)tw,
          (int16_t)th};
}

int app_grid_pages(const app_grid_t *g, int count) {
  if (count <= 0) return 1;
  return (count + g->per_page - 1) / g->per_page;
}

int app_grid_move(int count, int sel, int dir) {
  if (count <= 0) return 0;
  return ((sel + dir) % count + count) % count;
}

void app_mru_order(const char names[][32], int n, const char *mru, int *order) {
  bool used[64] = {};
  int k = 0;
  if (n > 64) n = 64;
  for (const char *p = mru ? mru : ""; *p;) {
    const char *end = strchr(p, ',');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    for (int i = 0; i < n; i++) {
      if (!used[i] && strlen(names[i]) == len && strncmp(names[i], p, len) == 0) {
        used[i] = true;
        order[k++] = i;
        break;
      }
    }
    p += len + (end ? 1 : 0);
  }
  for (int i = 0; i < n; i++) {
    if (!used[i]) order[k++] = i;
  }
}

void app_mru_touch(char *mru, size_t cap, const char *name) {
  if (!mru || cap < 2 || !name || !*name) return;
  if (cap > APP_MRU_MAX) cap = APP_MRU_MAX;
  char out[APP_MRU_MAX] = {};
  snprintf(out, cap, "%s", name);
  if (strlen(name) >= cap) { // the name alone does not fit: nothing useful to keep
    mru[0] = '\0';
    return;
  }
  size_t used = strlen(out);
  for (const char *p = mru; *p;) {
    const char *end = strchr(p, ',');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len && !(len == strlen(name) && strncmp(p, name, len) == 0)) {
      if (used + 1 + len >= cap) break; // the oldest names that do not fit are dropped
      out[used++] = ',';
      memcpy(out + used, p, len);
      used += len;
      out[used] = '\0';
    }
    p += len + (end ? 1 : 0);
  }
  memcpy(mru, out, used + 1);
}
