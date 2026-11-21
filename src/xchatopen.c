/* The words of X Chat's messages, see xchatopen.h. The identity key opens a
conversation's key from its key change events (ECIES, xcrypt_key_open), and
that key — or a per-message one derived from it — opens each message
(xcrypt_message_open). The senders' signing keys, as X lists them, check
each message's signature (xcrypt_signed_verify). The keyring, the identity
and the signing keys are behind a mutex, so the fetching threads and the
title's own may ask at once. */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mem.h"
#include "xchat.h"
#include "xchatopen.h"
#include "xcrypt.h"

#define KEYS_MAX     64   // conversation keys kept (a conversation and a version each)
#define WRAPPED_MAX  256  // base64 of a wrapped key (65 + 32 + 16 bytes is ~152)
#define MEDIA_MAX    128  // encrypted attachments kept by token: for the player, and their SHA-256
#define SIGNERS_MAX  64   // senders whose signing keys are kept (or asked for)
#define SIGNER_KEYS  XCHAT_SIGNER_KEYS
#define MAGIC        "XPS5xchat1\n"

// a sender's signing keys, as X listed them (GetPublicKeys): fetched 0 while
// they are wanted and not listed yet; miss, when a message named a version
// not among them (asked again, XCHAT_SIGNER_AGAIN seconds later)
typedef struct signer {
  char      user[24];
  long long fetched;
  int       miss;
  int       count;
  char      version[SIGNER_KEYS][32];
  uint8_t   key[SIGNER_KEYS][65];
} signer_t;

typedef struct keyentry {
  int     used;
  char    conversation[X_ID_MAX];
  char    version[32];
  uint8_t key[32];
} keyentry_t;

typedef struct mediaentry {
  char    token[XCHAT_TOKEN];
  char    url[256];
  uint8_t key[32];
  int     player;      // a GIF, video or voice note, which the player may ask for
  int     has_sha256;  // what its bytes hash to once opened, as its message says
  uint8_t sha256[32];
} mediaentry_t;

static struct {
  pthread_mutex_t lock;
  int             ready;         // an identity is set
  char            me[24];        // whose
  uint8_t         identity[32];
  keyentry_t      keys[KEYS_MAX];
  int             next;          // where to keep the next key when the ring is full
  int             count;         // keys kept now
  mediaentry_t    media[MEDIA_MAX]; // the attachments the player may ask for
  int             media_next;
  signer_t        signers[SIGNERS_MAX]; // public keys, kept whoever the user is
  int             signers_next;
} g = {.lock = PTHREAD_MUTEX_INITIALIZER};


/*
 * base64, its own copy (xchat.c's is static)
 */
static int
b64_value(int c) {
  return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 :
    c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1;
}

static long
b64_decode(const char *s, uint8_t *out, size_t size) {
  unsigned bits = 0;
  int have = 0, v;
  size_t n = 0;

  for(; s && *s && *s != '='; s++) {
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
      out[n++] = (uint8_t)(bits >> have);
      bits &= (1u << have) - 1;
    }
  }
  return (long)n;
}


/*
 * the keyring, under the lock
 */
static keyentry_t *
find(const char *conversation, const char *version) {
  for(int i=0; i < KEYS_MAX; i++) {
    if(g.keys[i].used && !strcmp(g.keys[i].conversation, conversation) &&
       !strcmp(g.keys[i].version, version)) {
      return &g.keys[i];
    }
  }
  return NULL;
}


static void
keep(const char *conversation, const char *version, const uint8_t key[32]) {
  keyentry_t *e = find(conversation, version);
  int slot;

  if(!e) {
    for(slot=0; slot < KEYS_MAX && g.keys[slot].used; slot++) {
      ;
    }
    if(slot == KEYS_MAX) {
      slot = g.next++ % KEYS_MAX; // the ring is full: the oldest goes
    } else {
      g.count++;
    }
    e = &g.keys[slot];
    e->used = 1;
    snprintf(e->conversation, sizeof e->conversation, "%s", conversation);
    snprintf(e->version, sizeof e->version, "%s", version);
  }
  memcpy(e->key, key, 32);
}


/**
 * the attachments kept for the player let go of; under the lock
 **/
static void
media_forget(void) {
  for(int i=0; i < MEDIA_MAX; i++) {
    xcrypt_wipe(g.media[i].key, 32);
  }
  memset(g.media, 0, sizeof g.media);
  g.media_next = 0;
}


/**
 * an encrypted attachment's url, key and SHA-256 (sha NULL: none said)
 * kept by its token (xchat_media_token), the same one again in its place;
 * player: a GIF, video or voice note, which the player may ask for
 **/
static void
media_keep(const char *url, const uint8_t key[32], const uint8_t *sha, int player) {
  char token[XCHAT_TOKEN];
  mediaentry_t *e = NULL;

  if(strlen(url) >= sizeof e->url) {
    return;
  }
  xchat_media_token(url, token, sizeof token);
  pthread_mutex_lock(&g.lock);
  for(int i=0; i < MEDIA_MAX && !e; i++) {
    if(!strcmp(g.media[i].token, token)) {
      e = &g.media[i];
    }
  }
  if(!e) {
    e = &g.media[g.media_next];
    g.media_next = (g.media_next + 1) % MEDIA_MAX;
  }
  snprintf(e->token, sizeof e->token, "%s", token);
  snprintf(e->url, sizeof e->url, "%s", url);
  memcpy(e->key, key, 32);
  e->player = player;
  e->has_sha256 = sha != NULL;
  if(sha) {
    memcpy(e->sha256, sha, 32);
  }
  pthread_mutex_unlock(&g.lock);
}


int
xchat_open_check(const char *url, const void *bytes, size_t len) {
  char token[XCHAT_TOKEN];
  uint8_t want[32], got[32];
  int known = 0;

  if(!url || strlen(url) >= sizeof g.media[0].url) {
    return 0;
  }
  xchat_media_token(url, token, sizeof token);
  pthread_mutex_lock(&g.lock);
  for(int i=0; i < MEDIA_MAX && !known; i++) {
    if(g.media[i].has_sha256 && !strcmp(g.media[i].token, token) && !strcmp(g.media[i].url, url)) {
      memcpy(want, g.media[i].sha256, 32);
      known = 1;
    }
  }
  pthread_mutex_unlock(&g.lock);
  if(!known) {
    return 0;
  }
  xcrypt_sha256(bytes, len, got);
  return xcrypt_equal(want, got, 32) ? 0 : -1;
}


int
xchat_open_media(const char *token, char *url, size_t size, uint8_t key[32]) {
  int ret = -1;

  pthread_mutex_lock(&g.lock);
  for(int i=0; i < MEDIA_MAX && token && token[0]; i++) {
    if(g.media[i].player && !strcmp(g.media[i].token, token) && strlen(g.media[i].url) < size) {
      snprintf(url, size, "%s", g.media[i].url);
      memcpy(key, g.media[i].key, 32);
      ret = 0;
      break;
    }
  }
  pthread_mutex_unlock(&g.lock);
  return ret;
}


void
xchat_open_set(const char *me, const uint8_t identity[32]) {
  pthread_mutex_lock(&g.lock);
  if(strcmp(g.me, me ? me : "")) {
    for(int i=0; i < KEYS_MAX; i++) {
      xcrypt_wipe(g.keys[i].key, 32);
    }
    memset(g.keys, 0, sizeof g.keys);
    g.count = g.next = 0;
    media_forget();
  }
  snprintf(g.me, sizeof g.me, "%s", me ? me : "");
  memcpy(g.identity, identity, 32);
  g.ready = 1;
  pthread_mutex_unlock(&g.lock);
}


void
xchat_open_forget(void) {
  pthread_mutex_lock(&g.lock);
  xcrypt_wipe(g.identity, sizeof g.identity);
  for(int i=0; i < KEYS_MAX; i++) {
    xcrypt_wipe(g.keys[i].key, 32);
  }
  memset(g.keys, 0, sizeof g.keys);
  media_forget();
  g.me[0] = 0;
  g.ready = g.count = g.next = 0;
  pthread_mutex_unlock(&g.lock);
}


int
xchat_open_ready(const char *me) {
  int r;

  pthread_mutex_lock(&g.lock);
  r = g.ready && (!me || !strcmp(g.me, me));
  pthread_mutex_unlock(&g.lock);
  return r;
}


int
xchat_open_keys(void) {
  int n;

  pthread_mutex_lock(&g.lock);
  n = g.count;
  pthread_mutex_unlock(&g.lock);
  return n;
}


/*
 * the hooks
 */
void
xchat_open_key(const char *conversation, const char *version, const char *user, const char *wrapped) {
  uint8_t raw[WRAPPED_MAX], key[32];
  long n;

  (void)user; // the cipher authenticates, so the one that opens is the user's
  if(!conversation || !conversation[0] || !version || !wrapped) {
    return;
  }
  pthread_mutex_lock(&g.lock);
  if(!g.ready || find(conversation, version)) {
    pthread_mutex_unlock(&g.lock);
    return;
  }
  n = b64_decode(wrapped, raw, sizeof raw);
  if(n > 0 && !xcrypt_key_open(g.identity, raw, (size_t)n, key)) {
    keep(conversation, version, key);
  }
  xcrypt_wipe(raw, sizeof raw);
  xcrypt_wipe(key, sizeof key);
  pthread_mutex_unlock(&g.lock);
}


int
xchat_open_entry(const char *conversation, const char *version, int encryption, const char *sender,
                 const char *message_id, const unsigned char *contents, size_t len, xchat_entry_t *entry) {
  uint8_t ckey[32], mkey[32];
  keyentry_t *e;
  uint8_t *plain;
  int ok = 0;

  memset(entry, 0, sizeof *entry);
  if(!conversation || !version || !contents || len < 40) {
    return 0;
  }
  pthread_mutex_lock(&g.lock);
  if((e=find(conversation, version))) {
    memcpy(ckey, e->key, 32);
  }
  pthread_mutex_unlock(&g.lock);
  if(!e) {
    return 0;
  }
  // the message's own key when it is encrypted with one (version 1 and up),
  // else the conversation key itself
  if(xcrypt_message_key(ckey, encryption >= 1 ? 1 : 0, sender, message_id, mkey)) {
    xcrypt_wipe(ckey, sizeof ckey);
    return 0;
  }
  if((plain=mem_alloc(len))) { // the plaintext is len - 40 bytes
    if(!xcrypt_message_open(mkey, contents, len, plain) && !xchat_entry(plain, len - 40, entry)) {
      xchat_attach_t *att = &entry->att;
      if(entry->media_kind && att->url[0]) {
        // its bytes are encrypted too: the attachment's key, from the
        // message's key and the id X resolves (its own, or the message's)
        const char *id = att->att_id[0] ? att->att_id : message_id;
        if(xcrypt_attachment_key(mkey, id, att->key)) {
          memset(att, 0, sizeof *att); // no key: it cannot be opened
          entry->media_kind = 0;
        } else {
          att->enc = 1;
          // for the player (a GIF, video or voice note), and what its bytes
          // must hash to once opened
          media_keep(att->url, att->key, att->has_sha256 ? att->sha256 : NULL,
                     entry->media_kind >= 2 && entry->media_kind <= 4);
        }
      }
      ok = entry->kind != XCHAT_ENTRY_NONE;
    }
    xcrypt_wipe(plain, len);
    mem_free(plain);
  }
  if(!ok) {
    xcrypt_wipe(entry, sizeof *entry);
  }
  xcrypt_wipe(ckey, sizeof ckey);
  xcrypt_wipe(mkey, sizeof mkey);
  return ok;
}


int
xchat_open_text(const char *conversation, const char *version, int encryption, const char *sender,
                const char *message_id, const unsigned char *contents, size_t len, char *text,
                size_t size, int *media, xchat_attach_t *att) {
  xchat_entry_t *entry = mem_alloc(sizeof *entry);
  int ok = 0;

  if(media) {
    *media = 0;
  }
  if(att) {
    memset(att, 0, sizeof *att);
  }
  if(size) {
    text[0] = 0;
  }
  if(entry && xchat_open_entry(conversation, version, encryption, sender, message_id, contents, len, entry) &&
     entry->kind == XCHAT_ENTRY_MESSAGE) {
    if(size) {
      snprintf(text, size, "%s", entry->text);
    }
    if(media) {
      *media = entry->media_kind;
    }
    if(att && entry->media_kind) {
      *att = entry->att;
    }
    ok = entry->text[0] != 0 || entry->media_kind; // a photo with no caption still opens
  }
  if(entry) {
    xcrypt_wipe(entry, sizeof *entry);
    mem_free(entry);
  }
  return ok;
}


/*
 * The senders' signing keys, and the messages' signatures checked with them
 */

/**
 * whether user is a sender's number X would know: digits only
 **/
static int
user_ok(const char *user) {
  size_t n = user ? strlen(user) : 0;

  if(!n || n >= sizeof g.signers[0].user) {
    return 0;
  }
  for(size_t i=0; i < n; i++) {
    if(user[i] < '0' || user[i] > '9') {
      return 0;
    }
  }
  return 1;
}


/**
 * the sender user's keys, or NULL; under the lock
 **/
static signer_t *
signer_find(const char *user) {
  for(int i=0; i < SIGNERS_MAX; i++) {
    if(g.signers[i].user[0] && !strcmp(g.signers[i].user, user)) {
      return &g.signers[i];
    }
  }
  return NULL;
}


/**
 * a place for the sender user's keys: theirs, or a free one, or the oldest
 * taken; under the lock
 **/
static signer_t *
signer_place(const char *user) {
  signer_t *s = signer_find(user);

  for(int i=0; !s && i < SIGNERS_MAX; i++) {
    if(!g.signers[i].user[0]) {
      s = &g.signers[i];
    }
  }
  if(!s) {
    s = &g.signers[g.signers_next];
    g.signers_next = (g.signers_next + 1) % SIGNERS_MAX;
  }
  if(strcmp(s->user, user)) {
    memset(s, 0, sizeof *s);
    snprintf(s->user, sizeof s->user, "%s", user);
  }
  return s;
}


void
xchat_open_signer(const char *user, const char *const *versions, const char *const *keys, int count,
                  long long now) {
  signer_t *s;
  uint8_t raw[96];

  if(!user_ok(user)) {
    return;
  }
  pthread_mutex_lock(&g.lock);
  s = signer_place(user);
  s->fetched = now > 0 ? now : 1;
  s->miss = 0;
  s->count = 0;
  for(int i=0; i < count && s->count < SIGNER_KEYS; i++) {
    long n = keys && keys[i] ? b64_decode(keys[i], raw, sizeof raw) : -1;
    if(n > 0 && !xcrypt_p256_key(raw, (size_t)n, s->key[s->count])) {
      snprintf(s->version[s->count], sizeof s->version[0], "%s", versions && versions[i] ? versions[i] : "");
      s->count++;
    }
  }
  pthread_mutex_unlock(&g.lock);
}


int
xchat_open_wanted(long long now, char (*users)[24], int max) {
  int n = 0;

  pthread_mutex_lock(&g.lock);
  for(int i=0; i < SIGNERS_MAX && n < max; i++) {
    const signer_t *s = &g.signers[i];
    if(s->user[0] && (!s->fetched || (s->miss && now - s->fetched >= XCHAT_SIGNER_AGAIN))) {
      snprintf(users[n++], 24, "%s", s->user);
    }
  }
  pthread_mutex_unlock(&g.lock);
  return n;
}


int
xchat_open_verify(const xchat_signed_t *e) {
  char version[SIGNER_KEYS][32];
  uint8_t key[SIGNER_KEYS][65], sig[64];
  xcrypt_signed_t m;
  signer_t *s;
  int count, known = 0, order[SIGNER_KEYS], k = 0;
  long n;

  if(!e || !user_ok(e->sender) || !e->signature || !e->public_key_version ||
     !(m.version=xcrypt_signed_version(e->signature_version))) {
    return XCHAT_SIG_NONE; // no signature this can check
  }
  pthread_mutex_lock(&g.lock);
  s = signer_place(e->sender); // wanted, when they are not known yet
  count = s->fetched ? s->count : 0;
  if(s->fetched && !count) {
    s->miss = 1; // X listed none (or did not answer): asked again later
  }
  memcpy(version, s->version, sizeof version);
  memcpy(key, s->key, sizeof key);
  pthread_mutex_unlock(&g.lock);
  if(!count) {
    return XCHAT_SIG_NONE; // the sender's keys are not in hand
  }
  m.message_id = e->message_id;
  m.sender = e->sender;
  m.conversation = e->conversation;
  m.key_version = e->key_version;
  m.contents = e->contents;
  m.len = e->len;
  m.has_encryption = e->has_encryption;
  m.encryption = e->encryption;
  // the key of the version it names first, then the sender's others
  for(int i=0; i < count; i++) {
    if(!strcmp(version[i], e->public_key_version)) {
      order[k++] = i;
      known = 1;
    }
  }
  for(int i=0; i < count; i++) {
    if(strcmp(version[i], e->public_key_version)) {
      order[k++] = i;
    }
  }
  n = b64_decode(e->signature, sig, sizeof sig);
  for(int i=0; n == 64 && i < k; i++) {
    if(!xcrypt_signed_verify(key[order[i]], &m, sig)) {
      return XCHAT_SIG_OK;
    }
  }
  if(known) {
    return XCHAT_SIG_BAD; // the key it names did not sign this
  }
  // a version not among the keys in hand: theirs may be newer, ask again
  pthread_mutex_lock(&g.lock);
  if((s=signer_find(e->sender))) {
    s->miss = 1;
  }
  pthread_mutex_unlock(&g.lock);
  return XCHAT_SIG_NONE;
}


void
xchat_open_install(void) {
  static const xchat_hooks_t hooks = {xchat_open_key, xchat_open_entry, xchat_open_verify};
  xchat_set_hooks(&hooks);
}


/*
 * the identity against the keys X lists for the user. X's SDK keeps
 * identity || signing (32 bytes each) in Juicebox, but what X's own apps
 * keep there is not written anywhere, so every 32 bytes of the secret are
 * tried, and those of the base64 or hex words in it when it is text: a
 * private key whose public key X lists for the user cannot be a mistake.
 */

#define POINTS_MAX 16 // public keys read from X's list

typedef struct {
  uint8_t p[65]; // 04 x y, or 02/03 x (33 bytes)
  int     len;
  int     signing;
} point_t;

static int
hex_value(int c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/**
 * s (len characters) as hex, when it is all hex and whole bytes; the bytes,
 * or -1
 **/
static long
hex_decode(const char *s, size_t len, uint8_t *out, size_t size) {
  if(!len || len % 2 || len / 2 > size) {
    return -1;
  }
  for(size_t i=0; i < len; i += 2) {
    int hi = hex_value((unsigned char)s[i]), lo = hex_value((unsigned char)s[i + 1]);
    if(hi < 0 || lo < 0) {
      return -1;
    }
    out[i / 2] = (uint8_t)(hi << 4 | lo);
  }
  return (long)(len / 2);
}

/**
 * a public key as X may write it (base64, standard or for URLs, with or
 * without its padding, or hex; of a SEC1 point, its SPKI, a compressed
 * point or the bare x y) into pt; 0, or -1
 **/
static int
point_read(const char *s, point_t *pt) {
  uint8_t raw[128];
  size_t slen = s ? strlen(s) : 0;
  long n = slen && slen < 2 * sizeof raw ? hex_decode(s, slen, raw, sizeof raw) : -1;

  if(n != 65 && n != 91 && n != 33 && n != 64) {
    n = slen ? b64_decode(s, raw, sizeof raw) : -1;
  }
  if((n == 65 || n == 91) && !xcrypt_p256_key(raw, (size_t)n, pt->p)) {
    pt->len = 65;
  } else if(n == 33 && (raw[0] == 2 || raw[0] == 3)) {
    memcpy(pt->p, raw, 33);
    pt->len = 33;
  } else if(n == 64) {
    pt->p[0] = 4;
    memcpy(pt->p + 1, raw, 64);
    pt->len = 65;
  } else {
    return -1;
  }
  return 0;
}

/**
 * whether the public key mine (65 bytes) is pt
 **/
static int
point_is(const uint8_t mine[65], const point_t *pt) {
  if(pt->len == 33) {
    return pt->p[0] == 2 + (mine[64] & 1) && !memcmp(pt->p + 1, mine + 1, 32);
  }
  return !memcmp(pt->p, mine, 65);
}

typedef struct {
  const point_t *points;
  int            count;
  int            tried;
  int            found;     // 1: a key X lists as the identity, 2: as the signing one
  int            at;        // where in the bytes looked at
  int            raw;       // whether those were the secret itself
  uint8_t        key[32];   // the identity found
} search_t;

/**
 * every 32 bytes of data (len) tried as a private key; stops at the first
 * that is an identity X lists, remembers the first that is a signing key
 **/
static void
search(search_t *s, const uint8_t *data, long len, int raw) {
  uint8_t mine[65];

  for(long off=0; off + 32 <= len && s->found != 1; off++) {
    if(xcrypt_p256_public(data + off, mine)) {
      continue;
    }
    s->tried++;
    for(int i=0; i < s->count; i++) {
      if(!point_is(mine, &s->points[i])) {
        continue;
      }
      if(!s->points[i].signing) {
        s->found = 1;
        memcpy(s->key, data + off, 32);
        break;
      }
      if(!s->found) {
        s->found = 2;
        s->at = (int)off;
        s->raw = raw;
      }
    }
  }
  xcrypt_wipe(mine, sizeof mine);
}

/**
 * the base64 and hex words of a secret that is text, each decoded and
 * searched
 **/
static void
search_text(search_t *s, const uint8_t *secret, int len) {
  char word[XCHAT_SECRET_MAX + 1];
  uint8_t raw[XCHAT_SECRET_MAX];

  for(int i=0; i < len; i++) {
    if(secret[i] < 0x20 || secret[i] > 0x7e) {
      return; // not text
    }
  }
  for(int i=0; i < len && s->found != 1;) {
    int n = 0;
    long got;
    while(i < len && b64_value(secret[i]) < 0) {
      i++;
    }
    while(i < len && (b64_value(secret[i]) >= 0 || secret[i] == '=') && n < (int)sizeof word - 1) {
      word[n++] = (char)secret[i++];
    }
    word[n] = 0;
    if(n < 43) {
      continue; // fewer than 32 bytes in it
    }
    if((got=hex_decode(word, (size_t)n, raw, sizeof raw)) >= 32) {
      search(s, raw, got, 0);
    }
    if(s->found != 1 && (got=b64_decode(word, raw, sizeof raw)) >= 32) {
      search(s, raw, got, 0);
    }
  }
  xcrypt_wipe(word, sizeof word);
  xcrypt_wipe(raw, sizeof raw);
}

int
xchat_open_identity(const uint8_t *secret, int len, const char *const *public_keys, const int *signing, int count,
                    uint8_t identity[32], xchat_identity_t *seen) {
  point_t points[POINTS_MAX];
  uint8_t other[65];
  search_t s;
  int n = 0, ret = 0;

  if(seen) {
    memset(seen, 0, sizeof *seen);
    seen->secret = len;
    seen->keys = count;
  }
  if(!secret || len < 32 || len > XCHAT_SECRET_MAX) {
    return -1;
  }
  for(int i=0; i < count && n < POINTS_MAX; i++) {
    if(public_keys[i] && !point_read(public_keys[i], &points[n])) {
      points[n++].signing = signing ? signing[i] : 0;
    }
  }
  memset(&s, 0, sizeof s);
  s.points = points;
  s.count = n;
  search(&s, secret, len, 1);
  if(s.found != 1) {
    search_text(&s, secret, len);
  }
  if(s.found == 1) {
    memcpy(identity, s.key, 32);
    ret = 1;
  } else if(s.found == 2 && s.raw && len == 64 && (s.at == 0 || s.at == 32) &&
            !xcrypt_p256_public(secret + (32 - s.at), other)) {
    // only the signing half is listed: the identity is the other half
    memcpy(identity, secret + (32 - s.at), 32);
    ret = 1;
  }
  if(seen) {
    seen->points = n;
    seen->tried = s.tried;
    seen->signing = s.found == 2;
  }
  ret = ret ? 1 : s.tried ? 0 : -1;
  xcrypt_wipe(&s, sizeof s);
  memset(points, 0, sizeof points);
  return ret;
}


/*
 * the key file: the magic and the 32 bytes of the identity
 */
int
xchat_open_path(const char *dir, const char *me, char *buf, size_t size) {
  int n = snprintf(buf, size, "%s/xchat-%s.key", dir ? dir : ".", me ? me : "");
  return n < 0 || (size_t)n >= size ? -1 : 0;
}


int
xchat_open_load(const char *path, uint8_t identity[32]) {
  char magic[sizeof MAGIC - 1];
  uint8_t id[32];
  FILE *f = fopen(path, "rb");
  int ok;

  if(!f) {
    return -1;
  }
  ok = fread(magic, 1, sizeof magic, f) == sizeof magic && !memcmp(magic, MAGIC, sizeof magic) &&
       fread(id, 1, 32, f) == 32;
  fclose(f);
  if(ok) {
    memcpy(identity, id, 32);
  }
  xcrypt_wipe(id, sizeof id);
  return ok ? 0 : -1;
}


int
xchat_open_save(const char *path, const uint8_t identity[32]) {
  FILE *f = fopen(path, "wb");
  int ok;

  if(!f) {
    return -1;
  }
  ok = fwrite(MAGIC, 1, sizeof MAGIC - 1, f) == sizeof MAGIC - 1 && fwrite(identity, 1, 32, f) == 32;
  if(fclose(f)) {
    ok = 0;
  }
  return ok ? 0 : -1;
}
