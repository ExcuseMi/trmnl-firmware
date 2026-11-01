#pragma once

#include <stddef.h>

// Paths of an app's files (app_api.h): relative to its folder, without "..", "." or empty parts,
// absolute paths or backslashes, and at most APP_PATH_MAX characters.

// Joins `base` (the app's folder, no trailing slash) and `rel` into `out`; false if `rel` is refused
// or the result does not fit. An empty `rel` is the folder itself.
bool app_path_join(const char *base, const char *rel, char *out, size_t out_len);

// The app's name from its ELF path: "/apps/hello.elf" -> "hello". False if the path is not
// /apps/<name>.elf with a name of letters, digits, '-' and '_' (at most 24 characters).
bool app_path_name(const char *elf_path, char *name, size_t name_len);

// Another app's folder, read only (API 2.8). An app name is 1..24 of letters, digits, '-' and '_' (what app_path_name
// accepts), so it cannot hold a slash or "..".
bool app_path_app_ok(const char *app);

// Joins `data_dir` ("/appdata"), the app `app` and `rel` (the rules of app_path_join) into `out`; false if the name or
// `rel` is refused or the result does not fit. An empty `rel` is the app's folder.
bool app_path_join_app(const char *data_dir, const char *app, const char *rel, char *out, size_t out_len);

// 0, or APP_EPERM when a write is asked of a handle opened read only (file_open_app)
int app_file_write_check(bool read_only);
