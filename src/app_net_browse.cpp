#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <app_api.h>
#include <esp_heap_caps.h>
#include <globals.h>
#include <mdns.h>
#include <modem.h>
#include <string.h>
#include <trmnl_log.h>

#include "app_net_browse.h"
#include "evlog.h"

int app_net_browse_s3(const char *service, const char *proto, uint32_t timeout_ms, nb_list_t *out) {
  esp_err_t e = mdns_init(); // the Wi-Fi code starts one with the device's host name; ours only when none runs
  bool own = e == ESP_OK;
  if (!own && e != ESP_ERR_INVALID_STATE) return e == ESP_ERR_NO_MEM ? APP_ENOMEM : APP_EIO;
  mdns_result_t *res = nullptr;
  e = mdns_query_ptr(service, proto, timeout_ms, NB_MAX * 2, &res);
  int rc = 0;
  if (e == ESP_ERR_NO_MEM) rc = APP_ENOMEM;
  else if (e != ESP_OK) rc = APP_EIO;
  for (mdns_result_t *r = res; r && !rc; r = r->next) {
    char ip[16] = "";
    for (mdns_ip_addr_t *a = r->addr; a && !ip[0]; a = a->next)
      if (a->addr.type == ESP_IPADDR_TYPE_V4) esp_ip4addr_ntoa(&a->addr.u_addr.ip4, ip, sizeof(ip));
    if (!ip[0] && r->hostname) { // no address in the answer: ask for it once, briefly
      esp_ip4_addr_t a4;
      if (mdns_query_a(r->hostname, 1500, &a4) == ESP_OK) esp_ip4addr_ntoa(&a4, ip, sizeof(ip));
    }
    const char *rp = "";
    for (size_t i = 0; i < r->txt_count; i++)
      if (r->txt[i].key && r->txt[i].value && strcmp(r->txt[i].key, "rp") == 0) rp = r->txt[i].value;
    if (ip[0]) nb_list_add(out, r->instance_name ? r->instance_name : "", ip, r->port, rp); // full list: dropped
  }
  if (res) mdns_query_results_free(res);
  if (own) mdns_free();
  return rc ? rc : out->n;
}

// ---- the modem path ----

#define PROBE_MS 300       // wait for a connect to one address
#define STOP_AFTER 4       // printers found: enough
#define IPP_REPLY_MAX 1400 // the head and attributes of the reply; the rest is not read

static uint8_t reply[IPP_REPLY_MAX]; // apps run one at a time and net_browse is not re-entered
static size_t reply_n;
static void on_reply(void *, const uint8_t *d, size_t n) {
  size_t c = n < sizeof(reply) - reply_n ? n : sizeof(reply) - reply_n;
  memcpy(reply + reply_n, d, c);
  reply_n += c;
}
static bool have_body(void *) {
  size_t b = nb_http_body_at(reply, reply_n);
  return b && reply_n >= b + 8 + 80; // the IPP header and the first attributes are in
}

int app_net_browse_modem(uint32_t timeout_ms, nb_list_t *out) {
  Modem *m = g_modem;
  if (!m) return APP_ENONET;
  uint32_t self = m->getIp();
  if (!self) return APP_ENONET;
  uint32_t t0 = millis(), probed = 0;
  int step = 0;
  uint32_t ip;
  while (out->n < STOP_AFTER && millis() - t0 < timeout_ms && nb_walk_next(self, &step, &ip)) {
    char host[16];
    nb_ip_str(ip, host, sizeof(host));
    probed++;
    reply_n = 0;
    if (!m->tcpOpen(host, 631, false, PROBE_MS, on_reply, nullptr, true)) continue;
    uint8_t q[256];
    char head[256];
    size_t ql = nb_ipp_query(q, sizeof(q), host, 631, "ipp/print");
    size_t hl = ql ? nb_ipp_http_head(head, sizeof(head), host, 631, "ipp/print", ql) : 0;
    char model[NB_NAME_MAX] = "";
    if (hl && m->tcpSend((const uint8_t *)head, hl, 3000) && m->tcpSend(q, ql, 3000)) {
      m->tcpPump(2000, have_body, nullptr);
      size_t b = nb_http_body_at(reply, reply_n);
      if (b) nb_ipp_model(reply + b, reply_n - b, model, sizeof(model));
    }
    bool http = reply_n >= 5 && memcmp(reply, "HTTP/", 5) == 0;
    m->tcpClose();
    // a port 631 that speaks HTTP is a print server; its model, else its address, names it
    if (model[0] || http) nb_list_add(out, model, host, 631, "ipp/print");
  }
  Log_event("Net: browse modem: %u addresses probed, %d found, %lu ms", (unsigned)probed, out->n,
            (unsigned long)(millis() - t0));
  return out->n;
}

#endif // BOARD_TRMNL_X
