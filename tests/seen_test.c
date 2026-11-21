/* Host tests of src/seen.c: the people the user follows as XPS5 saw them
around, their file, and the changes between two looks. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seen.h"

static int g_failed;

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_failed++; } } while(0)

#define T0 1700000000LL


static void
person(x_post_t *p, const char *id, const char *name, const char *game, int online) {
  memset(p, 0, sizeof *p);
  p->kind = X_KIND_USER;
  snprintf(p->user_id, sizeof p->user_id, "%s", id);
  snprintf(p->name, sizeof p->name, "%s", name);
  snprintf(p->user, sizeof p->user, "%s", name);
  snprintf(p->avatar, sizeof p->avatar, "https://pbs.twimg.com/profile_images/%s/a_normal.jpg", id);
  snprintf(p->playing, sizeof p->playing, "%s", game ? game : "");
  p->online = !game && online;
}


static void
test_note(void) {
  static seen_t s;
  static x_post_t look[4];
  const seen_person_t *p;

  memset(&s, 0, sizeof s);
  // the first look only notes: nobody's change is known
  person(&look[0], "11", "Ana", "ASTRO BOT", 0);
  person(&look[1], "22", "Rui", NULL, 1);
  person(&look[2], "33", "Eva", NULL, 0);
  CHECK(seen_note(&s, look, 3, T0) == 0 && s.count == 3 && !s.events && s.looked == T0);
  CHECK((p=seen_find(&s, "11")) && p->state == SEEN_PLAYING && p->at == T0 && !strcmp(p->game, "ASTRO BOT") &&
        !strcmp(p->games[0], "ASTRO BOT"));
  CHECK((p=seen_find(&s, "22")) && p->state == SEEN_ONLINE && p->at == T0 && !p->game[0] && !p->games[0][0]);
  CHECK((p=seen_find(&s, "33")) && p->state == SEEN_AWAY && !p->at);
  CHECK(!seen_find(&s, "44") && !seen_find(&s, "") && !seen_find(&s, NULL));

  // five minutes on: Eva opens XPS5, Ana changes game, Rui still around
  person(&look[0], "11", "Ana", "Gran Turismo 7", 0);
  person(&look[2], "33", "Eva", NULL, 1);
  CHECK(seen_note(&s, look, 3, T0 + 300) == 2 && s.events == 2);
  CHECK(!strcmp(s.event[0].id, "33") && !s.event[0].game[0] && s.event[0].at == T0 + 300 &&
        !strcmp(s.event[0].name, "Eva") && !strncmp(s.event[0].avatar, "https://pbs.twimg.com/", 22));
  CHECK(!strcmp(s.event[1].id, "11") && !strcmp(s.event[1].game, "Gran Turismo 7"));
  CHECK((p=seen_find(&s, "11")) && !strcmp(p->games[0], "Gran Turismo 7") && !strcmp(p->games[1], "ASTRO BOT"));
  CHECK((p=seen_find(&s, "22")) && p->at == T0 + 300);

  // Ana closes the game for XPS5, Rui goes: neither is news; Rui keeps when he was seen
  person(&look[0], "11", "Ana", NULL, 1);
  person(&look[1], "22", "Rui", NULL, 0);
  CHECK(seen_note(&s, look, 3, T0 + 600) == 0 && s.events == 2);
  CHECK((p=seen_find(&s, "22")) && p->state == SEEN_AWAY && p->at == T0 + 300);
  CHECK((p=seen_find(&s, "11")) && p->state == SEEN_ONLINE && !p->game[0] && !strcmp(p->games[0], "Gran Turismo 7"));

  // back to the first game: it moves to the front, not twice
  person(&look[0], "11", "Ana", "ASTRO BOT", 0);
  CHECK(seen_note(&s, look, 3, T0 + 900) == 1);
  CHECK((p=seen_find(&s, "11")) && !strcmp(p->games[0], "ASTRO BOT") && !strcmp(p->games[1], "Gran Turismo 7") &&
        !p->games[2][0]);

  // hours later the look before is too old to tell what changed
  person(&look[1], "22", "Rui", "Helldivers 2", 0);
  CHECK(seen_note(&s, look, 3, T0 + 900 + 4 * 3600) == 0);
  CHECK((p=seen_find(&s, "22")) && p->state == SEEN_PLAYING && !strcmp(p->games[0], "Helldivers 2"));

  // someone not in a look is not around, and back in the next not news (the
  // look may have left them out); one listed twice counts once
  CHECK(seen_note(&s, look, 1, T0 + 5 * 3600) == 0);
  CHECK((p=seen_find(&s, "22")) && p->state == SEEN_AWAY);
  person(&look[1], "22", "Rui", NULL, 1);
  person(&look[3], "22", "Rui", NULL, 1);
  CHECK(seen_note(&s, look, 4, T0 + 5 * 3600 + 60) == 0 && s.count == 3);
  person(&look[1], "22", "Rui", "Doom", 0);
  look[3] = look[1];
  CHECK(seen_note(&s, look, 4, T0 + 5 * 3600 + 120) == 1 && s.count == 3 && !strcmp(s.event[0].game, "Doom"));

  // not people, and no time: nothing
  look[0].kind = X_KIND_POST;
  CHECK(seen_note(&s, look, 1, T0 + 6 * 3600) == 0);
  CHECK(seen_note(&s, look, 1, 0) == 0);
}


static void
test_full(void) {
  static seen_t s;
  static x_post_t many[SEEN_PEOPLE + 10];
  char id[24];

  memset(&s, 0, sizeof s);
  for(int i=0; i < SEEN_PEOPLE; i++) {
    snprintf(id, sizeof id, "%d", 1000 + i);
    person(&many[i], id, "x", i == 5 ? "Tetris" : NULL, 0);
  }
  CHECK(seen_note(&s, many, SEEN_PEOPLE, T0) == 0 && s.count == SEEN_PEOPLE);
  // a full file makes room with the ones never seen, and keeps who played
  for(int i=0; i < 10; i++) {
    snprintf(id, sizeof id, "%d", 5000 + i);
    person(&many[i], id, "y", NULL, 1);
  }
  CHECK(seen_note(&s, many, 10, T0 + 60) == 0 && s.count == SEEN_PEOPLE);
  CHECK(seen_find(&s, "5009") && seen_find(&s, "1005") && seen_find(&s, "5000"));
  // more changes than are kept: the newest stay
  memset(&s, 0, sizeof s);
  for(int i=0; i < SEEN_EVENTS + 6; i++) {
    snprintf(id, sizeof id, "%d", 7000 + i);
    person(&many[i], id, "z", NULL, 0);
  }
  seen_note(&s, many, SEEN_EVENTS + 6, T0);
  for(int i=0; i < SEEN_EVENTS + 6; i++) {
    many[i].playing[0] = 'G';
    many[i].playing[1] = 0;
  }
  CHECK(seen_note(&s, many, SEEN_EVENTS + 6, T0 + 60) == SEEN_EVENTS + 6 && s.events == SEEN_EVENTS);
  CHECK(!strcmp(s.event[0].id, "7029") && !strcmp(s.event[SEEN_EVENTS - 1].id, "7006"));
}


static void
test_file(void) {
  static seen_t s, back;
  static x_post_t look[2];
  static char buf[SEEN_MOST];
  long long n;
  const seen_person_t *p;
  const char *odd =
    "l\t1700000600\n"
    "p\t11\t1700000600\t1\t1700000600\tASTRO BOT\tASTRO BOT\tGT7\t\n"
    "p\t0123\t1\t1\t1\t\t\t\t\n"     // not an account's number
    "p\t12\tsoon\t1\t1\t\t\t\t\n"   // not a time
    "p\t13\t5\t7\t1\t\t\t\t\n"      // not a state
    "p\t14\t5\t1\t\t\t\t\t\n"       // no look
    "p\t11\t9\t0\t9\t\t\t\t\n"      // twice
    "e\t1700000300\t11\tGT7\tAna\tana\thttps://evil.example/a.jpg\r\n"
    "x\twhat\n"
    "\n"
    "e\t1700000200\t22\t\tRui\trui\thttps://pbs.twimg.com/profile_images/2/r_normal.jpg";

  seen_parse(&s, odd, strlen(odd));
  CHECK(s.looked == 1700000600LL && s.count == 1 && s.events == 2);
  CHECK((p=seen_find(&s, "11")) && p->state == SEEN_PLAYING && !strcmp(p->games[1], "GT7") && !p->games[2][0]);
  CHECK(!s.event[0].avatar[0] && !strcmp(s.event[0].user, "ana")); // a picture from elsewhere is left out
  CHECK(!strcmp(s.event[1].avatar, "https://pbs.twimg.com/profile_images/2/r_normal.jpg") && !s.event[1].game[0]);

  // what it writes it reads back, tabs and line ends in names made spaces
  person(&look[0], "31", "Tab\tName\nTwo", "Game\twith tab", 0);
  person(&look[1], "32", "Ze", NULL, 1);
  seen_note(&s, look, 2, 1700000700LL);
  person(&look[1], "32", "Ze", "Doom", 0);
  seen_note(&s, look, 2, 1700000760LL);
  CHECK((n=seen_format(&s, buf, sizeof buf)) > 0 && (size_t)n == strlen(buf));
  seen_parse(&back, buf, (size_t)n);
  CHECK(back.looked == s.looked && back.count == s.count && back.events == s.events);
  CHECK((p=seen_find(&back, "31")) && !strcmp(p->game, "Game with tab") && p->state == SEEN_PLAYING);
  CHECK(!strcmp(back.event[0].id, "32") && !strcmp(back.event[0].game, "Doom") && back.event[0].at == 1700000760LL);
  // too small a buffer: -1 and nothing
  CHECK(seen_format(&s, buf, 20) == -1 && !buf[0]);
  CHECK(seen_format(&s, buf, 0) == -1);
  seen_parse(&back, NULL, 0);
  CHECK(!back.count && !back.events && !back.looked);
  // the account's file
  CHECK(!seen_path(1, buf, sizeof buf) && !strcmp(buf, "/data/TwitterPS5/seen1.txt"));
  CHECK(!seen_path(5, buf, sizeof buf) && seen_path(0, buf, sizeof buf) == -1 && seen_path(6, buf, sizeof buf) == -1);
  CHECK(seen_path(2, buf, 8) == -1);
}


int
main(void) {
  test_note();
  test_full();
  test_file();
  if(g_failed) {
    printf("seen: %d failed\n", g_failed);
    return 1;
  }
  printf("seen: all passed\n");
  return 0;
}
