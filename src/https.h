/* HTTPS GET and POST for the payloads and the title, through libSceHttp with the
certificates of tls.h. The PS5 gives payloads no root certificates and its
verifier does not follow cross-signatures, so the roots and the Let's Encrypt
chain are loaded as PEM (see tls.h). A certificate the library could not
verify is never accepted. */

#pragma once

#include <stddef.h>


typedef struct https_header {
  const char *name;
  const char *value;
} https_header_t;

typedef struct https_response {
  int    status;
  char  *body; // ends with a NUL; mem_free() it
  size_t len;
} https_response_t;


/**
 * set up libSceNet, libSceSsl and libSceHttp for requests sent as agent;
 * log gets a line for each step that fails (codes only, never content);
 * returns 0 or a negative SCE error
 **/
int https_init(const char *agent, void (*log)(const char *line));
void https_fini(void);

/**
 * GET url with the given headers; on 0, *res holds the status and the body
 **/
int https_get(const char *url, const https_header_t *hdr, int count,
              https_response_t *res);

/**
 * https_get for an answer of up to max bytes (a video), not 16 MiB
 **/
int https_get_big(const char *url, const https_header_t *hdr, int count, size_t max,
                  https_response_t *res);

enum {
  HTTPS_STREAM_WAITING, // for the answer's head
  HTTPS_STREAM_READING, // body and len are set, got grows
  HTTPS_STREAM_DONE,    // all of it came
  HTTPS_STREAM_FAILED,  // see error; what came before stays in body
};

/**
 * a GET read as it comes (a video played while it arrives): once state is
 * READING (read it with an acquire load), body has room for the answer's
 * len bytes, of which got (an acquire load too) came
 **/
typedef struct https_stream {
  int                 state;  // HTTPS_STREAM_*
  int                 status; // the HTTP status, once the head came
  int                 error;  // on FAILED: the SCE error, or -1 (not 200, too long, no length, no memory)
  char               *body;   // mem_free() it once https_get_stream returned
  size_t              len;
  size_t              got;
  const volatile int *stop;   // set to give up
} https_stream_t;

/**
 * GET url into st as above, for an answer of up to max bytes with its
 * length in its head, asked for as stored (Accept-Encoding: identity; an
 * encoded answer is refused); returns once all of it came (0), or it
 * failed or was stopped
 **/
int https_get_stream(const char *url, size_t max, https_stream_t *st);

/**
 * POST len bytes of data (a picture or a part of a video) to url
 **/
int https_post_data(const char *url, const https_header_t *hdr, int count, const void *data,
                    size_t len, https_response_t *res);

/**
 * POST data (a NUL-terminated text) to url, like https_get
 **/
int https_post(const char *url, const https_header_t *hdr, int count, const char *data,
               https_response_t *res);
