/* Linking an X account from a PC, see link.h. */

#include <stdio.h>
#include <string.h>

#include "json.h"
#include "link.h"

#define BODY_MAX 4096 // a session's body is a few hundred bytes


static int
lower(int c) {
  return c >= 'A' && c <= 'Z' ? c + 32 : c;
}


/**
 * whether the n bytes at s start with the header name (lower case) and
 * its colon
 **/
static int
is_header(const char *s, size_t n, const char *name) {
  size_t k = strlen(name);

  if(n <= k || s[k] != ':') {
    return 0;
  }
  for(size_t i=0; i < k; i++) {
    if(lower((unsigned char)s[i]) != name[i]) {
      return 0;
    }
  }
  return 1;
}


/**
 * where the headers of buf end, past their empty line, or 0 when they do
 * not within len
 **/
static size_t
headers_end(const char *buf, size_t len) {
  for(size_t i=0; i + 3 < len; i++) {
    if(buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
      return i + 4;
    }
  }
  return 0;
}


/**
 * the value of a header line of n bytes, past its name and the spaces
 **/
static const char *
header_value(const char *line, size_t n, size_t *len) {
  const char *at = memchr(line, ':', n), *end = line + n;

  for(at++; at < end && (*at == ' ' || *at == '\t'); at++) {
  }
  while(end > at && (end[-1] == ' ' || end[-1] == '\t')) {
    end--;
  }
  *len = (size_t)(end - at);
  return at;
}


/**
 * an Origin that may be answered: a browser extension's, of the letters
 * such an address has
 **/
static int
origin_allowed(const char *origin) {
  static const char *const schemes[] = {"chrome-extension://", "moz-extension://", "extension://"};
  int scheme = 0;

  for(size_t i=0; i < sizeof schemes / sizeof *schemes; i++) {
    scheme |= !strncmp(origin, schemes[i], strlen(schemes[i]));
  }
  if(!scheme) {
    return 0;
  }
  for(const char *c = origin; *c; c++) {
    if(!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
         *c == ':' || *c == '/' || *c == '.' || *c == '-' || *c == '_')) {
      return 0;
    }
  }
  return 1;
}


long
link_request_size(const char *buf, size_t len) {
  size_t end = headers_end(buf, len < LINK_REQUEST_MAX ? len : LINK_REQUEST_MAX), body = 0, n;
  const char *line, *next, *value;
  int found = 0;

  if(!end) {
    return len >= LINK_REQUEST_MAX ? -1 : 0;
  }
  for(line = buf; line < buf + end - 2; line = next + 2) {
    next = line;
    while(next < buf + end - 2 && !(next[0] == '\r' && next[1] == '\n')) {
      next++;
    }
    n = (size_t)(next - line);
    if(is_header(line, n, "transfer-encoding")) {
      return -1; // chunked: not from the extension
    }
    if(!is_header(line, n, "content-length")) {
      continue;
    }
    value = header_value(line, n, &n);
    if(!n || n > 6 || found++) {
      return -1;
    }
    body = 0;
    for(size_t i=0; i < n; i++) {
      if(value[i] < '0' || value[i] > '9') {
        return -1;
      }
      body = body * 10 + (size_t)(value[i] - '0');
    }
  }
  if(end + body > LINK_REQUEST_MAX) {
    return -1;
  }
  return len < end + body ? 0 : (long)(end + body);
}


int
link_parse_request(const char *buf, size_t len, link_request_t *r) {
  static const struct { const char *name; int method; } methods[] = {
    {"GET ", LINK_GET}, {"POST ", LINK_POST}, {"OPTIONS ", LINK_OPTIONS},
  };
  size_t end = headers_end(buf, len), n, k;
  const char *at = buf, *line, *next, *value;

  memset(r, 0, sizeof *r);
  if(!end) {
    return -1;
  }
  r->method = LINK_OTHER;
  for(size_t i=0; i < sizeof methods / sizeof *methods; i++) {
    if(!strncmp(buf, methods[i].name, strlen(methods[i].name))) {
      r->method = methods[i].method;
      at = buf + strlen(methods[i].name);
    }
  }
  if(r->method == LINK_OTHER) {
    at = memchr(buf, ' ', end);
    if(!at) {
      return -1;
    }
    at++;
  }
  // the path, up to its query or the version
  for(n=0; at + n < buf + end && at[n] != ' ' && at[n] != '?' && at[n] != '\r'; n++) {
  }
  if(at[0] != '/' && at[0] != '*') {
    return -1;
  }
  if(n < sizeof r->path) {
    memcpy(r->path, at, n);
  }
  for(line = memchr(buf, '\n', end) + 1; line < buf + end - 2; line = next + 2) {
    next = line;
    while(next < buf + end - 2 && !(next[0] == '\r' && next[1] == '\n')) {
      next++;
    }
    n = (size_t)(next - line);
    if(is_header(line, n, "origin")) {
      value = header_value(line, n, &k);
      if(k < sizeof r->origin) {
        memcpy(r->origin, value, k);
      } else {
        memcpy(r->origin, "?", 2); // too long to be an extension's: refused
      }
    }
  }
  r->origin_ok = !r->origin[0] || origin_allowed(r->origin);
  r->body = buf + end;
  r->body_len = len - end;
  return 0;
}


/**
 * whether s, of len bytes, is from min to max hex digits
 **/
static int
is_hex(const char *s, size_t len, size_t min, size_t max) {
  if(len < min || len > max) {
    return 0;
  }
  for(size_t i=0; i < len; i++) {
    if(!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) {
      return 0;
    }
  }
  return 1;
}


int
link_parse_session(const char *body, size_t len, char code[LINK_CODE_LEN + 1],
                   char *auth_token, size_t auth_size, char *ct0, size_t ct0_size) {
  static char text[BODY_MAX];
  const json_node_t *root, *c, *a, *t;
  json_doc_t *doc;
  size_t err;
  int ret = 0;

  memset(code, 0, LINK_CODE_LEN + 1);
  memset(auth_token, 0, auth_size);
  memset(ct0, 0, ct0_size);
  if(len >= sizeof text) {
    return -1;
  }
  memcpy(text, body, len);
  text[len] = 0;
  if(!(doc=json_parse(text, len, &err))) {
    memset(text, 0, sizeof text);
    return -1;
  }
  root = json_root(doc);
  c = json_get(root, "code");
  a = json_get(root, "auth_token");
  t = json_get(root, "ct0");
  if(!json_string(c) || !json_string(a) || !json_string(t)) {
    ret = -1;
  } else if(!is_hex(a->str, a->len, 20, auth_size - 1 < 127 ? auth_size - 1 : 127) ||
            !is_hex(t->str, t->len, 20, ct0_size - 1 < 255 ? ct0_size - 1 : 255)) {
    ret = -2;
  } else if(c->len != LINK_CODE_LEN || strspn(c->str, "0123456789") != LINK_CODE_LEN) {
    ret = -3;
  } else {
    memcpy(code, c->str, LINK_CODE_LEN);
    memcpy(auth_token, a->str, a->len);
    memcpy(ct0, t->str, t->len);
  }
  json_free(doc);
  memset(text, 0, sizeof text);
  return ret;
}


static const char *
status_text(int status) {
  switch(status) {
  case 200: return "OK";
  case 204: return "No Content";
  case 400: return "Bad Request";
  case 401: return "Unauthorized";
  case 403: return "Forbidden";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 413: return "Content Too Large";
  case 500: return "Internal Server Error";
  case 502: return "Bad Gateway";
  case 503: return "Service Unavailable";
  default:  return "Unknown";
  }
}


int
link_response(char *buf, size_t size, int status, const char *origin, const char *json) {
  size_t body = json ? strlen(json) : 0;
  int n;

  n = snprintf(buf, size,
               "HTTP/1.1 %d %s\r\n"
               "%s"
               "Cache-Control: no-store\r\n"
               "%s%s%s"
               "Vary: Origin\r\n"
               "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
               "Access-Control-Allow-Headers: Content-Type\r\n"
               "Access-Control-Allow-Private-Network: true\r\n"
               "Access-Control-Max-Age: 60\r\n"
               "Content-Length: %zu\r\n"
               "Connection: close\r\n"
               "\r\n"
               "%s",
               status, status_text(status), json ? "Content-Type: application/json\r\n" : "",
               origin && origin_allowed(origin) ? "Access-Control-Allow-Origin: " : "",
               origin && origin_allowed(origin) ? origin : "",
               origin && origin_allowed(origin) ? "\r\n" : "", body, json ? json : "");
  return n < 0 || (size_t)n >= size ? -1 : n;
}


int
link_file_head(char *buf, size_t size, const char *type, size_t len) {
  int n = snprintf(buf, size,
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "Cache-Control: no-store\r\n"
                   "X-Content-Type-Options: nosniff\r\n"
                   "Referrer-Policy: no-referrer\r\n"
                   "Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self'; "
                   "base-uri 'none'; form-action 'none'; frame-ancestors 'none'\r\n"
                   "X-Frame-Options: DENY\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   type, len);

  return n < 0 || (size_t)n >= size ? -1 : n;
}


unsigned long long
link_mix(unsigned long long z) {
  z += 0x9e3779b97f4a7c15ull;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}


void
link_code(unsigned long long bits, char code[LINK_CODE_LEN + 1]) {
  unsigned v = (unsigned)(link_mix(bits) % 1000000ull);

  for(int i=LINK_CODE_LEN - 1; i >= 0; i--) {
    code[i] = (char)('0' + v % 10);
    v /= 10;
  }
  code[LINK_CODE_LEN] = 0;
}


int
link_same(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b), n = la > lb ? la : lb;
  unsigned diff = la != lb;

  for(size_t i=0; i < n; i++) {
    diff |= (unsigned)(unsigned char)(i < la ? a[i] : 0) ^ (unsigned char)(i < lb ? b[i] : 0);
  }
  return !diff;
}


int
link_session_text(char *buf, size_t size, const char *auth_token, const char *ct0) {
  int n = snprintf(buf, size, "auth_token=%s\nct0=%s\n", auth_token, ct0);

  return n < 0 || (size_t)n >= size ? -1 : n;
}


/**
 * whether text is "-" (then emptied) or made only of the characters in
 * allowed, at least min of them
 **/
static int
state_text(char *text, const char *allowed, size_t min) {
  size_t n = strlen(text);

  if(!strcmp(text, "-")) {
    text[0] = 0;
    return 1;
  }
  return n >= min && strspn(text, allowed) == n;
}


int
link_state_line(char *buf, size_t size, const link_state_t *s) {
  int n = snprintf(buf, size, "link %d %d %d %d %d %d %d %s %s %s %d\n", s->state, s->last, s->wrong,
                   s->renewed, s->refused, s->error, s->account, s->ip[0] ? s->ip : "-",
                   s->code[0] ? s->code : "-", s->user[0] ? s->user : "-", s->seen);

  return n < 0 || (size_t)n >= size ? -1 : n;
}


int
link_state_parse(const char *line, link_state_t *s) {
  link_state_t t;
  const char *rest;
  int end = -1, more = -1;

  memset(&t, 0, sizeof t);
  memset(s, 0, sizeof *s);
  if(sscanf(line, "link %d %d %d %d %d %d %d %15s %6s %15s%n", &t.state, &t.last, &t.wrong, &t.renewed,
            &t.refused, &t.error, &t.account, t.ip, t.code, t.user, &end) != 10 || end < 0) {
    return -1;
  }
  rest = line + end;
  if(*rest == ' ') {
    if(sscanf(rest, " %d%n", &t.seen, &more) != 1 || more < 0) {
      return -1;
    }
    rest += more;
  }
  if(*rest && strcmp(rest, "\n")) {
    return -1;
  }
  if(t.state < LINK_STARTING || t.state > LINK_NO_NETWORK || t.last < LINK_FINE || t.last > LINK_UNWRITTEN ||
     t.wrong < 0 || t.wrong > LINK_TRIES || (t.renewed != 0 && t.renewed != 1) || (t.seen != 0 && t.seen != 1) ||
     !state_text(t.ip, "0123456789.", 7) || !state_text(t.code, "0123456789", LINK_CODE_LEN) ||
     !state_text(t.user, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", 1)) {
    return -1;
  }
  *s = t;
  return 0;
}
