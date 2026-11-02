#pragma once
// The X's log output on the USB serial port, held back while a cable upload runs (serial_hold.h)
#include <Print.h>

// What Log (ArduinoLog) and the serial-only log lines write to: Serial, unless an upload holds the port
Print &serial_log_out(void);

// ESP-IDF's own log lines (ESP_LOGx, the secondary console on the USB port) follow the same hold; call once at boot
void serial_log_hold_idf(void);
