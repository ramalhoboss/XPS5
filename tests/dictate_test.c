/* Dictation without the internet (src/dictate.h): whisper.cpp with the tiny
model on the sample whisper.cpp comes with (11 s of John F. Kennedy's speech,
16 kHz mono), and the edges: no model, no sound, too much sound, and a run
stopped before it starts. dictate_test <model> <sample.wav>; with no model
there, it says so and passes, as the model is a download of its own. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dictate.h"
#include "wav.h"


static int failures;

#define CHECK(cond) do { \
    if(!(cond)) { \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while(0)


int
main(int argc, char **argv) {
  char text[1024], flat[1024];
  volatile int stop = 1;
  int16_t *samples, silence[DICTATE_RATE] = {0};
  size_t count = 0;
  dictate_t *d;
  FILE *f;

  if(argc < 3) {
    fprintf(stderr, "dictate_test <model> <sample.wav>\n");
    return 2;
  }
  CHECK(!dictate_open("/no/such/model.bin", 4));
  if(!(f=fopen(argv[1], "rb"))) {
    printf("dictate_test: no model at %s, skipped\n", argv[1]);
    return 0;
  }
  fclose(f);
  if(!(samples=read_wav(argv[2], &count))) {
    fprintf(stderr, "%s is not 16 kHz mono 16-bit PCM\n", argv[2]);
    return 1;
  }
  CHECK(count > (size_t)DICTATE_RATE * 10 && count < (size_t)DICTATE_RATE * 12);

  d = dictate_open(argv[1], 4);
  CHECK(d != NULL);
  if(d) {
    // what he said, in English
    CHECK(dictate_run(d, samples, count, "en", text, sizeof text, NULL) == 0);
    plain(text, flat, sizeof flat);
    CHECK(strstr(flat, "ask not what your country can do for you") != NULL);
    CHECK(strstr(flat, "what you can do for your country") != NULL);
    CHECK(text[0] != ' ' && text[strlen(text) - 1] != ' ');

    // the language guessed
    CHECK(dictate_run(d, samples, count, NULL, text, sizeof text, NULL) == 0);
    plain(text, flat, sizeof flat);
    CHECK(strstr(flat, "your country") != NULL);

    // a short buffer: cut, ended
    CHECK(dictate_run(d, samples, count, "en", text, 12, NULL) == 0);
    CHECK(strlen(text) <= 11);

    // a second of silence says nothing (or close to nothing)
    {
      int r = dictate_run(d, silence, DICTATE_RATE, "en", text, sizeof text, NULL);
      CHECK(r == 1 || (r == 0 && strlen(text) < 40));
    }

    // the edges
    CHECK(dictate_run(d, samples, 0, "en", text, sizeof text, NULL) == -1);
    CHECK(dictate_run(d, samples, (size_t)DICTATE_RATE * DICTATE_MAX_S + 1, "en", text, sizeof text, NULL) == -1);
    CHECK(dictate_run(d, samples, count, "en", text, sizeof text, &stop) == -1);
    CHECK(text[0] == 0);
    CHECK(dictate_run(NULL, samples, count, "en", text, sizeof text, NULL) == -1);
    dictate_close(d);
  }
  dictate_close(NULL);
  free(samples);

  if(failures) {
    fprintf(stderr, "dictate_test: %d failed\n", failures);
    return 1;
  }
  printf("all dictation checks passed\n");
  return 0;
}
