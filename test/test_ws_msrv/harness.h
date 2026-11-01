#pragma once

// The Wi-Fi page's handlers behind the modem server (ws_msrv.h) with a fake transport and a fake device (ws_env_t):
// requests go in as +IPD frames of any size, answers come out as the bytes sent on each link.
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "../test_webshare/memfs.h"
#include <app_settings_form.h>
#include <kbd_layout.h>
#include <ws_msrv.h>
#include <ws_page.h>

struct Fake {
  uint32_t now = 1000, rnd = 7;
  std::map<int, std::string> sent;
  std::vector<int> closed; // links the server closed
  long send_fail_after = -1; // sends that work before every send fails
  long sends = 0;
  size_t biggest_send = 0;
  int kbd = 0;
  std::string touched;
  asf_form_t form = {};
  std::map<std::string, std::string> settings;
  int changed_events = 0;
  std::string note;
  int locks = 0;
};
static Fake F;
static MemFs *APP_FS, *ROOT_FS;

static uint32_t f_now(void) { return F.now; }
static uint32_t f_rnd(void) { return F.rnd = F.rnd * 1103515245u + 12345u; }
static void f_lock(void) { F.locks++; }
static void f_unlock(void) { F.locks--; }
static const char *f_page(int profile) {
  static const char *p[] = {"<html>writer", "<html>apps", "<html>files", "<html>settings"};
  return p[profile];
}
static void f_log(const char *, ...) {}
static int f_kbd_cur(void) { return F.kbd; }
static bool f_kbd_store(int l) { return (F.kbd = l), true; }
static bool f_touch(const char *b) {
  if (strcmp(b, "left") && strcmp(b, "middle") && strcmp(b, "right") && strcmp(b, "long") && strcmp(b, "stop")) return false;
  F.touched += b;
  return true;
}
static bool f_form(asf_form_t *out) { return (*out = F.form), true; }
static int f_get(const char *k, char *buf, size_t cap) {
  if (!F.settings.count(k)) return -1;
  std::string v = F.settings[k];
  size_t n = v.size() < cap ? v.size() : cap;
  memcpy(buf, v.data(), n);
  return (int)n;
}
static int f_set(const char *k, const char *v, size_t len) { return (F.settings[k] = std::string(v, len)), 0; }
static int f_remove(const char *k) { return F.settings.erase(k), 0; }
static void f_changed(int n) { F.changed_events += n; }
static void *f_alloc(size_t n) { return malloc(n); }
static size_t f_largest(void) { return 8u << 20; }
static bool f_note(char *out, size_t cap) { return snprintf(out, cap, "%s", F.note.c_str()), !F.note.empty(); }
static void f_note_rm(void) { F.note.clear(); }

static const ws_env_t ENV = {f_now, f_rnd, f_lock, f_unlock, f_page, "kbdjs", f_log, f_log, f_kbd_cur, f_kbd_store,
                             f_touch, f_form, f_get, f_set, f_remove, f_changed, f_alloc, free, f_largest, f_note,
                             f_note_rm};

static ws_msrv_t SRV;

static bool t_send(void *, int link, const uint8_t *d, size_t n) {
  if (F.send_fail_after >= 0 && F.sends >= F.send_fail_after) return false;
  F.sends++;
  if (n > F.biggest_send) F.biggest_send = n;
  F.sent[link].append((const char *)d, n);
  return true;
}
static void t_close(void *, int link) { F.closed.push_back(link); }
static uint32_t t_now(void *) { return F.now; }
static const ws_mtransport_t T = {t_send, t_close, t_now, nullptr};

static void start(int profile) {
  ws_msrv_end(&SRV);
  delete APP_FS;
  delete ROOT_FS;
  APP_FS = new MemFs();
  ROOT_FS = new MemFs();
  F = Fake();
  WP.host = "writer";
  WP.ap_url = "http://4.3.2.1";
  ws_page_start(profile, false, &ENV, &APP_FS->fs, &ROOT_FS->fs);
  WP.running = true;
  ws_msrv_init(&SRV, &T);
}

static void pump() {
  for (int i = 0; i < 100000 && ws_msrv_poll(&SRV); i++) {
  }
}

// One request on `link` in frames of `frame` bytes; the answer (everything sent on the link)
static std::string ask(const std::string &raw, size_t frame = 1460, int link = 0) {
  F.sent[link].clear();
  ws_msrv_connect(&SRV, link);
  for (size_t i = 0; i < raw.size(); i += frame) {
    std::string f = raw.substr(i, frame);
    ws_msrv_data(&SRV, link, (const uint8_t *)f.data(), f.size());
  }
  pump();
  return F.sent[link];
}

static int status(const std::string &resp) { return resp.size() > 12 ? atoi(resp.c_str() + 9) : 0; }
static std::string body(const std::string &resp) {
  size_t at = resp.find("\r\n\r\n");
  return at == std::string::npos ? "" : resp.substr(at + 4);
}
static std::string hdr(const std::string &resp, const std::string &name) {
  size_t at = resp.find("\r\n" + name + ": ");
  if (at == std::string::npos || at > resp.find("\r\n\r\n")) return "";
  at += name.size() + 4;
  return resp.substr(at, resp.find("\r\n", at) - at);
}

static std::string get(const std::string &path, const std::string &cookie = "", bool writer = true) {
  std::string h = "GET " + path + " HTTP/1.1\r\nHost: 192.168.1.9\r\n";
  if (!cookie.empty()) h += "Cookie: wsid=" + cookie + "\r\n";
  if (writer) h += "X-Writer: 1\r\n";
  return ask(h + "\r\n");
}

static std::string post(const std::string &path, const std::string &data, const std::string &cookie = "",
                        const std::string &type = "application/x-www-form-urlencoded", size_t frame = 1460,
                        bool writer = true) {
  std::string h = "POST " + path + " HTTP/1.1\r\nHost: 192.168.1.9\r\nContent-Length: " + std::to_string(data.size()) +
                  "\r\n";
  if (!type.empty()) h += "Content-Type: " + type + "\r\n";
  if (!cookie.empty()) h += "Cookie: wsid=" + cookie + "\r\n";
  if (writer) h += "X-Writer: 1\r\n";
  return ask(h + "\r\n" + data, frame);
}

// Logs in with the code; the session cookie
static std::string login() {
  std::string r = post("/api/login", std::string("pin=") + WP.auth.pin);
  std::string c = hdr(r, "Set-Cookie");
  return c.size() > 5 + WS_TOKEN_LEN ? c.substr(5, WS_TOKEN_LEN) : "";
}
