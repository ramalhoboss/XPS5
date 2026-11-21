/* Host tests of src/phone.c: what the title and the payload say to each
other for writing on the phone, and what the page sends. */

#include <stdio.h>
#include <string.h>

#include "phone.h"

static int g_failed;

#define CHECK(x) do { if(!(x)) { printf("%s:%d: %s\n", __FILE__, __LINE__, #x); g_failed++; } } while(0)


static void
test_state_line(void) {
  phone_state_t s, back;
  char line[160];
  size_t len;

  memset(&s, 0, sizeof s);
  s.state = PHONE_WAITING;
  s.seen = 1;
  s.seq = 7;
  snprintf(s.ip, sizeof s.ip, "192.168.1.70");
  phone_token(0x0123456789abcdefull, s.token);
  CHECK(!strcmp(s.token, "fedcba9876543210"));
  CHECK(phone_state_line(line, sizeof line, &s, 12) > 0);
  CHECK(!strcmp(line, "type 1 0 1 0 7 192.168.1.70 fedcba9876543210 12\n"));
  CHECK(!phone_state_parse(line, &back, &len) && len == 12 && back.state == PHONE_WAITING && back.seen &&
        !back.done && back.seq == 7 && !strcmp(back.ip, "192.168.1.70") && !strcmp(back.token, s.token));
  // before it waits: no address, no token
  memset(&s, 0, sizeof s);
  CHECK(phone_state_line(line, sizeof line, &s, 0) > 0 && !strcmp(line, "type 0 0 0 0 0 - - 0\n"));
  CHECK(!phone_state_parse(line, &back, &len) && !back.ip[0] && !back.token[0] && !len);
  // not one
  CHECK(phone_state_parse("on 22", &back, &len) == -1);
  CHECK(phone_state_parse("type 9 0 0 0 0 - - 0", &back, &len) == -1);
  CHECK(phone_state_parse("type 1 0 0 0 0 - - 99999", &back, &len) == -1);
  CHECK(phone_state_line(line, 10, &s, 0) == -1);
}


static void
test_origin(void) {
  CHECK(phone_origin_ok("", "192.168.1.70"));
  CHECK(phone_origin_ok(NULL, "192.168.1.70"));
  CHECK(phone_origin_ok("http://192.168.1.70:9071", "192.168.1.70"));
  CHECK(!phone_origin_ok("http://192.168.1.70:9070", "192.168.1.70"));
  CHECK(!phone_origin_ok("https://evil.example", "192.168.1.70"));
  CHECK(!phone_origin_ok("http://192.168.1.7:9071", "192.168.1.70"));
  CHECK(!phone_origin_ok("http://:9071", ""));
}


static void
test_body(void) {
  char token[PHONE_TOKEN_LEN + 1], text[16];
  const char *body;
  int has, done;

  body = "{\"token\":\"fedcba9876543210\",\"text\":\"Ol\\u00e1 \\\"PS5\\\"\",\"done\":true}";
  CHECK(!phone_parse_body(body, strlen(body), token, text, sizeof text, &has, &done));
  CHECK(!strcmp(token, "fedcba9876543210") && has && done && !strcmp(text, "Ol\xc3\xa1 \"PS5\""));
  // the draft's: a token only
  body = "{\"token\":\"fedcba9876543210\"}";
  CHECK(!phone_parse_body(body, strlen(body), token, text, sizeof text, &has, &done) && !has && !done);
  // cut on a whole character: "Olá" with "á" across the end stays "Ol"
  body = "{\"token\":\"fedcba9876543210\",\"text\":\"Ol\xc3\xa1\"}";
  CHECK(!phone_parse_body(body, strlen(body), token, text, 4, &has, &done) && !strcmp(text, "Ol"));
  // no token, or not one
  body = "{\"text\":\"x\"}";
  CHECK(phone_parse_body(body, strlen(body), token, text, sizeof text, &has, &done) == -1 && !token[0]);
  body = "{\"token\":\"FEDCBA9876543210\"}";
  CHECK(phone_parse_body(body, strlen(body), token, text, sizeof text, &has, &done) == -1);
  body = "{\"token\":\"fedcba98\"}";
  CHECK(phone_parse_body(body, strlen(body), token, text, sizeof text, &has, &done) == -1);
  CHECK(phone_parse_body("not json", 8, token, text, sizeof text, &has, &done) == -1);
}


static void
test_draft_and_hex(void) {
  char json[160], hex[64], back[32];

  CHECK(phone_draft_json(json, sizeof json, "Ol\xc3\xa1\n\"x\"\\", 280, "post", "pt", 3) > 0);
  CHECK(!strcmp(json, "{\"text\":\"Ol\xc3\xa1\\n\\\"x\\\"\\\\\",\"max\":280,\"what\":\"post\",\"lang\":\"pt\",\"seq\":3}"));
  CHECK(phone_draft_json(json, 20, "a long text that does not fit", 280, "post", "pt", 0) == -1);

  CHECK(phone_hex("", hex, sizeof hex) == 1 && !strcmp(hex, "-"));
  CHECK(phone_unhex("-", back, sizeof back) == 0 && !back[0]);
  CHECK(phone_hex("Ol\xc3\xa1!", hex, sizeof hex) == 10 && !strcmp(hex, "4f6cc3a121"));
  CHECK(phone_unhex(hex, back, sizeof back) == 5 && !strcmp(back, "Ol\xc3\xa1!"));
  CHECK(phone_unhex("4F6C", back, sizeof back) == 2 && !strcmp(back, "Ol"));
  // cut on a whole character, and no bytes that are not text
  CHECK(phone_unhex("4f6cc3a1", back, 4) == 2 && !strcmp(back, "Ol"));
  CHECK(phone_unhex("4f6", back, sizeof back) == -1);
  CHECK(phone_unhex("4fzz", back, sizeof back) == -1);
  CHECK(phone_unhex("4f00", back, sizeof back) == -1);
  CHECK(phone_hex("abc", hex, 6) == -1);
}


int
main(void) {
  test_state_line();
  test_origin();
  test_body();
  test_draft_and_hex();
  if(g_failed) {
    printf("%d phone checks failed\n", g_failed);
    return 1;
  }
  printf("all phone checks passed\n");
  return 0;
}
