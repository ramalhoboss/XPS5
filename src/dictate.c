/* Dictation without the internet (dictate.h): whisper.cpp's C API, with the
model read through a loader of its own (plain stdio, no C++ streams) and the
sound turned into the floats Whisper takes. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "whisper.h"

#include "dictate.h"


struct dictate {
  struct whisper_context *ctx;
  int threads;
};


/**
 * whisper.cpp's and ggml's messages, which may name what was heard: dropped
 **/
static void
quiet(enum ggml_log_level level, const char *text, void *data) {
  (void)level;
  (void)text;
  (void)data;
}


static size_t
model_read(void *ctx, void *out, size_t size) {
  return fread(out, 1, size, (FILE *)ctx);
}

static bool
model_eof(void *ctx) {
  return feof((FILE *)ctx) != 0;
}

static void
model_close(void *ctx) {
  fclose((FILE *)ctx);
}


dictate_t *
dictate_open(const char *path, int threads) {
  struct whisper_context_params params;
  whisper_model_loader loader;
  dictate_t *d;
  FILE *f;

  whisper_log_set(quiet, NULL);
  if(!path || !(d=calloc(1, sizeof *d))) {
    return NULL;
  }
  if(!(f=fopen(path, "rb"))) {
    free(d);
    return NULL;
  }
  loader.context = f;
  loader.read = model_read;
  loader.eof = model_eof;
  loader.close = model_close; // whisper.cpp closes it once the model is read
  params = whisper_context_default_params();
  params.use_gpu = false;
  params.flash_attn = false;
  if(!(d->ctx=whisper_init_with_params(&loader, params))) {
    free(d);
    return NULL;
  }
  d->threads = threads < 1 ? 1 : threads > 8 ? 8 : threads;
  return d;
}


static bool
should_stop(void *data) {
  const volatile int *stop = data;
  return stop && *stop;
}


/*
 * The languages XPS5 speaks (src/settings.c), the only ones a guess picks
 * from. Whisper's own guess picks among its 99, and on a few seconds of
 * speech a close one (Galician for Portuguese, say) can win.
 */
static const char *const g_known[] = {"en", "pt", "es", "fr", "de", "it", "nl"};


/**
 * which of g_known was spoken in the n samples of pcm, by Whisper's odds
 * for each language; NULL when they could not be had
 **/
static const char *
guess(dictate_t *d, const float *pcm, size_t n) {
  const int most = whisper_lang_max_id() + 1;
  const char *best = NULL;
  float *odds, top = -1;
  int id;

  if(!(odds=calloc((size_t)most, sizeof *odds))) {
    return NULL;
  }
  if(!whisper_pcm_to_mel(d->ctx, pcm, (int)n, d->threads) &&
     whisper_lang_auto_detect(d->ctx, 0, d->threads, odds) >= 0) {
    for(size_t i=0; i < sizeof g_known / sizeof *g_known; i++) {
      if((id=whisper_lang_id(g_known[i])) >= 0 && id < most && odds[id] > top) {
        top = odds[id];
        best = g_known[i];
      }
    }
  }
  free(odds);
  return best;
}


int
dictate_run(dictate_t *d, const int16_t *samples, size_t n, const char *lang, char *text, size_t size,
            const volatile int *stop) {
  struct whisper_full_params params;
  size_t used = 0, len;
  const char *s;
  float *pcm;
  int segments, ret;

  if(!d || !samples || !text || size < 1) {
    return -1;
  }
  text[0] = 0;
  if(!n || n > (size_t)DICTATE_RATE * DICTATE_MAX_S) {
    return -1;
  }
  if(!(pcm=malloc(n * sizeof *pcm))) {
    return -1;
  }
  for(size_t i=0; i < n; i++) {
    pcm[i] = samples[i] / 32768.0f;
  }
  if(!lang || !lang[0]) {
    lang = guess(d, pcm, n); // NULL: Whisper's own guess, below
  }
  params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  params.n_threads = d->threads;
  params.language = lang && lang[0] ? lang : "auto";
  params.detect_language = false;
  params.translate = false;
  params.no_context = true;
  params.no_timestamps = true;
  params.single_segment = false;
  params.print_special = false;
  params.print_progress = false;
  params.print_realtime = false;
  params.print_timestamps = false;
  params.suppress_blank = true;
  params.suppress_nst = true;
  params.abort_callback = should_stop;
  params.abort_callback_user_data = (void *)stop;
  ret = whisper_full(d->ctx, params, pcm, (int)n);
  memset(pcm, 0, n * sizeof *pcm); // the sound does not stay behind
  free(pcm);
  if(ret || should_stop((void *)stop)) {
    return -1;
  }

  // the segments one after the other, without the space Whisper puts first
  segments = whisper_full_n_segments(d->ctx);
  for(int i=0; i < segments; i++) {
    if(!(s=whisper_full_get_segment_text(d->ctx, i))) {
      continue;
    }
    while(*s == ' ') {
      s++;
    }
    len = strlen(s);
    if(!len) {
      continue;
    }
    if(used && used + 1 < size) {
      text[used++] = ' ';
    }
    if(used + len >= size) {
      // as much as fits, not cut inside a character
      len = size - 1 - used;
      while(len && ((unsigned char)s[len] & 0xc0) == 0x80) {
        len--;
      }
    }
    memcpy(text + used, s, len);
    used += len;
    text[used] = 0;
  }
  while(used && text[used - 1] == ' ') {
    text[--used] = 0;
  }
  return used ? 0 : 1;
}


void
dictate_close(dictate_t *d) {
  if(d) {
    whisper_free(d->ctx);
    free(d);
  }
}
