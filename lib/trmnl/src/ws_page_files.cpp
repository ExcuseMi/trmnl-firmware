// The files profile of the Wi-Fi page (API 2.3): an app's own files of one folder, e.g. the Game Boy app's ROMs.
// Transport-free (ws_http.h); the name, extension and size rules are webshare_files.
#include <string.h>

#include "ws_page_json.h"

#define WS_FILES_LIST_MAX 100

const char *ws_put_how(ws_put_t how); // ws_page_writer.cpp

static void handle_files(WsRequest &r) {
  if (!ws_page_authorized(r, false)) return ws_send_error(r, 401, "type the code first");
  JsonDocument d;
  JsonArray files = d["files"].to<JsonArray>();
  ws_files_list(WP.fs_app, &WP.files_cfg, WS_FILES_LIST_MAX, [](const char *name, uint32_t size, void *ctx) {
    JsonObject o = (*(JsonArray *)ctx).add<JsonObject>();
    o["n"] = name;
    o["s"] = size;
    return true;
  }, &files);
  JsonArray exts = d["exts"].to<JsonArray>();
  for (int i = 0; i < WP.files_cfg.n_exts; i++) exts.add(WP.files_cfg.exts[i]);
  d["max"] = WP.files_cfg.max_bytes;
  d["free"] = (uint32_t)WP.fs_app->free_bytes(WP.fs_app->ctx);
  ws_send_doc(r, 200, d);
}

static void send_why(WsRequest &r, ws_result_t rc) {
  char why[96];
  ws_files_why(&WP.files_cfg, rc, why, sizeof(why));
  ws_send_error(r, ws_http_code(rc), why);
}

static void handle_file_delete(WsRequest &r) {
  if (!ws_page_authorized(r, true)) return ws_send_error(r, 401, "type the code first");
  char name[WS_NAME_MAX + 2] = "";
  r.param("name", false, name, sizeof(name));
  ws_result_t rc = ws_files_delete(WP.fs_app, &WP.files_cfg, name);
  if (rc == WS_OK) return ws_send_json(r, 200, "{\"ok\":true}");
  send_why(r, rc);
}

// The answer of an upload: {"ok":true,"how":"new|same|both","name":"<file name kept>"} or {"error":"<why>"}
static void handle_file_done(WsRequest &r) {
  ws_up_req_t *q = (ws_up_req_t *)r.temp();
  if (q && q->unauth) return ws_send_error(r, 401, "type the code first");
  if (!q) { // no body at all: an empty file (or a request that never logged in)
    if (!ws_page_authorized(r, true)) return ws_send_error(r, 401, "type the code first");
    return ws_send_error(r, 400, "empty file");
  }
  if (q->rc != WS_OK || !q->done) return send_why(r, q->rc == WS_OK ? WS_E_SHORT : q->rc);
  JsonDocument d;
  d["ok"] = true;
  d["how"] = ws_put_how(q->how);
  const char *slash = strrchr(q->final_path, '/');
  d["name"] = slash ? slash + 1 : q->final_path;
  ws_send_doc(r, 200, d);
}

static void file_body(WsRequest &r, const uint8_t *data, size_t len, size_t index, size_t total) {
  char name[WS_NAME_MAX + 2] = "";
  r.param("name", false, name, sizeof(name));
  ws_feed_upload(r, name, data, len, index, total, index + len == total, true);
}

const ws_route_t WS_ROUTES_FILES[] = {
    {WS_POST, "/api/touch", ws_handle_touch, nullptr, nullptr},
    {WS_GET, "/api/files", handle_files, nullptr, nullptr},
    {WS_POST, "/api/filedelete", handle_file_delete, nullptr, nullptr},
    {WS_POST, "/api/file", handle_file_done, file_body, nullptr},
};
const int WS_ROUTES_FILES_N = (int)(sizeof(WS_ROUTES_FILES) / sizeof(WS_ROUTES_FILES[0]));
