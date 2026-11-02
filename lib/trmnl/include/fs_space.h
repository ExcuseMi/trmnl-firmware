#pragma once

#include <stdint.h>

// Free bytes of LittleFS from its total and a used-bytes reading. esp_littlefs clamps the reading to the total when
// lfs_fs_size fails or over-counts (a file open for writing in another task, blocks shared while copied): the owner's
// X 2026-11-02 said "10.1 of 10.1 MB used, 0 free" with 5.4 MB of files, and uploads were refused for no space. A
// reading of used >= total is taken as a failed one and read again, `tries` times at most, `wait_ms` apart;
// `*unsure` (may be nullptr) is set when every reading failed (0 is returned then: truly full, or not known).
typedef struct {
  void *ctx;
  uint64_t (*used)(void *ctx);
  void (*wait_ms)(void *ctx, uint32_t ms);
} fs_space_port_t;

#define FS_SPACE_TRIES 4
#define FS_SPACE_WAIT_MS 30

uint64_t fs_space_free(const fs_space_port_t *p, uint64_t total, uint64_t *used_out, bool *unsure);
