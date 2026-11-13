/* X's videos, see player.h. One session per video opened: a thread fetches
the file, another reads the samples of what came of it (again as more
comes) and decodes the pictures, pacing them by a clock that waits while
the file has not come that far, and another decodes the sound and feeds
the audio port, which waits for each block to play. Opening another video, or closing the viewer, tells the
session to stop; it frees itself, so the screen never waits for it. */

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>

#include "https.h"
#include "mem.h"
#include "mp4.h"
#include "player.h"

#define AUDIO_STACK (256 << 10) // the AAC decoder's, above the 64 KiB a thread gets

#define VIDEO_MAX   (512 << 20) // bytes of a file: 7 minutes of X's 1080p (src/xapi.c VIDEO_BYTES)
#define START_MS    1500       // of pictures that came before a video starts
#define RESUME_MS   1000       // of pictures that came before it goes on after waiting
#define TABLES_STEP (1 << 20)  // bytes that come before the samples are read again
#define FETCH_STACK (256 << 10)
#define URL_MAX     512
#define LATE_MS     120        // a picture this late is dropped, when another follows
#define AHEAD_MS    250        // the sound decoded ahead of the clock, at most
#define GRAIN       1024       // audio frames a block of the port
#define RATE        48000
#define MAX_W       1920
#define MAX_H       1080
#define DECODERS    4          // threads of the H.264 decoder
#define HELPERS     3          // threads that scale the pictures with the video's own
#define HELPER_STACK (128 << 10)

int sceKernelUsleep(unsigned microseconds);
uint64_t sceKernelGetProcessTime(void);
int sceAudioOutInit(void);
int sceAudioOutOpen(int user, int type, int index, unsigned len, unsigned freq, unsigned param);
int sceAudioOutOutput(int handle, const void *ptr);
int sceAudioOutClose(int handle);

typedef struct session {
  char      url[URL_MAX];
  int       w, h;
  int       want_w, want_h; // a new size the screen asked for, 0 for none
  volatile int stop;

  // what the screen reads, under g_lock
  int       state, error, code, pos_ms, len_ms, has_audio;
  uint32_t *frames[2];
  int       front;      // frames[front] is the one shown, -1 before the first
  int       sound;
  int       paused;
  int       seek_ms;    // asked for, -1 none

  // the clock: media time base_ms at base_us, stopped while paused
  int64_t   base_us, base_ms;
  int       epoch;      // a jump of the clock (a seek, the start again); the sound follows it

  // the file as it comes, and the samples of what came of it (read again
  // as more comes, by the pictures' thread; the sound's reads them under
  // tables)
  https_stream_t  net;
  pthread_t       fetcher;
  int             fetching;
  pthread_mutex_t tables;
  size_t          tried;      // bytes of the file the samples were last read from
  int             parse_ret;  // and how that went, MP4_*
  int             buffering;  // waiting for more of it, under g_lock
  mp4_t     mp4;
  pthread_t audio;
  int       audio_started;
} session_t;


/**
 * where each pixel of the picture shown comes from, for one size of the
 * video and of the picture: for a side, the first of the pixels it takes
 * (n of them, at most TAPS) and their weights, of 256 in all.
 *
 * The brightness, which carries the detail, by a cubic (Catmull-Rom): made
 * bigger, a pixel mixes the four around it; made smaller, the cubic is
 * stretched over the pixels it covers, so it stays as sharp as the
 * picture shown can be (a tent, as before, softens it) without fine lines
 * shimmering. Of the same size, each pixel is its own. The colour, which
 * has half as many pixels, by a tent: two around it made bigger, the ones
 * it covers made smaller.
 *
 * A side at a time: each row of the video a band needs is taken along x
 * once, kept in a ring of TAPS rows, and the rows of the ring are then
 * added up for each row of the picture.
 **/
#define TAPS 16

typedef struct axis {
  int      a, n;
  int16_t  w[TAPS]; // below 0 for the cubic's outer pixels
} axis_t;

typedef struct band_rows {
  int     row[3][TAPS];          // the row of the plane each holds, -1 for none
  int32_t px[3][TAPS][MAX_W];    // that row taken along x
  int32_t sum[3][MAX_W];         // the rows added up for a row of the picture
} band_rows_t;

typedef struct scale {
  int         sw, sh, w, h;
  axis_t      lx[MAX_W], cx[MAX_W], ly[MAX_H], cy[MAX_H];
  band_rows_t band[HELPERS + 1]; // each band's own
} scale_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static session_t      *g_cur;
static int             g_changed;
static void          (*g_log)(const char *fmt, ...);


static void
say(const char *fmt, ...) {
  char buf[200];
  va_list args;

  if(!g_log) {
    return;
  }
  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  g_log("%s", buf);
}


void
player_init(void (*log)(const char *fmt, ...)) {
  g_log = log;
}


static int64_t
now_us(void) {
  return (int64_t)sceKernelGetProcessTime();
}


/**
 * where the clock is, in ms of the video; under g_lock
 **/
static int64_t
clock_ms(const session_t *s) {
  return s->paused ? s->base_ms : s->base_ms + (now_us() - s->base_us) / 1000;
}


static void
set_clock(session_t *s, int64_t ms) {
  pthread_mutex_lock(&g_lock);
  s->base_ms = ms;
  s->base_us = now_us();
  s->epoch++;
  pthread_mutex_unlock(&g_lock);
}


static void
set_state(session_t *s, int state, int error, int code) {
  pthread_mutex_lock(&g_lock);
  s->state = state;
  s->error = error;
  s->code = code;
  g_changed = 1;
  pthread_mutex_unlock(&g_lock);
}


/*
 * The pictures: YUV 4:2:0 to the screen's colours, scaled.
 */

typedef struct colours {
  int y_off, ky, rv, gu, gv, bu; // times 256
} colours_t;


static colours_t
colours_of(const AVFrame *f) {
  int full = f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P;
  int hd = f->colorspace == AVCOL_SPC_BT709 || (f->colorspace == AVCOL_SPC_UNSPECIFIED && f->height >= 600);

  if(full) {
    return hd ? (colours_t){0, 256, 403, 48, 120, 475} : (colours_t){0, 256, 359, 88, 183, 454};
  }
  return hd ? (colours_t){16, 298, 459, 55, 136, 541} : (colours_t){16, 298, 409, 100, 208, 516};
}


static inline uint32_t
clamp8(int v) {
  return v < 0 ? 0 : v > 255 ? 255 : (uint32_t)v;
}


/**
 * the Catmull-Rom cubic, at d pixels from the centre
 **/
static float
cubic(float d) {
  d = d < 0 ? -d : d;
  return d < 1 ? (1.5f * d - 2.5f) * d * d + 1 : d < 2 ? ((-0.5f * d + 2.5f) * d - 4) * d + 2 : 0;
}


/**
 * the pixels of a side of in pixels that each of out takes, by their
 * centres: by the cubic when sharp (stretched by as many pixels of in as
 * one of out covers), else by a tent one pixel of out wide (at least one
 * of in)
 **/
static void
axis_of(axis_t *t, int out, int in, int sharp) {
  const float step = (float)in / out;
  float s = step > 1 ? step : 1, r, wf[TAPS], total;
  int first, last, n, left;

  if(sharp) {
    s = s > TAPS / 4.0f ? TAPS / 4.0f : s;
    r = 2 * s;
  } else {
    r = s > TAPS / 2.0f ? TAPS / 2.0f : s;
  }
  for(int i=0; i < out; i++) {
    float c = (i + 0.5f) * step - 0.5f; // the centre, in pixels of in
    if(in == out) {
      t[i].a = i; // each its own
      t[i].n = 1;
      t[i].w[0] = 256;
      continue;
    }
    first = (int)ceilf(c - r + 0.001f);
    last = (int)floorf(c + r - 0.001f);
    first = first < 0 ? 0 : first;
    last = last >= in ? in - 1 : last;
    last = last - first >= TAPS ? first + TAPS - 1 : last;
    first = first > last ? last : first;
    n = last - first + 1;
    total = 0;
    for(int k=0; k < n; k++) {
      float d = (first + k - c) / (sharp ? s : r);
      wf[k] = sharp ? cubic(d) : 1 - (d < 0 ? -d : d);
      wf[k] = !sharp && wf[k] < 0 ? 0 : wf[k];
      total += wf[k];
    }
    if(total <= 0.01f) {
      wf[0] = total = 1;
      n = 1;
    }
    t[i].a = first;
    t[i].n = n;
    left = 256;
    for(int k=0; k < n; k++) {
      int w = k + 1 < n ? (int)lroundf(wf[k] / total * 256) : left;
      t[i].w[k] = (int16_t)w;
      left -= w;
    }
  }
}


static void
scale_for(scale_t *sc, int sw, int sh, int w, int h) {
  if(sc->sw == sw && sc->sh == sh && sc->w == w && sc->h == h) {
    return;
  }
  axis_of(sc->lx, w, sw, 1);
  axis_of(sc->cx, w, (sw + 1) / 2, 0);
  axis_of(sc->ly, h, sh, 1);
  axis_of(sc->cy, h, (sh + 1) / 2, 0);
  sc->sw = sw;
  sc->sh = sh;
  sc->w = w;
  sc->h = h;
}


/**
 * row r of plane p of f taken along x (its pixels mixed by the axis), from
 * the band's ring, where it is put the first time
 **/
static const int32_t *
row_along_x(band_rows_t *b, const AVFrame *f, int p, int r, const axis_t *x, int w) {
  const int slot = r % TAPS;
  const uint8_t *row;
  int32_t *out = b->px[p][slot];

  if(b->row[p][slot] != r) {
    row = f->data[p] + (size_t)r * f->linesize[p];
    for(int i=0; i < w; i++) {
      const uint8_t *q = row + x[i].a;
      int v = 0;
      for(int k=0; k < x[i].n; k++) {
        v += q[k] * x[i].w[k];
      }
      out[i] = v;
    }
    b->row[p][slot] = r;
  }
  return out;
}


/**
 * the rows of plane p that y takes, each taken along x and times its
 * weight, added up into the band's sum of the plane
 **/
static const int32_t *
add_rows(band_rows_t *b, const AVFrame *f, int p, const axis_t *y, const axis_t *x, int w) {
  int32_t *sum = b->sum[p];

  memset(sum, 0, w * sizeof *sum);
  for(int k=0; k < y->n; k++) {
    const int32_t *px = row_along_x(b, f, p, y->a + k, x, w);
    const int wk = y->w[k];
    for(int i=0; i < w; i++) {
      sum[i] += px[i] * wk;
    }
  }
  return sum;
}


/**
 * rows [from, to) of the picture f into dst, sc->w x sc->h, band part's:
 * each plane's rows taken along x, then down, then the colours of the
 * screen
 **/
static void
convert_rows(const AVFrame *f, scale_t *sc, uint32_t *dst, int from, int to, int part) {
  const colours_t c = colours_of(f);
  band_rows_t *b = &sc->band[part];
  const int32_t *lum, *cu, *cv;
  int l, d, e;

  memset(b->row, 0xff, sizeof b->row); // a new picture: none of its rows taken yet
  dst += (size_t)from * sc->w;
  for(int y=from; y < to; y++) {
    lum = add_rows(b, f, 0, &sc->ly[y], sc->lx, sc->w);
    cu = add_rows(b, f, 1, &sc->cy[y], sc->cx, sc->w);
    cv = add_rows(b, f, 2, &sc->cy[y], sc->cx, sc->w);
    for(int x=0; x < sc->w; x++) {
      l = (((lum[x] + 32768) >> 16) - c.y_off) * c.ky;
      d = ((cu[x] + 32768) >> 16) - 128;
      e = ((cv[x] + 32768) >> 16) - 128;
      *dst++ = 0xff000000u | clamp8((l + c.bu * d + 128) >> 8) << 16 |
               clamp8((l - c.gu * d - c.gv * e + 128) >> 8) << 8 | clamp8((l + c.rv * e + 128) >> 8);
    }
  }
}


/*
 * The helpers: a picture is scaled in HELPERS + 1 bands of rows at once,
 * one by the video's thread. One picture at a time (g_convert), as a
 * video that stops may still be scaling its last one.
 */

static pthread_mutex_t g_convert = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_jobs = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_go = PTHREAD_COND_INITIALIZER, g_done = PTHREAD_COND_INITIALIZER;
static const AVFrame  *g_job_frame;
static scale_t        *g_job_scale;
static uint32_t       *g_job_dst;
static int             g_job_number, g_job_left, g_helpers;


static void
band(int part, const AVFrame *f, scale_t *sc, uint32_t *dst) {
  convert_rows(f, sc, dst, sc->h * part / (HELPERS + 1), sc->h * (part + 1) / (HELPERS + 1), part);
}


static void *
helper_main(void *arg) {
  int part = (int)(intptr_t)arg, seen = 0;
  const AVFrame *f;
  scale_t *sc;
  uint32_t *dst;

  for(;;) {
    pthread_mutex_lock(&g_jobs);
    while(g_job_number == seen) {
      pthread_cond_wait(&g_go, &g_jobs);
    }
    seen = g_job_number;
    f = g_job_frame;
    sc = g_job_scale;
    dst = g_job_dst;
    pthread_mutex_unlock(&g_jobs);
    band(part, f, sc, dst);
    pthread_mutex_lock(&g_jobs);
    if(!--g_job_left) {
      pthread_cond_signal(&g_done);
    }
    pthread_mutex_unlock(&g_jobs);
  }
  return NULL;
}


/**
 * the helpers, started once; how many there are
 **/
static int
start_helpers(void) {
  pthread_attr_t attr;
  pthread_t t;

  if(g_helpers) {
    return g_helpers;
  }
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, HELPER_STACK);
  // band 0 is the video's thread's; the helpers take 1..HELPERS, all of them or none
  for(int i=1; i <= HELPERS; i++) {
    if(pthread_create(&t, &attr, helper_main, (void *)(intptr_t)i)) {
      g_helpers = -1; // the ones started wait for nothing; the video's thread does it all
      break;
    }
    pthread_detach(t);
  }
  pthread_attr_destroy(&attr);
  if(!g_helpers) {
    g_helpers = HELPERS;
  }
  return g_helpers;
}


/**
 * the picture f into dst, sc->w x sc->h
 **/
static void
convert(const AVFrame *f, scale_t *sc, uint32_t *dst) {
  pthread_mutex_lock(&g_convert);
  if(start_helpers() < 0) {
    convert_rows(f, sc, dst, 0, sc->h, 0);
    pthread_mutex_unlock(&g_convert);
    return;
  }
  pthread_mutex_lock(&g_jobs);
  g_job_frame = f;
  g_job_scale = sc;
  g_job_dst = dst;
  g_job_left = HELPERS;
  g_job_number++;
  pthread_cond_broadcast(&g_go);
  pthread_mutex_unlock(&g_jobs);
  band(0, f, sc, dst);
  pthread_mutex_lock(&g_jobs);
  while(g_job_left) {
    pthread_cond_wait(&g_done, &g_jobs);
  }
  pthread_mutex_unlock(&g_jobs);
  pthread_mutex_unlock(&g_convert);
}


/**
 * show f: into the picture not shown, then that one shown
 **/
static void
present(session_t *s, scale_t *sc, const AVFrame *f, int ms) {
  uint32_t *a, *b;
  int back;

  pthread_mutex_lock(&g_lock);
  // a new size: new pictures, made here, the only thread that writes them
  if(s->want_w && (s->want_w != s->w || s->want_h != s->h)) {
    a = mem_alloc((size_t)s->want_w * s->want_h * 4);
    b = mem_alloc((size_t)s->want_w * s->want_h * 4);
    if(a && b) {
      mem_free(s->frames[0]);
      mem_free(s->frames[1]);
      s->frames[0] = a;
      s->frames[1] = b;
      s->w = s->want_w;
      s->h = s->want_h;
      s->front = -1;
    } else {
      mem_free(a);
      mem_free(b);
    }
  }
  s->want_w = s->want_h = 0;
  back = s->front == 0 ? 1 : 0;
  pthread_mutex_unlock(&g_lock);
  scale_for(sc, f->width, f->height, s->w, s->h);
  convert(f, sc, s->frames[back]);
  pthread_mutex_lock(&g_lock);
  s->front = back;
  s->pos_ms = ms;
  g_changed = 1;
  pthread_mutex_unlock(&g_lock);
}


/*
 * The sound.
 */

static int
open_decoder(AVCodecContext **out, enum AVCodecID id, const mp4_track_t *t, int threads) {
  const AVCodec *codec = avcodec_find_decoder(id);
  AVCodecContext *ctx;
  int ret;

  *out = NULL;
  if(!codec || !(ctx=avcodec_alloc_context3(codec))) {
    return AVERROR(ENOMEM);
  }
  if(t->config_len) {
    if(!(ctx->extradata=av_mallocz(t->config_len + AV_INPUT_BUFFER_PADDING_SIZE))) {
      avcodec_free_context(&ctx);
      return AVERROR(ENOMEM);
    }
    memcpy(ctx->extradata, t->config, t->config_len);
    ctx->extradata_size = (int)t->config_len;
  }
  ctx->pkt_timebase = (AVRational){1, (int)t->timescale};
  if(threads > 1) {
    ctx->thread_count = threads;
    ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
  }
  if((ret=avcodec_open2(ctx, codec, NULL)) < 0) {
    avcodec_free_context(&ctx);
    return ret;
  }
  *out = ctx;
  return 0;
}


static int
send_sample(AVCodecContext *ctx, AVPacket *pkt, const session_t *s, const mp4_sample_t *m) {
  if(av_new_packet(pkt, (int)m->size) < 0) {
    return AVERROR(ENOMEM);
  }
  memcpy(pkt->data, s->net.body + m->offset, m->size);
  pkt->dts = m->dts;
  pkt->pts = m->dts + m->cts;
  pkt->flags = m->key ? AV_PKT_FLAG_KEY : 0;
  return avcodec_send_packet(ctx, pkt);
}


/**
 * a copy of sample i of the track t and its time in ms, under the tables'
 * lock; 0 while it has not come
 **/
static int
sample_at(session_t *s, const mp4_track_t *t, int i, mp4_sample_t *m, int *ms) {
  int there;

  pthread_mutex_lock(&s->tables);
  if((there=i < t->count && t->timescale)) {
    *m = t->samples[i];
    *ms = (int)(m->dts * 1000 / t->timescale);
  }
  pthread_mutex_unlock(&s->tables);
  return there;
}


static void *
audio_main(void *arg) {
  session_t *s = arg;
  const mp4_track_t *t = &s->mp4.audio;
  AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
  int16_t block[GRAIN * 2];
  int16_t *out = NULL;
  AVCodecContext *ctx = NULL;
  SwrContext *swr = NULL;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *f = av_frame_alloc();
  int port = -1, epoch = -1, i = 0, filled = 0, room = 0, n, ret = -1, sound, paused, ms;
  mp4_sample_t sample;
  int64_t clock;

  if(pkt && f) {
    pthread_mutex_lock(&s->tables);
    ret = open_decoder(&ctx, AV_CODEC_ID_AAC, t, 1);
    pthread_mutex_unlock(&s->tables);
  }
  if(ret) {
    say("player: sound decoder %d", ret);
    goto out;
  }
  sceAudioOutInit();
  if((port=sceAudioOutOpen(0xff, 0, 0, GRAIN, RATE, 1)) < 0) {
    say("player: audio port 0x%08x", (unsigned)port);
    goto out;
  }

  while(!s->stop) {
    pthread_mutex_lock(&g_lock);
    sound = s->sound;
    paused = s->paused;
    clock = clock_ms(s);
    if(epoch != s->epoch) {
      epoch = s->epoch;
      pthread_mutex_unlock(&g_lock);
      avcodec_flush_buffers(ctx);
      swr_free(&swr);
      filled = 0;
      // the first sample at the clock
      pthread_mutex_lock(&s->tables);
      for(i=0; i < t->count && t->samples[i].dts * 1000 / t->timescale < clock; i++) {
      }
      pthread_mutex_unlock(&s->tables);
      continue;
    }
    pthread_mutex_unlock(&g_lock);
    if(!sound || paused || !sample_at(s, t, i, &sample, &ms)) {
      sceKernelUsleep(10000);
      continue;
    }
    if(ms > clock + AHEAD_MS) {
      sceKernelUsleep(5000);
      continue;
    }
    if((ret=send_sample(ctx, pkt, s, &sample)) < 0 && ret != AVERROR(EAGAIN)) {
      av_packet_unref(pkt);
      i++;
      continue;
    }
    av_packet_unref(pkt);
    i++;
    while(!s->stop && avcodec_receive_frame(ctx, f) >= 0) {
      if(!swr && (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, RATE, &f->ch_layout,
                                      f->format, f->sample_rate, 0, NULL) < 0 || swr_init(swr) < 0)) {
        swr_free(&swr);
        av_frame_unref(f);
        break;
      }
      n = swr_get_out_samples(swr, f->nb_samples);
      if(n > room) {
        free(out);
        room = n + GRAIN;
        if(!(out=malloc((size_t)room * 4))) {
          room = 0;
          av_frame_unref(f);
          break;
        }
      }
      uint8_t *planes[1] = {(uint8_t *)out};
      n = swr_convert(swr, planes, room, (const uint8_t *const *)f->extended_data, f->nb_samples);
      av_frame_unref(f);
      // into blocks of the port, each played before the next is taken
      for(int k=0; k < n && !s->stop; ) {
        int take = GRAIN - filled < n - k ? GRAIN - filled : n - k;
        memcpy(block + filled * 2, out + k * 2, (size_t)take * 4);
        filled += take;
        k += take;
        if(filled == GRAIN) {
          sceAudioOutOutput(port, block);
          filled = 0;
        }
      }
    }
  }

 out:
  if(port >= 0) {
    sceAudioOutOutput(port, NULL);
    sceAudioOutClose(port);
  }
  free(out);
  swr_free(&swr);
  avcodec_free_context(&ctx);
  av_frame_free(&f);
  av_packet_free(&pkt);
  return NULL;
}


/*
 * The pictures, and the session itself.
 */

static void
free_session(session_t *s) {
  if(s->audio_started) {
    pthread_join(s->audio, NULL);
  }
  if(s->fetching) {
    pthread_join(s->fetcher, NULL); // it stops at its next read
  }
  mp4_free(&s->mp4);
  mem_free(s->net.body);
  pthread_mutex_destroy(&s->tables);
  mem_free(s->frames[0]);
  mem_free(s->frames[1]);
  free(s);
}


/**
 * after a failure or the end: wait to be told to stop
 **/
static void
idle(session_t *s) {
  while(!s->stop) {
    sceKernelUsleep(20000);
  }
}


static void *
fetch_main(void *arg) {
  session_t *s = arg;

  https_get_stream(s->url, VIDEO_MAX, &s->net);
  return NULL;
}


/**
 * the samples of the file read again once TABLES_STEP more of it came, or
 * the rest of it: 0 when they were, 1 when nothing new came, or MP4_* when
 * what came cannot be read (yet: its tables may be at its end)
 **/
static int
read_tables(session_t *s) {
  const int state = __atomic_load_n(&s->net.state, __ATOMIC_ACQUIRE);
  const size_t got = state == HTTPS_STREAM_WAITING || !s->net.body ? 0 :
                     __atomic_load_n(&s->net.got, __ATOMIC_ACQUIRE);
  mp4_t m;

  if(!got || got == s->tried || (got - s->tried < TABLES_STEP && state == HTTPS_STREAM_READING)) {
    return 1;
  }
  s->tried = got;
  if((s->parse_ret=mp4_parse(&m, (const uint8_t *)s->net.body, got))) {
    return s->parse_ret;
  }
  pthread_mutex_lock(&s->tables);
  mp4_free(&s->mp4);
  s->mp4 = m;
  pthread_mutex_unlock(&s->tables);
  return 0;
}


/**
 * whether all of the file that will come came, and its samples were read
 **/
static int
complete(const session_t *s) {
  const int state = __atomic_load_n(&s->net.state, __ATOMIC_ACQUIRE);

  return (state == HTTPS_STREAM_DONE || state == HTTPS_STREAM_FAILED) &&
         s->tried == __atomic_load_n(&s->net.got, __ATOMIC_ACQUIRE);
}


/**
 * ms of the track from its sample i to the last that came
 **/
static int
ahead_ms(const mp4_track_t *t, int i) {
  if(i >= t->count || !t->timescale) {
    return 0;
  }
  return (int)((t->samples[t->count - 1].dts - t->samples[i].dts) * 1000 / t->timescale);
}


static void
set_buffering(session_t *s, int on) {
  pthread_mutex_lock(&g_lock);
  if(s->buffering != on) {
    s->buffering = on;
    g_changed = 1;
  }
  pthread_mutex_unlock(&g_lock);
}


static void *
video_main(void *arg) {
  session_t *s = arg;
  const mp4_track_t *t = &s->mp4.video;
  AVCodecContext *ctx = NULL;
  AVPacket *pkt = NULL;
  AVFrame *f = NULL, *last = NULL;
  scale_t *sc = NULL;
  pthread_attr_t attr;
  int i = 0, ret, have = 0, draining = 0, ms = 0, last_ms = 0, seek = -1, paused, resized, pictures = 0,
      dropped = 0, waits = 0, stalled = 0, state;
  int64_t clock;

  // the file comes on a thread of its own
  s->net.stop = &s->stop;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, FETCH_STACK);
  s->fetching = !pthread_create(&s->fetcher, &attr, fetch_main, s);
  pthread_attr_destroy(&attr);
  if(!s->fetching) {
    set_state(s, PLAYER_FAILED, PLAYER_ERR_MEMORY, 0);
    goto out;
  }
  // it starts once a second and a half of pictures came, or all of the
  // file when its tables are at its end
  for(;;) {
    if(s->stop) {
      goto out;
    }
    state = __atomic_load_n(&s->net.state, __ATOMIC_ACQUIRE);
    read_tables(s);
    if(t->found && t->count && (complete(s) || ahead_ms(t, 0) >= START_MS)) {
      break;
    }
    if(state == HTTPS_STREAM_FAILED && (!s->net.body || !t->count)) {
      if(s->net.status && s->net.status != 200) {
        say("player: video HTTP %d", s->net.status);
        set_state(s, PLAYER_FAILED, PLAYER_ERR_HTTP, s->net.status);
      } else {
        say("player: video network %d, %zu bytes", s->net.error, s->tried);
        set_state(s, PLAYER_FAILED, PLAYER_ERR_NETWORK, s->net.error);
      }
      goto out;
    }
    if(complete(s)) {
      say("player: mp4 %d (%zu bytes)", s->parse_ret, s->tried);
      set_state(s, PLAYER_FAILED, PLAYER_ERR_FORMAT, s->parse_ret ? s->parse_ret : MP4_NO_TRACK);
      goto out;
    }
    sceKernelUsleep(20000);
  }
  say("player: %dx%d, %d pictures, %d sound blocks, %lld ms, %zu of %zu KiB", t->width, t->height, t->count,
      s->mp4.audio.count, (long long)s->mp4.duration_ms, s->tried >> 10, s->net.len >> 10);
  if((ret=open_decoder(&ctx, AV_CODEC_ID_H264, t, DECODERS)) || !(pkt=av_packet_alloc()) ||
     !(f=av_frame_alloc()) || !(last=av_frame_alloc())) {
    say("player: decoder %d", ret);
    set_state(s, PLAYER_FAILED, PLAYER_ERR_DECODER, ret);
    goto out;
  }
  if(!(sc=mem_alloc(sizeof *sc))) {
    set_state(s, PLAYER_FAILED, PLAYER_ERR_MEMORY, 0);
    goto out;
  }
  memset(sc, 0, sizeof *sc);
  pthread_mutex_lock(&g_lock);
  // X's length, until all of the file came
  if(!s->len_ms || complete(s)) {
    s->len_ms = (int)s->mp4.duration_ms;
  }
  s->has_audio = s->mp4.audio.found;
  pthread_mutex_unlock(&g_lock);
  set_clock(s, 0);
  set_state(s, PLAYER_PLAYING, 0, 0);
  if(s->mp4.audio.found) {
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, AUDIO_STACK);
    s->audio_started = !pthread_create(&s->audio, &attr, audio_main, s);
    pthread_attr_destroy(&attr);
  }

  while(!s->stop) {
    pthread_mutex_lock(&g_lock);
    if(s->seek_ms >= 0) {
      seek = s->seek_ms; // kept until the file came that far
      s->seek_ms = -1;
    }
    paused = s->paused;
    resized = s->want_w != 0;
    pthread_mutex_unlock(&g_lock);

    // a new size (the tile, then the whole screen): the picture shown again
    // at once, also while paused
    if(resized && last->data[0]) {
      present(s, sc, last, last_ms);
    }

    // more of the file
    if(!read_tables(s) && complete(s) && s->mp4.duration_ms > 0) {
      pthread_mutex_lock(&g_lock);
      s->len_ms = (int)s->mp4.duration_ms;
      pthread_mutex_unlock(&g_lock);
    }

    if(seek >= 0) {
      if(!complete(s) && ahead_ms(t, 0) < seek + RESUME_MS) {
        set_buffering(s, 1);
        sceKernelUsleep(20000);
        continue;
      }
      i = mp4_key_before(t, seek);
      avcodec_flush_buffers(ctx);
      if(have) {
        av_frame_unref(f);
        have = 0;
      }
      draining = 0;
      seek = -1;
      stalled = 0;
      set_buffering(s, 0);
      set_clock(s, t->samples[i].dts * 1000 / t->timescale);
      continue;
    }
    if(paused) {
      sceKernelUsleep(10000);
      continue;
    }
    // waiting for the file: on once a second of pictures came after here
    if(stalled && !complete(s) && ahead_ms(t, i) < RESUME_MS) {
      sceKernelUsleep(20000);
      continue;
    }

    // the next picture out of the decoder
    while(!have && !s->stop) {
      ret = avcodec_receive_frame(ctx, f);
      if(ret >= 0) {
        have = 1;
        ms = (int)((f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts) *
                   1000 / t->timescale);
        break;
      }
      if(ret == AVERROR_EOF) {
        // the end: from the start again, like x.com
        avcodec_flush_buffers(ctx);
        draining = 0;
        i = 0;
        set_clock(s, t->samples[0].dts * 1000 / t->timescale);
        break;
      }
      if(i < t->count) {
        ret = send_sample(ctx, pkt, s, &t->samples[i]);
        av_packet_unref(pkt);
        if(ret != AVERROR(EAGAIN)) {
          i++;
        }
      } else if(!complete(s)) {
        break; // the file has not come that far
      } else if(!draining) {
        avcodec_send_packet(ctx, NULL);
        draining = 1;
      }
    }
    if(!have) {
      if(!stalled && i >= t->count && !complete(s)) {
        stalled = 1;
        waits++;
        set_buffering(s, 1);
      }
      continue;
    }
    if(stalled) {
      // after waiting, the clock goes on from this picture
      stalled = 0;
      set_buffering(s, 0);
      set_clock(s, ms);
    }

    pthread_mutex_lock(&g_lock);
    clock = clock_ms(s);
    pthread_mutex_unlock(&g_lock);
    if(ms > clock + 2) {
      sceKernelUsleep((unsigned)(ms - clock > 10 ? 10000 : (ms - clock) * 1000));
      continue;
    }
    if(ms < clock - LATE_MS && pictures && i < t->count) {
      dropped++;
    } else {
      present(s, sc, f, ms);
      pictures++;
      av_frame_unref(last);
      if(av_frame_ref(last, f) >= 0) {
        last_ms = ms;
      }
    }
    av_frame_unref(f);
    have = 0;
  }
  say("player: %d pictures shown, %d dropped, waited for the file %d times", pictures, dropped, waits);

 out:
  idle(s);
  mem_free(sc);
  av_frame_free(&last);
  av_frame_free(&f);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  free_session(s);
  return NULL;
}

void
player_open(const char *url, int w, int h, int sound, int len_ms) {
  pthread_attr_t attr;
  pthread_t thread;
  session_t *s;

  player_close();
  w = w < 16 ? 16 : w > MAX_W ? MAX_W : w;
  h = h < 16 ? 16 : h > MAX_H ? MAX_H : h;
  if(!(s=calloc(1, sizeof *s)) || !(s->frames[0]=mem_alloc((size_t)w * h * 4)) ||
     !(s->frames[1]=mem_alloc((size_t)w * h * 4))) {
    if(s) {
      mem_free(s->frames[0]);
      free(s);
    }
    return;
  }
  pthread_mutex_init(&s->tables, NULL);
  snprintf(s->url, sizeof s->url, "%s", url);
  s->w = w;
  s->h = h;
  s->front = -1;
  s->seek_ms = -1;
  s->sound = sound;
  s->len_ms = len_ms;
  s->state = PLAYER_LOADING;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 512 << 10);
  if(pthread_create(&thread, &attr, video_main, s)) {
    pthread_attr_destroy(&attr);
    mem_free(s->frames[0]);
    mem_free(s->frames[1]);
    free(s);
    return;
  }
  pthread_attr_destroy(&attr);
  pthread_detach(thread);
  pthread_mutex_lock(&g_lock);
  g_cur = s;
  g_changed = 1;
  pthread_mutex_unlock(&g_lock);
}


void
player_close(void) {
  pthread_mutex_lock(&g_lock);
  if(g_cur) {
    g_cur->stop = 1;
    g_cur = NULL;
    g_changed = 1;
  }
  pthread_mutex_unlock(&g_lock);
}


int
player_is(const char *url) {
  int is;

  pthread_mutex_lock(&g_lock);
  is = g_cur && !strcmp(g_cur->url, url);
  pthread_mutex_unlock(&g_lock);
  return is;
}


void
player_resize(int w, int h) {
  w = w < 16 ? 16 : w > MAX_W ? MAX_W : w;
  h = h < 16 ? 16 : h > MAX_H ? MAX_H : h;
  pthread_mutex_lock(&g_lock);
  if(g_cur && (w != g_cur->w || h != g_cur->h)) {
    g_cur->want_w = w;
    g_cur->want_h = h;
  }
  pthread_mutex_unlock(&g_lock);
}


void
player_pause(int paused) {
  pthread_mutex_lock(&g_lock);
  if(g_cur && g_cur->paused != paused && g_cur->state == PLAYER_PLAYING) {
    g_cur->base_ms = clock_ms(g_cur);
    g_cur->base_us = now_us();
    g_cur->paused = paused;
    g_changed = 1;
  }
  pthread_mutex_unlock(&g_lock);
}


void
player_seek(int delta_ms) {
  int to;

  pthread_mutex_lock(&g_lock);
  if(g_cur && g_cur->state == PLAYER_PLAYING) {
    to = (int)clock_ms(g_cur) + delta_ms;
    to = to < 0 ? 0 : g_cur->len_ms && to > g_cur->len_ms - 500 ? g_cur->len_ms - 500 : to;
    g_cur->seek_ms = to < 0 ? 0 : to;
    g_changed = 1;
  }
  pthread_mutex_unlock(&g_lock);
}


void
player_sound(int on) {
  pthread_mutex_lock(&g_lock);
  if(g_cur && g_cur->sound != on) {
    g_cur->sound = on;
    g_cur->epoch++; // the sound starts again where the pictures are
    g_changed = 1;
  }
  pthread_mutex_unlock(&g_lock);
}


void
player_status(player_status_t *st) {
  memset(st, 0, sizeof *st);
  pthread_mutex_lock(&g_lock);
  if(g_cur) {
    st->state = g_cur->paused && g_cur->state == PLAYER_PLAYING ? PLAYER_PAUSED : g_cur->state;
    st->error = g_cur->error;
    st->code = g_cur->code;
    st->pos_ms = g_cur->pos_ms;
    st->len_ms = g_cur->len_ms;
    st->has_audio = g_cur->has_audio;
    st->sound = g_cur->sound && g_cur->has_audio;
    st->buffering = g_cur->buffering && g_cur->state == PLAYER_PLAYING;
  }
  pthread_mutex_unlock(&g_lock);
}


const uint32_t *
player_frame(int *w, int *h) {
  pthread_mutex_lock(&g_lock);
  if(g_cur && g_cur->front >= 0) {
    *w = g_cur->w;
    *h = g_cur->h;
    return g_cur->frames[g_cur->front]; // held until player_frame_done
  }
  pthread_mutex_unlock(&g_lock);
  return NULL;
}


void
player_frame_done(void) {
  pthread_mutex_unlock(&g_lock);
}


int
player_changed(void) {
  int changed;

  pthread_mutex_lock(&g_lock);
  changed = g_changed;
  g_changed = 0;
  pthread_mutex_unlock(&g_lock);
  return changed;
}
