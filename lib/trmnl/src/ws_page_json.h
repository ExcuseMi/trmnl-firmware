#pragma once

// Private to ws_page_*.cpp: a JSON document as the answer
#include <ArduinoJson.h>

#include "ws_page.h"

void ws_send_doc(WsRequest &r, int code, const JsonDocument &d);
