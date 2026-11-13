/* Small JSON reader for the payloads, see json.h. Nodes come from blocks
that are freed together; nesting deeper than MAX_DEPTH is rejected so a
hostile document cannot exhaust the stack. */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "mem.h"

#define BLOCK_NODES 4096
#define MAX_DEPTH   256


typedef struct block {
  struct block *next;
  size_t        used;
  json_node_t   nodes[BLOCK_NODES];
} block_t;

struct json_doc {
  block_t     *blocks;
  json_node_t *root;
};

typedef struct parser {
  char       *p;
  char       *end;
  json_doc_t *doc;
  int         depth;
  int         why; // JSON_ERR_*
} parser_t;


static json_node_t *parse_value(parser_t *ps);


static json_node_t *
new_node(parser_t *ps, json_type_t type) {
  block_t *b = ps->doc->blocks;
  json_node_t *n;

  if(!b || b->used == BLOCK_NODES) {
    if(!(b=mem_alloc(sizeof *b))) {
      ps->why = JSON_ERR_MEMORY;
      return NULL;
    }
    b->next = ps->doc->blocks;
    b->used = 0;
    ps->doc->blocks = b;
  }

  n = &b->nodes[b->used++];
  memset(n, 0, sizeof *n);
  n->type = type;
  return n;
}


static void
skip_space(parser_t *ps) {
  while(ps->p < ps->end &&
        (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) {
    ps->p++;
  }
}


static int
hex4(const char *s, unsigned *out) {
  unsigned v = 0;

  for(int i=0; i<4; i++) {
    char c = s[i];
    v <<= 4;
    if(c >= '0' && c <= '9') {
      v |= c - '0';
    } else if(c >= 'a' && c <= 'f') {
      v |= c - 'a' + 10;
    } else if(c >= 'A' && c <= 'F') {
      v |= c - 'A' + 10;
    } else {
      return -1;
    }
  }
  *out = v;
  return 0;
}


static char *
put_utf8(char *w, unsigned c) {
  if(c < 0x80) {
    *w++ = c;
  } else if(c < 0x800) {
    *w++ = 0xC0 | (c >> 6);
    *w++ = 0x80 | (c & 0x3F);
  } else if(c < 0x10000) {
    *w++ = 0xE0 | (c >> 12);
    *w++ = 0x80 | ((c >> 6) & 0x3F);
    *w++ = 0x80 | (c & 0x3F);
  } else {
    *w++ = 0xF0 | (c >> 18);
    *w++ = 0x80 | ((c >> 12) & 0x3F);
    *w++ = 0x80 | ((c >> 6) & 0x3F);
    *w++ = 0x80 | (c & 0x3F);
  }
  return w;
}


/**
 * decode the string that starts after the opening quote; the decoded text
 * is never longer than its escaped form, so it is written over the input
 * and ends with a NUL where the closing quote (or earlier) was
 **/
static const char *
parse_string(parser_t *ps, size_t *len) {
  char *start = ps->p;
  char *w = ps->p;
  unsigned cp, lo;
  char c;

  while(ps->p < ps->end) {
    c = *ps->p++;
    if(c == '"') {
      *w = 0;
      *len = w - start;
      return start;
    }
    if((unsigned char)c < 0x20) {
      return NULL;
    }
    if(c != '\\') {
      *w++ = c;
      continue;
    }
    if(ps->p >= ps->end) {
      return NULL;
    }
    switch((c=*ps->p++)) {
    case '"':
    case '\\':
    case '/': *w++ = c; break;
    case 'b': *w++ = '\b'; break;
    case 'f': *w++ = '\f'; break;
    case 'n': *w++ = '\n'; break;
    case 'r': *w++ = '\r'; break;
    case 't': *w++ = '\t'; break;
    case 'u':
      if(ps->end - ps->p < 4 || hex4(ps->p, &cp)) {
        return NULL;
      }
      ps->p += 4;
      if(cp >= 0xD800 && cp < 0xDC00) {
        // a high surrogate needs its low half right after it
        if(ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u' &&
           !hex4(ps->p + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          ps->p += 6;
        } else {
          cp = 0xFFFD;
        }
      } else if(cp >= 0xDC00 && cp < 0xE000) {
        cp = 0xFFFD;
      }
      w = put_utf8(w, cp);
      break;
    default:
      return NULL;
    }
  }
  return NULL;
}


static json_node_t *
parse_literal(parser_t *ps, const char *word, json_type_t type) {
  size_t n = strlen(word);

  if((size_t)(ps->end - ps->p) < n || memcmp(ps->p, word, n)) {
    return NULL;
  }
  ps->p += n;
  return new_node(ps, type);
}


static json_node_t *
parse_number(parser_t *ps) {
  char *start = ps->p;
  json_node_t *n;

  while(ps->p < ps->end && strchr("+-0123456789.eE", *ps->p) && *ps->p) {
    ps->p++;
  }
  if(ps->p == start || !(n=new_node(ps, JSON_NUMBER))) {
    return NULL;
  }
  n->str = start;
  n->len = ps->p - start;
  return n;
}


static json_node_t *
parse_container(parser_t *ps, json_type_t type, char close) {
  json_node_t *node, *child, **tail;
  const char *key = NULL;
  size_t len;

  if(++ps->depth > MAX_DEPTH) {
    ps->why = JSON_ERR_DEPTH;
    return NULL;
  }
  if(!(node=new_node(ps, type))) {
    return NULL;
  }
  tail = &node->child;
  ps->p++;

  skip_space(ps);
  if(ps->p < ps->end && *ps->p == close) {
    ps->p++;
    ps->depth--;
    return node;
  }

  for(;;) {
    if(type == JSON_OBJECT) {
      skip_space(ps);
      if(ps->p >= ps->end || *ps->p != '"') {
        return NULL;
      }
      ps->p++;
      if(!(key=parse_string(ps, &len))) {
        return NULL;
      }
      skip_space(ps);
      if(ps->p >= ps->end || *ps->p != ':') {
        return NULL;
      }
      ps->p++;
    }
    if(!(child=parse_value(ps))) {
      return NULL;
    }
    child->key = key;
    *tail = child;
    tail = &child->next;

    skip_space(ps);
    if(ps->p >= ps->end) {
      return NULL;
    }
    if(*ps->p == ',') {
      ps->p++;
    } else if(*ps->p == close) {
      ps->p++;
      ps->depth--;
      return node;
    } else {
      return NULL;
    }
  }
}


static json_node_t *
parse_value(parser_t *ps) {
  json_node_t *n;

  skip_space(ps);
  if(ps->p >= ps->end) {
    return NULL;
  }

  switch(*ps->p) {
  case '{': return parse_container(ps, JSON_OBJECT, '}');
  case '[': return parse_container(ps, JSON_ARRAY, ']');
  case 't': return parse_literal(ps, "true", JSON_TRUE);
  case 'f': return parse_literal(ps, "false", JSON_FALSE);
  case 'n': return parse_literal(ps, "null", JSON_NULL);
  case '"':
    ps->p++;
    if(!(n=new_node(ps, JSON_STRING)) || !(n->str=parse_string(ps, &n->len))) {
      return NULL;
    }
    return n;
  default:
    return parse_number(ps);
  }
}


json_doc_t *
json_parse_why(char *text, size_t len, size_t *err_off, int *why) {
  parser_t ps = {.p = text, .end = text + len, .why = JSON_ERR_SYNTAX};

  if(!(ps.doc=mem_calloc(1, sizeof *ps.doc))) {
    *err_off = 0;
    *why = JSON_ERR_MEMORY;
    return NULL;
  }

  if((ps.doc->root=parse_value(&ps))) {
    skip_space(&ps);
    if(ps.p == ps.end) {
      return ps.doc;
    }
  }

  *err_off = ps.p - text;
  *why = ps.why;
  json_free(ps.doc);
  return NULL;
}


json_doc_t *
json_parse(char *text, size_t len, size_t *err_off) {
  int why;

  return json_parse_why(text, len, err_off, &why);
}


const json_node_t *
json_root(const json_doc_t *doc) {
  return doc->root;
}


void
json_free(json_doc_t *doc) {
  block_t *b, *next;

  if(!doc) {
    return;
  }
  for(b=doc->blocks; b; b=next) {
    next = b->next;
    mem_free(b);
  }
  mem_free(doc);
}


const json_node_t *
json_get(const json_node_t *obj, const char *key) {
  const json_node_t *n;

  if(!obj || obj->type != JSON_OBJECT) {
    return NULL;
  }
  for(n=obj->child; n; n=n->next) {
    if(!strcmp(n->key, key)) {
      return n;
    }
  }
  return NULL;
}


const json_node_t *
json_path(const json_node_t *node, ...) {
  const char *key;
  va_list args;

  va_start(args, node);
  while(node && (key=va_arg(args, const char*))) {
    node = json_get(node, key);
  }
  va_end(args);
  return node;
}


const char *
json_string(const json_node_t *node) {
  return node && node->type == JSON_STRING ? node->str : NULL;
}
