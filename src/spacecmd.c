/* The lines a Space goes between the title and the payload in, see
spacecmd.h. */

#include <stdio.h>
#include <string.h>

#include "spacecmd.h"


/**
 * an id or a screen name: letters, digits and '_', short; empty is fine
 **/
static int
name_ok(const char *s) {
  size_t n = 0;

  for(; s[n]; n++) {
    char c = s[n];
    if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
      return 0;
    }
  }
  return n < SPACE_NAME;
}


/**
 * an https address with nothing in it that would cut the line
 **/
static int
url_ok(const char *s) {
  size_t n = strlen(s);

  if(n < 9 || n >= SPACE_URL || strncmp(s, "https://", 8)) {
    return 0;
  }
  for(size_t i=0; i < n; i++) {
    if((unsigned char)s[i] <= ' ' || s[i] == 0x7f) {
      return 0;
    }
  }
  return 1;
}


/**
 * a name as it goes in a line: "-" for none
 **/
static const char *
dash(const char *s) {
  return s && s[0] ? s : "-";
}


/**
 * a name read from a line into out: "-" is none; 0, or -1 when it is no name
 **/
static int
undash(const char *s, char out[SPACE_NAME]) {
  if(!strcmp(s, "-")) {
    out[0] = 0;
    return 0;
  }
  if(!name_ok(s)) {
    out[0] = 0;
    return -1;
  }
  snprintf(out, SPACE_NAME, "%s", s);
  return 0;
}


/**
 * only spaces, then the end of the line, from s on
 **/
static int
line_ends(const char *s) {
  s += strspn(s, " ");
  return !*s || !strcmp(s, "\n") || !strcmp(s, "\r\n");
}


int
space_cmd_play(char *out, size_t size, unsigned n, int live, const char *id, const char *host,
               const char *url) {
  int len;

  if(!n || !url || !url_ok(url) || (id && !name_ok(id)) || (host && !name_ok(host))) {
    return -1;
  }
  len = snprintf(out, size, "space play %u %d %s %s %s\n", n, !!live, dash(id), dash(host), url);
  if(len < 0 || (size_t)len >= size) {
    if(size) {
      memset(out, 0, size); // the address does not stay behind cut in two
    }
    return -1;
  }
  return len;
}


int
space_cmd_play_args(const char *args, unsigned *n, int *live, char id[SPACE_NAME], char host[SPACE_NAME],
                    char url[SPACE_URL]) {
  char a[SPACE_NAME + 1], b[SPACE_NAME + 1];
  int at = 0;

  id[0] = host[0] = url[0] = 0;
  if(sscanf(args, "%u %d %32s %32s %767s%n", n, live, a, b, url, &at) != 5 || !at || !line_ends(args + at) ||
     !*n || (*live != 0 && *live != 1) || !url_ok(url) || undash(a, id) || undash(b, host)) {
    memset(url, 0, SPACE_URL);
    id[0] = host[0] = 0;
    return -1;
  }
  return 0;
}


int
space_line_put(char *out, size_t size, unsigned n, const space_status_t *s, int live, const char *id,
               const char *host) {
  int len;

  if((id && !name_ok(id)) || (host && !name_ok(host))) {
    id = host = NULL;
  }
  len = snprintf(out, size, "space %u %d %d %d %lld %d %d %d %s %s\n", n, s->state, s->error, s->code,
                 s->heard_ms, !!s->waiting, !!s->hushed, !!live, dash(id), dash(host));
  return len < 0 || (size_t)len >= size ? -1 : len;
}


int
space_line_get(const char *line, unsigned *n, space_status_t *s, int *live, char id[SPACE_NAME],
               char host[SPACE_NAME]) {
  char a[SPACE_NAME + 1], b[SPACE_NAME + 1];
  int at = 0;

  memset(s, 0, sizeof *s);
  id[0] = host[0] = 0;
  if(sscanf(line, "space %u %d %d %d %lld %d %d %d %32s %32s%n", n, &s->state, &s->error, &s->code,
            &s->heard_ms, &s->waiting, &s->hushed, live, a, b, &at) != 10 || !at || !line_ends(line + at) ||
     s->state < SPACE_IDLE || s->state > SPACE_FAILED || s->error < SPACE_ERR_NONE ||
     s->error > SPACE_ERR_GONE || s->heard_ms < 0 || undash(a, id) || undash(b, host)) {
    memset(s, 0, sizeof *s);
    id[0] = host[0] = 0;
    return -1;
  }
  s->waiting = !!s->waiting;
  s->hushed = !!s->hushed;
  *live = !!*live;
  return 0;
}
