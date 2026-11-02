#pragma once

#include <Arduino.h>
#include <functional>
#include <modem_at_rx.h>
#include <vector>

// UART pins (ESP32-S3 to ESP32-C5)
#define AT_UART_TX 43
#define AT_UART_RX 44
#define AT_UART_RTS 1
#define AT_UART_CTS 2

class Modem {
private:
  HardwareSerial ModemSerial;
  uint32_t baudRate;
  bool _initialized = false;
  String waitForResponse(const String& expected, unsigned long timeoutMs);

  // TCP/SSL link state (modem_tcp.cpp)
  modem_at_rx_t _rx;
  void (*_sink)(void *ctx, const uint8_t *data, size_t len) = nullptr;
  void *_sinkCtx = nullptr;
  const char *const *_want = nullptr;
  size_t _wantCount = 0;
  int _hit = -1;
  bool _prompt = false, _closed = true;
  char _last[2][48] = {}; // the last two answer lines (tcpLastLines), for a refused command
  size_t tcpRead();
  int tcpWait(const char *const *want, size_t count, uint32_t timeoutMs, bool prompt);
  static void tcpOnLine(void *ctx, const char *line);
  static void tcpOnPrompt(void *ctx);
  static void tcpOnPayload(void *ctx, const uint8_t *data, size_t len);

  // The Wi-Fi page's server (modem_server.cpp): link events and data while it runs, one answer line captured
  void (*_srvEvent)(void *ctx, int link, bool open) = nullptr;
  void (*_srvData)(void *ctx, int link, const uint8_t *data, size_t len) = nullptr;
  void *_srvCtx = nullptr;
  const char *_capPrefix = nullptr;
  char _cap[MODEM_AT_LINE_MAX];
  String *_lines = nullptr; // serverScan: every "+CWLAP:" line
  int _probeLink = -1; // serverProbeOpen: a client link next to the server, its events and data not the page's
  void serverLine(const char *line);

public:
  Modem(uint32_t baudRate);
  ~Modem();
  bool isInitialized() const { return _initialized; }
  bool sendCommand(const char* command);
  String readResponse(unsigned long timeout);
  bool flashFromFile(const char* filename, String& errorOut);
  bool eraseFlash();
  void setSerialBaud(uint32_t baud);

  struct ModemNetwork {
    String  ssid;
    int32_t rssi;
    bool    open;        // true if no encryption (ecn == 0)
    bool    is5GHz;      // true if channel >= 36
    bool    enterprise;  // true if WPA2-Enterprise (ecn == 5)
  };
  std::vector<ModemNetwork> scanNetworks();

  bool connectToNetwork(const String& ssid, const String& password, const String& hostname = "");
  bool disconnectFromNetwork();

  struct ModemHttpResult {
    bool   ok;
    int    statusCode;
    String body;          // populated in text mode (saveToFile empty)
    size_t bytesReceived; // total payload bytes in either mode
  };
  // saveToFile:    empty = text mode (body in String)
  //               non-empty = binary mode (stream to file, body stays empty)
  // contentLength: 0 = unknown (progress shows bytes + speed only)
  //               >0 = enables percentage in progress output
  // reqHeaders:   newline-separated "Key: Value" headers, e.g. "ID: mac\nKey: val"
  //               Set via AT+HTTPCHEAD before the request; cleared afterwards.
  ModemHttpResult httpGet(const String& url, const String& saveToFile = "", size_t contentLength = 0, const String& reqHeaders = "");
  // Chunk-callback overload: chunkCb is called for each received chunk; return false to abort
  ModemHttpResult httpGet(const String& url, std::function<bool(const uint8_t*, size_t)> chunkCb, size_t contentLength = 0, const String& reqHeaders = "", unsigned long timeoutMs = 60000);

  // Configures modem SNTP and returns Unix timestamp (UTC), or 0 on failure.
  time_t getSntpTime();

  // Returns the MAC address of the ESP32-C5 station, or empty string on failure.
  String getMacAddress();

  // Returns the RSSI (dBm) of the currently joined AP, (replaces WiFi.RSSI() on TRMNL_X)
  int32_t getSignalRssi();

  // AT+CWSTATE?: joined and holding an IPv4 address
  bool isJoined();

  // One TCP or SSL link for apps' HTTP (modem_tcp.cpp; pure framing in modem_at_rx.h). While it is open every
  // received byte goes to `sink`. SSL does not verify the server (the AT firmware holds no CA bundle).
  typedef void (*TcpSink)(void *ctx, const uint8_t *data, size_t len);
  // quick: wait only timeoutMs (no 5 s minimum), one try, a connect that got no answer is closed (address probes)
  // The last two lines the modem answered (e.g. "ERR CODE:..." and "ERROR"), for the event log
  void tcpLastLines(char *out, size_t n) const;
  bool tcpOpen(const char *host, uint16_t port, bool tls, uint32_t timeoutMs, TcpSink sink, void *ctx, bool quick = false);
  // The station's IPv4 address in host order (AT+CIPSTA?), 0 when there is none
  uint32_t getIp();
  // One AT+CIPSEND of at most MODEM_SEND_MAX bytes (modem_http.h); true on SEND OK
  bool tcpSend(const uint8_t *data, size_t len, uint32_t timeoutMs);
  enum TcpPumpEnd { TCP_PUMP_IDLE, TCP_PUMP_CLOSED, TCP_PUMP_ENOUGH };
  // Reads until the server closes, `enough(ctx)` or `idleMs` pass without a byte
  TcpPumpEnd tcpPump(uint32_t idleMs, bool (*enough)(void *ctx), void *ctx);
  void tcpClose();

  // The Wi-Fi page's TCP server on a 5 GHz network (modem_server.cpp; the HTTP side is lib/trmnl/ws_msrv.h).
  // AT+CIPMUX=1, AT+CIPSERVERMAXCONN, AT+CIPSERVER=1,<port>, AT+CIPSTO; `ip` gets the station's address.
  // While it runs, "<link>,CONNECT" / "<link>,CLOSED" go to `event` and "+IPD,<link>,<len>:" payloads to `data`.
  typedef void (*SrvEvent)(void *ctx, int link, bool open);
  typedef void (*SrvData)(void *ctx, int link, const uint8_t *data, size_t len);
  bool serverStart(uint16_t port, int maxConn, uint16_t timeoutS, SrvEvent event, SrvData data, void *ctx, char *ip,
                   size_t ipLen);
  bool serverMdns(const char *host, uint16_t port); // AT+MDNS=1,...; false: the AT firmware refused it
  size_t serverPump(); // reads what the UART holds (events and data); the bytes read
  bool serverSend(int link, const uint8_t *data, size_t len, uint32_t timeoutMs); // one AT+CIPSEND=<link>,<len>
  void serverClose(int link);
  void serverStop(); // every link closed, server off, AT+CIPMUX=0 again for tcpOpen
  // AT+CWLAP while the server runs (both bands): the answer goes through the framing, the links keep their data
  bool serverScan(std::vector<ModemNetwork> &out);
  // A client connect on a free `link` while the server runs (printer search): AT+CIPSTART=<link>,... with its own
  // timeout (net_browse.h nb_cipstart); its data goes to `sink`, its CONNECT/CLOSED never to the page. Send with
  // serverSend, read with tcpPump, end with serverProbeClose
  bool serverProbeOpen(int link, const char *host, uint16_t port, uint32_t timeoutMs, TcpSink sink, void *ctx);
  void serverProbeClose();
  uint32_t serverIp(); // AT+CIPSTA? through the framing (getIp reads the UART raw), 0 when none
};
