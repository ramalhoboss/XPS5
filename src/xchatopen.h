/* The words of X Chat's messages: the
user's identity key, recovered from Juicebox with their PIN (juicebox.h),
opens each conversation's keys from its key change events, and they open
the messages. xchat.c reads the events and asks here through its hooks
(xchat_set_hooks), which xchat_open_install() sets; the payload never sets
them, so it stays without the cryptography. Thread safe. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "xchat.h" // xchat_attach_t, for the hook that opens an attachment

#define XCHAT_KEY_FILE_MAX 160 // the path of a user's key file

/**
 * the identity key (32 bytes) of the user whose number is me, from now on;
 * the conversations' keys of another user are forgotten
 **/
void xchat_open_set(const char *me, const uint8_t identity[32]);

/**
 * no identity any more, and no conversation key
 **/
void xchat_open_forget(void);

/**
 * whether an identity is set, for the user me (or any, with me NULL)
 **/
int xchat_open_ready(const char *me);

/**
 * the hooks of xchat.c set to these
 **/
void xchat_open_install(void);

/**
 * what the identity check saw, for the message when it fails: sizes and
 * counts, nothing of the keys
 **/
typedef struct {
  int secret;  // bytes of Juicebox's secret
  int tried;   // private keys tried out of it
  int keys;    // public keys X listed
  int points;  // of them, read as P-256 points
  int signing; // only a signing key of the secret was among them
} xchat_identity_t;

#define XCHAT_SECRET_MAX 128 // the longest secret Juicebox keeps

/**
 * the user's identity from Juicebox's secret (X's SDK keeps identity ||
 * signing there, 32 bytes each; any 32 bytes of it, or of the base64 or hex
 * in it, are tried), checked against the public keys X lists for them
 * (base64 or hex of a SEC1 point, its SPKI, a compressed point or x y);
 * signing[i] says public_keys[i] is a signing key (NULL: none is). 1 when
 * the identity is found (or only the signing half of a 64-byte secret is
 * listed: the identity is the other half), 0 when none is, -1 when nothing
 * in the secret is a private key. seen, when given, says what was tried
 **/
int xchat_open_identity(const uint8_t *secret, int len, const char *const *public_keys, const int *signing, int count,
                        uint8_t identity[32], xchat_identity_t *seen);

/**
 * the key file of the user me; 0, or -1
 **/
int xchat_open_path(const char *dir, const char *me, char *buf, size_t size);

/**
 * the identity kept in path, or written there (the file holds nothing
 * else); 0, or -1
 **/
int xchat_open_load(const char *path, uint8_t identity[32]);
int xchat_open_save(const char *path, const uint8_t identity[32]);

/**
 * how many conversation keys are kept, for the log
 **/
int xchat_open_keys(void);

/**
 * the url and key of the encrypted video, GIF or voice note named token
 * (xchat_media_token, a post's video_url after "file://"), as the message
 * opened last gave them: into url (size bytes) and key, 0; -1 when none is
 * kept by that name
 **/
int xchat_open_media(const char *token, char *url, size_t size, uint8_t key[32]);

/**
 * the bytes of the encrypted attachment at url, once opened, held to the
 * SHA-256 its message gave: -1 when they hash to something else (they were
 * changed on the way), 0 when they match or no hash is known for url
 **/
int xchat_open_check(const char *url, const void *bytes, size_t len);

/*
 * The senders' signing keys, which check their messages' signatures: only
 * as X lists them (GetPublicKeys), never the key an event carries. A
 * message whose sender's keys are not in hand makes them wanted; the title
 * asks X for them (xchat_open_wanted), gives them here (xchat_open_signer)
 * and reads the page again. They are public, so they are kept whoever the
 * user is.
 */
#define XCHAT_SIGNER_KEYS  4   // of each sender, at most
#define XCHAT_SIGNER_AGAIN 600 // seconds before a sender's keys are asked for again,
                               // when a message names a version not among them

/**
 * the signing keys X lists for the sender user (their number), at now (Unix
 * time): count of them, each base64 of a SEC1 point (65 bytes) or its SPKI
 * (91), with its public_key_version (versions NULL, or one NULL: not said);
 * none (count 0) when X lists none or did not answer
 **/
void xchat_open_signer(const char *user, const char *const *versions, const char *const *keys, int count,
                       long long now);

/**
 * the senders whose keys a message wanted since (not listed yet, or due to
 * be asked again at now), into users (at most max); how many
 **/
int xchat_open_wanted(long long now, char (*users)[24], int max);

/**
 * the verify hook: a message's signature checked with the keys X lists for
 * its sender, the one of the version it names first; XCHAT_SIG_OK when one
 * signed it, XCHAT_SIG_BAD when the key of that version did not, else
 * XCHAT_SIG_NONE (and the sender's keys are wanted)
 **/
int xchat_open_verify(const xchat_signed_t *signed_event);

/*
 * What the hooks do, for the tests: a key change event's key of the user,
 * kept when it opens; a message's contents opened into an entry
 * (xchat_entry), or into text, the kind of its media and its attachment.
 */
void xchat_open_key(const char *conversation, const char *version, const char *user, const char *wrapped);
int xchat_open_entry(const char *conversation, const char *version, int encryption, const char *sender,
                     const char *message_id, const unsigned char *contents, size_t len, xchat_entry_t *entry);
int xchat_open_text(const char *conversation, const char *version, int encryption, const char *sender,
                    const char *message_id, const unsigned char *contents, size_t len, char *text,
                    size_t size, int *media, xchat_attach_t *att);
