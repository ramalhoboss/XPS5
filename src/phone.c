/* Writing on the phone: what the title and the payload say to each other,
and what the page sends; see phone.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "mem.h"
#include "phone.h"

static const char g_hexd[] = "0123456789abcdef";


int
phone_state_line(char *buf, size_t size, const phone_state_t *s, size_t len) {
  int n = snprintf(buf, size, "type %d %d %d %d %u %s %s %zu\n", s->state, s->error, !!s->seen, !!s->done,
                   s->seq, s->ip[0] ? s->ip : "-", s->token[0] ? s->token : "-", len);

  return n < 0 || (size_t)n >= size ? -1 : n;
}


int
phone_state_parse(const char *line, phone_state_t *s, size_t *len) {
  char ip[16], token[PHONE_TOKEN_LEN + 2];
  unsigned long long l;
  int seen, done;

  memset(s, 0, sizeof *s);
  *len = 0;
  if(sscanf(line, "type %d %d %d %d %u %15s %17s %llu", &s->state, &s->error, &seen, &done, &s->seq, ip, token,
            &l) != 8 || s->state < PHONE_STARTING || s->state > PHONE_NO_NETWORK || l > PHONE_TEXT_MAX ||
     strlen(token) > PHONE_TOKEN_LEN) {
    memset(s, 0, sizeof *s);
    return -1;
  }
  s->seen = !!seen;
  s->done = !!done;
  snprintf(s->ip, sizeof s->ip, "%s", strcmp(ip, "-") ? ip : "");
  snprintf(s->token, sizeof s->token, "%s", strcmp(token, "-") ? token : "");
  *len = (size_t)l;
  return 0;
}


void
phone_token(unsigned long long bits, char token[PHONE_TOKEN_LEN + 1]) {
  for(int i=0; i < PHONE_TOKEN_LEN; i++) {
    token[i] = g_hexd[(bits >> (4 * i)) & 15];
  }
  token[PHONE_TOKEN_LEN] = 0;
}


int
phone_origin_ok(const char *origin, const char *ip) {
  char own[48];

  if(!origin || !origin[0]) {
    return 1;
  }
  snprintf(own, sizeof own, "http://%s:%d", ip, PHONE_PORT);
  return ip[0] && !strcmp(origin, own);
}


/**
 * how many of the first n bytes of s end on a whole UTF-8 character
 **/
static size_t
whole(const char *s, size_t n) {
  size_t k = n;

  // back over the bytes that continue a character, then the one it began with
  while(k && ((unsigned char)s[k - 1] & 0xc0) == 0x80) {
    k--;
  }
  if(k && (unsigned char)s[k - 1] >= 0xc0) {
    unsigned char c = (unsigned char)s[k - 1];
    size_t need = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : 2;
    return n - (k - 1) >= need ? n : k - 1;
  }
  return n;
}


int
phone_parse_body(const char *body, size_t len, char token[PHONE_TOKEN_LEN + 1], char *text, size_t size,
                 int *has_text, int *done) {
  json_doc_t *doc;
  const json_node_t *root, *n;
  const char *t;
  char *copy;
  size_t off, k;
  int ret = -1;

  token[0] = 0;
  if(text && size) {
    text[0] = 0;
  }
  *has_text = *done = 0;
  if(!(copy=mem_alloc(len + 1))) {
    return -1;
  }
  memcpy(copy, body, len);
  copy[len] = 0;
  if((doc=json_parse(copy, len, &off))) {
    root = json_root(doc);
    t = json_string(json_get(root, "token"));
    if(t && strlen(t) == PHONE_TOKEN_LEN && strspn(t, g_hexd) == PHONE_TOKEN_LEN) {
      snprintf(token, PHONE_TOKEN_LEN + 1, "%s", t);
      ret = 0;
      if(text && size && (t=json_string(json_get(root, "text")))) {
        k = whole(t, strlen(t) < size - 1 ? strlen(t) : size - 1);
        memcpy(text, t, k);
        text[k] = 0;
        *has_text = 1;
      }
      n = json_get(root, "done");
      *done = n && n->type == JSON_TRUE;
    }
    json_free(doc);
  }
  memset(copy, 0, len);
  mem_free(copy);
  return ret;
}


/**
 * s as a JSON string's inside, appended at *n; 0, or -1 when it does not fit
 **/
static int
put_json(char *buf, size_t size, size_t *n, const char *s) {
  for(; *s; s++) {
    unsigned char c = (unsigned char)*s;
    char esc[8];
    size_t l;

    if(c == '"' || c == '\\') {
      esc[0] = '\\';
      esc[1] = (char)c;
      l = 2;
    } else if(c == '\n') {
      memcpy(esc, "\\n", 2);
      l = 2;
    } else if(c < 0x20) {
      snprintf(esc, sizeof esc, "\\u%04x", c);
      l = 6;
    } else {
      esc[0] = (char)c;
      l = 1;
    }
    if(*n + l >= size) {
      return -1;
    }
    memcpy(buf + *n, esc, l);
    *n += l;
  }
  buf[*n] = 0;
  return 0;
}


int
phone_draft_json(char *buf, size_t size, const char *text, int max, const char *what, const char *lang,
                 unsigned seq) {
  size_t n = 0;
  int k;

  if(size < 16) {
    return -1;
  }
  memcpy(buf, "{\"text\":\"", 9);
  n = 9;
  if(put_json(buf, size, &n, text)) {
    return -1;
  }
  k = snprintf(buf + n, size - n, "\",\"max\":%d,\"what\":\"%s\",\"lang\":\"%s\",\"seq\":%u}", max, what, lang, seq);
  if(k < 0 || (size_t)k >= size - n) {
    return -1;
  }
  return (int)(n + (size_t)k);
}


int
phone_hex(const char *text, char *buf, size_t size) {
  size_t len = strlen(text);

  if(!len) {
    if(size < 2) {
      return -1;
    }
    memcpy(buf, "-", 2);
    return 1;
  }
  if(2 * len + 1 > size) {
    return -1;
  }
  for(size_t i=0; i < len; i++) {
    buf[2 * i] = g_hexd[(unsigned char)text[i] >> 4];
    buf[2 * i + 1] = g_hexd[(unsigned char)text[i] & 15];
  }
  buf[2 * len] = 0;
  return (int)(2 * len);
}


static int
nibble(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
         c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}


int
phone_unhex(const char *hex, char *text, size_t size) {
  size_t len = strlen(hex), n;

  if(!size) {
    return -1;
  }
  text[0] = 0;
  if(!strcmp(hex, "-")) {
    return 0;
  }
  if(len % 2) {
    return -1;
  }
  n = len / 2 < size - 1 ? len / 2 : size - 1;
  for(size_t i=0; i < n; i++) {
    int a = nibble(hex[2 * i]), b = nibble(hex[2 * i + 1]);
    if(a < 0 || b < 0 || !(a | b)) { // no 0 byte in a text
      text[0] = 0;
      return -1;
    }
    text[i] = (char)(a << 4 | b);
  }
  n = whole(text, n);
  text[n] = 0;
  return (int)n;
}
