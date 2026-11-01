#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// net_browse (API 2.10): the pure parts. Result list with de-duplication, cleaning of what a network host sends
// (names and the IPP path), the address walk of the modem fallback and the IPP Get-Printer-Attributes exchange that
// names a printer found by a port probe. Glue: src/app_net_browse.cpp. Test: test_net_browse.

#define NB_MAX 8       // results kept; more are dropped (the count says how many were kept)
#define NB_NAME_MAX 64 // bytes with the NUL
#define NB_HOST_MAX 48
#define NB_PATH_MAX 48

typedef struct {
  char name[NB_NAME_MAX];
  char host[NB_HOST_MAX];
  uint16_t port;
  char path[NB_PATH_MAX]; // TXT rp without a leading slash ("ipp/print"); "" when the service has none
} nb_item_t;

typedef struct {
  nb_item_t item[NB_MAX];
  int n;
} nb_list_t;

void nb_list_init(nb_list_t *l);
// 1: added. 0: the same host and port was there (an empty name or path of the old entry is filled in).
// APP_EINVAL: no host, a host with a character outside [A-Za-z0-9.:-_], port 0. APP_ENOMEM: the list is full.
// The name is cleaned (control characters out, cut to fit); a bad rp is stored as "".
int nb_list_add(nb_list_t *l, const char *name, const char *host, uint16_t port, const char *rp);

// A TXT rp value as a path: no leading slashes, only [A-Za-z0-9._~/-], no "..", at most NB_PATH_MAX - 1 bytes.
// false (out = "") when it is not usable
bool nb_rp_clean(const char *rp, char *out, size_t n);

// The address walk of the modem fallback: addresses of the /24 of `self` in order of distance (self+1, self-1,
// self+2, ...), never self, .0 or .255. *step starts at 0. false when all are done
bool nb_walk_next(uint32_t self, int *step, uint32_t *out);
// "a.b.c.d" of an address in host order; parse back (false on anything else)
void nb_ip_str(uint32_t ip, char *out, size_t n);
bool nb_ip_parse(const char *s, uint32_t *ip);

// The IPP request that asks `path` ("ipp/print") of host:port for printer-make-and-model; its length, 0 if it does not fit
size_t nb_ipp_query(uint8_t *out, size_t cap, const char *host, uint16_t port, const char *path);
// The HTTP request head around it (POST /<path>, Content-Type application/ipp, Content-Length body_len)
size_t nb_ipp_http_head(char *out, size_t cap, const char *host, uint16_t port, const char *path, size_t body_len);
// printer-make-and-model from an IPP response (after the HTTP head); false when it is not there or malformed
bool nb_ipp_model(const uint8_t *resp, size_t len, char *out, size_t n);
// Where an HTTP response's body starts (after the blank line), or 0
size_t nb_http_body_at(const uint8_t *resp, size_t len);

// "_ipp._tcp" into service "_ipp" and proto "_tcp" (each at most 16 bytes with the NUL). false for anything else
bool nb_service_split(const char *svc, char *service, char *proto, size_t n);
