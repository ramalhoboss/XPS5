/* The x-client-transaction-id header that x.com's own pages put on their
requests, without the network. X refuses a post sent without it ("this
request looks like it might be automated", error 226).

A page of x.com carries a key (the twitter-site-verification meta tag) and
four hidden SVG animations; a small script of the page (the ondemand.s
chunk on abs.twimg.com) says which bytes of the key pick a row of an
animation and a moment in it. The header is the key, the time and a hash
of the request with that moment's colour and rotation, masked with a random
byte. This follows the open-source x-client-transaction-id library
(github.com/Lqm1, MIT), which matches what Chromium computes. */

#pragma once

#include <stddef.h>

#define XTID_KEY_MAX     64
#define XTID_INDICES_MAX 16
#define XTID_HEADER_MAX  160

typedef struct xtid {
  unsigned char key[XTID_KEY_MAX];
  int           key_len;
  char          animation[160];
} xtid_t;

/**
 * from the HTML of a page of x.com: the address of its ondemand.s chunk;
 * returns 0, or -1 when the page does not name one
 **/
int xtid_ondemand_url(const char *html, char *url, size_t size);

/**
 * from the ondemand.s chunk: the key byte indices, the row's first;
 * returns how many (at most max), or -1 for none
 **/
int xtid_indices(const char *js, int *indices, int max);

/**
 * read the key and work out the animation, from the page and the indices;
 * returns 0, -1 without a key, -2 without the animation row it needs
 **/
int xtid_init(xtid_t *t, const char *html, const int *indices, int count);

/**
 * the header for a request (method and path, without the query) at the
 * Unix time now, masked with the byte r; returns 0 or -1
 **/
/**
 * the SHA-256 of len bytes of data
 **/
void xtid_sha256(const void *data, size_t len, unsigned char out[32]);

int xtid_make(const xtid_t *t, const char *method, const char *path, long long now,
              unsigned r, char *out, size_t size);
