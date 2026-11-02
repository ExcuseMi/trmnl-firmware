// A whole file replaced through a temporary file and a rename (fs_replace.h)
#include "fs_replace.h"

int fs_replace(const ws_fs_t *fs, const char *path, const char *tmp, const void *data, size_t len) {
  if (!fs || !path || !tmp || (!data && len)) return -1;
  void *f = fs->open(fs->ctx, tmp, true);
  if (!f) return -1;
  int w = len ? fs->write(fs->ctx, f, data, len) : 0;
  int c = fs->close(fs->ctx, f);
  uint32_t size = 0;
  bool dir = false;
  bool ok = w == (int)len && c == 0 && fs->stat(fs->ctx, tmp, &size, &dir) == 0 && !dir && size == len;
  if (ok && fs->rename(fs->ctx, tmp, path) == 0) return 0;
  fs->remove(fs->ctx, tmp);
  return -1;
}
