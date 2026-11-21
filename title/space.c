/* A Space in the background, see space.h: played by the payload's radio
when the payload takes it, else by the title's own (src/radio.h).

Each space_play or space_stop moves g_gen on. A Space handed to the
payload has a thread of its own (away_main) while it is the one playing:
it hands it over, then asks the payload how it goes about once a second
(while the title runs: suspended, it asks nothing, and the payload plays
on), says a hush again while one is wanted, and once another Space or a
stop moved g_gen on, tells the payload to stop that one, by its number, so
a stop never reaches a newer one. */

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "radio.h"
#include "space.h"

#define AWAY_STACK  (64 << 10)
#define POLL_MS     1000  // between two asks of how it goes
#define HUSH_MS     4000  // a hush the payload keeps unless said again
#define LOST_ASKS   20    // asks in a row with no answer before the payload counts as gone
#define CHECKS      5     // asks of how it goes, a second apart, after a hand-over it said nothing to
#define FIND_TRIES  10    // at the start, asks for a Space the payload plays, 2 s apart

int sceKernelUsleep(unsigned microseconds);
uint64_t sceKernelGetProcessTime(void);

enum { WHERE_NONE, WHERE_HERE, WHERE_AWAY };

typedef struct away {
  char     url[SPACE_URL];   // wiped once handed over
  char     id[SPACE_NAME];
  char     host[SPACE_NAME];
  int      live;
  int      gen;              // it is the Space playing while g_gen is this
  unsigned n;                // its number at the payload, once handed over
  int      hushed;           // the payload was last told it is hushed
} away_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int             g_gen;       // under g_lock, as all below
static int             g_where;     // WHERE_* of the Space of g_gen
static space_status_t  g_away;      // the payload's radio, as it said last (WHERE_AWAY)
static int             g_hush;      // a video plays its sound
static x_space_t       g_found;     // for space_found
static int             g_found_set;
static void          (*g_log)(const char *fmt, ...);
static int           (*g_ask)(const char *cmd, char *answer, size_t size);


static void
say(const char *fmt, ...) {
  char line[160];
  va_list ap;

  if(!g_log) {
    return;
  }
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  g_log("%s", line);
}


/**
 * a number for a Space handed over, never 0
 **/
static unsigned
new_number(int gen) {
  unsigned n = (unsigned)(sceKernelGetProcessTime() * 2654435761u) ^ ((unsigned)gen << 20);

  n &= 0x7fffffff;
  return n ? n : 1;
}


/**
 * the payload's line for cmd, read into s; 1 when it was a Space's line,
 * 0 when it answered something else (an older payload), -1 when it said
 * nothing, -2 when it could not be reached
 **/
static int
ask(const char *cmd, unsigned *n, space_status_t *s, int *live, char id[SPACE_NAME], char host[SPACE_NAME]) {
  char line[SPACE_LINE];

  if(!g_ask(cmd, line, sizeof line)) {
    return -2;
  }
  if(!line[0]) {
    return -1;
  }
  return space_line_get(line, n, s, live, id, host) ? 0 : 1;
}


/**
 * what the payload's radio said, when a is still the Space playing
 **/
static void
keep(const away_t *a, const space_status_t *s) {
  pthread_mutex_lock(&g_lock);
  if(a->gen == g_gen && g_where == WHERE_AWAY) {
    g_away = *s;
  }
  pthread_mutex_unlock(&g_lock);
}


/**
 * a to the payload: 1 when it took it, else 0 (and it plays here, when it
 * is still the one wanted)
 **/
static int
hand_over(away_t *a) {
  char cmd[SPACE_URL + 96], id[SPACE_NAME], host[SPACE_NAME];
  space_status_t s;
  unsigned n = 0;
  int live, got = -2;
  const char *why = "its line";

  a->n = new_number(a->gen);
  if(space_cmd_play(cmd, sizeof cmd, a->n, a->live, a->id, a->host, a->url) > 0) {
    got = ask(cmd, &n, &s, &live, id, host);
    why = got == -2 ? "no payload" : !got ? "an older payload" : "no answer";
  }
  memset(cmd, 0, sizeof cmd);
  // busy with a reading: the play is read once it is done, so it is asked again how it goes
  for(int i=0; got == -1 && i < CHECKS; i++) {
    sceKernelUsleep(1000 * 1000);
    got = ask("space status\n", &n, &s, &live, id, host);
  }
  if(got == 1 && n == a->n) {
    memset(a->url, 0, sizeof a->url);
    keep(a, &s);
    say("space: the payload plays it");
    return 1;
  }
  if(got == -1) {
    // it may still read the play later: it stops it then
    snprintf(cmd, sizeof cmd, "space stop %u\n", a->n);
    ask(cmd, &n, &s, &live, id, host);
  }
  say("space: played here (%s)", got == 1 ? "the payload did not take it" : why);
  pthread_mutex_lock(&g_lock);
  if(a->gen == g_gen) {
    g_where = WHERE_HERE;
    radio_play(a->url, a->live);
    radio_hush(g_hush);
  }
  pthread_mutex_unlock(&g_lock);
  memset(a->url, 0, sizeof a->url);
  return 0;
}


/**
 * the end of a Space the payload played, for the screen: it ended there,
 * or the payload went away with it
 **/
static void
gone(const away_t *a, int state, int error) {
  space_status_t s;

  memset(&s, 0, sizeof s);
  s.state = state;
  s.error = error;
  keep(a, &s);
}


static void *
away_main(void *arg) {
  away_t *a = arg;
  char cmd[64], id[SPACE_NAME], host[SPACE_NAME];
  space_status_t s;
  unsigned n;
  int mine = 1, hush = 0, lost = 0, live, got;

  if(a->n || hand_over(a)) {
    while(mine) {
      for(int t=0; t < POLL_MS / 100; t++) {
        pthread_mutex_lock(&g_lock);
        mine = a->gen == g_gen;
        hush = g_hush;
        pthread_mutex_unlock(&g_lock);
        if(!mine || hush != a->hushed) {
          break;
        }
        sceKernelUsleep(100 * 1000);
      }
      if(!mine) {
        // another Space or a stop: this one stops there (a newer one, by its number, does not)
        snprintf(cmd, sizeof cmd, "space stop %u\n", a->n);
        ask(cmd, &n, &s, &live, id, host);
        say("space: the payload's stopped");
        break;
      }
      // a hush said again each time, while it is wanted: it lasts HUSH_MS there
      if(hush || a->hushed) {
        snprintf(cmd, sizeof cmd, "space hush %u %d\n", a->n, hush);
      } else {
        snprintf(cmd, sizeof cmd, "space status\n");
      }
      if((got=ask(cmd, &n, &s, &live, id, host)) != 1) {
        if(++lost >= LOST_ASKS) {
          say("space: the payload does not answer (%d)", got);
          gone(a, SPACE_FAILED, SPACE_ERR_GONE);
          break;
        }
        continue;
      }
      lost = 0;
      a->hushed = hush;
      if(n != a->n) {
        // it plays no Space of ours: a copy started again, say
        say("space: the payload no longer plays it");
        gone(a, SPACE_FAILED, SPACE_ERR_GONE);
        break;
      }
      keep(a, &s);
      if(s.state != SPACE_TUNING && s.state != SPACE_ON) {
        break; // over, or failed: the screen says so
      }
    }
  }
  memset(a, 0, sizeof *a);
  free(a);
  return NULL;
}


/**
 * a thread for a, which frees it; 0, or -1 (a not freed)
 **/
static int
start_away(away_t *a) {
  pthread_attr_t attr;
  pthread_t thread;
  int ret;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, AWAY_STACK);
  ret = pthread_create(&thread, &attr, away_main, a);
  pthread_attr_destroy(&attr);
  if(ret) {
    return -1;
  }
  pthread_detach(thread);
  return 0;
}


/**
 * at the start: a Space the payload plays from before (the title was
 * closed while it played), taken as the one playing
 **/
static void *
find_main(void *arg) {
  int gen = (int)(intptr_t)arg, live, got = -2;
  char id[SPACE_NAME], host[SPACE_NAME];
  space_status_t s;
  unsigned n = 0;
  away_t *a;

  // the payload may be starting with the title
  for(int i=0; i < FIND_TRIES && got < 0; i++) {
    if(i) {
      sceKernelUsleep(2000 * 1000);
    }
    got = ask("space status\n", &n, &s, &live, id, host);
  }
  if(got != 1 || !n || (s.state != SPACE_TUNING && s.state != SPACE_ON) || !(a=calloc(1, sizeof *a))) {
    return NULL;
  }
  pthread_mutex_lock(&g_lock);
  if(gen != g_gen) {
    pthread_mutex_unlock(&g_lock); // a Space was asked for meanwhile: that one counts
    free(a);
    return NULL;
  }
  a->gen = ++g_gen;
  a->n = n;
  a->live = live;
  g_where = WHERE_AWAY;
  g_away = s;
  memset(&g_found, 0, sizeof g_found);
  snprintf(g_found.id, sizeof g_found.id, "%s", id);
  snprintf(g_found.host, sizeof g_found.host, "%s", host);
  g_found.state = live ? X_SPACE_LIVE : X_SPACE_ENDED;
  g_found.replay = !live;
  g_found_set = 1;
  if(start_away(a)) {
    free(a);
    g_where = WHERE_NONE;
    g_found_set = 0;
  }
  pthread_mutex_unlock(&g_lock);
  say("space: the payload was playing one");
  return NULL;
}


void
space_init(void (*log)(const char *fmt, ...), int (*ask_payload)(const char *cmd, char *answer, size_t size)) {
  pthread_attr_t attr;
  pthread_t thread;
  int gen;

  g_log = log;
  g_ask = ask_payload;
  radio_init(log);
  if(!g_ask) {
    return;
  }
  pthread_mutex_lock(&g_lock);
  gen = g_gen;
  pthread_mutex_unlock(&g_lock);
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, AWAY_STACK);
  if(!pthread_create(&thread, &attr, find_main, (void *)(intptr_t)gen)) {
    pthread_detach(thread);
  }
  pthread_attr_destroy(&attr);
}


void
space_play(const char *url, int live, const char *id, const char *host) {
  away_t *a = NULL;

  pthread_mutex_lock(&g_lock);
  g_gen++;
  radio_stop(); // one playing here stops; one playing there, by its thread
  memset(&g_away, 0, sizeof g_away);
  if(g_ask && url && strlen(url) < sizeof a->url && (a=calloc(1, sizeof *a))) {
    snprintf(a->url, sizeof a->url, "%s", url);
    snprintf(a->id, sizeof a->id, "%s", id ? id : "");
    snprintf(a->host, sizeof a->host, "%s", host ? host : "");
    a->live = live;
    a->gen = g_gen;
    g_where = WHERE_AWAY;
    g_away.state = SPACE_TUNING; // until the payload says, or it plays here
    if(start_away(a)) {
      memset(a, 0, sizeof *a);
      free(a);
      a = NULL;
    }
  }
  if(!a) {
    g_where = WHERE_HERE;
    radio_play(url, live);
    radio_hush(g_hush);
  }
  pthread_mutex_unlock(&g_lock);
}


void
space_stop(void) {
  pthread_mutex_lock(&g_lock);
  g_gen++;
  g_where = WHERE_NONE;
  radio_stop();
  memset(&g_away, 0, sizeof g_away);
  pthread_mutex_unlock(&g_lock);
}


void
space_hush(int on) {
  pthread_mutex_lock(&g_lock);
  g_hush = !!on;
  if(g_where == WHERE_HERE) {
    radio_hush(g_hush);
  } else if(g_where == WHERE_AWAY) {
    // as the payload will have it, until it says
    g_away.hushed = g_hush && (g_away.state == SPACE_TUNING || g_away.state == SPACE_ON);
  }
  pthread_mutex_unlock(&g_lock);
}


void
space_status(space_status_t *s) {
  pthread_mutex_lock(&g_lock);
  if(g_where == WHERE_HERE) {
    radio_status(s);
  } else if(g_where == WHERE_AWAY) {
    *s = g_away;
  } else {
    memset(s, 0, sizeof *s);
  }
  pthread_mutex_unlock(&g_lock);
}


int
space_found(x_space_t *s) {
  int found;

  pthread_mutex_lock(&g_lock);
  if((found=g_found_set)) {
    *s = g_found;
    g_found_set = 0;
  }
  pthread_mutex_unlock(&g_lock);
  return found;
}
