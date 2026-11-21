/* The user's sessions of play, for the cards XPS5 offers to post: the
notifications payload (src/notify.c) notes when a game opens and closes,
the trophies and captures between, and keeps the sessions of the last weeks
in a text file, one line each; the title asks it for the one that just
ended and for the week's, and draws their cards. Nothing here opens a file,
so tests/sessions_test.c runs it on the host. */

#pragma once

#include <stddef.h>

#define SESSIONS_PATH   "/data/TwitterPS5/sessions.txt"
#define SESSIONS_KEEP   (35 * 24 * 3600LL) // seconds of sessions the file keeps
#define SESSION_LEAST   (10 * 60)          // a shorter one is not kept
#define SESSION_GAP     (10 * 60)          // the game unseen this long (the PS5 at rest) ends one
#define SESSION_ID      16
#define SESSION_NAME    128
#define SESSION_BEST    128
#define SESSION_PICS    4                  // screenshots of one kept for its card
#define SESSION_PATH    256
#define SESSION_LINE    (SESSION_NAME + 128)
#define WEEK_SECONDS    (7 * 24 * 3600LL)
#define WEEK_GAMES      16                 // games a week adds up apart; more go into the totals only

typedef struct session {
  char      id[SESSION_ID];     // the game's title id
  char      name[SESSION_NAME]; // its name, on one line, without tabs
  long long start, end;         // when it opened and was last seen open, in seconds
  int       trophies[4];        // earned meanwhile, by grade (bronze to platinum)
  int       shots;              // captures taken meanwhile
  char      best[SESSION_BEST]; // the best trophy's name, or empty
  char      pics[SESSION_PICS][SESSION_PATH]; // screenshots of it, the newest last
  int       pic_count;
} session_t;

typedef struct week_game {
  char      id[SESSION_ID];
  char      name[SESSION_NAME];
  long long seconds;
  int       trophies;           // of all grades
} week_game_t;

typedef struct week {
  int         sessions;
  long long   seconds;
  int         trophies[4];
  int         shots;
  int         count;            // games below, the most played first once week_sort ran
  week_game_t games[WEEK_GAMES];
} week_t;

/**
 * a session's line in the file: start, end, title id, trophies by grade
 * between commas, captures and name, between tabs, and a newline; 0, or
 * -1 when it does not fit
 **/
int session_line(const session_t *s, char *buf, size_t size);

/**
 * a line of the file into s (without its best trophy and screenshots);
 * 0, or -1 when it is not one
 **/
int session_parse(const char *line, session_t *s);

/**
 * text as a session keeps it: on one line, its tabs and control characters
 * as spaces, cut at a whole character
 **/
void session_text(const char *text, char *out, size_t size);

/**
 * the sessions of the file (text, len bytes) that ended in [from, to)
 * added into w, which starts zeroed; returns how many
 **/
int week_add_file(week_t *w, const char *text, size_t len, long long from, long long to);

/**
 * a session added into w
 **/
void week_add(week_t *w, const session_t *s);

/**
 * w's games, the most played first
 **/
void week_sort(week_t *w);

/**
 * the file without its sessions that ended before keep_from, then s's line
 * after them: into out (size bytes); returns its length, or -1 when it does
 * not fit (then out has only s's line, or nothing)
 **/
long long sessions_kept(const char *text, size_t len, long long keep_from, const session_t *s, char *out,
                        size_t size);

/**
 * a duration as people say it: "2 h 13 min", "45 min", "1 h"
 **/
void session_duration(long long seconds, char *buf, size_t size);
