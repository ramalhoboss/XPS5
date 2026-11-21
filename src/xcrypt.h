/* The cryptography of X Chat: the pieces of Juicebox's PIN
recovery (BLAKE2s, Noise NK, the OPRF and its proof, Shamir's shares, the
derivations), a small CBOR reader and writer for its messages, and what
opens a conversation's key and its messages. libsodium (ISC) and BearSSL
(MIT), in third_party/, do the curves and the ciphers. */

#pragma once

#include <stddef.h>
#include <stdint.h>


/**
 * where the random bytes come from (the title's system random bytes, mixed
 * with its clocks); before anything else here
 **/
void xcrypt_init(void (*random)(void *buf, size_t len));
void xcrypt_random(void *buf, size_t len);

/**
 * wipe len bytes, in a way the compiler keeps
 **/
void xcrypt_wipe(void *buf, size_t len);

/**
 * SHA-256 of len bytes
 **/
void xcrypt_sha256(const void *in, size_t len, uint8_t out[32]);

/**
 * whether a and b are the same len bytes, in a time that does not tell
 * where they differ
 **/
int xcrypt_equal(const void *a, const void *b, size_t len);


/*
 * BLAKE2s (RFC 7693): outlen 1 to 32 bytes, a key of 0 to 32 bytes.
 */
typedef struct xcrypt_blake2s {
  uint32_t h[8], t[2];
  uint8_t  b[64];
  size_t   c, outlen;
} xcrypt_blake2s_t;

int xcrypt_blake2s_init(xcrypt_blake2s_t *s, size_t outlen, const void *key, size_t keylen);
void xcrypt_blake2s_update(xcrypt_blake2s_t *s, const void *in, size_t len);
void xcrypt_blake2s_final(xcrypt_blake2s_t *s, void *out);
int xcrypt_blake2s(void *out, size_t outlen, const void *key, size_t keylen, const void *in, size_t len);


/*
 * Noise_NK_25519_ChaChaPoly_BLAKE2s, the client's side, with an empty
 * prologue: the realm's static key is known, the client's is none.
 */
typedef struct xcrypt_noise {
  uint8_t  ck[32], h[32], k[32];
  uint8_t  e[32];               // the ephemeral private key
  uint8_t  send[32], recv[32];  // after the handshake
  uint64_t send_n, recv_n;
} xcrypt_noise_t;

/**
 * -> e, es: from the realm's static key rs and a payload of len bytes,
 * e_pub (32 bytes) and ct (len + 16 bytes) to send; 0 or -1
 **/
int xcrypt_noise_start(xcrypt_noise_t *n, const uint8_t rs[32], const void *payload, size_t len,
                       uint8_t e_pub[32], uint8_t *ct);

/**
 * <- e, ee: the realm's ephemeral key re and its payload ct (len >= 16
 * bytes) into out (len - 16 bytes); then the session's keys; 0 or -1
 **/
int xcrypt_noise_finish(xcrypt_noise_t *n, const uint8_t re[32], const uint8_t *ct, size_t len,
                        uint8_t *out);

/**
 * a message of the session: len bytes sealed into len + 16, or len >= 16
 * opened into len - 16; 0 or -1
 **/
int xcrypt_noise_seal(xcrypt_noise_t *n, const void *in, size_t len, uint8_t *out);
int xcrypt_noise_open(xcrypt_noise_t *n, const uint8_t *in, size_t len, uint8_t *out);


/*
 * Ristretto255 and its scalars (32 bytes each), for the OPRF and the shares.
 */

/**
 * Lagrange's coefficients at 0 of count shares of the 1-based indices
 * index[] (all different), into lambda[count][32]; 0 or -1
 **/
int xcrypt_lagrange(const uint32_t *index, int count, uint8_t (*lambda)[32]);

/**
 * the shares combined: of scalars (into a scalar) or of points (into a
 * point), by Lagrange at 0; 0 or -1
 **/
int xcrypt_combine_scalars(const uint32_t *index, const uint8_t (*share)[32], int count, uint8_t out[32]);
int xcrypt_combine_points(const uint32_t *index, const uint8_t (*share)[32], int count, uint8_t out[32]);

/**
 * a uniformly random scalar, never 0
 **/
void xcrypt_scalar_random(uint8_t out[32]);

/**
 * 64 bytes reduced into a scalar
 **/
void xcrypt_scalar_wide(uint8_t out[32], const uint8_t in[64]);

/**
 * the OPRF's blinded input u = r * H(input), of the blinding factor r;
 * 0 or -1
 **/
int xcrypt_oprf_blind(const void *input, size_t len, const uint8_t r[32], uint8_t u[32]);

/**
 * the realm's proof (c, beta_z) that w = beta * u for its public key
 * v = beta * G (Juicebox_DLEQ_2023_1); 0 when it holds
 **/
int xcrypt_oprf_check(const uint8_t u[32], const uint8_t v[32], const uint8_t w[32],
                      const uint8_t c[32], const uint8_t beta_z[32]);

/**
 * the OPRF's output (64 bytes) of input, from the combined blinded result
 * w and the blinding factor r; 0 or -1
 **/
int xcrypt_oprf_finish(const void *input, size_t len, const uint8_t r[32], const uint8_t w[32],
                       uint8_t out[64]);

/**
 * a realm's OPRF public key, signed with its Ed25519 key: 0 when it is
 **/
int xcrypt_oprf_signed(const uint8_t realm[16], const uint8_t public_key[32],
                       const uint8_t verifying_key[32], const uint8_t signature[64]);


/*
 * Juicebox's derivations (Standard2019).
 */

/**
 * the PIN of len bytes hashed with the registration's version and the
 * user's info: Argon2id, 16 MiB, 32 passes; 0 or -1
 **/
int xcrypt_pin_hash(const void *pin, size_t len, const uint8_t version[16], const void *info,
                    size_t info_len, uint8_t access_key[32], uint8_t seed[32]);

/**
 * the unlock key and its commitment, from the OPRF's output
 **/
void xcrypt_unlock_key(const uint8_t oprf[64], uint8_t commitment[32], uint8_t unlock_key[32]);

/**
 * what a realm is told and answered, keyed by the unlock key
 **/
void xcrypt_unlock_tag(const uint8_t unlock_key[32], const uint8_t realm[16], uint8_t tag[16]);
void xcrypt_secret_commitment(const uint8_t unlock_key[32], const uint8_t realm[16],
                              const uint8_t share[32], const uint8_t secret[145], uint8_t out[16]);

/**
 * the user's secret (at most 128 bytes) out of the 145 a realm keeps, with
 * the seed and the combined scalar; its length, or -1
 **/
int xcrypt_secret_open(const uint8_t seed[32], const uint8_t scalar[32], const uint8_t secret[145],
                       uint8_t out[128]);

/**
 * the same, with the key itself (for the SDK's test)
 **/
int xcrypt_secret_open_key(const uint8_t key[32], const uint8_t secret[145], uint8_t out[128]);


/*
 * X Chat's keys and messages.
 */

/**
 * a conversation key (32 bytes) out of its wrapped copy for the user: the
 * sender's ephemeral P-256 key (65), the key encrypted (32) and its tag
 * (16), opened with the user's identity private key; 0 or -1
 **/
int xcrypt_key_open(const uint8_t identity[32], const uint8_t *wrapped, size_t len, uint8_t key[32]);

/**
 * the key a message is encrypted with: the conversation key itself
 * (version 0), or for version 1 HKDF-SHA256 of it with the sender and the
 * message's ids; 0 or -1
 **/
int xcrypt_message_key(const uint8_t conversation[32], int version, const char *sender,
                       const char *message_id, uint8_t key[32]);

/**
 * a message's contents (nonce 24, tag 16, the text) opened into out
 * (len - 40 bytes); 0 or -1
 **/
int xcrypt_message_open(const uint8_t key[32], const uint8_t *contents, size_t len, uint8_t *out);

/**
 * the key of an attachment: HKDF-SHA256 of the message's key (the per-message
 * one, or the conversation key when there is none) with the attachment's id
 * (its own, or the message id, as X resolves it); 0 or -1
 **/
int xcrypt_attachment_key(const uint8_t key[32], const char *id, uint8_t out[32]);

/**
 * an attachment's bytes, as X Chat stores them: a secretstream of
 * XChaCha20-Poly1305 (a 24-byte header, then 1024-byte chunks each with 17
 * bytes of overhead), opened with key into out (at most max bytes, *len
 * set); 0, or -1 when it does not open or does not fit
 **/
int xcrypt_media_open(const uint8_t key[32], const uint8_t *cipher, size_t clen, uint8_t *out,
                      size_t max, size_t *len);

/**
 * a message sealed the same way into out (len + 40 bytes), with a random
 * nonce; 0 or -1
 **/
int xcrypt_message_seal(const uint8_t key[32], const void *text, size_t len, uint8_t *out);

/**
 * ECDSA P-256 of SHA-256, raw r || s (64 bytes): a check with a public key
 * (65 bytes, 04 x y), 0 when it holds; a signature with a private key, 0
 **/
int xcrypt_p256_verify(const uint8_t public_key[65], const void *msg, size_t len, const uint8_t sig[64]);
int xcrypt_p256_sign(const uint8_t private_key[32], const void *msg, size_t len, uint8_t sig[64]);

/**
 * the public key (65 bytes) of a P-256 private key; 0 or -1
 **/
int xcrypt_p256_public(const uint8_t private_key[32], uint8_t public_key[65]);

/**
 * a P-256 public key as X lists it, a SEC1 point (65 bytes, 04 x y) or its
 * SubjectPublicKeyInfo (91 bytes), into the point; 0, or -1 when it is
 * neither
 **/
int xcrypt_p256_key(const uint8_t *der, size_t len, uint8_t point[65]);

/*
 * What the signature of a MessageCreateEvent covers (X's SDK, chat-xdk,
 * signature versions 2 to 11): "MessageCreateEvent,<message id>,<sender>,
 * <conversation>,<key version>,<contents>", the contents (as the event
 * carries them, encrypted or not) in base64 without padding; from version
 * 10 ",<contents' encryption version>" after them, and from version 9
 * ",signature_version=<version>" at the end. A key version that is not a
 * number, or an encryption version the event does not give, is "null"; a
 * conversation it does not give is left out.
 */
#define XCRYPT_SIGNED_MIN 2
#define XCRYPT_SIGNED_MAX 11

typedef struct xcrypt_signed {
  const char    *message_id, *sender;
  const char    *conversation;   // the event's own (field 4), NULL when it has none
  const char    *key_version;    // the conversation key's (field 101), NULL when none
  const uint8_t *contents;
  size_t         len;
  int            has_encryption; // the event gives its contents' encryption version
  int            encryption;     // (field 112)
  int            version;        // the signature's, XCRYPT_SIGNED_MIN to XCRYPT_SIGNED_MAX
} xcrypt_signed_t;

/**
 * those bytes into buf (size bytes, NUL-terminated when size is not 0): how
 * many there are, as snprintf counts, or -1 when the SDK would make none (a
 * version out of range, an id missing or with a comma)
 **/
long xcrypt_signed_bytes(const xcrypt_signed_t *m, char *buf, size_t size);

/**
 * a signature's version as the event writes it (digits): the number, or 0
 * when it is not one this knows
 **/
int xcrypt_signed_version(const char *text);

/**
 * a message's signature (raw r || s, 64 bytes) checked against those bytes
 * with the signer's public key (a SEC1 point, 65 bytes): 0 when it holds,
 * -1 when it does not or there are no bytes to check
 **/
int xcrypt_signed_verify(const uint8_t public_key[65], const xcrypt_signed_t *m, const uint8_t sig[64]);


/*
 * CBOR, the little Juicebox's messages use: maps, text, byte strings and
 * unsigned numbers, all of a known length.
 */
typedef struct xcrypt_cbor_out {
  uint8_t *buf;
  size_t   len, size;
  int      full;     // something did not fit
} xcrypt_cbor_out_t;

void xcrypt_cbor_map(xcrypt_cbor_out_t *o, size_t count);
void xcrypt_cbor_text(xcrypt_cbor_out_t *o, const char *text);
void xcrypt_cbor_bytes(xcrypt_cbor_out_t *o, const void *bytes, size_t len);
void xcrypt_cbor_uint(xcrypt_cbor_out_t *o, uint64_t value);

typedef struct xcrypt_cbor {
  const uint8_t *p;  // where the item starts
  size_t         len; // bytes from there to the end of the message
} xcrypt_cbor_t;

/**
 * the item's whole size (with what it holds), or 0 when it is not whole or
 * not of the kinds above
 **/
size_t xcrypt_cbor_size(xcrypt_cbor_t item);

/**
 * a map's member named key; 0 or -1
 **/
int xcrypt_cbor_get(xcrypt_cbor_t map, const char *key, xcrypt_cbor_t *value);

/**
 * an enum of Rust's serde: a text for a variant without data, or a map of
 * one member, the variant's name and its data; whether it is variant name
 * (with *data set when there is some)
 **/
int xcrypt_cbor_is(xcrypt_cbor_t item, const char *name, xcrypt_cbor_t *data);

/**
 * a byte string of exactly len bytes (or any, with len 0: *got set); a
 * number; 0 or -1
 **/
int xcrypt_cbor_get_bytes(xcrypt_cbor_t map, const char *key, const uint8_t **bytes, size_t len,
                          size_t *got);
int xcrypt_cbor_get_uint(xcrypt_cbor_t map, const char *key, uint64_t *value);
