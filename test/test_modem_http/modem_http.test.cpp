#include <app_api.h>
#include <modem_at_rx.h>
#include <modem_http.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unity.h>

void setUp() {}
void tearDown() {}

// ---- URL ----

// The Print app's printer (IPP over plain http on port 631)
void test_url_plain_http_with_port() {
  modem_url_t u;
  TEST_ASSERT_TRUE(modem_url_parse("http://192.168.1.40:631/ipp/print", &u));
  TEST_ASSERT_FALSE(u.tls);
  TEST_ASSERT_EQUAL_STRING("192.168.1.40", u.host);
  TEST_ASSERT_EQUAL(631, u.port);
  TEST_ASSERT_EQUAL_STRING("/ipp/print", u.path);
}

void test_url_https_default_port_and_query() {
  modem_url_t u;
  TEST_ASSERT_TRUE(modem_url_parse("https://trmnl.app?x=1", &u));
  TEST_ASSERT_TRUE(u.tls);
  TEST_ASSERT_EQUAL(443, u.port);
  TEST_ASSERT_EQUAL_STRING("trmnl.app", u.host);
  TEST_ASSERT_EQUAL_STRING("?x=1", u.path);
}

void test_url_bad_ones_refused() {
  modem_url_t u;
  TEST_ASSERT_FALSE(modem_url_parse("ftp://x/y", &u));
  TEST_ASSERT_FALSE(modem_url_parse("http://", &u));
  TEST_ASSERT_FALSE(modem_url_parse("http://h:0/", &u));
  TEST_ASSERT_FALSE(modem_url_parse("http://h:70000/", &u));
  TEST_ASSERT_FALSE(modem_url_parse("http://h:12x/", &u));
  TEST_ASSERT_FALSE(modem_url_parse("http://user@h/", &u));
  TEST_ASSERT_FALSE(modem_url_parse("http://h/a b", &u)); // a space would split the request line
  TEST_ASSERT_FALSE(modem_url_parse("http://h/a\r\nX: y", &u));
  TEST_ASSERT_FALSE(modem_url_parse("http://h\"/", &u)); // quoted into AT+CIPSTART
  TEST_ASSERT_FALSE(modem_url_parse(nullptr, &u));
}

// ---- request head ----

void test_head_post_with_content_type_and_length() {
  modem_url_t u;
  modem_url_parse("http://192.168.1.40:631/ipp/print", &u);
  const char *const h[] = {"Content-Type: application/ipp", nullptr};
  char b[MODEM_HEAD_MAX];
  size_t n = modem_http_head(b, sizeof(b), "POST", &u, h, 1048576);
  TEST_ASSERT_EQUAL(strlen(b), n);
  TEST_ASSERT_EQUAL_STRING("POST /ipp/print HTTP/1.1\r\nHost: 192.168.1.40:631\r\nUser-Agent: TRMNL-X\r\n"
                           "Connection: close\r\nContent-Length: 1048576\r\nContent-Type: application/ipp\r\n\r\n",
                           b);
}

void test_head_get_without_length_default_port_hidden() {
  modem_url_t u;
  modem_url_parse("https://example.com", &u);
  char b[512];
  modem_http_head(b, sizeof(b), nullptr, &u, nullptr, 0);
  TEST_ASSERT_EQUAL_STRING("GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: TRMNL-X\r\nConnection: close\r\n\r\n", b);
  modem_url_parse("https://example.com/a#frag", &u);
  modem_http_head(b, sizeof(b), "GET", &u, nullptr, 0);
  TEST_ASSERT_EQUAL(0, strncmp(b, "GET /a HTTP/1.1\r\n", 17));
}

void test_head_skips_bad_and_reserved_headers() {
  modem_url_t u;
  modem_url_parse("http://h/", &u);
  std::string long_name(64, 'a');
  long_name += ": v";
  const char *const h[] = {"no colon", ":empty", long_name.c_str(), "Host: evil", "content-length: 5",
                           "Connection: keep-alive", "X-A: 1\r\nX-B: 2", "Accept: */*", nullptr};
  char b[1024];
  modem_http_head(b, sizeof(b), "GET", &u, h, 0);
  TEST_ASSERT_EQUAL_STRING("GET / HTTP/1.1\r\nHost: h\r\nUser-Agent: TRMNL-X\r\nConnection: close\r\nAccept: */*\r\n\r\n",
                           b);
}

void test_head_too_big_is_zero() {
  modem_url_t u;
  modem_url_parse("http://h/", &u);
  char b[40];
  TEST_ASSERT_EQUAL(0, modem_http_head(b, sizeof(b), "GET", &u, nullptr, 0));
}

// ---- body chunks: 1 MB goes out in AT+CIPSEND pieces of at most 8192, straight from the app's buffer ----

void test_one_megabyte_body_in_cipsend_chunks() {
  const size_t total = 1048576 + 123;
  size_t sent = 0, chunks = 0, c;
  while ((c = modem_send_chunk(sent, total)) > 0) {
    TEST_ASSERT_TRUE(c <= MODEM_SEND_MAX);
    sent += c;
    chunks++;
  }
  TEST_ASSERT_EQUAL(total, sent);
  TEST_ASSERT_EQUAL(129, chunks);
  TEST_ASSERT_EQUAL(0, modem_send_chunk(0, 0));
  TEST_ASSERT_EQUAL(5, modem_send_chunk(8192, 8197));
}

// ---- AT stream ----

typedef struct {
  std::string lines, payload;
  int prompts;
} rx_seen_t;

static void on_line(void *c, const char *l) { ((rx_seen_t *)c)->lines += std::string(l) + "|"; }
static void on_prompt(void *c) { ((rx_seen_t *)c)->prompts++; }
static void on_payload(void *c, const uint8_t *d, size_t n) { ((rx_seen_t *)c)->payload.append((const char *)d, n); }

static void feed_bytewise(modem_at_rx_t *rx, const std::string &s, const modem_at_sink_t *k, size_t step) {
  for (size_t i = 0; i < s.size(); i += step) {
    size_t n = s.size() - i < step ? s.size() - i : step;
    modem_at_rx_feed(rx, (const uint8_t *)s.data() + i, n, k);
  }
}

void test_at_stream_splits_lines_prompt_and_ipd_in_any_pieces() {
  // a frame whose data holds CRLF, "OK" and "+IPD," itself, between the CIPSEND prompt and SEND OK
  std::string s = "AT+CIPSEND=5\r\n\r\nOK\r\n\r\n>\r\n+IPD,13:HTTP\r\nOK+IPD,\r\nRecv 5 bytes\r\n\r\nSEND OK\r\nCLOSED\r\n";
  for (size_t step = 1; step <= s.size(); step++) {
    rx_seen_t seen = {};
    modem_at_sink_t k = {on_line, on_prompt, on_payload, &seen};
    modem_at_rx_t rx;
    modem_at_rx_init(&rx);
    feed_bytewise(&rx, s, &k, step);
    TEST_ASSERT_EQUAL_STRING("AT+CIPSEND=5|OK|Recv 5 bytes|SEND OK|CLOSED|", seen.lines.c_str());
    TEST_ASSERT_EQUAL_STRING("HTTP\r\nOK+IPD,", seen.payload.c_str());
    TEST_ASSERT_EQUAL(1, seen.prompts);
  }
}

void test_at_stream_binary_payload_and_bad_frame() {
  std::string data("\0\n>\xff", 4);
  std::string s = "+IPD,4:" + data + "+IPD,x:y\r\nlong" + std::string(300, 'z') + "\r\nOK\r\n";
  rx_seen_t seen = {};
  modem_at_sink_t k = {on_line, on_prompt, on_payload, &seen};
  modem_at_rx_t rx;
  modem_at_rx_init(&rx);
  feed_bytewise(&rx, s, &k, 7);
  TEST_ASSERT_EQUAL(4, seen.payload.size());
  TEST_ASSERT_TRUE(seen.payload == data);
  TEST_ASSERT_EQUAL(0, seen.prompts);
  TEST_ASSERT_EQUAL(1, rx.bad_frames); // "+IPD,x": no length, passed on as a line
  TEST_ASSERT_NOT_NULL(strstr(seen.lines.c_str(), "+IPD,x:y|"));
  TEST_ASSERT_NOT_NULL(strstr(seen.lines.c_str(), "|OK|"));
  TEST_ASSERT_EQUAL(MODEM_AT_LINE_MAX - 1, seen.lines.find('|', seen.lines.find("long")) - seen.lines.find("long"));
}

// ---- response ----

static void feed_str(modem_resp_t *r, const std::string &s, size_t step) {
  for (size_t i = 0; i < s.size(); i += step) {
    size_t n = s.size() - i < step ? s.size() - i : step;
    modem_resp_feed(r, (const uint8_t *)s.data() + i, n);
  }
}

void test_response_content_length_into_buffer() {
  std::string s = "HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nContent-Length: 9\r\n\r\n";
  s += std::string("\x02\x00\x00\x00\x00\x00\x00\x01\x03", 9) + "trailing bytes are not body";
  for (size_t step = 1; step < 20; step++) {
    char buf[32];
    modem_resp_t r;
    modem_resp_init(&r, buf, sizeof(buf), false);
    feed_str(&r, s, step);
    TEST_ASSERT_EQUAL(MODEM_RESP_DONE, r.state);
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_EQUAL(9, r.got);
    TEST_ASSERT_FALSE(r.truncated);
    TEST_ASSERT_EQUAL(3, buf[8]);
  }
}

void test_response_truncated_like_the_s3_path() {
  std::string s = "HTTP/1.0 404 Not Found\r\n\r\n0123456789";
  char buf[4];
  modem_resp_t r;
  modem_resp_init(&r, buf, sizeof(buf), false);
  feed_str(&r, s, 3);
  TEST_ASSERT_EQUAL(404, r.status);
  TEST_ASSERT_EQUAL(4, r.got);
  TEST_ASSERT_TRUE(r.truncated);
  TEST_ASSERT_TRUE(modem_resp_enough(&r)); // stop reading, close
  TEST_ASSERT_EQUAL(0, memcmp(buf, "0123", 4));
}

void test_response_exactly_fitting_is_not_truncated() {
  char buf[4];
  modem_resp_t r;
  modem_resp_init(&r, buf, sizeof(buf), false);
  feed_str(&r, "HTTP/1.1 200 OK\r\n\r\nabcd", 5);
  TEST_ASSERT_FALSE(modem_resp_enough(&r));
  TEST_ASSERT_TRUE(modem_resp_closed(&r)); // no length: the close ends it
  TEST_ASSERT_FALSE(r.truncated);
  TEST_ASSERT_EQUAL(4, r.got);
}

void test_response_chunked_and_100_continue() {
  std::string s = "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 201 Created\r\nTransfer-Encoding: chunked\r\n\r\n"
                  "4\r\nWiki\r\n6;x=y\r\npedia \r\n0\r\nX-T: 1\r\n\r\n";
  for (size_t step = 1; step < 12; step++) {
    char buf[64] = {};
    modem_resp_t r;
    modem_resp_init(&r, buf, sizeof(buf), false);
    feed_str(&r, s, step);
    TEST_ASSERT_EQUAL(MODEM_RESP_DONE, r.state);
    TEST_ASSERT_EQUAL(201, r.status);
    TEST_ASSERT_EQUAL_STRING("Wikipedia ", buf);
  }
}

void test_response_head_and_204_have_no_body() {
  modem_resp_t r;
  modem_resp_init(&r, nullptr, 0, true);
  feed_str(&r, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n", 4);
  TEST_ASSERT_EQUAL(MODEM_RESP_DONE, r.state);
  modem_resp_init(&r, nullptr, 0, false);
  feed_str(&r, "HTTP/1.1 204 No Content\r\n\r\n", 4);
  TEST_ASSERT_EQUAL(MODEM_RESP_DONE, r.state);
}

void test_response_bad_or_cut_off() {
  char buf[8];
  modem_resp_t r;
  modem_resp_init(&r, buf, sizeof(buf), false);
  feed_str(&r, "SSH-2.0-OpenSSH\r\n", 4);
  TEST_ASSERT_EQUAL(MODEM_RESP_BAD, r.state);
  modem_resp_init(&r, buf, sizeof(buf), false);
  feed_str(&r, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc", 4);
  TEST_ASSERT_FALSE(modem_resp_closed(&r)); // closed before its length: incomplete
  modem_resp_init(&r, buf, sizeof(buf), false);
  TEST_ASSERT_FALSE(modem_resp_closed(&r)); // closed before any status line
  modem_resp_init(&r, buf, sizeof(buf), false);
  feed_str(&r, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n", 4);
  TEST_ASSERT_EQUAL(MODEM_RESP_BAD, r.state);
}

// ---- errors: the same codes an app sees on the S3 path ----

void test_error_mapping() {
  TEST_ASSERT_EQUAL(0, modem_http_app_error(MODEM_HTTP_OK));
  TEST_ASSERT_EQUAL(APP_ENONET, modem_http_app_error(MODEM_HTTP_NO_MODEM));
  TEST_ASSERT_EQUAL(APP_EINVAL, modem_http_app_error(MODEM_HTTP_BAD_URL));
  TEST_ASSERT_EQUAL(APP_EINVAL, modem_http_app_error(MODEM_HTTP_HEAD_TOO_BIG));
  TEST_ASSERT_EQUAL(APP_ETIMEDOUT, modem_http_app_error(MODEM_HTTP_CONNECT_FAILED));
  TEST_ASSERT_EQUAL(APP_ETIMEDOUT, modem_http_app_error(MODEM_HTTP_SEND_FAILED));
  TEST_ASSERT_EQUAL(APP_ETIMEDOUT, modem_http_app_error(MODEM_HTTP_TIMEOUT));
  TEST_ASSERT_EQUAL(APP_ETIMEDOUT, modem_http_app_error(MODEM_HTTP_BAD_RESPONSE));
  TEST_ASSERT_EQUAL_STRING("connect failed (DNS, refused or TLS)", modem_http_fail_name(MODEM_HTTP_CONNECT_FAILED));
}

// A 1xx is not an answer; a status line is, also when the body upload failed (413 and close) or was cut short
void test_outcome_per_response_state() {
  modem_resp_t r;
  modem_resp_init(&r, nullptr, 0, false);
  TEST_ASSERT_TRUE(modem_resp_wants_body(&r));
  TEST_ASSERT_EQUAL(MODEM_HTTP_TIMEOUT, modem_http_outcome(&r, true, true));
  TEST_ASSERT_EQUAL(MODEM_HTTP_BAD_RESPONSE, modem_http_outcome(&r, true, false)); // closed without a word
  TEST_ASSERT_EQUAL(MODEM_HTTP_SEND_FAILED, modem_http_outcome(&r, false, false));
  feed_str(&r, "HTTP/1.1 100 Continue\r\n\r\n", 5);
  TEST_ASSERT_TRUE(modem_resp_wants_body(&r));
  TEST_ASSERT_EQUAL(MODEM_HTTP_TIMEOUT, modem_http_outcome(&r, true, true));
  feed_str(&r, "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 10\r\n\r\nabc", 5);
  TEST_ASSERT_FALSE(modem_resp_wants_body(&r));
  TEST_ASSERT_EQUAL(MODEM_HTTP_OK, modem_http_outcome(&r, false, false));
  TEST_ASSERT_EQUAL(413, r.status);
  modem_resp_init(&r, nullptr, 0, false);
  feed_str(&r, "garbage\r\n", 5);
  TEST_ASSERT_FALSE(modem_resp_wants_body(&r));
  TEST_ASSERT_EQUAL(MODEM_HTTP_BAD_RESPONSE, modem_http_outcome(&r, true, true));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_url_plain_http_with_port);
  RUN_TEST(test_url_https_default_port_and_query);
  RUN_TEST(test_url_bad_ones_refused);
  RUN_TEST(test_head_post_with_content_type_and_length);
  RUN_TEST(test_head_get_without_length_default_port_hidden);
  RUN_TEST(test_head_skips_bad_and_reserved_headers);
  RUN_TEST(test_head_too_big_is_zero);
  RUN_TEST(test_one_megabyte_body_in_cipsend_chunks);
  RUN_TEST(test_at_stream_splits_lines_prompt_and_ipd_in_any_pieces);
  RUN_TEST(test_at_stream_binary_payload_and_bad_frame);
  RUN_TEST(test_response_content_length_into_buffer);
  RUN_TEST(test_response_truncated_like_the_s3_path);
  RUN_TEST(test_response_exactly_fitting_is_not_truncated);
  RUN_TEST(test_response_chunked_and_100_continue);
  RUN_TEST(test_response_head_and_204_have_no_body);
  RUN_TEST(test_response_bad_or_cut_off);
  RUN_TEST(test_error_mapping);
  RUN_TEST(test_outcome_per_response_state);
  return UNITY_END();
}
