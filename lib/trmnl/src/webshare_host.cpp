#include "webshare_host.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static size_t clean(const char *in, char *out) {
  size_t n = 0;
  for (; in && *in && n < WS_HOST_MAX; in++) {
    unsigned char c = (unsigned char)*in;
    if (isalnum(c) || c == '-') out[n++] = (char)c;
    else if (c == '_' || c == ' ') out[n++] = '-';
  }
  while (n > 0 && out[n - 1] == '-') n--; // a label does not end in '-'
  size_t s = 0;
  while (s < n && out[s] == '-') s++; // nor start with one
  if (s) memmove(out, out + s, n - s);
  out[n - s] = '\0';
  return n - s;
}

void ws_host_names(const char *name, const char *fallback, const uint8_t mac[6], char *ssid, char *host) {
  char fb[WS_HOST_MAX + 1];
  clean(fallback, fb);
  if (fb[0] == '\0') strcpy(fb, "device");
  size_t n = clean(name, ssid);
  if (n == 0) strcpy(ssid, fb), n = strlen(ssid);
  // only the board's prefix: nothing tells devices apart, so the MAC does
  if (strcasecmp(ssid, fb) == 0) {
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "-%02X%02X%02X", mac[3], mac[4], mac[5]);
    if (n + 7 > WS_HOST_MAX) n = WS_HOST_MAX - 7;
    snprintf(ssid + n, WS_HOST_MAX + 1 - n, "%s", suffix);
  }
  size_t i = 0;
  for (; ssid[i]; i++) host[i] = (char)tolower((unsigned char)ssid[i]);
  host[i] = '\0';
}
