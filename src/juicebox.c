/* The recovery of the X Chat keys from Juicebox's realms, see juicebox.h.
It follows juicebox-sdk 0.3.7's rust/sdk/src/recover.rs and request.rs: each
step goes to every realm of the configuration, and holds when at least
recover_threshold of them answered alike; otherwise the "least bad" error
of the realms is the answer, in the SDK's order. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "juicebox.h"
#include "mem.h"

#define BODY_MAX     2048  // the realms' limit on a request
#define PADDED       436   // the bytes of a hardware realm's padded answer
#define ATTEMPTS     5     // tries of a hardware realm's request, as the SDK's
#define VERSION_LEN  16

// what a realm's request came to: 0, or a JB_* code
typedef struct session {
  int            open;
  uint32_t       id;
  xcrypt_noise_t noise;
} session_t;

typedef struct run {
  const jb_config_t *c;
  const jb_net_t    *net;
  session_t          s[JB_REALMS];
  int                missing;  // a realm forgot the session: a new handshake
} run_t;


/*
 * The configuration
 */

static int
hex_value(int c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
         c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}


/**
 * exactly len bytes of hex into out; 0 or -1
 **/
static int
hex_bytes(const char *s, uint8_t *out, size_t len) {
  if(!s || strlen(s) != len * 2) {
    return -1;
  }
  for(size_t i=0; i < len; i++) {
    int hi = hex_value((unsigned char)s[2 * i]), lo = hex_value((unsigned char)s[2 * i + 1]);
    if(hi < 0 || lo < 0) {
      return -1;
    }
    out[i] = (uint8_t)(hi << 4 | lo);
  }
  return 0;
}


static long
json_long(const json_node_t *n, long fallback) {
  char buf[24];

  if(!n || (n->type != JSON_NUMBER && n->type != JSON_STRING) || !n->str || !n->len ||
     n->len >= sizeof buf) {
    return fallback;
  }
  memcpy(buf, n->str, n->len);
  buf[n->len] = 0;
  for(size_t i=0; i < n->len; i++) {
    if(buf[i] < '0' || buf[i] > '9') {
      return fallback;
    }
  }
  return atol(buf);
}


static int
by_id(const void *a, const void *b) {
  return memcmp(((const jb_realm_t *)a)->id, ((const jb_realm_t *)b)->id, 16);
}


/**
 * a realm: its id and address, and its public key when it has one
 **/
static int
add_realm(jb_config_t *c, const char *id, const char *address, const char *public_key) {
  jb_realm_t *r;
  size_t len;

  if(c->count >= JB_REALMS || !address || strncmp(address, "https://", 8)) {
    return -1;
  }
  r = &c->realm[c->count];
  memset(r, 0, sizeof *r);
  len = strlen(address);
  if(hex_bytes(id, r->id, sizeof r->id) ||
     snprintf(r->url, sizeof r->url, "%s%sreq", address, len && address[len - 1] == '/' ? "" : "/") >=
     (int)sizeof r->url) {
    return -1;
  }
  if(public_key && public_key[0]) {
    if(hex_bytes(public_key, r->public_key, sizeof r->public_key)) {
      return -1;
    }
    r->hardware = 1;
  }
  for(int i=0; i < c->count; i++) {
    if(!memcmp(c->realm[i].id, r->id, sizeof r->id)) {
      return -1; // the same realm twice
    }
  }
  c->count++;
  return 0;
}


int
jb_config_read(const json_node_t *juicebox_config, jb_config_t *c) {
  const json_node_t *map = json_get(juicebox_config, "token_map"), *realms, *root;
  const char *embedded = json_string(json_get(juicebox_config, "key_store_token_map_json"));
  const char *mode;
  json_doc_t *doc = NULL;
  char *text = NULL;
  size_t off, used = 0, size = 0;
  long threshold = 0;

  memset(c, 0, sizeof *c);
  c->max_guesses = (int)json_long(json_get(juicebox_config, "max_guess_count"), 20);
  if(embedded && embedded[0] && (text=mem_alloc(strlen(embedded) + 1))) {
    // the SDK's configuration as X keeps it, used as it is: the realms with
    // their public keys, and the thresholds
    memcpy(text, embedded, strlen(embedded) + 1);
    if((doc=json_parse(text, strlen(text), &off))) {
      root = json_root(doc);
      mode = json_string(json_get(root, "pin_hashing_mode"));
      realms = json_get(root, "realms");
      if(mode && strcmp(mode, "Standard2019")) {
        goto bad;
      }
      for(const json_node_t *r=realms ? realms->child : NULL; r; r=r->next) {
        if(add_realm(c, json_string(json_get(r, "id")), json_string(json_get(r, "address")),
                     json_string(json_get(r, "public_key")))) {
          goto bad;
        }
      }
      threshold = json_long(json_get(root, "recover_threshold"), 0);
    }
  }
  if(!c->count) {
    // no configuration of the SDK's: the token map's realms, which say their
    // public keys too
    for(const json_node_t *e=map ? map->child : NULL; e; e=e->next) {
      const json_node_t *v = json_get(e, "value");
      if(add_realm(c, json_string(json_get(e, "key")), json_string(json_get(v, "address")),
                   json_string(json_get(v, "public_key")))) {
        goto bad;
      }
    }
    threshold = json_long(json_get(juicebox_config, "recover_threshold"), 0);
  }
  if(!c->count) {
    goto bad;
  }
  if(threshold <= 0) {
    threshold = c->count / 2 + 1;
  }
  // a majority of the realms, as the SDK checks it
  if(threshold > c->count || threshold <= c->count / 2) {
    goto bad;
  }
  c->recover_threshold = (int)threshold;

  // each realm's token, from the token map
  for(const json_node_t *e=map ? map->child : NULL; e; e=e->next) {
    const char *t = json_string(json_path(e, "value", "token", NULL));
    size += t ? strlen(t) + 1 : 0;
  }
  if(!size || !(c->tokens=mem_alloc(size))) {
    goto bad;
  }
  for(const json_node_t *e=map ? map->child : NULL; e; e=e->next) {
    const char *t = json_string(json_path(e, "value", "token", NULL));
    uint8_t id[16];
    if(!t || hex_bytes(json_string(json_get(e, "key")), id, sizeof id)) {
      continue;
    }
    for(int i=0; i < c->count; i++) {
      if(!memcmp(c->realm[i].id, id, sizeof id) && !c->realm[i].token) {
        memcpy(c->tokens + used, t, strlen(t) + 1);
        c->realm[i].token = c->tokens + used;
        used += strlen(t) + 1;
      }
    }
  }
  for(int i=0; i < c->count; i++) {
    if(!c->realm[i].token) {
      goto bad; // a realm X gave no token for
    }
  }
  qsort(c->realm, (size_t)c->count, sizeof c->realm[0], by_id);
  if(doc) {
    json_free(doc);
  }
  mem_free(text);
  return 0;

bad:
  if(doc) {
    json_free(doc);
  }
  mem_free(text);
  jb_config_free(c);
  return -1;
}


void
jb_config_free(jb_config_t *c) {
  if(c->tokens) {
    xcrypt_wipe(c->tokens, strlen(c->tokens));
    mem_free(c->tokens);
  }
  xcrypt_wipe(c, sizeof *c);
}


/*
 * A realm's request
 */

/**
 * which of two errors the SDK keeps when too many realms failed: the
 * smallest of RecoverError's order (InvalidPin, NotRegistered, InvalidAuth,
 * UpgradeRequired, RateLimitExceeded, Assertion, Transient)
 **/
static int
rank(int code) {
  switch(code) {
  case JB_WRONG_PIN: case JB_NO_GUESSES: return 0;
  case JB_NOT_REGISTERED: return 1;
  case JB_REFUSED: return 2;
  case JB_UPGRADE: return 3;
  case JB_LIMITED: return 4;
  case JB_BAD: return 5;
  default: return 6;
  }
}


static int
http_code(int status) {
  return status == 401 ? JB_REFUSED : status == 426 ? JB_UPGRADE : status == 429 ? JB_LIMITED : JB_NETWORK;
}


/**
 * POST body to the realm; the answer of a 200 into out (size bytes), its
 * length in *len; 0 or a JB_* code
 **/
static int
post(const run_t *run, const jb_realm_t *r, const char *bearer, const uint8_t *body, size_t blen,
     uint8_t *out, size_t size, size_t *len) {
  uint8_t *answer = NULL;
  size_t alen = 0;
  int status = 0, code;

  if(run->net->post(run->net->ctx, r->url, bearer, body, blen, &status, &answer, &alen)) {
    return JB_NETWORK;
  }
  code = status != 200 ? http_code(status) : !answer || alen > size ? JB_BAD : 0;
  if(!code) {
    memcpy(out, answer, alen);
    *len = alen;
  }
  if(answer) {
    xcrypt_wipe(answer, alen);
    run->net->release(run->net->ctx, answer);
  }
  return code;
}


/**
 * what a hardware realm answers that is not Ok: a JB_* code, or -1 for
 * MissingSession
 **/
static int
client_error(xcrypt_cbor_t answer) {
  if(xcrypt_cbor_is(answer, "Unavailable", NULL)) {
    return JB_NETWORK;
  }
  if(xcrypt_cbor_is(answer, "InvalidAuth", NULL)) {
    return JB_REFUSED;
  }
  if(xcrypt_cbor_is(answer, "MissingSession", NULL)) {
    return -1;
  }
  if(xcrypt_cbor_is(answer, "RateLimitExceeded", NULL)) {
    return JB_LIMITED;
  }
  return JB_BAD; // SessionError, DecodingError, PayloadTooLarge, or not one
}


/**
 * the ClientRequest of a hardware realm, its encrypted part in Noise's
 * Handshake (e_pub set) or Transport; its length, or 0
 **/
static size_t
client_request(const jb_realm_t *r, uint32_t session, const char *kind, const uint8_t *e_pub,
               const uint8_t *ct, size_t ct_len, uint8_t *body, size_t size) {
  xcrypt_cbor_out_t o = {body, 0, size, 0};

  xcrypt_cbor_map(&o, 5);
  xcrypt_cbor_text(&o, "realm");
  xcrypt_cbor_bytes(&o, r->id, sizeof r->id);
  xcrypt_cbor_text(&o, "auth_token");
  xcrypt_cbor_text(&o, r->token);
  xcrypt_cbor_text(&o, "session_id");
  xcrypt_cbor_uint(&o, session);
  xcrypt_cbor_text(&o, "kind");
  xcrypt_cbor_text(&o, kind);
  xcrypt_cbor_text(&o, "encrypted");
  xcrypt_cbor_map(&o, 1);
  if(e_pub) {
    xcrypt_cbor_text(&o, "Handshake");
    xcrypt_cbor_map(&o, 1);
    xcrypt_cbor_text(&o, "handshake");
    xcrypt_cbor_map(&o, 2);
    xcrypt_cbor_text(&o, "client_ephemeral_public");
    xcrypt_cbor_bytes(&o, e_pub, 32);
    xcrypt_cbor_text(&o, "payload_ciphertext");
    xcrypt_cbor_bytes(&o, ct, ct_len);
  } else {
    xcrypt_cbor_text(&o, "Transport");
    xcrypt_cbor_map(&o, 1);
    xcrypt_cbor_text(&o, "ciphertext");
    xcrypt_cbor_bytes(&o, ct, ct_len);
  }
  return o.full ? 0 : o.len;
}


/**
 * a hardware realm's padded answer into the SecretsResponse it holds (moved
 * to the start of buf); its length, or 0
 **/
static size_t
unpad(uint8_t *buf, size_t len) {
  xcrypt_cbor_t padded = {buf, len};
  const uint8_t *bytes;
  uint64_t unpadded;

  if(xcrypt_cbor_get_uint(padded, "unpadded_length", &unpadded) ||
     xcrypt_cbor_get_bytes(padded, "padded_bytes", &bytes, PADDED, NULL) || !unpadded ||
     unpadded > PADDED) {
    return 0;
  }
  memmove(buf, bytes, (size_t)unpadded);
  return (size_t)unpadded;
}


/**
 * Noise's handshake with realm i, with the request req (or none, len 0):
 * the session opened, and the realm's answer to req into out; 0 or a JB_*
 * code, -1 for MissingSession
 **/
static int
handshake(run_t *run, int i, const uint8_t *req, size_t len, uint8_t *out, size_t *out_len) {
  const jb_realm_t *r = &run->c->realm[i];
  session_t *s = &run->s[i];
  uint8_t body[BODY_MAX], ct[BODY_MAX], answer[BODY_MAX], e_pub[32];
  const uint8_t *re, *pct;
  xcrypt_cbor_t a, ok, hs, inner;
  size_t blen, alen = 0, plen = 0;
  int code;

  memset(s, 0, sizeof *s);
  xcrypt_random(&s->id, sizeof s->id);
  if(len + 16 > sizeof ct ||
     xcrypt_noise_start(&s->noise, r->public_key, len ? req : (const uint8_t *)"", len, e_pub, ct) ||
     !(blen=client_request(r, s->id, len ? "SecretsRequest" : "HandshakeOnly", e_pub, ct, len + 16, body,
                           sizeof body))) {
    code = JB_BAD;
    goto done;
  }
  if((code=post(run, r, NULL, body, blen, answer, sizeof answer, &alen))) {
    goto done;
  }
  a = (xcrypt_cbor_t){answer, alen};
  if(!xcrypt_cbor_is(a, "Ok", &ok)) {
    code = client_error(a);
    goto done;
  }
  if(!xcrypt_cbor_is(ok, "Handshake", &hs) || xcrypt_cbor_get(hs, "handshake", &inner) ||
     xcrypt_cbor_get_bytes(inner, "server_ephemeral_public", &re, 32, NULL) ||
     xcrypt_cbor_get_bytes(inner, "payload_ciphertext", &pct, 0, &plen) || plen < 16 ||
     plen - 16 > sizeof ct || xcrypt_noise_finish(&s->noise, re, pct, plen, ct)) {
    code = JB_BAD;
    goto done;
  }
  plen -= 16;
  if(!len) {
    code = plen ? JB_BAD : 0; // a handshake alone answers nothing
  } else if(!(plen=unpad(ct, plen))) {
    code = JB_BAD;
  } else {
    memcpy(out, ct, plen);
    *out_len = plen;
  }
  s->open = !code;

done:
  xcrypt_wipe(ct, sizeof ct);
  xcrypt_wipe(answer, sizeof answer);
  if(code) {
    xcrypt_wipe(s, sizeof *s);
  }
  return code;
}


/**
 * req through realm i's open session; its answer into out; 0, a JB_* code,
 * or -1 for MissingSession
 **/
static int
transport(run_t *run, int i, const uint8_t *req, size_t len, uint8_t *out, size_t *out_len) {
  const jb_realm_t *r = &run->c->realm[i];
  session_t *s = &run->s[i];
  uint8_t body[BODY_MAX], ct[BODY_MAX], answer[BODY_MAX];
  const uint8_t *rct;
  xcrypt_cbor_t a, ok, tr;
  size_t blen, alen = 0, clen = 0;
  int code;

  if(len + 16 > sizeof ct || xcrypt_noise_seal(&s->noise, req, len, ct) ||
     !(blen=client_request(r, s->id, "SecretsRequest", NULL, ct, len + 16, body, sizeof body))) {
    code = JB_BAD;
    goto done;
  }
  if((code=post(run, r, NULL, body, blen, answer, sizeof answer, &alen))) {
    goto done;
  }
  a = (xcrypt_cbor_t){answer, alen};
  if(!xcrypt_cbor_is(a, "Ok", &ok)) {
    code = client_error(a);
    goto done;
  }
  if(!xcrypt_cbor_is(ok, "Transport", &tr) || xcrypt_cbor_get_bytes(tr, "ciphertext", &rct, 0, &clen) ||
     clen < 16 || clen - 16 > sizeof ct || xcrypt_noise_open(&s->noise, rct, clen, ct) ||
     !(clen=unpad(ct, clen - 16))) {
    code = JB_BAD;
    goto done;
  }
  memcpy(out, ct, clen);
  *out_len = clen;

done:
  xcrypt_wipe(ct, sizeof ct);
  xcrypt_wipe(answer, sizeof answer);
  if(code) {
    s->open = 0;
  }
  return code;
}


/**
 * the SecretsRequest req (len bytes) to realm i; its SecretsResponse into
 * out (BODY_MAX bytes); 0 or a JB_* code. secret: the request wants a
 * session of its own (Recover2 and Recover3)
 **/
static int
request(run_t *run, int i, const uint8_t *req, size_t len, int secret, uint8_t *out, size_t *out_len) {
  const jb_realm_t *r = &run->c->realm[i];
  session_t *s = &run->s[i];
  int code = JB_NETWORK;

  if(!r->hardware) {
    // a software realm: the request as it is, its token as the bearer; a
    // second try after a network error, as the SDK does
    for(int attempt=0; attempt < 2; attempt++) {
      if((code=post(run, r, r->token, req, len, out, BODY_MAX, out_len)) != JB_NETWORK) {
        break;
      }
    }
    return code;
  }
  for(int attempt=0; attempt < ATTEMPTS; attempt++) {
    if(!s->open && secret) {
      // its own session first, then the request through it
      if(!(code=handshake(run, i, NULL, 0, NULL, NULL))) {
        code = transport(run, i, req, len, out, out_len);
        code = code < 0 ? JB_BAD : code;
      }
    } else if(!s->open) {
      code = handshake(run, i, req, len, out, out_len);
    } else {
      code = transport(run, i, req, len, out, out_len);
    }
    if(code < 0) {
      run->missing++;
      s->open = 0;
      continue; // the realm forgot the session: a new one
    }
    if(code != JB_NETWORK) {
      break;
    }
  }
  return code < 0 ? JB_NETWORK : code;
}


/**
 * a SecretsResponse of the step name ("Recover1"...): its data
 **/
static int
step_answer(const uint8_t *buf, size_t len, const char *name, xcrypt_cbor_t *data) {
  xcrypt_cbor_t a = {buf, len};
  return xcrypt_cbor_is(a, name, data) && data->p ? 0 : -1;
}


/*
 * The steps
 */

typedef struct state {
  run_t    run;
  int      ok[JB_REALMS];         // the realm answered the step
  int      worst;                 // the error kept, JB_OK for none
  int      errors;
  int      guesses[JB_REALMS];    // each realm's guesses left, -1 for not said
  uint8_t  version[JB_REALMS][VERSION_LEN];
} state_t;


static void
failed(state_t *st, int code, int guesses) {
  st->errors++;
  if(!st->worst || rank(code) < rank(st->worst)) {
    st->worst = code;
    st->guesses[0] = guesses; // kept for the answer
  }
}


/**
 * Recover1 to each realm; the version recover_threshold of them agree on
 * into version, and which they are in st->ok; 0, or a JB_* code
 **/
static int
recover1(state_t *st, uint8_t version[VERSION_LEN], jb_result_t *res) {
  const jb_config_t *c = st->run.c;
  uint8_t req[16], out[BODY_MAX];
  xcrypt_cbor_out_t o = {req, 0, sizeof req, 0};
  xcrypt_cbor_t data, ok;
  const uint8_t *v;
  size_t len = 0;
  int code, best = -1, agree = 0;

  xcrypt_cbor_text(&o, "Recover1");
  for(int i=0; i < c->count; i++) {
    st->ok[i] = 0;
    if((code=request(&st->run, i, req, o.len, 0, out, &len))) {
      failed(st, code, -1);
    } else if(step_answer(out, len, "Recover1", &data)) {
      failed(st, JB_BAD, -1);
    } else if(xcrypt_cbor_is(data, "Ok", &ok) && ok.p &&
              !xcrypt_cbor_get_bytes(ok, "version", &v, VERSION_LEN, NULL)) {
      memcpy(st->version[i], v, VERSION_LEN);
      st->ok[i] = 1;
    } else if(xcrypt_cbor_is(data, "NotRegistered", NULL)) {
      failed(st, JB_NOT_REGISTERED, -1);
    } else if(xcrypt_cbor_is(data, "NoGuesses", NULL)) {
      failed(st, JB_NO_GUESSES, 0);
    } else {
      failed(st, JB_BAD, -1);
    }
    if(st->errors > c->count - c->recover_threshold) {
      res->step = 1;
      return st->worst;
    }
  }
  // the version most realms have, if enough of them have it
  for(int i=0; i < c->count; i++) {
    int n = 0;
    for(int j=0; st->ok[i] && j < c->count; j++) {
      n += st->ok[j] && !memcmp(st->version[i], st->version[j], VERSION_LEN);
    }
    if(n > agree) {
      agree = n;
      best = i;
    }
  }
  res->step = 1;
  res->realms_ok = agree;
  if(best < 0 || agree < c->recover_threshold) {
    return JB_NOT_REGISTERED;
  }
  memcpy(version, st->version[best], VERSION_LEN);
  for(int i=0; i < c->count; i++) {
    st->ok[i] = st->ok[i] && !memcmp(st->version[i], version, VERSION_LEN);
  }
  return 0;
}


static void
run_init(state_t *st, const jb_config_t *c, const jb_net_t *net) {
  memset(st, 0, sizeof *st);
  st->run.c = c;
  st->run.net = net;
}


void
jb_check(const jb_config_t *c, const jb_net_t *net, jb_result_t *res) {
  state_t *st = mem_calloc(1, sizeof *st);
  uint8_t version[VERSION_LEN];

  memset(res, 0, sizeof *res);
  res->guesses_left = -1;
  if(!st) {
    res->code = JB_NETWORK;
    return;
  }
  run_init(st, c, net);
  res->code = c->count ? recover1(st, version, res) : JB_BAD;
  if(res->code == JB_NO_GUESSES) {
    res->guesses_left = 0;
  }
  xcrypt_wipe(st, sizeof *st);
  mem_free(st);
}


typedef struct share2 {
  uint32_t index;
  uint8_t  point[32];
  uint8_t  commitment[32];
  uint8_t  verifying_key[32];
  int      guesses;
} share2_t;


typedef struct share3 {
  uint32_t index;
  uint8_t  scalar[32];
  uint8_t  secret[145];
} share3_t;


void
jb_recover(const jb_config_t *c, const jb_net_t *net, const void *pin, size_t pin_len,
           uint8_t secret[JB_SECRET], int *len, jb_result_t *res) {
  state_t *st = mem_calloc(1, sizeof *st);
  share2_t *s2 = mem_calloc(JB_REALMS, sizeof *s2);
  share3_t *s3 = mem_calloc(JB_REALMS, sizeof *s3);
  uint8_t version[VERSION_LEN], access_key[32], seed[32], r[32], u[32], unlock_key[32];
  uint8_t commitment[32], oprf[64], combined[32], req[256], out[BODY_MAX];
  uint8_t (*points)[32] = NULL;
  uint32_t index[JB_REALMS];
  xcrypt_cbor_out_t o;
  xcrypt_cbor_t data, ok, spk, proof;
  const uint8_t *pk, *vk, *sig, *w, *pc, *pz, *com, *sh, *es, *esc;
  size_t olen = 0;
  uint64_t num, count;
  int code, n2 = 0, n3 = 0, best, agree, group, open, least;

  memset(res, 0, sizeof *res);
  res->guesses_left = -1;
  *len = 0;
  if(!st || !s2 || !s3 || !c->count) {
    res->code = !c->count ? JB_BAD : JB_NETWORK;
    goto done;
  }
  run_init(st, c, net);

  // 1. the registration's version
  if((code=recover1(st, version, res))) {
    res->code = code;
    res->guesses_left = code == JB_NO_GUESSES ? 0 : -1;
    goto done;
  }

  // 2. the PIN hashed, and the OPRF's blinded input (the guess the realms count)
  if(xcrypt_pin_hash(pin, pin_len, version, NULL, 0, access_key, seed)) {
    res->code = JB_BAD;
    goto done;
  }
  xcrypt_scalar_random(r);
  if(xcrypt_oprf_blind(access_key, sizeof access_key, r, u)) {
    res->code = JB_BAD;
    goto done;
  }
  o = (xcrypt_cbor_out_t){req, 0, sizeof req, 0};
  xcrypt_cbor_map(&o, 1);
  xcrypt_cbor_text(&o, "Recover2");
  xcrypt_cbor_map(&o, 2);
  xcrypt_cbor_text(&o, "version");
  xcrypt_cbor_bytes(&o, version, VERSION_LEN);
  xcrypt_cbor_text(&o, "oprf_blinded_input");
  xcrypt_cbor_bytes(&o, u, sizeof u);
  st->worst = st->errors = 0;
  open = 0;
  for(int i=0; i < c->count; i++) {
    open += st->ok[i];
  }
  for(int i=0; i < c->count && !o.full; i++) {
    if(!st->ok[i]) {
      continue;
    }
    if((code=request(&st->run, i, req, o.len, 1, out, &olen))) {
      failed(st, code, -1);
    } else if(step_answer(out, olen, "Recover2", &data)) {
      failed(st, JB_BAD, -1);
    } else if(xcrypt_cbor_is(data, "Ok", &ok) && ok.p &&
              !xcrypt_cbor_get(ok, "oprf_signed_public_key", &spk) &&
              !xcrypt_cbor_get_bytes(spk, "public_key", &pk, 32, NULL) &&
              !xcrypt_cbor_get_bytes(spk, "verifying_key", &vk, 32, NULL) &&
              !xcrypt_cbor_get_bytes(spk, "signature", &sig, 64, NULL) &&
              !xcrypt_cbor_get_bytes(ok, "oprf_blinded_result", &w, 32, NULL) &&
              !xcrypt_cbor_get(ok, "oprf_proof", &proof) &&
              !xcrypt_cbor_get_bytes(proof, "c", &pc, 32, NULL) &&
              !xcrypt_cbor_get_bytes(proof, "beta_z", &pz, 32, NULL) &&
              !xcrypt_cbor_get_bytes(ok, "unlock_key_commitment", &com, 32, NULL) &&
              !xcrypt_cbor_get_uint(ok, "num_guesses", &num) && !xcrypt_cbor_get_uint(ok, "guess_count", &count)) {
      // the realm's OPRF key is its own, and its result is of that key
      if(xcrypt_oprf_signed(c->realm[i].id, pk, vk, sig) || xcrypt_oprf_check(u, pk, w, pc, pz)) {
        failed(st, JB_BAD, -1);
      } else {
        s2[n2].index = (uint32_t)i + 1;
        memcpy(s2[n2].point, w, 32);
        memcpy(s2[n2].commitment, com, 32);
        memcpy(s2[n2].verifying_key, vk, 32);
        s2[n2].guesses = num > count ? (int)(num - count) : 0;
        n2++;
      }
    } else if(xcrypt_cbor_is(data, "NotRegistered", NULL)) {
      failed(st, JB_NOT_REGISTERED, -1);
    } else if(xcrypt_cbor_is(data, "NoGuesses", NULL)) {
      failed(st, JB_NO_GUESSES, 0);
    } else {
      failed(st, JB_BAD, -1); // VersionMismatch, or not one
    }
    if(st->errors > open - c->recover_threshold) {
      res->step = 2;
      res->code = st->worst;
      res->guesses_left = st->worst == JB_NO_GUESSES ? 0 : -1;
      goto done;
    }
  }
  res->step = 2;
  res->realms_ok = n2;
  // the realms that agree on the commitment and the signing key
  best = -1;
  agree = 0;
  for(int i=0; i < n2; i++) {
    int n = 0;
    for(int j=0; j < n2; j++) {
      n += !memcmp(s2[i].commitment, s2[j].commitment, 32) && !memcmp(s2[i].verifying_key, s2[j].verifying_key, 32);
    }
    if(n > agree) {
      agree = n;
      best = i;
    }
  }
  if(best < 0 || agree < c->recover_threshold || !(points=mem_calloc(JB_REALMS, 32))) {
    res->code = JB_BAD;
    goto done;
  }
  group = 0;
  least = -1;
  for(int i=0; i < n2; i++) {
    if(!memcmp(s2[i].commitment, s2[best].commitment, 32) && !memcmp(s2[i].verifying_key, s2[best].verifying_key, 32)) {
      index[group] = s2[i].index;
      memcpy(points[group++], s2[i].point, 32);
      least = least < 0 || s2[i].guesses < least ? s2[i].guesses : least;
    }
  }
  if(xcrypt_combine_points(index, (const uint8_t (*)[32])points, group, combined) ||
     xcrypt_oprf_finish(access_key, sizeof access_key, r, combined, oprf)) {
    res->code = JB_BAD;
    goto done;
  }
  xcrypt_unlock_key(oprf, commitment, unlock_key);
  if(memcmp(commitment, s2[best].commitment, 32)) {
    // a wrong PIN: one guess counted at each realm
    res->code = JB_WRONG_PIN;
    res->guesses_left = least;
    goto done;
  }

  // 3. the shares of the secret, for the unlock key's tag
  st->worst = st->errors = 0;
  for(int i=0; i < c->count; i++) {
    uint8_t tag[16], mine[16];
    if(!st->ok[i]) {
      continue;
    }
    xcrypt_unlock_tag(unlock_key, c->realm[i].id, tag);
    o = (xcrypt_cbor_out_t){req, 0, sizeof req, 0};
    xcrypt_cbor_map(&o, 1);
    xcrypt_cbor_text(&o, "Recover3");
    xcrypt_cbor_map(&o, 2);
    xcrypt_cbor_text(&o, "version");
    xcrypt_cbor_bytes(&o, version, VERSION_LEN);
    xcrypt_cbor_text(&o, "unlock_key_tag");
    xcrypt_cbor_bytes(&o, tag, sizeof tag);
    if(o.full) {
      failed(st, JB_BAD, -1);
    } else if((code=request(&st->run, i, req, o.len, 1, out, &olen))) {
      failed(st, code, -1);
    } else if(step_answer(out, olen, "Recover3", &data)) {
      failed(st, JB_BAD, -1);
    } else if(xcrypt_cbor_is(data, "Ok", &ok) && ok.p &&
              !xcrypt_cbor_get_bytes(ok, "encryption_key_scalar_share", &sh, 32, NULL) &&
              !xcrypt_cbor_get_bytes(ok, "encrypted_secret", &es, 145, NULL) &&
              !xcrypt_cbor_get_bytes(ok, "encrypted_secret_commitment", &esc, 16, NULL)) {
      xcrypt_secret_commitment(unlock_key, c->realm[i].id, sh, es, mine);
      if(!memcmp(mine, esc, sizeof mine)) {
        s3[n3].index = (uint32_t)i + 1;
        memcpy(s3[n3].scalar, sh, 32);
        memcpy(s3[n3].secret, es, 145);
        n3++;
      } // else left out, as the SDK leaves it out
    } else if(xcrypt_cbor_is(data, "BadUnlockKeyTag", &ok)) {
      failed(st, JB_WRONG_PIN, ok.p && !xcrypt_cbor_get_uint(ok, "guesses_remaining", &num) ? (int)num : -1);
    } else if(xcrypt_cbor_is(data, "NotRegistered", NULL)) {
      failed(st, JB_NOT_REGISTERED, -1);
    } else if(xcrypt_cbor_is(data, "NoGuesses", NULL)) {
      failed(st, JB_NO_GUESSES, 0);
    } else {
      failed(st, JB_BAD, -1);
    }
    xcrypt_wipe(tag, sizeof tag);
    if(st->errors > open - c->recover_threshold) {
      res->step = 3;
      res->code = st->worst;
      res->guesses_left = st->worst == JB_NO_GUESSES ? 0 : st->worst == JB_WRONG_PIN ? st->guesses[0] : -1;
      goto done;
    }
  }
  res->step = 3;
  res->realms_ok = n3;
  best = -1;
  agree = 0;
  for(int i=0; i < n3; i++) {
    int n = 0;
    for(int j=0; j < n3; j++) {
      n += !memcmp(s3[i].secret, s3[j].secret, 145);
    }
    if(n > agree) {
      agree = n;
      best = i;
    }
  }
  if(best < 0 || agree < c->recover_threshold) {
    res->code = JB_BAD;
    goto done;
  }
  group = 0;
  for(int i=0; i < n3; i++) {
    if(!memcmp(s3[i].secret, s3[best].secret, 145)) {
      index[group] = s3[i].index;
      memcpy(points[group++], s3[i].scalar, 32);
    }
  }
  if(xcrypt_combine_scalars(index, (const uint8_t (*)[32])points, group, combined) ||
     (*len=xcrypt_secret_open(seed, combined, s3[best].secret, secret)) < 0) {
    *len = 0;
    res->code = JB_BAD;
    goto done;
  }
  res->code = JB_OK;

done:
  xcrypt_wipe(access_key, sizeof access_key);
  xcrypt_wipe(seed, sizeof seed);
  xcrypt_wipe(r, sizeof r);
  xcrypt_wipe(unlock_key, sizeof unlock_key);
  xcrypt_wipe(oprf, sizeof oprf);
  xcrypt_wipe(combined, sizeof combined);
  xcrypt_wipe(out, sizeof out);
  xcrypt_wipe(req, sizeof req);
  if(points) {
    xcrypt_wipe(points, JB_REALMS * 32);
    mem_free(points);
  }
  if(st) {
    xcrypt_wipe(st, sizeof *st);
    mem_free(st);
  }
  if(s2) {
    xcrypt_wipe(s2, JB_REALMS * sizeof *s2);
    mem_free(s2);
  }
  if(s3) {
    xcrypt_wipe(s3, JB_REALMS * sizeof *s3);
    mem_free(s3);
  }
}


const char *
jb_code_name(int code) {
  static const char *names[] = {"ok", "wrong PIN", "no guesses left", "not registered", "token refused",
                                "upgrade required", "rate limited", "network", "bad answer"};
  return code >= 0 && code < (int)(sizeof names / sizeof names[0]) ? names[code] : "?";
}
