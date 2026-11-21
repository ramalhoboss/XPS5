/* Host tests for src/xchatopen.c and the hooks of src/xchat.c: a
conversation's key opened from a key change event with the user's identity
key (the ECIES vectors xcrypt_test shares), and a message opened with it;
the three-level plaintext; the identity against the keys X lists; the key
file; and a conversation page read whole. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "mem.h"
#include "xchat.h"
#include "xchatopen.h"
#include "xcrypt.h"

static int g_failed;

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_failed++; } } while(0)

// the same vectors as tests/xcrypt_test.c: identity -> opens WRAPPED -> KEY
static const char ECIES_IDENTITY[] = "01d2c3b4a59687ff00112233445566778899aabbccddeeff0102030405060708";
static const char ECIES_IDENTITY_PUB[] =
  "04c95339c7b818e2acfaa1e7b930772898c59c05e50ea5dec75b31db460b5fcfd3"
  "a12fdef36f64504a5c6bdcfdb4f23bcf1460fbae4fd836d6d411bdeeba6be61e";
static const char ECIES_WRAPPED[] =
  "04f28ab7f9bf45373fbd0d9b20fb314a7177db2e14acebdd018cdceeda3192a7d4"
  "dbefa289f69cd13f4ac9ab631148e6c66a4cf30d51e432a55295742a31ef65661e"
  "8794be8777ff53da5197c6591636b22b8620bbf4190cad25fb7952d00e6819b0b0602b38b24634862d91b9f5b26b2c";


/*
 * helpers: random for xcrypt, hex, base64, Thrift
 */
static void
fill_random(void *buf, size_t len) {
  unsigned char *p = buf;
  static unsigned s = 0x1234567u;

  for(size_t i=0; i < len; i++) {
    s = s * 1103515245u + 12345u;
    p[i] = (unsigned char)(s >> 16);
  }
}

static size_t
unhex(const char *s, unsigned char *out) {
  size_t n = 0;

  for(; s[0] && s[1]; s += 2) {
    unsigned v;
    sscanf(s, "%2x", &v);
    out[n++] = (unsigned char)v;
  }
  return n;
}

static void
to_b64(const unsigned char *in, size_t n, char *out) {
  static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t i, o = 0;

  for(i=0; i + 2 < n; i += 3) {
    unsigned v = (unsigned)in[i] << 16 | (unsigned)in[i + 1] << 8 | in[i + 2];
    out[o++] = abc[v >> 18];
    out[o++] = abc[(v >> 12) & 63];
    out[o++] = abc[(v >> 6) & 63];
    out[o++] = abc[v & 63];
  }
  if(n - i == 1) {
    unsigned v = (unsigned)in[i] << 16;
    out[o++] = abc[v >> 18];
    out[o++] = abc[(v >> 12) & 63];
    out[o++] = '=';
    out[o++] = '=';
  } else if(n - i == 2) {
    unsigned v = (unsigned)in[i] << 16 | (unsigned)in[i + 1] << 8;
    out[o++] = abc[v >> 18];
    out[o++] = abc[(v >> 12) & 63];
    out[o++] = abc[(v >> 6) & 63];
    out[o++] = '=';
  }
  out[o] = 0;
}

typedef struct writer {
  unsigned char b[4096];
  size_t n;
} writer_t;

static void w8(writer_t *w, unsigned v) { w->b[w->n++] = (unsigned char)v; }
static void w16(writer_t *w, unsigned v) { w8(w, v >> 8); w8(w, v); }
static void w32(writer_t *w, unsigned long v) { w16(w, (unsigned)(v >> 16)); w16(w, (unsigned)v); }
static void field(writer_t *w, int type, int id) { w8(w, (unsigned)type); w16(w, (unsigned)id); }

static void
wbytes(writer_t *w, int id, const void *p, size_t n) {
  field(w, 11, id);
  w32(w, n);
  memcpy(w->b + w->n, p, n);
  w->n += n;
}

static void wstr(writer_t *w, int id, const char *s) { wbytes(w, id, s, strlen(s)); }


/**
 * the plaintext of a message with those words: MessageEntryHolder{1
 * contents{1 message{1 text}}}
 **/
static size_t
holder(unsigned char *out, const char *text) {
  writer_t h = {{0}, 0};

  field(&h, 12, 1);
  field(&h, 12, 1);
  wstr(&h, 1, text);
  w8(&h, 0);
  w8(&h, 0);
  w8(&h, 0);
  memcpy(out, h.b, h.n);
  return h.n;
}


/**
 * a holder whose message carries a MediaAttachment of kind (1 photo, 3
 * video, ...), with an optional caption
 **/
static size_t
holder_media_ms(unsigned char *out, const char *caption, int kind, const char *url, unsigned long ms) {
  writer_t h = {{0}, 0};

  field(&h, 12, 1);             // MessageEntryHolder.contents
  field(&h, 12, 1);             // MessageEntryContents.message
  if(caption && caption[0]) {
    wstr(&h, 1, caption);       // MessageContents.message_text
  }
  field(&h, 15, 3);             // MessageContents.attachments, a list
  w8(&h, 12);                   // of structs
  w32(&h, 1);                   // one
  field(&h, 12, 1);             // MessageAttachment.media (a MediaAttachment)
  field(&h, 12, 2);             // MediaAttachment.dimensions
  field(&h, 10, 1);             // width (i64)
  w32(&h, 0);
  w32(&h, 800);
  field(&h, 10, 2);             // height (i64)
  w32(&h, 0);
  w32(&h, 600);
  w8(&h, 0);                    // end MediaDimensions
  field(&h, 8, 3);              // type (i32)
  w32(&h, (unsigned long)kind);
  if(ms) {
    field(&h, 10, 4);           // duration_millis (i64)
    w32(&h, 0);
    w32(&h, ms);
  }
  if(url && url[0]) {
    wstr(&h, 8, url);           // legacy_media_url_https
  }
  w8(&h, 0);                    // end MediaAttachment
  w8(&h, 0);                    // end MessageAttachment
  w8(&h, 0);                    // end MessageContents
  w8(&h, 0);                    // end MessageEntryContents
  w8(&h, 0);                    // end MessageEntryHolder
  memcpy(out, h.b, h.n);
  return h.n;
}


static size_t
holder_media(unsigned char *out, const char *caption, int kind, const char *url) {
  return holder_media_ms(out, caption, kind, url, 0);
}


/*
 * the tests
 */
static void
test_plaintext(void) {
  unsigned char bytes[256];
  char text[256];
  size_t n = holder(bytes, "ol\303\241 mundo");

  xchat_plaintext(bytes, n, text, sizeof text);
  CHECK(!strcmp(text, "ol\303\241 mundo"));

  // a reaction (contents.2) has no words
  writer_t h = {{0}, 0};
  field(&h, 12, 1);       // holder.contents
  field(&h, 12, 2);       // contents.reaction_add
  wstr(&h, 1, "\xf0\x9f\x91\x8d");
  w8(&h, 0);
  w8(&h, 0);
  w8(&h, 0);
  xchat_plaintext(h.b, h.n, text, sizeof text);
  CHECK(!text[0]);

  // nonsense gives nothing, never a crash
  xchat_plaintext((const unsigned char *)"\x13\x37not thrift", 12, text, sizeof text);
  CHECK(!text[0]);

  // the first attachment: its kind, url and dimensions
  xchat_attach_t att;
  n = holder_media(bytes, "", 3, "https://x.example/v");
  CHECK(xchat_media_kind(bytes, n) == 3);
  CHECK(xchat_attachment(bytes, n, &att) && att.kind == 3 && !strcmp(att.url, "https://x.example/v"));
  CHECK(att.width == 800 && att.height == 600 && !att.enc);
  n = holder(bytes, "s\xc3\xb3 texto");
  CHECK(xchat_media_kind(bytes, n) == 0 && !xchat_attachment(bytes, n, &att));
}


static void
test_decrypt(void) {
  unsigned char identity[32], wrapped[160], plain[256], box[512];
  char wrapped_b64[256], text[256];
  const char *sender = "1234567890", *msgid = "a1b2c3d4-0000-4000-8000-123456789abc";
  uint8_t ckey[32], mkey[32];
  size_t hn, wn;
  int media;
  xchat_attach_t att;

  unhex(ECIES_IDENTITY, identity);
  wn = unhex(ECIES_WRAPPED, wrapped);
  to_b64(wrapped, wn, wrapped_b64);

  xchat_open_install();
  xchat_open_set("me", identity);
  CHECK(xchat_open_ready("me") && !xchat_open_ready("other"));

  // the key change event's key for me opens, and is kept
  CHECK(xchat_open_keys() == 0);
  xchat_open_key("111:222", "7", "me", wrapped_b64);
  CHECK(xchat_open_keys() == 1);
  // the same conversation and version again is not kept twice
  xchat_open_key("111:222", "7", "me", wrapped_b64);
  CHECK(xchat_open_keys() == 1);

  // the opened conversation key is ECIES_KEY, so seal a message with it and
  // the hook opens it (version 0: the conversation key itself)
  unhex("a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf", ckey);
  hn = holder(plain, "segredo um");
  CHECK(!xcrypt_message_seal(ckey, plain, hn, box));
  CHECK(xchat_open_text("111:222", "7", 0, sender, msgid, box, hn + 40, text, sizeof text, &media, NULL) == 1);
  CHECK(!strcmp(text, "segredo um"));

  // version 1: the per-message key derived from the conversation key
  CHECK(!xcrypt_message_key(ckey, 1, sender, msgid, mkey));
  hn = holder(plain, "segredo dois");
  CHECK(!xcrypt_message_seal(mkey, plain, hn, box));
  CHECK(xchat_open_text("111:222", "7", 1, sender, msgid, box, hn + 40, text, sizeof text, &media, NULL) == 1);
  CHECK(!strcmp(text, "segredo dois"));

  // a photo with no caption still opens, with its kind and the attachment
  // (url, dimensions) and a key to open its bytes
  hn = holder_media(plain, "", 1, "https://x.example/p.enc");
  CHECK(!xcrypt_message_seal(ckey, plain, hn, box));
  media = -1;
  CHECK(xchat_open_text("111:222", "7", 0, sender, msgid, box, hn + 40, text, sizeof text, &media, &att) == 1);
  CHECK(media == 1 && !text[0]);
  CHECK(att.kind == 1 && !strcmp(att.url, "https://x.example/p.enc") && att.enc && att.width == 800);
  {
    uint8_t zero[32] = {0};
    CHECK(memcmp(att.key, zero, 32) != 0); // a key was derived
  }
  // a video with a caption: the words, the kind and the attachment
  hn = holder_media(plain, "olha isto", 3, "https://x.example/v.enc");
  CHECK(!xcrypt_message_seal(ckey, plain, hn, box));
  media = 0;
  CHECK(xchat_open_text("111:222", "7", 0, sender, msgid, box, hn + 40, text, sizeof text, &media, &att) == 1);
  CHECK(media == 3 && !strcmp(text, "olha isto") && att.kind == 3 && att.enc);
  // a video is kept by its token for the player, with its url and key
  {
    char token[XCHAT_TOKEN], url[256];
    uint8_t key[32];
    xchat_media_token(att.url, token, sizeof token);
    CHECK(!strncmp(token, "xchat/", 6) && strlen(token) == 22);
    CHECK(!xchat_open_media(token, url, sizeof url, key));
    CHECK(!strcmp(url, "https://x.example/v.enc") && !memcmp(key, att.key, 32));
    CHECK(xchat_open_media("xchat/0000000000000000", url, sizeof url, key) == -1);
    CHECK(xchat_open_media(token, url, 8, key) == -1); // no room for its url
  }
  // a voice note: its length too; a photo is not kept for the player
  hn = holder_media_ms(plain, "", 4, "https://x.example/n.enc", 14250);
  CHECK(!xcrypt_message_seal(ckey, plain, hn, box));
  CHECK(xchat_open_text("111:222", "7", 0, sender, msgid, box, hn + 40, text, sizeof text, &media, &att) == 1);
  CHECK(media == 4 && att.kind == 4 && att.duration_ms == 14250 && att.enc);
  {
    char token[XCHAT_TOKEN], url[256];
    uint8_t key[32];
    xchat_media_token("https://x.example/p.enc", token, sizeof token);
    CHECK(xchat_open_media(token, url, sizeof url, key) == -1);
  }

  // a version with no key is not opened
  CHECK(xchat_open_text("111:222", "9", 0, sender, msgid, box, hn + 40, text, sizeof text, &media, NULL) == 0 && !text[0]);

  // a wrong per-message key (version 1 derives it from the message id) does
  // not open a message sealed with another
  CHECK(xchat_open_text("111:222", "7", 1, sender, "other-id", box, hn + 40, text, sizeof text, &media, NULL) == 0);

  // another user forgets the keys
  xchat_open_set("you", identity);
  CHECK(xchat_open_keys() == 0 && xchat_open_ready("you"));
  xchat_open_forget();
  CHECK(!xchat_open_ready(NULL));
  xchat_set_hooks(NULL);
}


static void
test_identity(void) {
  unsigned char identity[32], pub[65], out[32];
  char pub_b64[128], other_b64[128];
  const char *keys[2];
  size_t pn;

  unhex(ECIES_IDENTITY, identity);
  pn = unhex(ECIES_IDENTITY_PUB, pub);
  to_b64(pub, pn, pub_b64);
  // a different point
  pub[40] ^= 1;
  to_b64(pub, pn, other_b64);

  keys[0] = other_b64;
  keys[1] = pub_b64;
  CHECK(xchat_open_identity(identity, 32, keys, NULL, 2, out, NULL) == 1 && !memcmp(out, identity, 32));
  CHECK(xchat_open_identity(identity, 32, keys, NULL, 1, out, NULL) == 0); // only the other one
  CHECK(xchat_open_identity(identity, 16, keys, NULL, 2, out, NULL) == -1); // a bad length

  // a secret of identity || signing (64 bytes) finds the first 32
  unsigned char secret64[64], signing[32], spub[65], swapped[64], prefixed[65];
  char spub_b64[128], spki_b64[160], comp_b64[64], hex[140], url[128], text[200], raw_b64[100];
  xchat_identity_t seen;
  memcpy(secret64, identity, 32);
  memset(signing, 0x5a, 32);
  memcpy(secret64 + 32, signing, 32);
  memset(out, 0, 32);
  CHECK(xchat_open_identity(secret64, 64, keys, NULL, 2, out, &seen) == 1 && !memcmp(out, identity, 32));
  CHECK(seen.secret == 64 && seen.keys == 2 && seen.points == 2 && seen.tried >= 1);

  // signing || identity: the identity is still the one X lists as such
  memcpy(swapped, signing, 32);
  memcpy(swapped + 32, identity, 32);
  memset(out, 0, 32);
  CHECK(xchat_open_identity(swapped, 64, keys, NULL, 2, out, NULL) == 1 && !memcmp(out, identity, 32));

  // a byte before the keys (a version, say)
  prefixed[0] = 1;
  memcpy(prefixed + 1, secret64, 64);
  memset(out, 0, 32);
  CHECK(xchat_open_identity(prefixed, 65, keys, NULL, 2, out, NULL) == 1 && !memcmp(out, identity, 32));

  // only the signing key listed: the identity is the other half
  CHECK(!xcrypt_p256_public(signing, spub));
  to_b64(spub, 65, spub_b64);
  const char *only_signing[1] = {spub_b64};
  const int is_signing[2] = {1, 0};
  memset(out, 0, 32);
  CHECK(xchat_open_identity(secret64, 64, only_signing, is_signing, 1, out, &seen) == 1 && !memcmp(out, identity, 32));
  CHECK(seen.signing == 1);
  // and the signing key is never taken for the identity
  const char *both[2] = {spub_b64, pub_b64};
  memset(out, 0, 32);
  CHECK(xchat_open_identity(swapped, 64, both, is_signing, 2, out, NULL) == 1 && !memcmp(out, identity, 32));
  // a 32-byte secret that is only the signing key is not an identity
  CHECK(xchat_open_identity(signing, 32, only_signing, is_signing, 1, out, NULL) == 0);

  // the public key in other writings: SPKI, compressed, hex, base64 for URLs without padding, bare x y
  unsigned char spki[91] = {0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,
                            0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00};
  unsigned char comp[33];
  memcpy(spki + 26, pub, 65);
  spki[26 + 40] ^= 1; // pub was changed above: back to the identity's
  to_b64(spki, 91, spki_b64);
  comp[0] = (unsigned char)(2 + (spki[26 + 64] & 1));
  memcpy(comp + 1, spki + 27, 32);
  to_b64(comp, 33, comp_b64);
  for(int i=0; i < 65; i++) {
    snprintf(hex + 2 * i, 3, "%02x", spki[26 + i]);
  }
  snprintf(url, sizeof url, "%s", pub_b64);
  for(char *c=url; *c; c++) {
    *c = *c == '+' ? '-' : *c == '/' ? '_' : *c;
  }
  if(strchr(url, '=')) {
    *strchr(url, '=') = 0;
  }
  to_b64(spki + 27, 64, raw_b64);
  const char *forms[5] = {spki_b64, comp_b64, hex, url, raw_b64};
  for(int i=0; i < 5; i++) {
    memset(out, 0, 32);
    CHECK(xchat_open_identity(secret64, 64, &forms[i], NULL, 1, out, NULL) == 1 && !memcmp(out, identity, 32));
  }

  // a secret that is text: the keys in base64, or in hex
  char b64[100];
  to_b64(identity, 32, b64);
  snprintf(text, sizeof text, "{\"identity\":\"%s\"}", b64);
  memset(out, 0, 32);
  CHECK(xchat_open_identity((const unsigned char *)text, (int)strlen(text), keys, NULL, 2, out, &seen) == 1 &&
        !memcmp(out, identity, 32));
  for(int i=0; i < 32; i++) {
    snprintf(text + 2 * i, 3, "%02x", identity[i]);
  }
  memset(out, 0, 32);
  CHECK(xchat_open_identity((const unsigned char *)text, 64, keys, NULL, 2, out, NULL) == 1 &&
        !memcmp(out, identity, 32));

  // nothing of this account's: 0, and what was tried
  const char *other_only[1] = {other_b64};
  CHECK(xchat_open_identity(secret64, 64, other_only, NULL, 1, out, &seen) == 0 && seen.points == 1 &&
        seen.tried > 0 && !seen.signing);
}


static void
test_file(void) {
  unsigned char identity[32], got[32];
  char path[XCHAT_KEY_FILE_MAX];
  const char *dir = getenv("TMPDIR");

  unhex(ECIES_IDENTITY, identity);
  CHECK(!xchat_open_path(dir ? dir : ".", "42", path, sizeof path));
  CHECK(strstr(path, "xchat-42.key"));
  CHECK(!xchat_open_save(path, identity));
  memset(got, 0, sizeof got);
  CHECK(!xchat_open_load(path, got) && !memcmp(got, identity, 32));
  remove(path);
  CHECK(xchat_open_load(path, got) == -1); // gone now
  // a path that does not fit
  CHECK(xchat_open_path("x", "123456789012345678901234567890", path, 16) == -1);
}


static void
test_url(void) {
  char url[2048];

  CHECK(!xchat_conversation_url(url, sizeof url, "111:222", NULL));
  CHECK(!strncmp(url, "https://api.x.com/graphql/", 26));
  CHECK(strstr(url, "/GetConversationPageQuery?variables=%7B"));
  CHECK(strstr(url, "111%3A222") && strstr(url, "min_local_sequence_id") && !strchr(url, '{'));
  // the newest page, as x.com asks it: from Long.MAX back, every key change;
  // "0" made X answer "range order reversed"
  CHECK(strstr(url, "min_local_sequence_id%22%3A%229223372036854775807%22"));
  CHECK(strstr(url, "min_conversation_key_version%22%3A%229223372036854775807%22"));
  CHECK(!xchat_conversation_url(url, sizeof url, "111:222", "0") &&
        strstr(url, "min_local_sequence_id%22%3A%229223372036854775807%22"));
  CHECK(!xchat_conversation_url(url, sizeof url, "111:222", "1234") &&
        strstr(url, "min_local_sequence_id%22%3A%221234%22"));
  CHECK(xchat_conversation_url(url, 40, "111:222", "0") == -1);
  CHECK(xchat_conversation_url(url, sizeof url, "", "0") == -1);
}


/**
 * a MessageEvent that is a new message encrypted with the key, version 0
 **/
static void
msg_event_plain(char *out, const char *seq, const char *sender, const char *msgid, const uint8_t key[32],
                const unsigned char *plain, size_t hn) {
  unsigned char box[512];
  writer_t w = {{0}, 0};

  xcrypt_message_seal(key, plain, hn, box);
  wstr(&w, 1, seq);
  wstr(&w, 2, msgid);
  wstr(&w, 3, sender);
  wstr(&w, 4, "111:222");
  wstr(&w, 6, "1794000000000");
  field(&w, 12, 7);           // detail
  field(&w, 12, 1);           // messageCreateEvent
  wbytes(&w, 100, box, hn + 40);
  wstr(&w, 101, "7");         // conversation_key_version
  w8(&w, 0);                  // end messageCreateEvent
  w8(&w, 0);                  // end the detail union
  w8(&w, 0);                  // end the MessageEvent
  to_b64(w.b, w.n, out);
}


static void
msg_event(char *out, const char *seq, const char *sender, const char *msgid, const uint8_t key[32],
          const char *text) {
  unsigned char plain[256];

  msg_event_plain(out, seq, sender, msgid, key, plain, holder(plain, text));
}


/**
 * a MessageEvent that is a key change: the wrapped key for me
 **/
static void
key_event(char *out, const char *wrapped_b64) {
  writer_t w = {{0}, 0};

  wstr(&w, 1, "1");
  field(&w, 12, 7);           // detail
  field(&w, 12, 3);           // conversationKeyChangeEvent
  wstr(&w, 1, "7");           // version
  field(&w, 15, 2);           // list of participant keys
  w8(&w, 12);                 // of structs
  w32(&w, 1);                 // one
  wstr(&w, 1, "me");
  wstr(&w, 2, wrapped_b64);
  w8(&w, 0);                  // end the participant key
  w8(&w, 0);                  // end the key change event
  w8(&w, 0);                  // end the detail union
  w8(&w, 0);                  // end the MessageEvent
  to_b64(w.b, w.n, out);
}


static void
test_messages(void) {
  static char json[8192], k[2048], m1[2048], m2[2048];
  unsigned char wrapped[160];
  uint8_t ckey[32];
  char wrapped_b64[256];
  static x_post_t posts[8];
  json_doc_t *doc;
  size_t off, wn;
  int count;

  wn = unhex(ECIES_WRAPPED, wrapped);
  to_b64(wrapped, wn, wrapped_b64);
  unhex("a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf", ckey);

  key_event(k, wrapped_b64);
  msg_event(m1, "100", "1234567890", "id-1", ckey, "primeira");
  msg_event(m2, "102", "1234567890", "id-2", ckey, "segunda");

  // the key change comes apart, the two messages together (newest first, to
  // check they end up oldest first)
  snprintf(json, sizeof json,
    "{\"data\":{\"get_conversation_page\":{\"__typename\":\"XChatConversationPage\","
    "\"missing_conversation_key_change_events\":[\"%s\"],"
    "\"encoded_message_events\":[\"%s\",\"%s\"],\"has_more\":false}}}",
    k, m2, m1);
  doc = json_parse(json, strlen(json), &off);
  CHECK(doc != NULL);

  xchat_open_install();
  unsigned char identity[32];
  unhex(ECIES_IDENTITY, identity);
  xchat_open_set("1111", identity);

  CHECK(xchat_messages(json_root(doc), "111:222", "me", NULL, 0) == 2);
  count = xchat_messages(json_root(doc), "111:222", "me", posts, 8);
  CHECK(count == 2);
  // oldest first, opened
  CHECK(!strcmp(posts[0].id, "100") && !posts[0].sealed && !strcmp(posts[0].text, "primeira"));
  CHECK(!strcmp(posts[1].id, "102") && !posts[1].sealed && !strcmp(posts[1].text, "segunda"));
  CHECK(posts[0].encrypted && posts[1].encrypted); // came encrypted: the payload still tells of them
  CHECK(posts[0].kind == X_KIND_DM && posts[0].xchat && !strcmp(posts[0].user_id, "1234567890"));
  CHECK(!posts[0].media_count && posts[0].icon == X_DM_TEXT);
  json_free(doc);

  // a voice note and a video: their media plays from their token, which
  // gives the player their url and key; a photo shows from its url
  {
    static char m3[2048], m4[2048], m5[2048];
    unsigned char plain[256];
    char token[XCHAT_TOKEN], url[256], want[64];
    uint8_t key[32];

    msg_event_plain(m3, "104", "1234567890", "id-3", ckey, plain,
                    holder_media_ms(plain, "", 4, "https://x.example/n.enc", 9000));
    msg_event_plain(m4, "106", "1234567890", "id-4", ckey, plain,
                    holder_media_ms(plain, "v\xc3\xaa", 3, "https://x.example/v2.enc", 31000));
    msg_event_plain(m5, "108", "1234567890", "id-5", ckey, plain, holder_media(plain, "", 1, "https://x.example/p2.enc"));
    snprintf(json, sizeof json,
      "{\"data\":{\"get_conversation_page\":{\"__typename\":\"XChatConversationPage\","
      "\"missing_conversation_key_change_events\":[\"%s\"],"
      "\"encoded_message_events\":[\"%s\",\"%s\",\"%s\"],\"has_more\":false}}}",
      k, m5, m4, m3);
    doc = json_parse(json, strlen(json), &off);
    CHECK(doc != NULL);
    count = xchat_messages(json_root(doc), "111:222", "me", posts, 8);
    CHECK(count == 3);

    // the voice note
    xchat_media_token("https://x.example/n.enc", token, sizeof token);
    snprintf(want, sizeof want, "file://%s", token);
    CHECK(posts[0].icon == X_DM_VOICE && posts[0].media_count == 1 && posts[0].media[0].voice);
    CHECK(!posts[0].media[0].video && posts[0].media[0].xchat && posts[0].media[0].duration_ms == 9000);
    CHECK(!strcmp(posts[0].media[0].video_url, want) && !strcmp(posts[0].media[0].url, "https://x.example/n.enc"));
    CHECK(!xchat_open_media(token, url, sizeof url, key) && !memcmp(key, posts[0].media[0].xchat_key, 32));

    // the video, with its words
    xchat_media_token("https://x.example/v2.enc", token, sizeof token);
    snprintf(want, sizeof want, "file://%s", token);
    CHECK(posts[1].icon == X_DM_VIDEO && posts[1].media[0].video && !posts[1].media[0].voice);
    CHECK(!strcmp(posts[1].media[0].video_url, want) && posts[1].media[0].duration_ms == 31000);
    CHECK(!strcmp(posts[1].text, "v\xc3\xaa") && posts[1].media[0].width == 800);
    CHECK(!xchat_open_media(token, url, sizeof url, key) && !strcmp(url, "https://x.example/v2.enc"));

    // the photo: its url and key, no video
    CHECK(posts[2].icon == X_DM_PHOTO && posts[2].media_count == 1 && posts[2].media[0].xchat);
    CHECK(!posts[2].media[0].video && !posts[2].media[0].voice && !posts[2].media[0].video_url[0]);
    json_free(doc);
  }

  // the inbox, as the payload reads it for its toasts: the key change among
  // the item's events, its last message opened, still told of as encrypted
  snprintf(json, sizeof json,
    "{\"data\":{\"get_inbox_page\":{\"items\":[{\"conversation_detail\":{\"conversation_id\":\"111:222\","
    "\"participants_results\":[{\"rest_id\":\"1234567890\",\"result\":{\"core\":"
    "{\"name\":\"Ana\",\"screen_name\":\"ana\"}}},{\"rest_id\":\"me\"}]},"
    "\"latest_conversation_key_change_events\":[\"%s\"],"
    "\"latest_message_events\":[\"%s\",\"%s\"]}]}}}",
    k, m2, m1);
  doc = json_parse(json, strlen(json), &off);
  CHECK(doc != NULL);
  count = xchat_inbox(json_root(doc), "me", posts, 8);
  CHECK(count == 1 && !posts[0].sealed && posts[0].encrypted && !strcmp(posts[0].text, "segunda"));
  CHECK(posts[0].unread && !posts[0].mine && !strcmp(posts[0].user, "ana") && !posts[0].group);
  json_free(doc);

  // forgetting the keys forgets the media too
  {
    char token[XCHAT_TOKEN], url[256];
    uint8_t key[32];
    xchat_open_forget();
    xchat_media_token("https://x.example/n.enc", token, sizeof token);
    CHECK(xchat_open_media(token, url, sizeof url, key) == -1);
  }
  xchat_set_hooks(NULL);

  // without the hooks, the same messages stay sealed (json_parse works in
  // the buffer, so build it again)
  snprintf(json, sizeof json,
    "{\"data\":{\"get_conversation_page\":{\"__typename\":\"XChatConversationPage\","
    "\"missing_conversation_key_change_events\":[\"%s\"],"
    "\"encoded_message_events\":[\"%s\",\"%s\"],\"has_more\":false}}}",
    k, m2, m1);
  doc = json_parse(json, strlen(json), &off);
  count = xchat_messages(json_root(doc), "111:222", "me", posts, 8);
  CHECK(count == 2 && posts[0].sealed && !posts[0].text[0]);
  json_free(doc);

  // not a page
  snprintf(json, sizeof json, "%s", "{\"data\":{}}");
  doc = json_parse(json, strlen(json), &off);
  CHECK(xchat_messages(json_root(doc), "111:222", "me", posts, 8) == -1);
  json_free(doc);
}


/*
 * What one message event does to another, and the conversation's own
 * events: plaintexts of each kind (MessageEntryContents' fields) and
 * MessageEvents in the clear (a deletion, the group's changes)
 */
static void w64(writer_t *w, unsigned long long v) { w32(w, (unsigned long)(v >> 32)); w32(w, (unsigned long)v); }

/**
 * a reaction added (field 2) or taken back (3), or an edit (4): its
 * message's sequence number and the emoji, or the new words
 **/
static size_t
holder_to(unsigned char *out, int kind, const char *target, const char *what) {
  writer_t h = {{0}, 0};

  field(&h, 12, 1);
  field(&h, 12, kind);
  wstr(&h, 1, target);
  wstr(&h, 2, what);
  w8(&h, 0);
  w8(&h, 0);
  w8(&h, 0);
  memcpy(out, h.b, h.n);
  return h.n;
}


/**
 * a call ended (field 10: 2 its seconds, 3 sound only) or missed (11: 2
 * sound only)
 **/
static size_t
holder_call(unsigned char *out, int missed, unsigned long seconds, int audio) {
  writer_t h = {{0}, 0};

  field(&h, 12, 1);
  field(&h, 12, missed ? 11 : 10);
  if(!missed) {
    field(&h, 10, 2);
    w64(&h, seconds);
  }
  field(&h, 2, missed ? 2 : 3);
  w8(&h, (unsigned)audio);
  w8(&h, 0);
  w8(&h, 0);
  w8(&h, 0);
  memcpy(out, h.b, h.n);
  return h.n;
}


static size_t
holder_screenshot(unsigned char *out) {
  writer_t h = {{0}, 0};

  field(&h, 12, 1);
  field(&h, 12, 9);
  field(&h, 8, 1);
  w32(&h, 1);
  w8(&h, 0);
  w8(&h, 0);
  w8(&h, 0);
  memcpy(out, h.b, h.n);
  return h.n;
}


/**
 * a message with words that replies to the one numbered seq: what its
 * sender's app saw of it (whose, their name, its words)
 **/
static size_t
holder_reply(unsigned char *out, const char *text, const char *seq, const char *name, const char *said) {
  writer_t h = {{0}, 0};

  field(&h, 12, 1);
  field(&h, 12, 1);
  wstr(&h, 1, text);
  field(&h, 12, 4);             // replying_to_preview
  field(&h, 10, 1);             // sender_id
  w64(&h, 1234567890);
  if(said[0]) {
    wstr(&h, 2, said);
  }
  if(name[0]) {
    wstr(&h, 5, name);
  }
  wstr(&h, 6, seq);
  w8(&h, 0);
  w8(&h, 0);
  w8(&h, 0);
  w8(&h, 0);
  memcpy(out, h.b, h.n);
  return h.n;
}


/**
 * a file: its name (with a line break, which goes), size, url and the
 * SHA-256 of its bytes
 **/
static size_t
holder_file(unsigned char *out, const char *url, const uint8_t sha[32]) {
  writer_t h = {{0}, 0};

  field(&h, 12, 1);
  field(&h, 12, 1);
  field(&h, 15, 3);
  w8(&h, 12);
  w32(&h, 1);
  field(&h, 12, 1);
  field(&h, 8, 3);
  w32(&h, 5);
  field(&h, 10, 5);
  w64(&h, 2500000);
  wstr(&h, 6, "guia\ndo jogo.pdf");
  wstr(&h, 8, url);
  wbytes(&h, 11, sha, 32);
  w8(&h, 0);
  w8(&h, 0);
  w8(&h, 0);
  w8(&h, 0);
  w8(&h, 0);
  memcpy(out, h.b, h.n);
  return h.n;
}


/**
 * a MessageEvent in the clear whose detail (field 7) is a struct of id
 * detail, whose body the caller writes into w between the two calls
 **/
static void
detail_start(writer_t *w, const char *seq, const char *sender, int detail) {
  w->n = 0;
  wstr(w, 1, seq);
  wstr(w, 2, "id-d");
  wstr(w, 3, sender);
  wstr(w, 6, "1794000000000");
  field(w, 12, 7);
  field(w, 12, detail);
}


static void
detail_end(writer_t *w, char *out) {
  w8(w, 0);                   // end the detail's struct
  w8(w, 0);                   // end the detail union
  w8(w, 0);                   // end the MessageEvent
  to_b64(w->b, w->n, out);
}


/**
 * the members ids added (5), removed (7) to the group, in the clear
 **/
static void
members_event(char *out, const char *seq, const char *sender, int change, const char *a, const char *b) {
  writer_t w;

  detail_start(&w, seq, sender, 4);
  field(&w, 12, 1);           // group_change
  field(&w, 12, change);
  field(&w, 15, 1);           // member_ids
  w8(&w, 11);
  w32(&w, b ? 2 : 1);
  w32(&w, strlen(a));
  memcpy(w.b + w.n, a, strlen(a));
  w.n += strlen(a);
  if(b) {
    w32(&w, strlen(b));
    memcpy(w.b + w.n, b, strlen(b));
    w.n += strlen(b);
  }
  w8(&w, 0);                  // end the change
  w8(&w, 0);                  // end the union
  detail_end(&w, out);
}


static void
test_effects(void) {
  static char json[65536], k[2048], ev[24][2048];
  static x_post_t posts[24];
  unsigned char wrapped[160], plain[512], identity[32];
  uint8_t ckey[32], sha[32];
  char wrapped_b64[256];
  const char *pdf = "https://x.example/f.enc";
  json_doc_t *doc;
  writer_t w;
  size_t off, wn, len;
  int n = 0, count;

  wn = unhex(ECIES_WRAPPED, wrapped);
  to_b64(wrapped, wn, wrapped_b64);
  unhex("a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf", ckey);
  key_event(k, wrapped_b64);
  xcrypt_sha256("o guia", 6, sha);

  // Ana (1234567890) and the user (me): two messages, reactions, edits
  msg_event(ev[n++], "100", "1234567890", "id-100", ckey, "ol\xc3\xa1");
  msg_event(ev[n++], "101", "me", "id-101", ckey, "esta vai");
  msg_event_plain(ev[n++], "102", "me", "id-102", ckey, plain, holder_to(plain, 2, "100", "\xf0\x9f\x91\x8d"));
  msg_event_plain(ev[n++], "103", "1234567890", "id-103", ckey, plain, holder_to(plain, 2, "100", "\xe2\x9d\xa4"));
  msg_event_plain(ev[n++], "104", "1234567890", "id-104", ckey, plain, holder_to(plain, 2, "100", "\xf0\x9f\x91\x8d"));
  msg_event_plain(ev[n++], "105", "1234567890", "id-105", ckey, plain, holder_to(plain, 3, "100", "\xe2\x9d\xa4"));
  msg_event_plain(ev[n++], "106", "1234567890", "id-106", ckey, plain, holder_to(plain, 4, "100", "ol\xc3\xa1 a todos"));
  msg_event_plain(ev[n++], "107", "me", "id-107", ckey, plain, holder_to(plain, 4, "100", "not mine to edit"));
  // the user takes 101 back for everyone
  detail_start(&w, "108", "me", 7);
  field(&w, 15, 1);
  w8(&w, 11);
  w32(&w, 1);
  w32(&w, 3);
  memcpy(w.b + w.n, "101", 3);
  w.n += 3;
  field(&w, 8, 2);
  w32(&w, 2);
  detail_end(&w, ev[n++]);
  // replies: one with what Ana's app saw, one with no words in its preview
  msg_event_plain(ev[n++], "109", "me", "id-109", ckey, plain,
                  holder_reply(plain, "sim!", "100", "Ana", "ol\xc3\xa1"));
  msg_event_plain(ev[n++], "110", "me", "id-110", ckey, plain, holder_reply(plain, "e mais", "100", "", ""));
  // calls and a screenshot
  msg_event_plain(ev[n++], "111", "1234567890", "id-111", ckey, plain, holder_call(plain, 0, 185, 1));
  msg_event_plain(ev[n++], "112", "1234567890", "id-112", ckey, plain, holder_call(plain, 1, 0, 0));
  msg_event_plain(ev[n++], "113", "1234567890", "id-113", ckey, plain, holder_screenshot(plain));
  // the group: two added, a title, messages that go after a day, Ana leaves
  members_event(ev[n++], "114", "me", 5, "555", "666");
  detail_start(&w, "115", "me", 4);
  field(&w, 12, 1);
  field(&w, 12, 2);
  wstr(&w, 1, "Squad");
  w8(&w, 0);
  w8(&w, 0);
  detail_end(&w, ev[n++]);
  detail_start(&w, "116", "me", 9);
  field(&w, 12, 1);
  field(&w, 12, 1);
  field(&w, 10, 1);
  w64(&w, 86400000);
  w8(&w, 0);
  w8(&w, 0);
  detail_end(&w, ev[n++]);
  members_event(ev[n++], "117", "1234567890", 7, "1234567890", NULL);
  // the user takes 110 back for themselves: it goes
  detail_start(&w, "118", "me", 7);
  field(&w, 15, 1);
  w8(&w, 11);
  w32(&w, 1);
  w32(&w, 3);
  memcpy(w.b + w.n, "110", 3);
  w.n += 3;
  field(&w, 8, 2);
  w32(&w, 1);
  detail_end(&w, ev[n++]);
  // Ana tries to take the user's 109 back for everyone: not hers
  detail_start(&w, "119", "1234567890", 7);
  field(&w, 15, 1);
  w8(&w, 11);
  w32(&w, 1);
  w32(&w, 3);
  memcpy(w.b + w.n, "109", 3);
  w.n += 3;
  field(&w, 8, 2);
  w32(&w, 2);
  detail_end(&w, ev[n++]);
  // a file, its bytes held to their SHA-256
  msg_event_plain(ev[n++], "120", "1234567890", "id-120", ckey, plain, holder_file(plain, pdf, sha));

  len = (size_t)snprintf(json, sizeof json,
    "{\"data\":{\"get_conversation_page\":{\"__typename\":\"XChatConversationPage\","
    "\"missing_conversation_key_change_events\":[\"%s\"],\"encoded_message_events\":[", k);
  for(int i=n - 1; i >= 0; i--) { // newest first
    len += (size_t)snprintf(json + len, sizeof json - len, "\"%s\"%s", ev[i], i ? "," : "");
  }
  snprintf(json + len, sizeof json - len, "],\"has_more\":false}}}");
  doc = json_parse(json, strlen(json), &off);
  CHECK(doc != NULL);

  xchat_open_install();
  unhex(ECIES_IDENTITY, identity);
  xchat_open_set("1111", identity);

  // 100, 101, 109, 110, the two calls, the screenshot, the four of the
  // group and the file show; 110 goes once the deletions are done
  CHECK(xchat_messages(json_root(doc), "111:222", "me", NULL, 0) == 12);
  count = xchat_messages(json_root(doc), "111:222", "me", posts, 24);
  CHECK(count == 11);
  if(count != 11) {
    json_free(doc);
    return;
  }
  // 100: edited by Ana, a 👍 from each, Ana's ❤ taken back
  CHECK(!strcmp(posts[0].id, "100") && !strcmp(posts[0].text, "ol\xc3\xa1 a todos") && posts[0].edited);
  CHECK(!strcmp(posts[0].reactions, "\xf0\x9f\x91\x8d") && posts[0].reaction_count == 2 && posts[0].reacted);
  // 101: taken back for everyone
  CHECK(!strcmp(posts[1].id, "101") && posts[1].icon == X_DM_DELETED && !posts[1].text[0] && posts[1].mine);
  // 109: a reply, with what Ana's app saw; Ana could not take it back
  CHECK(!strcmp(posts[2].id, "109") && posts[2].icon == X_DM_TEXT && !strcmp(posts[2].text, "sim!"));
  CHECK(!strcmp(posts[2].quote_name, "Ana") && !strcmp(posts[2].quote_text, "ol\xc3\xa1"));
  CHECK(!strcmp(posts[2].quote_user, "1234567890") && !posts[2].quote_avatar[0]);
  // the calls
  CHECK(posts[3].icon == X_DM_CALL && posts[3].event == X_EV_CALL_ENDED && posts[3].event_n == 185 &&
        !posts[3].event_video);
  CHECK(posts[4].icon == X_DM_CALL && posts[4].event == X_EV_CALL_MISSED && posts[4].event_video);
  CHECK(posts[5].icon == X_DM_EVENT && posts[5].event == X_EV_SCREENSHOT && posts[5].event_n == 1);
  // the group's
  CHECK(posts[6].icon == X_DM_EVENT && posts[6].event == X_EV_ADDED && posts[6].event_n == 2 &&
        posts[6].mine && !posts[6].quote_user[0]);
  CHECK(posts[7].event == X_EV_TITLE && !strcmp(posts[7].text, "Squad"));
  CHECK(posts[8].event == X_EV_DISAPPEAR && posts[8].event_n == 86400);
  CHECK(posts[9].event == X_EV_LEFT && !strcmp(posts[9].user_id, "1234567890"));
  // the file: its name without the line break, its size; its bytes checked
  CHECK(!strcmp(posts[10].id, "120") && posts[10].icon == X_DM_FILE);
  CHECK(!strcmp(posts[10].file_name, "guia do jogo.pdf") && posts[10].file_size == 2500000);
  CHECK(xchat_open_check(pdf, "o guia", 6) == 0);
  CHECK(xchat_open_check(pdf, "o guiA", 6) == -1);
  CHECK(xchat_open_check("https://x.example/other", "o guiA", 6) == 0);
  for(int i=0; i < count; i++) {
    CHECK(strcmp(posts[i].id, "110")); // taken back for the user: it went
  }
  json_free(doc);

  // what each one is, on its own
  {
    xchat_entry_t e;
    CHECK(!xchat_entry(plain, holder_to(plain, 3, "100", "\xe2\x9d\xa4"), &e) && e.kind == XCHAT_ENTRY_UNREACT &&
          !strcmp(e.target, "100") && !strcmp(e.emoji, "\xe2\x9d\xa4"));
    CHECK(!xchat_entry(plain, holder_call(plain, 0, 60, 0), &e) && e.kind == XCHAT_ENTRY_CALL_ENDED &&
          e.seconds == 60 && e.video);
    CHECK(xchat_entry(plain, 3, &e) == -1 && e.kind == XCHAT_ENTRY_NONE);
  }
  xchat_open_forget();
  xchat_set_hooks(NULL);
}


/*
 * Signatures: messages signed as X's SDK signs them, checked with the keys
 * X lists for their senders, never the key the event carries
 */
typedef struct signing {
  const char    *sig_version;     // "10", "7", ...; NULL: no signature at all
  const char    *key_version;     // the public_key_version it names
  const uint8_t *priv;            // what signs (32 bytes)
  const char    *carried;         // a signing_public_key the event carries, or NULL
  int            enc;             // contents_encryption_version: 1, or -1 for none
  int            unpadded;        // its base64 without the padding
  int            tamper;          // a byte of the contents changed after signing
} signing_t;


/**
 * a MessageEvent with the plaintext hn bytes, encrypted with the
 * conversation key (or the message's own, with enc 1), signed as sg says
 **/
static void
signed_event(char *out, const char *seq, const char *sender, const char *msgid, const uint8_t ckey[32],
             const unsigned char *plain, size_t hn, const signing_t *sg) {
  unsigned char box[512];
  uint8_t mkey[32], sig[64];
  char bytes[1024], b64[128];
  writer_t w = {{0}, 0};
  xcrypt_signed_t m = {msgid, sender, "111:222", "7", box, hn + 40, sg->enc >= 0, sg->enc, 0};

  xcrypt_message_key(ckey, sg->enc >= 1 ? 1 : 0, sender, msgid, mkey);
  xcrypt_message_seal(mkey, plain, hn, box);
  if(sg->sig_version) {
    m.version = atoi(sg->sig_version);
    m.version = m.version > XCRYPT_SIGNED_MAX ? XCRYPT_SIGNED_MAX : m.version; // a later one: as if
    CHECK(xcrypt_signed_bytes(&m, bytes, sizeof bytes) > 0);
    CHECK(!xcrypt_p256_sign(sg->priv, bytes, strlen(bytes), sig));
    to_b64(sig, 64, b64);
    if(sg->unpadded && strchr(b64, '=')) {
      *strchr(b64, '=') = 0;
    }
  }
  if(sg->tamper) {
    box[hn + 39] ^= 1; // the last byte of the ciphertext: it no longer opens
  }
  wstr(&w, 1, seq);
  wstr(&w, 2, msgid);
  wstr(&w, 3, sender);
  wstr(&w, 4, "111:222");
  wstr(&w, 6, "1794000000000");
  field(&w, 12, 7);           // detail
  field(&w, 12, 1);           // messageCreateEvent
  wbytes(&w, 100, box, hn + 40);
  wstr(&w, 101, "7");         // conversation_key_version
  if(sg->enc >= 0) {
    field(&w, 6, 112);        // contents_encryption_version, an i16
    w16(&w, (unsigned)sg->enc);
  }
  w8(&w, 0);                  // end messageCreateEvent
  w8(&w, 0);                  // end the detail union
  if(sg->sig_version) {
    field(&w, 12, 9);         // message_event_signature
    wstr(&w, 1, b64);
    wstr(&w, 2, sg->key_version);
    wstr(&w, 3, sg->sig_version);
    if(sg->carried) {
      wstr(&w, 4, sg->carried);
    }
    w8(&w, 0);
  }
  w8(&w, 0);                  // end the MessageEvent
  to_b64(w.b, w.n, out);
}


/**
 * the page of a conversation: its key change event apart, then the events
 **/
static json_doc_t *
signed_page(char *json, size_t size, const char *k, char (*ev)[2048], int count) {
  size_t off, n;

  n = (size_t)snprintf(json, size, "{\"data\":{\"get_conversation_page\":{\"__typename\":\"XChatConversationPage\","
                       "\"missing_conversation_key_change_events\":[\"%s\"],\"encoded_message_events\":[", k);
  for(int i=0; i < count; i++) {
    n += (size_t)snprintf(json + n, size - n, "%s\"%s\"", i ? "," : "", ev[i]);
  }
  snprintf(json + n, size - n, "],\"has_more\":false}}}");
  return json_parse(json, strlen(json), &off);
}


static int
wanted_has(char (*users)[24], int n, const char *user) {
  for(int i=0; i < n; i++) {
    if(!strcmp(users[i], user)) {
      return 1;
    }
  }
  return 0;
}


static void
test_signatures(void) {
  static char json[65536], k[2048], ev[10][2048];
  static x_post_t posts[16];
  unsigned char wrapped[160], plain[512], identity[32], spki[91];
  uint8_t ckey[32], alice[32], mallory[32], pub[65];
  char wrapped_b64[256], alice_b64[128], alice_spki[160], mallory_b64[128], users[8][24];
  const char *alice_keys[1], *alice_versions[1] = {"1"};
  json_doc_t *doc;
  size_t wn;
  int count, n;
  const long long now = 1794000000;

  wn = unhex(ECIES_WRAPPED, wrapped);
  to_b64(wrapped, wn, wrapped_b64);
  unhex("a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf", ckey);
  key_event(k, wrapped_b64);
  unhex("11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff", alice);
  unhex("ffeeddccbbaa00998877665544332211ffeeddccbbaa00998877665544332211", mallory);
  CHECK(!xcrypt_p256_public(alice, pub));
  to_b64(pub, 65, alice_b64);
  unhex("3059301306072a8648ce3d020106082a8648ce3d030107034200", spki);
  memcpy(spki + 26, pub, 65);
  to_b64(spki, 91, alice_spki);
  CHECK(!xcrypt_p256_public(mallory, pub));
  to_b64(pub, 65, mallory_b64);

  {
    const signing_t v10 = {"10", "1", alice, NULL, 1, 1, 0};        // as x.com sends now
    const signing_t v7 = {"7", "1", alice, NULL, -1, 0, 0};         // the older way
    const signing_t forged = {"10", "1", mallory, mallory_b64, 1, 0, 0}; // another's key, carried along
    const signing_t unsigned_ = {NULL, NULL, NULL, NULL, 1, 0, 0};
    const signing_t elsewhere = {"10", "9", mallory, NULL, 1, 0, 0}; // a version X does not list
    const signing_t future = {"12", "1", alice, NULL, 1, 0, 0};     // a version this does not know
    signing_t v11 = v10;
    v11.sig_version = "11";
    signed_event(ev[0], "200", "1234567890", "id-s1", ckey, plain, holder(plain, "assinada"), &v10);
    signed_event(ev[1], "202", "1234567890", "id-s2", ckey, plain, holder(plain, "antiga"), &v7);
    signed_event(ev[2], "204", "1234567890", "id-s3", ckey, plain, holder(plain, "forjada"), &forged);
    signed_event(ev[3], "206", "1234567890", "id-s4", ckey, plain, holder(plain, "sem"), &unsigned_);
    signed_event(ev[4], "208", "1234567890", "id-s5", ckey, plain, holder(plain, "outra"), &elsewhere);
    signed_event(ev[5], "210", "777", "id-s6", ckey, plain, holder(plain, "desconhecido"), &v10);
    signed_event(ev[6], "212", "1234567890", "id-s7", ckey, plain, holder(plain, "futura"), &future);
    signed_event(ev[7], "214", "1234567890", "id-s8", ckey, plain, holder(plain, "onze"), &v11);
    // an edit of the first, by its sender but signed with another's key
    signed_event(ev[8], "216", "1234567890", "id-s9", ckey, plain,
                 holder_to(plain, 4, "200", "editada"), &forged);
  }

  xchat_open_install();
  unhex(ECIES_IDENTITY, identity);
  xchat_open_set("1111", identity);

  // counting checks nothing, so nobody's keys are wanted for it
  doc = signed_page(json, sizeof json, k, ev, 9);
  CHECK(doc && xchat_messages(json_root(doc), "111:222", "me", NULL, 0) == 8);
  CHECK(!xchat_open_wanted(now, users, 8));
  json_free(doc);

  // the keys not in hand: none checked, and both senders wanted
  doc = signed_page(json, sizeof json, k, ev, 9);
  count = xchat_messages(json_root(doc), "111:222", "me", posts, 16);
  CHECK(count == 8);
  for(int i=0; i < count; i++) {
    CHECK(posts[i].signature == XCHAT_SIG_NONE && !posts[i].sealed);
  }
  json_free(doc);
  n = xchat_open_wanted(now, users, 8);
  CHECK(n == 2 && wanted_has(users, n, "1234567890") && wanted_has(users, n, "777"));

  // X lists one key for the first sender (its SPKI), none for the other
  alice_keys[0] = alice_spki;
  xchat_open_signer("1234567890", alice_versions, alice_keys, 1, now);
  xchat_open_signer("777", NULL, NULL, 0, now);
  CHECK(!xchat_open_wanted(now, users, 8));
  doc = signed_page(json, sizeof json, k, ev, 9);
  count = xchat_messages(json_root(doc), "111:222", "me", posts, 16);
  CHECK(count == 8);
  CHECK(!strcmp(posts[0].id, "200") && !strcmp(posts[0].text, "editada") && posts[0].edited);
  CHECK(posts[0].signature == XCHAT_SIG_BAD);  // signed, but its edit was forged
  CHECK(!strcmp(posts[1].text, "antiga") && posts[1].signature == XCHAT_SIG_OK);
  CHECK(!strcmp(posts[2].text, "forjada") && posts[2].signature == XCHAT_SIG_BAD);
  CHECK(!strcmp(posts[3].text, "sem") && posts[3].signature == XCHAT_SIG_NONE);
  CHECK(!strcmp(posts[4].text, "outra") && posts[4].signature == XCHAT_SIG_NONE);
  CHECK(!strcmp(posts[5].text, "desconhecido") && posts[5].signature == XCHAT_SIG_NONE);
  CHECK(!strcmp(posts[6].text, "futura") && posts[6].signature == XCHAT_SIG_NONE);
  CHECK(!strcmp(posts[7].text, "onze") && posts[7].signature == XCHAT_SIG_OK);
  json_free(doc);
  // the version not listed, and the sender with none, are asked for again later
  CHECK(!xchat_open_wanted(now + XCHAT_SIGNER_AGAIN - 1, users, 8));
  n = xchat_open_wanted(now + XCHAT_SIGNER_AGAIN, users, 8);
  CHECK(n == 2 && wanted_has(users, n, "1234567890") && wanted_has(users, n, "777"));

  // the same key as a SEC1 point; the first one now signed and not edited
  alice_keys[0] = alice_b64;
  xchat_open_signer("1234567890", alice_versions, alice_keys, 1, now);
  doc = signed_page(json, sizeof json, k, ev, 8);
  count = xchat_messages(json_root(doc), "111:222", "me", posts, 16);
  CHECK(count == 8 && !strcmp(posts[0].text, "assinada") && posts[0].signature == XCHAT_SIG_OK);
  json_free(doc);

  // with the key under another version, a good signature still holds, and
  // a forged one is not called bad (its version is not among the keys)
  alice_versions[0] = "2";
  xchat_open_signer("1234567890", alice_versions, alice_keys, 1, now);
  doc = signed_page(json, sizeof json, k, ev, 8);
  count = xchat_messages(json_root(doc), "111:222", "me", posts, 16);
  CHECK(count == 8 && posts[0].signature == XCHAT_SIG_OK && posts[2].signature == XCHAT_SIG_NONE);
  json_free(doc);
  alice_versions[0] = "1";

  // contents changed on the way do not open, so they are not shown or checked
  {
    const signing_t tampered = {"10", "1", alice, NULL, 1, 0, 1};
    signed_event(ev[0], "200", "1234567890", "id-s1", ckey, plain, holder(plain, "assinada"), &tampered);
    doc = signed_page(json, sizeof json, k, ev, 1);
    count = xchat_messages(json_root(doc), "111:222", "me", posts, 16);
    CHECK(count == 1 && posts[0].sealed && posts[0].signature == XCHAT_SIG_NONE);
    json_free(doc);
  }

  // the hook alone: a sender that is not a number is never wanted
  {
    const xchat_signed_t s = {"id", "12a", NULL, NULL, NULL, 0, 0, 0, "AAAA", "1", "10"};
    CHECK(xchat_open_verify(&s) == XCHAT_SIG_NONE);
    CHECK(xchat_open_verify(NULL) == XCHAT_SIG_NONE);
    n = xchat_open_wanted(now, users, 8);
    CHECK(!wanted_has(users, n, "12a"));
  }

  // without the hooks nothing is checked
  xchat_set_hooks(NULL);
  doc = signed_page(json, sizeof json, k, ev, 1);
  count = xchat_messages(json_root(doc), "111:222", "me", posts, 16);
  CHECK(count == 1 && posts[0].signature == XCHAT_SIG_NONE);
  json_free(doc);
  xchat_open_forget();
}


int
main(void) {
  xcrypt_init(fill_random);
  test_plaintext();
  test_decrypt();
  test_identity();
  test_file();
  test_url();
  test_messages();
  test_effects();
  test_signatures();
  if(g_failed) {
    printf("%d X Chat open checks failed\n", g_failed);
    return 1;
  }
  printf("all X Chat open checks passed\n");
  return 0;
}
