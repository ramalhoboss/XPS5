/* The people the user follows as XPS5 saw them around, see seen.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seen.h"
#include "settings.h"

#define FIELDS 9 // of a line at most


int
seen_path(int account, char *buf, size_t size) {
  int n;

  if(account < 1 || account > SETTINGS_ACCOUNTS) {
    return -1;
  }
  n = snprintf(buf, size, "%sseen%d.txt", SETTINGS_SESSION_DIR, account);
  return n < 0 || (size_t)n >= size ? -1 : 0;
}


/**
 * len bytes of s into out, on one line (tabs and line ends as spaces), cut
 * at a whole character to fit size
 **/
static void
field_copy(char *out, size_t size, const char *s, size_t len) {
  size_t n = 0, c;

  if(!size) {
    return;
  }
  while(s && n < len && s[n]) {
    unsigned char b = (unsigned char)s[n];
    c = b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : b >= 0xc0 ? 2 : 1;
    if(n + c > len || n + c >= size) {
      break;
    }
    n += c;
  }
  memcpy(out, s, n);
  out[n] = 0;
  for(char *p=out; *p; p++) {
    *p = *p == '\t' || *p == '\n' || *p == '\r' ? ' ' : *p;
  }
}


/**
 * whether s is an account's number
 **/
static int
number(const char *s) {
  size_t n = strlen(s);

  if(!n || n >= 24 || s[0] == '0') {
    return 0;
  }
  return strspn(s, "0123456789") == n;
}


/**
 * a number of a field, or -1 when it is not one
 **/
static long long
count_of(const char *s) {
  char *end;
  long long v;

  if(!s[0] || strspn(s, "0123456789") != strlen(s) || strlen(s) > 18) {
    return -1;
  }
  v = strtoll(s, &end, 10);
  return *end ? -1 : v;
}


/**
 * one line (len bytes, without its end) into s
 **/
static void
parse_line(seen_t *s, const char *line, size_t len) {
  char f[FIELDS][256];
  int n = 0;
  size_t start = 0;
  long long at, state, in;

  for(size_t i=0; i <= len && n < FIELDS; i++) {
    if(i == len || line[i] == '\t') {
      field_copy(f[n++], sizeof f[0], line + start, i - start);
      start = i + 1;
    }
  }
  for(int i=n; i < FIELDS; i++) {
    f[i][0] = 0;
  }
  if(n >= 2 && !strcmp(f[0], "l") && (at=count_of(f[1])) >= 0) {
    s->looked = at;
  } else if(n >= 5 && !strcmp(f[0], "p") && s->count < SEEN_PEOPLE && number(f[1]) &&
     (at=count_of(f[2])) >= 0 && (state=count_of(f[3])) >= SEEN_AWAY && state <= SEEN_ONLINE &&
     (in=count_of(f[4])) >= 0 && !seen_find(s, f[1])) {
    seen_person_t *p = &s->people[s->count++];
    memset(p, 0, sizeof *p);
    field_copy(p->id, sizeof p->id, f[1], strlen(f[1]));
    p->at = at;
    p->state = (int)state;
    p->in = in;
    field_copy(p->game, sizeof p->game, f[5], strlen(f[5]));
    for(int g=0, k=0; g < SEEN_GAMES; g++) {
      if(f[6 + g][0]) {
        field_copy(p->games[k++], sizeof p->games[0], f[6 + g], strlen(f[6 + g]));
      }
    }
  } else if(n >= 4 && !strcmp(f[0], "e") && s->events < SEEN_EVENTS && (at=count_of(f[1])) > 0 &&
            number(f[2])) {
    seen_event_t *e = &s->event[s->events++];
    memset(e, 0, sizeof *e);
    e->at = at;
    field_copy(e->id, sizeof e->id, f[2], strlen(f[2]));
    field_copy(e->game, sizeof e->game, f[3], strlen(f[3]));
    field_copy(e->name, sizeof e->name, f[4], strlen(f[4]));
    field_copy(e->user, sizeof e->user, f[5], strlen(f[5]));
    // a picture of X's own, nothing else
    if(!strncmp(f[6], "https://pbs.twimg.com/", 22)) {
      field_copy(e->avatar, sizeof e->avatar, f[6], strlen(f[6]));
    }
  }
}


void
seen_parse(seen_t *s, const char *text, size_t len) {
  size_t start = 0;

  memset(s, 0, sizeof *s);
  for(size_t i=0; text && i <= len; i++) {
    if(i == len || text[i] == '\n') {
      size_t n = i - start;
      if(n && text[start + n - 1] == '\r') {
        n--;
      }
      if(n) {
        parse_line(s, text + start, n);
      }
      start = i + 1;
    }
  }
}


/**
 * a field as the file keeps it: on one line
 **/
static const char *
clean(const char *s, char *buf, size_t size) {
  field_copy(buf, size, s, strlen(s));
  return buf;
}


long long
seen_format(const seen_t *s, char *out, size_t size) {
  char a[256], b[256], c[256], d[256], e[256];
  size_t len = 0;
  int n;

  if(!size) {
    return -1;
  }
  out[0] = 0;
  n = snprintf(out, size, "l\t%lld\n", s->looked > 0 ? s->looked : 0);
  if(n < 0 || (size_t)n >= size) {
    out[0] = 0;
    return -1;
  }
  len = (size_t)n;
  for(int i=0; i < s->count; i++) {
    const seen_person_t *p = &s->people[i];
    n = snprintf(out + len, size - len, "p\t%s\t%lld\t%d\t%lld\t%s\t%s\t%s\t%s\n", p->id, p->at > 0 ? p->at : 0,
                 p->state, p->in > 0 ? p->in : 0, clean(p->game, a, sizeof a), clean(p->games[0], b, sizeof b),
                 clean(p->games[1], c, sizeof c), clean(p->games[2], d, sizeof d));
    if(n < 0 || (size_t)n >= size - len) {
      out[0] = 0;
      return -1;
    }
    len += (size_t)n;
  }
  for(int i=0; i < s->events; i++) {
    const seen_event_t *v = &s->event[i];
    n = snprintf(out + len, size - len, "e\t%lld\t%s\t%s\t%s\t%s\t%s\n", v->at, v->id, clean(v->game, a, sizeof a),
                 clean(v->name, b, sizeof b), clean(v->user, c, sizeof c), clean(v->avatar, e, sizeof e));
    if(n < 0 || (size_t)n >= size - len) {
      out[0] = 0;
      return -1;
    }
    len += (size_t)n;
  }
  return (long long)len;
}


const seen_person_t *
seen_find(const seen_t *s, const char *id) {
  for(int i=0; id && id[0] && i < s->count; i++) {
    if(!strcmp(s->people[i].id, id)) {
      return &s->people[i];
    }
  }
  return NULL;
}


/**
 * a place for the person with the number id: a new one, or, with all of them
 * taken, the one not in this look (marked) seen longest ago
 **/
static seen_person_t *
person_add(seen_t *s, const char *id, const unsigned char *marked) {
  int at = -1;

  if(s->count < SEEN_PEOPLE) {
    at = s->count++;
  } else {
    for(int i=0; i < s->count; i++) {
      if(!marked[i] && (at < 0 || s->people[i].at < s->people[at].at)) {
        at = i;
      }
    }
  }
  if(at < 0) {
    return NULL;
  }
  memset(&s->people[at], 0, sizeof s->people[at]);
  snprintf(s->people[at].id, sizeof s->people[at].id, "%s", id);
  return &s->people[at];
}


/**
 * game first among the ones p was seen playing
 **/
static void
games_add(seen_person_t *p, const char *game) {
  int k = SEEN_GAMES - 1;

  for(int i=0; i < SEEN_GAMES; i++) {
    if(!strcmp(p->games[i], game)) {
      k = i;
      break;
    }
  }
  memmove(p->games[1], p->games[0], (size_t)k * sizeof p->games[0]);
  snprintf(p->games[0], sizeof p->games[0], "%s", game);
}


/**
 * a change, the newest first
 **/
static void
event_add(seen_t *s, const x_post_t *p, const char *game, long long now) {
  seen_event_t *e = &s->event[0];

  memmove(&s->event[1], &s->event[0], (size_t)(SEEN_EVENTS - 1) * sizeof s->event[0]);
  s->events += s->events < SEEN_EVENTS;
  memset(e, 0, sizeof *e);
  e->at = now;
  snprintf(e->id, sizeof e->id, "%s", p->user_id);
  field_copy(e->game, sizeof e->game, game, strlen(game));
  field_copy(e->name, sizeof e->name, p->name, strlen(p->name));
  field_copy(e->user, sizeof e->user, p->user, strlen(p->user));
  if(!strncmp(p->avatar, "https://pbs.twimg.com/", 22)) {
    field_copy(e->avatar, sizeof e->avatar, p->avatar, strlen(p->avatar));
  }
}


int
seen_note(seen_t *s, const x_post_t *people, int count, long long now) {
  unsigned char marked[SEEN_PEOPLE];
  const int fresh = s->looked > 0 && now >= s->looked && now - s->looked <= SEEN_FRESH;
  int added = 0;

  if(now <= 0) {
    return 0;
  }
  memset(marked, 0, sizeof marked);
  for(int j=0; people && j < count; j++) {
    const x_post_t *p = &people[j];
    const int state = p->playing[0] ? SEEN_PLAYING : p->online ? SEEN_ONLINE : SEEN_AWAY;
    seen_person_t *k = (seen_person_t *)seen_find(s, p->user_id);
    int was, news, at;
    char game[PLAY_GAME];

    if(p->kind != X_KIND_USER || !number(p->user_id)) {
      continue;
    }
    if(k && marked[k - s->people]) {
      continue; // listed twice in the look
    }
    field_copy(game, sizeof game, p->playing, strlen(p->playing));
    was = k ? k->state : -1;
    // a game started (or another), or XPS5 opened by someone who was not
    // around; a game closed is not news
    news = fresh && k && k->in == s->looked &&
           (state == SEEN_PLAYING ? !(was == SEEN_PLAYING && !strcmp(k->game, game)) :
            state == SEEN_ONLINE && was == SEEN_AWAY);
    if(!k && !(k=person_add(s, p->user_id, marked))) {
      continue;
    }
    at = (int)(k - s->people);
    marked[at] = 1;
    k->state = state;
    k->in = now;
    if(state != SEEN_AWAY) {
      k->at = now;
      snprintf(k->game, sizeof k->game, "%s", game);
      if(game[0]) {
        games_add(k, game);
      }
    }
    if(news) {
      event_add(s, p, game, now);
      added++;
    }
  }
  // the ones not in the look are not around as far as XPS5 knows
  for(int i=0; i < s->count; i++) {
    if(!marked[i]) {
      s->people[i].state = SEEN_AWAY;
    }
  }
  s->looked = now;
  return added;
}
