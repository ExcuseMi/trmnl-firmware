#pragma once
#ifdef BOARD_TRMNL_X
// "TAPP NVS": read only. The NVS totals, then per namespace its entries, then every key with its type and size
// (owner's X 2026-11-02: "could not save every value (flash full?)"), then "TAPP NVS END". True for that line.
bool nvs_stats_serial_command(const char *line);
// One event log line per boot: used and free entries, failed writes so far
void nvs_stats_boot_log(void);
#endif
