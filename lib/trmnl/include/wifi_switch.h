#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Changing the X's Wi-Fi network from its phone page (docs/apps.md "The X's page", This X > Wi-Fi). Pure: the
// checks of what the page sends and the order of the switch. The page closes first (the phone loses it either way);
// the new network is saved only after the X joined it, so a failed join leaves the old one saved and in use.

#define WSW_SSID_MAX 32
#define WSW_PASS_MAX 64
#define WSW_LIST_MAX 16

typedef struct {
  char ssid[WSW_SSID_MAX + 1];
  int8_t rssi;
  bool open, is5, enterprise;
} wsw_net_t;

typedef struct {
  wsw_net_t it[WSW_LIST_MAX];
  int n;
} wsw_list_t;

typedef struct {
  char ssid[WSW_SSID_MAX + 1];
  char pass[WSW_PASS_MAX + 1];
  bool is5;
} wsw_join_t;

void wsw_list_init(wsw_list_t *l);
// Adds a scanned network; the same name and band once (the stronger kept), no empty names, sorted strongest first.
// false when it was not added (empty, too long, full and weaker than all)
bool wsw_list_add(wsw_list_t *l, const char *ssid, int8_t rssi, bool open, bool is5, bool enterprise);

// What the page sends for Join, against the last scan `l`: the network must be in it (its band and security come
// from there), not enterprise (set that up in the setup portal), a password of 8..63 characters or 64 hex digits
// unless open. 0 with *out filled, or APP_EINVAL / APP_ENOENT with `why` (the password is never copied into it)
int wsw_check(const wsw_list_t *l, const char *ssid, const char *pass, wsw_join_t *out, char *why, size_t why_len);

typedef struct {
  bool (*join)(void *ctx, const wsw_join_t *j); // try the new network: true once it has an address
  void (*save)(void *ctx, const wsw_join_t *j); // keep it as the network in use (the old one stays saved too)
  bool (*rejoin_old)(void *ctx); // back to the saved network
  void *ctx;
} wsw_ops_t;

typedef enum { WSW_JOINED, WSW_KEPT_OLD, WSW_NONE } wsw_result_t;
// Tries `j`; saves it only when joined, else goes back to the old network. The password in `j` is wiped after.
// WSW_NONE: neither the new nor the old one came up (the old one stays saved: the next wake tries it again)
wsw_result_t wsw_switch(wsw_join_t *j, const wsw_ops_t *ops);
