/* Host tests for the parts of the X client that do not need the PS5: the
JSON reader, the cookie file, the timeline URL and the posts of a timeline
answer. Run with tests/host-test.sh. */

#define _DEFAULT_SOURCE // mkstemp

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "json.h"
#include "tls.h"
#include "xapi.h"

static int g_failed;

#define CHECK(cond) do { \
  if(!(cond)) { \
    printf("%s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_failed++; \
  } \
} while(0)


static char *
read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  char *buf;
  long n;

  if(!f || fseek(f, 0, SEEK_END) || (n=ftell(f)) < 0 || fseek(f, 0, SEEK_SET) ||
     !(buf=malloc(n + 1)) || fread(buf, 1, n, f) != (size_t)n) {
    fprintf(stderr, "cannot read %s\n", path);
    exit(2);
  }
  fclose(f);
  buf[n] = 0;
  *len = n;
  return buf;
}


static json_doc_t *
parse(const char *text, size_t *err_off) {
  static char buf[4096];
  size_t len = strlen(text);

  memcpy(buf, text, len + 1);
  return json_parse(buf, len, err_off);
}


/**
 * every byte sequence is a whole UTF-8 character
 **/
static int
valid_utf8(const char *s) {
  const unsigned char *p = (const unsigned char*)s;
  int more;

  while(*p) {
    more = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 :
      (*p & 0xF8) == 0xF0 ? 3 : -1;
    if(more < 0) {
      return 0;
    }
    for(p++; more > 0; more--, p++) {
      if((*p & 0xC0) != 0x80) {
        return 0;
      }
    }
  }
  return 1;
}


static void
test_json(void) {
  const json_node_t *root;
  json_doc_t *doc;
  size_t off;
  char deep[700];

  doc = parse(" {\"a\": [1, -2.5e3, true, false, null], \"s\": \"x\\u00e9\\ud83d\\ude00\\n\","
              " \"lone\": \"\\ud800!\", \"o\": {}} ", &off);
  CHECK(doc != NULL);
  if(doc) {
    root = json_root(doc);
    CHECK(json_get(root, "a")->child->type == JSON_NUMBER);
    CHECK(json_get(root, "a")->child->next->len == 6);
    CHECK(json_get(root, "a")->child->next->next->type == JSON_TRUE);
    CHECK(!strcmp(json_string(json_get(root, "s")), "x\xc3\xa9\xf0\x9f\x98\x80\n"));
    CHECK(!strcmp(json_string(json_get(root, "lone")), "\xef\xbf\xbd!"));
    CHECK(json_get(root, "o")->type == JSON_OBJECT && !json_get(root, "o")->child);
    CHECK(json_get(root, "missing") == NULL);
    CHECK(json_path(root, "o", "x", NULL) == NULL);
    json_free(doc);
  }

  CHECK(parse("{\"a\":1} x", &off) == NULL && off == 8);
  CHECK(parse("{\"a\":\"open", &off) == NULL);
  CHECK(parse("{\"a\" 1}", &off) == NULL);
  CHECK(parse("[1,]", &off) == NULL);
  CHECK(parse("\"bad \\q escape\"", &off) == NULL);
  CHECK(parse("\"raw\ncontrol\"", &off) == NULL);
  CHECK(parse("", &off) == NULL);

  memset(deep, '[', 300);
  memset(deep + 300, ']', 300);
  deep[600] = 0;
  CHECK(parse(deep, &off) == NULL);
  memset(deep, '[', 200);
  memset(deep + 200, ']', 200);
  deep[400] = 0;
  CHECK((doc=parse(deep, &off)) != NULL);
  json_free(doc);
}


static void
test_session(void) {
  static const char *forms[] = {
    "auth_token=abc123\nct0=def456\n",
    "\xef\xbb\xbf" "auth_token = abc123\r\nct0: def456\r\n",
    "Cookie: guest_id=v1%3A1; auth_token=abc123; ct0=def456; twid=u%3D1",
    "auth_token\tabc123\t.x.com\t/\t2027-11-03T00:00:00.000Z\n"
    "ct0\tdef456\t.x.com\t/\t2027-11-03T00:00:00.000Z\n",
    "x_auth_token=wrong\nlct0=wrong\nauth_token=abc123\nct0=def456",
  };
  char big[400];
  x_session_t s;

  for(size_t i=0; i < sizeof forms / sizeof forms[0]; i++) {
    CHECK(x_session_parse(&s, forms[i], strlen(forms[i])) == 0);
    CHECK(!strcmp(s.auth_token, "abc123"));
    CHECK(!strcmp(s.ct0, "def456"));
  }

  CHECK(x_session_parse(&s, "ct0=def456", 10) == -1);
  CHECK(x_session_parse(&s, "auth_token=abc123", 17) == -2);
  CHECK(x_session_parse(&s, "auth_token=\nct0=def456", 22) == -1);

  // a value longer than the buffer is refused, not cut
  strcpy(big, "ct0=def456\nauth_token=");
  memset(big + strlen(big), 'a', 200);
  big[22 + 200] = 0;
  CHECK(x_session_parse(&s, big, strlen(big)) == -1);
}


/**
 * undo the URL escapes in place
 **/
static void
unescape(char *s) {
  char *w = s;
  unsigned c;

  for(; *s; s++) {
    if(*s == '%' && sscanf(s + 1, "%2x", &c) == 1) {
      *w++ = (char)c;
      s += 2;
    } else {
      *w++ = *s;
    }
  }
  *w = 0;
}


static void
test_url(void) {
  static const char start[] = "https://x.com/i/api/graphql/GjleyyGw4xvWe5tn8EQt9w/"
    "HomeLatestTimeline?variables=%7B%22count%22%3A20%2C";
  static char url[8192];
  const json_node_t *n;
  json_doc_t *doc;
  char *features;
  size_t off;
  int count = 0;

  CHECK(x_timeline_url(url, sizeof url, 20, 0, NULL) == 0);
  CHECK(strstr(url, "/fedFLLzmiCAuzPgA8lyh8Q/HomeTimeline?variables=") != NULL);
  CHECK(x_timeline_url(url, sizeof url, 20, 1, "") == 0);
  CHECK(!strncmp(url, start, strlen(start)));
  CHECK(strchr(url, '"') == NULL && strchr(url, '{') == NULL && strchr(url, ' ') == NULL);
  CHECK(x_timeline_url(url, 100, 20, 1, NULL) == -1);

  // the next page carries the cursor instead of the launch context
  CHECK(x_timeline_url(url, sizeof url, 20, 1, "DAAB+Cg/AB=") == 0);
  CHECK(strstr(url, "%22cursor%22%3A%22DAAB%2BCg%2FAB%3D%22") != NULL);
  CHECK(strstr(url, "requestContext") == NULL);
  CHECK(x_timeline_url(url, sizeof url, 20, 1, "bad\"cursor") == -1);
  CHECK(x_timeline_url(url, sizeof url, 20, 1, "bad\\cursor") == -1);

  // the features must be valid JSON with every switch set
  CHECK(x_timeline_url(url, sizeof url, 20, 1, "") == 0);
  CHECK((features=strstr(url, "&features=")) != NULL);
  if(!features) {
    return;
  }
  features += strlen("&features=");
  unescape(features);
  CHECK((doc=json_parse(features, strlen(features), &off)) != NULL);
  if(doc) {
    for(n=json_root(doc)->child; n; n=n->next) {
      CHECK(n->type == JSON_TRUE || n->type == JSON_FALSE);
      count++;
    }
    CHECK(count == 40);
    json_free(doc);
  }
}


/**
 * pictures, badges, counts, links and quotes, from tests/timeline_rich.json
 **/
static void
test_rich(const char *path) {
  x_post_t posts[4];
  json_doc_t *doc;
  char *text;
  size_t len, off;
  int total;

  text = read_file(path, &len);
  CHECK((doc=json_parse(text, len, &off)) != NULL);
  if(!doc) {
    printf("timeline_rich.json stops at byte %zu\n", off);
    free(text);
    return;
  }
  // the ad between the third and the fourth post is left out
  CHECK(x_timeline_posts(json_root(doc), posts, 4, &total) == 4 && total == 4);

  // links shown as X shows them; the photo's and the quote's links gone
  CHECK(!strcmp(posts[0].text, "Vejam isto exemplo.pt/artigo e isto exemplo.pt/artigo"));
  CHECK(!strcmp(posts[0].avatar, "https://pbs.twimg.com/profile_images/1/rita_normal.jpg"));
  CHECK(posts[0].badge == X_BADGE_BLUE);
  CHECK(posts[0].replies == 12 && posts[0].reposts == 3 && posts[0].likes == 250);
  CHECK(posts[0].views == 15342 && posts[0].bookmarks == 9);
  CHECK(posts[0].media_count == 1 && !posts[0].media[0].video);
  CHECK(!strcmp(posts[0].media[0].url, "https://pbs.twimg.com/media/AAA.jpg"));
  CHECK(posts[0].media[0].width == 1200 && posts[0].media[0].height == 675);
  CHECK(!strcmp(posts[0].quote_user, "tiago") && !strcmp(posts[0].quote_name, "Tiago"));
  CHECK(!strcmp(posts[0].quote_text, "o post citado"));
  CHECK(!strcmp(posts[0].quote_avatar, "https://pbs.twimg.com/profile_images/3/tiago_normal.jpg"));
  CHECK(posts[0].quote_badge == X_BADGE_BLUE);
  CHECK(posts[0].liked && !posts[0].reposted);

  // only pictures on pbs.twimg.com, and sizes that make sense
  CHECK(!strcmp(posts[1].avatar, "https://pbs.twimg.com/profile_images/2/loja_normal.png"));
  CHECK(posts[1].badge == X_BADGE_GOLD);
  CHECK(posts[1].media_count == 2 && posts[1].media[0].video && posts[1].media[1].video);
  // the sharpest, X's 1080p, in the timeline and over the whole screen,
  // and the smallest
  CHECK(!strcmp(posts[1].media[0].video_url, "https://video.twimg.com/ext_tw_video/1/pu/vid/1920x1080/V.mp4"));
  CHECK(!strcmp(posts[1].media[0].video_small, "https://video.twimg.com/ext_tw_video/1/pu/vid/640x360/V.mp4"));
  CHECK(posts[1].media[0].width == 1920 && posts[1].media[1].width == 0 &&
        posts[1].media[1].height == 0);
  CHECK(!strcmp(posts[1].text, "dois ficheiros") && posts[1].views == -1 && posts[1].bookmarks == -1);
  CHECK(posts[1].replies == 0 && !posts[1].quote_user[0] && !posts[1].liked);

  // a note's links come from its own entities
  CHECK(!strcmp(posts[2].text, "nota longa com certo.pt/x no meio"));
  CHECK(posts[2].badge == X_BADGE_GREY && !posts[2].avatar[0]);
  CHECK(posts[2].replies == -1 && posts[2].likes == -1);

  // a repost keeps who reposted it, by name too
  CHECK(!strcmp(posts[3].user, "ines") && !strcmp(posts[3].reposter, "hugo"));
  CHECK(!strcmp(posts[3].reposter_name, "Hugo Reis") && posts[3].likes == 7);
  CHECK(posts[3].badge == X_BADGE_NONE);

  // each post's number, the one shown for a repost; none here for more pages
  CHECK(!strcmp(posts[0].id, "2001") && !strcmp(posts[2].id, "2003") && !posts[3].id[0]);
  {
    char cursor[64];
    CHECK(x_timeline_cursor(json_root(doc), cursor, sizeof cursor) == -1 && !cursor[0]);
  }

  json_free(doc);
  free(text);
}


static void
test_timeline(const char *path) {
  x_post_t posts[10];
  json_doc_t *doc;
  char *text, line[64];
  size_t len, off;
  int total;

  text = read_file(path, &len);
  CHECK((doc=json_parse(text, len, &off)) != NULL);
  if(!doc) {
    printf("timeline.json stops at byte %zu\n", off);
    free(text);
    return;
  }

  CHECK(x_timeline_posts(json_root(doc), posts, 10, &total) == 6);
  CHECK(total == 6);
  CHECK(!strcmp(posts[0].user, "ana") && !posts[0].reposter[0]);
  CHECK(!strcmp(posts[0].name, "Ana") && posts[0].time == 1793739600);
  CHECK(!strcmp(posts[0].text, "Ol\xc3\xa1 mundo & amigos \xf0\x9f\x98\x80"));
  CHECK(!strcmp(posts[1].user, "bruno") && !strcmp(posts[1].name, "Bruno"));
  CHECK(!posts[2].name[0] && posts[2].time == 0);
  CHECK(!strcmp(posts[1].text, "line1\nline2 <3 \"quoted\" \\ back/slash"));
  CHECK(!strcmp(posts[2].user, "carla"));
  CHECK(!strcmp(posts[2].text, "the whole long note"));
  CHECK(!strcmp(posts[3].user, "eva") && !strcmp(posts[3].reposter, "dani"));
  CHECK(!strcmp(posts[3].text, "original from eva"));
  CHECK(!strcmp(posts[4].user, "fabio") && !strcmp(posts[5].user, "gil"));

  CHECK(x_timeline_posts(json_root(doc), posts, 3, &total) == 3);
  CHECK(total == 6);

  // where the next page starts: the Bottom cursor, not the Top one
  CHECK(x_timeline_cursor(json_root(doc), line, sizeof line) == 0 && !strcmp(line, "DAABCgABG2"));
  CHECK(x_timeline_cursor(json_root(doc), line, 5) == -1 && !line[0]);

  x_post_line(&posts[1], line, sizeof line);
  CHECK(!strcmp(line, "@bruno: line1 line2 <3 \"quoted\" \\ back/slash"));
  x_post_line(&posts[3], line, sizeof line);
  CHECK(!strcmp(line, "@dani reposted @eva: original from eva"));

  // cut inside the emoji: the line keeps whole characters and ends in ...
  for(size_t size=16; size <= 40; size++) {
    x_post_line(&posts[0], line, size);
    CHECK(strlen(line) < size);
    CHECK(valid_utf8(line));
    if(size < 31) { // the whole line is 30 bytes
      CHECK(strlen(line) >= 3 && !strcmp(line + strlen(line) - 3, "..."));
    }
  }
  x_post_line(&posts[0], line, sizeof line);
  CHECK(!strcmp(line, "@ana: Ol\xc3\xa1 mundo & amigos \xf0\x9f\x98\x80"));

  json_free(doc);
  free(text);
}


static void
test_time(void) {
  CHECK(x_parse_time("Thu Jan 01 00:00:00 +0000 1970") == 0);
  CHECK(x_parse_time("Sun Feb 29 12:34:56 +0000 2004") == 1078058096);
  CHECK(x_parse_time("Wed Dec 31 23:59:59 +0000 1969") == -1);
  CHECK(x_parse_time("Tue Xyz 03 21:00:00 +0000 2026") == 0);
  CHECK(x_parse_time("Tue Ova 03 21:00:00 +0000 2026") == 0); // inside "NovDec"
  CHECK(x_parse_time("yesterday") == 0);
  CHECK(x_parse_time(NULL) == 0);
}


static void
test_session_file(void) {
  char path[] = "/tmp/xapi_test_session_XXXXXX";
  x_session_t s;
  int fd = mkstemp(path);

  CHECK(fd >= 0);
  if(fd < 0) {
    return;
  }
  CHECK(write(fd, "auth_token=abc\nct0=def\n", 23) == 23);
  close(fd);
  CHECK(x_session_read(&s, path) == 0 && !strcmp(s.ct0, "def"));
  unlink(path);
  CHECK(x_session_read(&s, path) == -3);
}


static void
test_request(void) {
  x_session_t s;
  x_request_t r;

  CHECK(x_session_parse(&s, "auth_token=abc ct0=def", 22) == 0);
  x_request_init(&r, &s);
  CHECK(!strcmp(r.cookie, "auth_token=abc; ct0=def"));
  CHECK(!strcmp(r.headers[1].name, "x-csrf-token") && r.headers[1].value == s.ct0);
  CHECK(r.headers[2].value == r.cookie);
  for(int i=0; i<X_HEADER_COUNT; i++) {
    CHECK(r.headers[i].name && r.headers[i].value);
  }
}


static void
test_errors(void) {
  const char *message;
  json_doc_t *doc;
  size_t off;

  CHECK((doc=parse("{\"errors\":[{\"message\":\"Could not authenticate you\",\"code\":32}]}",
                   &off)) != NULL);
  if(doc) {
    CHECK(x_first_error(json_root(doc), &message) == 32);
    CHECK(message && !strcmp(message, "Could not authenticate you"));
    json_free(doc);
  }
  CHECK((doc=parse("{\"data\":{}}", &off)) != NULL);
  if(doc) {
    int total;
    x_post_t post;
    CHECK(x_first_error(json_root(doc), &message) == -1 && !message);
    CHECK(x_timeline_posts(json_root(doc), &post, 1, &total) == 0 && total == 0);
    json_free(doc);
  }
}


static void
test_tls(void) {
  static unsigned char der[16 << 10];
  tls_cert_t certs[TLS_CERTS];
  const unsigned char *d;

  tls_certs_pem(certs);
  CHECK(tls_certs_der(certs, TLS_CERTS, der, sizeof der) == 0);
  for(int i=0; i<TLS_CERTS; i++) {
    // a DER certificate is one SEQUENCE with a two-byte length
    d = certs[i].data;
    CHECK(certs[i].size > 4 && d[0] == 0x30 && d[1] == 0x82);
    CHECK(certs[i].size == 4 + (size_t)(d[2] << 8 | d[3]));
  }
  CHECK(certs[0].size == 1391); // ISRG Root X1

  tls_certs_pem(certs);
  CHECK(tls_certs_der(certs, TLS_ROOTS, der, 1500) == -1);
}


/**
 * damaged copies of the answer must fail cleanly or parse; the sanitizers
 * catch anything else
 **/
static void
test_damaged(const char *path) {
  x_post_t posts[10];
  json_doc_t *doc;
  char *orig, *copy;
  size_t len, off;
  int total;

  orig = read_file(path, &len);
  copy = malloc(len + 1);
  srand(5);
  for(int round=0; round < 3000; round++) {
    memcpy(copy, orig, len + 1);
    for(int i=0, n=1 + rand() % 4; i < n; i++) {
      copy[rand() % len] = "{}[]\",:\\u0aZ\x80\xff"[rand() % 15];
    }
    if((doc=json_parse(copy, round % 7 ? len : rand() % len, &off))) {
      x_timeline_posts(json_root(doc), posts, 10, &total);
      for(int i=0; i < total && i < 10; i++) {
        char line[300];
        x_post_line(&posts[i], line, sizeof line);
      }
      json_free(doc);
    } else {
      CHECK(off <= len);
    }
  }
  free(copy);
  free(orig);
}


/**
 * the POSTs of a like and a reply, and what X answers to them
 **/
static void
test_actions(void) {
  static char url[256], body[X_BODY_MAX], copy[X_BODY_MAX], text[X_REPLY_MAX + 2];
  const char *message;
  json_doc_t *doc;
  size_t off;

  CHECK(x_like_request(url, sizeof url, body, sizeof body, "2001", 1) == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/graphql/lI07N6Otwv1PhnEgXILM7A/FavoriteTweet"));
  CHECK(!strcmp(body, "{\"variables\":{\"tweet_id\":\"2001\"},\"queryId\":\"lI07N6Otwv1PhnEgXILM7A\"}"));
  CHECK(x_like_request(url, sizeof url, body, sizeof body, "2001", 0) == 0);
  CHECK(strstr(url, "/UnfavoriteTweet") && strstr(body, "\"tweet_id\":\"2001\""));
  CHECK(x_like_request(url, sizeof url, body, sizeof body, "20a1", 1) == -1);
  CHECK(x_like_request(url, sizeof url, body, sizeof body, "", 1) == -1);
  CHECK(x_like_request(url, sizeof url, body, sizeof body, "123456789012345678901234", 1) == -1);
  CHECK(x_like_request(url, sizeof url, body, 20, "2001", 1) == -1);

  // the text comes back whole from the JSON, whatever it holds
  const char *said = "Boa! \"Vamos\" jogar\\ logo\n\tà noite \xf0\x9f\x8e\xae";
  CHECK(x_reply_request(url, sizeof url, body, sizeof body, "2003", said) == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/graphql/l4hk8uVGvR-a8kzQ4nOhPA/CreateTweet"));
  strcpy(copy, body);
  CHECK((doc=json_parse(copy, strlen(copy), &off)) != NULL);
  if(doc) {
    const json_node_t *vars = json_get(json_root(doc), "variables");
    CHECK(json_string(json_get(vars, "tweet_text")) &&
          !strcmp(json_string(json_get(vars, "tweet_text")), said));
    CHECK(json_string(json_path(vars, "reply", "in_reply_to_tweet_id", NULL)) &&
          !strcmp(json_string(json_path(vars, "reply", "in_reply_to_tweet_id", NULL)), "2003"));
    CHECK(json_get(json_root(doc), "features") &&
          json_get(json_root(doc), "features")->type == JSON_OBJECT);
    CHECK(json_string(json_get(json_root(doc), "queryId")) &&
          !strcmp(json_string(json_get(json_root(doc), "queryId")), "l4hk8uVGvR-a8kzQ4nOhPA"));
    json_free(doc);
  }
  CHECK(x_reply_request(url, sizeof url, body, sizeof body, "2003", "") == -1);
  CHECK(x_reply_request(url, sizeof url, body, sizeof body, "x", "ok") == -1);
  CHECK(x_reply_request(url, sizeof url, body, 100, "2003", "ok") == -1);

  // the longest reply, all of it escaped, still fits
  memset(text, '"', X_REPLY_MAX);
  text[X_REPLY_MAX] = 0;
  CHECK(x_reply_request(url, sizeof url, body, sizeof body, "2003", text) == 0);
  text[X_REPLY_MAX] = '"';
  text[X_REPLY_MAX + 1] = 0;
  CHECK(x_reply_request(url, sizeof url, body, sizeof body, "2003", text) == -1);

  CHECK((doc=parse("{\"data\":{\"favorite_tweet\":\"Done\"}}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);
  CHECK((doc=parse("{\"data\":{\"unfavorite_tweet\":\"Done\"}}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);
  CHECK((doc=parse("{\"data\":{\"create_tweet\":{\"tweet_results\":{\"result\":"
                   "{\"rest_id\":\"2100\"}}}}}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);
  CHECK((doc=parse("{\"errors\":[{\"message\":\"You have already favorited this post.\","
                   "\"code\":139}],\"data\":{}}", &off)) &&
        x_action_done(json_root(doc), &message) == 139 && message);
  json_free(doc);
  CHECK((doc=parse("{\"data\":{\"create_tweet\":{\"tweet_results\":{}}}}", &off)) &&
        x_action_done(json_root(doc), &message) == -1 && !message);
  json_free(doc);

  // a post of the user's own goes out without a reply
  CHECK(x_reply_request(url, sizeof url, body, sizeof body, NULL, "Ol\xc3\xa1") == 0);
  CHECK(!strstr(body, "in_reply_to_tweet_id") && strstr(body, "\"tweet_text\":\"Ol\xc3\xa1\""));
  strcpy(copy, body);
  CHECK((doc=json_parse(copy, strlen(copy), &off)) != NULL);
  json_free(doc);
  CHECK(x_reply_request(url, sizeof url, body, sizeof body, "", "ok") == 0 &&
        !strstr(body, "in_reply_to_tweet_id"));

  // reposts
  CHECK(x_repost_request(url, sizeof url, body, sizeof body, "2001", 1) == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/graphql/mbRO74GrOvSfRcJnlMapnQ/CreateRetweet"));
  CHECK(!strcmp(body, "{\"variables\":{\"tweet_id\":\"2001\",\"dark_request\":false},"
                "\"queryId\":\"mbRO74GrOvSfRcJnlMapnQ\"}"));
  CHECK(x_repost_request(url, sizeof url, body, sizeof body, "2001", 0) == 0);
  CHECK(strstr(url, "/ZyZigVsNiFO6v1dEks1eWg/DeleteRetweet") &&
        strstr(body, "\"source_tweet_id\":\"2001\""));
  CHECK(x_repost_request(url, sizeof url, body, sizeof body, "x1", 1) == -1);
  CHECK((doc=parse("{\"data\":{\"create_retweet\":{\"retweet_results\":{\"result\":"
                   "{\"rest_id\":\"9\",\"legacy\":{}}}}}}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);
  CHECK((doc=parse("{\"data\":{\"unretweet\":{\"source_tweet_results\":{\"result\":"
                   "{\"rest_id\":\"2001\"}}}}}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);

  // deleting one's own post
  CHECK(x_delete_request(url, sizeof url, body, sizeof body, "2001") == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/graphql/nxpZCY2K-I6QoFHAHeojFQ/DeleteTweet"));
  CHECK(!strcmp(body, "{\"variables\":{\"tweet_id\":\"2001\",\"dark_request\":false},"
                "\"queryId\":\"nxpZCY2K-I6QoFHAHeojFQ\"}"));
  CHECK(x_delete_request(url, sizeof url, body, sizeof body, "20\"01") == -1);
  CHECK(x_delete_request(url, sizeof url, body, sizeof body, "") == -1);
  CHECK((doc=parse("{\"data\":{\"delete_tweet\":{\"tweet_results\":{}}}}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);
  CHECK((doc=parse("{\"errors\":[{\"message\":\"No status found\",\"code\":144}],"
                   "\"data\":{\"delete_tweet\":{\"tweet_results\":{}}}}", &off)) &&
        x_action_done(json_root(doc), &message) == 144 && message);
  json_free(doc);
}


/**
 * the URLs of the other lists and of profiles
 **/
static void
test_feeds(void) {
  static char url[8192];

  CHECK(x_feed_url(url, sizeof url, X_FEED_NOTIFICATIONS, NULL, 20, NULL) == 0);
  CHECK(strstr(url, "https://x.com/i/api/graphql/XANkUc-HLIcCl8q85rGC2A/NotificationsTimeline"
               "?variables=%7B%22timeline_type%22%3A%22All%22%2C%22count%22%3A20%7D&features=%7B"));
  CHECK(x_feed_url(url, sizeof url, X_FEED_BOOKMARKS, NULL, 20, "DAAB") == 0);
  CHECK(strstr(url, "/fjf5b5Lu82lYMSBRhppK2g/Bookmarks?") && strstr(url, "%22cursor%22%3A%22DAAB%22"));
  CHECK(!strstr(url, "fieldToggles"));
  CHECK(x_feed_url(url, sizeof url, X_FEED_USER, "44196397", 20, NULL) == 0);
  CHECK(strstr(url, "/D6z-FJI2pKuwEeYYxVIGbg/UserTweets?") &&
        strstr(url, "%22userId%22%3A%2244196397%22") && strstr(url, "&fieldToggles=%7B"));
  CHECK(x_feed_url(url, sizeof url, X_FEED_POST, "2001", 20, NULL) == 0);
  CHECK(strstr(url, "/dQDdKtt882nl-Km98FJGSQ/TweetDetail?") &&
        strstr(url, "%22focalTweetId%22%3A%222001%22") && strstr(url, "&fieldToggles=%7B"));
  CHECK(strchr(url, '"') == NULL && strchr(url, '{') == NULL && strchr(url, ' ') == NULL);
  CHECK(x_feed_url(url, sizeof url, X_FEED_POST, NULL, 20, NULL) == -1);
  CHECK(x_feed_url(url, sizeof url, X_FEED_USER, "12\"3", 20, NULL) == -1);
  CHECK(x_feed_url(url, sizeof url, X_FEED_USER, "1", 20, "bad\"cursor") == -1);
  CHECK(x_feed_url(url, sizeof url, 99, NULL, 20, NULL) == -1);
  CHECK(x_feed_url(url, 200, X_FEED_FOLLOWING, NULL, 20, NULL) == -1);

  CHECK(x_user_url(url, sizeof url, NULL) == 0);
  CHECK(strstr(url, "https://x.com/i/api/graphql/TpQxQtWMTMnIAMPlMGMqNw/Viewer?variables=") &&
        strstr(url, "&fieldToggles="));
  CHECK(x_user_url(url, sizeof url, "") == 0 && strstr(url, "/Viewer?"));
  CHECK(x_user_url(url, sizeof url, "44196397") == 0);
  CHECK(strstr(url, "/mxaYWwt52eAb6OAzYHMVzg/UserByRestId?variables=%7B%22userId%22%3A%2244196397%22"));
  CHECK(x_user_url(url, sizeof url, "abc") == -1);
  CHECK(x_user_url(url, 60, "44196397") == -1);
}


/**
 * notifications among the posts of an answer, and a profile's pinned post
 **/
static void
test_notices(void) {
  static const char notices[] =
    "{\"data\":{\"viewer_v2\":{\"user_results\":{\"result\":{\"notification_timeline\":{\"timeline\":"
    "{\"instructions\":[{\"type\":\"TimelineAddEntries\",\"entries\":["
    "{\"entryId\":\"notification-1\",\"content\":{\"itemContent\":{"
    "\"itemType\":\"TimelineNotification\",\"notification_icon\":\"heart_icon\","
    "\"rich_message\":{\"text\":\"Ana Lopes and 2 others liked your post\",\"entities\":[]},"
    "\"template\":{\"target_objects\":[{\"tweet_results\":{\"result\":{\"rest_id\":\"5001\","
    "\"core\":{\"user_results\":{\"result\":{\"rest_id\":\"77\",\"core\":{\"screen_name\":\"me\","
    "\"name\":\"Me\"}}}},\"legacy\":{\"full_text\":\"My new setup &amp; more\",\"favorited\":true}}}}],"
    "\"from_users\":[{\"user_results\":{\"result\":{\"rest_id\":\"501\",\"core\":{\"screen_name\":"
    "\"analopes\",\"name\":\"Ana Lopes\"},\"avatar\":{\"image_url\":"
    "\"https://pbs.twimg.com/profile_images/1/a_normal.jpg\"}}}},{\"user_results\":{\"result\":"
    "{\"rest_id\":\"502\",\"core\":{\"screen_name\":\"hugo\",\"name\":\"Hugo\"}}}}]},"
    "\"timestamp_ms\":\"1793793000000\"}}},"
    "{\"entryId\":\"notification-2\",\"content\":{\"itemContent\":{"
    "\"__typename\":\"TimelineNotification\",\"notification_icon\":\"person_icon\","
    "\"rich_message\":{\"text\":\"Rita followed you\"},\"template\":{\"from_users\":[{\"user_results\":"
    "{\"result\":{\"rest_id\":\"503\",\"core\":{\"screen_name\":\"rita\",\"name\":\"Rita\"}}}}],"
    "\"target_objects\":[]},\"timestamp_ms\":\"2025-05-05T01:18:21.657Z\"}}},"
    "{\"entryId\":\"tweet-6001\",\"content\":{\"itemContent\":{\"itemType\":\"TimelineTweet\","
    "\"tweet_results\":{\"result\":{\"rest_id\":\"6001\",\"core\":{\"user_results\":{\"result\":"
    "{\"rest_id\":\"504\",\"core\":{\"screen_name\":\"tiago\",\"name\":\"Tiago\"}}}},"
    "\"legacy\":{\"full_text\":\"@me agree!\"}}}}}},"
    "{\"entryId\":\"cursor-bottom-1\",\"content\":{\"value\":\"NOTIF+1\",\"cursorType\":\"Bottom\"}}"
    "]}]}}}}}}}";
  static const char pinned[] =
    "{\"data\":{\"user\":{\"result\":{\"timeline\":{\"timeline\":{\"instructions\":["
    "{\"type\":\"TimelineAddEntries\",\"entries\":[{\"content\":{\"itemContent\":{\"tweet_results\":"
    "{\"result\":{\"rest_id\":\"7001\",\"core\":{\"user_results\":{\"result\":{\"core\":"
    "{\"screen_name\":\"rita\"}}}},\"legacy\":{\"full_text\":\"one\"}}}}}},"
    "{\"content\":{\"itemContent\":{\"tweet_results\":{\"result\":{\"rest_id\":\"7002\",\"core\":"
    "{\"user_results\":{\"result\":{\"core\":{\"screen_name\":\"rita\"}}}},\"legacy\":"
    "{\"full_text\":\"two\"}}}}}}]},"
    "{\"type\":\"TimelinePinEntry\",\"entry\":{\"content\":{\"itemContent\":{\"tweet_results\":"
    "{\"result\":{\"rest_id\":\"7000\",\"core\":{\"user_results\":{\"result\":{\"core\":"
    "{\"screen_name\":\"rita\"}}}},\"legacy\":{\"full_text\":\"pinned\"}}}}}}}]}}}}}}";
  x_post_t posts[4];
  char cursor[64];
  json_doc_t *doc;
  size_t off;
  int total;

  CHECK((doc=parse(notices, &off)) != NULL);
  if(doc) {
    CHECK(x_timeline_posts(json_root(doc), posts, 4, &total) == 3 && total == 3);
    CHECK(posts[0].kind == X_KIND_NOTICE && posts[0].icon == X_ICON_LIKE);
    CHECK(!strcmp(posts[0].text, "Ana Lopes and 2 others liked your post"));
    CHECK(!strcmp(posts[0].user, "analopes") && !strcmp(posts[0].user_id, "501"));
    CHECK(posts[0].face_count == 2 &&
          !strcmp(posts[0].faces[0], "https://pbs.twimg.com/profile_images/1/a_normal.jpg") &&
          !posts[0].faces[1][0] && !strcmp(posts[0].avatar, posts[0].faces[0]));
    CHECK(!strcmp(posts[0].id, "5001") && !strcmp(posts[0].quote_user, "me") &&
          !strcmp(posts[0].quote_text, "My new setup & more") && posts[0].liked);
    CHECK(posts[0].time == 1793793000LL && posts[0].likes == -1);
    CHECK(posts[1].kind == X_KIND_NOTICE && posts[1].icon == X_ICON_FOLLOW && !posts[1].id[0] &&
          !strcmp(posts[1].user_id, "503") && posts[1].time == 1746407901LL);
    CHECK(posts[2].kind == X_KIND_POST && !strcmp(posts[2].id, "6001") &&
          !strcmp(posts[2].user_id, "504") && !strcmp(posts[2].text, "@me agree!"));
    CHECK(x_timeline_cursor(json_root(doc), cursor, sizeof cursor) == 0 && !strcmp(cursor, "NOTIF+1"));
    json_free(doc);
  }

  CHECK((doc=parse(pinned, &off)) != NULL);
  if(doc) {
    CHECK(x_timeline_posts(json_root(doc), posts, 4, &total) == 3);
    CHECK(!strcmp(posts[0].id, "7000") && posts[0].pinned);
    CHECK(!strcmp(posts[1].id, "7001") && !posts[1].pinned && !strcmp(posts[2].id, "7002"));
    CHECK(x_timeline_posts(json_root(doc), NULL, 0, &total) == 0 && total == 3);
    json_free(doc);
  }
}


/**
 * a profile, from UserByRestId and from Viewer
 **/
static void
test_profile(void) {
  static const char user[] =
    "{\"__typename\":\"User\",\"rest_id\":\"44196397\",\"is_blue_verified\":true,"
    "\"core\":{\"screen_name\":\"rita\",\"name\":\"Rita Sousa\","
    "\"created_at\":\"Tue Jun 02 20:12:29 +0000 2009\"},"
    "\"avatar\":{\"image_url\":\"https://pbs.twimg.com/profile_images/1/r_normal.jpg\"},"
    "\"location\":{\"location\":\"Porto\"},\"legacy\":{\"description\":"
    "\"Games &amp; photos https://t.co/abc\",\"entities\":{\"description\":{\"urls\":"
    "[{\"url\":\"https://t.co/abc\",\"display_url\":\"rita.pt\"}]}},\"followers_count\":4580,"
    "\"friends_count\":312,\"statuses_count\":2210,"
    "\"profile_banner_url\":\"https://pbs.twimg.com/profile_banners/1/2\"}}";
  char text[2048];
  json_doc_t *doc;
  x_user_t u;
  size_t off;

  snprintf(text, sizeof text, "{\"data\":{\"user\":{\"result\":%s}}}", user);
  CHECK((doc=parse(text, &off)) != NULL);
  if(doc) {
    CHECK(x_user_parse(json_root(doc), &u) == 0);
    CHECK(!strcmp(u.id, "44196397") && !strcmp(u.user, "rita") && !strcmp(u.name, "Rita Sousa"));
    CHECK(!strcmp(u.bio, "Games & photos rita.pt") && !strcmp(u.location, "Porto"));
    CHECK(u.followers == 4580 && u.following == 312 && u.posts == 2210 && u.badge == X_BADGE_BLUE);
    CHECK(!strcmp(u.banner, "https://pbs.twimg.com/profile_banners/1/2"));
    CHECK(!strcmp(u.avatar, "https://pbs.twimg.com/profile_images/1/r_normal.jpg"));
    CHECK(u.joined == x_parse_time("Tue Jun 02 20:12:29 +0000 2009") && u.joined > 0);
    json_free(doc);
  }
  snprintf(text, sizeof text, "{\"data\":{\"viewer\":{\"user_results\":{\"result\":%s}}}}", user);
  CHECK((doc=parse(text, &off)) && x_user_parse(json_root(doc), &u) == 0 && !strcmp(u.id, "44196397"));
  json_free(doc);

  // a post's poll, making one and voting
  {
    static const char polled[] =
      "{\"data\":{\"home\":{\"home_timeline_urt\":{\"instructions\":[{\"type\":\"TimelineAddEntries\",\"entries\":["
      "{\"entryId\":\"tweet-2001\",\"content\":{\"itemContent\":{\"tweet_results\":{\"result\":{\"__typename\":\"Tweet\","
      "\"rest_id\":\"2001\",\"core\":{\"user_results\":{\"result\":{\"rest_id\":\"7\",\"core\":{\"name\":\"Rui\","
      "\"screen_name\":\"rui\"}}}},\"legacy\":{\"full_text\":\"Qual?\"},\"card\":{\"rest_id\":\"card://1900000000000000009\","
      "\"legacy\":{\"name\":\"poll3choice_text_only\",\"url\":\"card://1900000000000000009\",\"binding_values\":["
      "{\"key\":\"choice1_label\",\"value\":{\"string_value\":\"GT7\",\"type\":\"STRING\"}},"
      "{\"key\":\"choice1_count\",\"value\":{\"string_value\":\"12\",\"type\":\"STRING\"}},"
      "{\"key\":\"choice2_label\",\"value\":{\"string_value\":\"Astro &amp; Bot\",\"type\":\"STRING\"}},"
      "{\"key\":\"choice2_count\",\"value\":{\"string_value\":\"30\",\"type\":\"STRING\"}},"
      "{\"key\":\"choice3_label\",\"value\":{\"string_value\":\"Outro\",\"type\":\"STRING\"}},"
      "{\"key\":\"end_datetime_utc\",\"value\":{\"string_value\":\"2026-11-14T12:00:00Z\",\"type\":\"STRING\"}},"
      "{\"key\":\"counts_are_final\",\"value\":{\"boolean_value\":false,\"type\":\"BOOLEAN\"}},"
      "{\"key\":\"selected_choice\",\"value\":{\"string_value\":\"2\",\"type\":\"STRING\"}},"
      "{\"key\":\"api\",\"value\":{\"string_value\":\"capi://passthrough/1\",\"type\":\"STRING\"}}]}}}}}}}]}]}}}}";
    const char *choices[] = {"Sim", "N\xc3\xa3o & talvez"};
    char url[256], body[1024], uri[64];
    x_post_t *p = calloc(1, sizeof *p);
    int total = 0;
    CHECK(p && (doc=parse(polled, &off)) != NULL);
    CHECK(p && doc && x_timeline_posts(json_root(doc), p, 1, &total) == 1 && p->poll_count == 3 &&
          !strcmp(p->poll_label[0], "GT7") && !strcmp(p->poll_label[1], "Astro & Bot") && p->poll_votes[1] == 30 &&
          p->poll_votes[2] == 0 && p->poll_voted == 2 && !p->poll_final && p->poll_end == 1794657600LL &&
          !strcmp(p->poll_card, "card://1900000000000000009") && !strcmp(p->poll_name, "poll3choice_text_only"));
    json_free(doc);
    free(p);
    CHECK(x_poll_card_request(url, sizeof url, body, sizeof body, choices, 2, 1440) == 0 &&
          !strcmp(url, "https://caps.x.com/v2/cards/create.json") &&
          !strcmp(body, "card_data=%7B%22twitter%3Acard%22%3A%22poll2choice_text_only%22%2C%22twitter%3Aapi%3Aapi"
                        "%3Aendpoint%22%3A%221%22%2C%22twitter%3Along%3Aduration_minutes%22%3A1440%2C%22twitter%3A"
                        "string%3Achoice1_label%22%3A%22Sim%22%2C%22twitter%3Astring%3Achoice2_label%22%3A%22N%C3%A3o"
                        "%20%26%20talvez%22%7D"));
    CHECK(x_poll_card_request(url, sizeof url, body, sizeof body, choices, 1, 1440) == -1);
    CHECK(x_poll_card_request(url, sizeof url, body, sizeof body, choices, 2, 4) == -1);
    {
      const char *longer[] = {"Sim", "uma escolha com mais de vinte e cinco"};
      CHECK(x_poll_card_request(url, sizeof url, body, sizeof body, longer, 2, 60) == -1);
    }
    CHECK((doc=parse("{\"card_uri\":\"card://1900000000000000010\",\"status\":\"OK\"}", &off)) != NULL);
    CHECK(doc && x_card_uri(json_root(doc), uri, sizeof uri) == 0 && !strcmp(uri, "card://1900000000000000010"));
    json_free(doc);
    CHECK((doc=parse("{\"card_uri\":\"https://evil\"}", &off)) != NULL);
    CHECK(doc && x_card_uri(json_root(doc), uri, sizeof uri) == -1 && !uri[0]);
    json_free(doc);
    CHECK(x_vote_request(url, sizeof url, body, sizeof body, "card://1900000000000000009", "2001",
                         "poll3choice_text_only", 3) == 0 &&
          !strcmp(url, "https://caps.x.com/v2/capi/passthrough/1") &&
          !strcmp(body, "twitter%3Astring%3Acard_uri=card%3A%2F%2F1900000000000000009&twitter%3Along%3A"
                        "original_tweet_id=2001&twitter%3Astring%3Aresponse_card_name=poll3choice_text_only&twitter"
                        "%3Astring%3Acards_platform=Web-12&twitter%3Astring%3Aselected_choice=3"));
    CHECK(x_vote_request(url, sizeof url, body, sizeof body, "card://1900000000000000009", "2001",
                         "poll2choice_text_only", 3) == -1);
    CHECK(x_vote_request(url, sizeof url, body, sizeof body, "card://x", "2001", "poll2choice_text_only", 1) == -1);
    {
      static char big[X_BODY_MAX];
      x_compose_t how = {NULL, NULL, NULL, NULL, "card://1900000000000000010"};
      CHECK(x_compose_request(url, sizeof url, big, sizeof big, &how, "Qual?", NULL, 0) == 0 &&
            strstr(big, "\"card_uri\":\"card://1900000000000000010\","));
      how.card_uri = "card://1\"";
      CHECK(x_compose_request(url, sizeof url, big, sizeof big, &how, "Qual?", NULL, 0) == -1);
    }
  }

  // the scheduled posts, soonest first, and taking one back
  {
    static const char sched[] =
      "{\"data\":{\"viewer\":{\"scheduled_tweet_list\":["
      "{\"rest_id\":\"1900000000000000002\",\"scheduling_info\":{\"execute_at\":1763000000000,\"state\":\"Scheduled\"},"
      "\"tweet_create_request\":{\"type\":\"PostTweetRequest\",\"status\":\"Mais tarde\"}},"
      "{\"rest_id\":\"1900000000000000001\",\"scheduling_info\":{\"execute_at\":1762000000000,\"state\":\"Failed\"},"
      "\"tweet_create_request\":{\"type\":\"PostTweetRequest\",\"status\":\"Primeiro\"}},"
      "{\"rest_id\":\"x\",\"scheduling_info\":{\"execute_at\":1}}]}}}";
    x_post_t s[4];
    char url[256], body[256];
    int total = 0;
    CHECK(x_scheduled_url(url, sizeof url) == 0 &&
          !strcmp(url, "https://x.com/i/api/graphql/H2elmT2R9DLhWoo0DZFNkA/FetchScheduledTweets?variables=%7B%7D"));
    CHECK((doc=parse(sched, &off)) != NULL);
    CHECK(doc && x_scheduled_posts(json_root(doc), NULL, 0, &total) == 0 && total == 2);
    CHECK(doc && x_scheduled_posts(json_root(doc), s, 4, &total) == 2 && s[0].kind == X_KIND_SCHEDULED &&
          !strcmp(s[0].id, "1900000000000000001") && s[0].time == 1762000000LL && s[0].icon == 1 &&
          !strcmp(s[0].text, "Primeiro") && !strcmp(s[1].text, "Mais tarde") && s[1].icon == 0);
    json_free(doc);
    CHECK(x_unschedule_request(url, sizeof url, body, sizeof body, "1900000000000000001") == 0 &&
          !strcmp(url, "https://x.com/i/api/graphql/CTOVqej0JBXAZSwkp1US0g/DeleteScheduledTweet") &&
          !strcmp(body, "{\"variables\":{\"scheduled_tweet_id\":\"1900000000000000001\"},"
                        "\"queryId\":\"CTOVqej0JBXAZSwkp1US0g\"}"));
    CHECK(x_unschedule_request(url, sizeof url, body, sizeof body, "1\"}") == -1);
  }

  // who to follow and x.com's side column of trends
  {
    char url[4096];
    CHECK(x_feed_url(url, sizeof url, X_FEED_WHO, NULL, 3, NULL) == 0 &&
          strstr(url, "/A_0eI5aoTb8yUzDUzwm5yw/ConnectTabTimeline?variables=") &&
          strstr(url, "%22count%22%3A3") && strstr(url, "%22context%22%3A%22%7B%7D%22"));
    CHECK(x_feed_url(url, sizeof url, X_FEED_SIDEBAR, NULL, 20, NULL) == 0 &&
          strstr(url, "/cM7LCaFTQnUWRA1jMWH6Pg/ExploreSidebar?variables=%7B%7D&features="));
  }
  {
    static const char who[] =
      "{\"data\":{\"connect_tab_timeline\":{\"timeline\":{\"instructions\":[{\"type\":\"TimelineAddEntries\","
      "\"entries\":[{\"content\":{\"items\":[{\"item\":{\"itemContent\":{\"itemType\":\"TimelineUser\","
      "\"user_results\":{\"result\":{\"rest_id\":\"77\",\"core\":{\"screen_name\":\"PlayStation\","
      "\"name\":\"PlayStation\"},\"avatar\":{\"image_url\":\"https://pbs.twimg.com/profile_images/7/p_normal.jpg\"},"
      "\"verification\":{\"verified_type\":\"Business\"},\"legacy\":{\"description\":\"Play Has No Limits\"},"
      "\"relationship_perspectives\":{\"following\":false}}}}}},"
      "{\"item\":{\"itemContent\":{\"itemType\":\"TimelineUser\",\"user_results\":{\"result\":"
      "{\"rest_id\":\"x\",\"core\":{\"screen_name\":\"bad\"}}}}}}]}}]}]}}}}";
    x_post_t people[4];
    int total;
    CHECK((doc=parse(who, &off)) != NULL);
    CHECK(doc && x_timeline_posts(json_root(doc), people, 4, &total) == 1 && total == 1);
    CHECK(doc && people[0].kind == X_KIND_USER && !strcmp(people[0].user, "PlayStation") &&
          !strcmp(people[0].user_id, "77") && people[0].badge == X_BADGE_GOLD && !people[0].following &&
          !strcmp(people[0].text, "Play Has No Limits") &&
          !strcmp(people[0].avatar, "https://pbs.twimg.com/profile_images/7/p_normal.jpg"));
    json_free(doc);
  }

  // the new shape: the banner beside the avatar, none in legacy
  CHECK((doc=parse("{\"data\":{\"user\":{\"result\":{\"rest_id\":\"7\",\"core\":{\"screen_name\":\"ana\"},"
                   "\"banner\":{\"image_url\":\"https://pbs.twimg.com/profile_banners/7/1700000000\"},"
                   "\"legacy\":{}}}}}", &off)) != NULL);
  CHECK(doc && x_user_parse(json_root(doc), &u) == 0 &&
        !strcmp(u.banner, "https://pbs.twimg.com/profile_banners/7/1700000000"));
  json_free(doc);
  // an address off X's image server is not kept
  CHECK((doc=parse("{\"data\":{\"user\":{\"result\":{\"rest_id\":\"7\",\"core\":{\"screen_name\":\"ana\"},"
                   "\"banner\":{\"image_url\":\"https://evil.example/b\"}}}}}", &off)) != NULL);
  CHECK(doc && x_user_parse(json_root(doc), &u) == 0 && !u.banner[0]);
  json_free(doc);
  CHECK((doc=parse("{\"data\":{\"user\":{\"result\":{\"__typename\":\"UserUnavailable\"}}}}", &off)) &&
        x_user_parse(json_root(doc), &u) == -1);
  json_free(doc);
}


/**
 * the variables of a URL of x_feed_url, as JSON
 **/
static const char *
url_variables(const char *url, char *buf, size_t size) {
  const char *s = strstr(url, "?variables="), *end = strstr(url, "&features=");
  size_t n = 0;
  unsigned c;

  if(!s || !end) {
    return "";
  }
  for(s += strlen("?variables="); s < end && n + 1 < size; s++) {
    if(*s == '%' && sscanf(s + 1, "%2x", &c) == 1) {
      buf[n++] = (char)c;
      s += 2;
    } else {
      buf[n++] = *s;
    }
  }
  buf[n] = 0;
  return buf;
}


/**
 * a search's URL, an answer the way SearchTimeline gives one, and bookmarks
 **/
static void
test_search(void) {
  static char url[8192], vars[2048], body[512], query[X_QUERY_MAX + 2];
  static const char answer[] =
    "{\"data\":{\"search_by_raw_query\":{\"search_timeline\":{\"timeline\":{\"instructions\":["
    "{\"type\":\"TimelineAddEntries\",\"entries\":["
    "{\"entryId\":\"user-module-1\",\"content\":{\"items\":[{\"item\":{\"itemContent\":"
    "{\"user_results\":{\"result\":{\"rest_id\":\"7\",\"core\":{\"screen_name\":\"ps\"}}}}}}]}},"
    "{\"entryId\":\"tweet-2001\",\"content\":{\"itemContent\":{\"tweet_results\":{\"result\":"
    "{\"rest_id\":\"2001\",\"core\":{\"user_results\":{\"result\":{\"rest_id\":\"9\","
    "\"core\":{\"screen_name\":\"rita\",\"name\":\"Rita\"}}}},"
    "\"legacy\":{\"full_text\":\"a PS5 nova\",\"bookmarked\":true}}}}}},"
    "{\"entryId\":\"tweet-2002\",\"content\":{\"itemContent\":{\"tweet_results\":{\"result\":"
    "{\"rest_id\":\"2002\",\"core\":{\"user_results\":{\"result\":{\"rest_id\":\"8\","
    "\"core\":{\"screen_name\":\"tiago\",\"name\":\"Tiago\"}}}},"
    "\"legacy\":{\"full_text\":\"outra\",\"bookmarked\":false}}}}}},"
    "{\"entryId\":\"cursor-bottom-0\",\"content\":{\"cursorType\":\"Bottom\","
    "\"value\":\"DAADDAABCgAB\"}}]}]}}}}}";
  // the pages after the first replace the cursor instead
  static const char next[] =
    "{\"data\":{\"search_by_raw_query\":{\"search_timeline\":{\"timeline\":{\"instructions\":["
    "{\"type\":\"TimelineAddEntries\",\"entries\":[]},"
    "{\"type\":\"TimelineReplaceEntry\",\"entry_id_to_replace\":\"cursor-bottom-0\","
    "\"entry\":{\"entryId\":\"cursor-bottom-0\",\"content\":{\"cursorType\":\"Bottom\","
    "\"value\":\"DAADDAABCgAC\"}}}]}}}}}";
  const char *message;
  x_post_t posts[4];
  json_doc_t *doc;
  size_t off;
  int total;

  CHECK(x_feed_url(url, sizeof url, X_FEED_SEARCH, "ps5 \"pro\" #jogos \xc3\xa0 100%", 20, NULL) == 0);
  CHECK(!strncmp(url, "https://x.com/i/api/graphql/vLHjqbDv7TPFMBkIIO1nOA/SearchTimeline?variables=",
                 strlen("https://x.com/i/api/graphql/vLHjqbDv7TPFMBkIIO1nOA/SearchTimeline?variables=")));
  CHECK(!strcmp(url_variables(url, vars, sizeof vars),
                "{\"rawQuery\":\"ps5 \\\"pro\\\" #jogos \xc3\xa0 100%\",\"count\":20,"
                "\"querySource\":\"typed_query\",\"product\":\"Top\"}"));
  CHECK(strchr(url, '"') == NULL && strchr(url, ' ') == NULL && strchr(url, '#') == NULL &&
        strstr(url, "&features=%7B") && !strstr(url, "fieldToggles"));
  CHECK(x_feed_url(url, sizeof url, X_FEED_SEARCH_LATEST, "ps5", 20, "DAADDAABCgAB") == 0);
  CHECK(!strcmp(url_variables(url, vars, sizeof vars),
                "{\"rawQuery\":\"ps5\",\"count\":20,\"cursor\":\"DAADDAABCgAB\","
                "\"querySource\":\"typed_query\",\"product\":\"Latest\"}"));

  // no words, too many, or a cursor that does not belong in a URL
  CHECK(x_feed_url(url, sizeof url, X_FEED_SEARCH, NULL, 20, NULL) == -1);
  CHECK(x_feed_url(url, sizeof url, X_FEED_SEARCH, "", 20, NULL) == -1);
  memset(query, '"', X_QUERY_MAX);
  query[X_QUERY_MAX] = 0;
  CHECK(x_feed_url(url, sizeof url, X_FEED_SEARCH, query, 20, NULL) == 0);
  query[X_QUERY_MAX] = 'a';
  query[X_QUERY_MAX + 1] = 0;
  CHECK(x_feed_url(url, sizeof url, X_FEED_SEARCH, query, 20, NULL) == -1);
  CHECK(x_feed_url(url, sizeof url, X_FEED_SEARCH, "ps5", 20, "bad\"cursor") == -1);
  CHECK(x_feed_url(url, 300, X_FEED_SEARCH, "ps5", 20, NULL) == -1);

  // the posts, without the people found, and whether they are bookmarked
  CHECK((doc=parse(answer, &off)) != NULL);
  if(doc) {
    CHECK(x_timeline_posts(json_root(doc), posts, 4, &total) == 2 && total == 2);
    CHECK(!strcmp(posts[0].id, "2001") && !strcmp(posts[0].user, "rita") && posts[0].bookmarked);
    CHECK(!strcmp(posts[1].id, "2002") && !posts[1].bookmarked);
    CHECK(x_timeline_cursor(json_root(doc), vars, sizeof vars) == 0 && !strcmp(vars, "DAADDAABCgAB"));
    json_free(doc);
  }
  CHECK((doc=parse(next, &off)) != NULL);
  if(doc) {
    CHECK(x_timeline_posts(json_root(doc), posts, 4, &total) == 0 && total == 0);
    CHECK(x_timeline_cursor(json_root(doc), vars, sizeof vars) == 0 && !strcmp(vars, "DAADDAABCgAC"));
    json_free(doc);
  }

  // bookmarks
  CHECK(x_bookmark_request(url, sizeof url, body, sizeof body, "2001", 1) == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/graphql/aoDbu3RHznuiSkQ9aNM67Q/CreateBookmark"));
  CHECK(!strcmp(body, "{\"variables\":{\"tweet_id\":\"2001\"},\"queryId\":\"aoDbu3RHznuiSkQ9aNM67Q\"}"));
  CHECK(x_bookmark_request(url, sizeof url, body, sizeof body, "2001", 0) == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/graphql/Wlmlj2-xzyS1GN3a6cj-mQ/DeleteBookmark"));
  CHECK(!strcmp(body, "{\"variables\":{\"tweet_id\":\"2001\"},\"queryId\":\"Wlmlj2-xzyS1GN3a6cj-mQ\"}"));
  CHECK(x_bookmark_request(url, sizeof url, body, sizeof body, "20\"1", 1) == -1);
  CHECK(x_bookmark_request(url, sizeof url, body, sizeof body, "", 1) == -1);
  CHECK(x_bookmark_request(url, 40, body, sizeof body, "2001", 1) == -1);
  CHECK(x_bookmark_request(url, sizeof url, body, 30, "2001", 1) == -1);
  CHECK((doc=parse("{\"data\":{\"tweet_bookmark_put\":\"Done\"}}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);
  CHECK((doc=parse("{\"data\":{\"tweet_bookmark_delete\":\"Done\"}}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);
}


static void
test_dm(void) {
  static char url[4096], body[4096], text[X_REPLY_MAX + 2];
  // two people and a group; the user is 1; the newest message of each
  static const char inbox[] =
    "{\"inbox_initial_state\":{\"last_seen_event_id\":\"900\",\"entries\":["
    "{\"message\":{\"id\":\"501\",\"time\":\"1700000001000\",\"conversation_id\":\"1-2\","
    "\"message_data\":{\"id\":\"501\",\"sender_id\":\"2\",\"text\":\"ol\xc3\xa1 &amp; adeus\"}}},"
    "{\"message\":{\"id\":\"510\",\"time\":\"1700000100000\",\"conversation_id\":\"1-2\","
    "\"message_data\":{\"id\":\"510\",\"sender_id\":\"1\",\"text\":\"v\xc3\xaa https://t.co/abc\","
    "\"entities\":{\"urls\":[{\"url\":\"https://t.co/abc\",\"display_url\":\"x.com/ps\"}]}}}},"
    "{\"message\":{\"id\":\"520\",\"time\":\"1700000200000\",\"conversation_id\":\"1-3\","
    "\"message_data\":{\"id\":\"520\",\"sender_id\":\"3\",\"text\":\"https://t.co/pic\","
    "\"attachment\":{\"photo\":{\"url\":\"https://t.co/pic\"}}}}},"
    "{\"message\":{\"id\":\"530\",\"time\":\"1700000300000\",\"conversation_id\":\"77\","
    "\"message_data\":{\"id\":\"530\",\"sender_id\":\"3\",\"text\":\"bora jogar\"}}}],"
    "\"users\":{\"2\":{\"id_str\":\"2\",\"name\":\"Rita\",\"screen_name\":\"rita\","
    "\"profile_image_url_https\":\"https://pbs.twimg.com/profile_images/2/a.jpg\",\"is_blue_verified\":true},"
    "\"3\":{\"id_str\":\"3\",\"name\":\"Tiago\",\"screen_name\":\"tiago\"},"
    "\"1\":{\"id_str\":\"1\",\"name\":\"Eu\",\"screen_name\":\"eu\"}},"
    "\"conversations\":{"
    "\"1-2\":{\"conversation_id\":\"1-2\",\"type\":\"ONE_TO_ONE\",\"sort_event_id\":\"510\","
    "\"sort_timestamp\":\"1700000100000\",\"trusted\":true,"
    "\"participants\":[{\"user_id\":\"1\",\"last_read_event_id\":\"510\"},{\"user_id\":\"2\"}]},"
    "\"1-3\":{\"conversation_id\":\"1-3\",\"type\":\"ONE_TO_ONE\",\"sort_event_id\":\"520\","
    "\"sort_timestamp\":\"1700000200000\",\"trusted\":false,"
    "\"participants\":[{\"user_id\":\"1\",\"last_read_event_id\":\"99\"},{\"user_id\":\"3\"}]},"
    "\"77\":{\"conversation_id\":\"77\",\"type\":\"GROUP_DM\",\"sort_event_id\":\"530\","
    "\"sort_timestamp\":\"1700000300000\",\"trusted\":true,"
    "\"participants\":[{\"user_id\":\"1\",\"last_read_event_id\":\"530\"},{\"user_id\":\"2\"},{\"user_id\":\"3\"}]},"
    "\"bad\\\"id\":{\"conversation_id\":\"bad\\\"id\"}}}}";
  static const char conversation[] =
    "{\"conversation_timeline\":{\"status\":\"AT_END\",\"entries\":["
    "{\"message\":{\"id\":\"510\",\"time\":\"1700000100000\",\"conversation_id\":\"1-2\","
    "\"message_data\":{\"id\":\"510\",\"sender_id\":\"1\",\"text\":\"depois\"}}},"
    "{\"reaction_create\":{\"id\":\"505\"}},"
    "{\"message\":{\"id\":\"501\",\"time\":\"1700000001000\",\"conversation_id\":\"1-2\","
    "\"message_data\":{\"id\":\"501\",\"sender_id\":\"2\",\"text\":\"primeiro\"}}},"
    "{\"message\":{\"id\":\"9\",\"time\":\"1600000000000\",\"conversation_id\":\"1-2\","
    "\"message_data\":{\"id\":\"9\",\"sender_id\":\"2\",\"text\":\"https://t.co/v\","
    "\"attachment\":{\"video\":{\"url\":\"https://t.co/v\"}}}}}],"
    "\"users\":{\"2\":{\"id_str\":\"2\",\"name\":\"Rita\",\"screen_name\":\"rita\"},"
    "\"1\":{\"id_str\":\"1\",\"name\":\"Eu\",\"screen_name\":\"eu\"}}}}";
  const char *message;
  x_post_t posts[6];
  json_doc_t *doc;
  size_t off;

  CHECK(x_dm_inbox_url(url, sizeof url) == 0);
  CHECK(!strncmp(url, "https://x.com/i/api/1.1/dm/inbox_initial_state.json?cards_platform=Web-12&", 74));
  CHECK(strstr(url, "&include_groups=true&") && strstr(url, "&tweet_mode=extended") && !strchr(url, ','));
  CHECK(x_dm_inbox_url(url, 100) == -1);
  CHECK(x_dm_conversation_url(url, sizeof url, "1-2") == 0);
  CHECK(!strncmp(url, "https://x.com/i/api/1.1/dm/conversation/1-2.json?cards_platform=", 64) &&
        strstr(url, "&context=FETCH_DM_CONVERSATION"));
  CHECK(x_dm_conversation_url(url, sizeof url, "1-2/../x") == -1);
  CHECK(x_dm_conversation_url(url, sizeof url, "-12") == -1);
  CHECK(x_dm_conversation_url(url, sizeof url, "") == -1);
  CHECK(x_dm_conversation_url(url, sizeof url, NULL) == -1);

  CHECK((doc=parse(inbox, &off)) != NULL);
  if(doc) {
    CHECK(x_dm_inbox(json_root(doc), "1", NULL, 0) == 3);
    CHECK(x_dm_inbox(json_root(doc), "1", posts, 6) == 3);
    // the group first, as the newest
    CHECK(posts[0].kind == X_KIND_CHAT && !strcmp(posts[0].id, "77") && posts[0].group);
    CHECK(!strcmp(posts[0].name, "Rita, Tiago") && !posts[0].user[0] && !posts[0].unread);
    CHECK(!strcmp(posts[0].text, "bora jogar") && !strcmp(posts[0].quote_name, "Tiago") && !posts[0].mine);
    CHECK(posts[0].face_count == 1 && posts[0].time == 1700000300);
    // a request, unread, with a photo
    CHECK(!strcmp(posts[1].id, "1-3") && !strcmp(posts[1].user, "tiago") && posts[1].request);
    CHECK(posts[1].unread && posts[1].icon == X_DM_PHOTO && !posts[1].text[0]);
    // read, the user's own last message, its link as X shows it
    CHECK(!strcmp(posts[2].id, "1-2") && !strcmp(posts[2].name, "Rita") && posts[2].badge == X_BADGE_BLUE);
    CHECK(!strcmp(posts[2].user_id, "2") && !strcmp(posts[2].avatar, "https://pbs.twimg.com/profile_images/2/a.jpg"));
    CHECK(!posts[2].unread && !posts[2].request && posts[2].mine && !strcmp(posts[2].text, "v\xc3\xaa x.com/ps"));
    CHECK(!posts[2].quote_user[0] && posts[2].likes == -1);
    CHECK(x_dm_inbox(json_root(doc), "1", posts, 1) == 1 && !strcmp(posts[0].id, "1-2"));
    json_free(doc);
  }

  CHECK((doc=parse(conversation, &off)) != NULL);
  if(doc) {
    CHECK(x_dm_messages(json_root(doc), "1-2", NULL, 0) == 3);
    CHECK(x_dm_messages(json_root(doc), "1-2", posts, 6) == 3);
    // oldest first
    CHECK(!strcmp(posts[0].id, "9") && posts[0].icon == X_DM_VIDEO && !posts[0].text[0]);
    CHECK(!strcmp(posts[1].id, "501") && !strcmp(posts[1].text, "primeiro") && !strcmp(posts[1].user, "rita"));
    CHECK(!strcmp(posts[1].user_id, "2") && posts[1].time == 1700000001 && posts[1].kind == X_KIND_DM);
    CHECK(!strcmp(posts[2].id, "510") && !strcmp(posts[2].user_id, "1") && !strcmp(posts[2].text, "depois"));
    CHECK(x_dm_messages(json_root(doc), "9-9", posts, 6) == 0);
    json_free(doc);
  }

  // sending
  CHECK(x_dm_send_request(url, sizeof url, body, sizeof body, "1-2", "ol\xc3\xa1 \"PS5\"\n",
                          "6f1c0a2e-0000-4000-8000-123456789abc") == 0);
  CHECK(!strncmp(url, "https://x.com/i/api/1.1/dm/new2.json?ext=mediaColor%2CaltText", 61));
  CHECK(!strcmp(body, "{\"conversation_id\":\"1-2\",\"recipient_ids\":false,"
                "\"request_id\":\"6f1c0a2e-0000-4000-8000-123456789abc\",\"text\":\"ol\xc3\xa1 \\\"PS5\\\"\\n\","
                "\"cards_platform\":\"Web-12\",\"include_cards\":1,\"include_quote_count\":true,\"dm_users\":false}"));
  CHECK(x_dm_send_request(url, sizeof url, body, sizeof body, "1-2", "", "a") == -1);
  CHECK(x_dm_send_request(url, sizeof url, body, sizeof body, "1\"2", "oi", "a") == -1);
  CHECK(x_dm_send_request(url, sizeof url, body, sizeof body, "1-2", "oi", "a\"b") == -1);
  CHECK(x_dm_send_request(url, sizeof url, body, 100, "1-2", "oi", "a") == -1);
  memset(text, 'a', X_REPLY_MAX + 1);
  text[X_REPLY_MAX + 1] = 0;
  CHECK(x_dm_send_request(url, sizeof url, body, sizeof body, "1-2", text, "a") == -1);
  CHECK((doc=parse("{\"entries\":[{\"message\":{\"id\":\"600\"}}]}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);
  CHECK((doc=parse("{\"errors\":[{\"code\":349,\"message\":\"You cannot send messages to this user.\"}]}", &off)) &&
        x_action_done(json_root(doc), &message) == 349);
  json_free(doc);
}


static void
test_social(void) {
  static char url[1024], body[X_BODY_MAX], text[X_REPLY_MAX + 2];
  x_compose_t quote = {NULL, "ps_pt", "2001", NULL, NULL}, edit = {NULL, NULL, NULL, "2002", NULL};
  x_compose_t bad = {NULL, "ps pt", "2001", NULL, NULL};
  const char *message;
  json_doc_t *doc;
  size_t off;

  // a quote, with no words of its own
  CHECK(x_compose_request(url, sizeof url, body, sizeof body, &quote, "", NULL, 0) == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/graphql/l4hk8uVGvR-a8kzQ4nOhPA/CreateTweet"));
  CHECK(strstr(body, "{\"variables\":{\"tweet_text\":\"\",\"attachment_url\":\"https://x.com/ps_pt/status/2001\","
               "\"dark_request\":false,") != NULL);
  CHECK(x_compose_request(url, sizeof url, body, sizeof body, &bad, "a", NULL, 0) == -1);
  // an edit
  CHECK(x_compose_request(url, sizeof url, body, sizeof body, &edit, "corrigido", NULL, 0) == 0);
  CHECK(strstr(body, "\"tweet_text\":\"corrigido\",\"edit_options\":{\"previous_tweet_id\":\"2002\"},") != NULL);
  CHECK(!strstr(body, "richtext_options"));
  // over 280: a long post of X Premium
  memset(text, 'a', 281);
  text[281] = 0;
  CHECK(x_post_length(text) == 281 && x_post_length("\xe6\x97\xa5\xe6\x9c\xac") == 4 &&
        x_post_length("ol\xc3\xa1") == 3 && x_post_length("\xf0\x9f\x98\x82") == 2);
  CHECK(x_compose_request(url, sizeof url, body, sizeof body, NULL, text, NULL, 0) == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/graphql/seOZkIS8AysCGfPo6yYPVA/CreateNoteTweet"));
  CHECK(strstr(body, "\"richtext_options\":{\"richtext_tags\":[]},\"disallowed_reply_options\":null}") &&
        strstr(body, ",\"queryId\":\"seOZkIS8AysCGfPo6yYPVA\"}"));
  memset(text, 'a', X_REPLY_MAX + 1);
  text[X_REPLY_MAX + 1] = 0;
  CHECK(x_compose_request(url, sizeof url, body, sizeof body, NULL, text, NULL, 0) == -1);
  CHECK((doc=parse("{\"data\":{\"notetweet_create\":{\"tweet_results\":{\"result\":{\"rest_id\":\"9\"}}}}}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);

  // pinning
  CHECK(x_pin_request(url, sizeof url, body, sizeof body, "2001", 1) == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/graphql/VIHsNu89pK-kW35JpHq7Xw/PinTweet"));
  CHECK(!strcmp(body, "{\"variables\":{\"tweet_id\":\"2001\"},\"queryId\":\"VIHsNu89pK-kW35JpHq7Xw\"}"));
  CHECK(x_pin_request(url, sizeof url, body, sizeof body, "2001", 0) == 0 && strstr(url, "/UnpinTweet"));
  CHECK(x_pin_request(url, sizeof url, body, sizeof body, "x", 1) == -1);
  CHECK((doc=parse("{\"data\":{\"pin_tweet\":{\"message\":\"ok\"}}}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);
  CHECK((doc=parse("{\"data\":{\"pin_tweet\":null},\"errors\":[{\"code\":34,\"message\":\"no\"}]}", &off)) &&
        x_action_done(json_root(doc), &message) == 34);
  json_free(doc);

  // following, muting, blocking
  CHECK(x_user_request(url, sizeof url, body, sizeof body, X_FOLLOW, "783214") == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/1.1/friendships/create.json"));
  CHECK(!strncmp(body, "include_profile_interstitial_type=1&", 36) && strstr(body, "&skip_status=1&user_id=783214"));
  CHECK(x_user_request(url, sizeof url, body, sizeof body, X_UNFOLLOW, "1") == 0 &&
        !strcmp(url, "https://x.com/i/api/1.1/friendships/destroy.json"));
  CHECK(x_user_request(url, sizeof url, body, sizeof body, X_MUTE, "1") == 0 &&
        !strcmp(url, "https://x.com/i/api/1.1/mutes/users/create.json"));
  CHECK(x_user_request(url, sizeof url, body, sizeof body, X_UNBLOCK, "1") == 0 &&
        !strcmp(url, "https://x.com/i/api/1.1/blocks/destroy.json"));
  CHECK(x_user_request(url, sizeof url, body, sizeof body, X_BLOCK, "1&x=2") == -1);
  CHECK(x_user_request(url, sizeof url, body, sizeof body, 99, "1") == -1);
  CHECK(x_user_request(url, sizeof url, body, 40, X_MUTE, "1") == -1);
  CHECK((doc=parse("{\"id\":783214,\"id_str\":\"783214\",\"screen_name\":\"x\",\"following\":true}", &off)) &&
        x_action_done(json_root(doc), &message) == 0);
  json_free(doc);
  CHECK((doc=parse("{\"errors\":[{\"code\":161,\"message\":\"You are unable to follow more people.\"}]}", &off)) &&
        x_action_done(json_root(doc), &message) == 161);
  json_free(doc);
}


static void
test_schedule(void) {
  static char url[256], body[X_BODY_MAX], text[400];
  const char *media[] = {"1001", "1002"}, *bad[] = {"10x"};
  const char *message;
  json_doc_t *doc;
  size_t off;

  CHECK(x_schedule_request(url, sizeof url, body, sizeof body, "Logo \xc3\xa0 noite \"live\"", NULL, 0,
                           1763000000) == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/graphql/LCVzRQGxOaGnOnYH01NQXg/CreateScheduledTweet"));
  CHECK(!strcmp(body, "{\"variables\":{\"post_tweet_request\":{\"auto_populate_reply_metadata\":false,"
                      "\"status\":\"Logo \xc3\xa0 noite \\\"live\\\"\",\"exclude_reply_user_ids\":[],\"media_ids\":[]},"
                      "\"execute_at\":1763000000},\"queryId\":\"LCVzRQGxOaGnOnYH01NQXg\"}"));
  CHECK(x_schedule_request(url, sizeof url, body, sizeof body, "", media, 2, 1763000000) == 0);
  CHECK(strstr(body, "\"media_ids\":[\"1001\",\"1002\"]},") != NULL);
  CHECK(x_schedule_request(url, sizeof url, body, sizeof body, "a", bad, 1, 1763000000) == -1);
  CHECK(x_schedule_request(url, sizeof url, body, sizeof body, "", NULL, 0, 1763000000) == -1);
  CHECK(x_schedule_request(url, sizeof url, body, sizeof body, "a", NULL, 0, 0) == -1);
  memset(text, 'a', 281);
  text[281] = 0;
  CHECK(x_schedule_request(url, sizeof url, body, sizeof body, text, NULL, 0, 1763000000) == -1);
  CHECK(x_schedule_request(url, sizeof url, body, 120, "a", NULL, 0, 1763000000) == -1);
  CHECK((doc=parse("{\"data\":{\"tweet\":{\"rest_id\":\"1856\"}}}", &off)) && x_action_done(json_root(doc), &message) == 0);
  json_free(doc);
}


static void
test_dm_read(void) {
  char url[256], body[128];

  CHECK(x_dm_read_request(url, sizeof url, body, sizeof body, "123-456", "1789") == 0);
  CHECK(!strcmp(url, "https://x.com/i/api/1.1/dm/conversation/123-456/mark_read.json"));
  CHECK(!strcmp(body, "conversationId=123-456&last_read_event_id=1789"));
  CHECK(x_dm_read_request(url, sizeof url, body, sizeof body, "123&x", "1789") == -1);
  CHECK(x_dm_read_request(url, sizeof url, body, sizeof body, "123-456", "17a") == -1);
  CHECK(x_dm_read_request(url, sizeof url, body, sizeof body, "123-456", NULL) == -1);
  CHECK(x_dm_read_request(url, 40, body, sizeof body, "123-456", "1789") == -1);
}


static void
test_explore(void) {
  static const char trends[] =
    "{\"data\":{\"timeline\":{\"timeline\":{\"instructions\":[{\"type\":\"TimelineAddEntries\",\"entries\":["
    "{\"entryId\":\"trend-1\",\"content\":{\"entryType\":\"TimelineTimelineItem\",\"itemContent\":{"
    "\"itemType\":\"TimelineTrend\",\"__typename\":\"TimelineTrend\",\"name\":\"#PS5\",\"rank\":\"1\","
    "\"trend_url\":{\"url\":\"twitter://search/?query=%23PS5+Pro&src=trend_click&vertical=trends\","
    "\"urlType\":\"DeepLink\"},\"trend_metadata\":{\"domain_context\":\"Trending in Portugal\","
    "\"meta_description\":\"12.3K posts\"}}}},"
    "{\"entryId\":\"trend-2\",\"content\":{\"entryType\":\"TimelineTimelineItem\",\"itemContent\":{"
    "\"__typename\":\"TimelineTrend\",\"name\":\"Ad\",\"promoted_metadata\":{\"promotedTrendName\":\"Ad\"},"
    "\"trend_url\":{\"url\":\"twitter://search/?query=Ad\"},\"trend_metadata\":{}}}},"
    "{\"entryId\":\"trend-3\",\"content\":{\"entryType\":\"TimelineTimelineItem\",\"itemContent\":{"
    "\"__typename\":\"TimelineTrend\",\"name\":\"Benfica &amp; Porto\",\"is_ai_trend\":true,"
    "\"trend_url\":{\"url\":\"https://x.com/i/trending/1\",\"urlType\":\"UrtEndpoint\"},"
    "\"trend_metadata\":{\"domain_context\":\"Sports \\u00b7 Trending\"},\"description\":\"Big match\","
    "\"thumbnail_image\":{\"original_img_url\":\"https://pbs.twimg.com/media/T1.jpg\"}}}},"
    "{\"entryId\":\"trend-4\",\"content\":{\"entryType\":\"TimelineTimelineItem\",\"itemContent\":{"
    "\"__typename\":\"TimelineTrend\",\"name\":\"bad\",\"trend_url\":{\"url\":\"twitter://search/?query=a%0Ab\"},"
    "\"trend_metadata\":{},\"images\":[{\"url\":\"https://example.com/x.jpg\"}]}}},"
    "{\"entryId\":\"cursor-bottom-1\",\"content\":{\"entryType\":\"TimelineTimelineCursor\","
    "\"value\":\"DAACCgACGQ\",\"cursorType\":\"Bottom\"}}]}]}}}}";
  static const char lists[] =
    "{\"data\":{\"viewer\":{\"list_management_timeline\":{\"timeline\":{\"instructions\":[{\"type\":"
    "\"TimelineAddEntries\",\"entries\":[{\"entryId\":\"owned-subscribed-list-module-0\",\"content\":{"
    "\"entryType\":\"TimelineTimelineModule\",\"items\":["
    "{\"entryId\":\"a-0\",\"item\":{\"itemContent\":{\"itemType\":\"TimelineTwitterList\",\"displayType\":"
    "\"ListWithPin\",\"list\":{\"id_str\":\"1500\",\"name\":\"Jogos\",\"description\":\"PS5 &amp; mais\","
    "\"member_count\":42,\"subscriber_count\":7,\"mode\":\"Private\",\"following\":false,\"pinning\":true,"
    "\"custom_banner_media\":{\"media_info\":{\"original_img_url\":\"https://pbs.twimg.com/list_banner_img/1/b\"}},"
    "\"user_results\":{\"result\":{\"rest_id\":\"99\",\"core\":{\"screen_name\":\"rita\",\"name\":\"Rita\"},"
    "\"is_blue_verified\":true}}}}}},"
    "{\"entryId\":\"a-1\",\"item\":{\"itemContent\":{\"itemType\":\"TimelineTwitterList\",\"displayType\":"
    "\"ListWithPin\",\"list\":{\"id_str\":\"1500\",\"name\":\"Jogos\"}}}},"
    "{\"entryId\":\"a-2\",\"item\":{\"itemContent\":{\"__typename\":\"TimelineTwitterList\",\"displayType\":"
    "\"ListWithPin\",\"list\":{\"id_str\":\"1600\",\"name\":\"Not\\u00edcias\",\"mode\":\"Public\","
    "\"following\":true,\"member_count\":3,"
    "\"default_banner_media\":{\"media_info\":{\"original_img_url\":\"https://pbs.twimg.com/media/D.jpg\"}}}}}},"
    "{\"entryId\":\"a-3\",\"item\":{\"itemContent\":{\"itemType\":\"TimelineTwitterList\",\"displayType\":"
    "\"ListWithSubscribe\",\"list\":{\"id_str\":\"1700\",\"name\":\"Sugerida\",\"following\":false}}}},"
    "{\"entryId\":\"a-4\",\"item\":{\"itemContent\":{\"itemType\":\"TimelineTwitterList\","
    "\"list\":{\"id_str\":\"17x\",\"name\":\"Bad\"}}}}]}}]}]}}}}}";
  static char url[8192], vars[512];
  x_post_t posts[6];
  json_doc_t *doc;
  size_t off;
  int total;

  // Explore's tabs
  CHECK(x_feed_url(url, sizeof url, X_FEED_TRENDING, NULL, 20, NULL) == 0);
  CHECK(!strncmp(url, "https://x.com/i/api/graphql/UToImY30uYcerTaw8gu21w/GenericTimelineById?variables=", 81));
  CHECK(!strcmp(url_variables(url, vars, sizeof vars),
                "{\"timelineId\":\"VGltZWxpbmU6DAC2CwABAAAACHRyZW5kaW5nAAA\",\"count\":20,"
                "\"withQuickPromoteEligibilityTweetFields\":true}"));
  CHECK(x_feed_url(url, sizeof url, X_FEED_NEWS, NULL, 20, "DAACCgACGQ") == 0);
  CHECK(!strcmp(url_variables(url, vars, sizeof vars),
                "{\"timelineId\":\"VGltZWxpbmU6DAC2CwABAAAABG5ld3MAAA\",\"count\":20,\"cursor\":\"DAACCgACGQ\","
                "\"withQuickPromoteEligibilityTweetFields\":true}"));
  // the lists, and a list's posts
  CHECK(x_feed_url(url, sizeof url, X_FEED_LISTS, NULL, 20, NULL) == 0);
  CHECK(strstr(url, "/HeY9oqJxWKHSTCoBXaWnHg/ListsManagementPageTimeline?variables=") &&
        !strcmp(url_variables(url, vars, sizeof vars), "{\"count\":100}"));
  CHECK(x_feed_url(url, sizeof url, X_FEED_LIST, "1500", 20, NULL) == 0);
  CHECK(strstr(url, "/UMc_lPEeKz_WiKSY8ZT7yw/ListLatestTweetsTimeline?variables=") &&
        !strcmp(url_variables(url, vars, sizeof vars), "{\"listId\":\"1500\",\"count\":20}"));
  CHECK(x_feed_url(url, sizeof url, X_FEED_LIST, "15\"00", 20, NULL) == -1);
  CHECK(x_feed_url(url, sizeof url, X_FEED_LIST, NULL, 20, NULL) == -1);
  CHECK(x_feed_url(url, sizeof url, X_FEED_FRIENDS + 1, "1", 20, NULL) == -1);

  // the trends, without the paid one; what each searches for
  CHECK((doc=parse(trends, &off)) != NULL);
  if(doc) {
    CHECK(x_timeline_posts(json_root(doc), posts, 6, &total) == 3 && total == 3);
    CHECK(posts[0].kind == X_KIND_TREND && !strcmp(posts[0].name, "#PS5") && !strcmp(posts[0].text, "#PS5 Pro") &&
          !strcmp(posts[0].quote_name, "Trending in Portugal") && !strcmp(posts[0].quote_text, "12.3K posts") &&
          posts[0].icon == 1 && !posts[0].group && !posts[0].avatar[0] && !posts[0].id[0]);
    CHECK(!strcmp(posts[1].name, "Benfica & Porto") && !strcmp(posts[1].text, "Benfica & Porto") &&
          posts[1].group && !strcmp(posts[1].quote_text, "Big match") &&
          !strcmp(posts[1].avatar, "https://pbs.twimg.com/media/T1.jpg") && posts[1].icon == 0);
    CHECK(!strcmp(posts[2].text, "bad") && !posts[2].avatar[0]);
    CHECK(x_timeline_cursor(json_root(doc), vars, sizeof vars) == 0 && !strcmp(vars, "DAACCgACGQ"));
    json_free(doc);
  }
  // the lists, once each, without the suggested one
  CHECK((doc=parse(lists, &off)) != NULL);
  if(doc) {
    CHECK(x_timeline_posts(json_root(doc), posts, 6, &total) == 2 && total == 2);
    CHECK(posts[0].kind == X_KIND_LIST && !strcmp(posts[0].id, "1500") && !strcmp(posts[0].name, "Jogos") &&
          !strcmp(posts[0].quote_text, "PS5 & mais") && posts[0].likes == 42 && posts[0].reposts == 7 &&
          posts[0].request && !posts[0].following && posts[0].pinned &&
          !strcmp(posts[0].avatar, "https://pbs.twimg.com/list_banner_img/1/b") && !strcmp(posts[0].user, "rita") &&
          !strcmp(posts[0].user_id, "99") && !strcmp(posts[0].quote_name, "Rita") && posts[0].badge == X_BADGE_BLUE);
    CHECK(!strcmp(posts[1].id, "1600") && !strcmp(posts[1].name, "Not\xc3\xad" "cias") && !posts[1].request &&
          posts[1].following && posts[1].likes == 3 && posts[1].reposts == -1 && !posts[1].user[0] &&
          !strcmp(posts[1].avatar, "https://pbs.twimg.com/media/D.jpg"));
    json_free(doc);
  }
}


/**
 * Playing now: the location's form, Following, a message with a capture,
 * its upload, and who plays in a conversation and its invite
 **/
static void
test_playing(void) {
  static char url[8192], body[4096];
  x_post_t posts[2];
  json_doc_t *doc;
  size_t off;
  const char *conv =
    "{\"conversation_timeline\":{\"entries\":[{\"message\":{\"id\":\"100\",\"time\":\"1700000000000\","
    "\"conversation_id\":\"2-3\",\"message_data\":{\"id\":\"100\",\"time\":\"1700000000000\",\"sender_id\":\"3\","
    "\"text\":\"\xf0\x9f\x8e\xae ASTRO BOT \xc2\xb7 bora jogar? (XPS5 PPSA01325)\"}}},"
    "{\"message\":{\"id\":\"101\",\"time\":\"1700000001000\",\"conversation_id\":\"2-3\",\"message_data\":"
    "{\"id\":\"101\",\"time\":\"1700000001000\",\"sender_id\":\"2\",\"text\":\"ok\"}}}],"
    "\"users\":{\"3\":{\"id_str\":\"3\",\"screen_name\":\"ana\",\"name\":\"Ana\","
    "\"location\":\"\xf0\x9f\x8e\xae ASTRO BOT \xc2\xb7 PS5\"},"
    "\"2\":{\"id_str\":\"2\",\"screen_name\":\"me\",\"name\":\"Me\",\"location\":\"Porto\"}}}}";

  CHECK(!x_location_request(url, sizeof url, body, sizeof body, "\xf0\x9f\x8e\xae ASTRO BOT \xc2\xb7 PS5"));
  CHECK(!strcmp(url, "https://x.com/i/api/1.1/account/update_profile.json"));
  CHECK(!strcmp(body, "location=%F0%9F%8E%AE%20ASTRO%20BOT%20%C2%B7%20PS5"));
  CHECK(!x_location_request(url, sizeof url, body, sizeof body, "") && !strcmp(body, "location="));
  CHECK(x_location_request(url, sizeof url, body, sizeof body, "1234567890123456789012345678901") == -1);
  CHECK(x_location_request(url, sizeof url, body, sizeof body, "a\nb") == -1);
  CHECK(x_location_request(url, sizeof url, body, sizeof body, NULL) == -1);

  CHECK(!x_feed_url(url, sizeof url, X_FEED_FRIENDS, "12", 100, NULL));
  CHECK(strstr(url, "/i/api/graphql/eDqlbOS46zda8jDT8dEQ_Q/Following?variables=") &&
        strstr(url, "%22userId%22%3A%2212%22") && strstr(url, "%22count%22%3A100"));
  CHECK(x_feed_url(url, sizeof url, X_FEED_FRIENDS, NULL, 100, NULL) == -1);

  CHECK(!x_dm_media_request(url, sizeof url, body, sizeof body, "2-3", "", "1234", "ab-12"));
  CHECK(strstr(body, "\"text\":\"\"") && strstr(body, ",\"media_id\":\"1234\"}"));
  CHECK(!x_dm_media_request(url, sizeof url, body, sizeof body, "2-3", "gg", NULL, "ab-12") &&
        !strstr(body, "media_id"));
  CHECK(x_dm_media_request(url, sizeof url, body, sizeof body, "2-3", "", NULL, "ab-12") == -1);
  CHECK(x_dm_media_request(url, sizeof url, body, sizeof body, "2-3", "", "12x", "ab-12") == -1);
  CHECK(x_dm_send_request(url, sizeof url, body, sizeof body, "2-3", "", "ab-12") == -1);

  CHECK(!x_upload_url(url, sizeof url, X_UPLOAD_INIT, NULL, 100, "image/jpeg", X_UPLOAD_FOR_DM) &&
        strstr(url, "media_category=dm_image"));
  CHECK(!x_upload_url(url, sizeof url, X_UPLOAD_INIT, NULL, 100, "video/mp4", X_UPLOAD_FOR_DM) &&
        strstr(url, "media_category=dm_video"));
  CHECK(!x_upload_url(url, sizeof url, X_UPLOAD_INIT, NULL, 100, "image/png", 0) &&
        strstr(url, "media_category=tweet_image"));

  // a profile in x.com's newer shape: its bio in profile_bio, its game in location
  {
    static x_user_t u;
    const char *profile =
      "{\"data\":{\"user\":{\"result\":{\"__typename\":\"User\",\"rest_id\":\"12\",\"core\":{\"screen_name\":"
      "\"ana\",\"name\":\"Ana\"},\"profile_bio\":{\"description\":\"Ol\xc3\xa1 https://t.co/abc\",\"entities\":"
      "{\"description\":{\"urls\":[{\"url\":\"https://t.co/abc\",\"display_url\":\"ana.pt\"}]}}},"
      "\"location\":{\"location\":\"\xf0\x9f\x8e\xae ASTRO BOT \xc2\xb7 PS5\"},\"legacy\":{}}}}}";

    CHECK((doc=parse(profile, &off)) != NULL);
    if(doc) {
      CHECK(!x_user_parse(json_root(doc), &u) && !strcmp(u.bio, "Ol\xc3\xa1 ana.pt") &&
            !strcmp(u.location, "\xf0\x9f\x8e\xae ASTRO BOT \xc2\xb7 PS5"));
      json_free(doc);
    }
  }

  CHECK((doc=parse(conv, &off)) != NULL);
  if(doc) {
    CHECK(x_dm_messages(json_root(doc), "2-3", posts, 2) == 2);
    CHECK(posts[0].icon == X_DM_INVITE && !strcmp(posts[0].playing, "ASTRO BOT") && !strcmp(posts[0].user, "ana"));
    CHECK(posts[1].icon == X_DM_TEXT && !posts[1].playing[0]);
    json_free(doc);
  }
}


/**
 * Grok's requests, its question and reading its answer
 **/
static void
test_grok(void) {
  static char url[256], body[4096], prompt[2048], out[64];
  char id[X_GROK_ID_MAX], answer[] =
    "{\"conversationId\":\"1890\"}\n"
    "{\"result\":{\"sender\":\"USER\",\"message\":\"where is the key\"}}\n"
    "{\"result\":{\"sender\":\"ASSISTANT\",\"message\":\"hmm\",\"isThinking\":true}}\n"
    "\n"
    "not json\n"
    "{\"result\":{\"sender\":\"ASSISTANT\",\"message\":\"Look **behind** \"}}\n"
    "{\"result\":{\"sender\":\"ASSISTANT\",\"message\":\"the waterfall \\u00e9.\"}}\n"
    "{\"result\":{\"sender\":\"ASSISTANT\",\"followUpSuggestions\":[]}}";
  char errors[] = "{\"errors\":[{\"code\":88,\"message\":\"Rate limit exceeded\"}]}";
  json_doc_t *doc;
  size_t off;
  int x_code;

  CHECK(!x_grok_create_request(url, sizeof url, body, sizeof body) &&
        !strcmp(url, "https://x.com/i/api/graphql/vvC5uy7pWWHXS2aDi1FZeA/CreateGrokConversation") &&
        !strcmp(body, "{\"variables\":{},\"queryId\":\"vvC5uy7pWWHXS2aDi1FZeA\"}"));
  CHECK(x_grok_create_request(url, 20, body, sizeof body) == -1);
  CHECK((doc=parse("{\"data\":{\"create_grok_conversation\":{\"conversation_id\":\"1890123\"}}}", &off)));
  CHECK(doc && !x_grok_conversation(json_root(doc), id, sizeof id) && !strcmp(id, "1890123"));
  CHECK(doc && x_grok_conversation(json_root(doc), id, 4) == -1 && !id[0]);
  json_free(doc);
  CHECK((doc=parse("{\"data\":{\"create_grok_conversation\":{\"conversation_id\":\"1\\\"2\"}}}", &off)));
  CHECK(doc && x_grok_conversation(json_root(doc), id, sizeof id) == -1);
  json_free(doc);

  CHECK(!x_grok_prompt(prompt, sizeof prompt, "ASTRO BOT", "how do I get past the ice level?", "English") &&
        !strncmp(prompt, "I am playing \"ASTRO BOT\" on a PS5 and I want a hint, not a walkthrough: how do I get "
                 "past the ice level?\n\nAnswer in English,", 115) && strstr(prompt, "Do not spoil anything"));
  CHECK(!x_grok_prompt(prompt, sizeof prompt, "", "where?", "Dutch") &&
        !strncmp(prompt, "I am playing a game on a PS5", 28) && strstr(prompt, "Answer in Dutch,"));
  CHECK(x_grok_prompt(prompt, sizeof prompt, "X", "", "English") == -1);
  CHECK(x_grok_prompt(prompt, 40, "X", "where?", "English") == -1 && !prompt[0]);

  CHECK(!x_grok_request(url, sizeof url, body, sizeof body, X_GROK_HOST, "1890", X_GROK_MODEL, "ol\xc3\xa1 \"x\"\n") &&
        !strcmp(url, "https://grok.x.com/2/grok/add_response.json") &&
        !strncmp(body, "{\"responses\":[{\"message\":\"ol\xc3\xa1 \\\"x\\\"\\n\",\"sender\":1,", 46) &&
        strstr(body, "\"grokModelOptionId\":\"grok-3\",\"conversationId\":\"1890\",") &&
        strstr(body, "\"isReasoning\":false}"));
  CHECK((doc=parse(body, &off)) != NULL); // it is JSON
  json_free(doc);
  CHECK(!x_grok_request(url, sizeof url, body, sizeof body, X_GROK_HOST_OLD, "1890", NULL, "hi") &&
        !strcmp(url, "https://api.x.com/2/grok/add_response.json") && !strstr(body, "grokModelOptionId") &&
        strstr(body, "\"systemPromptName\":\"\",\"conversationId\":\"1890\""));
  CHECK(x_grok_request(url, sizeof url, body, sizeof body, X_GROK_HOST, "", NULL, "hi") == -1);
  CHECK(x_grok_request(url, sizeof url, body, sizeof body, X_GROK_HOST, "1/2", NULL, "hi") == -1);
  CHECK(x_grok_request(url, sizeof url, body, sizeof body, X_GROK_HOST, "1890", NULL, "") == -1);
  CHECK(x_grok_request(url, sizeof url, body, sizeof body, "evil.com/x", "1890", NULL, "hi") == -1);
  CHECK(x_grok_request(url, sizeof url, body, 100, X_GROK_HOST, "1890", NULL, "hi") == -1);

  {
    char copy[sizeof answer], small[12], cut[28];
    memcpy(copy, answer, sizeof answer);
    CHECK(x_grok_answer(copy, strlen(copy), out, sizeof out, &x_code) == 2 &&
          !strcmp(out, "Look behind the waterfall \xc3\xa9.") && x_code == -1);
    memcpy(copy, answer, sizeof answer);
    // cut where it is full, and at a whole character
    CHECK(x_grok_answer(copy, strlen(copy), small, sizeof small, &x_code) == 1 && !strcmp(small, "Look behind"));
    memcpy(copy, answer, sizeof answer);
    CHECK(x_grok_answer(copy, strlen(copy), cut, sizeof cut, &x_code) == 2 && !strcmp(cut, "Look behind the waterfall "));
    CHECK(x_grok_answer(errors, strlen(errors), out, sizeof out, &x_code) == 0 && !out[0] && x_code == 88);
    CHECK(x_grok_answer(copy, 0, out, sizeof out, &x_code) == 0 && !out[0]);
  }
}


int
main(int argc, char **argv) {
  if(argc != 3) {
    fprintf(stderr, "usage: %s tests/timeline.json tests/timeline_rich.json\n", argv[0]);
    return 2;
  }
  test_json();
  test_session();
  test_url();
  test_timeline(argv[1]);
  test_rich(argv[2]);
  test_errors();
  test_time();
  test_session_file();
  test_request();
  test_actions();
  test_feeds();
  test_notices();
  test_profile();
  test_search();
  test_dm();
  test_social();
  test_explore();
  test_dm_read();
  test_schedule();
  test_tls();
  test_grok();
  test_playing();
  test_damaged(argv[1]);
  test_damaged(argv[2]);

  printf(g_failed ? "%d checks failed\n" : "all checks passed\n", g_failed);
  return g_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
