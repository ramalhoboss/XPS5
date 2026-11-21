/* Host tests for src/xchat.c: X Chat's inbox read from an answer whose
events are base64 of Thrift's binary protocol, as x.com's Chat gets them,
and the toasts of its new messages (src/xnotify.c). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "xchat.h"
#include "xnotify.h"

static int g_failed;

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_failed++; } } while(0)

#define T0 1794000000LL // 2026-11-06 21:20 UTC
#define ME "111"


/*
 * Thrift's binary protocol, written
 */

typedef struct writer {
  unsigned char b[4096];
  size_t n;
} writer_t;

static void
w8(writer_t *w, unsigned v) {
  w->b[w->n++] = (unsigned char)v;
}

static void
w16(writer_t *w, unsigned v) {
  w8(w, v >> 8);
  w8(w, v);
}

static void
w32(writer_t *w, unsigned long v) {
  w16(w, (unsigned)(v >> 16));
  w16(w, (unsigned)v);
}

static void
field(writer_t *w, int type, int id) {
  w8(w, (unsigned)type);
  w16(w, (unsigned)id);
}

static void
bytes(writer_t *w, int id, const void *p, size_t n) {
  field(w, 11, id);
  w32(w, n);
  memcpy(w->b + w->n, p, n);
  w->n += n;
}

static void
str(writer_t *w, int id, const char *s) {
  bytes(w, id, s, strlen(s));
}

static void
b64(const writer_t *w, char *out) {
  static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t i, n = 0;

  for(i=0; i + 2 < w->n; i += 3) {
    unsigned v = (unsigned)w->b[i] << 16 | (unsigned)w->b[i + 1] << 8 | w->b[i + 2];
    out[n++] = abc[v >> 18];
    out[n++] = abc[(v >> 12) & 63];
    out[n++] = abc[(v >> 6) & 63];
    out[n++] = abc[v & 63];
  }
  if(w->n - i == 1) {
    unsigned v = (unsigned)w->b[i] << 16;
    out[n++] = abc[v >> 18];
    out[n++] = abc[(v >> 12) & 63];
    out[n++] = '=';
    out[n++] = '=';
  } else if(w->n - i == 2) {
    unsigned v = (unsigned)w->b[i] << 16 | (unsigned)w->b[i + 1] << 8;
    out[n++] = abc[v >> 18];
    out[n++] = abc[(v >> 12) & 63];
    out[n++] = abc[(v >> 6) & 63];
    out[n++] = '=';
  }
  out[n] = 0;
}


/**
 * a MessageEvent of a new message: encrypted with key version 1 when text
 * is NULL, else sent unencrypted with those words
 **/
static void
message(char *out, const char *seq, const char *sender, long long sec, const char *text) {
  writer_t w = {{0}, 0}, in = {{0}, 0};
  char msec[32];

  if(text) {
    field(&in, 12, 1);  // MessageEntryHolder.contents
    field(&in, 12, 1);  // MessageEntryContents.message
    str(&in, 1, text);  // MessageContents.message_text
    w8(&in, 0);         // end MessageContents
    w8(&in, 0);         // end MessageEntryContents
    w8(&in, 0);         // end MessageEntryHolder
  } else {
    memcpy(in.b, "\x13\x37\xc0\xde not thrift", 15);
    in.n = 15;
  }
  snprintf(msec, sizeof msec, "%lld", sec * 1000 + 250);
  str(&w, 1, seq);
  str(&w, 2, "msg");
  str(&w, 3, sender);
  str(&w, 4, "conv");
  str(&w, 5, "token");
  str(&w, 6, msec);
  field(&w, 12, 7);       // the detail, a union
  field(&w, 12, 1);       // messageCreateEvent
  bytes(&w, 100, in.b, in.n);
  if(!text) {
    str(&w, 101, "1");
  }
  field(&w, 2, 102);      // should_notify
  w8(&w, 1);
  w8(&w, 0);
  w8(&w, 0);
  field(&w, 8, 8);        // relay_source, skipped
  w32(&w, 3);
  field(&w, 15, 20);      // a list it does not know, skipped
  w8(&w, 11);
  w32(&w, 1);
  w32(&w, 2);
  w8(&w, 'h');
  w8(&w, 'i');
  w8(&w, 0);
  b64(&w, out);
}


/**
 * a MessageEvent of reader having read up to seen
 **/
static void
read_up(char *out, const char *seq, const char *reader, const char *seen) {
  writer_t w = {{0}, 0};

  str(&w, 1, seq);
  str(&w, 3, reader);
  str(&w, 6, "1794000000000");
  field(&w, 12, 7);
  field(&w, 12, 12);      // markConversationReadEvent
  str(&w, 1, seen);
  field(&w, 10, 2);       // seen_at_millis
  w32(&w, 0);
  w32(&w, 12345);
  w8(&w, 0);
  w8(&w, 0);
  w8(&w, 0);
  b64(&w, out);
}


static json_doc_t *
inbox(char *json, size_t size, const char *c3_message) {
  static char m1[2048], m1_old[2048], r1[1024], r1_mine[1024], m2[2048], r2[1024], m3[2048], m4[2048];
  size_t off;

  message(m1, "105", "222", T0, NULL);
  message(m1_old, "104", ME, T0 - 600, "j\303\241 te ligo");
  read_up(r1, "106", "222", "105");
  read_up(r1_mine, "103", ME, "104");
  message(m2, "50", "333", T0 - 100, "ol\303\241 \"tu\"");
  read_up(r2, "51", ME, "50");
  message(m4, "70", ME, T0 - 200, "at\303\251 j\303\241");
  snprintf(m3, sizeof m3, "%s", c3_message);
  snprintf(json, size,
    "{\"data\":{\"get_initial_chat_page\":{\"__typename\":\"XChatGetInboxPageResponse\",\"items\":["
    // João, one to one: his message, encrypted, after where the user read
    "{\"conversation_detail\":{\"conversation_id\":\"111:222\",\"participants_results\":["
    "{\"rest_id\":\"111\",\"result\":{\"rest_id\":\"111\",\"core\":{\"name\":\"Eu\",\"screen_name\":\"eu\"}}},"
    "{\"rest_id\":\"222\",\"result\":{\"rest_id\":\"222\",\"core\":{\"name\":\"Jo\303\243o\",\"screen_name\":\"joao\"},"
    "\"avatar\":{\"image_url\":\"https://pbs.twimg.com/profile_images/3/joao_normal.jpg\"},"
    "\"verification\":{\"is_blue_verified\":true}}}],\"group_metadata\":null},"
    "\"latest_notifiable_message_create_event\":\"%s\",\"latest_message_events\":[\"%s\",\"%s\"],"
    "\"latest_read_events_per_participant\":[{\"participant_id\":{\"rest_id\":\"111\"},"
    "\"latest_mark_conversation_read_event\":\"%s\"}]},"
    // Rita: a message sent unencrypted, read
    "{\"conversation_detail\":{\"conversation_id\":\"111:333\",\"participants_results\":["
    "{\"rest_id\":\"333\",\"result\":{\"rest_id\":\"333\",\"core\":{\"name\":\"Rita\",\"screen_name\":\"rita\"}}},"
    "{\"rest_id\":\"111\",\"result\":{\"rest_id\":\"111\",\"core\":{\"name\":\"Eu\",\"screen_name\":\"eu\"}}}]},"
    "\"latest_message_events\":[\"%s\",\"%s\"]},"
    // a group: Ana wrote, nobody read
    "{\"conversation_detail\":{\"conversation_id\":\"g900\",\"participants_results\":["
    "{\"rest_id\":\"111\",\"result\":{\"rest_id\":\"111\",\"core\":{\"name\":\"Eu\",\"screen_name\":\"eu\"}}},"
    "{\"rest_id\":\"444\",\"result\":{\"rest_id\":\"444\",\"core\":{\"name\":\"Ana Costa\",\"screen_name\":\"ana\"},"
    "\"avatar\":{\"image_url\":\"https://pbs.twimg.com/profile_images/4/ana_normal.jpg\"}}},"
    "{\"rest_id\":\"555\",\"result\":{\"rest_id\":\"555\",\"core\":{\"name\":\"Hugo Reis\",\"screen_name\":\"hugo\"}}}],"
    "\"group_metadata\":{\"group_name\":\"1:c2VhbGVk\",\"group_avatar_url\":\"https://pbs.twimg.com/group/9.jpg\"}},"
    "\"latest_message_events\":[\"%s\"]},"
    // Pedro: the user wrote last
    "{\"conversation_detail\":{\"conversation_id\":\"111:666\",\"participants_results\":["
    "{\"rest_id\":\"666\",\"result\":{\"rest_id\":\"666\",\"core\":{\"name\":\"Pedro\",\"screen_name\":\"pedro\"}}}]},"
    "\"latest_message_events\":[\"%s\"]},"
    // one whose events do not decode, and one with no number
    "{\"conversation_detail\":{\"conversation_id\":\"111:777\",\"participants_results\":[]},"
    "\"latest_notifiable_message_create_event\":\"AAAA\",\"latest_message_events\":[\"!!\",\"CwAB\"]},"
    "{\"conversation_detail\":{}}"
    "],\"inboxCursor\":{\"pull_finished\":true}}}}",
    m1, m1_old, r1, r1_mine, m2, r2, m3, m4);
  return json_parse(json, strlen(json), &off);
}


static void
test_inbox(void) {
  static char json[32768], m3[2048];
  static x_post_t posts[8];
  static xnotify_t n;
  xnotify_toast_t t;
  json_doc_t *doc;
  int count, fresh[3];

  message(m3, "900", "444", T0 - 50, NULL);
  doc = inbox(json, sizeof json, m3);
  CHECK(doc != NULL);
  if(!doc) {
    return;
  }
  CHECK(xchat_inbox(json_root(doc), ME, NULL, 0) == 5);
  count = xchat_inbox(json_root(doc), ME, posts, 8);
  CHECK(count == 5);

  // newest first: João, the group, Rita, Pedro, the one that did not decode
  CHECK(!strcmp(posts[0].id, "111:222") && !strcmp(posts[1].id, "g900") && !strcmp(posts[2].id, "111:333"));
  CHECK(!strcmp(posts[3].id, "111:666") && !strcmp(posts[4].id, "111:777") && posts[4].time == 0);

  // João's: who, when, encrypted and unread
  CHECK(posts[0].kind == X_KIND_CHAT && posts[0].xchat && posts[0].sealed && !posts[0].text[0]);
  CHECK(posts[0].encrypted && posts[1].encrypted && !posts[2].encrypted);
  CHECK(!strcmp(posts[0].user, "joao") && !strcmp(posts[0].name, "Jo\303\243o") && !strcmp(posts[0].user_id, "222"));
  CHECK(!strcmp(posts[0].avatar, "https://pbs.twimg.com/profile_images/3/joao_normal.jpg"));
  CHECK(posts[0].badge == X_BADGE_BLUE && posts[0].time == T0 && posts[0].unread && !posts[0].mine);
  CHECK(!posts[0].group && posts[0].replies == -1);

  // the group: its members' names and picture, who wrote
  CHECK(posts[1].group && !strcmp(posts[1].name, "Ana Costa, Hugo Reis") && !posts[1].user[0]);
  CHECK(!strcmp(posts[1].avatar, "https://pbs.twimg.com/group/9.jpg") && posts[1].face_count == 1);
  CHECK(!strcmp(posts[1].quote_name, "Ana Costa") && posts[1].unread && posts[1].sealed);

  // Rita's, unencrypted and read; Pedro's, the user's own
  CHECK(!posts[2].sealed && !strcmp(posts[2].text, "ol\303\241 \"tu\"") && !posts[2].unread);
  CHECK(posts[3].mine && !posts[3].unread && !strcmp(posts[3].text, "at\303\251 j\303\241"));

  // the notices: the first look only remembers; then the new one in Rita's
  CHECK(xnotify_dm_fresh(&n, posts, &count, fresh, 3) == 0 && count == 2 && n.primed);
  json_free(doc);
  message(m3, "900", "444", T0 - 50, NULL);
  doc = inbox(json, sizeof json, m3);
  count = xchat_inbox(json_root(doc), ME, posts, 8);
  CHECK(xnotify_dm_fresh(&n, posts, &count, fresh, 3) == 0);
  json_free(doc);
  message(m3, "901", "555", T0 + 30, NULL);
  doc = inbox(json, sizeof json, m3);
  count = xchat_inbox(json_root(doc), ME, posts, 8);
  CHECK(xnotify_dm_fresh(&n, posts, &count, fresh, 3) == 1 && !strcmp(posts[fresh[0]].id, "g900"));
  CHECK(!strcmp(posts[fresh[0]].quote_name, "Hugo Reis"));

  // their toasts: who, and that the words are encrypted
  xnotify_dm_toast(&posts[fresh[0]], 0, &t);
  CHECK(!strcmp(t.title, "Ana Costa, Hugo Reis") && !strcmp(t.body, "Hugo Reis: Encrypted message"));
  CHECK(!strcmp(t.extra, "X Chat \302\267 XPS5"));
  for(int i=0; i < count; i++) {
    if(!strcmp(posts[i].id, "111:222")) {
      xnotify_dm_toast(&posts[i], 1, &t);
      CHECK(!strcmp(t.title, "Jo\303\243o (@joao)") && !strcmp(t.body, "Enviou-te uma mensagem cifrada"));
      CHECK(!strcmp(t.avatar, "https://pbs.twimg.com/profile_images/3/joao_200x200.jpg"));
    }
  }
  json_free(doc);
}


static void
chat(x_post_t *p, const char *id, const char *user_id, long long time, int xchat, int sealed) {
  memset(p, 0, sizeof *p);
  p->kind = X_KIND_CHAT;
  snprintf(p->id, sizeof p->id, "%s", id);
  snprintf(p->user_id, sizeof p->user_id, "%s", user_id);
  p->time = time;
  p->xchat = xchat;
  p->sealed = sealed;
  p->group = !user_id[0];
}


static void
test_merge(void) {
  static x_post_t all[7];

  // the inbox's: João's, with a photo, and a group
  chat(&all[0], "111-222", "222", T0 - 1000, 0, 0);
  snprintf(all[0].text, sizeof all[0].text, "%s", "foto");
  all[0].icon = X_DM_PHOTO;
  chat(&all[1], "999", "", T0 - 10, 0, 0);
  snprintf(all[1].text, sizeof all[1].text, "%s", "ol\303\241");
  // X Chat's: João again, newer and encrypted; the group, older; Rita's,
  // only there; Pedro's, written the other way round and only there
  chat(&all[2], "222:111", "222", T0, 1, 1);
  all[2].unread = 1;
  chat(&all[3], "999", "", T0 - 20, 1, 1);
  chat(&all[4], "111:333", "333", T0 - 5, 1, 1);
  chat(&all[5], "666:111", "666", T0 - 7, 1, 0);
  CHECK(xchat_merge(all, 2, 4) == 4);
  CHECK(!strcmp(all[0].id, "111-222") && !all[0].xchat && all[0].sealed && all[0].unread);
  CHECK(all[0].time == T0 && !all[0].text[0] && all[0].icon == X_DM_TEXT);
  CHECK(!strcmp(all[1].id, "999") && !all[1].sealed && all[1].time == T0 - 10 && !strcmp(all[1].text, "ol\303\241"));
  CHECK(!strcmp(all[2].id, "111:333") && all[2].xchat && !strcmp(all[3].id, "666:111"));
  CHECK(!all[4].id[0] && !all[5].id[0]);

  // the same number, X Chat's way
  chat(&all[0], "111-444", "", T0 - 100, 0, 0);
  chat(&all[1], "111:444", "", T0, 1, 1);
  CHECK(xchat_merge(all, 1, 1) == 1 && all[0].sealed && all[0].time == T0);
  // nothing of the inbox's: all of X Chat's
  CHECK(xchat_merge(all, 0, 2) == 2);
}


static void
test_newer(void) {
  static x_post_t p[3];
  long long newest = 0;

  p[0].time = T0;
  p[1].time = T0 - 60;
  p[2].time = T0 + 5;
  CHECK(xnotify_newer(p, 3, T0 - 1, &newest) == 2 && newest == T0 + 5);
  CHECK(xnotify_newer(p, 3, T0 + 5, &newest) == 0 && newest == T0 + 5);
  CHECK(xnotify_newer(p, 0, 0, NULL) == 0 && xnotify_newer(p, 3, 0, NULL) == 3);
}


static void
test_answers(void) {
  static const char *const bad[] = {
    "{\"errors\":[{\"message\":\"Query not found\"}]}", "{\"data\":{}}", "{\"data\":{\"get_initial_chat_page\":null}}",
  };
  char text[256];
  json_doc_t *doc;
  size_t off;

  // json_parse works in the text it is given
  for(size_t i=0; i < sizeof bad / sizeof *bad; i++) {
    snprintf(text, sizeof text, "%s", bad[i]);
    doc = json_parse(text, strlen(text), &off);
    CHECK(doc && xchat_inbox(json_root(doc), ME, NULL, 0) == -1);
    json_free(doc);
  }
  snprintf(text, sizeof text, "%s", "{\"data\":{\"get_inbox_page\":{\"items\":[]}}}");
  doc = json_parse(text, strlen(text), &off);
  CHECK(doc && xchat_inbox(json_root(doc), ME, NULL, 0) == 0);
  json_free(doc);
}


static void
test_request(void) {
  static const unsigned char ff[16] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  char url[1024], uuid[37];
  x_request_t r;
  https_header_t headers[XCHAT_HEADER_COUNT];
  int n;

  CHECK(!xchat_inbox_url(url, sizeof url));
  CHECK(!strncmp(url, "https://api.x.com/graphql/", 26) && strstr(url, "/GetInitialXChatPageQuery?variables=%7B"));
  CHECK(!strchr(url, '{') && !strchr(url, '"') && strstr(url, "message_pull_version"));
  CHECK(xchat_inbox_url(url, 40) == -1);

  xchat_uuid(ff, uuid);
  CHECK(!strcmp(uuid, "ffffffff-ffff-4fff-bfff-ffffffffffff"));

  memset(&r, 0, sizeof r);
  for(int i=0; i < X_HEADER_COUNT; i++) {
    r.headers[i] = (https_header_t){i == X_HEADER_COUNT - 1 ? "referer" : "x-header", "v"};
  }
  n = xchat_headers(&r, uuid, "tid", headers);
  CHECK(n == XCHAT_HEADER_COUNT && !strcmp(headers[X_HEADER_COUNT - 1].value, "https://x.com/i/chat"));
  CHECK(!strcmp(headers[X_HEADER_COUNT].name, "origin") && !strcmp(headers[X_HEADER_COUNT].value, "https://x.com"));
  CHECK(!strcmp(headers[X_HEADER_COUNT + 1].value, uuid) && !strcmp(headers[X_HEADER_COUNT + 2].value, "tid"));
  CHECK(xchat_headers(&r, NULL, NULL, headers) == X_HEADER_COUNT + 1);
}


int
main(void) {
  test_inbox();
  test_merge();
  test_newer();
  test_answers();
  test_request();
  if(g_failed) {
    printf("%d X Chat checks failed\n", g_failed);
    return 1;
  }
  printf("all X Chat checks passed\n");
  return 0;
}
