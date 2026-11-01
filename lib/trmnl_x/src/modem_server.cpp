#ifdef BOARD_TRMNL_X

// The Wi-Fi page's TCP server through the ESP32-C5's AT firmware (ESP-AT v5.1: AT+CIPMUX=1, AT+CIPSERVER, active
// receive "+IPD,<link>,<len>:<data>" paced by the UART's RTS/CTS), for a saved 5 GHz network the S3 cannot join.
// The HTTP side: lib/trmnl/ws_msrv.h, run by src/webshare_modem.cpp. Framing: modem_at_rx.h.

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "modem.h"

static const char *const OK_ERR[] = {"OK", "ERROR"};

// "<link>,CONNECT" / "<link>,CLOSED" (also "<link>,CONNECT FAIL"), and the line a command's answer starts with
void Modem::serverLine(const char *line) {
  if (_capPrefix && strncmp(line, _capPrefix, strlen(_capPrefix)) == 0) {
    snprintf(_cap, sizeof(_cap), "%s", line + strlen(_capPrefix));
  }
  if (!_srvEvent || line[0] < '0' || line[0] > '9') return;
  char *end;
  long link = strtol(line, &end, 10);
  if (*end != ',' || link > 31) return;
  if (strcmp(end, ",CONNECT") == 0) _srvEvent(_srvCtx, (int)link, true);
  else if (strcmp(end, ",CLOSED") == 0 || strcmp(end, ",CONNECT FAIL") == 0) _srvEvent(_srvCtx, (int)link, false);
}

bool Modem::serverStart(uint16_t port, int maxConn, uint16_t timeoutS, SrvEvent event, SrvData data, void *ctx, char *ip,
                        size_t ipLen) {
  char cmd[64];
  while (ModemSerial.available()) ModemSerial.read();
  modem_at_rx_init(&_rx);
  _sink = nullptr;
  ip[0] = '\0';
  sendCommand("AT+CIPSERVER=0,1"); // a server left by a crash: off, its links closed (ERROR when none: fine)
  tcpWait(OK_ERR, 2, 3000, false);
  sendCommand("AT+CIPCLOSE"); // an app's client link (CIPMUX=0) would refuse CIPMUX=1
  tcpWait(OK_ERR, 2, 3000, false);
  sendCommand("AT+CIPMUX=1");
  if (tcpWait(OK_ERR, 2, 2000, false) != 0) {
    Serial.println("[MODEM] serverStart: AT+CIPMUX=1 refused");
    return false;
  }
  snprintf(cmd, sizeof(cmd), "AT+CIPSERVERMAXCONN=%d", maxConn);
  sendCommand(cmd);
  tcpWait(OK_ERR, 2, 2000, false); // the default (5) is fine when refused
  sendCommand("AT+CIPDINFO=0"); // "+IPD,<link>,<len>:" without the remote address
  tcpWait(OK_ERR, 2, 2000, false);
  _srvEvent = event;
  _srvData = data;
  _srvCtx = ctx;
  snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%u", (unsigned)port);
  sendCommand(cmd);
  if (tcpWait(OK_ERR, 2, 3000, false) != 0) {
    Serial.println("[MODEM] serverStart: AT+CIPSERVER refused");
    serverStop();
    return false;
  }
  snprintf(cmd, sizeof(cmd), "AT+CIPSTO=%u", (unsigned)timeoutS); // the modem drops a link idle this long
  sendCommand(cmd);
  tcpWait(OK_ERR, 2, 2000, false);
  _capPrefix = "+CIPSTA:ip:\"";
  _cap[0] = '\0';
  sendCommand("AT+CIPSTA?");
  tcpWait(OK_ERR, 2, 2000, false);
  _capPrefix = nullptr;
  char *q = strchr(_cap, '"');
  if (q) *q = '\0';
  snprintf(ip, ipLen, "%s", _cap);
  Serial.printf("[MODEM] server on port %u, %s\n", (unsigned)port, ip);
  return ip[0] != '\0';
}

bool Modem::serverMdns(const char *host, uint16_t port) {
  char cmd[96];
  sendCommand("AT+MDNS=0");
  tcpWait(OK_ERR, 2, 2000, false);
  snprintf(cmd, sizeof(cmd), "AT+MDNS=1,\"%s\",\"_http\",%u", host, (unsigned)port);
  sendCommand(cmd);
  return tcpWait(OK_ERR, 2, 5000, false) == 0;
}

size_t Modem::serverPump() { return tcpRead(); }

bool Modem::serverSend(int link, const uint8_t *data, size_t len, uint32_t timeoutMs) {
  static const char *const refused[] = {"ERROR", "link is not valid"};
  static const char *const sent[] = {"SEND OK", "SEND FAIL", "ERROR"};
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%u", link, (unsigned)len);
  sendCommand(cmd);
  if (tcpWait(refused, 2, 5000, true) != 2) return false; // no ">": the link is gone
  size_t off = 0;
  uint32_t t0 = millis();
  while (off < len) { // the UART's TX buffer takes what fits; RTS/CTS paces it
    size_t n = ModemSerial.write(data + off, len - off);
    off += n;
    tcpRead();
    if (n == 0) {
      if (millis() - t0 >= timeoutMs) return false;
      delay(1);
    }
  }
  return tcpWait(sent, 3, timeoutMs, false) == 0;
}

void Modem::serverClose(int link) {
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d", link);
  sendCommand(cmd);
  tcpWait(OK_ERR, 2, 3000, false);
}

void Modem::serverStop() {
  _srvEvent = nullptr;
  _srvData = nullptr;
  sendCommand("AT+MDNS=0");
  tcpWait(OK_ERR, 2, 2000, false);
  sendCommand("AT+CIPSERVER=0,1"); // off, every link closed
  tcpWait(OK_ERR, 2, 5000, false);
  sendCommand("AT+CIPMUX=0");
  tcpWait(OK_ERR, 2, 2000, false);
}

#endif // BOARD_TRMNL_X
