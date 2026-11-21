/* The sessions of play, see sessions.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sessions.h"


void
session_text(const char *text, char *out, size_t size) {
  size_t n = 0, len;

  if(!size) {
    return;
  }
  if(!text) {
    out[0] = 0;
    return;
  }
  len = strlen(text); // first: out may be text itself
  for(size_t i=0; i < len && n + 1 < size; i++) {
    out[n++] = (unsigned char)text[i] < 0x20 || text[i] == 0x7f ? ' ' : text[i];
  }
  if(n < len) {
    while(n > 0 && ((unsigned char)text[n] & 0xc0) == 0x80) { // not inside a character
      n--;
    }
  }
  out[n] = 0;
}


int
session_line(const session_t *s, char *buf, size_t size) {
  char name[SESSION_NAME];
  int n;

  session_text(s->name, name, sizeof name);
  n = snprintf(buf, size, "%lld\t%lld\t%s\t%d,%d,%d,%d\t%d\t%s\n", s->start, s->end, s->id, s->trophies[0],
               s->trophies[1], s->trophies[2], s->trophies[3], s->shots, name);
  return n < 0 || (size_t)n >= size ? -1 : 0;
}


int
session_parse(const char *line, session_t *s) {
  const char *tab;
  size_t n;
  int at = 0;

  memset(s, 0, sizeof *s);
  if(sscanf(line, "%lld\t%lld\t%n", &s->start, &s->end, &at) != 2 || !at || s->end < s->start) {
    return -1;
  }
  line += at;
  if(!(tab=strchr(line, '\t')) || (n=(size_t)(tab - line)) == 0 || n >= sizeof s->id) {
    return -1;
  }
  memcpy(s->id, line, n);
  s->id[n] = 0;
  line = tab + 1;
  at = 0;
  if(sscanf(line, "%d,%d,%d,%d\t%d\t%n", &s->trophies[0], &s->trophies[1], &s->trophies[2], &s->trophies[3],
            &s->shots, &at) != 5 || !at) {
    return -1;
  }
  for(int i=0; i<4; i++) {
    if(s->trophies[i] < 0) {
      return -1;
    }
  }
  if(s->shots < 0) {
    return -1;
  }
  line += at;
  n = strcspn(line, "\r\n");
  n = n < sizeof s->name ? n : sizeof s->name - 1;
  memcpy(s->name, line, n);
  s->name[n] = 0;
  session_text(s->name, s->name, sizeof s->name);
  return 0;
}


void
week_add(week_t *w, const session_t *s) {
  long long seconds = s->end - s->start;
  int all = 0, at = -1;

  w->sessions++;
  w->seconds += seconds;
  for(int i=0; i<4; i++) {
    w->trophies[i] += s->trophies[i];
    all += s->trophies[i];
  }
  w->shots += s->shots;
  for(int i=0; i < w->count && at < 0; i++) {
    at = !strcmp(w->games[i].id, s->id) ? i : at;
  }
  if(at < 0 && w->count < WEEK_GAMES) {
    at = w->count++;
    memset(&w->games[at], 0, sizeof w->games[at]);
    snprintf(w->games[at].id, sizeof w->games[at].id, "%s", s->id);
  }
  if(at >= 0) {
    w->games[at].seconds += seconds;
    w->games[at].trophies += all;
    if(s->name[0]) {
      snprintf(w->games[at].name, sizeof w->games[at].name, "%s", s->name); // its newest name
    }
  }
}


static int
more_played(const void *a, const void *b) {
  const week_game_t *x = a, *y = b;

  return x->seconds < y->seconds ? 1 : x->seconds > y->seconds ? -1 : strcmp(x->id, y->id);
}


void
week_sort(week_t *w) {
  qsort(w->games, (size_t)w->count, sizeof w->games[0], more_played);
}


int
week_add_file(week_t *w, const char *text, size_t len, long long from, long long to) {
  char line[SESSION_LINE + 64];
  session_t s;
  size_t i = 0, n;
  int added = 0;

  while(i < len) {
    n = 0;
    while(i + n < len && text[i + n] != '\n') {
      n++;
    }
    if(n < sizeof line) {
      memcpy(line, text + i, n);
      line[n] = 0;
      if(!session_parse(line, &s) && s.end >= from && s.end < to) {
        week_add(w, &s);
        added++;
      }
    }
    i += n + 1;
  }
  return added;
}


long long
sessions_kept(const char *text, size_t len, long long keep_from, const session_t *s, char *out, size_t size) {
  char line[SESSION_LINE + 64], mine[SESSION_LINE + 64];
  session_t old;
  size_t i = 0, n, used = 0, mine_len;

  if(!size) {
    return -1;
  }
  out[0] = 0;
  if(session_line(s, mine, sizeof mine)) {
    return -1;
  }
  mine_len = strlen(mine);
  while(text && i < len) {
    n = 0;
    while(i + n < len && text[i + n] != '\n') {
      n++;
    }
    if(n < sizeof line) {
      memcpy(line, text + i, n);
      line[n] = 0;
      // the lines still kept, and only lines: a broken one goes
      if(!session_parse(line, &old) && old.end >= keep_from) {
        if(used + n + 1 + mine_len + 1 > size) {
          break; // the oldest are kept first; what does not fit goes
        }
        memcpy(out + used, line, n);
        out[used + n] = '\n';
        used += n + 1;
      }
    }
    i += n + 1;
  }
  if(used + mine_len + 1 > size) {
    if(mine_len + 1 > size) {
      out[0] = 0;
      return -1;
    }
    used = 0;
  }
  memcpy(out + used, mine, mine_len + 1);
  return (long long)(used + mine_len);
}


void
session_duration(long long seconds, char *buf, size_t size) {
  long long minutes = (seconds + 30) / 60, h = minutes / 60, m = minutes % 60;

  if(h && m) {
    snprintf(buf, size, "%lld h %lld min", h, m);
  } else if(h) {
    snprintf(buf, size, "%lld h", h);
  } else {
    snprintf(buf, size, "%lld min", minutes < 1 ? 1 : minutes);
  }
}
