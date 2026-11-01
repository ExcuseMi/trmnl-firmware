#pragma once

#include <app_api.h>

// Apps' network on a saved 5 GHz network: the ESP32-C5 modem joins it and carries http_request
// (docs/apps.md "net"; pure parts in modem_http.h and radio_share.h).

// True when the modem is on the network; *joined: it had to join now. *rssi: the joined AP's signal.
bool app_net_modem_up(bool *joined, int *rssi);
// The same contract as the S3 path's http_request; call after app_net_modem_up
int app_net_modem_request(const app_http_request_t *req, void *buf, size_t buf_len, app_http_response_t *resp);
