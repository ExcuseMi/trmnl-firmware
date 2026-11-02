#pragma once

#include <stddef.h>
#include <stdint.h>

#include "webshare_fs.h"

// Storage for apps on the X's file system (docs/apps.md "Storage"): how much an upload may use, making room by
// dropping the .bak copies app_install keeps, and what each app takes. The paths are under the root of `fs`.
// The app being replaced is never touched before its new version is complete (app_install.h): the upload needs
// its own room next to it.

// Bytes an upload may use: the file system's free bytes plus a stale upload temp file (it is removed first)
uint64_t app_space_free(const ws_fs_t *fs);

// Room for an upload of `need` bytes plus APP_UPLOAD_MARGIN: drops every apps/<name>.elf.bak while it is short.
// Returns the bytes free for it afterwards (app_space_free); `dropped` (may be nullptr) counts the .bak files gone
uint64_t app_space_make_room(const ws_fs_t *fs, uint64_t need, int *dropped);

// "no space: 120 KB free, 146 KB needed" (the margin counted in `needed`) into `out`, for "TAPP ERR"
void app_space_no_room_text(uint64_t free_bytes, uint64_t need, char *out, size_t out_len);

// "Free space": removes every .bak copy and a stale upload temp file; returns the bytes freed
uint64_t app_space_clean(const ws_fs_t *fs, int *removed);

// One app: its .elf, its .bak copy, its saved data (appdata/<name>, all files under it)
typedef struct {
  char name[32];
  uint32_t elf, bak;
  uint64_t data;
} app_space_app_t;

// The apps in apps/ with their sizes, at most `max`; returns the count
int app_space_apps(const ws_fs_t *fs, app_space_app_t *out, int max);

// The bytes of all files under `dir` ("" the root), any depth
uint64_t app_space_dir_bytes(const ws_fs_t *fs, const char *dir);

// "1.2 MB", "130 KB", "512 B" into `out`
void app_space_size_text(uint64_t bytes, char *out, size_t out_len);

// Every file under `dir` with its size, any depth (the serial "TAPP LS"): `cb(path, size)`; false stops
typedef bool (*app_space_file_cb_t)(const char *path, uint64_t size, bool is_dir, void *ctx);
void app_space_walk(const ws_fs_t *fs, const char *dir, app_space_file_cb_t cb, void *ctx);

// The apps folder in one line for the event log: "apps: gameboy 412 KB, sketch 130 KB (+bak 1.0 MB)"
void app_space_apps_line(const ws_fs_t *fs, char *out, size_t out_len);
