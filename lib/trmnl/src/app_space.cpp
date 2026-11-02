#include "app_space.h"

#include <stdio.h>
#include <string.h>

#include <new>

#include <app_upload.h>

#include "app_install.h"

#define BAK_MAX 16      // .bak files collected per pass: no removal while the directory is walked
#define SUBDIRS_MAX 24  // folders collected per level of a walk
#define DEPTH_MAX 6     // a walk goes no deeper
#define NAME_LEN 64

static bool file_size(const ws_fs_t *fs, const char *path, uint32_t *size) {
  bool dir = false;
  return fs->stat(fs->ctx, path, size, &dir) == 0 && !dir;
}

uint64_t app_space_free(const ws_fs_t *fs) {
  uint64_t free_bytes = fs->free_bytes ? fs->free_bytes(fs->ctx) : 0;
  uint32_t tmp = 0;
  if (file_size(fs, APP_UPLOAD_TMP + 1, &tmp)) free_bytes += tmp; // removed before the next upload starts
  return free_bytes;
}

static bool fits(uint64_t free_bytes, uint64_t need) {
  return free_bytes >= APP_UPLOAD_MARGIN && need <= free_bytes - APP_UPLOAD_MARGIN;
}

typedef struct {
  char names[BAK_MAX][NAME_LEN];
  int n;
} names_t;

static bool is_bak(const char *file) {
  size_t n = strlen(file);
  return n > 8 && strcmp(file + n - 8, ".elf.bak") == 0;
}

static bool bak_cb(const char *file, bool is_dir, uint32_t, void *ctx) {
  names_t *b = (names_t *)ctx;
  if (!is_dir && is_bak(file) && strlen(file) < NAME_LEN - sizeof(APPS_DIR_REL) && b->n < BAK_MAX) {
    snprintf(b->names[b->n++], NAME_LEN, APPS_DIR_REL "/%s", file);
  }
  return true;
}

// Removes the .bak files (all, or until `need` fits when `need` > 0); counts them and the bytes freed
static uint64_t drop_baks(const ws_fs_t *fs, uint64_t need, int *dropped) {
  names_t b;
  b.n = 0;
  fs->list(fs->ctx, APPS_DIR_REL, bak_cb, &b);
  uint64_t freed = 0;
  for (int i = 0; i < b.n; i++) {
    if (need && fits(app_space_free(fs), need)) break;
    uint32_t size = 0;
    if (!file_size(fs, b.names[i], &size) || fs->remove(fs->ctx, b.names[i]) < 0) continue;
    freed += size;
    if (dropped) (*dropped)++;
  }
  return freed;
}

uint64_t app_space_make_room(const ws_fs_t *fs, uint64_t need, int *dropped) {
  if (dropped) *dropped = 0;
  if (!fits(app_space_free(fs), need)) drop_baks(fs, need ? need : 1, dropped);
  return app_space_free(fs);
}

void app_space_size_text(uint64_t bytes, char *out, size_t out_len) {
  if (bytes >= 1024u * 1024u) {
    unsigned tenths = (unsigned)((bytes * 10 + 512u * 1024u) / (1024u * 1024u));
    snprintf(out, out_len, "%u.%u MB", tenths / 10, tenths % 10);
  } else if (bytes >= 1024u) {
    snprintf(out, out_len, "%u KB", (unsigned)((bytes + 512u) / 1024u));
  } else {
    snprintf(out, out_len, "%u B", (unsigned)bytes);
  }
}

void app_space_no_room_text(uint64_t free_bytes, uint64_t need, char *out, size_t out_len) {
  char f[16], n[16];
  app_space_size_text(free_bytes, f, sizeof(f));
  app_space_size_text(need + APP_UPLOAD_MARGIN, n, sizeof(n));
  snprintf(out, out_len, "%s free, %s needed", f, n);
}

uint64_t app_space_clean(const ws_fs_t *fs, int *removed) {
  if (removed) *removed = 0;
  uint64_t freed = drop_baks(fs, 0, removed);
  uint32_t tmp = 0;
  if (file_size(fs, APP_UPLOAD_TMP + 1, &tmp) && fs->remove(fs->ctx, APP_UPLOAD_TMP + 1) == 0) {
    freed += tmp;
    if (removed) (*removed)++;
  }
  return freed;
}

typedef struct {
  char dirs[SUBDIRS_MAX][NAME_LEN * 2];
  int n;
  const char *base;
  app_space_file_cb_t cb;
  void *ctx;
  bool stop;
} level_t;

static bool level_cb(const char *name, bool is_dir, uint32_t size, void *ctx) {
  level_t *l = (level_t *)ctx;
  char path[NAME_LEN * 2];
  snprintf(path, sizeof(path), "%s%s%s", l->base, l->base[0] ? "/" : "", name);
  if (!l->cb(path, size, is_dir, l->ctx)) {
    l->stop = true;
    return false;
  }
  if (is_dir && l->n < SUBDIRS_MAX) memcpy(l->dirs[l->n++], path, sizeof(path));
  return true;
}

static bool walk(const ws_fs_t *fs, const char *dir, int depth, app_space_file_cb_t cb, void *ctx) {
  level_t *l = new (std::nothrow) level_t; // NAME_LEN * SUBDIRS_MAX per level: off the small task stacks
  if (!l) return false;
  l->n = 0;
  l->base = dir;
  l->cb = cb;
  l->ctx = ctx;
  l->stop = false;
  fs->list(fs->ctx, dir, level_cb, l);
  bool go_on = !l->stop;
  for (int i = 0; go_on && depth < DEPTH_MAX && i < l->n; i++) go_on = walk(fs, l->dirs[i], depth + 1, cb, ctx);
  delete l;
  return go_on;
}

void app_space_walk(const ws_fs_t *fs, const char *dir, app_space_file_cb_t cb, void *ctx) { walk(fs, dir, 0, cb, ctx); }

static bool sum_cb(const char *, uint64_t size, bool is_dir, void *ctx) {
  if (!is_dir) *(uint64_t *)ctx += size;
  return true;
}

uint64_t app_space_dir_bytes(const ws_fs_t *fs, const char *dir) {
  uint64_t total = 0;
  app_space_walk(fs, dir, sum_cb, &total);
  return total;
}

typedef struct {
  app_space_app_t *out;
  int max, n;
} apps_t;

static bool app_cb(const char *file, bool is_dir, uint32_t size, void *ctx) {
  apps_t *a = (apps_t *)ctx;
  app_space_app_t *e = &a->out[a->n];
  if (!is_dir && a->n < a->max && app_install_name_ok(file, e->name, sizeof(e->name))) {
    e->elf = size;
    e->bak = 0;
    e->data = 0;
    a->n++;
  }
  return a->n < a->max;
}

int app_space_apps(const ws_fs_t *fs, app_space_app_t *out, int max) {
  apps_t a = {out, max, 0};
  if (max <= 0) return 0;
  fs->list(fs->ctx, APPS_DIR_REL, app_cb, &a);
  for (int i = 0; i < a.n; i++) {
    char path[NAME_LEN * 2];
    snprintf(path, sizeof(path), APPS_DIR_REL "/%s.elf.bak", out[i].name);
    file_size(fs, path, &out[i].bak);
    snprintf(path, sizeof(path), APPS_DATA_DIR_REL "/%s", out[i].name);
    out[i].data = app_space_dir_bytes(fs, path);
  }
  return a.n;
}

void app_space_apps_line(const ws_fs_t *fs, char *out, size_t out_len) {
  static app_space_app_t apps[16];
  int n = app_space_apps(fs, apps, 16);
  size_t o = (size_t)snprintf(out, out_len, "apps:%s", n ? "" : " none");
  for (int i = 0; i < n && o < out_len; i++) {
    char e[16], b[16], d[16];
    app_space_size_text(apps[i].elf, e, sizeof(e));
    app_space_size_text(apps[i].bak, b, sizeof(b));
    app_space_size_text(apps[i].data, d, sizeof(d));
    o += (size_t)snprintf(out + o, out_len - o, "%s %s %s%s%s%s%s", i ? "," : "", apps[i].name, e,
                          apps[i].bak ? " +bak " : "", apps[i].bak ? b : "", apps[i].data ? " +data " : "",
                          apps[i].data ? d : "");
  }
  if (o < out_len) {
    char f[16];
    app_space_size_text(fs->free_bytes ? fs->free_bytes(fs->ctx) : 0, f, sizeof(f));
    snprintf(out + o, out_len - o, "; %s free", f);
  }
}
