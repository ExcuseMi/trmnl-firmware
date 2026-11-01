// The apps profile of the Wi-Fi page ("Install via Wi-Fi" in the Apps menu). Its handlers:
// lib/trmnl/ws_page_apps.cpp; the checks and the .bak bookkeeping: lib/trmnl/webshare_apps + app_install.
#ifdef BOARD_TRMNL_X

#include "webshare_install_page.h"
#include "webshare_internal.h"

const char *webshare_install_page(void) { return WEBSHARE_INSTALL_PAGE; }

int webshare_start_install(void) { return webshare_start_profile(APP_WEBSHARE_AUTO, WS_PROFILE_INSTALL); }

#endif // BOARD_TRMNL_X
