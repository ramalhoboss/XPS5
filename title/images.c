/* The timeline's pictures, see images.h. The cache is a small table behind
one lock; only images_get and images_frame, on the drawing thread, let go of
a picture, so what a frame got stays alive while it draws. */

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "images.h"
#include "mem.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS   // two loaders decode at once, and nothing here needs them
#define STBI_NO_FAILURE_STRINGS
#define STBI_ASSERT(x) ((void)0) // the title's libc has no __assert
#define STBI_MALLOC(n)     mem_alloc(n) // decoding takes MiB, which malloc does not have
#define STBI_REALLOC(p, n) mem_realloc(p, n)
#define STBI_FREE(p)       mem_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#pragma GCC diagnostic pop

#define SLOTS      384
#define STALE      90                // frames a wanted picture waits before it is not wanted
#define MAX_PIXELS (4096 * 4096)     // of a file to decode: a PS5 screenshot is 3840 x 2160
#define MAX_SIDE   4096              // of a picture to make

enum { FREE, WANTED, LOADING, READY, FAILED };

typedef struct entry {
  char      url[IMAGES_URL_MAX];
  int       w, h;
  int       state;
  unsigned  used; // the frame that last asked for it
  uint32_t *pixels;
} entry_t;

static pthread_mutex_t g_lock;
static entry_t  g_slots[SLOTS];
static size_t   g_budget = 32 << 20;
static size_t   g_bytes;   // of the READY pictures
static unsigned g_frame = 1;
static int      g_arrived;


void
images_init(size_t budget) {
  pthread_mutex_init(&g_lock, NULL);
  g_budget = budget;
}


static size_t
bytes(const entry_t *e) {
  return (size_t)e->w * e->h * sizeof *e->pixels;
}


static void
release(entry_t *e) {
  if(e->state == READY) {
    g_bytes -= bytes(e);
  }
  mem_free(e->pixels);
  memset(e, 0, sizeof *e);
}


/**
 * the entry least recently asked for that this frame has not asked for and
 * no loader holds, of the given state or, with -1, of any
 **/
static entry_t *
oldest(int state) {
  entry_t *best = NULL;

  for(int i=0; i<SLOTS; i++) {
    entry_t *e = &g_slots[i];
    if(e->state == FREE || e->state == LOADING || e->used >= g_frame ||
       (state >= 0 && e->state != state)) {
      continue;
    }
    if(!best || e->used < best->used) {
      best = e;
    }
  }
  return best;
}


void
images_frame(void) {
  entry_t *e;

  pthread_mutex_lock(&g_lock);
  g_frame++;
  while(g_bytes > g_budget && (e=oldest(READY))) {
    release(e);
  }
  pthread_mutex_unlock(&g_lock);
}


static entry_t *
find(const char *url, int w, int h) {
  for(int i=0; i<SLOTS; i++) {
    entry_t *e = &g_slots[i];
    if(e->state != FREE && e->w == w && e->h == h && !strcmp(e->url, url)) {
      return e;
    }
  }
  return NULL;
}


const uint32_t *
images_get(const char *url, int w, int h) {
  const uint32_t *pixels = NULL;
  entry_t *e;

  if(!url || !url[0] || strlen(url) >= IMAGES_URL_MAX || w <= 0 || h <= 0 ||
     w > MAX_SIDE || h > MAX_SIDE) {
    return NULL;
  }
  pthread_mutex_lock(&g_lock);
  if((e=find(url, w, h))) {
    e->used = g_frame;
    pixels = e->state == READY ? e->pixels : NULL;
  } else {
    for(int i=0; i<SLOTS && !e; i++) {
      e = g_slots[i].state == FREE ? &g_slots[i] : NULL;
    }
    if(!e && (e=oldest(-1))) {
      release(e);
    }
    if(e) {
      strcpy(e->url, url);
      e->w = w;
      e->h = h;
      e->state = WANTED;
      e->used = g_frame;
    }
  }
  pthread_mutex_unlock(&g_lock);
  return pixels;
}


int
images_next(char *url, size_t size, int *w, int *h) {
  entry_t *best = NULL;

  pthread_mutex_lock(&g_lock);
  for(int i=0; i<SLOTS; i++) {
    entry_t *e = &g_slots[i];
    if(e->state != WANTED) {
      continue;
    }
    if(e->used + STALE < g_frame) {
      release(e); // scrolled away before its turn
    } else if(!best || e->used > best->used) {
      best = e;
    }
  }
  if(best && strlen(best->url) < size) {
    best->state = LOADING;
    strcpy(url, best->url);
    *w = best->w;
    *h = best->h;
  } else {
    best = NULL;
  }
  pthread_mutex_unlock(&g_lock);
  return best != NULL;
}


void
images_done_pixels(const char *url, int w, int h, uint32_t *pixels) {
  entry_t *e;

  pthread_mutex_lock(&g_lock);
  if((e=find(url, w, h)) && e->state == LOADING) {
    if(pixels) {
      e->pixels = pixels;
      e->state = READY;
      g_bytes += bytes(e);
      pixels = NULL;
    } else {
      e->state = FAILED;
    }
    g_arrived = 1;
  }
  pthread_mutex_unlock(&g_lock);
  mem_free(pixels);
}


void
images_done(const char *url, int w, int h, const void *data, size_t len) {
  images_done_pixels(url, w, h, data ? image_fit(data, len, w, h) : NULL);
}


void
images_retry(void) {
  pthread_mutex_lock(&g_lock);
  for(int i=0; i<SLOTS; i++) {
    if(g_slots[i].state == FAILED) {
      release(&g_slots[i]);
    }
  }
  pthread_mutex_unlock(&g_lock);
}


int
images_arrived(void) {
  int arrived;

  pthread_mutex_lock(&g_lock);
  arrived = g_arrived;
  g_arrived = 0;
  pthread_mutex_unlock(&g_lock);
  return arrived;
}


/*
 * Scaling.
 */

static uint32_t
pack(const unsigned char *p) {
  return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}


/**
 * the source around (fx, fy), between its four nearest pixels
 **/
static uint32_t
bilinear(const unsigned char *src, int iw, int ih, float fx, float fy) {
  int x0 = (int)floorf(fx), y0 = (int)floorf(fy), x1, y1;
  float tx = fx - x0, ty = fy - y0;
  unsigned char out[4];

  x1 = x0 + 1 < iw ? x0 + 1 : iw - 1;
  y1 = y0 + 1 < ih ? y0 + 1 : ih - 1;
  x0 = x0 < 0 ? 0 : x0 >= iw ? iw - 1 : x0;
  y0 = y0 < 0 ? 0 : y0 >= ih ? ih - 1 : y0;
  x1 = x1 < 0 ? 0 : x1;
  y1 = y1 < 0 ? 0 : y1;
  for(int c=0; c<4; c++) {
    float top = src[(y0 * iw + x0) * 4 + c] * (1 - tx) + src[(y0 * iw + x1) * 4 + c] * tx;
    float bottom = src[(y1 * iw + x0) * 4 + c] * (1 - tx) + src[(y1 * iw + x1) * 4 + c] * tx;
    out[c] = (unsigned char)(top * (1 - ty) + bottom * ty + 0.5f);
  }
  return pack(out);
}


/**
 * the mean of the source pixels under [fx0, fx1) x [fy0, fy1)
 **/
static uint32_t
mean(const unsigned char *src, int iw, int ih, float fx0, float fx1, float fy0, float fy1) {
  int x0 = (int)floorf(fx0), x1 = (int)ceilf(fx1), y0 = (int)floorf(fy0), y1 = (int)ceilf(fy1);
  unsigned sum[4] = {0, 0, 0, 0}, n;
  unsigned char out[4];

  x0 = x0 < 0 ? 0 : x0;
  y0 = y0 < 0 ? 0 : y0;
  x1 = x1 > iw ? iw : x1;
  y1 = y1 > ih ? ih : y1;
  x1 = x1 > x0 ? x1 : x0 + 1;
  y1 = y1 > y0 ? y1 : y0 + 1;
  n = (unsigned)((x1 - x0) * (y1 - y0));
  for(int y=y0; y<y1; y++) {
    for(int x=x0; x<x1; x++) {
      for(int c=0; c<4; c++) {
        sum[c] += src[(y * iw + x) * 4 + c];
      }
    }
  }
  for(int c=0; c<4; c++) {
    out[c] = (unsigned char)((sum[c] + n / 2) / n);
  }
  return pack(out);
}


int
image_size(const void *data, size_t len, int *w, int *h) {
  int n;

  return data && len && len <= (64u << 20) && stbi_info_from_memory(data, (int)len, w, h, &n) &&
         *w > 0 && *h > 0 && (long long)*w * *h <= MAX_PIXELS ? 0 : -1;
}


uint32_t *
image_scaled(const void *data, size_t len, int max_side, int *w, int *h) {
  int iw, ih;
  float k;

  if(max_side <= 0 || image_size(data, len, &iw, &ih)) {
    return NULL;
  }
  k = iw > ih ? (float)max_side / iw : (float)max_side / ih;
  k = k > 1 ? 1 : k;
  *w = (int)(iw * k + 0.5f);
  *h = (int)(ih * k + 0.5f);
  *w = *w < 1 ? 1 : *w;
  *h = *h < 1 ? 1 : *h;
  return image_fit(data, len, *w, *h);
}


uint32_t *
image_fit(const void *data, size_t len, int w, int h) {
  unsigned char *src;
  uint32_t *out;
  float step, ox, oy, fx, fy;
  int iw, ih, n;

  if(w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE || image_size(data, len, &iw, &ih)) {
    return NULL;
  }
  if(!(src=stbi_load_from_memory(data, (int)len, &iw, &ih, &n, 4))) {
    return NULL;
  }
  if(!(out=mem_alloc((size_t)w * h * sizeof *out))) {
    stbi_image_free(src);
    return NULL;
  }

  // source pixels for each pixel made, the same across and down, so the
  // picture covers w x h and its middle shows
  step = fminf((float)iw / w, (float)ih / h);
  ox = (iw - w * step) / 2;
  oy = (ih - h * step) / 2;
  for(int y=0; y<h; y++) {
    fy = oy + y * step;
    for(int x=0; x<w; x++) {
      fx = ox + x * step;
      out[y * w + x] = step <= 1 ?
        bilinear(src, iw, ih, fx + step / 2 - 0.5f, fy + step / 2 - 0.5f) :
        mean(src, iw, ih, fx, fx + step, fy, fy + step);
    }
  }
  stbi_image_free(src);
  return out;
}
