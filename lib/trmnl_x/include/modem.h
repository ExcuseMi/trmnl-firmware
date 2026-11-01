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
  size_t tcpRead();
  int tcpWait(const char *const *want, size_t count, uint32_t timeoutMs, bool prompt);
  static void tcpOnLine(void *ctx, const char *line);
  static void tcpOnPrompt(void *ctx);
  static void tcpOnPayload(void *ctx, const uint8_t *data, size_t len);

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
  bool tcpOpen(const char *host, uint16_t port, bool tls, uint32_t timeoutMs, TcpSink sink, void *ctx);
  // One AT+CIPSEND of at most MODEM_SEND_MAX bytes (modem_http.h); true on SEND OK
  bool tcpSend(const uint8_t *data, size_t len, uint32_t timeoutMs);
  enum TcpPumpEnd { TCP_PUMP_IDLE, TCP_PUMP_CLOSED, TCP_PUMP_ENOUGH };
  // Reads until the server closes, `enough(ctx)` or `idleMs` pass without a byte
  TcpPumpEnd tcpPump(uint32_t idleMs, bool (*enough)(void *ctx), void *ctx);
  void tcpClose();
};
