#ifndef MENU_APPS_H
#define MENU_APPS_H

// The Apps submenu's grid of installed apps (Open an app, Remove an app): a tile each with the icon and the display
// name, then a Back tile; the touch bar moves left and right, the middle chooses. Layout and order: app_grid.h.
#ifdef BOARD_TRMNL_X

#include <app_meta.h>
#include <apps.h>

#include "menu.h"

#define MENU_APPS_BACK    (-1)
#define MENU_APPS_TIMEOUT (-2)

// Shows the grid of `n` apps (metas[order[0]], ...) under `title` and runs it until a tile is chosen: returns the
// position in `order` (0..n-1), MENU_APPS_BACK for Back, MENU_APPS_TIMEOUT after `timeout_ms` without input.
// A full refresh when it opens and when the page changes; moving the selection inside a page is partial.
int menu_apps_pick(const menu_controls_t &controls, const char *title, const app_meta_t *metas, const int *order, int n,
                   unsigned long timeout_ms);

// Shows a screen of labels (and body lines above them) and returns the chosen index, or -1 on timeout
typedef int (*menu_apps_choose_t)(const char *const *labels, int count, const char *const *body, int body_count,
                                  int selected);

// Reads the display names and icons of the apps in names[] (file names, without .elf) from /apps
void menu_apps_load(app_meta_t *metas, const char names[][32], int n);

// "Open an app": the grid, most recently used first. Returns the app's index in names[] (and remembers it as the most
// recent), MENU_APPS_BACK or MENU_APPS_TIMEOUT.
int menu_apps_open(const menu_controls_t &controls, const app_meta_t *metas, const char names[][32], int n);

// "Remove an app": the grid, a confirmation, the removal (app_remove.h; saved files only after a second
// confirmation). Returns MENU_APPS_BACK when done or cancelled, MENU_APPS_TIMEOUT on a timeout.
int menu_apps_remove(const menu_controls_t &controls, menu_apps_choose_t choose, const app_meta_t *metas,
                     const char names[][32], int n);

#endif
#endif
