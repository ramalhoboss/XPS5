/* X Chat, the chat that took the place of X's direct messages (November
2025): its inbox as x.com's Chat reads it (GetInitialXChatPageQuery on
api.x.com), for who wrote and when. The words of a conversation started
there are end-to-end encrypted and stay so here: only what X keeps in the
clear is read (who sent a message, when, in which conversation, and how far
each one read), and the words of a message X sent unencrypted. Each event
comes as base64 of Thrift's binary protocol. Apart from the PS5, so the
host tests cover it. */

#pragma once

#include <stddef.h>

#include "json.h"
#include "xapi.h"

#define XCHAT_HEADER_COUNT (X_HEADER_COUNT + 3)

/*
 * A media attachment of a message: where its bytes are and, when they are
 * encrypted, the key that opens them (xcrypt_media_open). X Chat keeps the
 * bytes as a secretstream, apart from the message.
 */
typedef struct xchat_attach {
  int     kind;        // the MediaType: 1 a photo, 2 a GIF, 3 a video, 4 a voice note, ...; 0 none
  char    url[256];    // where the bytes are (legacy_media_url_https)
  int     width, height;
  int     enc;         // the bytes are encrypted (a secretstream), to open with key
  uint8_t key[32];
  char    att_id[64];  // the attachment's own id, for the key (empty: the message's)
  int     duration_ms; // of a video or voice note, 0 when unknown
  char    filename[128]; // of a file: its name and size (0 when unknown)
  long long filesize;
  int     has_sha256;  // the SHA-256 its bytes have once opened, when the message says it
  uint8_t sha256[32];
} xchat_attach_t;

/*
 * What a message event's plaintext is (a MessageEntryHolder): a message,
 * a reaction to one or one taken back, an edit of one, a call, or a
 * screenshot taken of the conversation.
 */
enum { XCHAT_ENTRY_NONE, XCHAT_ENTRY_MESSAGE, XCHAT_ENTRY_REACT, XCHAT_ENTRY_UNREACT, XCHAT_ENTRY_EDIT,
       XCHAT_ENTRY_CALL_STARTED, XCHAT_ENTRY_CALL_ENDED, XCHAT_ENTRY_CALL_MISSED, XCHAT_ENTRY_SCREENSHOT };

typedef struct xchat_entry {
  int            kind;            // XCHAT_ENTRY_*
  char           text[4096];      // a message's words, or an edit's new ones
  char           target[32];      // a reaction's or an edit's message: its sequence number
  char           emoji[32];       // a reaction's
  char           reply_name[128]; // a message's reply: whom it answers (their name, as the
  char           reply_text[320]; // sender's app saw it), what that one said, and its
  char           reply_seq[32];   // sequence number
  char           reply_user[24];  // that one's sender, their number
  int            reply_media;     // the kind of what that one carried, when it had no words
  long long      seconds;         // a call's
  int            video;           // a call with video, not only sound
  int            shot;            // a screenshot (1) or a recording of the screen (2)
  int            media_kind;      // the kind of a message's first attachment (xchat_media_kind)
  xchat_attach_t att;             // and that attachment
} xchat_entry_t;

/**
 * the plaintext of a message event (a MessageEntryHolder of Thrift) into e;
 * 0, or -1 when it is not one (e->kind XCHAT_ENTRY_NONE)
 **/
int xchat_entry(const unsigned char *p, size_t len, xchat_entry_t *e);

/**
 * the first media attachment of a message's plaintext into att (kind, url,
 * dimensions and its own id); 1 when it has one, 0 when it has none. It does
 * not fill the key: the caller derives it (the id it resolves)
 **/
int xchat_attachment(const unsigned char *p, size_t len, xchat_attach_t *att);

/**
 * the name of an encrypted attachment for the player ("xchat/" and 16 hex
 * digits of its url, never its key): a video's or voice note's video_url is
 * "file://" and it, and the title's reader of such files finds the url and
 * key it stands for (xchat_open_media); XCHAT_TOKEN bytes at most
 **/
#define XCHAT_TOKEN 24
void xchat_media_token(const char *url, char *buf, size_t size);


/*
 * A message's signature: whether it held, checked
 * with its sender's signing key as X lists it (GetPublicKeys), never the
 * key the event carries. NONE when it could not be checked (no signature,
 * the key not known yet, a version this does not know): the message shows
 * as it always did.
 */
enum { XCHAT_SIG_NONE, XCHAT_SIG_OK, XCHAT_SIG_BAD };

/*
 * What a MessageCreateEvent's signature is checked with: the event's parts
 * it covers, and the signature itself as the event writes it
 * (MessageEventSignature, MessageEvent's field 9).
 */
typedef struct xchat_signed {
  const char          *message_id, *sender;
  const char          *conversation;   // the event's own (field 4), NULL when it has none
  const char          *key_version;    // the conversation key's (field 101), NULL when none
  const unsigned char *contents;       // as they came, encrypted or not
  size_t               len;
  int                  has_encryption; // the contents' encryption version (field 112) is given
  int                  encryption;
  const char          *signature;      // base64 of r || s (64 bytes)
  const char          *public_key_version; // which of the sender's signing keys
  const char          *signature_version;
} xchat_signed_t;

/*
 * What opens the words of X Chat's encrypted messages, when the app has the
 * user's keys (xchatopen.h), and checks their signatures. xchat.c asks
 * through them while it reads events.
 */
typedef struct xchat_hooks {
  /**
   * a conversation's key for the user, from a key change event: the user
   * whose wrapped copy this is and the copy (base64), of the version; the
   * hook keeps it when it opens with the user's identity key
   **/
  void (*key)(const char *conversation, const char *version, const char *user, const char *wrapped);
  /**
   * a message event's plaintext, when the hook has the conversation's key
   * of that version: the message's encryption version, who sent it and its
   * id (for the per-message key), and its contents (nonce, tag, the
   * ciphertext) opened into entry (xchat_entry), its attachment's key
   * filled, ready for xcrypt_media_open; 1 when it opened, 0 when it could
   * not
   **/
  int (*entry)(const char *conversation, const char *version, int encryption, const char *sender,
               const char *message_id, const unsigned char *contents, size_t len, xchat_entry_t *entry);
  /**
   * a message event's signature checked with its sender's signing key, as X
   * lists it: XCHAT_SIG_*. Asked only for the conversation's page
   * (xchat_messages), of the messages whose words show
   **/
  int (*verify)(const xchat_signed_t *signed_event);
} xchat_hooks_t;

/**
 * the hooks xchat.c asks through from now on (none with NULL)
 **/
void xchat_set_hooks(const xchat_hooks_t *hooks);

/**
 * the words of a message's plaintext (a MessageEntryHolder of Thrift, whose
 * contents' message's text they are), into buf (size bytes), empty when
 * there are none; the same bytes whether they came encrypted or not
 **/
void xchat_plaintext(const unsigned char *p, size_t len, char *buf, size_t size);

/**
 * the kind of the first media a message's plaintext carries: 1 a photo, 2 a
 * GIF, 3 a video, 4 a voice note, 5 a file, 6 an SVG; 0 when it has none
 **/
int xchat_media_kind(const unsigned char *p, size_t len);

/**
 * URL of the inbox's first page, as x.com's Chat asks for it; 0, or -1
 * when it does not fit
 **/
int xchat_inbox_url(char *buf, size_t size);

/**
 * URL of a conversation's page (GetConversationPageQuery): its newest
 * events (at most conversation_event_limit of them) and every key change,
 * or those before the sequence number before (its digits; NULL or "0" for
 * the newest); 0, or -1 when it does not fit
 **/
int xchat_conversation_url(char *buf, size_t size, const char *conversation, const char *before);

/**
 * the headers of a request of X Chat's to api.x.com: r's (x_request_init),
 * the origin x.com, the client's uuid and, when tid is not NULL, the
 * transaction id; returns how many are in headers (XCHAT_HEADER_COUNT at
 * most), which point into r, uuid and tid
 **/
int xchat_headers(const x_request_t *r, const char *uuid, const char *tid, https_header_t *headers);

/**
 * a version 4 uuid, 36 characters, made of 16 random bytes
 **/
void xchat_uuid(const unsigned char bytes[16], char buf[37]);

/**
 * the conversations of an inbox answer as posts of Chat (X_KIND_CHAT, with
 * xchat set), newest first: who it is with, when its last message came and
 * whether the user (number me) sent it (mine) or has not read it (unread);
 * its words only when X sent them unencrypted (else sealed is set). With
 * posts NULL, how many there are. Returns how many, or -1 when the answer
 * is not an inbox (X's errors)
 **/
int xchat_inbox(const json_node_t *root, const char *me, x_post_t *posts, int max);

/**
 * X Chat's conversations (count of them, after the legacy inbox's legacy
 * ones in all) put with the inbox's: one the inbox has too (the same number,
 * or the same one person) is left out, and when its newest message came on
 * X Chat later, encrypted, the inbox's says so (sealed, its time, unread),
 * with the X Chat conversation's number kept in reposter so its page can be
 * read for it; returns how many are in all now
 **/
int xchat_merge(x_post_t *all, int legacy, int count);

/**
 * the messages of a conversation's page answer into posts (X_KIND_DM, with
 * xchat set), oldest first, me being the signed-in user's number: who sent
 * each, when, its words when the hooks opened them (else sealed), and in
 * signature whether its signature held (XCHAT_SIG_*; an edited one's is the
 * worse of its own and its edit's); its key change events are read first,
 * so later calls open more. With posts NULL, how many there are (nothing
 * checked); returns how many, or -1 when the answer is not a page
 **/
int xchat_messages(const json_node_t *root, const char *conversation, const char *me, x_post_t *posts, int max);
