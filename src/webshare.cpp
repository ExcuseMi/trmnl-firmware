// The writer's Wi-Fi page: an HTTP server on the X's own access point (named like the device) or on the home network, for a phone
// browser. All decisions (names, code, no-overwrite, atomic writes, zip) are in lib/trmnl/webshare_*, the handlers in
// lib/trmnl/ws_page_*; this file connects them to LittleFS and picks the transport: AsyncWebServer on the S3's
// 2.4 GHz radio (webshare_async.cpp), or on a saved 5 GHz network the ESP32-C5 modem (webshare_modem.cpp).
#ifdef BOARD_TRMNL_X

#include <app_sleep_gate.h>
#include "evlog.h"
#include "webshare.h"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

#include "app_storage_x.h"
#include <WiFi.h>
#include <captive_ap.h>
#include <esp_random.h>
#include <trmnl_log.h>
#include <webshare_apps.h>
#include <webshare_mode.h>
#include <webshare_host.h>
#include <config.h>
#include <wifi_network.h>
#include <esp_mac.h>

#include <apps.h>

#include "app_host.h"
#include "webshare_internal.h"

#define WS_AP_URL "http://4.3.2.1"

// The device's own name for the access point and mDNS (webshare_host.h), made at each start
static char dev_ssid[WS_HOST_MAX + 1], dev_host[WS_HOST_MAX + 1];

static void make_dev_names(void) {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  ws_host_names(getWifiClientHostname().c_str(), WIFI_CLIENT_HOSTNAME_PREFIX, mac, dev_ssid, dev_host);
}

const char *webshare_device_name(void) {
  if (!dev_ssid[0]) make_dev_names();
  return dev_ssid;
}

// ---- LittleFS under the app's folder ----

static char base_dir[48]; // the writer's folder; the install profile uses the root (ctx "")

static bool full(void *ctx, const char *rel, char *out, size_t len) {
  const char *base = (const char *)ctx;
  if (!rel[0]) return snprintf(out, len, "%s", base[0] ? base : "/") < (int)len; // the folder itself
  return snprintf(out, len, "%s/%s", base, rel) < (int)len;
}

static int fs_stat(void *c, const char *path, uint32_t *size, bool *is_dir) {
  char p[128];
  if (!full(c, path, p, sizeof(p)) || !LittleFS.exists(p)) return -1;
  File f = LittleFS.open(p);
  if (!f) return -1;
  if (size) *size = (uint32_t)f.size();
  if (is_dir) *is_dir = f.isDirectory();
  f.close();
  return 0;
}

static void *fs_open(void *c, const char *path, bool write) {
  char p[128];
  if (!full(c, path, p, sizeof(p))) return nullptr;
  File *f = new (std::nothrow) File(LittleFS.open(p, write ? FILE_WRITE : FILE_READ));
  if (!f || !*f || f->isDirectory()) {
    delete f;
    return nullptr;
  }
  return f;
}

static int fs_read(void *, void *file, void *buf, size_t len) { return ((File *)file)->read((uint8_t *)buf, len); }

static int fs_write(void *, void *file, const void *buf, size_t len) {
  return ((File *)file)->write((const uint8_t *)buf, len) == len ? (int)len : -1;
}

static int fs_close(void *, void *file) {
  File *f = (File *)file;
  f->close();
  delete f;
  return 0; // the Arduino wrapper does not report errors; webshare_store checks the size on disk
}

static int fs_rename(void *c, const char *from, const char *to) {
  char a[128], b[128];
  return full(c, from, a, sizeof(a)) && full(c, to, b, sizeof(b)) && LittleFS.rename(a, b) ? 0 : -1;
}

static int fs_remove(void *c, const char *path) {
  char p[128];
  return full(c, path, p, sizeof(p)) && LittleFS.remove(p) ? 0 : -1;
}

static int fs_mkdir(void *c, const char *path) {
  char p[128];
  return full(c, path, p, sizeof(p)) && LittleFS.mkdir(p) ? 0 : -1;
}

static int fs_list(void *c, const char *dir, ws_list_cb_t cb, void *cx) {
  char p[128];
  if (!full(c, dir, p, sizeof(p))) return -1;
  File root = LittleFS.open(p);
  if (!root || !root.isDirectory()) return -1;
  int n = 0;
  while (File e = root.openNextFile()) {
    String name = e.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    bool go_on = cb(name.c_str(), e.isDirectory(), (uint32_t)e.size(), cx);
    e.close();
    n++;
    if (!go_on) break;
  }
  root.close();
  return n;
}

static uint64_t fs_free(void *) { return app_storage_fs_free(nullptr, nullptr); } // full readings read again

static int64_t fs_mtime(void *c, const char *path) {
  char p[128];
  if (!full(c, path, p, sizeof(p))) return 0;
  File f = LittleFS.open(p);
  int64_t t = f ? (int64_t)f.getLastWrite() : 0;
  f.close();
  return t > 1700000000 ? t : 0; // before late 2023: the clock was not set
}

static const ws_fs_t FS = {(void *)base_dir, fs_stat, fs_open, fs_read, fs_write, fs_close, fs_rename, fs_remove,
                           fs_mkdir, fs_list, fs_free, fs_mtime};
static const ws_fs_t FS_ROOT = {(void *)"", fs_stat, fs_open, fs_read, fs_write, fs_close, fs_rename, fs_remove,
                                fs_mkdir, fs_list, fs_free, fs_mtime};

const ws_fs_t *webshare_fs_root(void) { return &FS_ROOT; }


// ---- state ----

webshare_state_t S;
static AsyncWebServer *server;
static DNSServer *dns;

// ---- start, stop, status ----

static void stop_locked() {
  if (!WP.running) return;
  WP.running = false; // the modem's task sees it, closes its links and the server itself
  ws_page_drop_upload(); // a half upload: the temporary file goes
  if (server) {
    server->end();
    delete server;
    server = nullptr;
  }
  if (S.mode == APP_WEBSHARE_AP) {
    if (dns) {
      dns->stop();
      delete dns;
      dns = nullptr;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
  } else if (!S.modem) {
    MDNS.end();
    if (ws_release_wifi_on_stop(S.mode, S.connected_before)) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
  }
  Log_info("webshare: off");
  evlog_add("webshare: off");
}

// Switches the page off after WS_IDLE_MS without a request, and drops an upload that stopped coming
static void watch_task(void *) {
  while (WP.running) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    xSemaphoreTake(S.lock, portMAX_DELAY);
    bool idle = false;
    if (ws_page_tick(millis(), &idle)) {
      Log_info("webshare: idle or locked out, switching off");
      evlog_add("webshare: switched off, %s", idle ? "idle" : "too many wrong codes");
      stop_locked();
    }
    xSemaphoreGive(S.lock);
  }
  S.watch_alive = false;
  vTaskDelete(nullptr);
}

void webshare_stop(void) {
  if (!S.lock) return;
  xSemaphoreTake(S.lock, portMAX_DELAY);
  stop_locked();
  xSemaphoreGive(S.lock);
  // the watcher and the modem's task leave by themselves
  for (int i = 0; i < 50 && (S.watch_alive || webshare_modem_alive()); i++) vTaskDelay(pdMS_TO_TICKS(100));
}

// The network: the S3's own station (2.4 GHz), else on a saved 5 GHz network the modem, else (AUTO) the access point
static bool pick_network(app_webshare_mode_t requested, app_webshare_mode_t *mode, bool *modem) {
  *modem = false;
  bool up = requested != APP_WEBSHARE_AP && app_host_net_up();
  if (!up && requested != APP_WEBSHARE_AP && app_host_net_up_modem()) up = *modem = true;
  return ws_pick_mode(requested, up, mode);
}

static int start_modem(void) {
  bool mdns_ok = false;
  int rc = webshare_modem_start(dev_host, S.ip, sizeof(S.ip), &mdns_ok);
  if (rc) return rc;
  // without mDNS the X shows the address with the IP (docs/apps.md "Wi-Fi page on 5 GHz")
  if (mdns_ok) snprintf(S.address, sizeof(S.address), "http://%s.local", dev_host);
  else snprintf(S.address, sizeof(S.address), "http://%s", S.ip);
  return 0;
}

int webshare_start_profile(app_webshare_mode_t requested, int profile) {
  if (requested != APP_WEBSHARE_AUTO && requested != APP_WEBSHARE_AP && requested != APP_WEBSHARE_HOME) {
    return APP_EINVAL;
  }
  if (!S.lock) S.lock = xSemaphoreCreateMutex();
  if (!S.lock) return APP_ENOMEM;
  webshare_stop(); // a new start is a new code
  if (webshare_modem_alive()) return APP_EBUSY; // the last modem server has not closed yet
  app_webshare_mode_t mode;
  bool modem;
  bool connected_before = WiFi.status() == WL_CONNECTED;
  if (!pick_network(requested, &mode, &modem)) return APP_ENONET;
  if (mode == APP_WEBSHARE_AP) Log_info("webshare: no home network, using the access point");

  make_dev_names();
  xSemaphoreTake(S.lock, portMAX_DELAY);
  if (profile == WS_PROFILE_INSTALL) { // the file system root, the apps folder
    LittleFS.mkdir(APPS_DIR);
    ws_apps_cleanup(&FS_ROOT);
  } else {
    snprintf(base_dir, sizeof(base_dir), "%s", app_host.dir);
    LittleFS.mkdir(APPS_DATA_DIR);
    LittleFS.mkdir(base_dir);
    if (profile == WS_PROFILE_FILES) ws_files_cleanup(&FS, &WP.files_cfg); // a temporary file a power loss left
    else if (profile == WS_PROFILE_WRITER) ws_store_cleanup(&FS);
  }
  WP.ap_url = WS_AP_URL;
  WP.host = dev_host;
  ws_page_start(profile, mode == APP_WEBSHARE_AP, &WEBSHARE_ENV, &FS, &FS_ROOT);
  S.mode = mode;
  S.modem = modem;
  S.connected_before = connected_before;
  S.network[0] = '\0';
  int rc = 0;
  WP.running = true; // before the modem's task starts: it runs while this is set
  if (modem) {
    rc = start_modem();
  } else if (!(server = new (std::nothrow) AsyncWebServer(80))) {
    rc = APP_ENOMEM;
  } else if (mode == APP_WEBSHARE_AP) {
    dns = new (std::nothrow) DNSServer();
    if (!dns) {
      rc = APP_ENOMEM;
    } else {
      captive_ap_up(IPAddress(4, 3, 2, 1), dev_ssid, nullptr, *dns);
      snprintf(S.network, sizeof(S.network), "%s", dev_ssid);
      snprintf(S.address, sizeof(S.address), "%s", WS_AP_URL);
      snprintf(S.ip, sizeof(S.ip), "4.3.2.1");
    }
  } else {
    MDNS.end(); // the setup code started one with the device's own host name
    if (!MDNS.begin(dev_host)) Log_error("webshare: mDNS failed, use the address");
    snprintf(S.address, sizeof(S.address), "http://%s.local", dev_host);
    snprintf(S.ip, sizeof(S.ip), "%s", WiFi.localIP().toString().c_str());
  }
  if (rc == 0) {
    snprintf(S.url, sizeof(S.url), "http://%s/", S.ip);
    if (server) {
      webshare_async_routes(*server, mode == APP_WEBSHARE_AP, WS_AP_URL);
      server->begin();
    }
    S.watch_alive = true;
    if (xTaskCreate(watch_task, "webshare", 4096, nullptr, 1, nullptr) != pdPASS) {
      S.watch_alive = false;
      rc = APP_ENOMEM;
      stop_locked();
    }
  } else {
    WP.running = false;
    delete server;
    server = nullptr;
    delete dns;
    dns = nullptr;
  }
  xSemaphoreGive(S.lock);
  if (rc == 0) {
    Log_info("webshare: on, %s, %s", mode == APP_WEBSHARE_AP ? "access point" : modem ? "home network (modem, 5 GHz)" : "home network",
             S.address);
  }
  return rc;
}

int webshare_start(app_webshare_mode_t requested) { return webshare_start_profile(requested, WS_PROFILE_WRITER); }

int webshare_status(app_webshare_status_t *st) {
  if (!st) return APP_EINVAL;
  memset(st, 0, sizeof(*st));
  if (!S.lock || !WP.running) return 0;
  st->running = true;
  st->mode = S.mode;
  snprintf(st->url, sizeof(st->url), "%s", S.url);
  if (WP.auth.login_token[0]) snprintf(st->login_url, sizeof(st->login_url), "%s?t=%s", S.url, WP.auth.login_token);
  else snprintf(st->login_url, sizeof(st->login_url), "%s", S.url); // used up: the code is what is left
  snprintf(st->code, sizeof(st->code), "%s", WP.auth.pin);
  snprintf(st->network, sizeof(st->network), "%s", S.network);
  snprintf(st->address, sizeof(st->address), "%s", S.address);
  snprintf(st->ip, sizeof(st->ip), "%s", S.ip);
  uint32_t used = millis() - WP.last_request;
  st->idle_left_s = used >= WS_IDLE_MS ? 0 : (WS_IDLE_MS - used) / 1000;
  st->locked = WP.auth.fails >= WS_MAX_FAILS ? 2 : ws_auth_lock_left(&WP.auth, millis()) ? 1 : 0;
  return 0;
}

#endif // BOARD_TRMNL_X
