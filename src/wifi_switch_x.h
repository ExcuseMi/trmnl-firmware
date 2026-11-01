#pragma once

#include <wifi_switch.h>

// The phone page's Wi-Fi Join, after the page has closed (wifi_switch_x.cpp): tries `j` (its password wiped after)
wsw_result_t wifi_switch_x(wsw_join_t *j);
