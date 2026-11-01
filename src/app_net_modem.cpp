#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <WifiCaptive.h>
#include <globals.h>
#include <modem.h>
#include <modem_http.h>
#include <string.h>
#include <trmnl_log.h>
#include <wifi_network.h>
#include <wifi_session.h>

#include "app_net_modem.h"
#include "evlog.h"

bool app_net_modem_up(bool *joined, int *rssi) {
  *joined = false;
  *rssi = 0;
  Modem *m = wifiSessionModem();
  if (!m) return false;
  bool up = m->isJoined();
  if (!up) {
    WifiCredentials creds = WifiCaptivePortal.getLastCredentials();
    up = m->connectToNetwork(creds.ssid, creds.pswd, getWifiClientHostname());
    *joined = up;
  }
  if (up) *rssi = (int)m->getSignalRssi();
  return up;
}

static void on_body(void *ctx, const uint8_t *data, size_t len) { modem_resp_feed((modem_resp_t *)ctx, data, len); }
static bool enough(void *ctx) { return modem_resp_enough((const modem_resp_t *)ctx); }

// Apps run one at a time and http_request is not re-entered: one head and one parser state (off the app's stack)
static char head[MODEM_HEAD_MAX];
static modem_resp_t rs;

int app_net_modem_request(const app_http_request_t *req, void *buf, size_t buf_len, app_http_response_t *resp) {
  Modem *m = g_modem;
  const char *method = req->method && req->method[0] ? req->method : "GET";
  uint32_t t = req->timeout_ms ? req->timeout_ms : 15000;
  uint32_t t0 = millis();
  size_t sent = 0;
  modem_url_t u;
  size_t head_len = 0;
  modem_http_fail_t f = MODEM_HTTP_OK;
  modem_resp_init(&rs, buf, buf_len, strcmp(method, "HEAD") == 0);
  if (!m) f = MODEM_HTTP_NO_MODEM;
  else if (!modem_url_parse(req->url, &u)) f = MODEM_HTTP_BAD_URL;
  else if ((head_len = modem_http_head(head, sizeof(head), method, &u, req->headers, req->body_len)) == 0)
    f = MODEM_HTTP_HEAD_TOO_BIG;
  else if (!m->tcpOpen(u.host, u.port, u.tls, t, on_body, &rs)) f = MODEM_HTTP_CONNECT_FAILED;
  if (f == MODEM_HTTP_OK) {
    bool ok = m->tcpSend((const uint8_t *)head, head_len, t);
    const uint8_t *body = (const uint8_t *)req->body;
    size_t c;
    // the body straight from the app's buffer; a server that answers early (413, 401) stops it
    while (ok && body && modem_resp_wants_body(&rs) && (c = modem_send_chunk(sent, req->body_len)) > 0) {
      ok = m->tcpSend(body + sent, c, t);
      if (ok) sent += c;
    }
    Modem::TcpPumpEnd end = m->tcpPump(t, enough, &rs);
    bool complete = end == Modem::TCP_PUMP_ENOUGH || (end == Modem::TCP_PUMP_CLOSED && modem_resp_closed(&rs));
    f = modem_http_outcome(&rs, ok, end == Modem::TCP_PUMP_IDLE);
    if (f == MODEM_HTTP_OK && !complete) Log_info("Net: modem %s: response cut short (%u bytes)", method, (unsigned)rs.got);
    m->tcpClose();
  }
  int rc = modem_http_app_error(f);
  resp->status = rc ? rc : rs.status;
  resp->body_len = rc ? 0 : rs.got;
  resp->truncated = rc ? false : rs.truncated;
  unsigned long ms = millis() - t0;
  if (rc) {
    Log_event("Net: modem %s failed: %s, sent %u, %lu ms", method, modem_http_fail_name(f), (unsigned)sent, ms);
  } else {
    Log_event("Net: modem %s %d, sent %u, got %u%s, %lu ms", method, rs.status, (unsigned)sent, (unsigned)rs.got,
              rs.truncated ? " (truncated)" : "", ms);
  }
  return rc;
}

#endif // BOARD_TRMNL_X
