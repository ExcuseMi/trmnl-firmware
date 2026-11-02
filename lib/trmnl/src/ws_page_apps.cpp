// The apps profile of the Wi-Fi page ("Install via Wi-Fi" in the Apps menu): list, add and delete the ELF files in
// /apps from a phone or `curl`. Transport-free (ws_http.h); the checks and the .bak bookkeeping are webshare_apps and
// app_install. A proof of concept: no manifest, no permissions.
#include <stdio.h>
#include <string.h>

#include "app_install.h"
#include "app_space.h"
#include "webshare_apps.h"
#include "ws_page_json.h"

void ws_install_finish(ws_up_req_t *q, bool exact) {
  const ws_env_t *e = WP.env;
  uint8_t *buf = (uint8_t *)e->big_alloc(WP.upload.got ? WP.upload.got : 1);
  if (!buf) {
    ws_upload_abort(&WP.upload);
    q->rc = WS_E_NO_SPACE;
    return;
  }
  size_t free_mem = e->big_largest();
  q->rc = ws_apps_end(&WP.upload, WP.fs_root, exact, buf, WP.upload.got, free_mem, q->reason, sizeof(q->reason),
                      q->installed, sizeof(q->installed));
  e->big_free(buf);
  if (q->rc == WS_OK) {
    char name[32], note[48];
    if (app_install_name_ok(q->installed, name, sizeof(name)) && e->note_get(note, sizeof(note)) && !strcmp(note, name)) {
      e->note_remove(); // a new version replaces the one whose start failed
    }
    e->log("webshare: installed %s", q->installed);
  } else if (q->rc == WS_E_REFUSED) {
    e->log("webshare: an upload was refused: %s", q->reason);
  }
}

static void handle_apps(WsRequest &r) {
  char sid[WS_TOKEN_LEN + 1];
  ws_session_cookie(r, sid, sizeof(sid));
  if (!WP.running || !ws_auth_session_ok(&WP.auth, sid)) return ws_send_error(r, 401, "type the code first");
  WP.last_request = WP.env->now();
  JsonDocument d;
  JsonArray apps = d["apps"].to<JsonArray>();
  ws_apps_list(WP.fs_root, 40, [](const char *file, uint32_t size, int64_t mtime, bool has_bak, void *ctx) {
    JsonObject o = (*(JsonArray *)ctx).add<JsonObject>();
    o["n"] = file;
    o["s"] = size;
    o["d"] = (int64_t)mtime;
    o["bak"] = has_bak;
    return true;
  }, &apps);
  static app_space_app_t sizes[40]; // per app: the .bak copy and the saved files (app_space.h)
  int n = app_space_apps(WP.fs_root, sizes, 40);
  for (JsonObject o : apps) {
    for (int i = 0; i < n; i++) {
      char file[40];
      snprintf(file, sizeof(file), "%s.elf", sizes[i].name);
      if (strcmp(file, o["n"] | "") != 0) continue;
      o["bs"] = sizes[i].bak;
      o["data"] = sizes[i].data;
    }
  }
  JsonObject fs = d["fs"].to<JsonObject>();
  fs["used"] = app_space_dir_bytes(WP.fs_root, "");
  fs["free"] = app_space_free(WP.fs_root);
  char note[48];
  if (WP.env->note_get(note, sizeof(note)) && note[0]) {
    char msg[128];
    snprintf(msg, sizeof(msg), "The last start of %s failed; the older version was put back.", note);
    d["note"] = msg;
  }
  ws_send_doc(r, 200, d);
}

static void handle_app_delete(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  char name[WS_NAME_MAX + 2] = "";
  r.param("name", false, name, sizeof(name));
  ws_result_t rc = ws_apps_delete(WP.fs_root, name);
  if (rc == WS_OK) return ws_send_json(r, 200, "{\"ok\":true}");
  ws_send_error(r, ws_http_code(rc), ws_result_str(rc));
}

// "Free space": the .bak copies and a stale upload temp file go; apps and saved files stay
static void handle_app_free(WsRequest &r) {
  if (!ws_install_allowed(r)) return ws_send_error(r, 401, "type the code first");
  int removed = 0;
  uint64_t freed = app_space_clean(WP.fs_root, &removed);
  WP.env->log("webshare: Free space removed %d files, %lu bytes", removed, (unsigned long)freed);
  JsonDocument d;
  d["ok"] = true;
  d["removed"] = removed;
  d["freed"] = freed;
  ws_send_doc(r, 200, d);
}

// The answer of an upload (page and curl): {"ok":true,"name":"x.elf"} or {"error":"<why>"}
static void handle_app_done(WsRequest &r) {
  ws_up_req_t *q = (ws_up_req_t *)r.temp();
  if (!q) return ws_send_error(r, 400, "no file in the request (curl -F file=@app.elf)");
  if (q->unauth) return ws_send_error(r, 401, "type the code first");
  if (q->rc == WS_E_REFUSED) {
    char why[80];
    snprintf(why, sizeof(why), "refused: %s", q->reason);
    return ws_send_error(r, 400, why);
  }
  if (q->rc != WS_OK || !q->done) {
    ws_result_t rc = q->rc == WS_OK ? WS_E_SHORT : q->rc;
    return ws_send_error(r, ws_http_code(rc), ws_result_str(rc));
  }
  JsonDocument d;
  d["ok"] = true;
  d["name"] = q->installed;
  ws_send_doc(r, 200, d);
}

// the page: the raw file as the body, its name in ?name=
static void app_raw(WsRequest &r, const uint8_t *data, size_t len, size_t index, size_t total) {
  char name[WS_NAME_MAX + 2] = "";
  r.param("name", false, name, sizeof(name));
  ws_feed_upload(r, name, data, len, index, total, index + len == total, true);
}

// curl -F file=@app.elf (multipart; the size is the whole body, an upper bound)
static void app_part(WsRequest &r, const char *filename, size_t index, const uint8_t *data, size_t len, bool final) {
  ws_feed_upload(r, filename, data, len, index, r.content_length(), final, false);
}

const ws_route_t WS_ROUTES_INSTALL[] = {
    {WS_POST, "/api/touch", ws_handle_touch, nullptr, nullptr},
    {WS_GET, "/api/apps", handle_apps, nullptr, nullptr},
    {WS_POST, "/api/appdelete", handle_app_delete, nullptr, nullptr},
    {WS_POST, "/api/appfree", handle_app_free, nullptr, nullptr},
    {WS_POST, "/api/app", handle_app_done, app_raw, nullptr},
    {WS_POST, "/apps", handle_app_done, app_raw, app_part}, // or --data-binary with ?name=
};
const int WS_ROUTES_INSTALL_N = (int)(sizeof(WS_ROUTES_INSTALL) / sizeof(WS_ROUTES_INSTALL[0]));
