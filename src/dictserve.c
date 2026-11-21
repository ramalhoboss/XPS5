/* The dictation payload's side of src/dictproto.h (see src/dictserve.h).
The sound is wiped once its words are made, and the words once they are
sent; the log gets how many samples, the status and how long it took. */

#define _POSIX_C_SOURCE 200809L // clock_gettime

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dictproto.h"
#include "dictserve.h"

_Static_assert(DICT_RATE == DICTATE_RATE && DICT_MAX_S == DICTATE_MAX_S, "dictproto.h and dictate.h agree");


/**
 * all of len bytes from c into buf; 0, or -1 when it ended or broke first
 **/
static int
read_all(int c, void *buf, size_t len) {
  size_t got = 0;
  ssize_t n;

  while(got < len) {
    if((n=read(c, (char *)buf + got, len - got)) > 0) {
      got += (size_t)n;
    } else if(n < 0 && errno == EINTR) {
      continue;
    } else {
      return -1;
    }
  }
  return 0;
}


static int
write_all(int c, const void *buf, size_t len) {
  size_t off = 0;
  ssize_t n;

  while(off < len) {
    if((n=write(c, (const char *)buf + off, len - off)) > 0) {
      off += (size_t)n;
    } else if(n < 0 && errno == EINTR) {
      continue;
    } else {
      return -1;
    }
  }
  return 0;
}


/**
 * the answer: this payload's version, the status, and the text (len bytes)
 **/
static int
answer(int c, int status, const char *text, size_t len) {
  unsigned char head[DICT_ANSWER];
  const uint32_t version = DICT_VERSION, n = (uint32_t)len;
  const int32_t s = status;

  memcpy(head, &version, 4);
  memcpy(head + 4, &s, 4);
  memcpy(head + 8, &n, 4);
  return write_all(c, head, sizeof head) || (len && write_all(c, text, len)) ? -100 : status;
}


/*
 * While the words are made, the title closing its end (or sending anything
 * more) stops them: a thread looks at the connection for it.
 */
typedef struct watch {
  int          c;
  volatile int stop, done;
} watch_t;

static void *
watch_main(void *arg) {
  watch_t *w = arg;
  struct pollfd p;

  while(!w->done) {
    p.fd = w->c;
    p.events = POLLIN;
    p.revents = 0;
    if(poll(&p, 1, 50) > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR))) {
      w->stop = 1;
      break;
    }
  }
  return NULL;
}


static long long
now_ms(void) {
  struct timespec t;

  clock_gettime(CLOCK_MONOTONIC, &t);
  return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}


int
dictserve_answer(int c, dictate_t **d, const char *path, int threads, void (*log)(const char *fmt, ...)) {
  unsigned char head[DICT_HEAD];
  char lang[DICT_LANG], *text = NULL;
  int16_t *pcm = NULL;
  uint32_t version, n;
  long long began = now_ms();
  watch_t w = {.c = c};
  pthread_t watcher;
  int status, watching = 0;
  size_t len;

  if(read_all(c, head, sizeof head) || memcmp(head, DICT_MAGIC, 8)) {
    log("dictate: not a request");
    return answer(c, DICT_BAD, NULL, 0);
  }
  memcpy(&version, head + 8, 4);
  if(version != DICT_VERSION) {
    log("dictate: the title is of version %u, this %u", (unsigned)version, DICT_VERSION);
    return answer(c, DICT_OLD, NULL, 0);
  }
  // the language: small letters only, as Whisper names them; "" to guess
  memset(lang, 0, sizeof lang);
  for(size_t i=0; i < DICT_LANG - 1 && head[12 + i] >= 'a' && head[12 + i] <= 'z'; i++) {
    lang[i] = (char)head[12 + i];
  }
  memcpy(&n, head + 12 + DICT_LANG, 4);
  if(!n || n > DICT_MAX_SAMPLES) {
    log("dictate: %u samples", (unsigned)n);
    return answer(c, DICT_BAD, NULL, 0);
  }
  if(!(pcm=malloc((size_t)n * sizeof *pcm)) || !(text=malloc(DICT_TEXT_MAX + 1))) {
    free(pcm);
    log("dictate: no memory for %u samples", (unsigned)n);
    return answer(c, DICT_FAILED, NULL, 0);
  }
  if(read_all(c, pcm, (size_t)n * sizeof *pcm)) {
    memset(pcm, 0, (size_t)n * sizeof *pcm);
    free(pcm);
    free(text);
    log("dictate: the sound did not all come");
    return -100;
  }
  // each step, so the klog shows where one that never answers stopped
  log("dictate: %u samples came, the model %s", (unsigned)n, *d ? "read already" : "to read");

  if(!*d && !(*d=dictate_open(path, threads))) {
    status = access(path, R_OK) ? DICT_NO_MODEL : DICT_FAILED;
    log("dictate: the model does not open (%s)", status == DICT_NO_MODEL ? "not there" : "not a model");
  } else {
    log("dictate: the model ready after %lld ms, %d threads", now_ms() - began, threads);
    if(!(watching=!pthread_create(&watcher, NULL, watch_main, &w))) {
      log("dictate: no watcher, it runs to its end");
    }
    status = dictate_run(*d, pcm, n, lang[0] ? lang : NULL, text, DICT_TEXT_MAX + 1, &w.stop);
    w.done = 1;
    if(watching) {
      pthread_join(watcher, NULL);
    }
    status = status == 0 ? DICT_OK : status == 1 ? DICT_NOTHING : DICT_FAILED;
  }
  memset(pcm, 0, (size_t)n * sizeof *pcm);
  free(pcm);

  len = status == DICT_OK ? strlen(text) : 0;
  log("dictate: %u samples, status %d, %zu bytes, %lld ms%s", (unsigned)n, status, len, now_ms() - began,
      w.stop ? ", stopped" : "");
  status = w.stop ? -100 : answer(c, status, text, len);
  memset(text, 0, DICT_TEXT_MAX + 1);
  free(text);
  return status;
}
