/* The dictation payload's answers (src/dictserve.h, src/dictproto.h), over
a socket pair as the title would ask on 127.0.0.1: the words of whisper.cpp's
sample, the model kept for the next request, the requests it refuses, a
model not there, and a request the title stops by closing its end. The log
must get counts and codes, never the words. dictserve_test <model>
<sample.wav>; with no model there, only what needs none is checked. */

#define _POSIX_C_SOURCE 200809L // clock_gettime

#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "dictproto.h"
#include "dictserve.h"
#include "wav.h"


static int failures;

#define CHECK(cond) do { \
    if(!(cond)) { \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while(0)


static char            g_log[8192];
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static void
log_keep(const char *fmt, ...) {
  size_t n;
  va_list ap;

  pthread_mutex_lock(&g_log_lock);
  n = strlen(g_log);
  va_start(ap, fmt);
  vsnprintf(g_log + n, sizeof g_log - n, fmt, ap);
  va_end(ap);
  n = strlen(g_log);
  if(n + 1 < sizeof g_log) {
    g_log[n] = '\n';
    g_log[n + 1] = 0;
  }
  pthread_mutex_unlock(&g_log_lock);
}


typedef struct server {
  int         fd;
  dictate_t **d;
  const char *path;
  int         result;
} server_t;

static void *
server_main(void *arg) {
  server_t *s = arg;

  s->result = dictserve_answer(s->fd, s->d, s->path, 4, log_keep);
  close(s->fd);
  return NULL;
}


static int
write_all(int fd, const void *buf, size_t len) {
  size_t off = 0;
  ssize_t n;

  while(off < len && (n=write(fd, (const char *)buf + off, len - off)) > 0) {
    off += (size_t)n;
  }
  return off == len ? 0 : -1;
}


static size_t
read_all(int fd, void *buf, size_t len) {
  size_t got = 0;
  ssize_t n;

  while(got < len && (n=read(fd, (char *)buf + got, len - got)) > 0) {
    got += (size_t)n;
  }
  return got;
}


/**
 * the head of a request: magic, version, lang and how many samples
 **/
static void
head(unsigned char *h, const char *magic, uint32_t version, const char *lang, uint32_t n) {
  memset(h, 0, DICT_HEAD);
  memcpy(h, magic, 8);
  memcpy(h + 8, &version, 4);
  strncpy((char *)h + 12, lang, DICT_LANG);
  memcpy(h + 12 + DICT_LANG, &n, 4);
}


typedef struct reply {
  int      got;     // the answer's head came
  uint32_t version;
  int32_t  status;
  char     text[DICT_TEXT_MAX + 1];
  int      result;  // dictserve_answer's
  double   seconds; // the whole of it took
} reply_t;

/**
 * one request to a server on a socket pair: its head, then send samples of
 * pcm (fewer than the head says when the title gives up early); close_early
 * closes the title's end once they are sent, without waiting for the words
 **/
static void
ask(dictate_t **d, const char *path, const unsigned char *h, const int16_t *pcm, size_t send, int close_early,
    reply_t *r) {
  unsigned char a[DICT_ANSWER];
  struct timespec t0, t1;
  server_t s = {.d = d, .path = path};
  pthread_t thread;
  uint32_t len;
  int sv[2];

  memset(r, 0, sizeof *r);
  clock_gettime(CLOCK_MONOTONIC, &t0);
  if(socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) {
    CHECK(!"socketpair");
    return;
  }
  s.fd = sv[1];
  if(pthread_create(&thread, NULL, server_main, &s)) {
    CHECK(!"pthread_create");
    close(sv[0]);
    close(sv[1]);
    return;
  }
  if(!write_all(sv[0], h, DICT_HEAD) && send) {
    write_all(sv[0], pcm, send * sizeof *pcm);
  }
  if(close_early) {
    close(sv[0]);
  } else {
    if(read_all(sv[0], a, sizeof a) == sizeof a) {
      r->got = 1;
      memcpy(&r->version, a, 4);
      memcpy(&r->status, a + 4, 4);
      memcpy(&len, a + 8, 4);
      CHECK(len <= DICT_TEXT_MAX);
      if(len <= DICT_TEXT_MAX) {
        CHECK(read_all(sv[0], r->text, len) == len);
        r->text[len] = 0;
      }
    }
    close(sv[0]);
  }
  pthread_join(thread, NULL);
  r->result = s.result;
  clock_gettime(CLOCK_MONOTONIC, &t1);
  r->seconds = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
}


int
main(int argc, char **argv) {
  unsigned char h[DICT_HEAD];
  char flat[DICT_TEXT_MAX + 1];
  dictate_t *d = NULL, *none = NULL;
  int16_t *samples = NULL, one[DICT_RATE] = {0};
  size_t count = 0;
  double whole = 0;
  reply_t r;
  FILE *f;

  if(argc < 3) {
    fprintf(stderr, "dictserve_test <model> <sample.wav>\n");
    return 2;
  }
  signal(SIGPIPE, SIG_IGN); // as the payload: an answer to a closed end is not a death

  // what is not a request: refused, nothing opened
  head(h, "XPS5DICX", DICT_VERSION, "en", DICT_RATE);
  ask(&d, argv[1], h, NULL, 0, 0, &r);
  CHECK(r.got && r.status == DICT_BAD && r.version == DICT_VERSION && !r.text[0] && !d);
  head(h, DICT_MAGIC, DICT_VERSION + 1, "en", DICT_RATE);
  ask(&d, argv[1], h, NULL, 0, 0, &r);
  CHECK(r.got && r.status == DICT_OLD && r.result == DICT_OLD && !d);
  head(h, DICT_MAGIC, DICT_VERSION, "en", 0);
  ask(&d, argv[1], h, NULL, 0, 0, &r);
  CHECK(r.got && r.status == DICT_BAD && !d);
  head(h, DICT_MAGIC, DICT_VERSION, "en", DICT_MAX_SAMPLES + 1);
  ask(&d, argv[1], h, NULL, 0, 0, &r);
  CHECK(r.got && r.status == DICT_BAD && !d);

  // the sound cut short: no answer, nothing opened
  head(h, DICT_MAGIC, DICT_VERSION, "en", DICT_RATE);
  ask(&d, argv[1], h, one, DICT_RATE / 2, 1, &r);
  CHECK(!r.got && r.result == -100 && !d);

  // no model there
  ask(&none, "/no/such/model.bin", h, one, DICT_RATE, 0, &r);
  CHECK(r.got && r.status == DICT_NO_MODEL && !none);

  if(!(f=fopen(argv[1], "rb"))) {
    printf("dictserve_test: no model at %s, the words skipped\n", argv[1]);
    goto done;
  }
  fclose(f);
  if(!(samples=read_wav(argv[2], &count)) || count > DICT_MAX_SAMPLES) {
    fprintf(stderr, "%s is not 16 kHz mono 16-bit PCM of 30 s at most\n", argv[2]);
    return 1;
  }

  // the words, in English; the model kept for the next
  head(h, DICT_MAGIC, DICT_VERSION, "en", (uint32_t)count);
  ask(&d, argv[1], h, samples, count, 0, &r);
  CHECK(r.got && r.status == DICT_OK && r.result == DICT_OK && d != NULL);
  plain(r.text, flat, sizeof flat);
  CHECK(strstr(flat, "ask not what your country can do for you") != NULL);
  whole = r.seconds;
  {
    dictate_t *kept = d;
    // guessed, and a language name that is not one: guessed too
    head(h, DICT_MAGIC, DICT_VERSION, "", (uint32_t)count);
    ask(&d, argv[1], h, samples, count, 0, &r);
    CHECK(r.got && r.status == DICT_OK && d == kept);
    plain(r.text, flat, sizeof flat);
    CHECK(strstr(flat, "your country") != NULL);
    head(h, DICT_MAGIC, DICT_VERSION, "E\"n;", (uint32_t)count);
    ask(&d, argv[1], h, samples, count, 0, &r);
    CHECK(r.got && r.status == DICT_OK && d == kept);
  }

  // silence: nothing said, or close to nothing
  head(h, DICT_MAGIC, DICT_VERSION, "en", DICT_RATE);
  ask(&d, argv[1], h, one, DICT_RATE, 0, &r);
  CHECK(r.got && (r.status == DICT_NOTHING || (r.status == DICT_OK && strlen(r.text) < 40)));

  // the title closes its end once the sound is sent: the words stop early
  head(h, DICT_MAGIC, DICT_VERSION, "en", (uint32_t)count);
  ask(&d, argv[1], h, samples, count, 1, &r);
  CHECK(r.result == -100);
  CHECK(r.seconds < whole);

  // and the model still works after
  ask(&d, argv[1], h, samples, count, 0, &r);
  CHECK(r.got && r.status == DICT_OK);

  // the log: counts and codes, never the words
  CHECK(strstr(g_log, "country") == NULL && strstr(g_log, "Country") == NULL);
  CHECK(strstr(g_log, "stopped") != NULL);
  dictate_close(d);
  free(samples);

done:
  if(failures) {
    fprintf(stderr, "dictserve_test: %d failed\n%s", failures, g_log);
    return 1;
  }
  printf("all dictation payload checks passed\n");
  return 0;
}
