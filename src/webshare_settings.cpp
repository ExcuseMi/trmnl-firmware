// The settings profile of the Wi-Fi page (API 2.6, app_api.h "webshare_start_settings"). Its handlers and the keyboard
// layout every profile's page shows: lib/trmnl/ws_page_settings.cpp and ws_page_core.cpp.
#ifdef BOARD_TRMNL_X

#include "app_settings_x.h"
#include "webshare_internal.h"
#include "webshare_settings_page.h"

const char *webshare_settings_page(void) { return WEBSHARE_SETTINGS_PAGE; }

int webshare_start_settings(app_webshare_mode_t mode) {
  if (!app_settings_x_declared()) return APP_ENOENT;
  return webshare_start_profile(mode, WS_PROFILE_SETTINGS);
}

#endif // BOARD_TRMNL_X
