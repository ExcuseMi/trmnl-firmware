#pragma once
// A Preferences put* that wrote nothing is reported, never silent (owner's X 2026-11-02: an app ended by a long press
// came back after every wake although the resume flag was cleared three times, and a printer address never saved:
// suspected NVS writes failing). Pure: the report is a function.
#include <stddef.h>

typedef void (*nvs_write_report_t)(const char *key, size_t got, size_t want);

// `got`: what put* returned; `want`: the bytes it should have written (a bool or char 1, a uint 4, a string its
// length). True when it worked; else `report` (if set) is told once and the number of failures goes up.
bool nvs_write_check(const char *key, size_t got, size_t want, nvs_write_report_t report);

// Failures since boot
unsigned nvs_write_failures(void);
