#pragma once

#include <stddef.h>

// One app starting another (API 2.9, docs/apps.md "Launching an app"). The hand-over lives in RAM only: a restart
// forgets it, so the resume after a restart never starts an app with an old argument (it starts with "").

#define APP_LAUNCH_ARG_MAX 128 // bytes of the argument, without the terminating NUL
#define APP_LAUNCH_NAME_MAX 24

typedef struct {
  bool pending;
  char app[APP_LAUNCH_NAME_MAX + 1];
  char arg[APP_LAUNCH_ARG_MAX + 1];
} app_launch_t;

// Records that `app` starts next with `arg`. `app_exists`: /apps/<app>.elf is there (the caller checks the file system).
// 0, APP_EINVAL (NULL, a bad name, an argument over APP_LAUNCH_ARG_MAX bytes) or APP_ENOENT. Nothing is recorded on
// an error; a second call replaces the first.
int app_launch_set(app_launch_t *rec, const char *app, const char *arg, bool app_exists);

// If a launch is pending: copies the app name and argument out, clears it, returns true
bool app_launch_take(app_launch_t *rec, char *app, size_t app_len, char *arg, size_t arg_len);

// Copies the start argument `arg` (NULL: "") to `out` (cut to n - 1 bytes) and returns its full length, or APP_EINVAL
int app_arg_copy(const char *arg, char *out, size_t n);
