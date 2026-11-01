#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include <ws_http.h>
#include <ws_http_parse.h>
#include <ws_multipart.h>

// ---- the request parser ----

struct Seen {
  int heads = 0, dones = 0;
  std::string body;
};

static void s_head(void *c) { ((Seen *)c)->heads++; }
static void s_body(void *c, const uint8_t *d, size_t n) { ((Seen *)c)->body.append((const char *)d, n); }
static void s_done(void *c) { ((Seen *)c)->dones++; }

// Feeds `req` split at `cut` (and, when `step`, in pieces of `step` bytes); returns the bytes taken
static size_t feed(ws_hp_t &p, Seen &s, const std::string &req, size_t cut, size_t step = 0) {
  ws_hp_sink_t sink = {s_head, s_body, s_done, &s};
  ws_hp_init(&p);
  size_t took = 0;
  std::vector<std::string> pieces;
  if (step) {
    for (size_t i = 0; i < req.size(); i += step) pieces.push_back(req.substr(i, step));
  } else {
    pieces.push_back(req.substr(0, cut));
    pieces.push_back(req.substr(cut));
  }
  for (auto &pc : pieces) took += ws_hp_feed(&p, (const uint8_t *)pc.data(), pc.size(), &sink);
  return took;
}

static const char GET[] = "GET /api/get?p=documents%2FNovel%2FA+b.md&x=1 HTTP/1.1\r\nHost: 10.0.0.5\r\n"
                          "Cookie: other=1; wsid=0123456789abcdef\r\nX-Writer: 1\r\n\r\n";

void test_get_split_at_every_byte() {
  std::string req = GET;
  for (size_t cut = 0; cut <= req.size(); cut++) {
    ws_hp_t p;
    Seen s;
    TEST_ASSERT_EQUAL(req.size(), feed(p, s, req, cut));
    TEST_ASSERT_EQUAL_MESSAGE(WS_HP_DONE, p.state, std::to_string(cut).c_str());
    TEST_ASSERT_EQUAL(1, s.heads);
    TEST_ASSERT_EQUAL(1, s.dones);
    TEST_ASSERT_EQUAL(WS_GET, p.method);
    TEST_ASSERT_EQUAL_STRING("/api/get", p.target);
    TEST_ASSERT_EQUAL_STRING("p=documents%2FNovel%2FA+b.md&x=1", ws_hp_query(&p));
    TEST_ASSERT_EQUAL_STRING("other=1; wsid=0123456789abcdef", ws_hp_header(&p, "cookie"));
    TEST_ASSERT_EQUAL_STRING("1", ws_hp_header(&p, "X-WRITER"));
    TEST_ASSERT_NULL(ws_hp_header(&p, "Content-Type"));
  }
  char v[64];
  ws_hp_t p;
  Seen s;
  feed(p, s, req, 3);
  const char *q = ws_hp_query(&p);
  TEST_ASSERT_TRUE(ws_form_param(q, strlen(q), "p", v, sizeof(v)));
  TEST_ASSERT_EQUAL_STRING("documents/Novel/A b.md", v);
  TEST_ASSERT_FALSE(ws_form_param(q, strlen(q), "y", v, sizeof(v)));
}

void test_content_length_body_split_everywhere() {
  std::string body = "pin=123456&a=%41%zz";
  std::string req = "POST /api/login HTTP/1.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n\r\n" + body + "GET /next";
  for (size_t cut = 0; cut <= req.size(); cut++) {
    ws_hp_t p;
    Seen s;
    size_t took = feed(p, s, req, cut);
    TEST_ASSERT_EQUAL_MESSAGE(req.size() - 9, took, std::to_string(cut).c_str()); // the next request is not taken
    TEST_ASSERT_EQUAL(WS_HP_DONE, p.state);
    TEST_ASSERT_EQUAL_STRING(body.c_str(), s.body.c_str());
    TEST_ASSERT_EQUAL(1, s.dones);
  }
  char v[16];
  TEST_ASSERT_TRUE(ws_form_param(body.c_str(), body.size(), "a", v, sizeof(v)));
  TEST_ASSERT_EQUAL_STRING("A%zz", v); // a bad escape stays as it is
}

void test_chunked_body_in_pieces() {
  std::string req = "POST /api/settings HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
                    "5\r\nhello\r\n1;ext=1\r\n \r\nB\r\nworld-wide!\r\n0\r\nTrailer: x\r\n\r\n";
  for (size_t step = 1; step <= 7; step++) {
    ws_hp_t p;
    Seen s;
    feed(p, s, req, 0, step);
    TEST_ASSERT_EQUAL_MESSAGE(WS_HP_DONE, p.state, std::to_string(step).c_str());
    TEST_ASSERT_EQUAL_STRING("hello world-wide!", s.body.c_str());
    TEST_ASSERT_EQUAL(1, s.dones);
  }
  for (size_t cut = 0; cut <= req.size(); cut++) {
    ws_hp_t p;
    Seen s;
    feed(p, s, req, cut);
    TEST_ASSERT_EQUAL_STRING("hello world-wide!", s.body.c_str());
  }
}

void test_bad_requests() {
  struct { std::string req; int code; } bad[] = {
      {"GET /a HTTP/1.1\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n", 400}, // smuggling
      {"GET /a HTTP/1.1\r\nContent-Length: 4\r\nContent-Length: 5\r\n\r\n", 400},
      {"GET /a HTTP/1.1\r\nContent-Length: x\r\n\r\n", 400},
      {"GET /a HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\n", 501},
      {"PUT /a HTTP/1.1\r\n\r\n", 501},
      {"GET a HTTP/1.1\r\n\r\n", 400},
      {"GET /a FTP/1.0\r\n\r\n", 400},
      {"GET /" + std::string(600, 'x') + " HTTP/1.1\r\n\r\n", 414},
      {"GET /" + std::string(2000, 'x') + " HTTP/1.1\r\n\r\n", 414},
      {"POST /a HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n", 400},
      {"GET /a HTTP/1.1\r\nNoColon\r\n\r\n", 400},
  };
  for (auto &b : bad) {
    ws_hp_t p;
    Seen s;
    feed(p, s, b.req, b.req.size() / 2);
    TEST_ASSERT_EQUAL_MESSAGE(WS_HP_ERROR, p.state, b.req.substr(0, 40).c_str());
    TEST_ASSERT_EQUAL_MESSAGE(b.code, p.error, b.req.substr(0, 40).c_str());
  }
  std::string many = "GET /a HTTP/1.1\r\n";
  for (int i = 0; i < 70; i++) many += "X-" + std::to_string(i) + ": 1\r\n";
  ws_hp_t p;
  Seen s;
  feed(p, s, many + "\r\n", 5);
  TEST_ASSERT_EQUAL(431, p.error);
}

void test_long_and_many_headers_are_skipped_not_fatal() {
  std::string req = "GET / HTTP/1.1\r\nUser-Agent: " + std::string(1500, 'u') + "\r\nAccept: " +
                    std::string(900, 'a') + "\r\nB: " + std::string(900, 'b') + "\r\nCookie: wsid=abc\r\n\r\n";
  ws_hp_t p;
  Seen s;
  feed(p, s, req, 0, 13);
  TEST_ASSERT_EQUAL(WS_HP_DONE, p.state);
  TEST_ASSERT_NULL(ws_hp_header(&p, "User-Agent")); // over one line's limit
  TEST_ASSERT_NOT_NULL(ws_hp_header(&p, "Accept"));
  TEST_ASSERT_EQUAL_STRING("wsid=abc", ws_hp_header(&p, "Cookie"));
}

void test_route_find_like_async_web_server() {
  static void (*nop)(WsRequest &) = [](WsRequest &) {};
  ws_route_t r[] = {{WS_GET, "/", nop, nullptr, nullptr},
                    {WS_GET, "/api/kbd", nop, nullptr, nullptr},
                    {WS_POST, "/api/kbd", nop, nullptr, nullptr},
                    {WS_POST, "/apps", nop, nullptr, nullptr}};
  TEST_ASSERT_EQUAL_PTR(&r[0], ws_route_find(r, 4, WS_GET, "/"));
  TEST_ASSERT_EQUAL_PTR(&r[1], ws_route_find(r, 4, WS_GET, "/api/kbd"));
  TEST_ASSERT_EQUAL_PTR(&r[2], ws_route_find(r, 4, WS_POST, "/api/kbd"));
  TEST_ASSERT_EQUAL_PTR(&r[3], ws_route_find(r, 4, WS_POST, "/apps/x"));
  TEST_ASSERT_NULL(ws_route_find(r, 4, WS_GET, "/apps"));
  TEST_ASSERT_NULL(ws_route_find(r, 4, WS_GET, "/api/kbdx"));
  TEST_ASSERT_NULL(ws_route_find(r, 4, WS_GET, "/x"));
}

// ---- multipart ----

struct Parts {
  std::vector<std::string> names, files, data;
  int ends = 0;
};
static void p_part(void *c, const char *name, const char *file) {
  Parts *p = (Parts *)c;
  p->names.push_back(name);
  p->files.push_back(file);
  p->data.push_back("");
}
static void p_data(void *c, const uint8_t *d, size_t n) { ((Parts *)c)->data.back().append((const char *)d, n); }
static void p_end(void *c) { ((Parts *)c)->ends++; }

void test_multipart_split_at_every_byte() {
  const char *ct = "multipart/form-data; boundary=----X1y2";
  // the file holds CR LF, "--" and a near-boundary, and ends in CR
  std::string file = std::string("ELF\r\n--\r\n------X1y\r\n------X1\0\x7f", 30) + "\r";
  std::string body = "preamble\r\n------X1y2\r\nContent-Disposition: form-data; name=\"note\"\r\n\r\nhi\r\n"
                     "------X1y2\r\nContent-Disposition: form-data; name=\"file\"; filename=\"app.elf\"\r\n"
                     "Content-Type: application/octet-stream\r\n\r\n" + file + "\r\n------X1y2--\r\nepilogue";
  for (size_t cut = 0; cut <= body.size(); cut++) {
    for (size_t cut2 = cut; cut2 <= body.size(); cut2 += 7) {
      ws_mp_t m;
      TEST_ASSERT_TRUE(ws_mp_init(&m, ct));
      Parts p;
      ws_mp_sink_t s = {p_part, p_data, p_end, &p};
      ws_mp_feed(&m, (const uint8_t *)body.data(), cut, &s);
      ws_mp_feed(&m, (const uint8_t *)body.data() + cut, cut2 - cut, &s);
      ws_mp_feed(&m, (const uint8_t *)body.data() + cut2, body.size() - cut2, &s);
      std::string where = std::to_string(cut) + "/" + std::to_string(cut2);
      TEST_ASSERT_TRUE_MESSAGE(ws_mp_done(&m), where.c_str());
      TEST_ASSERT_EQUAL(2, (int)p.names.size());
      TEST_ASSERT_EQUAL(2, p.ends);
      TEST_ASSERT_EQUAL_STRING("note", p.names[0].c_str());
      TEST_ASSERT_EQUAL_STRING("", p.files[0].c_str());
      TEST_ASSERT_EQUAL_STRING("hi", p.data[0].c_str());
      TEST_ASSERT_EQUAL_STRING("file", p.names[1].c_str());
      TEST_ASSERT_EQUAL_STRING("app.elf", p.files[1].c_str());
      TEST_ASSERT_EQUAL_MESSAGE(file.size(), p.data[1].size(), where.c_str());
      TEST_ASSERT_TRUE_MESSAGE(file == p.data[1], where.c_str());
    }
  }
}

void test_multipart_refusals() {
  ws_mp_t m;
  TEST_ASSERT_FALSE(ws_mp_init(&m, "application/octet-stream"));
  TEST_ASSERT_FALSE(ws_mp_init(&m, "multipart/form-data"));
  TEST_ASSERT_FALSE(ws_mp_init(&m, nullptr));
  TEST_ASSERT_FALSE(ws_mp_init(&m, ("multipart/form-data; boundary=" + std::string(71, 'b')).c_str()));
  TEST_ASSERT_TRUE(ws_mp_init(&m, "Multipart/Form-Data; charset=x; boundary=\"q q\""));
  Parts p;
  ws_mp_sink_t s = {p_part, p_data, p_end, &p};
  std::string cut = "--q q\r\nContent-Disposition: form-data; name=\"f\"; filename=\"a\"\r\n\r\nabc"; // no end
  ws_mp_feed(&m, (const uint8_t *)cut.data(), cut.size(), &s);
  TEST_ASSERT_FALSE(ws_mp_done(&m));
  TEST_ASSERT_EQUAL_STRING("abc", p.data[0].c_str());
  TEST_ASSERT_EQUAL(0, p.ends); // the upload sees no end: it is dropped as short
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_get_split_at_every_byte);
  RUN_TEST(test_content_length_body_split_everywhere);
  RUN_TEST(test_chunked_body_in_pieces);
  RUN_TEST(test_bad_requests);
  RUN_TEST(test_long_and_many_headers_are_skipped_not_fatal);
  RUN_TEST(test_route_find_like_async_web_server);
  RUN_TEST(test_multipart_split_at_every_byte);
  RUN_TEST(test_multipart_refusals);
  return UNITY_END();
}
