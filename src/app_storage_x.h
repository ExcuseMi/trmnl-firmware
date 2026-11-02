#pragma once
#ifdef BOARD_TRMNL_X

#include <stddef.h>
#include <stdint.h>

// The X's storage for apps (lib/trmnl/include/app_space.h on LittleFS): the serial "TAPP DF" / "TAPP LS", the line at
// boot in the event log, the Apps > Storage screen's text and its "Free space" action.

// "TAPP DF" (total, used, free bytes) and "TAPP LS" (every file with its size, then "TAPP LS END"); false otherwise
bool app_storage_serial_command(const char *line);

// One event log line with the apps and their sizes (a boot that was not a deep sleep wake)
void app_storage_boot_log(void);

// "Storage: 9.1 MB used, 820 KB free" into `out`
void app_storage_line(char *out, size_t len);

// Removes the .bak copies and a stale upload temp file; returns the bytes freed
uint64_t app_storage_free_space(void);

#endif
