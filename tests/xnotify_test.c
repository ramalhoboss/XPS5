/* Host tests for src/xnotify.c: which notifications are new, and the JSON
of their PS5 toasts. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "xnotify.h"

static int g_failed;

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_failed++; } } while(0)

#define T0 1794000000LL // 2026-11-06 21:20 UTC


static void
notice(x_post_t *p, const char *text, const char *about, long long time, int icon) {
  memset(p, 0, sizeof *p);
  p->kind = X_KIND_NOTICE;
  snprintf(p->text, sizeof p->text, "%s", text);
  snprintf(p->quote_text, sizeof p->quote_text, "%s", about);
  snprintf(p->id, sizeof p->id, "2001");
  p->time = time;
  p->icon = icon;
  p->face_count = 1;
  snprintf(p->faces[0], sizeof p->faces[0], "https://pbs.twimg.com/profile_images/1/ana_normal.jpg");
}


static void
reply(x_post_t *p, const char *text, long long time) {
  memset(p, 0, sizeof *p);
  p->kind = X_KIND_POST;
  snprintf(p->id, sizeof p->id, "3001");
  snprintf(p->user, sizeof p->user, "rita");
  snprintf(p->name, sizeof p->name, "Rita Sousa");
  snprintf(p->text, sizeof p->text, "%s", text);
  snprintf(p->avatar, sizeof p->avatar, "https://pbs.twimg.com/profile_images/2/rita_normal.png");
  p->time = time;
}


static void
test_fresh(void) {
  static xnotify_t n;
  x_post_t posts[8];
  int fresh[3];

  // the first reading only remembers
  notice(&posts[0], "Ana Lopes liked your post", "a sala", T0 - 60, X_ICON_LIKE);
  reply(&posts[1], "@eu boa!", T0 - 600);
  CHECK(xnotify_fresh(&n, posts, 2, fresh, 3) == 0 && n.primed && n.newest == T0 - 60);
  CHECK(xnotify_fresh(&n, posts, 2, fresh, 3) == 0);

  // a reply on top, and the like that more people gave, are new
  posts[2] = posts[1];
  reply(&posts[0], "@eu e agora?", T0);
  notice(&posts[1], "Ana Lopes and Hugo Reis liked your post", "a sala", T0 - 30, X_ICON_LIKE);
  CHECK(xnotify_fresh(&n, posts, 3, fresh, 3) == 2 && fresh[0] == 0 && fresh[1] == 1);
  CHECK(n.newest == T0);
  CHECK(xnotify_fresh(&n, posts, 3, fresh, 3) == 0);

  // an older one the list shows again is not
  notice(&posts[3], "Hugo Reis followed you", "", T0 - 86400, X_ICON_FOLLOW);
  CHECK(xnotify_fresh(&n, posts, 4, fresh, 3) == 0);

  // more than fit: the count, and the newest ones
  for(int i=0; i<5; i++) {
    reply(&posts[i], "", T0 + 100 - i);
    snprintf(posts[i].text, sizeof posts[i].text, "mention %d", i);
  }
  CHECK(xnotify_fresh(&n, posts, 5, fresh, 3) == 5 && fresh[0] == 0 && fresh[2] == 2);

  // remembered past the ring's size
  for(int round=0; round < 40; round++) {
    for(int i=0; i<8; i++) {
      reply(&posts[i], "", T0 + 1000 + round * 8 + (8 - i));
      snprintf(posts[i].text, sizeof posts[i].text, "r%d-%d", round, i);
    }
    CHECK(xnotify_fresh(&n, posts, 8, fresh, 3) == 8);
  }
  CHECK(n.count == XNOTIFY_SEEN);
}


static void
test_toast(void) {
  static char long_text[1024];
  xnotify_toast_t t;
  x_post_t p;
  size_t len;

  notice(&p, "Ana Lopes liked your post", "A minha nova sala\nde jogos", T0, X_ICON_LIKE);
  xnotify_toast(&p, 1, &t);
  CHECK(!strcmp(t.title, "Ana Lopes liked your post"));
  CHECK(!strcmp(t.body, "A minha nova sala de jogos"));
  CHECK(!strcmp(t.avatar, "https://pbs.twimg.com/profile_images/1/ana_200x200.jpg"));
  CHECK(!strcmp(t.icon, "favorite"));
  CHECK(!strcmp(t.extra, "XPS5"));
  snprintf(p.user, sizeof p.user, "analopes");
  xnotify_toast(&p, 1, &t);
  CHECK(!strcmp(t.extra, "@analopes \xc2\xb7 XPS5"));
  notice(&p, "Hugo Reis followed you", "", T0, X_ICON_FOLLOW);
  p.face_count = 0;
  xnotify_toast(&p, 0, &t);
  CHECK(!strcmp(t.body, "XPS5") && !t.avatar[0] && !strcmp(t.icon, "ps_user"));

  reply(&p, "@eu Vamos jogar logo? \xf0\x9f\x8e\xae", T0);
  xnotify_toast(&p, 1, &t);
  CHECK(!strcmp(t.title, "Rita Sousa (@rita) escreveu-te"));
  CHECK(!strcmp(t.extra, "@rita \xc2\xb7 XPS5"));
  CHECK(!strcmp(t.body, "@eu Vamos jogar logo? \xf0\x9f\x8e\xae"));
  CHECK(!strcmp(t.avatar, "https://pbs.twimg.com/profile_images/2/rita_200x200.png"));
  xnotify_toast(&p, 0, &t);
  CHECK(!strcmp(t.title, "Rita Sousa (@rita) wrote to you"));

  // a long text is cut between characters, with an ellipsis
  for(size_t i=0; i + 2 < sizeof long_text; i += 2) {
    memcpy(long_text + i, "\xc3\xa0", 2);
  }
  reply(&p, long_text, T0);
  xnotify_toast(&p, 0, &t);
  len = strlen(t.body);
  CHECK(len < XNOTIFY_TEXT && len > XNOTIFY_TEXT - 8);
  CHECK(!strcmp(t.body + len - 3, "\xe2\x80\xa6") && !memcmp(t.body + len - 5, "\xc3\xa0", 2));
}


static void
test_json(void) {
  static char buf[XNOTIFY_JSON];
  const json_node_t *root, *view, *action;
  xnotify_toast_t t;
  json_doc_t *doc;
  size_t off;

  memset(&t, 0, sizeof t);
  snprintf(t.title, sizeof t.title, "Ana \"A\" Lopes\\ liked\x01 it");
  snprintf(t.body, sizeof t.body, "sala");
  snprintf(t.avatar, sizeof t.avatar, "https://pbs.twimg.com/profile_images/1/ana_200x200.jpg");
  CHECK(xnotify_json(buf, sizeof buf, &t, "Abrir", "psgm:play?id=TWPS00001", T0, 77) == 0);
  CHECK((doc=json_parse(buf, strlen(buf), &off)) != NULL);
  if(doc) {
    root = json_root(doc);
    view = json_path(root, "rawData", "viewData", NULL);
    action = json_get(view, "actions") ? json_get(view, "actions")->child : NULL;
    CHECK(json_string(json_path(root, "rawData", "viewTemplateType", NULL)) &&
          !strcmp(json_string(json_path(root, "rawData", "viewTemplateType", NULL)),
                  "InteractiveToastTemplateB"));
    CHECK(json_string(json_path(view, "message", "body", NULL)) &&
          !strcmp(json_string(json_path(view, "message", "body", NULL)), "Ana \"A\" Lopes\\ liked\x01 it"));
    CHECK(json_string(json_path(view, "subMessage", "body", NULL)) &&
          !strcmp(json_string(json_path(view, "subMessage", "body", NULL)), "sala"));
    CHECK(!json_get(view, "extraMessage")); // none without a third line
    CHECK(json_string(json_path(view, "icon", "type", NULL)) &&
          !strcmp(json_string(json_path(view, "icon", "type", NULL)), "Url"));
    CHECK(json_string(json_path(view, "icon", "parameters", "url", NULL)) &&
          !strcmp(json_string(json_path(view, "icon", "parameters", "url", NULL)), t.avatar));
    CHECK(action && json_string(json_path(action, "parameters", "actionUrl", NULL)) &&
          !strcmp(json_string(json_path(action, "parameters", "actionUrl", NULL)), "psgm:play?id=TWPS00001"));
    CHECK(action && !strcmp(json_string(json_get(action, "actionName")), "Abrir"));
    CHECK(json_string(json_get(root, "createdDateTime")) &&
          !strcmp(json_string(json_get(root, "createdDateTime")), "2026-11-06T21:20:00.000Z"));
    CHECK(json_string(json_get(root, "localNotificationId")) &&
          !strcmp(json_string(json_get(root, "localNotificationId")), "77"));
    CHECK(json_string(json_path(root, "rawData", "platformViews", "previewDisabled", "viewData", "message",
                                "body", NULL)) != NULL);
    json_free(doc);
  }

  // the third line, under the other two
  snprintf(t.extra, sizeof t.extra, "@ana \xc2\xb7 XPS5");
  CHECK(xnotify_json(buf, sizeof buf, &t, "Abrir", "psgm:play?id=TWPS00001", T0, 78) == 0);
  CHECK((doc=json_parse(buf, strlen(buf), &off)) != NULL);
  if(doc) {
    view = json_path(json_root(doc), "rawData", "viewData", NULL);
    CHECK(json_string(json_path(view, "extraMessage", "body", NULL)) &&
          !strcmp(json_string(json_path(view, "extraMessage", "body", NULL)), "@ana \xc2\xb7 XPS5"));
    CHECK(json_get(view, "actions") != NULL);
    json_free(doc);
  }
  t.extra[0] = 0;

  // the PS5's own icon, and no button
  t.avatar[0] = 0;
  t.icon = "favorite";
  CHECK(xnotify_json(buf, sizeof buf, &t, "Open", NULL, T0, 1) == 0);
  CHECK(!strstr(buf, "actions") && strstr(buf, "\"type\": \"Predefined\", \"parameters\": {\"icon\": \"favorite\"}"));
  CHECK((doc=json_parse(buf, strlen(buf), &off)) != NULL);
  json_free(doc);
  CHECK(xnotify_json(buf, 300, &t, "Open", NULL, T0, 1) == -1);
}


static void
own(x_post_t *p, const char *id, long long time, long long likes) {
  memset(p, 0, sizeof *p);
  p->kind = X_KIND_POST;
  snprintf(p->id, sizeof p->id, "%s", id);
  snprintf(p->user, sizeof p->user, "joao");
  snprintf(p->user_id, sizeof p->user_id, "42");
  snprintf(p->text, sizeof p->text, "post %s\nsecond line", id);
  snprintf(p->avatar, sizeof p->avatar, "https://pbs.twimg.com/profile_images/9/joao_normal.jpg");
  p->time = time;
  p->likes = likes;
  p->reposts = likes / 10;
  p->replies = likes / 20;
}


/**
 * the user's post rising, among theirs
 **/
static void
test_rising(void) {
  static x_post_t posts[8];
  xnotify_rising_t r;
  xnotify_toast_t t;

  memset(&r, 0, sizeof r);
  own(&posts[0], "501", T0 - 2 * 3600, 400);  // two hours old: rising
  own(&posts[1], "502", T0 - 30 * 3600, 900); // more liked, but over a day old
  own(&posts[2], "503", T0 - 3 * 86400, 30);
  own(&posts[3], "504", T0 - 5 * 86400, 20);
  own(&posts[4], "505", T0 - 6 * 86400, 25);
  own(&posts[5], "506", T0 - 3600, 5000);     // someone else's
  snprintf(posts[5].user_id, sizeof posts[5].user_id, "77");
  own(&posts[6], "507", T0 - 1800, 40);       // new, under 50 likes
  CHECK(xnotify_rising(&r, posts, 7, "42", T0) == 0);
  CHECK(xnotify_rising(&r, posts, 7, "42", T0) == -1); // said once
  CHECK(xnotify_rising(&r, posts, 7, "", T0) == -1);
  posts[6].likes = 600;                                 // now it rises too
  CHECK(xnotify_rising(&r, posts, 7, "42", T0) == 6);
  // a repost of the user's is not theirs to say
  memset(&r, 0, sizeof r);
  snprintf(posts[0].reposter, sizeof posts[0].reposter, "joao");
  posts[6].likes = 40;
  CHECK(xnotify_rising(&r, posts, 7, "42", T0) == -1);
  posts[0].reposter[0] = 0;

  // all of them alike: none stands out
  memset(&r, 0, sizeof r);
  for(int i=0; i<5; i++) {
    char id[8];
    snprintf(id, sizeof id, "60%d", i);
    own(&posts[i], id, T0 - (i + 1) * 3600, 100 * (i + 1));
  }
  CHECK(xnotify_rising(&r, posts, 5, "42", T0) == -1);

  own(&posts[0], "501", T0 - 2 * 3600, 400);
  xnotify_rising_toast(&posts[0], "Your post is rising \xc2\xb7 400 likes", &t);
  CHECK(!strcmp(t.title, "Your post is rising \xc2\xb7 400 likes") && !strcmp(t.body, "post 501 second line"));
  CHECK(!strcmp(t.extra, "XPS5") && !strcmp(t.icon, "favorite") &&
        !strcmp(t.avatar, "https://pbs.twimg.com/profile_images/9/joao_200x200.jpg"));
}


int
main(void) {
  test_fresh();
  test_toast();
  test_json();
  test_rising();
  if(g_failed) {
    printf("%d notification checks failed\n", g_failed);
    return 1;
  }
  printf("all notification checks passed\n");
  return 0;
}
