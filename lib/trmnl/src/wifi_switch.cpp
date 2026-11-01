#include "wifi_switch.h"

#include <app_api.h>
#include <stdio.h>
#include <string.h>

void wsw_list_init(wsw_list_t *l) { memset(l, 0, sizeof(*l)); }

bool wsw_list_add(wsw_list_t *l, const char *ssid, int8_t rssi, bool open, bool is5, bool enterprise) {
  size_t len = ssid ? strlen(ssid) : 0;
  if (!len || len > WSW_SSID_MAX) return false;
  for (int i = 0; i < l->n; i++) {
    if (strcmp(l->it[i].ssid, ssid) == 0 && l->it[i].is5 == is5) {
      if (rssi <= l->it[i].rssi) return false;
      for (int k = i; k + 1 < l->n; k++) l->it[k] = l->it[k + 1]; // taken out, put back at its new place
      l->n--;
      break;
    }
  }
  int at = l->n;
  while (at > 0 && l->it[at - 1].rssi < rssi) at--;
  if (at >= WSW_LIST_MAX) return false;
  int last = l->n < WSW_LIST_MAX ? l->n : WSW_LIST_MAX - 1;
  for (int k = last; k > at; k--) l->it[k] = l->it[k - 1];
  wsw_net_t *e = &l->it[at];
  memset(e, 0, sizeof(*e));
  memcpy(e->ssid, ssid, len);
  e->rssi = rssi;
  e->open = open;
  e->is5 = is5;
  e->enterprise = enterprise;
  if (l->n < WSW_LIST_MAX) l->n++;
  return true;
}

static int why_is(char *why, size_t n, int rc, const char *text) {
  if (why && n) snprintf(why, n, "%s", text);
  return rc;
}

static bool all_hex(const char *s) {
  for (; *s; s++)
    if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F'))) return false;
  return true;
}

int wsw_check(const wsw_list_t *l, const char *ssid, const char *pass, wsw_join_t *out, char *why, size_t n) {
  if (!l || !ssid || !pass || !out) return why_is(why, n, APP_EINVAL, "missing network or password");
  const wsw_net_t *net = nullptr; // the strongest of that name (a 2.4 and a 5 GHz one: the list is sorted)
  for (int i = 0; i < l->n && !net; i++)
    if (strcmp(l->it[i].ssid, ssid) == 0) net = &l->it[i];
  if (!net) return why_is(why, n, APP_ENOENT, "that network is not in the last scan: scan again");
  if (net->enterprise) return why_is(why, n, APP_EINVAL, "an enterprise network: set it up in the setup portal");
  size_t pl = strlen(pass);
  if (!net->open) {
    if (pl < 8 || pl > 64) return why_is(why, n, APP_EINVAL, "the password has 8 to 63 characters");
    if (pl == 64 && !all_hex(pass)) return why_is(why, n, APP_EINVAL, "a 64-character key is hex digits only");
    for (size_t i = 0; i < pl; i++)
      if ((unsigned char)pass[i] < 0x20 || pass[i] == 0x7f) return why_is(why, n, APP_EINVAL, "the password has a control character");
  } else if (pl) {
    return why_is(why, n, APP_EINVAL, "an open network takes no password");
  }
  memset(out, 0, sizeof(*out));
  memcpy(out->ssid, net->ssid, strlen(net->ssid));
  memcpy(out->pass, pass, pl);
  out->is5 = net->is5;
  return 0;
}

wsw_result_t wsw_switch(wsw_join_t *j, const wsw_ops_t *ops) {
  wsw_result_t r;
  if (ops->join(ops->ctx, j)) {
    ops->save(ops->ctx, j);
    r = WSW_JOINED;
  } else {
    r = ops->rejoin_old(ops->ctx) ? WSW_KEPT_OLD : WSW_NONE;
  }
  volatile char *p = j->pass; // not optimised away: the password leaves RAM
  for (size_t i = 0; i < sizeof(j->pass); i++) p[i] = 0;
  return r;
}
