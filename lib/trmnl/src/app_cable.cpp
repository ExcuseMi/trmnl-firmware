#include "app_cable.h"

#include <string.h>

#include <app_path.h>

app_cable_action_t app_cable_plan(usb_role_t role, const char *running, const char *path) {
  if (role != USB_ROLE_SERIAL) return APP_CABLE_REFUSE_PORT;
  if (!running || !running[0]) return APP_CABLE_INSTALL_NOW;
  char name[32];
  if (app_path_name(path, name, sizeof(name)) && strcmp(name, running) == 0) return APP_CABLE_REPLACE_AND_RESTART;
  return APP_CABLE_INSTALL_KEEP_RUNNING;
}

const char *app_cable_refusal(usb_role_t role) {
  return role == USB_ROLE_DRIVE ? "busy: the USB drive has the port" : "busy: USB host mode has the port";
}

void app_cable_line_init(app_cable_line_t *l) {
  l->len = 0;
  l->ended = false;
}

bool app_cable_line_feed(app_cable_line_t *l, unsigned char b, const char **header) {
  l->ended = false;
  if (b == '\r') return false;
  if (b != '\n') {
    if (l->len + 1 >= sizeof(l->line)) { // too long: keep the tail
      memmove(l->line, l->line + 1, l->len - 1);
      l->len--;
    }
    l->line[l->len++] = (char)b;
    return false;
  }
  l->line[l->len] = '\0';
  l->len = 0;
  l->ended = true;
  const char *h = app_upload_find_header(l->line);
  if (!h || strncmp(h, "TAPP PUT ", 9) != 0) return false;
  *header = h;
  return true;
}

const char *app_cable_line_command(const app_cable_line_t *l) {
  if (!l->ended) return nullptr;
  const char *h = app_upload_find_header(l->line);
  return h && strncmp(h, "TAPP PUT ", 9) != 0 ? h : nullptr;
}

static volatile bool busy;
void app_cable_busy_set(bool b) { busy = b; }
bool app_cable_busy(void) { return busy; }

static volatile uint32_t last_rx;
static volatile bool rx_seen;
void app_cable_rx_note(uint32_t now_ms) {
  last_rx = now_ms;
  rx_seen = true;
}

bool app_cable_sleep_hold(uint32_t asked_ms, uint32_t now_ms) {
  if (now_ms - asked_ms >= APP_CABLE_HOLD_MAX_MS) return false;
  if (busy) return true;
  return rx_seen && now_ms - last_rx < APP_CABLE_QUIET_MS;
}
