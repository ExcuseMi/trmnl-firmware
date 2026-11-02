// LittleFS free bytes with its clamped "full" readings read again (fs_space.h)
#include "fs_space.h"

uint64_t fs_space_free(const fs_space_port_t *p, uint64_t total, uint64_t *used_out, bool *unsure) {
  uint64_t used = total;
  for (int i = 0; i < FS_SPACE_TRIES; i++) {
    if (i) p->wait_ms(p->ctx, FS_SPACE_WAIT_MS);
    used = p->used(p->ctx);
    if (used < total) break;
  }
  if (used_out) *used_out = used < total ? used : total;
  if (unsure) *unsure = used >= total;
  return used < total ? total - used : 0;
}
