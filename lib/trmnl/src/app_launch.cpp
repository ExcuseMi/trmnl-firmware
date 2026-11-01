#include "app_launch.h"

#include <app_api.h>
#include <app_path.h>

#include <stdio.h>
#include <string.h>

int app_launch_set(app_launch_t *rec, const char *app, const char *arg, bool app_exists) {
  if (!rec || !app || !arg || !app_path_app_ok(app) || strlen(arg) > APP_LAUNCH_ARG_MAX) return APP_EINVAL;
  if (!app_exists) return APP_ENOENT;
  snprintf(rec->app, sizeof(rec->app), "%s", app);
  snprintf(rec->arg, sizeof(rec->arg), "%s", arg);
  rec->pending = true;
  return 0;
}

int app_launch_page(app_launch_t *rec, const char *section, const char *self) {
  static const char *const SECTIONS[] = {"configure", "apps", "devices", "this-x"};
  if (!rec || !section) return APP_EINVAL;
  int which = -1;
  for (int i = 0; i < 4; i++)
    if (strcmp(section, SECTIONS[i]) == 0) which = i;
  if (which < 0) return APP_EINVAL;
  if (which == 0 && (!self || !app_path_app_ok(self))) return APP_EINVAL;
  if (which == 0) snprintf(rec->arg, sizeof(rec->arg), "configure=%s", self);
  else snprintf(rec->arg, sizeof(rec->arg), "%s", section);
  rec->app[0] = '\0';
  rec->pending = true;
  return 0;
}

bool app_launch_take(app_launch_t *rec, char *app, size_t app_len, char *arg, size_t arg_len) {
  if (!rec || !rec->pending || !app || !arg || app_len <= strlen(rec->app) || arg_len <= strlen(rec->arg)) return false;
  snprintf(app, app_len, "%s", rec->app);
  snprintf(arg, arg_len, "%s", rec->arg);
  rec->pending = false;
  return true;
}

int app_arg_copy(const char *arg, char *out, size_t n) {
  if (!out || n == 0) return APP_EINVAL;
  if (!arg) arg = "";
  snprintf(out, n, "%s", arg);
  return (int)strlen(arg);
}
