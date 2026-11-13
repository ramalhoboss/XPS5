/* Host tests of src/link.c: the requests the XPS5 extension sends the
title, the session it carries, and the answers. The cookies here are made
up. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link.h"
#include "xapi.h"

static int g_failed;

#define CHECK(cond) do { \
  if(!(cond)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_failed++; \
  } \
} while(0)

#define AUTH "0123456789abcdef0123456789abcdef01234567"
#define CT0  "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"


static long
size_of(const char *s) {
  return link_request_size(s, strlen(s));
}


static void
test_size(void) {
  const char *whole = "GET /hello HTTP/1.1\r\nHost: x\r\n\r\n";
  const char *post = "POST /session HTTP/1.1\r\ncontent-length:  5 \r\n\r\nabcde";
  char big[LINK_REQUEST_MAX + 16];

  CHECK(size_of("GET /hello HTTP/1.1\r\nHost: x\r\n") == 0);
  CHECK(size_of(whole) == (long)strlen(whole));
  CHECK(size_of("POST /session HTTP/1.1\r\nContent-Length: 5\r\n\r\nabc") == 0);
  CHECK(size_of(post) == (long)strlen(post));
  CHECK(size_of("POST /session HTTP/1.1\r\nContent-Length: 9000\r\n\r\n") == -1);
  CHECK(size_of("POST /session HTTP/1.1\r\nContent-Length: 5x\r\n\r\nabcde") == -1);
  CHECK(size_of("POST /session HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\na") == -1);
  CHECK(size_of("POST /session HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n") == -1);
  memset(big, 'a', sizeof big);
  CHECK(link_request_size(big, sizeof big) == -1);
  CHECK(link_request_size(big, 100) == 0);
}


static void
test_request(void) {
  const char *get = "GET /hello?x=1 HTTP/1.1\r\nHost: 192.168.1.70:9070\r\n"
                    "Origin: chrome-extension://abcdefghijklmnop\r\n\r\n";
  const char *evil = "POST /session HTTP/1.1\r\norigin: http://example.com\r\nContent-Length: 2\r\n\r\n{}";
  const char *bare = "OPTIONS /session HTTP/1.1\r\nHost: x\r\n\r\n";
  const char *put = "PUT /session HTTP/1.1\r\n\r\n";
  link_request_t r;

  CHECK(!link_parse_request(get, strlen(get), &r));
  CHECK(r.method == LINK_GET && !strcmp(r.path, "/hello"));
  CHECK(!strcmp(r.origin, "chrome-extension://abcdefghijklmnop") && r.origin_ok);
  CHECK(r.body_len == 0);

  CHECK(!link_parse_request(evil, strlen(evil), &r));
  CHECK(r.method == LINK_POST && !strcmp(r.path, "/session") && !r.origin_ok);
  CHECK(r.body_len == 2 && !memcmp(r.body, "{}", 2));

  CHECK(!link_parse_request(bare, strlen(bare), &r));
  CHECK(r.method == LINK_OPTIONS && r.origin_ok && !r.origin[0]);

  CHECK(!link_parse_request(put, strlen(put), &r));
  CHECK(r.method == LINK_OTHER && !strcmp(r.path, "/session"));

  CHECK(link_parse_request("GET /x HTTP/1.1\r\n", 17, &r) == -1);
  CHECK(link_parse_request("GET x HTTP/1.1\r\n\r\n", 18, &r) == -1);
}


static int
session(const char *body, char *code, char *auth, char *ct0) {
  return link_parse_session(body, strlen(body), code, auth, 128, ct0, 256);
}


static void
test_session(void) {
  char code[LINK_CODE_LEN + 1], auth[128], ct0[256], text[512];
  x_session_t s;
  int n;

  CHECK(!session("{\"code\":\"012345\",\"auth_token\":\"" AUTH "\",\"ct0\":\"" CT0 "\"}", code, auth, ct0));
  CHECK(!strcmp(code, "012345") && !strcmp(auth, AUTH) && !strcmp(ct0, CT0));

  // what it writes, x_session_parse reads back
  CHECK((n=link_session_text(text, sizeof text, auth, ct0)) > 0);
  CHECK(!x_session_parse(&s, text, (size_t)n));
  CHECK(!strcmp(s.auth_token, AUTH) && !strcmp(s.ct0, CT0));
  CHECK(link_session_text(text, 20, auth, ct0) == -1);

  CHECK(session("{\"code\":\"012345\",\"auth_token\":\"" AUTH "\"}", code, auth, ct0) == -1);
  CHECK(!auth[0] && !code[0]);
  CHECK(session("{\"code\":123456,\"auth_token\":\"" AUTH "\",\"ct0\":\"" CT0 "\"}", code, auth, ct0) == -1);
  CHECK(session("not json", code, auth, ct0) == -1);
  CHECK(session("{\"code\":\"012345\",\"auth_token\":\"zz" AUTH "\",\"ct0\":\"" CT0 "\"}", code, auth, ct0) == -2);
  CHECK(session("{\"code\":\"012345\",\"auth_token\":\"abc\",\"ct0\":\"" CT0 "\"}", code, auth, ct0) == -2);
  CHECK(session("{\"code\":\"012345\",\"auth_token\":\"" AUTH "\",\"ct0\":\"" CT0 ";x=1\"}", code, auth, ct0) == -2);
  CHECK(session("{\"code\":\"01234\",\"auth_token\":\"" AUTH "\",\"ct0\":\"" CT0 "\"}", code, auth, ct0) == -3);
  CHECK(session("{\"code\":\"01234a\",\"auth_token\":\"" AUTH "\",\"ct0\":\"" CT0 "\"}", code, auth, ct0) == -3);
  CHECK(!auth[0] && !ct0[0]);
}


static void
test_response(void) {
  char buf[1024];
  const char *json = "{\"app\":\"XPS5\",\"v\":1}";
  int n;

  CHECK((n=link_response(buf, sizeof buf, 200, "chrome-extension://abcdefghijklmnop", json)) > 0);
  CHECK((size_t)n == strlen(buf));
  CHECK(!strncmp(buf, "HTTP/1.1 200 OK\r\n", 17));
  CHECK(strstr(buf, "\r\nAccess-Control-Allow-Origin: chrome-extension://abcdefghijklmnop\r\n"));
  CHECK(strstr(buf, "\r\nAccess-Control-Allow-Private-Network: true\r\n"));
  CHECK(strstr(buf, "\r\nContent-Length: 20\r\n"));
  CHECK(!strcmp(buf + n - strlen(json), json));

  CHECK((n=link_response(buf, sizeof buf, 403, "http://example.com", "{}")) > 0);
  CHECK(!strstr(buf, "Allow-Origin"));
  CHECK((n=link_response(buf, sizeof buf, 204, "chrome-extension://a\r\nX-Evil: 1", NULL)) > 0);
  CHECK(!strstr(buf, "Allow-Origin") && !strstr(buf, "Evil") && !strstr(buf, "Content-Type: application"));
  CHECK(strstr(buf, "HTTP/1.1 204 No Content\r\n") && strstr(buf, "Content-Length: 0\r\n"));
  CHECK(link_response(buf, 40, 200, NULL, json) == -1);

  // a file of the QR's page
  CHECK((n=link_file_head(buf, sizeof buf, "text/html; charset=utf-8", 1234)) > 0);
  CHECK((size_t)n == strlen(buf) && !strcmp(buf + n - 4, "\r\n\r\n"));
  CHECK(!strncmp(buf, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 1234\r\n", 78));
  CHECK(strstr(buf, "\r\nContent-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self';"));
  CHECK(strstr(buf, "\r\nX-Content-Type-Options: nosniff\r\n") && !strstr(buf, "Access-Control"));
  CHECK(link_file_head(buf, 64, "text/css", 10) == -1);
}


static void
test_code(void) {
  char a[LINK_CODE_LEN + 1], b[LINK_CODE_LEN + 1];
  int differ = 0;

  for(unsigned long long i=0; i < 64; i++) {
    link_code(i, a);
    link_code(i + 1, b);
    CHECK(strlen(a) == LINK_CODE_LEN && strspn(a, "0123456789") == LINK_CODE_LEN);
    differ += strcmp(a, b) != 0;
  }
  CHECK(differ >= 60);
  CHECK(link_same("123456", "123456"));
  CHECK(!link_same("123456", "123457"));
  CHECK(!link_same("123456", "12345"));
  CHECK(!link_same("", "1"));
  CHECK(link_mix(1) != link_mix(2));
}


static void
test_state(void) {
  link_state_t s, t;
  char line[160];

  memset(&s, 0, sizeof s);
  s.state = LINK_WAITING;
  s.last = LINK_WRONG_CODE;
  s.wrong = 2;
  s.refused = -1;
  s.error = (int)0x80410112u;
  s.account = 3;
  snprintf(s.ip, sizeof s.ip, "192.168.1.70");
  snprintf(s.code, sizeof s.code, "482913");
  CHECK(link_state_line(line, sizeof line, &s) > 0);
  CHECK(!strcmp(line, "link 1 1 2 0 -1 -2143223534 3 192.168.1.70 482913 - 0\n"));
  CHECK(!link_state_parse(line, &t));
  CHECK(!memcmp(&s, &t, sizeof s));
  line[strlen(line) - 1] = 0; // without its '\n' too
  CHECK(!link_state_parse(line, &t) && !memcmp(&s, &t, sizeof s));

  memset(&s, 0, sizeof s);
  s.state = LINK_DONE;
  s.account = 1;
  snprintf(s.user, sizeof s.user, "joao_2");
  CHECK(link_state_line(line, sizeof line, &s) > 0);
  CHECK(!link_state_parse(line, &t) && t.state == LINK_DONE && !strcmp(t.user, "joao_2") && !t.ip[0] &&
        !t.code[0]);
  CHECK(link_state_line(line, 20, &s) == -1);

  // the QR's page opened; and the line of a payload before version 6, without it
  memset(&s, 0, sizeof s);
  s.state = LINK_WAITING;
  s.seen = 1;
  snprintf(s.ip, sizeof s.ip, "10.0.0.5");
  snprintf(s.code, sizeof s.code, "000001");
  CHECK(link_state_line(line, sizeof line, &s) > 0);
  CHECK(!strcmp(line, "link 1 0 0 0 0 0 0 10.0.0.5 000001 - 1\n"));
  CHECK(!link_state_parse(line, &t) && !memcmp(&s, &t, sizeof s));
  CHECK(!link_state_parse("link 1 0 0 0 0 0 1 10.0.0.5 000001 -\n", &t) && t.state == LINK_WAITING && !t.seen &&
        !strcmp(t.code, "000001"));
  CHECK(link_state_parse("link 1 0 0 0 0 0 1 - - - 2", &t) == -1);         // seen is 0 or 1
  CHECK(link_state_parse("link 1 0 0 0 0 0 1 - - - 1 0", &t) == -1);
  CHECK(link_state_parse("link 1 0 0 0 0 0 1 - - - ", &t) == -1);

  // what the title must not take
  CHECK(link_state_parse("on 4", &t) == -1);
  CHECK(link_state_parse("link 9 0 0 0 0 0 1 - - -", &t) == -1);           // no such state
  CHECK(link_state_parse("link 1 7 0 0 0 0 1 - - -", &t) == -1);           // no such try
  CHECK(link_state_parse("link 1 0 0 2 0 0 1 - - -", &t) == -1);           // renewed is 0 or 1
  CHECK(link_state_parse("link 1 0 0 0 0 0 1 1.2.3.4 12345 -", &t) == -1);  // a short code
  CHECK(link_state_parse("link 1 0 0 0 0 0 1 1.2.3.4 1234567 -", &t) == -1); // a long one
  CHECK(link_state_parse("link 1 0 0 0 0 0 1 1.2.3.4 12a456 -", &t) == -1);
  CHECK(link_state_parse("link 3 0 0 0 0 0 1 - - a<b>", &t) == -1);        // not a screen name
  CHECK(link_state_parse("link 1 0 0 0 0 0 1 x.y.z.w 123456 -", &t) == -1);
  CHECK(link_state_parse("link 1 0 0 0 0 0 1 - - - more", &t) == -1);
  CHECK(link_state_parse("link 1 0 0 0 0 0 1 - -", &t) == -1);
  CHECK(link_state_parse("", &t) == -1);
}


int
main(void) {
  test_size();
  test_request();
  test_session();
  test_response();
  test_code();
  test_state();
  if(g_failed) {
    fprintf(stderr, "link_test: %d failed\n", g_failed);
    return 1;
  }
  printf("link_test: ok\n");
  return 0;
}
