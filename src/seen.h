/* The people the user follows as XPS5 saw them around, as PSN's "Last
online" and "What's new" have it: when each was last seen playing or on
XPS5 and in what game, the games each played lately, and what changed from
one look to the next (a game started, XPS5 opened) for the friends'
activity. The notifications payload and the title note what each look at
the people the user follows shows (their profiles' locations, play.h), into
a file of the account's (seen_path); it never leaves the PS5. Nothing here
opens a file, so tests/seen_test.c runs it on the host. */

#pragma once

#include <stddef.h>

#include "play.h"
#include "xapi.h"

#define SEEN_PEOPLE 128          // people kept: the ones of the last look, then the ones seen last
#define SEEN_GAMES  3            // games kept for each, the newest first
#define SEEN_EVENTS 24           // changes kept, the newest first
#define SEEN_MOST   (96 << 10)   // bytes of the file at most
#define SEEN_FRESH  (15 * 60)    // seconds a look at most between the two of a change

// how someone was at the last look
enum { SEEN_AWAY, SEEN_PLAYING, SEEN_ONLINE };

typedef struct seen_person {
  char      id[24];                      // their account's number
  long long at;                          // when last seen playing or on XPS5, or 0 for never
  int       state;                       // SEEN_*, at the last look
  long long in;                          // the last look they were in
  char      game[PLAY_GAME];             // what they played when last seen, or empty: on XPS5
  char      games[SEEN_GAMES][PLAY_GAME]; // the games they were seen playing, the newest first
} seen_person_t;

typedef struct seen_event {
  long long at;
  char      id[24];
  char      game[PLAY_GAME];             // the game they started, or empty: they opened XPS5
  char      name[128];                   // who, as the look named them
  char      user[64];
  char      avatar[192];
} seen_event_t;

typedef struct seen {
  long long     looked;                  // the last look, or 0
  int           count, events;
  seen_person_t people[SEEN_PEOPLE];
  seen_event_t  event[SEEN_EVENTS];      // the newest first
} seen_t;

/**
 * the file of the account (1 to SETTINGS_ACCOUNTS); 0 or -1
 **/
int seen_path(int account, char *buf, size_t size);

/**
 * the file's text (len bytes) into s, which it empties first; what is not
 * one of its lines is left out
 **/
void seen_parse(seen_t *s, const char *text, size_t len);

/**
 * s as the file keeps it, into out (size bytes); its length, or -1 when it
 * does not fit
 **/
long long seen_format(const seen_t *s, char *out, size_t size);

/**
 * a look at count people (X_KIND_USER, their game or "on XPS5" from their
 * profiles) at now: who of them plays or is on XPS5 is seen then, and a game
 * started or XPS5 opened since the look before is a change, when they were
 * in that look and it was at most SEEN_FRESH before (a change between two
 * looks far apart is not told: it may be hours old; nor is someone back
 * from a look that left them out). Returns the changes added
 **/
int seen_note(seen_t *s, const x_post_t *people, int count, long long now);

/**
 * the person with the number id, or NULL
 **/
const seen_person_t *seen_find(const seen_t *s, const char *id);
