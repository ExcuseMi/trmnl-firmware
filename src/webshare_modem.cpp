// The Wi-Fi page on a saved 5 GHz network: the S3's radio is 2.4 GHz only, so the ESP32-C5 modem runs the TCP
// server (Modem::server*) and this task turns its AT traffic into ws_msrv calls (lib/trmnl/ws_msrv.h): the same
// handlers as on AsyncWebServer. One task owns the modem's UART while the page runs (apps' http_request waits).
#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <globals.h>
#include <modem.h>
#include <trmnl_log.h>
#include <net_browse.h>
#include <ws_msrv.h>

#include "evlog.h"
#include "webshare_internal.h"

#define WS_MODEM_PORT 80
#define WS_MODEM_TIMEOUT_S 30 // AT+CIPSTO: the modem drops a link idle this long
#define WS_MODEM_SEND_MS 10000

static ws_msrv_t srv;
static volatile bool alive; // the task runs (or cleans up)
static bool sending; // inside Modem::serverSend: a close waits until it returns
static uint32_t pending_close; // links to close once the send is done

static void on_event(void *, int link, bool open) {
  if (open) ws_msrv_connect(&srv, link);
  else ws_msrv_closed(&srv, link);
}

static void on_data(void *, int link, const uint8_t *data, size_t len) { ws_msrv_data(&srv, link, data, len); }

static bool t_send(void *, int link, const uint8_t *data, size_t len) {
  sending = true;
  bool ok = g_modem->serverSend(link, data, len, WS_MODEM_SEND_MS);
  sending = false;
  return ok;
}

static void t_close(void *, int link) {
  if (sending && link >= 0 && link < 32) pending_close |= 1u << link; // a command inside a command's wait: later
  else g_modem->serverClose(link);
}

static uint32_t t_now(void *) { return millis(); }

static const ws_mtransport_t T = {t_send, t_close, t_now, nullptr};

static void server_task(void *) {
  while (WP.running) {
    size_t n = g_modem->serverPump();
    bool more = ws_msrv_poll(&srv);
    for (int i = 0; pending_close && i < 32; i++) {
      if (pending_close & (1u << i)) {
        pending_close &= ~(1u << i);
        g_modem->serverClose(i);
      }
    }
    if (!n && !more) vTaskDelay(pdMS_TO_TICKS(2));
  }
  ws_msrv_end(&srv); // a half upload's temporary file goes with its link
  g_modem->serverStop();
  Log_info("webshare: modem server off (%u links refused)", (unsigned)srv.refused);
  alive = false;
  vTaskDelete(nullptr);
}

bool webshare_modem_alive(void) { return alive; }

int webshare_modem_free_link(void) {
  uint32_t used = 0;
  for (int i = 0; i < WS_MSRV_LINKS; i++)
    if (srv.links[i]) used |= 1u << i;
  return nb_free_link(used, WS_MSRV_LINKS);
}

bool webshare_modem_owns_uart(void) { return alive; }

int webshare_modem_start(const char *host, char *ip, size_t ip_len, bool *mdns_ok) {
  *mdns_ok = false;
  if (!g_modem) return APP_ENONET;
  if (alive) return APP_EBUSY;
  ws_msrv_init(&srv, &T);
  pending_close = 0;
  if (!g_modem->serverStart(WS_MODEM_PORT, WS_MSRV_LINKS, WS_MODEM_TIMEOUT_S, on_event, on_data, nullptr, ip, ip_len)) {
    Log_error("webshare: the modem refused the server");
    return APP_ENONET;
  }
  *mdns_ok = g_modem->serverMdns(host, WS_MODEM_PORT);
  if (!*mdns_ok) Log_info("webshare: modem mDNS refused, the X shows the IP address");
  alive = true;
  // the handlers' JSON documents and the settings form copy live on the heap; 8 KB of stack is plenty
  if (xTaskCreate(server_task, "wsmodem", 8192, nullptr, 2, nullptr) != pdPASS) {
    alive = false;
    g_modem->serverStop();
    return APP_ENOMEM;
  }
  evlog_add("webshare: on the 5 GHz network through the modem, %s", ip);
  return 0;
}

#endif // BOARD_TRMNL_X
