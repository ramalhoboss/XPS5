/* The controller's microphone, see mic.h. sceAudioInOpen takes the user, a
type, an index, the samples a block, the rate and the format, as the PS4's
libSceAudioIn has them (shadPS4 describes it the same way); which type the
PS5 gives a title is not known, so it tries the ones games use and logs the
answer of each. */

#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mic.h"

// as the sandbox shows the system's libraries, see ime.c
#define MIC_LIBRARY "libSceAudioIn.sprx"

#define MIC_S16_MONO  0
#define MIC_SYSTEM    0xff // the system's user, as the audio ports take it
#define MIC_PEAK_HOLD 60   // blocks the peak stays before it falls
#define MIC_RELEASE   0.9f // how much of the meter stays, each block
#define MIC_FIRST     4    // the blocks whose answer and time are logged
#define MIC_AHEAD     200000 // us ahead of the clock that make it wait

enum { MIC_VOICE_CHAT = 0, MIC_GENERAL = 1, MIC_VOICE_RECOGNITION = 5 };


int sceKernelLoadStartModule(const char *path, size_t args, const void *argp, uint32_t flags,
                             void *opt, int *res);
const char *sceKernelGetFsSandboxRandomWord(void);
uint64_t sceKernelGetProcessTime(void);

// linked from title/stubs, the way games call them, see ime.c
int sceAudioInOpen(int user, uint32_t type, uint32_t index, uint32_t len, uint32_t freq, uint32_t param);
int sceAudioInHqOpen(int user, uint32_t type, uint32_t index, uint32_t len, uint32_t freq, uint32_t param);
int sceAudioInInput(int handle, void *dest);
int sceAudioInClose(int handle);
int sceAudioInGetSilentState(int handle);


// the ways to open it, tried in order: the user's own voice chat first
static const struct {
  int hq, type, rate, system;
} g_mic_ways[] = {
  {0, MIC_VOICE_CHAT, 16000, 0},
  {0, MIC_GENERAL, 16000, 0},
  {0, MIC_GENERAL, 48000, 0},
  {0, MIC_VOICE_RECOGNITION, 16000, 0},
  {1, MIC_VOICE_CHAT, 48000, 0},
  {0, MIC_GENERAL, 16000, 1},
};

static pthread_mutex_t g_mic_lock = PTHREAD_MUTEX_INITIALIZER;
static mic_status_t    g_mic;
static int             g_mic_running, g_mic_stop, g_mic_user, g_mic_loaded;
static void          (*g_mic_log)(const char *fmt, ...);
static int16_t        *g_tape;           // a voice note's samples, while it is taped
static size_t          g_tape_room, g_tape_len;
static int             g_tape_sum, g_tape_part; // of a 48 kHz microphone: the samples of the next one


void
mic_measure(const int16_t *samples, int n, float *rms, float *peak) {
  double sum = 0;
  int top = 0;

  for(int i=0; i < n; i++) {
    int v = samples[i];
    sum += (double)v * v;
    v = v < 0 ? -v : v;
    top = v > top ? v : top;
  }
  *rms = n > 0 ? (float)(sqrt(sum / n) / 32768.0) : 0;
  *peak = top / 32768.0f;
}


float
mic_meter(float loudness) {
  float m;

  if(!(loudness > 0)) {
    return 0;
  }
  m = (20 * log10f(loudness) + 60) / 60;
  return m < 0 ? 0 : m > 1 ? 1 : m;
}


/**
 * the library from where the sandbox shows it, else from /system; its
 * handle or the SCE error of the last try
 **/
static int
mic_load(void) {
  char path[160];
  const char *word = sceKernelGetFsSandboxRandomWord();
  int handle = -1;

  if(word && *word) {
    snprintf(path, sizeof path, "/%s/common/lib/" MIC_LIBRARY, word);
    handle = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, NULL);
    g_mic_log("mic: library from the sandbox 0x%08x", (unsigned)handle);
  }
  if(handle < 0) {
    handle = sceKernelLoadStartModule("/system/common/lib/" MIC_LIBRARY, 0, NULL, 0, NULL, NULL);
    g_mic_log("mic: library from /system 0x%08x", (unsigned)handle);
  }
  return handle;
}


static int
mic_stopping(void) {
  int stop;

  pthread_mutex_lock(&g_mic_lock);
  stop = g_mic_stop;
  pthread_mutex_unlock(&g_mic_lock);
  return stop;
}


/**
 * a port on the first way that opens; its handle and rate, or the error
 * of the last way
 **/
static int
mic_open(int *rate) {
  int handle = -1;

  for(size_t i=0; i < sizeof g_mic_ways / sizeof g_mic_ways[0] && !mic_stopping(); i++) {
    int user = g_mic_ways[i].system ? MIC_SYSTEM : g_mic_user;
    if(g_mic_ways[i].hq) {
      handle = sceAudioInHqOpen(user, g_mic_ways[i].type, 0, MIC_GRAIN, g_mic_ways[i].rate, MIC_S16_MONO);
    } else {
      handle = sceAudioInOpen(user, g_mic_ways[i].type, 0, MIC_GRAIN, g_mic_ways[i].rate, MIC_S16_MONO);
    }
    g_mic_log("mic: %s type %d at %d Hz for the %s user: 0x%08x", g_mic_ways[i].hq ? "hq open" : "open",
              g_mic_ways[i].type, g_mic_ways[i].rate, g_mic_ways[i].system ? "system's" : "signed-in",
              (unsigned)handle);
    if(handle >= 0) {
      *rate = g_mic_ways[i].rate;
      return handle;
    }
  }
  return handle;
}


/**
 * a block onto the tape, while there is one: a 48 kHz microphone's samples
 * three by three, averaged
 **/
static void
tape_block(const int16_t *block, int n, int rate) {
  pthread_mutex_lock(&g_mic_lock);
  for(int i=0; g_tape && i < n && g_tape_len < g_tape_room; i++) {
    if(rate != 3 * MIC_TAPE_RATE) {
      g_tape[g_tape_len++] = block[i];
      continue;
    }
    g_tape_sum += block[i];
    if(++g_tape_part == 3) {
      g_tape[g_tape_len++] = (int16_t)(g_tape_sum / 3);
      g_tape_sum = g_tape_part = 0;
    }
  }
  pthread_mutex_unlock(&g_mic_lock);
}


void
mic_tape(int16_t *tape, size_t room) {
  pthread_mutex_lock(&g_mic_lock);
  g_tape = tape;
  g_tape_room = room;
  g_tape_len = 0;
  g_tape_sum = g_tape_part = 0;
  pthread_mutex_unlock(&g_mic_lock);
}


size_t
mic_taped(void) {
  size_t n;

  pthread_mutex_lock(&g_mic_lock);
  n = g_tape_len;
  pthread_mutex_unlock(&g_mic_lock);
  return n;
}


size_t
mic_tape_end(void) {
  size_t n;

  pthread_mutex_lock(&g_mic_lock);
  n = g_tape_len;
  g_tape = NULL;
  g_tape_room = g_tape_len = 0;
  pthread_mutex_unlock(&g_mic_lock);
  return n;
}


static void *
mic_main(void *arg) {
  int16_t block[MIC_GRAIN];
  float rms, top, level = 0, peak = 0, loudest = 0;
  int handle, rate = 0, ret, silent = 0, held = 0, failed = 0;
  unsigned blocks = 0;
  uint64_t start, before, now, block_us;

  (void)arg;
  if(!g_mic_loaded) {
    mic_load(); // linked functions may answer anyway; the opens say
    g_mic_loaded = 1;
  }
  if((handle=mic_open(&rate)) < 0) {
    pthread_mutex_lock(&g_mic_lock);
    g_mic.state = g_mic_stop ? MIC_OFF : MIC_FAILED;
    g_mic.code = handle;
    g_mic_running = 0;
    pthread_mutex_unlock(&g_mic_lock);
    return NULL;
  }
  pthread_mutex_lock(&g_mic_lock);
  g_mic.state = MIC_ON;
  g_mic.rate = rate;
  pthread_mutex_unlock(&g_mic_lock);

  block_us = 1000000ull * MIC_GRAIN / (unsigned)rate;
  start = sceKernelGetProcessTime();
  while(!mic_stopping()) {
    before = sceKernelGetProcessTime();
    if((ret=sceAudioInInput(handle, block)) < 0) {
      g_mic_log("mic: block %u did not come: 0x%08x", blocks, (unsigned)ret);
      failed = ret;
      break;
    }
    now = sceKernelGetProcessTime();
    if(blocks < MIC_FIRST) {
      g_mic_log("mic: block %u gave %d after %llu us", blocks, ret, (unsigned long long)(now - before));
    }
    blocks++;
    // a read that does not wait would turn here without end
    if(now - start + MIC_AHEAD < blocks * block_us) {
      usleep((useconds_t)block_us);
    }

    if(rate == MIC_TAPE_RATE || rate == 3 * MIC_TAPE_RATE) {
      tape_block(block, MIC_GRAIN, rate);
    }
    mic_measure(block, MIC_GRAIN, &rms, &top);
    loudest = rms > loudest ? rms : loudest;
    rms = mic_meter(rms);
    top = mic_meter(top);
    level = rms > level ? rms : level * MIC_RELEASE;
    if(top >= peak) {
      peak = top;
      held = MIC_PEAK_HOLD;
    } else if(held) {
      held--;
    } else {
      peak *= MIC_RELEASE;
    }
    if(blocks % 30 == 1 && (ret=sceAudioInGetSilentState(handle)) >= 0 && ret != silent) {
      g_mic_log("mic: silent state 0x%x after %u blocks", (unsigned)ret, blocks);
      silent = ret;
    }

    pthread_mutex_lock(&g_mic_lock);
    g_mic.level = level;
    g_mic.peak = peak;
    g_mic.silent = silent;
    g_mic.blocks = blocks;
    pthread_mutex_unlock(&g_mic_lock);
  }
  ret = sceAudioInClose(handle);
  g_mic_log("mic: closed (0x%08x) after %u blocks in %llu ms, loudest %.1f dBFS", (unsigned)ret, blocks,
            (unsigned long long)((sceKernelGetProcessTime() - start) / 1000),
            loudest > 0 ? 20 * log10f(loudest) : -99.0f);

  pthread_mutex_lock(&g_mic_lock);
  g_mic.state = failed && !g_mic_stop ? MIC_FAILED : MIC_OFF;
  g_mic.code = failed;
  g_mic.level = g_mic.peak = 0;
  g_mic_running = 0;
  pthread_mutex_unlock(&g_mic_lock);
  return NULL;
}


static void
mic_no_log(const char *fmt, ...) {
  (void)fmt;
}


int
mic_start(int user, void (*log)(const char *fmt, ...)) {
  pthread_attr_t attr;
  pthread_t thread;
  int ok;

  pthread_mutex_lock(&g_mic_lock);
  if(g_mic_running) {
    pthread_mutex_unlock(&g_mic_lock);
    return -1;
  }
  g_mic_running = 1;
  g_mic_stop = 0;
  g_mic_user = user;
  g_mic_log = log ? log : mic_no_log;
  memset(&g_mic, 0, sizeof g_mic);
  g_mic.state = MIC_OPENING;
  pthread_mutex_unlock(&g_mic_lock);

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 64 << 10);
  ok = !pthread_create(&thread, &attr, mic_main, NULL);
  if(ok) {
    pthread_detach(thread);
  }
  pthread_attr_destroy(&attr);
  if(!ok) {
    g_mic_log("mic: no thread");
    pthread_mutex_lock(&g_mic_lock);
    g_mic.state = MIC_FAILED;
    g_mic.code = -1;
    g_mic_running = 0;
    pthread_mutex_unlock(&g_mic_lock);
  }
  return 0;
}


void
mic_stop(void) {
  pthread_mutex_lock(&g_mic_lock);
  if(g_mic_running) {
    g_mic_stop = 1;
  } else if(g_mic.state == MIC_FAILED) {
    g_mic.state = MIC_OFF;
  }
  pthread_mutex_unlock(&g_mic_lock);
}


void
mic_get(mic_status_t *status) {
  pthread_mutex_lock(&g_mic_lock);
  *status = g_mic;
  pthread_mutex_unlock(&g_mic_lock);
}
