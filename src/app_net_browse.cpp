#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <app_api.h>
#include <esp_heap_caps.h>
#include <globals.h>
#include <mdns.h>
#include <modem.h>
#include <string.h>
#include <trmnl_log.h>
#include <x_devset.h>

#include "app_net_browse.h"
#include "evlog.h"
#include "x_devset_x.h"

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

// One address: a connect with its own short timeout, then Get-Printer-Attributes; a port 631 that speaks HTTP is a
// print server, its model, else its address, names it
static void probe(Modem *m, int link, uint32_t ip, uint32_t wait, nb_list_t *out, bool tell) {
  char host[16];
  nb_ip_str(ip, host, sizeof(host));
  reply_n = 0;
  uint32_t t0 = millis();
  bool open = link >= 0 ? m->serverProbeOpen(link, host, 631, wait, on_reply, nullptr)
                        : m->tcpOpen(host, 631, false, wait, on_reply, nullptr, true);
  if (tell) { // the first probes' answers: a connect refused at once shows why (2026-11-02: 253 in 2.7 s)
    char last[104];
    m->tcpLastLines(last, sizeof(last));
    evlog_add("Net: probe %s %s after %lu ms, modem: %s", host, open ? "open" : "no", (unsigned long)(millis() - t0),
              last);
  }
  if (!open) return;
  uint8_t q[256];
  char head[256];
  size_t ql = nb_ipp_query(q, sizeof(q), host, 631, "ipp/print");
  size_t hl = ql ? nb_ipp_http_head(head, sizeof(head), host, 631, "ipp/print", ql) : 0;
  char model[NB_NAME_MAX] = "";
  bool sent = hl && (link >= 0 ? m->serverSend(link, (const uint8_t *)head, hl, 3000) && m->serverSend(link, q, ql, 3000)
                               : m->tcpSend((const uint8_t *)head, hl, 3000) && m->tcpSend(q, ql, 3000));
  if (sent) {
    m->tcpPump(2000, have_body, nullptr);
    size_t b = nb_http_body_at(reply, reply_n);
    if (b) nb_ipp_model(reply + b, reply_n - b, model, sizeof(model));
  }
  bool http = reply_n >= 5 && memcmp(reply, "HTTP/", 5) == 0;
  if (link >= 0) m->serverProbeClose();
  else m->tcpClose();
  if (model[0] || http) nb_list_add(out, model, host, 631, "ipp/print");
}

// The saved default printer's address first (it is found even when the walk runs out of time), then the /24 outward
// from the X; one connect at a time (ESP-AT's parser runs one AT+CIPSTART), each with its own timeout (nb_probe_ms)
static int walk(Modem *m, int link, uint32_t timeout_ms, nb_list_t *out) {
  uint32_t self = link >= 0 ? m->serverIp() : m->getIp();
  if (!self) return APP_ENONET;
  uint32_t t0 = millis(), probed = 0, wait = nb_probe_ms(timeout_ms), saved = 0, last = 0;
  char cfg[XDS_VALUE_MAX + 1];
  int cn = x_devset_get("printer", cfg, sizeof(cfg));
  if (cn > 0 && nb_cfg_host_ip(cfg, (size_t)cn, &saved) && saved != self) {
    probed++;
    probe(m, link, saved, wait, out, true);
  } else {
    saved = 0;
  }
  int step = 0;
  uint32_t ip;
  while (out->n < STOP_AFTER && millis() - t0 < timeout_ms && nb_walk_next(self, &step, &ip)) {
    if (ip == saved) continue;
    probed++;
    last = ip;
    probe(m, link, ip, wait, out, probed <= 3);
  }
  char me[16], to[16], sv[16];
  nb_ip_str(self, me, sizeof(me));
  nb_ip_str(last, to, sizeof(to));
  nb_ip_str(saved, sv, sizeof(sv));
  Log_event("Net: browse modem%s: %u probed (%u ms each) from %s to %s, saved %s, %d found, %lu ms",
            link >= 0 ? " beside the page" : "", (unsigned)probed, (unsigned)wait, me, to, saved ? sv : "none", out->n,
            (unsigned long)(millis() - t0));
  return out->n;
}

int app_net_browse_modem(uint32_t timeout_ms, nb_list_t *out) { return g_modem ? walk(g_modem, -1, timeout_ms, out) : APP_ENONET; }

int app_net_browse_modem_beside(int link, uint32_t timeout_ms, nb_list_t *out) {
  return g_modem && link >= 0 ? walk(g_modem, link, timeout_ms, out) : APP_ENONET;
}

#endif // BOARD_TRMNL_X
