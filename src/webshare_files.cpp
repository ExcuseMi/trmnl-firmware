// The files profile of the Wi-Fi page (API 2.3, app_api.h "webshare_start_files"): an app's own files of one folder, e.g.
// the Game Boy app's ROMs. Its handlers: lib/trmnl/ws_page_files.cpp; the rules: lib/trmnl/webshare_files.
#ifdef BOARD_TRMNL_X

#include <string.h>

#include <webshare_files.h>
#include <webshare_mode.h>

#include "evlog.h"

#include "webshare_files_page.h"
#include "webshare_internal.h"

const char *webshare_files_page(void) { return WEBSHARE_FILES_PAGE; }

int webshare_start_files(app_webshare_mode_t mode, const app_webshare_files_t *files) {
  ws_files_cfg_t c;
  if (!files || !files->dir || !files->exts || strnlen(files->dir, 64) >= 64 || strnlen(files->exts, 96) >= 96 ||
      !ws_files_cfg_set(&c, files->dir, files->exts, files->max_bytes)) {
    evlog_add("webshare: not started (files): %s", ws_refusal_reason(APP_EINVAL));
    return APP_EINVAL;
  }
  webshare_stop(); // the old page is down before its settings change
  WP.files_cfg = c;
  return webshare_start_profile(mode, WS_PROFILE_FILES);
}

#endif // BOARD_TRMNL_X
