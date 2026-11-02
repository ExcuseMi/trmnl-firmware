#pragma once

// Each profile's main routes through the modem server (the same handlers AsyncWebServer runs)
#include <unity.h>

#include "../test_app_elf_check/hello_elf.h"
#include "harness.h"

static void test_writer_login_and_page() {
  start(WS_PROFILE_WRITER);
  std::string r = get("/");
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_EQUAL_STRING("<html>writer", body(r).c_str());
  TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", hdr(r, "Content-Type").c_str());
  TEST_ASSERT_EQUAL_STRING("close", hdr(r, "Connection").c_str());
  TEST_ASSERT_EQUAL_STRING("12", hdr(r, "Content-Length").c_str());
  TEST_ASSERT_EQUAL(401, status(get("/api/list")));
  TEST_ASSERT_EQUAL(401, status(post("/api/login", "pin=000000", "", "application/x-www-form-urlencoded", 1460, false)));
  TEST_ASSERT_EQUAL(403, status(post("/api/login", "pin=999999x")));
  std::string c = login();
  TEST_ASSERT_EQUAL(WS_TOKEN_LEN, (int)c.size());
  r = get("/api/list", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_EQUAL_STRING("no-store", hdr(r, "Cache-Control").c_str());
  TEST_ASSERT_TRUE(body(r).find("\"docs\":[]") != std::string::npos);
}

static void test_writer_qr_link_logs_in_once() {
  start(WS_PROFILE_WRITER);
  std::string t = WP.auth.login_token;
  std::string r = get("/?t=" + t, "", false);
  TEST_ASSERT_EQUAL(302, status(r));
  TEST_ASSERT_EQUAL_STRING("/", hdr(r, "Location").c_str());
  std::string c = hdr(r, "Set-Cookie").substr(5, WS_TOKEN_LEN);
  TEST_ASSERT_EQUAL(200, status(get("/api/list", c)));
  r = get("/?t=" + t, "", false); // used up: the page, no cookie
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_EQUAL_STRING("", hdr(r, "Set-Cookie").c_str());
}

static void test_writer_upload_list_get_zip_rename_delete() {
  start(WS_PROFILE_WRITER);
  std::string c = login();
  std::string doc;
  for (int i = 0; i < 3000; i++) doc += "line " + std::to_string(i) + "\r\n------\r\n";
  for (size_t frame : {1u, 7u, 1460u, 8192u}) { // +IPD frames of any size
    std::string r = post("/api/put?p=documents%2FNovel%2FCh+1.md", doc, c, "text/markdown", frame);
    TEST_ASSERT_EQUAL_MESSAGE(200, status(r), std::to_string(frame).c_str());
    TEST_ASSERT_TRUE(body(r).find(frame == 1 ? "\"how\":\"new\"" : "\"how\":\"same\"") != std::string::npos);
  }
  TEST_ASSERT_TRUE(APP_FS->get("documents/Novel/Ch 1.md") == doc);
  TEST_ASSERT_EQUAL(401, status(post("/api/put?p=documents%2FNovel%2Fx.md", "x", c, "", 1460, false))); // no X-Writer
  std::string r = get("/api/list", c);
  TEST_ASSERT_TRUE(body(r).find("\"p\":\"documents/Novel/Ch 1.md\"") != std::string::npos);
  r = get("/api/get?p=documents%2FNovel%2FCh+1.md", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_EQUAL_STRING("attachment; filename=\"Ch 1.md\"", hdr(r, "Content-Disposition").c_str());
  TEST_ASSERT_EQUAL_STRING(std::to_string(doc.size()).c_str(), hdr(r, "Content-Length").c_str());
  TEST_ASSERT_TRUE(body(r) == doc);
  TEST_ASSERT_EQUAL(404, status(get("/api/get?p=settings%2Fsettings.json", c)));
  r = get("/api/zip", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_EQUAL_STRING("", hdr(r, "Content-Length").c_str()); // until the connection closes
  TEST_ASSERT_EQUAL_STRING("attachment; filename=\"writer-documents.zip\"", hdr(r, "Content-Disposition").c_str());
  TEST_ASSERT_TRUE(body(r).compare(0, 4, "PK\x03\x04") == 0);
  TEST_ASSERT_TRUE(body(r).size() > doc.size());
  TEST_ASSERT_FALSE(WP.zip_busy);
  TEST_ASSERT_EQUAL(200, status(post("/api/rename?from=documents%2FNovel%2FCh+1.md&to=documents%2FNovel%2FOne.md", "", c, "")));
  TEST_ASSERT_TRUE(APP_FS->has("documents/Novel/One.md"));
  TEST_ASSERT_EQUAL(404, status(post("/api/delete?p=documents%2FNovel%2FCh+1.md", "", c, "")));
  TEST_ASSERT_EQUAL(200, status(post("/api/delete?p=documents%2FNovel%2FOne.md", "", c, "")));
  TEST_ASSERT_FALSE(APP_FS->has("documents/Novel/One.md"));
  TEST_ASSERT_EQUAL(0, F.locks);
}

static void test_keyboard_and_touch() {
  start(WS_PROFILE_FILES);
  std::string c = login();
  std::string r = get("/api/kbd", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE(body(r).find("\"layouts\":[") != std::string::npos);
  std::string code = kbd_layout_code(KBD_LAYOUT_COUNT - 1);
  TEST_ASSERT_EQUAL(200, status(post("/api/kbd", "c=" + code, c)));
  TEST_ASSERT_EQUAL(KBD_LAYOUT_COUNT - 1, F.kbd);
  TEST_ASSERT_EQUAL(400, status(post("/api/kbd", "c=zz", c)));
  TEST_ASSERT_EQUAL(200, status(post("/api/touch", "b=middle", c)));
  TEST_ASSERT_EQUAL(400, status(post("/api/touch", "b=up", c)));
  TEST_ASSERT_EQUAL_STRING("middle", F.touched.c_str());
}

static void test_files_profile_big_upload_streamed() {
  ws_files_cfg_set(&WP.files_cfg, "roms", ".gb,.gbc", 8u << 20);
  start(WS_PROFILE_FILES);
  std::string c = login();
  std::string rom(3u << 20, '\0'); // 3 MB, written in WS_MSRV_CHUNK pieces as it arrives
  for (size_t i = 0; i < rom.size(); i++) rom[i] = (char)(i * 131 + (i >> 9));
  std::string r = post("/api/file?name=Tetris.gb", rom, c, "application/octet-stream", 1460);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE(body(r).find("\"name\":\"Tetris.gb\"") != std::string::npos);
  TEST_ASSERT_TRUE(APP_FS->get("roms/Tetris.gb") == rom);
  r = get("/api/files", c);
  TEST_ASSERT_TRUE(body(r).find("\"n\":\"Tetris.gb\"") != std::string::npos);
  TEST_ASSERT_TRUE(body(r).find("\"exts\":[\".gb\",\".gbc\"]") != std::string::npos);
  r = post("/api/file?name=x.exe", "MZ", c, "application/octet-stream");
  TEST_ASSERT_EQUAL(400, status(r));
  TEST_ASSERT_TRUE(body(r).find(".gb .gbc") != std::string::npos);
  TEST_ASSERT_EQUAL(200, status(post("/api/filedelete?name=Tetris.gb", "", c, "")));
  TEST_ASSERT_FALSE(APP_FS->has("roms/Tetris.gb"));
}

static std::string elf() { return std::string((const char *)hello_elf, sizeof(hello_elf)); }

static void test_apps_profile_raw_and_multipart() {
  start(WS_PROFILE_INSTALL);
  F.note = "hello";
  TEST_ASSERT_EQUAL_STRING("<html>apps", body(get("/")).c_str());
  std::string c = login();
  std::string r = get("/api/apps", c);
  TEST_ASSERT_TRUE(body(r).find("The last start of hello failed") != std::string::npos);
  r = post("/api/app?name=hello.elf", elf(), c, "application/octet-stream", 333);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE(body(r).find("\"name\":\"hello.elf\"") != std::string::npos);
  TEST_ASSERT_TRUE(ROOT_FS->get("apps/hello.elf") == elf());
  TEST_ASSERT_EQUAL_STRING("", F.note.c_str()); // the new version replaces the one whose start failed
  // curl -F file=@hey.elf "http://<ip>/apps?code=<code>": multipart, the boundary split across frames
  std::string b = "------curl7f";
  std::string mp = "--" + b + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"hey.elf\"\r\n"
                   "Content-Type: application/octet-stream\r\n\r\n" + elf() + "\r\n--" + b + "--\r\n";
  for (size_t frame : {5u, 64u, 1460u}) {
    r = post(std::string("/apps?code=") + WP.auth.pin, mp, "", "multipart/form-data; boundary=" + b, frame, false);
    TEST_ASSERT_EQUAL_MESSAGE(200, status(r), body(r).c_str());
    TEST_ASSERT_TRUE(ROOT_FS->get("apps/hey.elf") == elf());
  }
  TEST_ASSERT_EQUAL(401, status(post("/apps?code=000000x", mp, "", "multipart/form-data; boundary=" + b, 1460, false)));
  r = post("/api/app?name=bad.elf", "not an elf at all", c, "application/octet-stream");
  TEST_ASSERT_EQUAL(400, status(r));
  TEST_ASSERT_TRUE(body(r).find("refused: ") != std::string::npos);
  TEST_ASSERT_FALSE(ROOT_FS->has("apps/bad.elf"));
  TEST_ASSERT_EQUAL(200, status(post("/api/appdelete?name=hey.elf", "", c, "")));
  TEST_ASSERT_FALSE(ROOT_FS->has("apps/hey.elf"));
  // Storage (owner's X 2026-11-02: no room, nothing showed what held it): sizes per app, Free space drops .bak copies
  ROOT_FS->put("apps/hello.elf.bak", std::string(3000, 'b'));
  ROOT_FS->nodes["appdata"] = MemFs::Node{true, ""};
  ROOT_FS->nodes["appdata/hello"] = MemFs::Node{true, ""};
  ROOT_FS->put("appdata/hello/save.bin", std::string(700, 's'));
  r = get("/api/apps", c);
  TEST_ASSERT_TRUE_MESSAGE(body(r).find("\"bs\":3000,\"data\":700") != std::string::npos, body(r).c_str());
  TEST_ASSERT_TRUE(body(r).find("\"fs\":{\"used\":") != std::string::npos);
  TEST_ASSERT_EQUAL(401, status(post("/api/appfree", "", "", "")));
  r = post("/api/appfree", "", c, "");
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE_MESSAGE(body(r).find("\"freed\":3000") != std::string::npos, body(r).c_str());
  TEST_ASSERT_FALSE(ROOT_FS->has("apps/hello.elf.bak"));
  TEST_ASSERT_TRUE(ROOT_FS->has("apps/hello.elf"));
  TEST_ASSERT_TRUE(ROOT_FS->has("appdata/hello/save.bin"));
}

static void test_settings_profile_form() {
  static const app_setting_decl_t d[] = {{"city", "City", nullptr, 1, 40, APP_SETTING_TEXT},
                                         {"key", "API key", nullptr, 0, 64, APP_SETTING_SECRET},
                                         {"n", "Count", nullptr, 1, 9, APP_SETTING_NUMBER}};
  start(WS_PROFILE_SETTINGS);
  TEST_ASSERT_EQUAL(0, asf_declare(&F.form, d, 3));
  F.settings["key"] = "s3cret";
  std::string c = login();
  std::string r = get("/api/settings", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE_MESSAGE(body(r).find("\"k\":\"city\"") != std::string::npos, body(r).c_str());
  TEST_ASSERT_TRUE(body(r).find("s3cret") == std::string::npos); // a secret never goes back
  r = post("/api/settings", "v_city=Gent+%C3%A9&v_n=12", c);
  TEST_ASSERT_EQUAL(400, status(r)); // one bad value writes nothing
  TEST_ASSERT_EQUAL(0u, F.settings.count("city"));
  r = post("/api/settings", "v_city=Gent+%C3%A9&v_n=3&v_key=", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_EQUAL_STRING("Gent \xc3\xa9", F.settings["city"].c_str());
  TEST_ASSERT_EQUAL(0u, F.settings.count("key")); // an empty secret removes it
  TEST_ASSERT_EQUAL(3, F.changed_events);
  TEST_ASSERT_EQUAL(401, status(post("/api/settings", "v_n=4", c, "application/x-www-form-urlencoded", 1460, false)));
}

void run_page_tests() {
  RUN_TEST(test_writer_login_and_page);
  RUN_TEST(test_writer_qr_link_logs_in_once);
  RUN_TEST(test_writer_upload_list_get_zip_rename_delete);
  RUN_TEST(test_keyboard_and_touch);
  RUN_TEST(test_files_profile_big_upload_streamed);
  RUN_TEST(test_apps_profile_raw_and_multipart);
  RUN_TEST(test_settings_profile_form);
}
