#pragma once

#include <net_browse.h>
#include <stdint.h>

// net_browse (API 2.10, docs/apps.md "Finding services"): the two network paths. Pure parts in net_browse.h.
// Both fill `out` (at most NB_MAX) and return the count, or an APP_E* code.

// 2.4 GHz network, the S3: mDNS PTR query (service "_ipp", proto "_tcp")
int app_net_browse_s3(const char *service, const char *proto, uint32_t timeout_ms, nb_list_t *out);
// Saved 5 GHz network, the modem (no mDNS query in its AT firmware): /24 port-631 probe, nearest addresses first, then
// IPP Get-Printer-Attributes on each hit. Bounded by timeout_ms
int app_net_browse_modem(uint32_t timeout_ms, nb_list_t *out);
