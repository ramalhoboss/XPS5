/* Host tests for src/sessions.c: a session's line and its reading back, the
week's sums, the file kept without its old sessions, and the durations.
Run with tests/host-test.sh. */

#include <stdio.h>
#include <string.h>

#include "sessions.h"

static int g_failed;

#define CHECK(cond) do { \
  if(!(cond)) { \
    printf("%s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_failed++; \
  } \
} while(0)


static session_t
make(const char *id, const char *name, long long start, long long end, int b, int g, int shots) {
  session_t s;

  memset(&s, 0, sizeof s);
  snprintf(s.id, sizeof s.id, "%s", id);
  snprintf(s.name, sizeof s.name, "%s", name);
  s.start = start;
  s.end = end;
  s.trophies[0] = b;
  s.trophies[2] = g;
  s.shots = shots;
  return s;
}


static void
test_line(void) {
  session_t s = make("PPSA07399", "ASTRO\tBOT\n", 1000, 9000, 3, 1, 12), back;
  char line[SESSION_LINE], small[16];

  CHECK(!session_line(&s, line, sizeof line));
  CHECK(!strcmp(line, "1000\t9000\tPPSA07399\t3,0,1,0\t12\tASTRO BOT \n"));
  CHECK(session_line(&s, small, sizeof small) == -1);
  CHECK(!session_parse(line, &back) && back.start == 1000 && back.end == 9000 && !strcmp(back.id, "PPSA07399") &&
        back.trophies[0] == 3 && back.trophies[2] == 1 && back.shots == 12 && !strcmp(back.name, "ASTRO BOT "));
  CHECK(session_parse("1000\t900\tPPSA07399\t0,0,0,0\t0\tx", &back) == -1);   // it ends before it starts
  CHECK(session_parse("1000\t2000\t\t0,0,0,0\t0\tx", &back) == -1);           // no title id
  CHECK(session_parse("1000\t2000\tPPSA07399\t0,-1,0,0\t0\tx", &back) == -1); // a count below 0
  CHECK(session_parse("garbage", &back) == -1);
  CHECK(!session_parse("1\t2\tCUSA00001\t0,0,0,1\t0\t", &back) && !back.name[0] && back.trophies[3] == 1);

  // text on one line, cut at a whole character, in place too
  session_text("a\tb\x7f" "c", small, sizeof small);
  CHECK(!strcmp(small, "a b c"));
  session_text("caf\xc3\xa9", small, 5);
  CHECK(!strcmp(small, "caf"));
  strcpy(small, "x\ty");
  session_text(small, small, sizeof small);
  CHECK(!strcmp(small, "x y"));
}


static void
test_week(void) {
  const char *file =
    "100\t3700\tPPSA07399\t2,0,0,0\t3\tASTRO BOT\n"
    "broken line\n"
    "5000\t12200\tPPSA01325\t1,1,0,1\t1\tGran Turismo 7\n"
    "20000\t21800\tPPSA07399\t0,0,1,0\t0\tASTRO BOT 2\n"
    "90000\t99000\tPPSA00001\t0,0,0,0\t0\tLater"; // no newline at the end
  week_t w;

  memset(&w, 0, sizeof w);
  CHECK(week_add_file(&w, file, strlen(file), 3700, 90000) == 3); // from the first one's end, the last out
  week_sort(&w);
  CHECK(w.sessions == 3 && w.seconds == 3600 + 7200 + 1800 && w.shots == 4);
  CHECK(w.trophies[0] == 3 && w.trophies[1] == 1 && w.trophies[2] == 1 && w.trophies[3] == 1);
  CHECK(w.count == 2);
  CHECK(!strcmp(w.games[0].id, "PPSA01325") && w.games[0].seconds == 7200 && w.games[0].trophies == 3);
  CHECK(!strcmp(w.games[1].id, "PPSA07399") && w.games[1].seconds == 5400 && w.games[1].trophies == 3 &&
        !strcmp(w.games[1].name, "ASTRO BOT 2"));

  // more games than it keeps apart: the totals still count them
  memset(&w, 0, sizeof w);
  for(int i=0; i < WEEK_GAMES + 3; i++) {
    char id[16];
    session_t s;
    snprintf(id, sizeof id, "PPSA%05d", i);
    s = make(id, "g", 0, 60 * (i + 1), 0, 0, 0);
    week_add(&w, &s);
  }
  CHECK(w.count == WEEK_GAMES && w.sessions == WEEK_GAMES + 3);
}


static void
test_kept(void) {
  const char *file = "100\t200\tPPSA00001\t0,0,0,0\t0\told\n"
                     "x\n"
                     "5000\t6000\tPPSA00002\t0,0,0,0\t0\tkept\n";
  session_t s = make("PPSA00003", "new", 7000, 8000, 0, 0, 0);
  char out[512], tiny[60];
  long long n;

  n = sessions_kept(file, strlen(file), 1000, &s, out, sizeof out);
  CHECK(n == (long long)strlen(out));
  CHECK(!strcmp(out, "5000\t6000\tPPSA00002\t0,0,0,0\t0\tkept\n7000\t8000\tPPSA00003\t0,0,0,0\t0\tnew\n"));
  // a new file
  CHECK(sessions_kept(NULL, 0, 0, &s, out, sizeof out) > 0 && !strcmp(out, "7000\t8000\tPPSA00003\t0,0,0,0\t0\tnew\n"));
  // room for the new line only
  CHECK(sessions_kept(file, strlen(file), 0, &s, tiny, sizeof tiny) > 0 &&
        !strcmp(tiny, "7000\t8000\tPPSA00003\t0,0,0,0\t0\tnew\n"));
  CHECK(sessions_kept(file, strlen(file), 0, &s, tiny, 10) == -1 && !tiny[0]);
}


static void
test_duration(void) {
  char buf[32];

  session_duration(2 * 3600 + 13 * 60, buf, sizeof buf);
  CHECK(!strcmp(buf, "2 h 13 min"));
  session_duration(3600 + 10, buf, sizeof buf);
  CHECK(!strcmp(buf, "1 h"));
  session_duration(45 * 60 + 29, buf, sizeof buf);
  CHECK(!strcmp(buf, "45 min"));
  session_duration(5, buf, sizeof buf);
  CHECK(!strcmp(buf, "1 min"));
}


int
main(void) {
  test_line();
  test_week();
  test_kept();
  test_duration();
  if(g_failed) {
    printf("%d session checks failed\n", g_failed);
    return 1;
  }
  printf("all session checks passed\n");
  return 0;
}
