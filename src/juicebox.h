/* The recovery of the user's X Chat keys from Juicebox's realms with their
PIN, as the Juicebox SDK 0.3.7 (MIT,
juicebox-systems/juicebox-sdk) does it: Recover1 for the registration's
version, Recover2 for the OPRF (the guess the realms count), Recover3 for
the shares of the secret. Software realms are spoken to in CBOR with a
bearer token, hardware realms through Noise NK. It only recovers: it never
registers, changes or deletes anything. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "json.h"
#include "xcrypt.h"

#define JB_VERSION   "0.3.7"  // the SDK's, sent in X-Juicebox-Version
#define JB_REALMS    8
#define JB_SECRET    128      // bytes of a secret at most

typedef struct jb_realm {
  uint8_t id[16];
  char    url[256];           // its address with "req"
  int     hardware;           // a public key: Noise
  uint8_t public_key[32];
  char   *token;              // its auth token from X (mem in the config)
} jb_realm_t;

typedef struct jb_config {
  jb_realm_t realm[JB_REALMS]; // sorted by id: a share's index is its place, from 1
  int        count;
  int        recover_threshold;
  int        max_guesses;      // X's max_guess_count
  char      *tokens;           // where the tokens are kept
} jb_config_t;

/**
 * the configuration from X's juicebox_config (key_store_token_map_json,
 * max_guess_count, token_map); 0, or -1 when it is not one this reads
 * (Standard2019 only); jb_config_free() it
 **/
int jb_config_read(const json_node_t *juicebox_config, jb_config_t *c);
void jb_config_free(jb_config_t *c);

/**
 * how a request goes: POST body to url (with Authorization: Bearer bearer
 * when bearer is not NULL, and X-Juicebox-Version); the HTTP status and the
 * answer, which release() gives back; 0, or -1 when nothing came
 **/
typedef struct jb_net {
  int  (*post)(void *ctx, const char *url, const char *bearer, const uint8_t *body, size_t len,
               int *status, uint8_t **answer, size_t *answer_len);
  void (*release)(void *ctx, uint8_t *answer);
  void  *ctx;
} jb_net_t;

enum {
  JB_OK,
  JB_WRONG_PIN,      // guesses_left set
  JB_NO_GUESSES,     // the realms keep the secret locked: only a reset in X's apps
  JB_NOT_REGISTERED, // no X Chat keys kept with this configuration
  JB_REFUSED,        // a realm refused its token (401)
  JB_UPGRADE,        // a realm wants a newer client (426)
  JB_LIMITED,        // too many requests (429)
  JB_NETWORK,        // no answer, or one that is not one
  JB_BAD,            // an answer that does not hold together
};

typedef struct jb_result {
  int code;          // JB_*
  int guesses_left;  // after a wrong PIN, or -1
  int realms_ok;     // realms that answered the last step
  int step;          // 1 to 3: the last step tried
} jb_result_t;

/**
 * whether the keys are registered, Recover1 only (no guess is counted)
 **/
void jb_check(const jb_config_t *c, const jb_net_t *net, jb_result_t *res);

/**
 * the secret (at most JB_SECRET bytes, *len set) with the PIN of pin_len
 * bytes; a wrong PIN counts one guess at the realms
 **/
void jb_recover(const jb_config_t *c, const jb_net_t *net, const void *pin, size_t pin_len,
                uint8_t secret[JB_SECRET], int *len, jb_result_t *res);

/**
 * a name of a JB_* code, for the log
 **/
const char *jb_code_name(int code);
