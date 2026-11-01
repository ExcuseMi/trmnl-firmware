#include "ws_http.h"

#include <string.h>

const ws_route_t *ws_route_find(const ws_route_t *routes, int n, int method, const char *path) {
  for (int i = 0; i < n; i++) {
    const ws_route_t *r = &routes[i];
    if (!(r->method & method)) continue;
    size_t len = strlen(r->path);
    if (strcmp(path, r->path) == 0) return r;
    if (strncmp(path, r->path, len) == 0 && path[len] == '/' && strcmp(r->path, "/") != 0) return r;
  }
  return nullptr;
}

static int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void ws_url_decode(const char *in, size_t len, char *out, size_t cap) {
  size_t o = 0;
  for (size_t i = 0; i < len && o + 1 < cap; i++) {
    char c = in[i];
    if (c == '+') c = ' ';
    else if (c == '%' && i + 2 < len && hex(in[i + 1]) >= 0 && hex(in[i + 2]) >= 0) {
      c = (char)(hex(in[i + 1]) * 16 + hex(in[i + 2]));
      i += 2;
    }
    out[o++] = c;
  }
  if (cap) out[o] = '\0';
}

bool ws_form_param(const char *qs, size_t len, const char *name, char *out, size_t cap) {
  if (!qs) return false;
  size_t nlen = strlen(name);
  size_t i = 0;
  while (i < len) {
    size_t end = i;
    while (end < len && qs[end] != '&') end++;
    size_t eq = i;
    while (eq < end && qs[eq] != '=') eq++;
    char key[64];
    ws_url_decode(qs + i, eq - i, key, sizeof(key));
    if (strlen(key) == nlen && memcmp(key, name, nlen) == 0) {
      if (eq < end) ws_url_decode(qs + eq + 1, end - eq - 1, out, cap);
      else if (cap) out[0] = '\0';
      return true;
    }
    i = end + 1;
  }
  return false;
}
