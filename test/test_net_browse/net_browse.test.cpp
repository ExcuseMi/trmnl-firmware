#include <app_api.h>
#include <net_browse.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_a_printer_is_added_once_and_cleaned() {
  nb_list_t l;
  nb_list_init(&l);
  TEST_ASSERT_EQUAL(1, nb_list_add(&l, "  Brother MFC\x01-L2710DW series ", "192.168.1.50", 631, "/ipp/print"));
  TEST_ASSERT_EQUAL_STRING("Brother MFC-L2710DW series", l.item[0].name);
  TEST_ASSERT_EQUAL_STRING("ipp/print", l.item[0].path);
  TEST_ASSERT_EQUAL(0, nb_list_add(&l, "Other name", "192.168.1.50", 631, "x"));
  TEST_ASSERT_EQUAL(1, l.n);
  TEST_ASSERT_EQUAL(1, nb_list_add(&l, "B", "192.168.1.50", 632, NULL));
  TEST_ASSERT_EQUAL(2, l.n);
}

void test_a_duplicate_fills_what_the_first_lacked() {
  nb_list_t l;
  nb_list_init(&l);
  nb_list_add(&l, "", "10.0.0.2", 631, NULL);
  nb_list_add(&l, "HP", "10.0.0.2", 631, "ipp/print");
  TEST_ASSERT_EQUAL_STRING("HP", l.item[0].name);
  TEST_ASSERT_EQUAL_STRING("ipp/print", l.item[0].path);
}

void test_bad_input_and_a_full_list_are_refused() {
  nb_list_t l;
  nb_list_init(&l);
  TEST_ASSERT_EQUAL(APP_EINVAL, nb_list_add(&l, "x", "", 631, NULL));
  TEST_ASSERT_EQUAL(APP_EINVAL, nb_list_add(&l, "x", NULL, 631, NULL));
  TEST_ASSERT_EQUAL(APP_EINVAL, nb_list_add(&l, "x", "a b", 631, NULL));
  TEST_ASSERT_EQUAL(APP_EINVAL, nb_list_add(&l, "x", "a\"b", 631, NULL));
  TEST_ASSERT_EQUAL(APP_EINVAL, nb_list_add(&l, "x", "h", 0, NULL));
  TEST_ASSERT_EQUAL(APP_EINVAL, nb_list_add(NULL, "x", "h", 1, NULL));
  char h[16];
  for (int i = 0; i < NB_MAX; i++) {
    snprintf(h, sizeof(h), "h%d", i);
    TEST_ASSERT_EQUAL(1, nb_list_add(&l, "n", h, 631, NULL));
  }
  TEST_ASSERT_EQUAL(APP_ENOMEM, nb_list_add(&l, "n", "extra", 631, NULL));
  TEST_ASSERT_EQUAL(NB_MAX, l.n);
  char longname[300];
  memset(longname, 'a', 299);
  longname[299] = 0;
  nb_list_init(&l);
  TEST_ASSERT_EQUAL(1, nb_list_add(&l, longname, "h", 631, NULL));
  TEST_ASSERT_EQUAL(NB_NAME_MAX - 1, (int)strlen(l.item[0].name));
}

void test_rp_must_be_a_safe_path() {
  char o[NB_PATH_MAX];
  TEST_ASSERT_TRUE(nb_rp_clean("//ipp/print", o, sizeof(o)));
  TEST_ASSERT_EQUAL_STRING("ipp/print", o);
  TEST_ASSERT_FALSE(nb_rp_clean("", o, sizeof(o)));
  TEST_ASSERT_FALSE(nb_rp_clean("/", o, sizeof(o)));
  TEST_ASSERT_FALSE(nb_rp_clean("a/../b", o, sizeof(o)));
  TEST_ASSERT_FALSE(nb_rp_clean("a b", o, sizeof(o)));
  TEST_ASSERT_FALSE(nb_rp_clean("a\r\nb", o, sizeof(o)));
  TEST_ASSERT_FALSE(nb_rp_clean("a?x=1", o, sizeof(o)));
  TEST_ASSERT_EQUAL_STRING("", o);
  char big[80];
  memset(big, 'a', 79);
  big[79] = 0;
  TEST_ASSERT_FALSE(nb_rp_clean(big, o, sizeof(o)));
  TEST_ASSERT_FALSE(nb_rp_clean("ipp/print", o, 5));
}

void test_the_walk_goes_outward_skips_self_and_ends() {
  uint32_t self = (192u << 24) | (168u << 16) | (1u << 8) | 100u, a;
  int step = 0;
  TEST_ASSERT_TRUE(nb_walk_next(self, &step, &a));
  TEST_ASSERT_EQUAL_UINT32(self + 1, a);
  TEST_ASSERT_TRUE(nb_walk_next(self, &step, &a));
  TEST_ASSERT_EQUAL_UINT32(self - 1, a);
  int count = 2;
  bool seen[256] = {};
  seen[101] = seen[99] = true;
  while (nb_walk_next(self, &step, &a)) {
    count++;
    TEST_ASSERT_EQUAL_UINT32(self & 0xffffff00u, a & 0xffffff00u);
    TEST_ASSERT_TRUE((a & 255) >= 1 && (a & 255) <= 254 && (a & 255) != 100);
    TEST_ASSERT_FALSE(seen[a & 255]);
    seen[a & 255] = true;
  }
  TEST_ASSERT_EQUAL(253, count);
  TEST_ASSERT_FALSE(nb_walk_next(self, &step, &a));
}

void test_the_walk_at_the_edge() {
  uint32_t self = (10u << 24) | 254u, a;
  int step = 0, count = 0;
  while (nb_walk_next(self, &step, &a)) count++;
  TEST_ASSERT_EQUAL(253, count);
  TEST_ASSERT_FALSE(nb_walk_next(self, nullptr, &a));
}

void test_ip_text_round_trip_and_garbage() {
  char s[20];
  uint32_t ip;
  nb_ip_str(0xC0A80164u, s, sizeof(s));
  TEST_ASSERT_EQUAL_STRING("192.168.1.100", s);
  TEST_ASSERT_TRUE(nb_ip_parse(s, &ip));
  TEST_ASSERT_EQUAL_UINT32(0xC0A80164u, ip);
  TEST_ASSERT_FALSE(nb_ip_parse("1.2.3", &ip));
  TEST_ASSERT_FALSE(nb_ip_parse("1.2.3.256", &ip));
  TEST_ASSERT_FALSE(nb_ip_parse("1.2.3.4x", &ip));
  TEST_ASSERT_FALSE(nb_ip_parse("", &ip));
  TEST_ASSERT_FALSE(nb_ip_parse("1.2.3.4444", &ip));
}

static size_t reply(uint8_t *b, const char *model, int status) {
  size_t n = 0;
  b[n++] = 1; b[n++] = 1; b[n++] = (uint8_t)(status >> 8); b[n++] = (uint8_t)status;
  b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 1;
  b[n++] = 0x01;
  b[n++] = 0x47; b[n++] = 0; b[n++] = 18; memcpy(b + n, "attributes-charset", 18); n += 18; b[n++] = 0; b[n++] = 5; memcpy(b + n, "utf-8", 5); n += 5;
  b[n++] = 0x04;
  if (model) {
    b[n++] = 0x41; b[n++] = 0; b[n++] = 22; memcpy(b + n, "printer-make-and-model", 22); n += 22;
    b[n++] = 0; b[n++] = (uint8_t)strlen(model); memcpy(b + n, model, strlen(model)); n += strlen(model);
  }
  b[n++] = 0x03;
  return n;
}

void test_the_ipp_query_and_head() {
  uint8_t b[300];
  size_t n = nb_ipp_query(b, sizeof(b), "192.168.1.50", 631, "ipp/print");
  TEST_ASSERT_TRUE(n > 60);
  TEST_ASSERT_EQUAL(0x0B, b[3]);
  TEST_ASSERT_NOT_NULL(memmem(b, n, "ipp://192.168.1.50:631/ipp/print", 32));
  TEST_ASSERT_NOT_NULL(memmem(b, n, "printer-make-and-model", 22));
  TEST_ASSERT_EQUAL(0, nb_ipp_query(b, 40, "192.168.1.50", 631, "ipp/print"));
  TEST_ASSERT_EQUAL(0, nb_ipp_query(b, sizeof(b), "bad host", 631, "ipp/print"));
  TEST_ASSERT_EQUAL(0, nb_ipp_query(b, sizeof(b), "h", 631, "../x"));
  char h[200];
  size_t hl = nb_ipp_http_head(h, sizeof(h), "10.0.0.2", 631, "/ipp/print", 99);
  TEST_ASSERT_TRUE(hl > 0);
  TEST_ASSERT_NOT_NULL(strstr(h, "POST /ipp/print HTTP/1.1\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(h, "Content-Length: 99\r\n"));
  TEST_ASSERT_EQUAL(0, nb_ipp_http_head(h, 20, "10.0.0.2", 631, "ipp/print", 99));
}

void test_the_model_is_read_from_a_reply_and_bad_replies_are_refused() {
  uint8_t b[300];
  char m[64];
  size_t n = reply(b, "Brother MFC-L2710DW series", 0);
  TEST_ASSERT_TRUE(nb_ipp_model(b, n, m, sizeof(m)));
  TEST_ASSERT_EQUAL_STRING("Brother MFC-L2710DW series", m);
  n = reply(b, NULL, 0);
  TEST_ASSERT_FALSE(nb_ipp_model(b, n, m, sizeof(m)));
  n = reply(b, "X", 0x0400);
  TEST_ASSERT_FALSE(nb_ipp_model(b, n, m, sizeof(m)));
  n = reply(b, "Brother", 0);
  for (size_t cut = 0; cut < n; cut++) { /* every truncation is refused or reads safely */
    bool ok = nb_ipp_model(b, cut, m, sizeof(m));
    TEST_ASSERT_TRUE(!ok || strcmp(m, "Brother") == 0);
  }
  TEST_ASSERT_FALSE(nb_ipp_model(b, n, m, 0));
  const uint8_t http[] = "HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\n\r\nBODY";
  TEST_ASSERT_EQUAL(sizeof(http) - 1 - 4, nb_http_body_at(http, sizeof(http) - 1));
  TEST_ASSERT_EQUAL(0, nb_http_body_at((const uint8_t *)"HTTP/1.1 200", 12));
}

void test_service_names_are_split_or_refused() {
  char a[17], b[17];
  TEST_ASSERT_TRUE(nb_service_split("_ipp._tcp", a, b, sizeof(a)));
  TEST_ASSERT_EQUAL_STRING("_ipp", a);
  TEST_ASSERT_EQUAL_STRING("_tcp", b);
  TEST_ASSERT_FALSE(nb_service_split("ipp._tcp", a, b, sizeof(a)));
  TEST_ASSERT_FALSE(nb_service_split("_ipp._sctp", a, b, sizeof(a)));
  TEST_ASSERT_FALSE(nb_service_split("_ipp", a, b, sizeof(a)));
  TEST_ASSERT_FALSE(nb_service_split("_._tcp", a, b, sizeof(a)));
  TEST_ASSERT_FALSE(nb_service_split("_i pp._tcp", a, b, sizeof(a)));
  TEST_ASSERT_FALSE(nb_service_split("_aaaaaaaaaaaaaaaaaaaa._tcp", a, b, sizeof(a)));
  TEST_ASSERT_FALSE(nb_service_split(NULL, a, b, sizeof(a)));
  TEST_ASSERT_FALSE(nb_service_split("_ipp._tcp", a, b, 8));
}

// The modem probe (one connect at a time in ESP-AT): its own short timeout, so the /24 fits the budget
void test_the_probe_fits_the_budget() {
  TEST_ASSERT_EQUAL(47, nb_probe_ms(15000)); // 253 addresses * 47 ms = 11.9 s of the 15 s
  TEST_ASSERT_TRUE(253 * nb_probe_ms(15000) <= 15000 * 4 / 5);
  TEST_ASSERT_EQUAL(NB_PROBE_MIN_MS, nb_probe_ms(1000));
  TEST_ASSERT_EQUAL(NB_PROBE_MAX_MS, nb_probe_ms(600000));
}

void test_cipstart_carries_the_timeout_and_the_link() {
  char b[96];
  TEST_ASSERT_TRUE(nb_cipstart(b, sizeof(b), 4, "192.168.1.7", 631, 47) > 0);
  TEST_ASSERT_EQUAL_STRING("AT+CIPSTART=4,\"TCP\",\"192.168.1.7\",631,,,47", b);
  TEST_ASSERT_TRUE(nb_cipstart(b, sizeof(b), -1, "10.0.0.2", 631, 300) > 0);
  TEST_ASSERT_EQUAL_STRING("AT+CIPSTART=\"TCP\",\"10.0.0.2\",631,,,300", b);
  TEST_ASSERT_EQUAL(0, nb_cipstart(b, sizeof(b), 1, "printer\",\"x", 631, 47)); // only an address goes in
  TEST_ASSERT_EQUAL(0, nb_cipstart(b, sizeof(b), 1, "1.2.3.4", 0, 47));
  TEST_ASSERT_EQUAL(0, nb_cipstart(b, 20, 1, "192.168.100.100", 631, 47)); // does not fit
}

// tcpOpen's quick probe: the connect timeout goes to the modem, the X waits a little longer than it (2026-11-02)
void test_a_quick_probe_gives_the_modem_its_timeout() {
  char b[96];
  TEST_ASSERT_EQUAL(47 + 2000, nb_tcp_start(b, sizeof(b), "192.168.1.40", 631, false, true, 47));
  TEST_ASSERT_EQUAL_STRING("AT+CIPSTART=\"TCP\",\"192.168.1.40\",631,,,47", b);
  TEST_ASSERT_EQUAL(0, nb_tcp_start(b, sizeof(b), "printer.local", 631, false, true, 47)); // a probe is an address
  TEST_ASSERT_EQUAL(5000, nb_tcp_start(b, sizeof(b), "example.com", 443, true, false, 3000)); // apps' HTTP as before
  TEST_ASSERT_EQUAL_STRING("AT+CIPSTART=\"SSL\",\"example.com\",443", b);
  TEST_ASSERT_EQUAL(0, nb_tcp_start(b, 20, "example.com", 443, true, false, 3000)); // does not fit
}

void test_the_saved_printer_host_is_read() {
  uint32_t ip = 0;
  const char *c = "name=Office\nhost=192.168.1.40\nport=631\npath=ipp/print\n";
  TEST_ASSERT_TRUE(nb_cfg_host_ip(c, strlen(c), &ip));
  TEST_ASSERT_EQUAL_HEX32(0xc0a80128, ip);
  const char *d = "host=printer.local\n";
  TEST_ASSERT_FALSE(nb_cfg_host_ip(d, strlen(d), &ip)); // a name: the walk cannot probe it
  TEST_ASSERT_FALSE(nb_cfg_host_ip("", 0, &ip));
  const char *e = "name=x\nhost=10.0.0.9";
  TEST_ASSERT_TRUE(nb_cfg_host_ip(e, strlen(e), &ip)); // no newline at the end
  TEST_ASSERT_EQUAL_HEX32(0x0a000009, ip);
}

void test_a_free_link_next_to_the_page() {
  TEST_ASSERT_EQUAL(4, nb_free_link(0, 5));
  TEST_ASSERT_EQUAL(3, nb_free_link(1u << 4, 5));
  TEST_ASSERT_EQUAL(0, nb_free_link(0x1e, 5));
  TEST_ASSERT_EQUAL(-1, nb_free_link(0x1f, 5)); // the phone holds all five
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_printer_is_added_once_and_cleaned);
  RUN_TEST(test_a_duplicate_fills_what_the_first_lacked);
  RUN_TEST(test_bad_input_and_a_full_list_are_refused);
  RUN_TEST(test_rp_must_be_a_safe_path);
  RUN_TEST(test_the_walk_goes_outward_skips_self_and_ends);
  RUN_TEST(test_the_walk_at_the_edge);
  RUN_TEST(test_ip_text_round_trip_and_garbage);
  RUN_TEST(test_the_ipp_query_and_head);
  RUN_TEST(test_the_model_is_read_from_a_reply_and_bad_replies_are_refused);
  RUN_TEST(test_service_names_are_split_or_refused);
  RUN_TEST(test_the_probe_fits_the_budget);
  RUN_TEST(test_cipstart_carries_the_timeout_and_the_link);
  RUN_TEST(test_a_free_link_next_to_the_page);
  RUN_TEST(test_a_quick_probe_gives_the_modem_its_timeout);
  RUN_TEST(test_the_saved_printer_host_is_read);
  return UNITY_END();
}
