/* Host tests for src/juicebox.c: the configuration X gives (as its
GetPublicKeysQuery answers it), and the recovery against three software
realms played here as juicebox-sdk 0.3.7's realms answer: a registration
made with the same derivations, the OPRF's shares, its proof and signature,
the guesses counted and reset. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sodium.h>

#include "json.h"
#include "juicebox.h"
#include "mem.h"
#include "xcrypt.h"

#define REALMS    3
#define THRESHOLD 2
#define GUESSES   20

static int g_failed;

#define CHECK(cond, ...) do { \
    if(!(cond)) { \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__); \
      printf("\n"); \
      g_failed = 1; \
    } \
  } while(0)


static void
random_bytes(void *buf, size_t len) {
  FILE *f = fopen("/dev/urandom", "rb");

  if(!f || fread(buf, 1, len, f) != len) {
    abort();
  }
  fclose(f);
}


/**
 * q = n * p on Ristretto255; the test's points are never the identity, so it
 * must hold
 **/
static void
mustmul(uint8_t q[32], const uint8_t n[32], const uint8_t p[32]) {
  if(crypto_scalarmult_ristretto255(q, n, p)) {
    printf("scalarmult failed\n");
    abort();
  }
}


/*
 * The realms
 */

typedef struct realm {
  uint8_t id[16];
  char    token[32];
  uint8_t oprf_key[32];       // its share of the OPRF's key
  uint8_t oprf_public[32];
  uint8_t sign_secret[64];    // Ed25519, for the OPRF's public key
  uint8_t sign_public[32];
  uint8_t tag[16];            // the unlock key's tag it expects
  uint8_t share[32];          // its share of the encryption key's scalar
  uint8_t commitment[16];     // of its share and the encrypted secret
  int     guess_count;
  int     down;               // answers nothing
  int     requests;
  int     bad_bearer;
} realm_t;

static struct {
  realm_t realm[REALMS];
  uint8_t version[16];
  uint8_t unlock_commitment[32];
  uint8_t encrypted[145];
  uint8_t secret[64];
  uint8_t sign_secret[64];      // Ed25519 of the tenant, shared: it signs every realm's OPRF key
  uint8_t sign_public[32];
} g;


static void
scalar_of(uint32_t i, uint8_t out[32]) {
  memset(out, 0, 32);
  out[0] = (uint8_t)i;
  out[1] = (uint8_t)(i >> 8);
}


/**
 * the shares f(1), f(2)... of f(x) = secret + a x, a random: Shamir's of
 * a threshold of 2
 **/
static void
shares(const uint8_t secret[32], uint8_t out[REALMS][32]) {
  uint8_t a[32], x[32], ax[32];

  crypto_core_ristretto255_scalar_random(a);
  for(int i=0; i < REALMS; i++) {
    scalar_of((uint32_t)i + 1, x);
    crypto_core_ristretto255_scalar_mul(ax, a, x);
    crypto_core_ristretto255_scalar_add(out[i], secret, ax);
  }
}


static void
be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}


static void
mac_part(xcrypt_blake2s_t *s, const void *part, size_t len) {
  uint8_t head[4];

  be32(head, (uint32_t)len);
  xcrypt_blake2s_update(s, head, 4);
  xcrypt_blake2s_update(s, part, len);
}


/**
 * a registration of secret (len bytes) with pin, as register.rs makes it
 **/
static void
registration(const char *pin, const uint8_t *secret, size_t len) {
  static const char label[] = "User Secret Encryption Key";
  static const uint8_t nonce[12];
  uint8_t access_key[32], seed[32], root[32], keys[REALMS][32], one[32], u[32], w[32], oprf[64];
  uint8_t unlock_key[32], scalar[32], scalar_shares[REALMS][32], key[32], padded[129];
  unsigned long long clen = 0;
  int order[REALMS];
  xcrypt_blake2s_t s;

  memset(&g, 0, sizeof g);
  randombytes_buf(g.version, sizeof g.version);
  memcpy(g.secret, secret, len);
  for(int i=0; i < REALMS; i++) {
    realm_t *r = &g.realm[i];
    randombytes_buf(r->id, sizeof r->id);
    r->id[0] = (uint8_t)(0x10 * (REALMS - i)); // sorted the other way round than here
    snprintf(r->token, sizeof r->token, "token-%d", i);
  }
  xcrypt_pin_hash(pin, strlen(pin), g.version, NULL, 0, access_key, seed);

  // the realms sorted by id, as jb_config_read sorts them: a share's index
  // is the realm's place in that order, from 1 (so Lagrange reconstructs)
  for(int i=0; i < REALMS; i++) {
    order[i] = i;
  }
  for(int i=0; i < REALMS; i++) {
    for(int j=i + 1; j < REALMS; j++) {
      if(memcmp(g.realm[order[j]].id, g.realm[order[i]].id, 16) < 0) {
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
      }
    }
  }

  // the OPRF's key, shared among the realms by Shamir; one signing key of
  // the tenant signs every realm's OPRF public key, so they share a
  // verifying key (the SDK groups the answers by it and expects one group)
  crypto_sign_ed25519_keypair(g.sign_public, g.sign_secret);
  crypto_core_ristretto255_scalar_random(root);
  shares(root, keys);
  for(int k=0; k < REALMS; k++) {
    realm_t *r = &g.realm[order[k]]; // the realm at sorted place k holds share k (index k+1)
    memcpy(r->oprf_key, keys[k], 32);
    crypto_scalarmult_ristretto255_base(r->oprf_public, r->oprf_key);
  }

  // the unlock key, from the OPRF of the access key under the whole key
  scalar_of(1, one);
  xcrypt_oprf_blind(access_key, sizeof access_key, one, u);
  mustmul(w, root, u);
  xcrypt_oprf_finish(access_key, sizeof access_key, one, w, oprf);
  xcrypt_unlock_key(oprf, g.unlock_commitment, unlock_key);

  // the secret, encrypted with a key of the seed and a scalar that is shared
  crypto_core_ristretto255_scalar_random(scalar);
  shares(scalar, scalar_shares);
  xcrypt_blake2s_init(&s, 32, seed, 32);
  mac_part(&s, label, sizeof label - 1);
  mac_part(&s, scalar, 32);
  xcrypt_blake2s_final(&s, key);
  memset(padded, 0, sizeof padded);
  padded[0] = (uint8_t)len;
  memcpy(padded + 1, secret, len);
  crypto_aead_chacha20poly1305_ietf_encrypt(g.encrypted, &clen, padded, sizeof padded, NULL, 0, NULL, nonce,
                                            key);
  for(int k=0; k < REALMS; k++) {
    realm_t *r = &g.realm[order[k]]; // the share's index follows the sorted place, like the OPRF's
    xcrypt_unlock_tag(unlock_key, r->id, r->tag);
    memcpy(r->share, scalar_shares[k], 32);
    xcrypt_secret_commitment(unlock_key, r->id, r->share, g.encrypted, r->commitment);
  }
}


static uint8_t *
answer_of(xcrypt_cbor_out_t *o, size_t *len) {
  uint8_t *a = malloc(o->len);

  memcpy(a, o->buf, o->len);
  *len = o->len;
  return a;
}


/**
 * a software realm's answer to a SecretsRequest
 **/
static uint8_t *
realm_answer(realm_t *r, const uint8_t *body, size_t blen, size_t *len) {
  static uint8_t buf[2048];
  xcrypt_cbor_out_t o = {buf, 0, sizeof buf, 0};
  xcrypt_cbor_t req = {body, blen}, data;
  const uint8_t *version, *u, *tag;

  if(xcrypt_cbor_is(req, "Recover1", NULL)) {
    xcrypt_cbor_map(&o, 1);
    xcrypt_cbor_text(&o, "Recover1");
    if(r->guess_count >= GUESSES) {
      xcrypt_cbor_text(&o, "NoGuesses");
    } else {
      xcrypt_cbor_map(&o, 1);
      xcrypt_cbor_text(&o, "Ok");
      xcrypt_cbor_map(&o, 1);
      xcrypt_cbor_text(&o, "version");
      xcrypt_cbor_bytes(&o, g.version, 16);
    }
    return answer_of(&o, len);
  }
  if(xcrypt_cbor_is(req, "Recover2", &data) && data.p &&
     !xcrypt_cbor_get_bytes(data, "version", &version, 16, NULL) &&
     !xcrypt_cbor_get_bytes(data, "oprf_blinded_input", &u, 32, NULL)) {
    uint8_t w[32], t[32], vt[32], wt[32], digest[64], c[32], ck[32], beta_z[32], sig[64], msg[52];
    crypto_hash_sha512_state s;
    static const char label[] = "Juicebox_DLEQ_2023_1;";

    xcrypt_cbor_map(&o, 1);
    xcrypt_cbor_text(&o, "Recover2");
    if(memcmp(version, g.version, 16)) {
      xcrypt_cbor_text(&o, "VersionMismatch");
      return answer_of(&o, len);
    }
    if(r->guess_count >= GUESSES) {
      xcrypt_cbor_text(&o, "NoGuesses");
      return answer_of(&o, len);
    }
    r->guess_count++;
    mustmul(w, r->oprf_key, u);
    // the proof that w = k u for v = k G
    crypto_core_ristretto255_scalar_random(t);
    crypto_scalarmult_ristretto255_base(vt, t);
    mustmul(wt, t, u);
    crypto_hash_sha512_init(&s);
    crypto_hash_sha512_update(&s, (const uint8_t *)label, sizeof label - 1);
    crypto_hash_sha512_update(&s, u, 32);
    crypto_hash_sha512_update(&s, r->oprf_public, 32);
    crypto_hash_sha512_update(&s, w, 32);
    crypto_hash_sha512_update(&s, vt, 32);
    crypto_hash_sha512_update(&s, wt, 32);
    crypto_hash_sha512_final(&s, digest);
    crypto_core_ristretto255_scalar_reduce(c, digest);
    crypto_core_ristretto255_scalar_mul(ck, c, r->oprf_key);
    crypto_core_ristretto255_scalar_add(beta_z, t, ck);
    msg[0] = 0;
    msg[1] = 16;
    memcpy(msg + 2, r->id, 16);
    msg[18] = 0;
    msg[19] = 32;
    memcpy(msg + 20, r->oprf_public, 32);
    crypto_sign_ed25519_detached(sig, NULL, msg, sizeof msg, g.sign_secret);

    xcrypt_cbor_map(&o, 1);
    xcrypt_cbor_text(&o, "Ok");
    xcrypt_cbor_map(&o, 6);
    xcrypt_cbor_text(&o, "oprf_signed_public_key");
    xcrypt_cbor_map(&o, 3);
    xcrypt_cbor_text(&o, "public_key");
    xcrypt_cbor_bytes(&o, r->oprf_public, 32);
    xcrypt_cbor_text(&o, "verifying_key");
    xcrypt_cbor_bytes(&o, g.sign_public, 32);
    xcrypt_cbor_text(&o, "signature");
    xcrypt_cbor_bytes(&o, sig, 64);
    xcrypt_cbor_text(&o, "oprf_blinded_result");
    xcrypt_cbor_bytes(&o, w, 32);
    xcrypt_cbor_text(&o, "oprf_proof");
    xcrypt_cbor_map(&o, 2);
    xcrypt_cbor_text(&o, "c");
    xcrypt_cbor_bytes(&o, c, 32);
    xcrypt_cbor_text(&o, "beta_z");
    xcrypt_cbor_bytes(&o, beta_z, 32);
    xcrypt_cbor_text(&o, "unlock_key_commitment");
    xcrypt_cbor_bytes(&o, g.unlock_commitment, 32);
    xcrypt_cbor_text(&o, "num_guesses");
    xcrypt_cbor_uint(&o, GUESSES);
    xcrypt_cbor_text(&o, "guess_count");
    xcrypt_cbor_uint(&o, (uint64_t)r->guess_count);
    return answer_of(&o, len);
  }
  if(xcrypt_cbor_is(req, "Recover3", &data) && data.p &&
     !xcrypt_cbor_get_bytes(data, "version", &version, 16, NULL) &&
     !xcrypt_cbor_get_bytes(data, "unlock_key_tag", &tag, 16, NULL)) {
    xcrypt_cbor_map(&o, 1);
    xcrypt_cbor_text(&o, "Recover3");
    if(memcmp(tag, r->tag, 16)) {
      xcrypt_cbor_map(&o, 1);
      xcrypt_cbor_text(&o, "BadUnlockKeyTag");
      xcrypt_cbor_map(&o, 1);
      xcrypt_cbor_text(&o, "guesses_remaining");
      xcrypt_cbor_uint(&o, (uint64_t)(GUESSES - r->guess_count));
      return answer_of(&o, len);
    }
    r->guess_count = 0;
    xcrypt_cbor_map(&o, 1);
    xcrypt_cbor_text(&o, "Ok");
    xcrypt_cbor_map(&o, 3);
    xcrypt_cbor_text(&o, "encryption_key_scalar_share");
    xcrypt_cbor_bytes(&o, r->share, 32);
    xcrypt_cbor_text(&o, "encrypted_secret");
    xcrypt_cbor_bytes(&o, g.encrypted, 145);
    xcrypt_cbor_text(&o, "encrypted_secret_commitment");
    xcrypt_cbor_bytes(&o, r->commitment, 16);
    return answer_of(&o, len);
  }
  return NULL;
}


static int
fake_post(void *ctx, const char *url, const char *bearer, const uint8_t *body, size_t len, int *status,
          uint8_t **answer, size_t *answer_len) {
  (void)ctx;
  for(int i=0; i < REALMS; i++) {
    realm_t *r = &g.realm[i];
    char want[64];
    snprintf(want, sizeof want, "https://realm-%d.example/req", i);
    if(strcmp(url, want)) {
      continue;
    }
    r->requests++;
    if(r->down) {
      return -1;
    }
    if(!bearer || strcmp(bearer, r->token)) {
      r->bad_bearer++;
      *status = 401;
      *answer = NULL;
      *answer_len = 0;
      return 0;
    }
    *answer = realm_answer(r, body, len, answer_len);
    *status = *answer ? 200 : 400;
    return 0;
  }
  return -1;
}


static void
fake_release(void *ctx, uint8_t *answer) {
  (void)ctx;
  free(answer);
}


/**
 * the juicebox_config of GetPublicKeysQuery's token_map for the realms
 **/
static void
config_json(char *buf, size_t size, int embedded) {
  char sdk[1024], hex[33], line[256];
  size_t n = 0, m = 0;

  m += (size_t)snprintf(sdk + m, sizeof sdk - m, "{\\\"realms\\\":[");
  for(int i=0; i < REALMS; i++) {
    sodium_bin2hex(hex, sizeof hex, g.realm[i].id, 16);
    m += (size_t)snprintf(sdk + m, sizeof sdk - m, "%s{\\\"id\\\":\\\"%s\\\",\\\"address\\\":"
                          "\\\"https://realm-%d.example/\\\"}", i ? "," : "", hex, i);
  }
  m += (size_t)snprintf(sdk + m, sizeof sdk - m, "],\\\"register_threshold\\\":3,\\\"recover_threshold\\\":%d,"
                        "\\\"pin_hashing_mode\\\":\\\"Standard2019\\\"}", THRESHOLD);
  n += (size_t)snprintf(buf + n, size - n, "{\"__typename\":\"KeyStoreTokenMap\",\"max_guess_count\":%d,"
                        "\"recover_threshold\":%d,\"register_threshold\":3,\"token_map\":[", GUESSES, THRESHOLD);
  for(int i=0; i < REALMS; i++) {
    sodium_bin2hex(hex, sizeof hex, g.realm[i].id, 16);
    snprintf(line, sizeof line, "%s{\"key\":\"%s\",\"value\":{\"token\":\"%s\",\"address\":"
             "\"https://realm-%d.example\",\"public_key\":null}}", i ? "," : "", hex, g.realm[i].token, i);
    n += (size_t)snprintf(buf + n, size - n, "%s", line);
  }
  n += (size_t)snprintf(buf + n, size - n, "],\"key_store_token_map_json\":%s%s%s}", embedded ? "\"" : "",
                        embedded ? sdk : "null", embedded ? "\"" : "");
}


static int
read_config(jb_config_t *c, int embedded) {
  static char text[8192];
  json_doc_t *doc;
  size_t off;
  int ret;

  config_json(text, sizeof text, embedded);
  if(!(doc=json_parse(text, strlen(text), &off))) {
    printf("config JSON does not parse at %zu\n", off);
    return -1;
  }
  ret = jb_config_read(json_root(doc), c);
  json_free(doc);
  return ret;
}


static void
test_config(void) {
  jb_config_t c;

  for(int embedded=0; embedded < 2; embedded++) {
    registration("1234", (const uint8_t *)"x", 1);
    CHECK(!read_config(&c, embedded), "config %d reads", embedded);
    CHECK(c.count == REALMS && c.recover_threshold == THRESHOLD && c.max_guesses == GUESSES,
          "config %d: %d realms, threshold %d, %d guesses", embedded, c.count, c.recover_threshold, c.max_guesses);
    for(int i=0; i < c.count; i++) {
      char want[64];
      // sorted by id: the realms were made the other way round
      int j = REALMS - 1 - i;
      snprintf(want, sizeof want, "https://realm-%d.example/req", j);
      CHECK(!memcmp(c.realm[i].id, g.realm[j].id, 16), "config %d: realm %d sorted", embedded, i);
      CHECK(!strcmp(c.realm[i].url, want), "config %d: realm %d url %s", embedded, i, c.realm[i].url);
      CHECK(c.realm[i].token && !strcmp(c.realm[i].token, g.realm[j].token), "config %d: realm %d token",
            embedded, i);
      CHECK(!c.realm[i].hardware, "config %d: realm %d software", embedded, i);
    }
    jb_config_free(&c);
  }
}


static const jb_net_t g_net = {fake_post, fake_release, NULL};


static void
test_recover(void) {
  static const uint8_t keys[64] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
    58, 59, 60, 61, 62, 63, 64};
  uint8_t secret[JB_SECRET];
  jb_config_t c;
  jb_result_t res;
  int len;

  registration("2580", keys, sizeof keys);
  CHECK(!read_config(&c, 1), "config reads");

  // registered, and no guess counted for asking
  jb_check(&c, &g_net, &res);
  CHECK(res.code == JB_OK && res.realms_ok == REALMS, "check: %s, %d realms", jb_code_name(res.code),
        res.realms_ok);
  for(int i=0; i < REALMS; i++) {
    CHECK(g.realm[i].guess_count == 0, "check counts no guess at realm %d", i);
    CHECK(!g.realm[i].bad_bearer, "realm %d had its token", i);
  }

  // a wrong PIN: one guess at each realm, and how many are left
  jb_recover(&c, &g_net, "1111", 4, secret, &len, &res);
  CHECK(res.code == JB_WRONG_PIN && res.guesses_left == GUESSES - 1 && len == 0,
        "wrong PIN: %s, %d left, %d bytes", jb_code_name(res.code), res.guesses_left, len);
  for(int i=0; i < REALMS; i++) {
    CHECK(g.realm[i].guess_count == 1, "wrong PIN counted at realm %d (%d)", i, g.realm[i].guess_count);
  }

  // the right one: the secret, and the guesses reset
  jb_recover(&c, &g_net, "2580", 4, secret, &len, &res);
  CHECK(res.code == JB_OK && len == (int)sizeof keys && !memcmp(secret, keys, sizeof keys),
        "right PIN: %s, %d bytes", jb_code_name(res.code), len);
  for(int i=0; i < REALMS; i++) {
    CHECK(g.realm[i].guess_count == 0, "guesses reset at realm %d", i);
  }

  // one realm down: two are enough
  g.realm[1].down = 1;
  jb_recover(&c, &g_net, "2580", 4, secret, &len, &res);
  CHECK(res.code == JB_OK && len == (int)sizeof keys && !memcmp(secret, keys, sizeof keys),
        "one realm down: %s, %d bytes", jb_code_name(res.code), len);

  // two down: not enough, and no guess spent
  g.realm[2].down = 1;
  g.realm[0].guess_count = 0;
  jb_recover(&c, &g_net, "2580", 4, secret, &len, &res);
  CHECK(res.code == JB_NETWORK && res.step == 1 && len == 0, "two realms down: %s at step %d",
        jb_code_name(res.code), res.step);
  CHECK(g.realm[0].guess_count == 0, "no guess spent when the realms are not enough");
  g.realm[1].down = g.realm[2].down = 0;

  // no guesses left anywhere
  for(int i=0; i < REALMS; i++) {
    g.realm[i].guess_count = GUESSES;
  }
  jb_recover(&c, &g_net, "2580", 4, secret, &len, &res);
  CHECK(res.code == JB_NO_GUESSES && res.guesses_left == 0, "no guesses: %s, %d left", jb_code_name(res.code),
        res.guesses_left);
  jb_config_free(&c);
}


int
main(void) {
  xcrypt_init(random_bytes);
  test_config();
  test_recover();
  if(!g_failed) {
    printf("all Juicebox checks passed\n");
  }
  return g_failed;
}
