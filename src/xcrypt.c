/* The cryptography of X Chat; see xcrypt.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bearssl.h>
#include <sodium.h>

#include "xcrypt.h"

// libsodium's own Argon2 (its crypto_pwhash takes only 16-byte salts, and
// Juicebox's are longer): crypto_pwhash/argon2/argon2.h
int argon2id_hash_raw(const uint32_t t_cost, const uint32_t m_cost, const uint32_t parallelism,
                      const void *pwd, const size_t pwdlen, const void *salt, const size_t saltlen,
                      void *hash, const size_t hashlen);

static void (*g_random)(void *buf, size_t len);


void
xcrypt_init(void (*random)(void *buf, size_t len)) {
  g_random = random;
}


void
xcrypt_random(void *buf, size_t len) {
  if(!g_random) {
    abort(); // a key from no randomness is worse than none
  }
  g_random(buf, len);
}


void
xcrypt_wipe(void *buf, size_t len) {
  sodium_memzero(buf, len);
}


void
xcrypt_sha256(const void *in, size_t len, uint8_t out[32]) {
  br_sha256_context sha;

  br_sha256_init(&sha);
  br_sha256_update(&sha, in, len);
  br_sha256_out(&sha, out);
}


int
xcrypt_equal(const void *a, const void *b, size_t len) {
  return sodium_memcmp(a, b, len) == 0;
}


/*
 * What libsodium asks of the program around it, as it is built here
 * without its core.c (tools/build-crypto.sh).
 */

void
randombytes_buf(void *buf, const size_t len) {
  xcrypt_random(buf, len);
}


void
sodium_misuse(void) {
  abort();
}


/*
 * BLAKE2s, RFC 7693.
 */

static const uint32_t g_blake2s_iv[8] = {
  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

static const uint8_t g_blake2s_sigma[10][16] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
  {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
  {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
  { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
  { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
  { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
  {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
  {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
  { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
  {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
};


static uint32_t
rotr32(uint32_t x, int n) {
  return x >> n | x << (32 - n);
}


static uint32_t
le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}


#define G(a, b, c, d, x, y) do { \
    v[a] = v[a] + v[b] + (x); v[d] = rotr32(v[d] ^ v[a], 16); \
    v[c] = v[c] + v[d];       v[b] = rotr32(v[b] ^ v[c], 12); \
    v[a] = v[a] + v[b] + (y); v[d] = rotr32(v[d] ^ v[a], 8);  \
    v[c] = v[c] + v[d];       v[b] = rotr32(v[b] ^ v[c], 7);  \
  } while(0)

static void
blake2s_compress(xcrypt_blake2s_t *s, int last) {
  uint32_t v[16], m[16];

  for(int i=0; i<8; i++) {
    v[i] = s->h[i];
    v[i + 8] = g_blake2s_iv[i];
  }
  v[12] ^= s->t[0];
  v[13] ^= s->t[1];
  if(last) {
    v[14] = ~v[14];
  }
  for(int i=0; i<16; i++) {
    m[i] = le32(s->b + 4 * i);
  }
  for(int r=0; r<10; r++) {
    const uint8_t *z = g_blake2s_sigma[r];
    G(0, 4, 8, 12, m[z[0]], m[z[1]]);
    G(1, 5, 9, 13, m[z[2]], m[z[3]]);
    G(2, 6, 10, 14, m[z[4]], m[z[5]]);
    G(3, 7, 11, 15, m[z[6]], m[z[7]]);
    G(0, 5, 10, 15, m[z[8]], m[z[9]]);
    G(1, 6, 11, 12, m[z[10]], m[z[11]]);
    G(2, 7, 8, 13, m[z[12]], m[z[13]]);
    G(3, 4, 9, 14, m[z[14]], m[z[15]]);
  }
  for(int i=0; i<8; i++) {
    s->h[i] ^= v[i] ^ v[i + 8];
  }
  xcrypt_wipe(v, sizeof v);
  xcrypt_wipe(m, sizeof m);
}

#undef G


int
xcrypt_blake2s_init(xcrypt_blake2s_t *s, size_t outlen, const void *key, size_t keylen) {
  if(!outlen || outlen > 32 || keylen > 32) {
    return -1;
  }
  memset(s, 0, sizeof *s);
  memcpy(s->h, g_blake2s_iv, sizeof s->h);
  s->h[0] ^= 0x01010000u ^ (uint32_t)keylen << 8 ^ (uint32_t)outlen;
  s->outlen = outlen;
  if(keylen) {
    // the key, as a whole block of its own
    xcrypt_blake2s_update(s, key, keylen);
    s->c = 64;
  }
  return 0;
}


void
xcrypt_blake2s_update(xcrypt_blake2s_t *s, const void *in, size_t len) {
  const uint8_t *p = in;

  for(size_t i=0; i<len; i++) {
    if(s->c == 64) {
      // a block is compressed once more comes after it: the last is final's
      s->t[0] += 64;
      s->t[1] += s->t[0] < 64;
      blake2s_compress(s, 0);
      s->c = 0;
    }
    s->b[s->c++] = p[i];
  }
}


void
xcrypt_blake2s_final(xcrypt_blake2s_t *s, void *out) {
  uint8_t bytes[32];

  s->t[0] += (uint32_t)s->c;
  s->t[1] += s->t[0] < s->c;
  memset(s->b + s->c, 0, 64 - s->c);
  blake2s_compress(s, 1);
  for(int i=0; i<8; i++) {
    bytes[4 * i] = (uint8_t)s->h[i];
    bytes[4 * i + 1] = (uint8_t)(s->h[i] >> 8);
    bytes[4 * i + 2] = (uint8_t)(s->h[i] >> 16);
    bytes[4 * i + 3] = (uint8_t)(s->h[i] >> 24);
  }
  memcpy(out, bytes, s->outlen);
  xcrypt_wipe(bytes, sizeof bytes);
  xcrypt_wipe(s, sizeof *s);
}


int
xcrypt_blake2s(void *out, size_t outlen, const void *key, size_t keylen, const void *in, size_t len) {
  xcrypt_blake2s_t s;

  if(xcrypt_blake2s_init(&s, outlen, key, keylen)) {
    return -1;
  }
  xcrypt_blake2s_update(&s, in, len);
  xcrypt_blake2s_final(&s, out);
  return 0;
}


/*
 * Noise (noiseprotocol.org, revision 34): HASH BLAKE2s, its HMAC and HKDF,
 * DH X25519, CIPHER ChaCha20-Poly1305 with the counter after four zero
 * bytes, little-endian.
 */

static void
hmac_blake2s(const uint8_t key[32], const void *a, size_t alen, const void *b, size_t blen,
             uint8_t out[32]) {
  uint8_t pad[64], inner[32];
  xcrypt_blake2s_t s;

  memset(pad, 0x36, sizeof pad);
  for(int i=0; i<32; i++) {
    pad[i] ^= key[i];
  }
  xcrypt_blake2s_init(&s, 32, NULL, 0);
  xcrypt_blake2s_update(&s, pad, sizeof pad);
  xcrypt_blake2s_update(&s, a, alen);
  xcrypt_blake2s_update(&s, b, blen);
  xcrypt_blake2s_final(&s, inner);
  memset(pad, 0x5c, sizeof pad);
  for(int i=0; i<32; i++) {
    pad[i] ^= key[i];
  }
  xcrypt_blake2s_init(&s, 32, NULL, 0);
  xcrypt_blake2s_update(&s, pad, sizeof pad);
  xcrypt_blake2s_update(&s, inner, sizeof inner);
  xcrypt_blake2s_final(&s, out);
  xcrypt_wipe(pad, sizeof pad);
  xcrypt_wipe(inner, sizeof inner);
}


/**
 * HKDF(chaining_key, input) into two outputs of 32 bytes
 **/
static void
noise_hkdf(const uint8_t ck[32], const void *input, size_t len, uint8_t out1[32], uint8_t out2[32]) {
  static const uint8_t one = 1, two = 2;
  uint8_t temp[32], first[32];

  hmac_blake2s(ck, input, len, NULL, 0, temp);
  hmac_blake2s(temp, &one, 1, NULL, 0, first);
  hmac_blake2s(temp, first, 32, &two, 1, out2);
  memcpy(out1, first, 32);
  xcrypt_wipe(temp, sizeof temp);
  xcrypt_wipe(first, sizeof first);
}


static void
noise_mix_hash(xcrypt_noise_t *n, const void *data, size_t len) {
  xcrypt_blake2s_t s;

  xcrypt_blake2s_init(&s, 32, NULL, 0);
  xcrypt_blake2s_update(&s, n->h, 32);
  xcrypt_blake2s_update(&s, data, len);
  xcrypt_blake2s_final(&s, n->h);
}


/**
 * MixKey of a DH: the private key and the other side's public one; 0 or -1
 **/
static int
noise_mix_dh(xcrypt_noise_t *n, const uint8_t priv[32], const uint8_t pub[32]) {
  uint8_t shared[32];
  int ret = crypto_scalarmult_curve25519(shared, priv, pub);

  noise_hkdf(n->ck, shared, sizeof shared, n->ck, n->k);
  xcrypt_wipe(shared, sizeof shared);
  return ret ? -1 : 0;
}


static void
noise_nonce(uint64_t counter, uint8_t nonce[12]) {
  memset(nonce, 0, 12);
  for(int i=0; i<8; i++) {
    nonce[4 + i] = (uint8_t)(counter >> 8 * i);
  }
}


static int
noise_encrypt(const uint8_t key[32], uint64_t counter, const uint8_t *ad, size_t adlen,
              const void *in, size_t len, uint8_t *out) {
  uint8_t nonce[12];

  noise_nonce(counter, nonce);
  return crypto_aead_chacha20poly1305_ietf_encrypt(out, NULL, in, len, ad, adlen, NULL, nonce, key) ? -1 : 0;
}


static int
noise_decrypt(const uint8_t key[32], uint64_t counter, const uint8_t *ad, size_t adlen,
              const uint8_t *in, size_t len, uint8_t *out) {
  uint8_t nonce[12];

  if(len < 16) {
    return -1;
  }
  noise_nonce(counter, nonce);
  return crypto_aead_chacha20poly1305_ietf_decrypt(out, NULL, NULL, in, len, ad, adlen, nonce, key) ? -1 : 0;
}


/**
 * the handshake state of the pattern, before its first message
 **/
static void
noise_begin(xcrypt_noise_t *n, const uint8_t rs[32]) {
  static const char name[] = "Noise_NK_25519_ChaChaPoly_BLAKE2s"; // longer than HASHLEN: hashed

  memset(n, 0, sizeof *n);
  xcrypt_blake2s(n->h, 32, NULL, 0, name, sizeof name - 1);
  memcpy(n->ck, n->h, 32);
  noise_mix_hash(n, "", 0); // the prologue, empty
  noise_mix_hash(n, rs, 32); // <- s, known before
}


int
xcrypt_noise_start(xcrypt_noise_t *n, const uint8_t rs[32], const void *payload, size_t len,
                   uint8_t e_pub[32], uint8_t *ct) {
  noise_begin(n, rs);
  xcrypt_random(n->e, sizeof n->e);
  if(crypto_scalarmult_curve25519_base(e_pub, n->e)) {
    return -1;
  }
  noise_mix_hash(n, e_pub, 32);
  if(noise_mix_dh(n, n->e, rs) || noise_encrypt(n->k, 0, n->h, 32, payload, len, ct)) {
    xcrypt_wipe(n, sizeof *n);
    return -1;
  }
  noise_mix_hash(n, ct, len + 16);
  return 0;
}


int
xcrypt_noise_finish(xcrypt_noise_t *n, const uint8_t re[32], const uint8_t *ct, size_t len,
                    uint8_t *out) {
  int ret;

  noise_mix_hash(n, re, 32);
  ret = noise_mix_dh(n, n->e, re) || noise_decrypt(n->k, 0, n->h, 32, ct, len, out) ? -1 : 0;
  if(!ret) {
    noise_mix_hash(n, ct, len);
    noise_hkdf(n->ck, NULL, 0, n->send, n->recv); // Split(): the initiator sends with the first
    n->send_n = n->recv_n = 0;
  }
  xcrypt_wipe(n->ck, sizeof n->ck);
  xcrypt_wipe(n->k, sizeof n->k);
  xcrypt_wipe(n->e, sizeof n->e);
  return ret;
}


int
xcrypt_noise_seal(xcrypt_noise_t *n, const void *in, size_t len, uint8_t *out) {
  if(n->send_n == UINT64_MAX || noise_encrypt(n->send, n->send_n, NULL, 0, in, len, out)) {
    return -1;
  }
  n->send_n++;
  return 0;
}


int
xcrypt_noise_open(xcrypt_noise_t *n, const uint8_t *in, size_t len, uint8_t *out) {
  if(n->recv_n == UINT64_MAX || noise_decrypt(n->recv, n->recv_n, NULL, 0, in, len, out)) {
    return -1;
  }
  n->recv_n++;
  return 0;
}


/*
 * Ristretto255, its scalars, and Shamir's shares of them.
 */

static void
scalar_of(uint32_t value, uint8_t out[32]) {
  memset(out, 0, 32);
  for(int i=0; i<4; i++) {
    out[i] = (uint8_t)(value >> 8 * i);
  }
}


int
xcrypt_lagrange(const uint32_t *index, int count, uint8_t (*lambda)[32]) {
  uint8_t num[32], den[32], xi[32], xj[32], diff[32];

  for(int i=0; i<count; i++) {
    // prod x_j / prod (x_j - x_i), over j != i
    scalar_of(1, num);
    scalar_of(1, den);
    scalar_of(index[i], xi);
    for(int j=0; j<count; j++) {
      if(j == i) {
        continue;
      }
      if(index[j] == index[i]) {
        return -1;
      }
      scalar_of(index[j], xj);
      crypto_core_ristretto255_scalar_mul(num, num, xj);
      crypto_core_ristretto255_scalar_sub(diff, xj, xi);
      crypto_core_ristretto255_scalar_mul(den, den, diff);
    }
    if(crypto_core_ristretto255_scalar_invert(den, den)) {
      return -1;
    }
    crypto_core_ristretto255_scalar_mul(lambda[i], num, den);
  }
  return 0;
}


#define SHARES_MAX 16

int
xcrypt_combine_scalars(const uint32_t *index, const uint8_t (*share)[32], int count, uint8_t out[32]) {
  uint8_t lambda[SHARES_MAX][32], term[32];

  if(count < 1 || count > SHARES_MAX || xcrypt_lagrange(index, count, lambda)) {
    return -1;
  }
  memset(out, 0, 32);
  for(int i=0; i<count; i++) {
    crypto_core_ristretto255_scalar_mul(term, lambda[i], share[i]);
    crypto_core_ristretto255_scalar_add(out, out, term);
  }
  xcrypt_wipe(term, sizeof term);
  xcrypt_wipe(lambda, sizeof lambda);
  return 0;
}


int
xcrypt_combine_points(const uint32_t *index, const uint8_t (*share)[32], int count, uint8_t out[32]) {
  uint8_t lambda[SHARES_MAX][32], term[32];
  int ret = 0;

  if(count < 1 || count > SHARES_MAX || xcrypt_lagrange(index, count, lambda)) {
    return -1;
  }
  for(int i=0; i<count && !ret; i++) {
    ret = crypto_scalarmult_ristretto255(term, lambda[i], share[i]) ||
          (i ? crypto_core_ristretto255_add(out, out, term) : (memcpy(out, term, 32), 0)) ? -1 : 0;
  }
  xcrypt_wipe(lambda, sizeof lambda);
  return ret;
}


void
xcrypt_scalar_wide(uint8_t out[32], const uint8_t in[64]) {
  uint8_t wide[64];

  memcpy(wide, in, sizeof wide);
  crypto_core_ristretto255_scalar_reduce(out, wide);
  xcrypt_wipe(wide, sizeof wide);
}


void
xcrypt_scalar_random(uint8_t out[32]) {
  uint8_t wide[64];

  do {
    xcrypt_random(wide, sizeof wide);
    xcrypt_scalar_wide(out, wide);
  } while(sodium_is_zero(out, 32));
  xcrypt_wipe(wide, sizeof wide);
}


/**
 * H(input): SHA-512 of it, mapped to the group
 **/
static int
oprf_hash(const void *input, size_t len, uint8_t point[32]) {
  uint8_t digest[64];
  int ret;

  crypto_hash_sha512(digest, input, len);
  ret = crypto_core_ristretto255_from_hash(point, digest);
  xcrypt_wipe(digest, sizeof digest);
  return ret ? -1 : 0;
}


int
xcrypt_oprf_blind(const void *input, size_t len, const uint8_t r[32], uint8_t u[32]) {
  uint8_t h[32];
  int ret = oprf_hash(input, len, h) || crypto_scalarmult_ristretto255(u, r, h) ? -1 : 0;

  xcrypt_wipe(h, sizeof h);
  return ret;
}


/**
 * a * p - b * q (beta_z * point - c * other)
 **/
static int
mul_sub(const uint8_t a[32], const uint8_t *p, const uint8_t b[32], const uint8_t q[32], uint8_t out[32]) {
  uint8_t ap[32], bq[32];

  if((p ? crypto_scalarmult_ristretto255(ap, a, p) : crypto_scalarmult_ristretto255_base(ap, a)) ||
     crypto_scalarmult_ristretto255(bq, b, q)) {
    return -1;
  }
  return crypto_core_ristretto255_sub(out, ap, bq) ? -1 : 0;
}


int
xcrypt_oprf_check(const uint8_t u[32], const uint8_t v[32], const uint8_t w[32],
                  const uint8_t c[32], const uint8_t beta_z[32]) {
  static const char label[] = "Juicebox_DLEQ_2023_1;";
  crypto_hash_sha512_state s;
  uint8_t v_t[32], w_t[32], digest[64], c2[32];

  if(!crypto_core_ristretto255_is_valid_point(v) || mul_sub(beta_z, NULL, c, v, v_t) ||
     mul_sub(beta_z, u, c, w, w_t)) {
    return -1;
  }
  crypto_hash_sha512_init(&s);
  crypto_hash_sha512_update(&s, (const uint8_t *)label, sizeof label - 1);
  crypto_hash_sha512_update(&s, u, 32);
  crypto_hash_sha512_update(&s, v, 32);
  crypto_hash_sha512_update(&s, w, 32);
  crypto_hash_sha512_update(&s, v_t, 32);
  crypto_hash_sha512_update(&s, w_t, 32);
  crypto_hash_sha512_final(&s, digest);
  xcrypt_scalar_wide(c2, digest);
  return sodium_memcmp(c2, c, 32) ? -1 : 0;
}


int
xcrypt_oprf_finish(const void *input, size_t len, const uint8_t r[32], const uint8_t w[32],
                   uint8_t out[64]) {
  static const char label[] = "Juicebox_OPRF_2023_1;";
  crypto_hash_sha512_state s;
  uint8_t inverse[32], result[32];

  if(crypto_core_ristretto255_scalar_invert(inverse, r) ||
     crypto_scalarmult_ristretto255(result, inverse, w)) {
    xcrypt_wipe(inverse, sizeof inverse);
    return -1;
  }
  crypto_hash_sha512_init(&s);
  crypto_hash_sha512_update(&s, (const uint8_t *)label, sizeof label - 1);
  crypto_hash_sha512_update(&s, input, len);
  crypto_hash_sha512_update(&s, result, sizeof result);
  crypto_hash_sha512_final(&s, out);
  xcrypt_wipe(inverse, sizeof inverse);
  xcrypt_wipe(result, sizeof result);
  xcrypt_wipe(&s, sizeof s);
  return 0;
}


int
xcrypt_oprf_signed(const uint8_t realm[16], const uint8_t public_key[32],
                   const uint8_t verifying_key[32], const uint8_t signature[64]) {
  uint8_t msg[2 + 16 + 2 + 32];

  // be16(16) || realm id || be16(32) || the OPRF's public key
  msg[0] = 0;
  msg[1] = 16;
  memcpy(msg + 2, realm, 16);
  msg[18] = 0;
  msg[19] = 32;
  memcpy(msg + 20, public_key, 32);
  return crypto_sign_ed25519_verify_detached(signature, msg, sizeof msg, verifying_key) ? -1 : 0;
}


/*
 * Juicebox's derivations.
 */

static void
be32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)(value >> 24);
  p[1] = (uint8_t)(value >> 16);
  p[2] = (uint8_t)(value >> 8);
  p[3] = (uint8_t)value;
}


/**
 * a part of a MAC's message: its length (4 bytes, big-endian), then itself
 **/
static void
mac_part(xcrypt_blake2s_t *s, const void *part, size_t len) {
  uint8_t head[4];

  be32(head, (uint32_t)len);
  xcrypt_blake2s_update(s, head, 4);
  xcrypt_blake2s_update(s, part, len);
}


int
xcrypt_pin_hash(const void *pin, size_t len, const uint8_t version[16], const void *info,
                size_t info_len, uint8_t access_key[32], uint8_t seed[32]) {
  uint8_t *salt, out[64];
  size_t salt_len = 4 + 16 + 4 + info_len;
  int ret;

  if(!(salt=malloc(salt_len))) {
    return -1;
  }
  be32(salt, 16);
  memcpy(salt + 4, version, 16);
  be32(salt + 20, (uint32_t)info_len);
  if(info_len) {
    memcpy(salt + 24, info, info_len);
  }
  // Standard2019: 16 MiB, 32 passes, one lane
  ret = argon2id_hash_raw(32, 16 * 1024, 1, pin, len, salt, salt_len, out, sizeof out) ? -1 : 0;
  memcpy(access_key, out, 32);
  memcpy(seed, out + 32, 32);
  xcrypt_wipe(out, sizeof out);
  free(salt);
  return ret;
}


void
xcrypt_unlock_key(const uint8_t oprf[64], uint8_t commitment[32], uint8_t unlock_key[32]) {
  uint8_t digest[64];

  crypto_hash_sha512(digest, oprf, 64);
  memcpy(commitment, digest, 32);
  memcpy(unlock_key, digest + 32, 32);
  xcrypt_wipe(digest, sizeof digest);
}


void
xcrypt_unlock_tag(const uint8_t unlock_key[32], const uint8_t realm[16], uint8_t tag[16]) {
  static const char label[] = "Unlock Key Tag";
  xcrypt_blake2s_t s;

  xcrypt_blake2s_init(&s, 16, unlock_key, 32);
  mac_part(&s, label, sizeof label - 1);
  mac_part(&s, realm, 16);
  xcrypt_blake2s_final(&s, tag);
}


void
xcrypt_secret_commitment(const uint8_t unlock_key[32], const uint8_t realm[16],
                         const uint8_t share[32], const uint8_t secret[145], uint8_t out[16]) {
  static const char label[] = "Encrypted User Secret Commitment";
  xcrypt_blake2s_t s;

  xcrypt_blake2s_init(&s, 16, unlock_key, 32);
  mac_part(&s, label, sizeof label - 1);
  mac_part(&s, realm, 16);
  mac_part(&s, share, 32);
  mac_part(&s, secret, 145);
  xcrypt_blake2s_final(&s, out);
}


int
xcrypt_secret_open_key(const uint8_t key[32], const uint8_t secret[145], uint8_t out[128]) {
  static const uint8_t nonce[12];
  uint8_t padded[129];
  int len = -1;

  if(!crypto_aead_chacha20poly1305_ietf_decrypt(padded, NULL, NULL, secret, 145, NULL, 0, nonce, key) &&
     padded[0] <= 128) {
    len = padded[0];
    memcpy(out, padded + 1, (size_t)len);
  }
  xcrypt_wipe(padded, sizeof padded);
  return len;
}


int
xcrypt_secret_open(const uint8_t seed[32], const uint8_t scalar[32], const uint8_t secret[145],
                   uint8_t out[128]) {
  static const char label[] = "User Secret Encryption Key";
  xcrypt_blake2s_t s;
  uint8_t key[32];
  int len;

  xcrypt_blake2s_init(&s, 32, seed, 32);
  mac_part(&s, label, sizeof label - 1);
  mac_part(&s, scalar, 32);
  xcrypt_blake2s_final(&s, key);
  len = xcrypt_secret_open_key(key, secret, out);
  xcrypt_wipe(key, sizeof key);
  return len;
}


/*
 * X Chat's keys and messages.
 */

/**
 * AES-128-GCM, a 16-byte IV, no additional data: len bytes in place,
 * decrypted (and the tag checked) or encrypted (and the tag made)
 **/
static int
aes_gcm(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t len, uint8_t tag[16],
        int encrypt) {
  br_aes_ct64_ctr_keys aes;
  br_gcm_context gcm;
  int ret = 0;

  br_aes_ct64_ctr_init(&aes, key, 16);
  br_gcm_init(&gcm, &aes.vtable, br_ghash_ctmul64);
  br_gcm_reset(&gcm, iv, 16);
  br_gcm_flip(&gcm);
  br_gcm_run(&gcm, encrypt, data, len);
  if(encrypt) {
    br_gcm_get_tag(&gcm, tag);
  } else {
    ret = br_gcm_check_tag(&gcm, tag) ? 0 : -1;
  }
  xcrypt_wipe(&aes, sizeof aes);
  xcrypt_wipe(&gcm, sizeof gcm);
  return ret;
}


int
xcrypt_key_open(const uint8_t identity[32], const uint8_t *wrapped, size_t len, uint8_t key[32]) {
  static const uint8_t counter[4] = {0, 0, 0, 1};
  uint8_t point[65], k[32], data[32], tag[16];
  br_sha256_context sha;
  int ret;

  if(len != 65 + 32 + 16 || wrapped[0] != 4) {
    return -1;
  }
  // Z, the x of the shared point; K = SHA-256(Z || 00000001 || the ephemeral key)
  memcpy(point, wrapped, 65);
  if(br_ec_p256_m31.mul(point, sizeof point, identity, 32, BR_EC_secp256r1) != 1) {
    xcrypt_wipe(point, sizeof point);
    return -1;
  }
  br_sha256_init(&sha);
  br_sha256_update(&sha, point + 1, 32);
  br_sha256_update(&sha, counter, sizeof counter);
  br_sha256_update(&sha, wrapped, 65);
  br_sha256_out(&sha, k);
  memcpy(data, wrapped + 65, 32);
  memcpy(tag, wrapped + 97, 16);
  ret = aes_gcm(k, k + 16, data, sizeof data, tag, 0);
  if(!ret) {
    memcpy(key, data, 32);
  }
  xcrypt_wipe(point, sizeof point);
  xcrypt_wipe(k, sizeof k);
  xcrypt_wipe(data, sizeof data);
  xcrypt_wipe(&sha, sizeof sha);
  return ret;
}


int
xcrypt_message_key(const uint8_t conversation[32], int version, const char *sender,
                   const char *message_id, uint8_t key[32]) {
  static const char salt[] = "xchat-message-key";
  uint8_t prk[crypto_kdf_hkdf_sha256_KEYBYTES];
  char info[160];
  int n, ret;

  if(!version) {
    memcpy(key, conversation, 32);
    return 0;
  }
  n = snprintf(info, sizeof info, "%s:%s", sender, message_id);
  if(version != 1 || n < 0 || (size_t)n >= sizeof info) {
    return -1;
  }
  ret = crypto_kdf_hkdf_sha256_extract(prk, (const uint8_t *)salt, sizeof salt - 1, conversation, 32) ||
        crypto_kdf_hkdf_sha256_expand(key, 32, info, (size_t)n, prk) ? -1 : 0;
  xcrypt_wipe(prk, sizeof prk);
  return ret;
}


int
xcrypt_message_open(const uint8_t key[32], const uint8_t *contents, size_t len, uint8_t *out) {
  if(len < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) {
    return -1;
  }
  return crypto_secretbox_open_easy(out, contents + crypto_secretbox_NONCEBYTES,
                                    len - crypto_secretbox_NONCEBYTES, contents, key) ? -1 : 0;
}


int
xcrypt_message_seal(const uint8_t key[32], const void *text, size_t len, uint8_t *out) {
  xcrypt_random(out, crypto_secretbox_NONCEBYTES);
  return crypto_secretbox_easy(out + crypto_secretbox_NONCEBYTES, text, len, out, key) ? -1 : 0;
}


int
xcrypt_attachment_key(const uint8_t key[32], const char *id, uint8_t out[32]) {
  static const char salt[] = "xchat-attachment-key";
  uint8_t prk[crypto_kdf_hkdf_sha256_KEYBYTES];
  int ret;

  if(!id) {
    return -1;
  }
  ret = crypto_kdf_hkdf_sha256_extract(prk, (const uint8_t *)salt, sizeof salt - 1, key, 32) ||
        crypto_kdf_hkdf_sha256_expand(out, 32, id, strlen(id), prk) ? -1 : 0;
  xcrypt_wipe(prk, sizeof prk);
  return ret;
}


int
xcrypt_media_open(const uint8_t key[32], const uint8_t *cipher, size_t clen, uint8_t *out,
                  size_t max, size_t *len) {
  crypto_secretstream_xchacha20poly1305_state st;
  const size_t enc_chunk = 1024 + crypto_secretstream_xchacha20poly1305_ABYTES; // X Chat's 1024-byte chunks
  size_t pos = crypto_secretstream_xchacha20poly1305_HEADERBYTES, o = 0;
  int done = 0;

  *len = 0;
  if(clen < crypto_secretstream_xchacha20poly1305_HEADERBYTES ||
     crypto_secretstream_xchacha20poly1305_init_pull(&st, cipher, key)) {
    return -1;
  }
  while(pos < clen && !done) {
    size_t chunk = clen - pos < enc_chunk ? clen - pos : enc_chunk;
    unsigned long long mlen = 0;
    unsigned char tag = 0;

    if(chunk < crypto_secretstream_xchacha20poly1305_ABYTES ||
       o + (chunk - crypto_secretstream_xchacha20poly1305_ABYTES) > max ||
       crypto_secretstream_xchacha20poly1305_pull(&st, out + o, &mlen, &tag, cipher + pos, chunk, NULL, 0)) {
      sodium_memzero(&st, sizeof st);
      return -1;
    }
    o += (size_t)mlen;
    pos += chunk;
    done = tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL;
  }
  sodium_memzero(&st, sizeof st);
  if(!done) {
    return -1; // the stream did not end: something is missing or wrong
  }
  *len = o;
  return 0;
}


/**
 * ECDSA P-256 of a SHA-256 already made; 0 when it holds
 **/
static int
p256_verify_hash(const uint8_t public_key[65], const uint8_t hash[32], const uint8_t sig[64]) {
  br_ec_public_key pk;
  uint8_t q[65];

  memcpy(q, public_key, sizeof q);
  pk.curve = BR_EC_secp256r1;
  pk.q = q;
  pk.qlen = sizeof q;
  // BearSSL checks the point is on the curve, and r and s in range
  return br_ecdsa_i31_vrfy_raw(&br_ec_p256_m31, hash, 32, &pk, sig, 64) == 1 ? 0 : -1;
}


int
xcrypt_p256_verify(const uint8_t public_key[65], const void *msg, size_t len, const uint8_t sig[64]) {
  uint8_t hash[32];
  br_sha256_context sha;

  br_sha256_init(&sha);
  br_sha256_update(&sha, msg, len);
  br_sha256_out(&sha, hash);
  return p256_verify_hash(public_key, hash, sig);
}


int
xcrypt_p256_key(const uint8_t *der, size_t len, uint8_t point[65]) {
  // SubjectPublicKeyInfo of id-ecPublicKey on prime256v1, its bit string
  // of 66 bytes (no unused bits, then the point)
  static const uint8_t spki[26] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,
    0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00};

  if(der && len == 91 && !memcmp(der, spki, sizeof spki)) {
    der += sizeof spki;
    len = 65;
  }
  if(!der || len != 65 || der[0] != 4) {
    return -1;
  }
  memcpy(point, der, 65);
  return 0;
}


/*
 * What a MessageCreateEvent's signature covers, made as X's SDK makes it
 * (chat-xdk, crates/core/src/core.rs): kept in a buffer, or only hashed.
 */
typedef struct sink {
  char              *buf;  // the bytes kept (NULL: none), size of them at most
  size_t             size, len;
  br_sha256_context *sha;  // and hashed (NULL: not)
} sink_t;


static void
sink_put(sink_t *s, const void *p, size_t n) {
  if(s->sha) {
    br_sha256_update(s->sha, p, n);
  }
  if(s->buf && s->len < s->size) {
    memcpy(s->buf + s->len, p, n < s->size - s->len ? n : s->size - s->len);
  }
  s->len += n;
}


static void
sink_str(sink_t *s, const char *str) {
  sink_put(s, str, strlen(str));
}


/**
 * len bytes in base64 of the standard alphabet, unpadded
 **/
static void
sink_b64(sink_t *s, const uint8_t *p, size_t len) {
  static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char out[256];
  size_t o = 0;

  for(size_t i=0; p && i < len; i += 3) {
    const size_t left = len - i, n = left >= 3 ? 4 : left + 1; // 1 byte, 2 letters; 2 bytes, 3
    const uint32_t v = (uint32_t)p[i] << 16 | (left > 1 ? (uint32_t)p[i + 1] << 8 : 0) |
                       (left > 2 ? (uint32_t)p[i + 2] : 0);
    for(size_t k=0; k < n; k++) {
      out[o++] = abc[(v >> (18 - 6 * k)) & 63];
    }
    if(o > sizeof out - 4) {
      sink_put(s, out, o);
      o = 0;
    }
  }
  sink_put(s, out, o);
}


/**
 * text as Rust's parse::<i64>() reads it (a sign or none, then digits only,
 * in range) into its digits as Rust writes the number back; 0, or -1 when
 * it is not one
 **/
static int
signed_number(const char *s, char out[24]) {
  unsigned long long n = 0, limit;
  int negative = 0;

  if(!s) {
    return -1;
  }
  if(*s == '+' || *s == '-') {
    negative = *s++ == '-';
  }
  if(!*s) {
    return -1;
  }
  limit = negative ? 9223372036854775808ull : 9223372036854775807ull;
  for(; *s; s++) {
    const unsigned d = (unsigned)(*s - '0');
    if(*s < '0' || *s > '9' || n > (limit - d) / 10) {
      return -1;
    }
    n = n * 10 + d;
  }
  snprintf(out, 24, "%s%llu", negative && n ? "-" : "", n);
  return 0;
}


/**
 * the bytes signed: 0, or -1 when there are none for m
 **/
static int
signed_parts(const xcrypt_signed_t *m, sink_t *s) {
  char num[24];

  if(!m || m->version < XCRYPT_SIGNED_MIN || m->version > XCRYPT_SIGNED_MAX || !m->message_id ||
     !m->message_id[0] || !m->sender || !m->sender[0] || strchr(m->message_id, ',') ||
     strchr(m->sender, ',') || (m->conversation && strchr(m->conversation, ','))) {
    return -1; // the SDK will not join a part with a comma in it
  }
  sink_str(s, "MessageCreateEvent,");
  sink_str(s, m->message_id);
  sink_str(s, ",");
  sink_str(s, m->sender);
  sink_str(s, ",");
  if(m->conversation) {
    sink_str(s, m->conversation);
    sink_str(s, ",");
  }
  sink_str(s, signed_number(m->key_version, num) ? "null" : num);
  sink_str(s, ",");
  sink_b64(s, m->contents, m->len);
  if(m->version >= 10) {
    snprintf(num, sizeof num, "%d", m->encryption);
    sink_str(s, ",");
    sink_str(s, m->has_encryption ? num : "null");
  }
  if(m->version >= 9) {
    snprintf(num, sizeof num, "%d", m->version);
    sink_str(s, ",signature_version=");
    sink_str(s, num);
  }
  return 0;
}


long
xcrypt_signed_bytes(const xcrypt_signed_t *m, char *buf, size_t size) {
  sink_t s = {buf, size ? size - 1 : 0, 0, NULL};

  if(signed_parts(m, &s)) {
    if(size) {
      buf[0] = 0;
    }
    return -1;
  }
  if(size) {
    buf[s.len < size - 1 ? s.len : size - 1] = 0;
  }
  return (long)s.len;
}


int
xcrypt_signed_version(const char *text) {
  char num[24];
  long v;

  if(signed_number(text, num)) {
    return 0;
  }
  v = strlen(num) < 4 ? strtol(num, NULL, 10) : 0;
  return v >= XCRYPT_SIGNED_MIN && v <= XCRYPT_SIGNED_MAX ? (int)v : 0;
}


int
xcrypt_signed_verify(const uint8_t public_key[65], const xcrypt_signed_t *m, const uint8_t sig[64]) {
  br_sha256_context sha;
  sink_t s = {NULL, 0, 0, &sha};
  uint8_t hash[32];

  br_sha256_init(&sha);
  if(signed_parts(m, &s)) {
    return -1;
  }
  br_sha256_out(&sha, hash);
  return p256_verify_hash(public_key, hash, sig);
}


int
xcrypt_p256_sign(const uint8_t private_key[32], const void *msg, size_t len, uint8_t sig[64]) {
  br_ec_private_key sk;
  uint8_t hash[32], x[32];
  br_sha256_context sha;
  size_t n;

  br_sha256_init(&sha);
  br_sha256_update(&sha, msg, len);
  br_sha256_out(&sha, hash);
  memcpy(x, private_key, sizeof x);
  sk.curve = BR_EC_secp256r1;
  sk.x = x;
  sk.xlen = sizeof x;
  // RFC 6979: its nonce comes from the key and the message, not from chance
  n = br_ecdsa_i31_sign_raw(&br_ec_p256_m31, &br_sha256_vtable, hash, &sk, sig);
  xcrypt_wipe(x, sizeof x);
  return n == 64 ? 0 : -1;
}


int
xcrypt_p256_public(const uint8_t private_key[32], uint8_t public_key[65]) {
  br_ec_private_key sk;
  uint8_t x[32];
  size_t n;

  memcpy(x, private_key, sizeof x);
  sk.curve = BR_EC_secp256r1;
  sk.x = x;
  sk.xlen = sizeof x;
  n = br_ec_compute_pub(&br_ec_p256_m31, NULL, public_key, &sk);
  xcrypt_wipe(x, sizeof x);
  return n == 65 ? 0 : -1;
}


/*
 * CBOR (RFC 8949), the small part of it Juicebox's messages use.
 */

enum { CBOR_UINT = 0, CBOR_BYTES = 2, CBOR_TEXT = 3, CBOR_ARRAY = 4, CBOR_MAP = 5 };


static void
cbor_put(xcrypt_cbor_out_t *o, const void *data, size_t len) {
  if(o->full || o->size - o->len < len) {
    o->full = 1;
    return;
  }
  memcpy(o->buf + o->len, data, len);
  o->len += len;
}


static void
cbor_head(xcrypt_cbor_out_t *o, int major, uint64_t value) {
  uint8_t head[9];
  size_t n;

  if(value < 24) {
    head[0] = (uint8_t)(major << 5 | (int)value);
    n = 1;
  } else {
    // the shortest that holds it, as ciborium writes
    int bytes = value < 0x100 ? 1 : value < 0x10000 ? 2 : value < 0x100000000ull ? 4 : 8;
    head[0] = (uint8_t)(major << 5 | (bytes == 1 ? 24 : bytes == 2 ? 25 : bytes == 4 ? 26 : 27));
    for(int i=0; i<bytes; i++) {
      head[1 + i] = (uint8_t)(value >> 8 * (bytes - 1 - i));
    }
    n = 1 + (size_t)bytes;
  }
  cbor_put(o, head, n);
}


void
xcrypt_cbor_map(xcrypt_cbor_out_t *o, size_t count) {
  cbor_head(o, CBOR_MAP, count);
}


void
xcrypt_cbor_text(xcrypt_cbor_out_t *o, const char *text) {
  size_t len = strlen(text);

  cbor_head(o, CBOR_TEXT, len);
  cbor_put(o, text, len);
}


void
xcrypt_cbor_bytes(xcrypt_cbor_out_t *o, const void *bytes, size_t len) {
  cbor_head(o, CBOR_BYTES, len);
  cbor_put(o, bytes, len);
}


void
xcrypt_cbor_uint(xcrypt_cbor_out_t *o, uint64_t value) {
  cbor_head(o, CBOR_UINT, value);
}


/**
 * an item's head: its major type and its number (a length, a count, or
 * itself); the bytes of the head, or 0
 **/
static size_t
cbor_read_head(const uint8_t *p, size_t len, int *major, uint64_t *value) {
  int info, bytes;

  if(!len) {
    return 0;
  }
  *major = p[0] >> 5;
  info = p[0] & 31;
  if(info < 24) {
    *value = (uint64_t)info;
    return 1;
  }
  if(info > 27) {
    return 0; // indefinite lengths and the reserved ones: none in these messages
  }
  bytes = 1 << (info - 24);
  if(len < 1 + (size_t)bytes) {
    return 0;
  }
  *value = 0;
  for(int i=0; i<bytes; i++) {
    *value = *value << 8 | p[1 + i];
  }
  return 1 + (size_t)bytes;
}


static size_t
cbor_size(const uint8_t *p, size_t len, int depth) {
  size_t head, size, part;
  uint64_t value;
  int major;

  if(depth > 16 || !(head=cbor_read_head(p, len, &major, &value))) {
    return 0;
  }
  switch(major) {
  case CBOR_UINT:
  case 1: // a negative number
    return head;
  case CBOR_BYTES:
  case CBOR_TEXT:
    return value <= len - head ? head + (size_t)value : 0;
  case CBOR_ARRAY:
  case CBOR_MAP:
    if(value > len) {
      return 0;
    }
    size = head;
    for(uint64_t i=0; i < value * (major == CBOR_MAP ? 2 : 1); i++) {
      if(!(part=cbor_size(p + size, len - size, depth + 1))) {
        return 0;
      }
      size += part;
    }
    return size;
  case 7: // false, true, null
    return head == 1 ? 1 : 0;
  default:
    return 0;
  }
}


size_t
xcrypt_cbor_size(xcrypt_cbor_t item) {
  return cbor_size(item.p, item.len, 0);
}


/**
 * whether the item is the text name
 **/
static int
cbor_text_is(xcrypt_cbor_t item, const char *name) {
  uint64_t len;
  size_t head;
  int major;

  head = cbor_read_head(item.p, item.len, &major, &len);
  return head && major == CBOR_TEXT && len == strlen(name) && len <= item.len - head &&
         !memcmp(item.p + head, name, (size_t)len);
}


int
xcrypt_cbor_get(xcrypt_cbor_t map, const char *key, xcrypt_cbor_t *value) {
  size_t head, off, size;
  uint64_t count;
  int major;
  xcrypt_cbor_t k;

  if(!(head=cbor_read_head(map.p, map.len, &major, &count)) || major != CBOR_MAP) {
    return -1;
  }
  off = head;
  for(uint64_t i=0; i<count; i++) {
    k.p = map.p + off;
    k.len = map.len - off;
    if(!(size=xcrypt_cbor_size(k))) {
      return -1;
    }
    off += size;
    value->p = map.p + off;
    value->len = map.len - off;
    if(!(size=xcrypt_cbor_size(*value))) {
      return -1;
    }
    if(cbor_text_is(k, key)) {
      value->len = size;
      return 0;
    }
    off += size;
  }
  return -1;
}


int
xcrypt_cbor_is(xcrypt_cbor_t item, const char *name, xcrypt_cbor_t *data) {
  uint64_t count;
  int major;

  if(cbor_text_is(item, name)) {
    if(data) {
      data->p = NULL;
      data->len = 0;
    }
    return 1;
  }
  if(!cbor_read_head(item.p, item.len, &major, &count) || major != CBOR_MAP || count != 1) {
    return 0;
  }
  if(xcrypt_cbor_get(item, name, data ? data : &(xcrypt_cbor_t){0})) {
    return 0;
  }
  return 1;
}


int
xcrypt_cbor_get_bytes(xcrypt_cbor_t map, const char *key, const uint8_t **bytes, size_t len,
                      size_t *got) {
  xcrypt_cbor_t value;
  uint64_t n;
  size_t head;
  int major;

  if(xcrypt_cbor_get(map, key, &value) || !(head=cbor_read_head(value.p, value.len, &major, &n)) ||
     major != CBOR_BYTES || (len && n != len)) {
    return -1;
  }
  *bytes = value.p + head;
  if(got) {
    *got = (size_t)n;
  }
  return 0;
}


int
xcrypt_cbor_get_uint(xcrypt_cbor_t map, const char *key, uint64_t *value) {
  xcrypt_cbor_t item;
  int major;

  if(xcrypt_cbor_get(map, key, &item) || !cbor_read_head(item.p, item.len, &major, value) ||
     major != CBOR_UINT) {
    return -1;
  }
  return 0;
}
