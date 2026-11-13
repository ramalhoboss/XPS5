/* The pointer's tick (see sound.h). */

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "sound.h"

#define PI        3.14159265f
#define TICK_GAIN 0.16f // about -16 dB under full scale: heard, never over a game
#define RETRY_US  (5 * 1000000ull) // between tries at a port that did not open

int sceAudioOutInit(void);
int sceAudioOutOpen(int user, int type, int index, unsigned len, unsigned freq, unsigned param);
int sceAudioOutOutput(int handle, const void *ptr);
uint64_t sceKernelGetProcessTime(void);

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_wake = PTHREAD_COND_INITIALIZER;
static int             g_started, g_pending;
static float           g_pan;
static float           g_tick[SOUND_FRAMES];
static void          (*g_log)(const char *fmt, ...);


void
sound_make(float *out, int frames) {
  // two partials a fifth apart, the upper one dying sooner, under an
  // envelope that rises in 1.5 ms (no click) and falls with a 9 ms time
  // constant; the last 4 ms fade to 0, so the end makes no click either
  const float f1 = 1850.0f, f2 = 2775.0f, rise = 0.0015f, tau = 0.009f, fade = 0.004f;
  const float end = (float)SOUND_FRAMES / SOUND_RATE;
  float t, env, v, peak = 0;

  for(int i=0; i < frames; i++) {
    t = (float)i / SOUND_RATE;
    if(i >= SOUND_FRAMES) {
      out[i] = 0;
      continue;
    }
    env = expf(-t / tau);
    if(t < rise) {
      env *= 0.5f - 0.5f * cosf(PI * t / rise);
    }
    if(t > end - fade) {
      env *= 0.5f + 0.5f * cosf(PI * (t - (end - fade)) / fade);
    }
    v = sinf(2 * PI * f1 * t) + 0.45f * expf(-t / (tau * 0.5f)) * sinf(2 * PI * f2 * t);
    out[i] = env * v;
    peak = fabsf(out[i]) > peak ? fabsf(out[i]) : peak;
  }
  for(int i=0; i < frames && i < SOUND_FRAMES && peak > 0; i++) {
    out[i] /= peak;
  }
}


/**
 * block b of the tick, both channels, panned
 **/
static void
fill_block(int16_t *block, int b, float pan) {
  // equal power: the middle is as loud as a side
  float a = (pan + 1) * PI / 4, left = cosf(a) * TICK_GAIN * 1.41421f,
        right = sinf(a) * TICK_GAIN * 1.41421f;

  for(int i=0; i < SOUND_GRAIN; i++) {
    float v = g_tick[b * SOUND_GRAIN + i];
    block[2 * i] = (int16_t)lrintf(v * left * 32767);
    block[2 * i + 1] = (int16_t)lrintf(v * right * 32767);
  }
}


static void *
sound_main(void *arg) {
  static int16_t block[SOUND_GRAIN * 2], quiet[SOUND_GRAIN * 2];
  uint64_t tried = 0;
  int port = -1, b;
  float pan;

  (void)arg;
  sound_make(g_tick, SOUND_FRAMES);
  sceAudioOutInit();
  for(;;) {
    pthread_mutex_lock(&g_lock);
    while(!g_pending) {
      pthread_cond_wait(&g_wake, &g_lock);
    }
    g_pending = 0;
    pan = g_pan;
    pthread_mutex_unlock(&g_lock);
    if(port < 0) {
      // at the first tick, and again a while after it did not open
      if(tried && sceKernelGetProcessTime() - tried < RETRY_US) {
        continue;
      }
      tried = sceKernelGetProcessTime();
      if((port=sceAudioOutOpen(0xff, 0, 0, SOUND_GRAIN, SOUND_RATE, 1)) < 0) {
        if(g_log) {
          g_log("sound: port 0x%08x", (unsigned)port);
        }
        continue;
      }
    }
    for(b=0; b < SOUND_BLOCKS; b++) {
      pthread_mutex_lock(&g_lock);
      if(g_pending) {
        // another item lit up: the tick starts again, where the pointer is
        g_pending = 0;
        pan = g_pan;
        b = 0;
      }
      pthread_mutex_unlock(&g_lock);
      fill_block(block, b, pan);
      sceAudioOutOutput(port, block); // waits for the block before it
    }
    sceAudioOutOutput(port, quiet); // the port ends on silence
  }
  return NULL;
}


int
sound_init(void (*log)(const char *fmt, ...)) {
  pthread_attr_t attr;
  pthread_t thread;
  int ok;

  g_log = log;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 64 << 10);
  ok = !pthread_create(&thread, &attr, sound_main, NULL);
  if(ok) {
    pthread_detach(thread);
  }
  pthread_attr_destroy(&attr);
  pthread_mutex_lock(&g_lock);
  g_started = ok;
  pthread_mutex_unlock(&g_lock);
  if(!ok && log) {
    log("sound: no thread");
  }
  return ok ? 0 : -1;
}


void
sound_tick(float pan) {
  pthread_mutex_lock(&g_lock);
  if(g_started) {
    g_pan = pan < -1 ? -1 : pan > 1 ? 1 : pan;
    g_pending = 1;
    pthread_cond_signal(&g_wake);
  }
  pthread_mutex_unlock(&g_lock);
}
