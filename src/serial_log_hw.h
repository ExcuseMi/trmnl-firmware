#pragma once
// The X's log output on the USB serial port, held back while a cable upload runs (serial_hold.h)
#include <Print.h>

// What Log (ArduinoLog) and the serial-only log lines write to: Serial through console_out.h (never waits on a PC
// that does not read), unless an upload holds the port
Print &serial_log_out(void);

// ESP-IDF's own log lines (ESP_LOGx, the secondary console on the USB port) follow the same hold; call once at boot
void serial_log_hold_idf(void);

// The serial watchdog's poll: moves log text that waited for a reader, and once it all went out logs what was
// dropped meanwhile ("console: N bytes dropped while nobody read", event log too). Never waits
void serial_log_pump(void);
