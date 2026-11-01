#include <unity.h>

#include "harness.h"

// ---- links: open, close, limit, cut, idle ----

void test_links_open_close_limit() {
  start(WS_PROFILE_WRITER);
  for (int i = 0; i < WS_MSRV_LINKS; i++) ws_msrv_connect(&SRV, i);
  TEST_ASSERT_EQUAL(WS_MSRV_LINKS, ws_msrv_open_links(&SRV));
  ws_msrv_connect(&SRV, WS_MSRV_LINKS); // past the limit: closed at once
  TEST_ASSERT_EQUAL(WS_MSRV_LINKS, ws_msrv_open_links(&SRV));
  TEST_ASSERT_EQUAL(1, (int)F.closed.size());
  TEST_ASSERT_EQUAL(WS_MSRV_LINKS, F.closed[0]);
  ws_msrv_connect(&SRV, -1);
  TEST_ASSERT_EQUAL(2u, SRV.refused);
  ws_msrv_closed(&SRV, 2);
  ws_msrv_closed(&SRV, 2); // twice: nothing
  ws_msrv_closed(&SRV, 9);
  TEST_ASSERT_EQUAL(WS_MSRV_LINKS - 1, ws_msrv_open_links(&SRV));
  ws_msrv_connect(&SRV, 2);
  ws_msrv_connect(&SRV, 2); // a CLOSED we missed: a fresh request
  TEST_ASSERT_EQUAL(WS_MSRV_LINKS, ws_msrv_open_links(&SRV));
  const char *g = "GET / HTTP/1.1\r\n\r\n";
  ws_msrv_data(&SRV, 7, (const uint8_t *)g, strlen(g)); // an unknown link: dropped
  ws_msrv_data(&SRV, 3, (const uint8_t *)g, strlen(g));
  pump();
  TEST_ASSERT_EQUAL(200, status(F.sent[3]));
  TEST_ASSERT_EQUAL(0u, F.sent.count(7));
  TEST_ASSERT_EQUAL(WS_MSRV_LINKS - 1, ws_msrv_open_links(&SRV)); // answered and closed
  TEST_ASSERT_EQUAL(3, F.closed.back());
  ws_msrv_end(&SRV);
  TEST_ASSERT_EQUAL(0, ws_msrv_open_links(&SRV));
}

void test_links_interleaved() {
  start(WS_PROFILE_WRITER);
  std::string a = "GET /kbd.js HTTP/1.1\r\nHost: x\r\n\r\n";
  std::string b = std::string("POST /api/login HTTP/1.1\r\nX-Writer: 1\r\nContent-Type: application/x-www-form-urlencoded\r\n"
                              "Content-Length: 10\r\n\r\npin=") + WP.auth.pin;
  ws_msrv_connect(&SRV, 0);
  ws_msrv_connect(&SRV, 4);
  for (size_t i = 0; i < b.size(); i++) { // a byte of each in turn
    if (i < a.size()) ws_msrv_data(&SRV, 0, (const uint8_t *)a.data() + i, 1);
    ws_msrv_data(&SRV, 4, (const uint8_t *)b.data() + i, 1);
    ws_msrv_poll(&SRV);
  }
  pump();
  TEST_ASSERT_EQUAL(200, status(F.sent[0]));
  TEST_ASSERT_EQUAL_STRING("kbdjs", body(F.sent[0]).c_str());
  TEST_ASSERT_EQUAL(200, status(F.sent[4]));
  TEST_ASSERT_TRUE(hdr(F.sent[4], "Set-Cookie").find("wsid=") == 0);
}

void test_link_cut_mid_upload_drops_the_temporary_file() {
  start(WS_PROFILE_WRITER);
  std::string c = login();
  std::string h = "POST /api/put?p=documents%2FA%2Fb.md HTTP/1.1\r\nCookie: wsid=" + c +
                  "\r\nX-Writer: 1\r\nContent-Length: 20000\r\n\r\n";
  ws_msrv_connect(&SRV, 1);
  ws_msrv_data(&SRV, 1, (const uint8_t *)h.data(), h.size());
  std::string part(9000, 'x');
  ws_msrv_data(&SRV, 1, (const uint8_t *)part.data(), part.size());
  pump();
  TEST_ASSERT_NOT_NULL(WP.upload_owner);
  TEST_ASSERT_TRUE(APP_FS->has("documents/A/.webshare.tmp"));
  ws_msrv_closed(&SRV, 1); // the phone left
  TEST_ASSERT_NULL(WP.upload_owner);
  TEST_ASSERT_FALSE(APP_FS->has("documents/A/.webshare.tmp"));
  TEST_ASSERT_FALSE(APP_FS->has("documents/A/b.md"));
  TEST_ASSERT_EQUAL(0, F.locks);
  // the next upload is not refused as busy
  std::string r = post("/api/put?p=documents%2FA%2Fb.md", "hello", c, "text/markdown");
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_EQUAL_STRING("hello", APP_FS->get("documents/A/b.md").c_str());
}

void test_link_idle_is_cut() {
  start(WS_PROFILE_WRITER);
  ws_msrv_connect(&SRV, 0);
  const char *g = "GET / HTTP/1.1\r\nHost";
  ws_msrv_data(&SRV, 0, (const uint8_t *)g, strlen(g));
  F.now += WS_MSRV_IDLE_MS - 1;
  ws_msrv_poll(&SRV);
  TEST_ASSERT_EQUAL(1, ws_msrv_open_links(&SRV));
  F.now += 1;
  ws_msrv_poll(&SRV);
  TEST_ASSERT_EQUAL(0, ws_msrv_open_links(&SRV));
  TEST_ASSERT_EQUAL(0, F.closed.back());
}

void test_send_fails_mid_download_closes_the_file() {
  start(WS_PROFILE_WRITER);
  std::string c = login();
  std::string doc(30000, 'd');
  TEST_ASSERT_EQUAL(200, status(post("/api/put?p=documents%2FA%2Fbig.md", doc, c, "")));
  F.send_fail_after = F.sends + 2; // the head and one chunk, then the phone is gone
  std::string r = get("/api/get?p=documents%2FA%2Fbig.md", c);
  TEST_ASSERT_EQUAL(0, ws_msrv_open_links(&SRV));
  TEST_ASSERT_TRUE(r.size() < doc.size());
  TEST_ASSERT_EQUAL(0, (int)APP_FS->open_handles.size()); // the file was closed
  F.send_fail_after = -1;
  r = get("/api/get?p=documents%2FA%2Fbig.md", c);
  TEST_ASSERT_EQUAL(200, status(r));
  TEST_ASSERT_TRUE(body(r) == doc);
  TEST_ASSERT_TRUE(F.biggest_send <= WS_MSRV_CHUNK);
}

void test_refusals_before_any_handler() {
  start(WS_PROFILE_WRITER);
  std::string c = login();
  std::string r = ask("POST /api/put?p=documents%2FA%2Fb.md HTTP/1.1\r\nTransfer-Encoding: chunked\r\nCookie: wsid=" + c +
                      "\r\nX-Writer: 1\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
  TEST_ASSERT_EQUAL(411, status(r)); // an upload's size is checked before it starts
  r = post("/api/login", "pin=" + std::string(WS_MSRV_FORM_MAX, '1'));
  TEST_ASSERT_EQUAL(413, status(r));
  TEST_ASSERT_EQUAL(404, status(get("/nothing")));
  TEST_ASSERT_EQUAL(501, status(ask("DELETE / HTTP/1.1\r\n\r\n")));
  TEST_ASSERT_EQUAL(0, ws_msrv_open_links(&SRV));
}

void setUp() {}
void tearDown() {}

#include "page_tests.h"

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_links_open_close_limit);
  RUN_TEST(test_links_interleaved);
  RUN_TEST(test_link_cut_mid_upload_drops_the_temporary_file);
  RUN_TEST(test_link_idle_is_cut);
  RUN_TEST(test_send_fails_mid_download_closes_the_file);
  RUN_TEST(test_refusals_before_any_handler);
  run_page_tests();
  return UNITY_END();
}
