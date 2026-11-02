#pragma once

#include <stddef.h>
#include <stdint.h>

#include "webshare_fs.h"

// Replaces a whole file so that a full disk or a cut never leaves it empty or half written: the bytes go to `tmp`,
// its size on disk is checked (the Arduino wrapper hides a short write at close) and only then is it renamed over
// `path` (LittleFS renames atomically). On a failure `tmp` is removed and `path` keeps its old bytes.
// Owner's X 2026-11-02: the event log was opened with "w" on a full disk and lost.
// Returns 0, or < 0 when nothing changed.
int fs_replace(const ws_fs_t *fs, const char *path, const char *tmp, const void *data, size_t len);
