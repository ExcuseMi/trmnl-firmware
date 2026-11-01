// The writer profile of the Wi-Fi page (documents and fonts) and the upload path every profile shares.
// Transport-free (ws_http.h); the rules are webshare_store / webshare_zip.
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "webshare_apps.h"
#include "webshare_zip.h"
#include "ws_page_json.h"

#define WS_LIST_MAX 200

void ws_install_finish(ws_up_req_t *q, bool exact); // ws_page_apps.cpp

static void handle_list(WsRequest &r) {
  if (!ws_page_guard(r, false, true)) return;
  JsonDocument d;
  JsonArray docs = d["docs"].to<JsonArray>(), fonts = d["fonts"].to<JsonArray>();
  // documents first, then fonts: ws_store_list(fonts=true) lists fonts before documents, so list them apart
  struct Both { JsonArray docs, fonts; } both = {docs, fonts};
  ws_store_list(WP.fs_app, true, WS_LIST_MAX, [](const char *path, uint32_t size, ws_kind_t kind, void *ctx) {
    Both *b = (Both *)ctx;
    JsonObject o = (kind == WS_FONT ? b->fonts : b->docs).add<JsonObject>();
    o["p"] = path;
    o["s"] = size;
    return true;
  }, &both);
  d["free"] = (uint32_t)WP.fs_app->free_bytes(WP.fs_app->ctx);
  ws_send_doc(r, 200, d);
}

// ---- download of one document ----

typedef struct {
  const ws_fs_t *fs;
  void *file;
} get_job_t;

static int get_fill(void *ctx, uint8_t *buf, size_t cap) {
  get_job_t *j = (get_job_t *)ctx;
  int n = j->fs->read(j->fs->ctx, j->file, buf, cap);
  if (n > 0) WP.last_request = WP.env->now();
  return n;
}

static void get_end(void *ctx) {
  get_job_t *j = (get_job_t *)ctx;
  j->fs->close(j->fs->ctx, j->file);
  free(j);
}

static void handle_get(WsRequest &r) {
  if (!ws_page_guard(r, false, true)) return;
  char p[WS_PATH_MAX + 2] = "";
  r.param("p", false, p, sizeof(p));
  uint32_t size;
  bool dir;
  const ws_fs_t *fs = WP.fs_app;
  if (ws_classify(p) != WS_DOC || fs->stat(fs->ctx, p, &size, &dir) != 0 || dir) {
    return ws_send_error(r, 404, "no such document");
  }
  get_job_t *j = (get_job_t *)malloc(sizeof(get_job_t));
  void *f = j ? fs->open(fs->ctx, p, false) : nullptr;
  if (!f) {
    free(j);
    return ws_send_error(r, 404, "no such document");
  }
  j->fs = fs;
  j->file = f;
  const char *slash = strrchr(p, '/');
  char disp[WS_PATH_MAX + 32];
  snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", slash ? slash + 1 : p);
  ws_header_t h[2] = {{"Content-Disposition", disp}, {"Cache-Control", "no-store"}};
  r.on_disconnect(get_end, j);
  r.send_stream(200, "application/octet-stream", size, get_fill, j, h, 2);
}

// ---- all documents as one zip ----

typedef struct {
  ws_zip_t z;
  ws_zip_entry_t entries[WS_ZIP_MAX_FILES];
  char paths[WS_ZIP_MAX_FILES][WS_PATH_MAX + 1];
  const char *ptrs[WS_ZIP_MAX_FILES];
  int n;
} zip_job_t;

static int zip_fill(void *ctx, uint8_t *buf, size_t cap) {
  int n = ws_zip_read(&((zip_job_t *)ctx)->z, buf, cap);
  if (n < 0) WP.env->log("webshare: zip aborted, a file could not be read");
  else WP.last_request = WP.env->now();
  return n;
}

static void zip_end(void *ctx) {
  zip_job_t *job = (zip_job_t *)ctx;
  ws_zip_end(&job->z);
  delete job;
  WP.zip_busy = false;
}

static void handle_zip(WsRequest &r) {
  if (!ws_page_guard(r, false, true)) return;
  if (WP.zip_busy) return ws_send_error(r, 503, "another zip is being made");
  zip_job_t *job = new (std::nothrow) zip_job_t;
  if (!job) return ws_send_error(r, 503, "out of memory");
  job->n = 0;
  ws_store_list(WP.fs_app, false, WS_ZIP_MAX_FILES, [](const char *path, uint32_t, ws_kind_t, void *ctx) {
    zip_job_t *j = (zip_job_t *)ctx;
    strcpy(j->paths[j->n], path);
    j->ptrs[j->n] = j->paths[j->n];
    j->n++;
    return true;
  }, job);
  if (!ws_zip_begin(&job->z, WP.fs_app, job->entries, job->ptrs, job->n)) {
    delete job;
    return ws_send_error(r, 500, "zip failed");
  }
  WP.zip_busy = true;
  char disp[80];
  snprintf(disp, sizeof(disp), "attachment; filename=\"%s-documents.zip\"", WP.host ? WP.host : "writer");
  ws_header_t h[2] = {{"Content-Disposition", disp}, {"Cache-Control", "no-store"}};
  r.on_disconnect(zip_end, job);
  r.send_stream(200, "application/zip", -1, zip_fill, job, h, 2);
}

static void handle_rename(WsRequest &r) {
  if (!ws_page_guard(r, true, true)) return;
  char from[WS_PATH_MAX + 2] = "", to[WS_PATH_MAX + 2] = "";
  r.param("from", false, from, sizeof(from));
  r.param("to", false, to, sizeof(to));
  ws_result_t rc = ws_store_rename(WP.fs_app, from, to);
  if (rc == WS_OK) return ws_send_json(r, 200, "{\"ok\":true}");
  ws_send_error(r, ws_http_code(rc), ws_result_str(rc));
}

static void handle_delete(WsRequest &r) {
  if (!ws_page_guard(r, true, true)) return;
  char p[WS_PATH_MAX + 2] = "";
  r.param("p", false, p, sizeof(p));
  ws_result_t rc = ws_store_delete(WP.fs_app, p);
  if (rc == WS_OK) return ws_send_json(r, 200, "{\"ok\":true}");
  ws_send_error(r, ws_http_code(rc), ws_result_str(rc));
}

// ---- uploads (every profile) ----

static void on_upload_gone(void *ctx) { // cable pulled, phone gone: drop the temporary file
  WP.env->lock();
  if (WP.upload_owner == ctx) ws_page_drop_upload();
  WP.env->unlock();
}

// One piece of an upload. One upload at a time (the rest get 503). Writer: raw file bytes of POST /api/put?p=<path>.
void ws_feed_upload(WsRequest &r, const char *name, const uint8_t *data, size_t len, size_t index, size_t total,
                    bool last, bool exact) {
  ws_up_req_t *q = (ws_up_req_t *)r.temp();
  if (index == 0 && !q) {
    q = (ws_up_req_t *)calloc(1, sizeof(ws_up_req_t));
    r.temp() = q;
    if (!q) return;
    if (!ws_page_authorized(r, true)) {
      q->unauth = true;
      return;
    }
    WP.env->lock();
    q->rc = WP.profile == WS_PROFILE_INSTALL ? ws_apps_begin(&WP.upload, WP.fs_root, name, (uint32_t)total)
            : WP.profile == WS_PROFILE_FILES ? ws_files_begin(&WP.upload, WP.fs_app, &WP.files_cfg, name, (uint32_t)total)
                                             : ws_upload_begin(&WP.upload, WP.fs_app, name, (uint32_t)total);
    q->began = q->rc == WS_OK;
    if (q->began) {
      WP.upload_owner = r.id();
      WP.upload_last_chunk = WP.env->now();
      r.on_disconnect(on_upload_gone, (void *)r.id());
    }
    WP.env->unlock();
  }
  if (!q || !q->began) return;
  WP.env->lock();
  if (WP.upload_owner == r.id()) {
    WP.last_request = WP.upload_last_chunk = WP.env->now();
    q->rc = ws_upload_chunk(&WP.upload, data, len);
    if (q->rc != WS_OK) {
      q->began = false;
      WP.upload_owner = nullptr;
    } else if (last) {
      if (WP.profile == WS_PROFILE_INSTALL) ws_install_finish(q, exact);
      else q->rc = ws_upload_end(&WP.upload, &q->how, q->final_path);
      q->began = false;
      q->done = true;
      WP.upload_owner = nullptr;
    }
  } else {
    q->began = false; // the stall check dropped it
    q->rc = WS_E_SHORT;
  }
  WP.env->unlock();
}

static void handle_put_body(WsRequest &r, const uint8_t *data, size_t len, size_t index, size_t total) {
  char p[WS_PATH_MAX + 2] = "";
  r.param("p", false, p, sizeof(p));
  ws_feed_upload(r, p, data, len, index, total, index + len == total, true);
}

const char *ws_put_how(ws_put_t how) { return how == WS_PUT_NEW ? "new" : how == WS_PUT_SAME ? "same" : "both"; }

static void handle_put(WsRequest &r) {
  ws_up_req_t *q = (ws_up_req_t *)r.temp();
  if (!ws_page_guard(r, true, true)) return;
  ws_up_req_t empty = {};
  if (!q) { // no body: an empty file
    char p[WS_PATH_MAX + 2] = "";
    r.param("p", false, p, sizeof(p));
    WP.env->lock();
    empty.rc = ws_upload_begin(&WP.upload, WP.fs_app, p, 0);
    if (empty.rc == WS_OK) empty.rc = ws_upload_end(&WP.upload, &empty.how, empty.final_path);
    empty.done = empty.rc == WS_OK;
    WP.env->unlock();
    q = &empty;
  }
  if (q->rc != WS_OK || !q->done) {
    ws_result_t rc = q->rc == WS_OK ? WS_E_SHORT : q->rc;
    return ws_send_error(r, ws_http_code(rc), ws_result_str(rc));
  }
  JsonDocument d;
  d["ok"] = true;
  d["how"] = ws_put_how(q->how);
  d["path"] = q->final_path;
  ws_send_doc(r, 200, d);
}

const ws_route_t WS_ROUTES_WRITER[] = {
    {WS_GET, "/api/list", handle_list, nullptr, nullptr},
    {WS_GET, "/api/get", handle_get, nullptr, nullptr},
    {WS_GET, "/api/zip", handle_zip, nullptr, nullptr},
    {WS_POST, "/api/rename", handle_rename, nullptr, nullptr},
    {WS_POST, "/api/delete", handle_delete, nullptr, nullptr},
    {WS_POST, "/api/put", handle_put, handle_put_body, nullptr},
};
const int WS_ROUTES_WRITER_N = (int)(sizeof(WS_ROUTES_WRITER) / sizeof(WS_ROUTES_WRITER[0]));
