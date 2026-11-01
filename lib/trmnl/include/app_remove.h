#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "webshare_fs.h"

// Removing an installed app (Apps > Remove an app, docs/apps.md): its ELF and its older version (.bak) always; its
// saved files in <data_dir>/<name> only when asked. The paths are under the root of `fs`.
typedef enum {
  APP_REMOVE_OK,
  APP_REMOVE_NOT_CONFIRMED, // `confirmed` is false: nothing is touched
  APP_REMOVE_BAD_NAME, // not an app name (letters, digits, - and _, at most 24)
  APP_REMOVE_ACTIVE, // the app is running or due to resume
  APP_REMOVE_NOT_INSTALLED,
  APP_REMOVE_FAILED, // a file could not be removed
} app_remove_t;

// `apps_dir`, `data_dir`: "/apps", "/appdata". `active`: the name of the running / resuming app, or "".
// `with_files` needs its own `confirmed` too: the caller asks twice (the second states what will be deleted).
app_remove_t app_remove(const ws_fs_t *fs, const char *apps_dir, const char *data_dir, const char *name,
                        const char *active, bool with_files, bool confirmed);
