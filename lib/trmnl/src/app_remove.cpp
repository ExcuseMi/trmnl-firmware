#include "app_remove.h"

#include <stdio.h>
#include <string.h>

#include "app_path.h"

#define REMOVE_PATH_MAX 96
#define REMOVE_DEPTH_MAX 6
#define REMOVE_ENTRIES_MAX 64 // per folder; more is a folder an app should not have made: left in place, FAILED

typedef struct {
  char name[40];
  bool is_dir;
} entry_t;

typedef struct {
  entry_t *e;
  int n;
  bool full;
} entries_t;

static bool collect(const char *name, bool is_dir, uint32_t, void *ctx) {
  entries_t *es = (entries_t *)ctx;
  if (es->n == REMOVE_ENTRIES_MAX) {
    es->full = true;
    return false;
  }
  snprintf(es->e[es->n].name, sizeof(es->e[0].name), "%s", name);
  es->e[es->n++].is_dir = is_dir;
  return true;
}

// Removes a folder and what is in it; false if anything is left
static bool remove_tree(const ws_fs_t *fs, const char *path, int depth) {
  if (depth > REMOVE_DEPTH_MAX) return false;
  entry_t items[REMOVE_ENTRIES_MAX];
  entries_t es = {items, 0, false};
  if (fs->list(fs->ctx, path, collect, &es) < 0) return false;
  bool ok = !es.full;
  for (int i = 0; i < es.n; i++) {
    char child[REMOVE_PATH_MAX];
    if (snprintf(child, sizeof(child), "%s/%s", path, items[i].name) >= (int)sizeof(child)) {
      ok = false;
      continue;
    }
    ok = (items[i].is_dir ? remove_tree(fs, child, depth + 1) : fs->remove(fs->ctx, child) == 0) && ok;
  }
  return fs->remove(fs->ctx, path) == 0 && ok;
}

app_remove_t app_remove(const ws_fs_t *fs, const char *apps_dir, const char *data_dir, const char *name,
                        const char *active, bool with_files, bool confirmed) {
  if (!confirmed) return APP_REMOVE_NOT_CONFIRMED;
  if (!fs || !name || !app_path_app_ok(name)) return APP_REMOVE_BAD_NAME;
  if (active && strcmp(active, name) == 0) return APP_REMOVE_ACTIVE;
  char elf[REMOVE_PATH_MAX], bak[REMOVE_PATH_MAX + 8];
  snprintf(elf, sizeof(elf), "%s/%s.elf", apps_dir, name);
  snprintf(bak, sizeof(bak), "%s.bak", elf);
  uint32_t size;
  bool dir;
  if (fs->stat(fs->ctx, elf, &size, &dir) != 0 || dir) return APP_REMOVE_NOT_INSTALLED;
  bool ok = fs->remove(fs->ctx, elf) == 0;
  if (fs->stat(fs->ctx, bak, &size, &dir) == 0) ok = fs->remove(fs->ctx, bak) == 0 && ok;
  if (with_files) {
    char data[REMOVE_PATH_MAX];
    if (app_path_join_app(data_dir, name, "", data, sizeof(data)) &&
        fs->stat(fs->ctx, data, &size, &dir) == 0) {
      ok = remove_tree(fs, data, 0) && ok;
    }
  }
  return ok ? APP_REMOVE_OK : APP_REMOVE_FAILED;
}
