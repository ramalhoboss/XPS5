/* "Playing now" and invites to play, see play.h. */

#include <stdio.h>
#include <string.h>

#include "play.h"

#define ELLIPSIS "\xe2\x80\xa6"


/**
 * the bytes of the UTF-8 character that starts with c
 **/
static int
char_len(unsigned char c) {
  return c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
}


int
play_units(const char *s) {
  int n = 0;

  for(const unsigned char *p=(const unsigned char *)s; p && *p; p++) {
    if((*p & 0xc0) != 0x80) {
      n += *p >= 0xf0 ? 2 : 1; // past the Basic Multilingual Plane: a surrogate pair
    }
  }
  return n;
}


/**
 * len bytes of s at most, cut at a whole character
 **/
static size_t
whole(const char *s, size_t len) {
  size_t n = 0, c;

  while(n < len && s[n]) {
    c = (size_t)char_len((unsigned char)s[n]);
    if(n + c > len) {
      break;
    }
    n += c;
  }
  return n;
}


/**
 * a name on one line, without the spaces around it, into out
 **/
static void
one_line(const char *s, size_t len, char *out, size_t size) {
  size_t n = 0;

  while(len && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) {
    s++;
    len--;
  }
  while(len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\n' || s[len - 1] == '\r')) {
    len--;
  }
  len = whole(s, len < size ? len : size - 1);
  for(; n < len; n++) {
    out[n] = s[n] == '\n' || s[n] == '\r' || s[n] == '\t' ? ' ' : s[n];
  }
  out[n] = 0;
}


int
play_status(const char *game, char *out, size_t size) {
  char name[PLAY_STATUS];
  int room = PLAY_LOCATION - play_units(PLAY_MARK) - play_units(PLAY_TAIL);
  size_t n = 0, c;
  int units = 0;

  if(!size) {
    return -1;
  }
  out[0] = 0;
  if(!game) {
    return -1;
  }
  one_line(game, strlen(game), name, sizeof name);
  if(!name[0]) {
    return -1;
  }
  if(play_units(name) > room) {
    // as much of it as leaves room for "…"
    while(name[n]) {
      c = (size_t)char_len((unsigned char)name[n]);
      if(units + (c == 4 ? 2 : 1) > room - 1) {
        break;
      }
      units += c == 4 ? 2 : 1;
      n += c;
    }
    while(n && name[n - 1] == ' ') {
      n--;
    }
    name[n] = 0;
    if(snprintf(out, size, PLAY_MARK "%s" ELLIPSIS PLAY_TAIL, name) >= (int)size) {
      out[0] = 0;
      return -1;
    }
    return 0;
  }
  if(snprintf(out, size, PLAY_MARK "%s" PLAY_TAIL, name) >= (int)size) {
    out[0] = 0;
    return -1;
  }
  return 0;
}


int
play_game_of(const char *location, char *game, size_t size) {
  size_t len, mark = strlen(PLAY_MARK), tail = strlen(PLAY_TAIL);

  if(size) {
    game[0] = 0;
  }
  if(!location || !size) {
    return 0;
  }
  len = strlen(location);
  if(len <= mark + tail || strncmp(location, PLAY_MARK, mark) || strcmp(location + len - tail, PLAY_TAIL)) {
    return 0;
  }
  one_line(location + mark, len - mark - tail, game, size);
  return game[0] ? 1 : 0;
}


/**
 * whether id looks like a title id: four capitals and five digits
 **/
static int
title_id(const char *id, size_t len) {
  if(len != 9) {
    return 0;
  }
  for(size_t i=0; i < 9; i++) {
    if(i < 4 ? !(id[i] >= 'A' && id[i] <= 'Z') : !(id[i] >= '0' && id[i] <= '9')) {
      return 0;
    }
  }
  return 1;
}


int
play_invite(const char *game, const char *id, const char *phrase, char *out, size_t size) {
  char name[PLAY_GAME * 2];
  int n;

  if(!size) {
    return -1;
  }
  out[0] = 0;
  if(!game || !phrase || !phrase[0] || (id && id[0] && !title_id(id, strlen(id)))) {
    return -1;
  }
  one_line(game, strlen(game), name, sizeof name);
  if(!name[0] || strstr(name, " \xc2\xb7 ")) {
    return -1;
  }
  if(id && id[0]) {
    n = snprintf(out, size, PLAY_MARK "%s \xc2\xb7 %s (XPS5 %s)", name, phrase, id);
  } else {
    n = snprintf(out, size, PLAY_MARK "%s \xc2\xb7 %s (XPS5)", name, phrase);
  }
  if(n < 0 || n >= (int)size) {
    out[0] = 0;
    return -1;
  }
  return 0;
}


int
play_invite_parse(const char *text, char *game, size_t gsize, char *id, size_t isize) {
  size_t len, mark = strlen(PLAY_MARK);
  const char *dot, *tag;

  if(gsize) {
    game[0] = 0;
  }
  if(isize) {
    id[0] = 0;
  }
  if(!text || !gsize || isize < 10) {
    return 0;
  }
  len = strlen(text);
  while(len && (text[len - 1] == ' ' || text[len - 1] == '\n' || text[len - 1] == '\r')) {
    len--;
  }
  if(len < mark + 8 || strncmp(text, PLAY_MARK, mark) || text[len - 1] != ')' ||
     !(dot=strstr(text + mark, " \xc2\xb7 ")) || (size_t)(dot - text) >= len) {
    return 0;
  }
  // its last brackets: "(XPS5)" or "(XPS5 <title id>)"
  for(tag=text + len - 1; tag > dot && *tag != '('; tag--) {
  }
  if(*tag != '(' || strncmp(tag, "(XPS5", 5)) {
    return 0;
  }
  if(tag[5] == ' ') {
    if(!title_id(tag + 6, (size_t)(text + len - 1 - (tag + 6)))) {
      return 0;
    }
    memcpy(id, tag + 6, 9);
    id[9] = 0;
  } else if(tag + 5 != text + len - 1) {
    return 0;
  }
  one_line(text + mark, (size_t)(dot - text) - mark, game, gsize);
  if(!game[0]) {
    id[0] = 0;
    return 0;
  }
  return 1;
}


/**
 * whether s is an account's number
 **/
static int
number(const char *s) {
  size_t n = s ? strlen(s) : 0;

  if(!n || n >= 24 || s[0] == '0') {
    return 0;
  }
  for(size_t i=0; i < n; i++) {
    if(s[i] < '0' || s[i] > '9') {
      return 0;
    }
  }
  return 1;
}


int
play_conversation(const char *me, const char *them, char *out, size_t size) {
  const char *low, *high;
  int n;

  if(size) {
    out[0] = 0;
  }
  if(!number(me) || !number(them) || !strcmp(me, them)) {
    return -1;
  }
  // the smaller first: fewer digits, or the same and before
  if(strlen(me) != strlen(them)) {
    low = strlen(me) < strlen(them) ? me : them;
  } else {
    low = strcmp(me, them) < 0 ? me : them;
  }
  high = low == me ? them : me;
  n = snprintf(out, size, "%s-%s", low, high);
  if(n < 0 || (size_t)n >= size) {
    if(size) {
      out[0] = 0;
    }
    return -1;
  }
  return 0;
}
