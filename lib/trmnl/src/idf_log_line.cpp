// ESP-IDF's error and warning lines for the event log (idf_log_line.h)
#include "idf_log_line.h"

#include <stdio.h>
#include <string.h>

bool idf_log_line_keep(const char *line, char *out, size_t cap) {
  if (!line || !out || cap < 2) return false;
  if ((line[0] != 'E' && line[0] != 'W') || line[1] != ' ' || line[2] != '(') return false;
  const char *p = strchr(line + 3, ')');
  if (!p) return false;
  p++;
  while (*p == ' ') p++;
  if (!*p || *p == '\n') return false;
  size_t n = strcspn(p, "\r\n");
  size_t room = cap - 1 < IDF_LOG_LINE_MAX ? cap - 1 : IDF_LOG_LINE_MAX;
  if (room < 2) return false;
  if (n > room - 2) n = room - 2;
  out[0] = line[0];
  out[1] = ' ';
  memcpy(out + 2, p, n);
  out[n + 2] = '\0';
  return true;
}

bool idf_log_rate_allow(idf_log_rate_t *r, uint32_t now_ms, unsigned *dropped_before) {
  if (dropped_before) *dropped_before = 0;
  if (!r) return false;
  if (r->minute_at == 0 || (uint32_t)(now_ms - r->minute_at) >= 60000u) {
    if (dropped_before) *dropped_before = r->dropped;
    r->minute_at = now_ms ? now_ms : 1;
    r->kept = 0;
    r->dropped = 0;
  }
  if (r->kept >= IDF_LOG_LINES_PER_MIN) {
    r->dropped++;
    return false;
  }
  r->kept++;
  return true;
}
