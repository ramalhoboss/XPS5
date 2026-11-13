/* The PS5's trophies, see trophy.h. Every number in these files is big
endian; nothing in them is trusted: anything out of place makes it -1. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "trophy.h"

#define NPBIND_MAGIC   0xd294a018u
#define NPBIND_RECORDS 0x80     // where its records start: tag, size, data
#define NPBIND_SET     0x0010   // the tag of the set's name
#define UCP_MAGIC      0xb228c60au
#define T2_VERSION     0x10000u
#define T2_HEADER      64       // TRPTITLE.DAT's, before its pages
#define T2_PAGE        1024     // each page's data, then its SHA-256
#define T2_STRIDE      (T2_PAGE + 32)
#define T2_STREAM      48       // where the pages' directory starts
#define T2_ENTRY       32       // bytes of an entry of the directory
#define T2_STATES      0x800u   // the entry, and the rows, of the trophies' states
#define T2_EARNED      0x11u


static unsigned
be16(const unsigned char *p) {
  return (unsigned)p[0] << 8 | p[1];
}


static unsigned
be32(const unsigned char *p) {
  return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}


static unsigned long long
be64(const unsigned char *p) {
  return (unsigned long long)be32(p) << 32 | be32(p + 4);
}


static int
digits(const char *s, int n) {
  for(int i=0; i < n; i++) {
    if(s[i] < '0' || s[i] > '9') {
      return 0;
    }
  }
  return 1;
}


int
trophy_set_of(const unsigned char *data, size_t len, char set[TROPHY_SET_LEN]) {
  size_t at = NPBIND_RECORDS, n;
  unsigned tag, size;

  set[0] = 0;
  if(len < NPBIND_RECORDS + 4 || be32(data) != NPBIND_MAGIC) {
    return -1;
  }
  while(at + 4 <= len) {
    tag = be16(data + at);
    size = be16(data + at + 2);
    if(at + 4 + size > len) {
      break;
    }
    if(tag == NPBIND_SET) {
      for(n=0; n < size && data[at + 4 + n]; n++) {
      }
      if(n != 12 || memcmp(data + at + 4, "NPWR", 4) || !digits((const char *)data + at + 8, 5) ||
         data[at + 13] != '_' || !digits((const char *)data + at + 14, 2)) {
        return -1;
      }
      memcpy(set, data + at + 4, n);
      set[n] = 0;
      return 0;
    }
    at += 4 + size;
  }
  return -1;
}


int
trophy_earned(const unsigned char *data, size_t len, trophy_earned_t *e) {
  unsigned long long pages, offset, stride, slen;
  unsigned count, payload, version, repeat, number;
  unsigned char *stream;
  int found = -1;

  memset(e, 0, sizeof *e);
  if(len < T2_HEADER || memcmp(data, "T2PD", 4) || be32(data + 4) != T2_VERSION || be64(data + 8) != T2_STRIDE) {
    return -1;
  }
  pages = be64(data + 32);
  if(!pages || pages > TROPHY_RECORD_MOST / T2_STRIDE || len != T2_HEADER + pages * T2_STRIDE) {
    return -1;
  }
  slen = pages * T2_PAGE;
  if(!(stream=malloc(slen))) {
    return -1;
  }
  // the pages' data, one after the other
  for(unsigned long long p=0; p < pages; p++) {
    memcpy(stream + p * T2_PAGE, data + T2_HEADER + p * T2_STRIDE, T2_PAGE);
  }
  if(memcmp(stream, "T2TD", 4) || be32(stream + 4) != T2_VERSION || !(count=be32(stream + 8)) || count > 64 ||
     T2_STREAM + (unsigned long long)count * T2_ENTRY > slen) {
    goto out;
  }
  for(unsigned i=0; i < count; i++) {
    const unsigned char *at = stream + T2_STREAM + i * T2_ENTRY;
    if(be32(at) != T2_STATES) {
      continue;
    }
    payload = be32(at + 4);
    version = be32(at + 8);
    repeat = be32(at + 12);
    offset = be64(at + 16);
    stride = 16ull + payload;
    if(version != 1 || payload < 0x30 || !repeat || repeat > 4096 || offset > slen ||
       repeat * stride > slen - offset) {
      goto out;
    }
    for(unsigned r=0; r < repeat; r++) {
      const unsigned char *row = stream + offset + r * stride;
      if(be32(row) != T2_STATES) {
        e->count = 0;
        memset(e->bits, 0, sizeof e->bits);
        goto out;
      }
      number = be32(row + 0x10);
      if(be32(row + 0x14) == T2_EARNED && be64(row + 0x20) && be64(row + 0x30) && number < TROPHY_IDS &&
         !trophy_has(e, (int)number)) {
        e->bits[number / 8] |= (unsigned char)(1u << (number % 8));
        e->count++;
      }
    }
    found = e->count;
    goto out;
  }
out:
  free(stream);
  if(found < 0) {
    memset(e, 0, sizeof *e);
  }
  return found;
}


int
trophy_has(const trophy_earned_t *e, int id) {
  return id >= 0 && id < TROPHY_IDS && (e->bits[id / 8] >> (id % 8) & 1);
}


int
trophy_ucp_files(const unsigned char *head, size_t len) {
  unsigned count;

  if(len < TROPHY_UCP_HEAD || be32(head) != UCP_MAGIC || be32(head + 0x14) != TROPHY_UCP_ENTRY) {
    return -1;
  }
  count = be32(head + 0x10);
  return count && count <= TROPHY_UCP_MOST ? (int)count : -1;
}


/**
 * the name of the table's entry i, NUL after it; 0, or -1 when it cannot
 * be a file's
 **/
static int
entry_name(const unsigned char *table, int i, char name[33]) {
  const unsigned char *at = table + (size_t)i * TROPHY_UCP_ENTRY;
  int n;

  for(n=0; n < 32 && at[n]; n++) {
    if(at[n] <= 0x20 || at[n] >= 0x7f || at[n] == '/' || at[n] == '\\') {
      return -1;
    }
    name[n] = (char)at[n];
  }
  name[n] = 0;
  return n ? 0 : -1;
}


int
trophy_ucp_find(const unsigned char *table, size_t len, int count, const char *name,
                unsigned long long *off, unsigned long long *size) {
  char at[33];

  if(count <= 0 || len < (size_t)count * TROPHY_UCP_ENTRY) {
    return -1;
  }
  for(int i=0; i < count; i++) {
    if(!entry_name(table, i, at) && !strcmp(at, name)) {
      *off = be64(table + (size_t)i * TROPHY_UCP_ENTRY + 32);
      *size = be64(table + (size_t)i * TROPHY_UCP_ENTRY + 40);
      return 0;
    }
  }
  return -1;
}


static int
is_meta(const char *name, const char *start) {
  size_t n = strlen(name), k = strlen(start);

  return n > k + 5 && !strncmp(name, start, k) && !strcmp(name + n - 5, ".json");
}


int
trophy_ucp_meta(const unsigned char *table, size_t len, int count, const char *locale, char *name,
                size_t size) {
  const char *const wanted[] = {locale, "en-US", "en-GB"};
  char at[33], file[48];
  int any = -1, english = -1;

  if(count <= 0 || len < (size_t)count * TROPHY_UCP_ENTRY) {
    return -1;
  }
  for(size_t w=0; w < sizeof wanted / sizeof wanted[0]; w++) {
    snprintf(file, sizeof file, "tropmeta_%s.json", wanted[w] ? wanted[w] : "");
    for(int i=0; i < count; i++) {
      if(!entry_name(table, i, at) && !strcmp(at, file)) {
        snprintf(name, size, "%s", at);
        return 0;
      }
    }
  }
  for(int i=0; i < count; i++) {
    if(!entry_name(table, i, at)) {
      english = english < 0 && is_meta(at, "tropmeta_en") ? i : english;
      any = any < 0 && is_meta(at, "tropmeta_") ? i : any;
    }
  }
  if(english < 0 && any < 0) {
    return -1;
  }
  entry_name(table, english >= 0 ? english : any, at);
  snprintf(name, size, "%s", at);
  return 0;
}


/**
 * the number a node holds, as a number or as text, or -1
 **/
static long
number_of(const json_node_t *n) {
  long v = 0;

  if(!n || (n->type != JSON_NUMBER && n->type != JSON_STRING) || !n->len || n->len > 9) {
    return -1;
  }
  for(size_t i=0; i < n->len; i++) {
    if(n->str[i] < '0' || n->str[i] > '9') {
      return -1;
    }
    v = v * 10 + (n->str[i] - '0');
  }
  return v;
}


int
trophy_grade(char *conf, size_t len, int id) {
  const json_node_t *list, *item;
  const char *grade;
  json_doc_t *doc;
  size_t off;
  int ret = -1;

  if(!(doc=json_parse(conf, len, &off))) {
    return -1;
  }
  if((list=json_get(json_root(doc), "trophies")) && list->type == JSON_ARRAY) {
    for(item=list->child; item && ret < 0; item=item->next) {
      if(item->type == JSON_OBJECT && number_of(json_get(item, "id")) == id &&
         (grade=json_string(json_get(item, "grade")))) {
        ret = grade[0] == 'P' || grade[0] == 'p' ? TROPHY_PLATINUM : grade[0] == 'G' || grade[0] == 'g' ? TROPHY_GOLD :
              grade[0] == 'S' || grade[0] == 's' ? TROPHY_SILVER : grade[0] == 'B' || grade[0] == 'b' ? TROPHY_BRONZE : -1;
      }
    }
  }
  json_free(doc);
  return ret;
}


/**
 * text into out, its control characters as spaces and the spaces at its
 * ends left out, cut at a whole character
 **/
static void
clean(const char *text, char *out, size_t size) {
  size_t n = 0, start = 0, end;

  out[0] = 0;
  if(!text || !size) {
    return;
  }
  end = strlen(text);
  while(start < end && (unsigned char)text[start] <= 0x20) {
    start++;
  }
  while(end > start && (unsigned char)text[end - 1] <= 0x20) {
    end--;
  }
  for(size_t i=start; i < end && n + 1 < size; i++) {
    out[n++] = (unsigned char)text[i] < 0x20 || text[i] == 0x7f ? ' ' : text[i];
  }
  // cut short: not in the middle of a character of UTF-8
  if(n < end - start) {
    while(n > 0 && ((unsigned char)text[start + n] & 0xc0) == 0x80) {
      n--;
    }
  }
  out[n] = 0;
}


int
trophy_names(char *meta, size_t len, int id, char *name, size_t size, char *game, size_t game_size) {
  const json_node_t *data, *list, *item;
  json_doc_t *doc;
  size_t off;
  int ret = -1;

  name[0] = game[0] = 0;
  if(!(doc=json_parse(meta, len, &off))) {
    return -1;
  }
  if((data=json_get(json_root(doc), "metadata"))) {
    clean(json_string(json_path(data, "titleMetadata", "name", NULL)), game, game_size);
    if((list=json_get(data, "trophyMetadata")) && list->type == JSON_ARRAY) {
      for(item=list->child; item && ret < 0; item=item->next) {
        if(item->type == JSON_OBJECT && number_of(json_get(item, "id")) == id) {
          clean(json_string(json_get(item, "name")), name, size);
          ret = name[0] ? 0 : -1;
        }
      }
    }
  }
  json_free(doc);
  return ret;
}
