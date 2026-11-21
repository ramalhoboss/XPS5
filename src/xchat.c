/* X Chat's inbox, see xchat.h. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "xchat.h"

// GetInitialXChatPageQuery, and the settings x.com's Chat asks with
#define XCHAT_INBOX "https://api.x.com/graphql/Gl7r1aY59L7jLBjVC98lqg/GetInitialXChatPageQuery?variables="
#define XCHAT_VARIABLES "%7B%22query_settings%22%3A%7B%22inbox_conversation_event_limit%22%3A5" \
  "%2C%22inbox_conversation_limit%22%3A20%2C%22conversation_event_limit%22%3A200" \
  "%2C%22user_event_limit%22%3A500%7D%2C%22message_pull_version%22%3A1761251295%7D"

// Thrift's binary protocol: the types of its fields
enum { T_STOP = 0, T_BOOL = 2, T_BYTE = 3, T_DOUBLE = 4, T_I16 = 6, T_I32 = 8, T_I64 = 10, T_STRING = 11,
       T_STRUCT = 12, T_MAP = 13, T_SET = 14, T_LIST = 15 };

#define THRIFT_DEPTH 16
#define EVENT_MAX    (256 << 10) // bytes of an event, decoded

// what an event of a conversation is: a message event (a message, a
// reaction, an edit, a call...: entry says which, once opened), a read, a
// key change (its keys go to the hook as it is read), messages taken back,
// a change of the group's or of how long messages last, or something else
enum { EV_OTHER, EV_MESSAGE, EV_READ, EV_KEY, EV_DELETE, EV_GROUP };

#define DELETED_MAX 16 // messages one event takes back that are kept

typedef struct event {
  int       kind;               // EV_*
  char      seq[32];            // its sequence number, digits
  char      sender[24];
  char      message_id[64];     // for a per-message key
  long long msec;               // when, in milliseconds
  int       sealed;             // a message event whose contents are encrypted and not opened
  int       encrypted;          // its contents came encrypted, opened or not
  int       enc_version;        // its contents_encryption_version (0 when none)
  int       has_enc_version;    // the event gives it
  xchat_entry_t entry;          // what it is, when it is known (its attachment's key, when encrypted)
  // what its signature covers and the signature (MessageEventSignature),
  // while decode_event reads it: contents points into the event's bytes
  char      conversation[X_ID_MAX]; // its own conversation id (field 4)
  int       has_conversation;
  char      key_version[32];    // the conversation key's version (field 101)
  int       has_key_version;
  const unsigned char *contents;
  size_t    contents_len;
  char      sig[128];           // base64 of r || s
  char      sig_key[32];        // public_key_version
  char      sig_version[16];
  int       has_sig;
  int       sig_cut;            // a part it covers did not fit here whole: not checked
  int       signature;          // XCHAT_SIG_*, once checked
  char      seen_until[32];     // a read one: up to which sequence number
  char      deleted[DELETED_MAX][32]; // taken back: their sequence numbers
  int       deleted_count;
  int       for_all;            // for everyone, not only for who took them back
  int       group;              // a change of the group's: X_EV_*
  long long group_n;            // how many people, or seconds
  char      group_text[128];    // a title
  char      group_who[24];      // the one added or removed, when only one was
} event_t;


/**
 * the icon (X_DM_*) for a message whose first attachment is of that kind
 **/
static int
media_icon(int kind) {
  return kind == 1 || kind == 6 ? X_DM_PHOTO : kind == 2 ? X_DM_GIF : kind == 3 ? X_DM_VIDEO :
         kind == 4 ? X_DM_VOICE : kind == 5 ? X_DM_FILE : X_DM_TEXT;
}

static xchat_hooks_t g_hooks;

void
xchat_set_hooks(const xchat_hooks_t *hooks) {
  g_hooks = hooks ? *hooks : (xchat_hooks_t){0};
}

typedef struct thrift {
  const unsigned char *p;
  size_t len, at;
  int bad;
} thrift_t;


int
xchat_inbox_url(char *buf, size_t size) {
  return snprintf(buf, size, "%s%s", XCHAT_INBOX, XCHAT_VARIABLES) >= (int)size ? -1 : 0;
}


int
xchat_headers(const x_request_t *r, const char *uuid, const char *tid, https_header_t *headers) {
  int n = X_HEADER_COUNT;

  memcpy(headers, r->headers, sizeof r->headers);
  for(int i=0; i < n; i++) {
    if(!strcmp(headers[i].name, "referer")) {
      headers[i].value = "https://x.com/i/chat";
    }
  }
  headers[n++] = (https_header_t){"origin", "https://x.com"};
  if(uuid && uuid[0]) {
    headers[n++] = (https_header_t){"x-client-uuid", uuid};
  }
  if(tid && tid[0]) {
    headers[n++] = (https_header_t){"x-client-transaction-id", tid};
  }
  return n;
}


void
xchat_uuid(const unsigned char bytes[16], char buf[37]) {
  static const char hex[] = "0123456789abcdef";
  unsigned char b[16];
  int n = 0;

  memcpy(b, bytes, sizeof b);
  b[6] = (b[6] & 0x0f) | 0x40; // version 4
  b[8] = (b[8] & 0x3f) | 0x80; // the variant of RFC 4122
  for(int i=0; i < 16; i++) {
    if(i == 4 || i == 6 || i == 8 || i == 10) {
      buf[n++] = '-';
    }
    buf[n++] = hex[b[i] >> 4];
    buf[n++] = hex[b[i] & 15];
  }
  buf[n] = 0;
}


/*
 * base64 and Thrift
 */

static int
b64_value(int c) {
  return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 :
    c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1;
}


/**
 * s decoded into out (size bytes); how many bytes, or -1
 **/
static long
b64_decode(const char *s, unsigned char *out, size_t size) {
  unsigned bits = 0;
  int have = 0, v;
  size_t n = 0;

  for(; *s && *s != '='; s++) {
    if((v=b64_value((unsigned char)*s)) < 0) {
      return -1;
    }
    bits = (bits << 6) | (unsigned)v;
    have += 6;
    if(have >= 8) {
      have -= 8;
      if(n >= size) {
        return -1;
      }
      out[n++] = (unsigned char)(bits >> have);
      bits &= (1u << have) - 1;
    }
  }
  return (long)n;
}


static unsigned long long
t_uint(thrift_t *t, int bytes) {
  unsigned long long v = 0;

  if(t->bad || t->at + (size_t)bytes > t->len) {
    t->bad = 1;
    return 0;
  }
  for(int i=0; i < bytes; i++) {
    v = (v << 8) | t->p[t->at++];
  }
  return v;
}


/**
 * a string's bytes: where they start, their number in n; NULL when the
 * data ends first
 **/
static const unsigned char *
t_string(thrift_t *t, size_t *n) {
  const unsigned char *s;
  long long len = (int32_t)t_uint(t, 4);

  if(t->bad || len < 0 || t->at + (size_t)len > t->len) {
    t->bad = 1;
    return NULL;
  }
  s = t->p + t->at;
  t->at += (size_t)len;
  *n = (size_t)len;
  return s;
}


/**
 * a string into buf (size bytes), cut to fit: 0 when it went in whole, -1
 * when it was cut, held a NUL or was not there
 **/
static int
t_copy(thrift_t *t, char *buf, size_t size) {
  size_t n = 0, len;
  const unsigned char *s = t_string(t, &n);

  buf[0] = 0;
  if(!s || !size) {
    return -1;
  }
  len = n < size - 1 ? n : size - 1;
  memcpy(buf, s, len);
  buf[len] = 0;
  return len == n && !memchr(s, 0, n) ? 0 : -1;
}


static void t_skip(thrift_t *t, int type, int depth);


/**
 * the next field of a struct: its type and number; 0 at its end
 **/
static int
t_field(thrift_t *t, int *type, int *id) {
  *type = (int)t_uint(t, 1);
  if(t->bad || *type == T_STOP) {
    return 0;
  }
  *id = (int16_t)t_uint(t, 2);
  return !t->bad;
}


static void
t_skip(thrift_t *t, int type, int depth) {
  int ftype, id, ktype, vtype;
  long long count;
  size_t n;

  if(depth > THRIFT_DEPTH) {
    t->bad = 1;
    return;
  }
  switch(type) {
  case T_BOOL: case T_BYTE: t_uint(t, 1); break;
  case T_I16: t_uint(t, 2); break;
  case T_I32: t_uint(t, 4); break;
  case T_I64: case T_DOUBLE: t_uint(t, 8); break;
  case T_STRING: t_string(t, &n); break;
  case T_STRUCT:
    while(t_field(t, &ftype, &id)) {
      t_skip(t, ftype, depth + 1);
    }
    break;
  case T_MAP:
    ktype = (int)t_uint(t, 1);
    vtype = (int)t_uint(t, 1);
    count = (int32_t)t_uint(t, 4);
    for(long long i=0; i < count && !t->bad; i++) {
      t_skip(t, ktype, depth + 1);
      t_skip(t, vtype, depth + 1);
    }
    t->bad |= count < 0;
    break;
  case T_SET: case T_LIST:
    vtype = (int)t_uint(t, 1);
    count = (int32_t)t_uint(t, 4);
    for(long long i=0; i < count && !t->bad; i++) {
      t_skip(t, vtype, depth + 1);
    }
    t->bad |= count < 0;
    break;
  default:
    t->bad = 1;
  }
}


/**
 * the words of a message's plaintext: a MessageEntryHolder, whose field 1
 * is the contents, whose field 1 is the message, whose field 1 is the text
 **/
void
xchat_plaintext(const unsigned char *p, size_t len, char *buf, size_t size) {
  thrift_t t = {p, len, 0, 0};
  int type, id, type2, id2, type3, id3;

  if(size) {
    buf[0] = 0;
  }
  while(t_field(&t, &type, &id)) {               // MessageEntryHolder
    if(id != 1 || type != T_STRUCT) {
      t_skip(&t, type, 1);
      continue;
    }
    while(t_field(&t, &type2, &id2)) {           // MessageEntryContents, a union
      if(id2 != 1 || type2 != T_STRUCT) {
        t_skip(&t, type2, 2);
        continue;
      }
      while(t_field(&t, &type3, &id3)) {         // MessageContents
        if(id3 == 1 && type3 == T_STRING) {
          t_copy(&t, buf, size);
        } else {
          t_skip(&t, type3, 3);
        }
      }
    }
  }
  if(t.bad && size) {
    buf[0] = 0;
  }
}


/**
 * s cut where it ends whole: a character's bytes that a copy cut short go
 **/
static void
utf8_whole(char *s) {
  size_t n = strlen(s), i = n, need;
  unsigned char c;

  while(i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) {
    i--;
  }
  if(!i) {
    s[0] = 0;
    return;
  }
  c = (unsigned char)s[i - 1];
  need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
  if(n - (i - 1) < need) {
    s[i - 1] = 0;
  }
}


/**
 * a list of MessageAttachment (a union) at t: the first MediaAttachment of
 * a kind into att, and *found set, once there is one; its type, url,
 * dimensions, id, a file's name and size, and the SHA-256 of its bytes
 **/
static void
read_attachments(thrift_t *t, xchat_attach_t *att, int *found) {
  int etype = (int)t_uint(t, 1);
  long long count = (int32_t)t_uint(t, 4);

  for(long long k=0; k < count && !t->bad; k++) {
    int type4, id4;
    if(etype != T_STRUCT) {
      t_skip(t, etype, 4);
      continue;
    }
    while(t_field(t, &type4, &id4)) {         // MessageAttachment, a union
      xchat_attach_t one;
      int type5, id5;
      if(id4 != 1 || type4 != T_STRUCT) {     // only a MediaAttachment (field 1)
        t_skip(t, type4, 4);
        continue;
      }
      memset(&one, 0, sizeof one);
      while(t_field(t, &type5, &id5)) {       // MediaAttachment
        if(id5 == 3 && type5 == T_I32) {
          one.kind = (int)(int32_t)t_uint(t, 4);
        } else if(id5 == 8 && type5 == T_STRING) {
          t_copy(t, one.url, sizeof one.url);                // legacy_media_url_https
        } else if(id5 == 4 && type5 == T_I64) {
          long long ms = (long long)t_uint(t, 8);          // duration_millis
          one.duration_ms = ms > 0 && ms < 0x7fffffff ? (int)ms : 0;
        } else if(id5 == 5 && type5 == T_I64) {
          long long size = (long long)t_uint(t, 8);        // filesize_bytes
          one.filesize = size > 0 ? size : 0;
        } else if(id5 == 6 && type5 == T_STRING) {
          t_copy(t, one.filename, sizeof one.filename);      // filename
        } else if(id5 == 7 && type5 == T_STRING) {
          t_copy(t, one.att_id, sizeof one.att_id);          // attachment_id
        } else if(id5 == 11 && type5 == T_STRING) {         // plaintext_sha256
          size_t n = 0;
          const unsigned char *s = t_string(t, &n);
          if(s && n == sizeof one.sha256) {
            memcpy(one.sha256, s, sizeof one.sha256);
            one.has_sha256 = 1;
          }
        } else if(id5 == 2 && type5 == T_STRUCT) {          // MediaDimensions
          int type6, id6;
          while(t_field(t, &type6, &id6)) {
            if(id6 == 1 && type6 == T_I64) {
              one.width = (int)t_uint(t, 8);
            } else if(id6 == 2 && type6 == T_I64) {
              one.height = (int)t_uint(t, 8);
            } else {
              t_skip(t, type6, 6);
            }
          }
        } else {
          t_skip(t, type5, 5);
        }
      }
      if(!*found && one.kind) {
        // a file's name shows: no line breaks or other controls in it
        for(char *c=one.filename; *c; c++) {
          *c = (unsigned char)*c < 0x20 || *c == 0x7f ? ' ' : *c;
        }
        utf8_whole(one.filename);
        *att = one;
        *found = 1;
      }
    }
  }
  t->bad |= count < 0;
}


/**
 * the first media attachment of a message (its MessageContents' field 3, a
 * list whose first MediaAttachment has a type, a url, dimensions and an id):
 * att filled, 1 when it has one, 0 when it has none
 **/
int
xchat_attachment(const unsigned char *p, size_t len, xchat_attach_t *att) {
  thrift_t t = {p, len, 0, 0};
  int type, id, found = 0;

  memset(att, 0, sizeof *att);
  while(t_field(&t, &type, &id)) {                 // MessageEntryHolder
    int type2, id2;
    if(id != 1 || type != T_STRUCT) {
      t_skip(&t, type, 1);
      continue;
    }
    while(t_field(&t, &type2, &id2)) {             // MessageEntryContents, a union
      int type3, id3;
      if(id2 != 1 || type2 != T_STRUCT) {
        t_skip(&t, type2, 2);
        continue;
      }
      while(t_field(&t, &type3, &id3)) {           // MessageContents
        if(id3 == 3 && type3 == T_LIST) {          // the attachments
          read_attachments(&t, att, &found);
        } else {
          t_skip(&t, type3, 3);
        }
      }
    }
  }
  if(t.bad) {
    memset(att, 0, sizeof *att);
    return 0;
  }
  return found;
}


/**
 * a ReplyingToPreview: whom the message answers and what that one said, as
 * the sender's app saw it (1 their number, 2 its words, 4 its attachments,
 * 5 their name, 6 its sequence number)
 **/
static void
read_reply(thrift_t *t, xchat_entry_t *e) {
  xchat_attach_t att;
  int type, id, found = 0;

  memset(&att, 0, sizeof att);
  while(t_field(t, &type, &id)) {
    if(id == 1 && type == T_I64) {
      snprintf(e->reply_user, sizeof e->reply_user, "%llu", t_uint(t, 8));
    } else if(id == 2 && type == T_STRING) {
      t_copy(t, e->reply_text, sizeof e->reply_text);
    } else if(id == 4 && type == T_LIST) {
      read_attachments(t, &att, &found);
    } else if(id == 5 && type == T_STRING) {
      t_copy(t, e->reply_name, sizeof e->reply_name);
    } else if(id == 6 && type == T_STRING) {
      t_copy(t, e->reply_seq, sizeof e->reply_seq);
    } else {
      t_skip(t, type, 4);
    }
  }
  e->reply_media = found ? att.kind : 0;
  memset(&att, 0, sizeof att);
}


/**
 * MessageContents: 1 its words, 3 its attachments, 4 what it replies to
 **/
static void
read_message(thrift_t *t, xchat_entry_t *e) {
  int type, id, found = 0;

  e->kind = XCHAT_ENTRY_MESSAGE;
  while(t_field(t, &type, &id)) {
    if(id == 1 && type == T_STRING) {
      t_copy(t, e->text, sizeof e->text);
    } else if(id == 3 && type == T_LIST) {
      read_attachments(t, &e->att, &found);
    } else if(id == 4 && type == T_STRUCT) {
      read_reply(t, e);
    } else {
      t_skip(t, type, 3);
    }
  }
  e->media_kind = found ? e->att.kind : 0;
}


int
xchat_entry(const unsigned char *p, size_t len, xchat_entry_t *e) {
  thrift_t t = {p, len, 0, 0};
  int type, id, type2, id2, type3, id3, k;

  memset(e, 0, sizeof *e);
  while(t_field(&t, &type, &id)) {                 // MessageEntryHolder
    if(id != 1 || type != T_STRUCT) {
      t_skip(&t, type, 1);
      continue;
    }
    while(t_field(&t, &type2, &id2)) {             // MessageEntryContents, a union
      if(type2 != T_STRUCT) {
        t_skip(&t, type2, 2);
        continue;
      }
      if(id2 == 1) {
        read_message(&t, e);
        continue;
      }
      // 2 a reaction, 3 one taken back, 4 an edit, 9 a screenshot, 10 a
      // call ended, 11 one missed, 16 one started
      k = id2 == 2 ? XCHAT_ENTRY_REACT : id2 == 3 ? XCHAT_ENTRY_UNREACT : id2 == 4 ? XCHAT_ENTRY_EDIT :
          id2 == 9 ? XCHAT_ENTRY_SCREENSHOT : id2 == 10 ? XCHAT_ENTRY_CALL_ENDED :
          id2 == 11 ? XCHAT_ENTRY_CALL_MISSED : id2 == 16 ? XCHAT_ENTRY_CALL_STARTED : XCHAT_ENTRY_NONE;
      if(!k) {
        t_skip(&t, type2, 2);
        continue;
      }
      e->kind = k;
      while(t_field(&t, &type3, &id3)) {
        const int to_one = k == XCHAT_ENTRY_REACT || k == XCHAT_ENTRY_UNREACT || k == XCHAT_ENTRY_EDIT;
        if(to_one && id3 == 1 && type3 == T_STRING) {
          t_copy(&t, e->target, sizeof e->target);       // message_sequence_id
        } else if(k != XCHAT_ENTRY_EDIT && to_one && id3 == 2 && type3 == T_STRING) {
          t_copy(&t, e->emoji, sizeof e->emoji);
        } else if(k == XCHAT_ENTRY_EDIT && id3 == 2 && type3 == T_STRING) {
          t_copy(&t, e->text, sizeof e->text);           // updated_text
        } else if(k == XCHAT_ENTRY_CALL_ENDED && id3 == 2 && type3 == T_I64) {
          long long s = (long long)t_uint(&t, 8);       // duration_seconds
          e->seconds = s > 0 ? s : 0;
        } else if(type3 == T_BOOL && ((k == XCHAT_ENTRY_CALL_STARTED && id3 == 1) ||
                                       (k == XCHAT_ENTRY_CALL_ENDED && id3 == 3) ||
                                       (k == XCHAT_ENTRY_CALL_MISSED && id3 == 2))) {
          e->video = !t_uint(&t, 1);                       // is_audio_only
        } else if(k == XCHAT_ENTRY_SCREENSHOT && id3 == 1 && type3 == T_I32) {
          e->shot = (int)(int32_t)t_uint(&t, 4);
        } else {
          t_skip(&t, type3, 3);
        }
      }
    }
  }
  if(t.bad) {
    memset(e, 0, sizeof *e);
    return -1;
  }
  utf8_whole(e->text);
  utf8_whole(e->reply_text);
  utf8_whole(e->reply_name);
  utf8_whole(e->emoji);
  return e->kind ? 0 : -1;
}


/**
 * the kind of a message's first attachment: 1 a photo, 2 a GIF, 3 a video,
 * 4 a voice note, 5 a file, 6 an SVG; 0 when it has none
 **/
int
xchat_media_kind(const unsigned char *p, size_t len) {
  xchat_attach_t att;
  return xchat_attachment(p, len, &att) ? att.kind : 0;
}


/**
 * a MessageCreateEvent: 100 its contents, 101 the version of the key they
 * are encrypted with (empty when they are not), 112 how they are encrypted;
 * the hook opens the words when the app has the key, else they stay sealed
 **/
static void
message_create(thrift_t *t, event_t *e, const char *conversation) {
  const unsigned char *contents = NULL;
  size_t len = 0;
  char version[32] = "";
  int type, id;

  while(t_field(t, &type, &id)) {
    if(id == 100 && type == T_STRING) {
      contents = t_string(t, &len);
    } else if(id == 101 && type == T_STRING) {
      e->sig_cut |= t_copy(t, version, sizeof version) != 0;
      e->has_key_version = 1;
    } else if(id == 112 && type == T_I16) {
      e->enc_version = (int16_t)t_uint(t, 2);
      e->has_enc_version = 1;
    } else {
      t_skip(t, type, 3);
    }
  }
  e->kind = EV_MESSAGE;
  e->sealed = version[0] != 0;
  e->encrypted = e->sealed;
  memcpy(e->key_version, version, sizeof version);
  e->contents = contents; // for its signature, once the whole event is read
  e->contents_len = contents ? len : 0;
  if(!contents || !len) {
    return;
  }
  if(!e->sealed) {
    xchat_entry(contents, len, &e->entry); // an unencrypted message's media is in the clear too
  } else if(g_hooks.entry && conversation &&
            g_hooks.entry(conversation, version, e->enc_version, e->sender, e->message_id,
                          contents, len, &e->entry)) {
    e->sealed = 0;
  }
}


/**
 * a list of strings at t (a list's header first): the first max of them
 * into ids (each cut to its size), *kept how many; returns how many there
 * are
 **/
static long long
read_ids(thrift_t *t, char (*ids)[32], int max, int *kept) {
  int etype = (int)t_uint(t, 1);
  long long count = (int32_t)t_uint(t, 4);

  *kept = 0;
  for(long long i=0; i < count && !t->bad; i++) {
    if(etype == T_STRING && *kept < max) {
      t_copy(t, ids[(*kept)++], sizeof ids[0]);
    } else {
      t_skip(t, etype, 4);
    }
  }
  t->bad |= count < 0;
  return count < 0 ? 0 : count;
}


/**
 * a MessageDeleteEvent: 1 the sequence numbers taken back, 2 how (1 for
 * who did it, 2 for everyone)
 **/
static void
message_delete(thrift_t *t, event_t *e) {
  int type, id;

  e->kind = EV_DELETE;
  while(t_field(t, &type, &id)) {
    if(id == 1 && type == T_LIST) {
      read_ids(t, e->deleted, DELETED_MAX, &e->deleted_count);
    } else if(id == 2 && type == T_I32) {
      e->for_all = (int32_t)t_uint(t, 4) == 2;
    } else {
      t_skip(t, type, 3);
    }
  }
}


/**
 * a GroupChangeEvent: 1 the change (a union: 1 the group made, 2 its title,
 * 3 its picture, 4 admins added, 5 members added, 7 members removed, 10
 * someone asking to join, 14 its description), 2 whether it only turns its
 * keys, which says nothing to show
 **/
static void
group_change(thrift_t *t, event_t *e) {
  char ids[2][32];
  int type, id, type2, id2, type3, id3, kept, rotation = 0;

  while(t_field(t, &type, &id)) {
    if(id == 2 && type == T_BOOL) {
      rotation = t_uint(t, 1) != 0;
      continue;
    }
    if(id != 1 || type != T_STRUCT) {
      t_skip(t, type, 3);
      continue;
    }
    while(t_field(t, &type2, &id2)) {
      const int g = id2 == 1 ? X_EV_GROUP_MADE : id2 == 2 ? X_EV_TITLE : id2 == 3 ? X_EV_PICTURE :
                    id2 == 4 ? X_EV_ADMINS : id2 == 5 ? X_EV_ADDED : id2 == 7 ? X_EV_REMOVED :
                    id2 == 10 ? X_EV_JOIN_ASKED : id2 == 14 ? X_EV_DESCRIPTION : X_EV_NONE;
      if(type2 != T_STRUCT || !g) {
        t_skip(t, type2, 4);
        continue;
      }
      e->group = g;
      while(t_field(t, &type3, &id3)) {
        if(id3 == 1 && type3 == T_LIST && g != X_EV_TITLE && g != X_EV_DESCRIPTION) {
          // the people: added, removed, made admins, or in the group made
          e->group_n = read_ids(t, ids, 2, &kept);
          if(e->group_n == 1 && kept) {
            snprintf(e->group_who, sizeof e->group_who, "%.23s", ids[0]);
          }
        } else if(id3 == 1 && type3 == T_STRING && (g == X_EV_TITLE || g == X_EV_JOIN_ASKED)) {
          if(g == X_EV_TITLE) {
            t_copy(t, e->group_text, sizeof e->group_text);   // custom_title
          } else {
            t_copy(t, e->group_who, sizeof e->group_who);     // requesting_user_id
          }
        } else if(id3 == 3 && type3 == T_STRING && g == X_EV_GROUP_MADE) {
          t_copy(t, e->group_text, sizeof e->group_text);     // title
        } else {
          t_skip(t, type3, 5);
        }
      }
    }
  }
  e->kind = rotation || !e->group ? EV_OTHER : EV_GROUP;
  utf8_whole(e->group_text);
}


/**
 * a ConversationMetadataChangeEvent: 1 the change, a union of which only
 * how long messages last shows (1 a time, in milliseconds; 2 no more)
 **/
static void
metadata_change(thrift_t *t, event_t *e) {
  int type, id, type2, id2, type3, id3;

  while(t_field(t, &type, &id)) {
    if(id != 1 || type != T_STRUCT) {
      t_skip(t, type, 3);
      continue;
    }
    while(t_field(t, &type2, &id2)) {
      if(type2 != T_STRUCT || (id2 != 1 && id2 != 2)) {
        t_skip(t, type2, 4);
        continue;
      }
      e->kind = EV_GROUP;
      e->group = X_EV_DISAPPEAR;
      while(t_field(t, &type3, &id3)) {
        if(id2 == 1 && id3 == 1 && type3 == T_I64) {
          long long ms = (long long)t_uint(t, 8);
          e->group_n = ms > 0 ? ms / 1000 : 0;
        } else {
          t_skip(t, type3, 5);
        }
      }
    }
  }
}


/**
 * a ConversationKeyChangeEvent: 1 the version, 2 the list of the
 * participants' wrapped keys (1 the user, 2 the key, base64); each goes to
 * the hook, which keeps the one that opens with the user's identity key
 **/
static void
key_change(thrift_t *t, const char *conversation) {
  char version[32] = "";
  int type, id;

  while(t_field(t, &type, &id)) {
    if(id == 1 && type == T_STRING) {
      t_copy(t, version, sizeof version);
    } else if(id == 2 && type == T_LIST) {
      int etype = (int)t_uint(t, 1);
      long long count = (int32_t)t_uint(t, 4);
      for(long long i=0; i < count && !t->bad; i++) {
        char user[24] = "", wrapped[512] = "";
        int t2, i2;
        if(etype != T_STRUCT) {
          t_skip(t, etype, 4);
          continue;
        }
        while(t_field(t, &t2, &i2)) {
          if(i2 == 1 && t2 == T_STRING) {
            t_copy(t, user, sizeof user);
          } else if(i2 == 2 && t2 == T_STRING) {
            t_copy(t, wrapped, sizeof wrapped);
          } else {
            t_skip(t, t2, 4);
          }
        }
        if(g_hooks.key && conversation && version[0] && wrapped[0]) {
          g_hooks.key(conversation, version, user, wrapped);
        }
      }
      t->bad |= count < 0;
    } else {
      t_skip(t, type, 3);
    }
  }
}


/**
 * a MessageEvent: 1 sequence number, 2 its id, 3 sender, 6 when
 * (milliseconds, as digits), 7 what (1 a message event, 3 a key change, 4 a
 * change of the group's, 7 messages taken back, 9 how long messages last,
 * 12 a conversation read), 9 its signature; conversation is the number it
 * belongs to, for the key. With verify, a message's signature is checked
 * (the hook's verify), when its words show
 **/
static int
decode_event(const char *b64, const char *conversation, event_t *e, int verify) {
  unsigned char *bytes;
  char msec[24] = "";
  thrift_t t = {NULL, 0, 0, 0};
  size_t size = b64 ? strlen(b64) / 4 * 3 + 3 : 0;
  long len;
  int type, id, type2, id2, bad;

  memset(e, 0, sizeof *e);
  if(!size || size > EVENT_MAX || !(bytes=mem_alloc(size))) {
    return -1;
  }
  if((len=b64_decode(b64, bytes, size)) <= 0) {
    mem_free(bytes);
    return -1;
  }
  t.p = bytes;
  t.len = (size_t)len;
  while(t_field(&t, &type, &id)) {
    if(id == 1 && type == T_STRING) {
      t_copy(&t, e->seq, sizeof e->seq);
    } else if(id == 2 && type == T_STRING) {
      e->sig_cut |= t_copy(&t, e->message_id, sizeof e->message_id) != 0;
    } else if(id == 3 && type == T_STRING) {
      e->sig_cut |= t_copy(&t, e->sender, sizeof e->sender) != 0;
    } else if(id == 4 && type == T_STRING) {
      e->sig_cut |= t_copy(&t, e->conversation, sizeof e->conversation) != 0;
      e->has_conversation = 1;
    } else if(id == 9 && type == T_STRUCT) {
      // MessageEventSignature: 1 the signature, 2 which of the sender's
      // keys, 3 its version (4, the key it says it is, is never used)
      while(t_field(&t, &type2, &id2)) {
        if(id2 == 1 && type2 == T_STRING) {
          e->sig_cut |= t_copy(&t, e->sig, sizeof e->sig) != 0;
          e->has_sig = 1;
        } else if(id2 == 2 && type2 == T_STRING) {
          e->sig_cut |= t_copy(&t, e->sig_key, sizeof e->sig_key) != 0;
        } else if(id2 == 3 && type2 == T_STRING) {
          e->sig_cut |= t_copy(&t, e->sig_version, sizeof e->sig_version) != 0;
        } else {
          t_skip(&t, type2, 2);
        }
      }
    } else if(id == 6 && type == T_STRING) {
      t_copy(&t, msec, sizeof msec);
    } else if(id == 7 && type == T_STRUCT) {
      // a union: one of its fields
      while(t_field(&t, &type2, &id2)) {
        if(id2 == 1 && type2 == T_STRUCT) {
          message_create(&t, e, conversation);
        } else if(id2 == 3 && type2 == T_STRUCT) {
          e->kind = EV_KEY;
          key_change(&t, conversation);
        } else if(id2 == 4 && type2 == T_STRUCT) {
          group_change(&t, e);
        } else if(id2 == 7 && type2 == T_STRUCT) {
          message_delete(&t, e);
        } else if(id2 == 9 && type2 == T_STRUCT) {
          metadata_change(&t, e);
        } else if(id2 == 12 && type2 == T_STRUCT) {
          int type3, id3;
          e->kind = EV_READ;
          while(t_field(&t, &type3, &id3)) {
            if(id3 == 1 && type3 == T_STRING) {
              t_copy(&t, e->seen_until, sizeof e->seen_until);
            } else {
              t_skip(&t, type3, 3);
            }
          }
        } else {
          t_skip(&t, type2, 2);
        }
      }
    } else {
      t_skip(&t, type, 1);
    }
  }
  e->msec = atoll(msec);
  bad = t.bad;
  // its signature, while its contents are at hand: of a message whose words
  // show (opened, or sent in the clear), whole as it came
  if(verify && !bad && e->kind == EV_MESSAGE && !e->sealed && e->has_sig && !e->sig_cut && g_hooks.verify) {
    const xchat_signed_t s = {e->message_id, e->sender, e->has_conversation ? e->conversation : NULL,
                              e->has_key_version ? e->key_version : NULL, e->contents, e->contents_len,
                              e->has_enc_version, e->enc_version, e->sig, e->sig_key, e->sig_version};
    e->signature = g_hooks.verify(&s);
  }
  e->contents = NULL;
  e->contents_len = 0;
  memset(bytes, 0, (size_t)len);
  mem_free(bytes);
  return bad || !e->seq[0] ? -1 : 0;
}


/**
 * whether the sequence number a comes after b (both digits)
 **/
static int
seq_after(const char *a, const char *b) {
  size_t la, lb;

  while(*a == '0' && a[1]) {
    a++;
  }
  while(*b == '0' && b[1]) {
    b++;
  }
  la = strlen(a);
  lb = strlen(b);
  return la != lb ? la > lb : strcmp(a, b) > 0;
}


/*
 * The inbox
 */

static void
copy_str(char *buf, size_t size, const char *s) {
  snprintf(buf, size, "%s", s ? s : "");
}


void
xchat_media_token(const char *url, char *buf, size_t size) {
  uint64_t h = 0xcbf29ce484222325ull; // FNV-1a

  for(const unsigned char *s=(const unsigned char *)url; *s; s++) {
    h = (h ^ *s) * 0x100000001b3ull;
  }
  snprintf(buf, size, "xchat/%016llx", (unsigned long long)h);
}


/**
 * a message's first attachment into the post's media, for the ones the app
 * shows (a photo or an SVG, a video or GIF, a voice note): where its bytes
 * are and, when they are encrypted, the key that opens them. A video's or
 * voice note's MP4 plays from video_url: its token (xchat_media_token) when
 * encrypted, for the title's reader to fetch and open, or the url itself
 **/
static void
put_media(x_post_t *p, const xchat_attach_t *att) {
  x_media_t *m = &p->media[0];
  const int photo = att->kind == 1 || att->kind == 6, video = att->kind == 2 || att->kind == 3;
  const int voice = att->kind == 4;
  char token[XCHAT_TOKEN];

  if((!photo && !video && !voice) || !att->url[0] || strlen(att->url) >= sizeof m->url) {
    return; // a file, or nothing to fetch: its label only
  }
  if(!photo) {
    if(att->enc) {
      xchat_media_token(att->url, token, sizeof token);
      snprintf(m->video_url, sizeof m->video_url, "file://%s", token);
    } else if(strlen(att->url) < sizeof m->video_url) {
      copy_str(m->video_url, sizeof m->video_url, att->url);
    } else {
      return;
    }
    m->video = video;
    m->voice = voice;
    m->duration_ms = att->kind == 2 ? 0 : att->duration_ms;
  }
  p->media_count = 1;
  copy_str(m->url, sizeof m->url, att->url);
  m->width = att->width;
  m->height = att->height;
  m->xchat = att->enc;
  if(att->enc) {
    memcpy(m->xchat_key, att->key, 32);
  }
}


/**
 * whether a message event shows as one of the conversation's: a message
 * (or one whose words stay sealed, which may be one), a call, a screenshot
 **/
static int
event_shows(const event_t *e) {
  const int k = e->entry.kind;

  return e->kind == EV_MESSAGE && (e->sealed || k == XCHAT_ENTRY_MESSAGE || k == XCHAT_ENTRY_CALL_STARTED ||
                                   k == XCHAT_ENTRY_CALL_ENDED || k == XCHAT_ENTRY_CALL_MISSED ||
                                   k == XCHAT_ENTRY_SCREENSHOT);
}


/**
 * what a message event that shows (event_shows) is, into p: its words and
 * what it carries, a call's or screenshot's kind; whole, for the
 * conversation's page, also its picture, video or voice note into the
 * media, and whom it replies to (quote_name, quote_user, quote_text, and
 * in quote_badge what that one carried without words)
 **/
static void
put_entry(x_post_t *p, const event_t *e, int whole) {
  const xchat_entry_t *x = &e->entry;

  p->sealed = e->sealed;
  p->encrypted = e->encrypted;
  p->signature = e->signature;
  p->icon = X_DM_TEXT;
  if(e->sealed) {
    return;
  }
  switch(x->kind) {
  case XCHAT_ENTRY_CALL_STARTED:
  case XCHAT_ENTRY_CALL_ENDED:
  case XCHAT_ENTRY_CALL_MISSED:
    p->icon = X_DM_CALL;
    p->event = x->kind == XCHAT_ENTRY_CALL_STARTED ? X_EV_CALL_STARTED :
               x->kind == XCHAT_ENTRY_CALL_ENDED ? X_EV_CALL_ENDED : X_EV_CALL_MISSED;
    p->event_n = x->seconds;
    p->event_video = x->video;
    return;
  case XCHAT_ENTRY_SCREENSHOT:
    p->icon = X_DM_EVENT;
    p->event = X_EV_SCREENSHOT;
    p->event_n = x->shot == 2 ? 2 : 1;
    return;
  default:
    break;
  }
  copy_str(p->text, sizeof p->text, x->text);
  p->icon = media_icon(x->media_kind); // "Foto"/"Vídeo"/"GIF" when it is one
  if(x->media_kind == 5) {
    copy_str(p->file_name, sizeof p->file_name, x->att.filename);
    p->file_size = x->att.filesize;
  }
  if(!whole) {
    return;
  }
  put_media(p, &x->att); // a photo renders in the conversation
  if(x->reply_seq[0] || x->reply_text[0] || x->reply_name[0]) {
    copy_str(p->quote_user, sizeof p->quote_user, x->reply_user);
    copy_str(p->quote_name, sizeof p->quote_name, x->reply_name);
    copy_str(p->quote_text, sizeof p->quote_text, x->reply_text);
    p->quote_badge = x->reply_text[0] ? 0 : media_icon(x->reply_media); // what it carried, without words
  }
}


static int
newer_first(const void *a, const void *b) {
  const x_post_t *x = a, *y = b;
  return x->time != y->time ? (x->time < y->time ? 1 : -1) : strcmp(x->id, y->id);
}


/**
 * the participant whose number is id, among a conversation's
 **/
static const json_node_t *
participant(const json_node_t *list, const char *id) {
  for(const json_node_t *u=list ? list->child : NULL; u && id; u=u->next) {
    const char *uid = json_string(json_get(u, "rest_id"));
    if(uid && !strcmp(uid, id)) {
      return json_get(u, "result");
    }
  }
  return NULL;
}


/**
 * one of the inbox's items into p
 **/
static void
item(const json_node_t *it, const char *me, x_post_t *p) {
  event_t best, one;
  const json_node_t *detail = json_get(it, "conversation_detail"), *people, *meta, *list, *who;
  const char *encoded;
  char seen[32] = "";
  int others = 0, found = 0;
  size_t len = 0;

  memset(&best, 0, sizeof best);

  people = json_get(detail, "participants_results");
  meta = json_get(detail, "group_metadata");
  for(const json_node_t *u=people ? people->child : NULL; u; u=u->next) {
    const char *uid = json_string(json_get(u, "rest_id"));
    others += uid && (!me || strcmp(uid, me));
  }
  p->group = (meta && meta->type == JSON_OBJECT) || others > 1;

  // who it is with: the other one, or the names of a group's
  for(const json_node_t *u=people ? people->child : NULL; u; u=u->next) {
    const char *uid = json_string(json_get(u, "rest_id"));
    const json_node_t *r = json_get(u, "result");
    const char *name = json_string(json_path(r, "core", "name", NULL));
    const char *avatar = json_string(json_path(r, "avatar", "image_url", NULL));
    if(!uid || (me && !strcmp(uid, me))) {
      continue;
    }
    if(p->face_count < X_MAX_FACES && avatar && !strncmp(avatar, "https://", 8)) {
      copy_str(p->faces[p->face_count++], sizeof p->faces[0], avatar);
    }
    if(!p->group && !p->user_id[0]) {
      copy_str(p->user_id, sizeof p->user_id, uid);
      copy_str(p->user, sizeof p->user, json_string(json_path(r, "core", "screen_name", NULL)));
      copy_str(p->name, sizeof p->name, name);
      if(avatar && !strncmp(avatar, "https://", 8)) {
        copy_str(p->avatar, sizeof p->avatar, avatar);
      }
      who = json_path(r, "verification", "is_blue_verified", NULL);
      p->badge = who && who->type == JSON_TRUE ? X_BADGE_BLUE : X_BADGE_NONE;
    } else if(p->group && name && len < sizeof p->name) {
      len += snprintf(p->name + len, sizeof p->name - len, "%s%s", len ? ", " : "", name);
    }
  }
  if(p->group) {
    const char *avatar = json_string(json_get(meta, "group_avatar_url"));
    if(avatar && !strncmp(avatar, "https://", 8)) {
      copy_str(p->avatar, sizeof p->avatar, avatar);
    }
  }

  // its keys first, from any key change events, so the last message (and
  // later refreshes) open; only when the app installed the hooks
  if(g_hooks.key) {
    list = json_get(it, "latest_conversation_key_change_events");
    for(const json_node_t *e=list ? list->child : NULL; e; e=e->next) {
      decode_event(json_string(e), p->id, &one, 0);
    }
  }

  // its last message: the newest of the messages among its events
  for(int k=0; k < 2; k++) {
    list = k ? json_get(it, "latest_message_events") : NULL;
    encoded = k ? NULL : json_string(json_get(it, "latest_notifiable_message_create_event"));
    for(const json_node_t *e=list ? list->child : NULL; k == 0 || e; e=e ? e->next : NULL) {
      if(k) {
        encoded = json_string(e);
      }
      if(encoded && !decode_event(encoded, p->id, &one, 0)) {
        if(event_shows(&one) && (!found || seq_after(one.seq, best.seq))) {
          best = one;
          found = 1;
        } else if(one.kind == EV_READ && me && !strcmp(one.sender, me) &&
                  (!seen[0] || seq_after(one.seen_until, seen))) {
          copy_str(seen, sizeof seen, one.seen_until);
        }
      }
      if(!k) {
        break;
      }
    }
  }
  // how far the user read
  list = json_get(it, "latest_read_events_per_participant");
  for(const json_node_t *r=list ? list->child : NULL; r && me; r=r->next) {
    const char *pid = json_string(json_path(r, "participant_id", "rest_id", NULL));
    if(pid && !strcmp(pid, me) &&
       !decode_event(json_string(json_get(r, "latest_mark_conversation_read_event")), p->id, &one, 0) &&
       one.kind == EV_READ && (!seen[0] || seq_after(one.seen_until, seen))) {
      copy_str(seen, sizeof seen, one.seen_until);
    }
  }
  if(found) {
    p->time = best.msec > 0 ? best.msec / 1000 : 0;
    p->mine = me && !strcmp(best.sender, me);
    put_entry(p, &best, 0); // "Foto"/"Vídeo"/"GIF" when it is one, a call
    p->unread = me && !p->mine && (!seen[0] || seq_after(best.seq, seen));
    if(p->group && !p->mine && (who=participant(people, best.sender))) {
      copy_str(p->quote_name, sizeof p->quote_name, json_string(json_path(who, "core", "name", NULL)));
    }
  }
  memset(&best, 0, sizeof best);
  memset(&one, 0, sizeof one);
}


/**
 * whether a, of the legacy inbox, is the conversation x of X Chat: the same
 * number (X Chat writes "1:2" for the inbox's "1-2"), or the same one person
 **/
static int
same_conversation(const x_post_t *a, const x_post_t *x) {
  const char *s = a->id, *t = x->id;

  while(*s && (*s == *t || (*s == '-' && *t == ':'))) {
    s++;
    t++;
  }
  if(!*s && !*t) {
    return 1;
  }
  return !a->group && !x->group && a->user_id[0] && !strcmp(a->user_id, x->user_id);
}


int
xchat_merge(x_post_t *all, int legacy, int count) {
  int kept = legacy;

  for(int j=legacy; j < legacy + count; j++) {
    x_post_t *x = &all[j], *a = NULL;

    for(int i=0; i < legacy && !a; i++) {
      a = same_conversation(&all[i], x) ? &all[i] : NULL;
    }
    if(!a) {
      if(kept != j) {
        all[kept] = *x;
      }
      kept++;
    } else {
      // the inbox has this conversation too: keep its X Chat number, so its
      // page can be read for it, and when X Chat has the newer last message,
      // let the inbox's row show it (its words when the hooks opened them)
      copy_str(a->reposter, sizeof a->reposter, x->id);
      if(x->time > a->time) {
        a->time = x->time;
        a->unread = x->unread;
        a->mine = x->mine;
        a->sealed = x->sealed;
        a->encrypted = x->encrypted;
        a->icon = x->icon; // "Foto"/"Vídeo"/"GIF" when it opened to one, else text
        copy_str(a->text, sizeof a->text, x->sealed ? "" : x->text);
        copy_str(a->quote_name, sizeof a->quote_name, x->quote_name);
      }
    }
  }
  if(kept < legacy + count) {
    memset(&all[kept], 0, (size_t)(legacy + count - kept) * sizeof *all);
  }
  return kept;
}


int
xchat_inbox(const json_node_t *root, const char *me, x_post_t *posts, int max) {
  const json_node_t *page = json_path(root, "data", "get_initial_chat_page", NULL), *items;
  int n = 0;

  if(!page) {
    page = json_path(root, "data", "get_inbox_page", NULL);
  }
  if(!page || page->type != JSON_OBJECT) {
    return -1;
  }
  items = json_get(page, "items");
  for(const json_node_t *it=items ? items->child : NULL; it && (!posts || n < max); it=it->next) {
    const char *id = json_string(json_path(it, "conversation_detail", "conversation_id", NULL));
    x_post_t *p;

    if(!id || !id[0] || strlen(id) >= X_ID_MAX) {
      continue;
    }
    if(!posts) {
      n++;
      continue;
    }
    p = &posts[n++];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_CHAT;
    p->xchat = 1;
    copy_str(p->id, sizeof p->id, id);
    p->replies = p->reposts = p->likes = p->views = p->bookmarks = -1;
    item(it, me, p);
  }
  if(posts && n > 1) {
    qsort(posts, n, sizeof *posts, newer_first);
  }
  return n;
}


/*
 * A conversation's page
 */

// GetConversationPageQuery, with x.com's current id. x.com's Chat
// (abs.twimg.com/x-web/xchat/embed, dm-core) reads older messages with it:
// min_local_sequence_id is where it goes back from and
// min_conversation_key_version the oldest key it has, both Long.MAX when it
// has none, so the page is the newest events and every key change. "0"
// there makes X answer "range order reversed; begin must be <= end".
#define XCHAT_PAGE "https://api.x.com/graphql/GX9ZijkxG8AqRMQVD7hMnQ/GetConversationPageQuery?variables="
#define XCHAT_NEWEST "9223372036854775807"


/**
 * s percent-encoded into out (size bytes); 0 or -1
 **/
static int
url_encode(const char *s, char *out, size_t size) {
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;

  for(; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
       c == '-' || c == '_' || c == '.' || c == '~') {
      if(n + 1 >= size) {
        return -1;
      }
      out[n++] = (char)c;
    } else {
      if(n + 3 >= size) {
        return -1;
      }
      out[n++] = '%';
      out[n++] = hex[c >> 4];
      out[n++] = hex[c & 15];
    }
  }
  if(n >= size) {
    return -1;
  }
  out[n] = 0;
  return 0;
}


int
xchat_conversation_url(char *buf, size_t size, const char *conversation, const char *before) {
  char json[512], enc[1536];
  int n;

  if(!conversation || !conversation[0]) {
    return -1;
  }
  n = snprintf(json, sizeof json,
    "{\"conversation_id\":\"%s\",\"min_local_sequence_id\":\"%s\","
    "\"min_conversation_key_version\":\"%s\",\"query_settings\":{\"conversation_event_limit\":200}}",
    conversation, before && before[0] && strcmp(before, "0") ? before : XCHAT_NEWEST, XCHAT_NEWEST);
  if(n < 0 || (size_t)n >= sizeof json || url_encode(json, enc, sizeof enc)) {
    return -1;
  }
  n = snprintf(buf, size, "%s%s", XCHAT_PAGE, enc);
  return n < 0 || (size_t)n >= size ? -1 : 0;
}


/**
 * oldest first, by the sequence number
 **/
static int
by_seq(const void *a, const void *b) {
  const x_post_t *x = a, *y = b;
  return seq_after(x->id, y->id) ? 1 : seq_after(y->id, x->id) ? -1 : 0;
}


/*
 * What a message event does to another, done once all are read, in their
 * order: a reaction added or taken back, an edit, a message taken back.
 */
#define DONE_DELETE 100 // an effect's kind for a message taken back

typedef struct effect {
  char seq[32];       // its own sequence number, for the order
  int  kind;          // XCHAT_ENTRY_REACT, _UNREACT, _EDIT, or DONE_DELETE
  int  for_all;       // a message taken back for everyone
  char sender[24];
  char target[32];    // the message it is about
  char emoji[32];
  char *text;         // an edit's words (mem_alloc'd)
  int  signature;     // whether its own signature held: XCHAT_SIG_*
} effect_t;

typedef struct effects {
  effect_t *list;
  int      count, size;
} effects_t;


static effect_t *
effect_add(effects_t *fx, const event_t *e, int kind, const char *target) {
  effect_t *f;

  if(fx->count == fx->size) {
    int size = fx->size ? 2 * fx->size : 32;
    effect_t *grown = mem_realloc(fx->list, (size_t)size * sizeof *grown);
    if(!grown) {
      return NULL;
    }
    fx->list = grown;
    fx->size = size;
  }
  f = &fx->list[fx->count++];
  memset(f, 0, sizeof *f);
  copy_str(f->seq, sizeof f->seq, e->seq);
  copy_str(f->sender, sizeof f->sender, e->sender);
  copy_str(f->target, sizeof f->target, target);
  f->kind = kind;
  f->signature = e->signature;
  return f;
}


/**
 * the worse of two signatures' XCHAT_SIG_*: one that failed, then one not
 * checked, then one that held
 **/
static int
sig_worse(int a, int b) {
  const int ra = a == XCHAT_SIG_BAD ? 2 : a == XCHAT_SIG_NONE ? 1 : 0;
  const int rb = b == XCHAT_SIG_BAD ? 2 : b == XCHAT_SIG_NONE ? 1 : 0;
  return ra >= rb ? a : b;
}


static void
effects_free(effects_t *fx) {
  for(int i=0; i < fx->count; i++) {
    if(fx->list[i].text) {
      memset(fx->list[i].text, 0, strlen(fx->list[i].text));
      mem_free(fx->list[i].text);
    }
  }
  mem_free(fx->list);
  memset(fx, 0, sizeof *fx);
}


static int
effect_order(const void *a, const void *b) {
  const effect_t *x = a, *y = b;
  return seq_after(x->seq, y->seq) ? 1 : seq_after(y->seq, x->seq) ? -1 : 0;
}


/**
 * the message whose sequence number is seq among posts, or NULL
 **/
static x_post_t *
message_of(x_post_t *posts, int n, const char *seq) {
  for(int i=0; seq[0] && i < n; i++) {
    if(!strcmp(posts[i].id, seq)) {
      return &posts[i];
    }
  }
  return NULL;
}


/**
 * the effects done to posts in their order: the reactions that stay, each
 * message's put together; edits by who wrote the message; messages taken
 * back for everyone by who sent them, or for the user by the user (those
 * go: *gone set on them, as kind -1)
 **/
static void
effects_do(effects_t *fx, x_post_t *posts, int n, const char *me) {
  int live = 0;

  if(fx->count > 1) {
    qsort(fx->list, (size_t)fx->count, sizeof *fx->list, effect_order);
  }
  for(int i=0; i < fx->count; i++) {
    effect_t *f = &fx->list[i];
    x_post_t *p = message_of(posts, n, f->target);

    switch(f->kind) {
    case XCHAT_ENTRY_REACT:
    case XCHAT_ENTRY_UNREACT:
      // the reactions that stay are kept at the front of the list, in order
      for(int k=0; k < live; k++) {
        effect_t *r = &fx->list[k];
        if(r->kind == XCHAT_ENTRY_REACT && !strcmp(r->target, f->target) && !strcmp(r->sender, f->sender) &&
           !strcmp(r->emoji, f->emoji)) {
          r->kind = 0; // the same again, or taken back
        }
      }
      if(f->kind == XCHAT_ENTRY_REACT && p && f->emoji[0]) {
        fx->list[live++] = *f; // live <= i: what it overwrites is done with
      }
      break;
    case XCHAT_ENTRY_EDIT:
      if(p && f->text && p->icon != X_DM_DELETED && !strcmp(p->user_id, f->sender)) {
        copy_str(p->text, sizeof p->text, f->text);
        p->edited = 1;
        p->sealed = 0;
        p->signature = sig_worse(p->signature, f->signature); // the words now are the edit's
      }
      if(f->text) {
        memset(f->text, 0, strlen(f->text));
        mem_free(f->text);
        f->text = NULL;
      }
      break;
    case DONE_DELETE:
      if(p && f->for_all && !strcmp(p->user_id, f->sender)) {
        const long long when = p->time;
        char id[X_ID_MAX], sender[24];
        const int mine = p->mine;
        copy_str(id, sizeof id, p->id);
        copy_str(sender, sizeof sender, p->user_id);
        memset(p, 0, sizeof *p);
        p->kind = X_KIND_DM;
        p->xchat = 1;
        p->icon = X_DM_DELETED;
        copy_str(p->id, sizeof p->id, id);
        copy_str(p->user_id, sizeof p->user_id, sender);
        p->time = when;
        p->mine = mine;
        p->replies = p->reposts = p->likes = p->views = p->bookmarks = -1;
      } else if(p && !f->for_all && me && !strcmp(f->sender, me)) {
        p->kind = -1;
      }
      break;
    default:
      break;
    }
  }
  // each message's reactions: the emoji once each, in order, and how many
  for(int k=0; k < live; k++) {
    const effect_t *r = &fx->list[k];
    x_post_t *p = r->kind == XCHAT_ENTRY_REACT ? message_of(posts, n, r->target) : NULL;
    if(!p || p->icon == X_DM_DELETED || p->kind != X_KIND_DM) {
      continue;
    }
    p->reaction_count++;
    p->reacted |= me && !strcmp(r->sender, me);
    if(!strstr(p->reactions, r->emoji) && strlen(p->reactions) + strlen(r->emoji) < sizeof p->reactions) {
      strcat(p->reactions, r->emoji);
    }
  }
  fx->count = live; // the rest was done with, and its words let go of
}


int
xchat_messages(const json_node_t *root, const char *conversation, const char *me, x_post_t *posts, int max) {
  const json_node_t *page = json_path(root, "data", "get_conversation_page", NULL), *list, *e;
  effects_t fx = {NULL, 0, 0};
  event_t one;
  int n = 0, kept;

  if(!page || page->type != JSON_OBJECT) {
    return -1;
  }
  // the keys first: the key change events the page carries apart, then those
  // among the message events, so every message below has its key to open
  if(g_hooks.key) {
    list = json_get(page, "missing_conversation_key_change_events");
    for(e=list ? list->child : NULL; e; e=e->next) {
      decode_event(json_string(e), conversation, &one, 0);
    }
    list = json_get(page, "encoded_message_events");
    for(e=list ? list->child : NULL; e; e=e->next) {
      decode_event(json_string(e), conversation, &one, 0);
    }
  }
  // the messages, calls and changes of the group's; what the others do to
  // them (reactions, edits, messages taken back) is kept for after
  list = json_get(page, "encoded_message_events");
  for(e=list ? list->child : NULL; e; e=e->next) {
    const int shows = !decode_event(json_string(e), conversation, &one, posts != NULL) &&
                      (event_shows(&one) || one.kind == EV_GROUP);
    x_post_t *p;

    if(!posts) {
      n += shows;
      continue;
    }
    if(!shows) {
      const int k = one.entry.kind;
      if(one.kind == EV_MESSAGE && !one.sealed &&
         (k == XCHAT_ENTRY_REACT || k == XCHAT_ENTRY_UNREACT || k == XCHAT_ENTRY_EDIT)) {
        effect_t *f = effect_add(&fx, &one, k, one.entry.target);
        if(f && k == XCHAT_ENTRY_EDIT) {
          size_t len = strlen(one.entry.text);
          if((f->text=mem_alloc(len + 1))) {
            memcpy(f->text, one.entry.text, len + 1);
          }
        } else if(f) {
          copy_str(f->emoji, sizeof f->emoji, one.entry.emoji);
        }
      } else if(one.kind == EV_DELETE) {
        for(int i=0; i < one.deleted_count; i++) {
          effect_t *f = effect_add(&fx, &one, DONE_DELETE, one.deleted[i]);
          if(f) {
            f->for_all = one.for_all;
          }
        }
      }
      continue;
    }
    if(n >= max) {
      continue;
    }
    p = &posts[n++];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_DM;
    p->xchat = 1;
    copy_str(p->id, sizeof p->id, one.seq);
    copy_str(p->user_id, sizeof p->user_id, one.sender);
    p->time = one.msec > 0 ? one.msec / 1000 : 0;
    p->mine = me && !strcmp(one.sender, me);
    p->replies = p->reposts = p->likes = p->views = p->bookmarks = -1;
    if(one.kind == EV_GROUP) {
      p->icon = X_DM_EVENT;
      p->event = one.group == X_EV_REMOVED && one.group_n == 1 && !strcmp(one.group_who, one.sender) ?
                 X_EV_LEFT : one.group;
      p->event_n = one.group_n;
      copy_str(p->text, sizeof p->text, one.group_text);
      copy_str(p->quote_user, sizeof p->quote_user, one.group_who); // the one added or removed
    } else {
      put_entry(p, &one, 1);
      // the message it replies to, by its number, for its words below
      copy_str(p->quote_avatar, sizeof p->quote_avatar, one.entry.reply_seq);
    }
  }
  if(posts && n > 1) {
    qsort(posts, n, sizeof *posts, by_seq);
  }
  if(posts) {
    effects_do(&fx, posts, n, me);
    // a reply whose preview had no words: the words of the one it replies
    // to, when it is here
    for(int i=0; i < n; i++) {
      x_post_t *p = &posts[i], *to;
      if(p->kind == X_KIND_DM && p->event == X_EV_NONE && p->quote_avatar[0] && !p->quote_text[0] &&
         !p->quote_badge && (to=message_of(posts, n, p->quote_avatar)) && to->kind == X_KIND_DM) {
        copy_str(p->quote_text, sizeof p->quote_text, to->text);
        p->quote_badge = to->text[0] ? 0 : to->icon;
        if(!p->quote_user[0]) {
          copy_str(p->quote_user, sizeof p->quote_user, to->user_id);
        }
      }
    }
    for(int i=0; i < n; i++) {
      if(posts[i].kind == X_KIND_DM && posts[i].event == X_EV_NONE) {
        posts[i].quote_avatar[0] = 0;
      }
    }
    // the ones taken back for the user go
    kept = 0;
    for(int i=0; i < n; i++) {
      if(posts[i].kind != -1) {
        if(kept != i) {
          posts[kept] = posts[i];
        }
        kept++;
      }
    }
    n = kept;
  }
  effects_free(&fx);
  memset(&one, 0, sizeof one);
  return n;
}
