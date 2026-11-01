#ifdef BOARD_TRMNL_X

// A TCP or SSL link through the ESP32-C5's AT firmware (AT+CIPSTART, AT+CIPSEND, active receive with
// "+IPD,<len>:<data>"), for apps' HTTP on a 5 GHz network (src/app_net_modem.cpp). Framing: modem_at_rx.h.

#include <Arduino.h>
#include <string.h>

#include <net_browse.h>

#include "modem.h"

void Modem::tcpOnLine(void *ctx, const char *line) {
  Modem *m = (Modem *)ctx;
  if (m->_srvEvent || m->_capPrefix) m->serverLine(line);
  size_t n = strlen(line);
  bool closed = strcmp(line, "CLOSED") == 0 || (n > 7 && strcmp(line + n - 7, ",CLOSED") == 0);
  if (closed && m->_probeLink < 0) m->_closed = true; // a probe's own link: serverLine (a phone's link is not it)
  if (m->_hit >= 0) return;
  for (size_t i = 0; i < m->_wantCount; i++) {
    if (strcmp(line, m->_want[i]) == 0) {
      m->_hit = (int)i;
      return;
    }
  }
}

void Modem::tcpOnPrompt(void *ctx) { ((Modem *)ctx)->_prompt = true; }

void Modem::tcpOnPayload(void *ctx, const uint8_t *data, size_t len) {
  Modem *m = (Modem *)ctx;
  if (m->_probeLink >= 0 && m->_rx.link == m->_probeLink) {
    if (m->_sink) m->_sink(m->_sinkCtx, data, len);
  } else if (m->_srvData) m->_srvData(m->_srvCtx, m->_rx.link, data, len);
  else if (m->_sink) m->_sink(m->_sinkCtx, data, len);
}

// Feeds what the UART holds through the framing; returns the bytes read
size_t Modem::tcpRead() {
  static const modem_at_sink_t sinkTable = {tcpOnLine, tcpOnPrompt, tcpOnPayload, nullptr};
  modem_at_sink_t sink = sinkTable;
  sink.ctx = this;
  size_t total = 0;
  uint8_t batch[256];
  int avail;
  while ((avail = ModemSerial.available()) > 0) {
    int got = ModemSerial.readBytes(batch, avail < (int)sizeof(batch) ? avail : (int)sizeof(batch));
    if (got <= 0) break;
    modem_at_rx_feed(&_rx, batch, (size_t)got, &sink);
    total += (size_t)got;
  }
  return total;
}

// The index of the first line in `want` that arrives, `count` for the ">" prompt (when `prompt`), -1 on timeout
int Modem::tcpWait(const char *const *want, size_t count, uint32_t timeoutMs, bool prompt) {
  _want = want;
  _wantCount = count;
  _hit = -1;
  _prompt = false;
  uint32_t t0 = millis();
  int rc = -1;
  for (;;) {
    size_t n = tcpRead();
    if (prompt && _prompt) {
      rc = (int)count;
      break;
    }
    if (_hit >= 0) {
      rc = _hit;
      break;
    }
    if (millis() - t0 >= timeoutMs) break;
    if (n == 0) delay(1);
  }
  _want = nullptr;
  _wantCount = 0;
  return rc;
}

bool Modem::isJoined() {
  while (ModemSerial.available()) ModemSerial.read();
  sendCommand("AT+CWSTATE?");
  String resp = waitForResponse("OK", 3000);
  int idx = resp.indexOf("+CWSTATE:");
  return idx >= 0 && resp.charAt(idx + 9) == '2'; // 2: connected, IPv4 address obtained
}

// The station's IPv4 address in host order (AT+CIPSTA?), 0 when there is none
uint32_t Modem::getIp() {
  while (ModemSerial.available()) ModemSerial.read();
  sendCommand("AT+CIPSTA?");
  String resp = waitForResponse("OK", 3000);
  int idx = resp.indexOf("+CIPSTA:ip:\"");
  if (idx < 0) return 0;
  idx += 12;
  int end = resp.indexOf('"', idx);
  uint32_t ip = 0;
  if (end < 0 || end - idx > 15) return 0;
  String s = resp.substring(idx, end);
  return nb_ip_parse(s.c_str(), &ip) ? ip : 0;
}

bool Modem::tcpOpen(const char *host, uint16_t port, bool tls, uint32_t timeoutMs, TcpSink sink, void *ctx, bool quick) {
  static const char *const okErr[] = {"OK", "ERROR"};
  static const char *const start[] = {"OK", "ERROR", "ALREADY CONNECTED"};
  while (ModemSerial.available()) ModemSerial.read();
  modem_at_rx_init(&_rx);
  _sink = nullptr;
  _closed = false;
  sendCommand("AT+CIPMUX=0");
  tcpWait(okErr, 2, 2000, false);
  sendCommand("AT+CIPDINFO=0"); // "+IPD,<len>:" without the remote address
  tcpWait(okErr, 2, 2000, false);
  char cmd[MODEM_AT_LINE_MAX + 64];
  if (tls) {
    snprintf(cmd, sizeof(cmd), "AT+CIPSSLCSNI=\"%s\"", host); // host has no quote or backslash (modem_url_parse)
    sendCommand(cmd);
    tcpWait(okErr, 2, 2000, false);
  }
  _sink = sink;
  _sinkCtx = ctx;
  snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"%s\",\"%s\",%u", tls ? "SSL" : "TCP", host, (unsigned)port);
  uint32_t wait = (timeoutMs < 5000 && !quick) ? 5000 : timeoutMs;
  for (int attempt = 0; attempt < (quick ? 1 : 2); attempt++) {
    _closed = false;
    sendCommand(cmd);
    int rc = tcpWait(start, 3, wait, false);
    if (rc == 0) return true;
    if (rc != 2) {
      if (rc < 0) { // no answer in time (quick probe of an address nobody has): drop the pending connect
        sendCommand("AT+CIPCLOSE");
        tcpWait(okErr, 2, 1000, false);
      }
      break;
    }
    Serial.println("[MODEM] tcpOpen: a link was still open, closing it");
    sendCommand("AT+CIPCLOSE");
    tcpWait(okErr, 2, 3000, false);
  }
  if (!quick) Serial.printf("[MODEM] tcpOpen %s:%u (%s) failed\n", host, (unsigned)port, tls ? "SSL" : "TCP");
  _sink = nullptr;
  _closed = true;
  return false;
}

bool Modem::tcpSend(const uint8_t *data, size_t len, uint32_t timeoutMs) {
  static const char *const refused[] = {"ERROR", "link is not valid"};
  static const char *const sent[] = {"SEND OK", "SEND FAIL", "ERROR"};
  if (_closed || len == 0) return len == 0;
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)len);
  sendCommand(cmd);
  if (tcpWait(refused, 2, 5000, true) != 2) return false; // no ">"
  size_t off = 0;
  uint32_t t0 = millis();
  while (off < len) { // the UART's TX buffer takes what fits; RTS/CTS paces it
    size_t n = ModemSerial.write(data + off, len - off);
    off += n;
    tcpRead(); // keep the RX side drained while the server answers early
    if (n == 0) {
      if (millis() - t0 >= timeoutMs) return false;
      delay(1);
    }
  }
  return tcpWait(sent, 3, timeoutMs, false) == 0;
}

Modem::TcpPumpEnd Modem::tcpPump(uint32_t idleMs, bool (*enough)(void *ctx), void *ctx) {
  uint32_t last = millis();
  for (;;) {
    size_t n = tcpRead();
    if (enough && enough(ctx)) return TCP_PUMP_ENOUGH;
    if (_closed) return TCP_PUMP_CLOSED;
    uint32_t now = millis();
    if (n > 0) last = now;
    else if (now - last >= idleMs) return TCP_PUMP_IDLE;
    else delay(1);
  }
}

void Modem::tcpClose() {
  static const char *const okErr[] = {"OK", "ERROR"};
  _sink = nullptr; // what still arrives is dropped
  if (!_closed) {
    sendCommand("AT+CIPCLOSE");
    tcpWait(okErr, 2, 3000, false);
  }
  _closed = true;
}

#endif // BOARD_TRMNL_X
