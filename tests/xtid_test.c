/* Host tests for src/xtid.c, the x-client-transaction-id header. The
expected values are the reference vectors of the x-client-transaction-id
library (github.com/Lqm1, MIT), which come from Chromium running X's own
script; the page here is made up around the two animation rows they use. */

#include <stdio.h>
#include <string.h>

#include "xtid.h"

static int g_failed;

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_failed++; } } while(0)

#define LIVE_KEY     "DBvIZ0xwwkWXaB3H5ryhjd/137RFDskbsDZvRr6cG0uCWYCdxhOvMwV86MRGVP5m"
#define OVERFLOW_KEY "82gCFB4o3UqR2UZV7LutHF1un2u45+FysCFgrRgyrep9mS9XJH14V2PvG0loA+Dr"

// both keys pick the first animation; LIVE_KEY its row 1, OVERFLOW_KEY row 13
#define ROW  " 10,20 30,40 50,60 h 7 s 80,90 100,110 "
#define ROWS ROW "C 94,180 111,88 77,22 h 2 s 69,153 50,189 C" ROW "C" ROW "C" ROW "C" ROW "C" \
  ROW "C" ROW "C" ROW "C" ROW "C" ROW "C" ROW "C" ROW \
  "C 238,219 18,89 190,126 h 63 s 131,5 5,132 C" ROW "C" ROW

#define ANIM(n, rows) "<svg viewBox=\"0 0 24 24\" id=\"loading-x-anim-" #n "\" aria-hidden=\"true\" " \
  "class=\"r-1p0dtai\"><g><path d=\"M18.244 2.25h3.308z\"></path><path d=\"M 10,30 C" rows \
  "\"></path></g></svg>"

static const char page_head[] =
  "<!DOCTYPE html><html dir=\"ltr\" lang=\"en\"><head><meta charset=\"utf-8\" />"
  "<meta name=\"viewport\" content=\"width=device-width\" />"
  "<meta name=\"twitter-site-verification\" content=\"";
static const char page_tail[] =
  "\" /><title>X</title></head><body><div id=\"react-root\"></div>"
  ANIM(0, ROWS) ANIM(1, ROW) ANIM(2, ROW) ANIM(3, ROW)
  "<script>window.__SCRIPTS_LOADED__ = {};</script>"
  "<script>(()=>{var u=e=>(({59812:\"loader.Other\",59924:\"ondemand.s\",60041:\"i18n/emoji-gu\"})[e]||e)"
  "+\".\"+({1159924:\"0000000000000000\",59923:\"1111111111111111\",59924:\"6f4d46a32a10fdcc\"})[e]+\"a.js\"})()"
  "</script></body></html>";


static void
make_page(char *buf, size_t size, const char *key) {
  snprintf(buf, size, "%s%s%s", page_head, key, page_tail);
}


static void
test_ondemand(void) {
  static char page[8192];
  char url[256];

  make_page(page, sizeof page, LIVE_KEY);
  CHECK(xtid_ondemand_url(page, url, sizeof url) == 0);
  CHECK(!strcmp(url, "https://abs.twimg.com/responsive-web/client-web/ondemand.s.6f4d46a32a10fdcca.js"));
  CHECK(xtid_ondemand_url(page, url, 40) == -1);

  // without the runtime's address builder, the hash anywhere after a comma
  CHECK(xtid_ondemand_url("{1:\"a\",77:'ondemand.s'} {,177:\"bad\",77:\"abc_-9\"}", url, sizeof url) == 0);
  CHECK(!strcmp(url, "https://abs.twimg.com/responsive-web/client-web/ondemand.s.abc_-9a.js"));
  CHECK(xtid_ondemand_url("<html><body>no chunks</body></html>", url, sizeof url) == -1);
  CHECK(xtid_ondemand_url("x:\"ondemand.s\" 5:\"ondemand.s.bundle\"", url, sizeof url) == -1);
  CHECK(xtid_ondemand_url("ondemand.s", url, sizeof url) == -1);
}


static void
test_indices(void) {
  int idx[XTID_INDICES_MAX];

  CHECK(xtid_indices("(t[14], 16)(t[20], 16)(t[19],16)(t[30],\n16)", idx, XTID_INDICES_MAX) == 4);
  CHECK(idx[0] == 14 && idx[1] == 20 && idx[2] == 19 && idx[3] == 30);
  CHECK(xtid_indices("a=(n[13],16),b=(tt[2],16),c=(n[123],16),d=(n[7],10),e=(n[42], 16)", idx,
                     XTID_INDICES_MAX) == 2);
  CHECK(idx[0] == 13 && idx[1] == 42);
  CHECK(xtid_indices("(t[1],16)(t[2],16)(t[3],16)", idx, 2) == 2 && idx[1] == 2);
  CHECK(xtid_indices("no indices here", idx, XTID_INDICES_MAX) == -1);
  CHECK(xtid_indices("(", idx, XTID_INDICES_MAX) == -1);
}


static void
test_ids(void) {
  static char page[8192];
  static const int live[] = {14, 20, 19, 30};
  char id[XTID_HEADER_MAX];
  xtid_t t;

  make_page(page, sizeof page, LIVE_KEY);
  CHECK(xtid_init(&t, page, live, 4) == 0 && t.key_len == 48);
  CHECK(!strcmp(t.animation, "5eaf6b100ccccccccccccd00ccccccccccccd100"));
  CHECK(xtid_make(&t, "GET", "/i/api/graphql/abc/UserByScreenName", 1682924400LL + 123456789, 128,
                  id, sizeof id) == 0);
  CHECK(!strcmp(id, "gIybSOfM8ELFF+idR2Y8IQ1fdV80xY5JmzC278Y+HJvLAtkAHUaTL7OF/GhExtR+5pVN24elPZCct5ujO2lBVvrPTDecgw"));
  CHECK(xtid_make(&t, "POST", "/1.1/jot/client_event.json", 1682924400LL + 98765432, 128,
                  id, sizeof id) == 0);
  CHECK(!strcmp(id, "gIybSOfM8ELFF+idR2Y8IQ1fdV80xY5JmzC278Y+HJvLAtkAHUaTL7OF/GhExtR+5viKY4XfkO81y2F6yffUbiKONGRtgw"));
  CHECK(xtid_make(&t, "POST", "/x", 1682924400LL + 1, 0, id, 20) == -1);
  CHECK(xtid_make(&t, "POST", "/x", 1600000000LL, 0, id, sizeof id) == -1);

  // the eased colour overshoots 255 here, and the browser clamps it
  make_page(page, sizeof page, OVERFLOW_KEY);
  CHECK(xtid_init(&t, page, live, 4) == 0);
  CHECK(!strcmp(t.animation, "ffe600a8f5c28f5c28f80c28f5c28f5c290c28f5c28f5c290a8f5c28f5c28f800"));

  // no key, a key byte past the key, a row the animation does not have
  CHECK(xtid_init(&t, "<html><head><meta name=\"description\" content=\"x\"></head></html>", live, 4) == -1);
  CHECK(xtid_make(&t, "GET", "/x", 1794000000LL, 1, id, sizeof id) == -1);
  CHECK(xtid_init(&t, page, (const int[]){14, 48}, 2) == -2);
  make_page(page, sizeof page, "AAAAAAAAAAAAAAAA"); // row 0 of the first animation, a short one
  memcpy(strstr(page, "C 10,20"), "C 10,2C", 7);
  CHECK(xtid_init(&t, page, (const int[]){1, 2}, 2) == -2);
  CHECK(xtid_init(&t, page, live, 0) == -1);
}


int
main(void) {
  test_ondemand();
  test_indices();
  test_ids();
  if(g_failed) {
    printf("%d transaction id checks failed\n", g_failed);
    return 1;
  }
  printf("all transaction id checks passed\n");
  return 0;
}
