#pragma once

#include <stddef.h>
#include <stdint.h>

// The Wi-Fi page's name for the device itself (not for an app): the Wi-Fi host name TRMNL already uses
// ("TRMNL-X-BJRLAT"), made safe for an access point SSID and an mDNS host.
#define WS_HOST_MAX 31 // characters of the host / SSID, without the NUL (an SSID may be 32)

// `name`: the device's Wi-Fi host name, or empty. `fallback`: used when `name` is empty or has no characters left
// ("TRMNL-X"). `mac`: 6 bytes; when the result is only the fallback (no device ID in it) its last 3 bytes are
// appended as hex, so two devices never share a name. ssid: letters, digits and '-' only, cut to WS_HOST_MAX, as
// given (capitals kept); host: the same, lowercase. Both buffers WS_HOST_MAX + 1 bytes.
void ws_host_names(const char *name, const char *fallback, const uint8_t mac[6], char *ssid, char *host);
