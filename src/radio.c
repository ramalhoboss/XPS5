/* A Space's radio, see radio.h. One radio per Space played: a thread that
owns it decodes and plays, and starts a fetcher that reads the playlist and
queues each new segment. radio_stop (or another Space) moves g_gen on;
both threads see they are not the radio playing any more, stop, and the
owner frees it, so the caller never waits on the network. */

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

#include "hls.h"
#include "https.h"
#include "mem.h"
#include "radio.h"

#define RADIO_STACK  (256 << 10) // the AAC decoder's, as the player's sound has
#define FETCH_STACK  (256 << 10) // as the player's fetcher
#define QUEUE        6           // segments fetched ahead of what plays
#define PLAYLIST_MAX (4 << 20)   // bytes of a playlist (a recording of hours names thousands)
#define SEGMENT_MAX  (8 << 20)
#define BEHIND       3           // segments behind a live Space's newest that it starts at
#define STALL_S      45          // a live one that adds no segment for this long is over
#define TRIES        4           // failed reads of the playlist in a row before it stops
#define GRAIN        1024        // audio frames a block of the port
#define RATE         48000

int sceKernelUsleep(unsigned microseconds);
uint64_t sceKernelGetProcessTime(void);
int sceAudioOutInit(void);
int sceAudioOutOpen(int user, int type, int index, unsigned len, unsigned freq, unsigned param);
int sceAudioOutOutput(int handle, const void *ptr);
int sceAudioOutClose(int handle);

#define AUDIO_MAIN   0 // SCE_AUDIO_OUT_PORT_TYPE_MAIN: the TV's
#define AUDIO_STEREO 1 // SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO

typedef struct segment {
  char  *body;
  size_t len;
} segment_t;

typedef struct radio {
  char          url[HLS_URL];  // its playlist, wiped when it is freed
  int           live;
  int           gen;           // it plays while g_gen is this
  volatile int  stop;          // the owner is done: the fetcher stops too

  pthread_mutex_t lock;        // of the queue and of what the fetcher says
  segment_t     queue[QUEUE];
  int           head, count;
  int           done;          // the fetcher has no more
  int           ended;         // because the playlist said so (or a live one went quiet)
  int           error, code;   // why it stopped, SPACE_ERR_*
  int           fetched;       // segments queued, of all

  hls_media_t   media;         // the fetcher's
  pthread_t     fetcher;
  int           fetching;
} radio_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int             g_gen;        // the radio that plays, under g_lock
static space_status_t  g_status;     // under g_lock
static int64_t         g_hush_until; // ms: hushed until then, 0 for as long as it is on, under g_lock
static int             g_alive;      // radios not freed yet, atomic
static void          (*g_log)(const char *fmt, ...);


static void
say(const char *fmt, ...) {
  char line[160];
  va_list ap;

  if(!g_log) {
    return;
  }
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  g_log("%s", line);
}


void
radio_init(void (*log)(const char *fmt, ...)) {
  g_log = log;
}


static int64_t
now_ms(void) {
  return (int64_t)(sceKernelGetProcessTime() / 1000);
}


/**
 * whether r is silent now: hushed, and its lease (if any) not over; under
 * g_lock
 **/
static int
hushed_now(const radio_t *r) {
  if(g_status.hushed && g_hush_until && now_ms() >= g_hush_until) {
    g_status.hushed = 0; // its lease ran out: whoever hushed it is not there to say so again
    g_hush_until = 0;
  }
  return g_status.hushed && r->gen == g_gen;
}


/**
 * whether r should stop: another radio plays, or its owner is done
 **/
static int
halted(const radio_t *r) {
  return r->stop || __atomic_load_n(&g_gen, __ATOMIC_ACQUIRE) != r->gen;
}


/**
 * how r stands, for the screen, while it is the radio playing
 **/
static void
set_state(const radio_t *r, int state, int error, int code) {
  pthread_mutex_lock(&g_lock);
  if(r->gen == g_gen) {
    g_status.state = state;
    g_status.error = error;
    g_status.code = code;
  }
  pthread_mutex_unlock(&g_lock);
}


/**
 * wait ms, a little at a time, unless r stops; 0, or -1 when it stopped
 **/
static int
nap(const radio_t *r, int ms) {
  for(; ms > 0 && !halted(r); ms -= 100) {
    sceKernelUsleep((unsigned)(ms < 100 ? ms : 100) * 1000);
  }
  return halted(r) ? -1 : 0;
}


/*
 * The fetcher: the playlist, read again while it is live, and its segments.
 */

/**
 * GET url with no X session: no cookies, no tokens, no X headers (the
 * library's cookie jar is off, see https.c); 0, or -1 with r's error set
 **/
static int
get(radio_t *r, const char *url, size_t max, https_response_t *res) {
  int ret = https_get_big(url, NULL, 0, max, res);

  if(ret) {
    r->error = SPACE_ERR_NETWORK;
    r->code = ret;
    return -1;
  }
  if(res->status != 200) {
    r->error = SPACE_ERR_HTTP;
    r->code = res->status;
    mem_free(res->body);
    res->body = NULL;
    return -1;
  }
  return 0;
}


/**
 * a segment into the queue, waiting while it is full; 0, or -1 when r
 * stopped (and the segment is freed)
 **/
static int
push(radio_t *r, char *body, size_t len) {
  while(!halted(r)) {
    pthread_mutex_lock(&r->lock);
    if(r->count < QUEUE) {
      r->queue[(r->head + r->count) % QUEUE] = (segment_t){body, len};
      r->count++;
      r->fetched++;
      pthread_mutex_unlock(&r->lock);
      return 0;
    }
    pthread_mutex_unlock(&r->lock);
    sceKernelUsleep(50000);
  }
  mem_free(body);
  return -1;
}


static void *
fetch_main(void *arg) {
  radio_t *r = arg;
  char playlist[HLS_URL], variant[HLS_URL];
  https_response_t res = {0};
  long long next = -1, n;
  int64_t heard_new = now_ms();
  int tries = 0, ret, got, wait_ms;

  snprintf(playlist, sizeof playlist, "%s", r->url);
  if(get(r, playlist, PLAYLIST_MAX, &res)) {
    say("space: playlist %s %d", r->error == SPACE_ERR_HTTP ? "HTTP" : "network", r->code);
    goto out;
  }
  // a master playlist names its variants: the one with the most sound
  if(!(ret=hls_variant(res.body, res.len, playlist, variant, sizeof variant))) {
    mem_free(res.body);
    res.body = NULL;
    snprintf(playlist, sizeof playlist, "%s", variant);
    memset(variant, 0, sizeof variant);
    if(get(r, playlist, PLAYLIST_MAX, &res)) {
      say("space: its variant %s %d", r->error == SPACE_ERR_HTTP ? "HTTP" : "network", r->code);
      goto out;
    }
  } else if(ret < 0) {
    say("space: not a playlist (%zu bytes)", res.len);
    r->error = SPACE_ERR_FORMAT;
    goto out;
  }

  while(!halted(r)) {
    if(res.body) {
      tries = 0;
      ret = hls_media(res.body, res.len, playlist, next < 0 ? (r->live ? -1 : 0) : next, &r->media);
      mem_free(res.body);
      res.body = NULL;
      if(ret) {
        r->error = SPACE_ERR_FORMAT;
        break;
      }
      if(next < 0) {
        // live: a few behind the newest; a recording: from its start
        next = r->media.first + (r->live && r->media.count > BEHIND ? r->media.count - BEHIND : 0);
        say("space: %s playlist of %d segments, %d s each", r->media.ended ? "a recorded" : "a live",
            r->media.count + r->media.more, r->media.target_s);
      } else if(next < r->media.first) {
        next = r->media.first; // it went on faster than these came: on from what it still names
      }
      got = 0;
      for(int i=0; i < r->media.count && !halted(r); i++) {
        n = r->media.first + i;
        if(n < next) {
          continue;
        }
        next = n + 1;
        if(!r->media.uri[i][0]) {
          continue; // not one that could be fetched (not https)
        }
        if(get(r, r->media.uri[i], SEGMENT_MAX, &res)) {
          say("space: segment %s %d", r->error == SPACE_ERR_HTTP ? "HTTP" : "network", r->code);
          r->error = SPACE_ERR_NONE; // one lost: the next may come
          continue;
        }
        if(!r->fetched) {
          say("space: first segment, %zu bytes", res.len);
        }
        got = 1;
        char *body = res.body;
        res.body = NULL;
        if(push(r, body, res.len)) {
          break;
        }
      }
      memset(&r->media.uri, 0, sizeof r->media.uri);
      if(got) {
        heard_new = now_ms();
      }
      if(r->media.ended && !r->media.more && next >= r->media.first + r->media.count) {
        r->ended = 1; // all of it is in the queue
        break;
      }
      if(r->live && now_ms() - heard_new > STALL_S * 1000) {
        say("space: no new segment for %d s", STALL_S);
        r->ended = 1;
        break;
      }
    }
    // live: read again in half a segment's time; a recording's next batch at
    // once; after a failed read, in 2 s
    wait_ms = r->media.ended ? 0 : r->media.target_s > 0 ? r->media.target_s * 500 : 1500;
    wait_ms = wait_ms > 5000 ? 5000 : wait_ms < 1000 && !r->media.ended ? 1000 : wait_ms;
    wait_ms = tries ? 2000 : wait_ms;
    if(nap(r, wait_ms)) {
      break;
    }
    if(get(r, playlist, PLAYLIST_MAX, &res)) {
      if(++tries >= TRIES) {
        say("space: playlist %s %d, %d times", r->error == SPACE_ERR_HTTP ? "HTTP" : "network", r->code, tries);
        break;
      }
      r->error = SPACE_ERR_NONE;
    }
  }

 out:
  mem_free(res.body);
  memset(playlist, 0, sizeof playlist);
  memset(&r->media, 0, sizeof r->media);
  pthread_mutex_lock(&r->lock);
  r->done = 1;
  pthread_mutex_unlock(&r->lock);
  return NULL;
}


/*
 * The owner: the frames of each segment decoded and played.
 */

typedef struct out {
  int16_t   block[GRAIN * 2];
  int       filled;
  int       port;
  long long blocks;  // played
  int16_t  *pcm;
  int       room;
  SwrContext *swr;
} out_t;


/**
 * the frame f to the port, a block at a time; each block waits for the
 * one before it to play, which paces it all. Silent when hushed
 **/
static int
play_frame(radio_t *r, out_t *o, AVFrame *f) {
  AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
  int n, hushed;

  if(!o->swr && (swr_alloc_set_opts2(&o->swr, &stereo, AV_SAMPLE_FMT_S16, RATE, &f->ch_layout, f->format,
                                     f->sample_rate, 0, NULL) < 0 || swr_init(o->swr) < 0)) {
    swr_free(&o->swr);
    return SPACE_ERR_DECODER;
  }
  n = swr_get_out_samples(o->swr, f->nb_samples);
  if(n > o->room) {
    free(o->pcm);
    o->room = n + GRAIN;
    if(!(o->pcm=malloc((size_t)o->room * 4))) {
      o->room = 0;
      return SPACE_ERR_MEMORY;
    }
  }
  uint8_t *planes[1] = {(uint8_t *)o->pcm};
  n = swr_convert(o->swr, planes, o->room, (const uint8_t *const *)f->extended_data, f->nb_samples);
  if(o->port < 0) {
    sceAudioOutInit();
    if((o->port=sceAudioOutOpen(0xff, AUDIO_MAIN, 0, GRAIN, RATE, AUDIO_STEREO)) < 0) {
      say("space: audio port 0x%08x", (unsigned)o->port);
      r->code = o->port;
      return SPACE_ERR_AUDIO;
    }
  }
  for(int k=0; k < n && !halted(r); ) {
    int take = GRAIN - o->filled < n - k ? GRAIN - o->filled : n - k;
    memcpy(o->block + o->filled * 2, o->pcm + k * 2, (size_t)take * 4);
    o->filled += take;
    k += take;
    if(o->filled < GRAIN) {
      continue;
    }
    pthread_mutex_lock(&g_lock);
    hushed = hushed_now(r);
    pthread_mutex_unlock(&g_lock);
    // a recording waits while hushed; a live one plays on, silent, to stay live
    while(hushed && !r->live && !halted(r)) {
      sceKernelUsleep(50000);
      pthread_mutex_lock(&g_lock);
      hushed = hushed_now(r);
      pthread_mutex_unlock(&g_lock);
    }
    if(hushed) {
      memset(o->block, 0, sizeof o->block);
    }
    sceAudioOutOutput(o->port, o->block);
    o->filled = 0;
    o->blocks++;
    pthread_mutex_lock(&g_lock);
    if(r->gen == g_gen) {
      g_status.heard_ms = o->blocks * GRAIN * 1000 / RATE;
    }
    pthread_mutex_unlock(&g_lock);
  }
  return SPACE_ERR_NONE;
}


/**
 * a segment's frames: its ID3 tags passed over, each ADTS frame decoded
 * and played; a frame cut short ends it, bytes that are no frame are
 * passed over to the next sync word
 **/
static int
play_segment(radio_t *r, out_t *o, AVCodecContext *ctx, AVPacket *pkt, AVFrame *f, const segment_t *s) {
  const uint8_t *p = (const uint8_t *)s->body, *sync;
  size_t at = hls_id3(p, s->len), frame;
  int ret, err = SPACE_ERR_NONE;

  while(at < s->len && !halted(r) && !err) {
    if((ret=hls_adts(p + at, s->len - at, &frame)) == 0) {
      break;
    }
    if(ret < 0) {
      sync = memchr(p + at + 1, 0xff, s->len - at - 1);
      at = sync ? (size_t)(sync - p) : s->len;
      continue;
    }
    if(av_new_packet(pkt, (int)frame) < 0) {
      return SPACE_ERR_MEMORY;
    }
    memcpy(pkt->data, p + at, frame);
    at += frame;
    ret = avcodec_send_packet(ctx, pkt);
    av_packet_unref(pkt);
    if(ret < 0 && ret != AVERROR(EAGAIN)) {
      continue; // a frame the decoder did not take: the next
    }
    while(!err && avcodec_receive_frame(ctx, f) >= 0) {
      err = play_frame(r, o, f);
      av_frame_unref(f);
    }
  }
  return err;
}


static void
free_radio(radio_t *r) {
  r->stop = 1;
  if(r->fetching) {
    pthread_join(r->fetcher, NULL); // it stops at its next read or nap
  }
  for(int i=0; i < r->count; i++) {
    mem_free(r->queue[(r->head + i) % QUEUE].body);
  }
  pthread_mutex_destroy(&r->lock);
  memset(r->url, 0, sizeof r->url);
  free(r);
  __atomic_sub_fetch(&g_alive, 1, __ATOMIC_ACQ_REL);
}


static void *
radio_main(void *arg) {
  radio_t *r = arg;
  const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
  AVCodecContext *ctx = NULL;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *f = av_frame_alloc();
  pthread_attr_t attr;
  out_t o = {.port = -1};
  segment_t s;
  int err = SPACE_ERR_NONE, have, done, ret, on = 0, waiting = 0;

  // ADTS carries the stream's settings in each frame: no extradata
  if(!pkt || !f || !codec || !(ctx=avcodec_alloc_context3(codec))) {
    err = SPACE_ERR_MEMORY;
  } else if((ret=avcodec_open2(ctx, codec, NULL)) < 0) {
    say("space: sound decoder %d", ret);
    r->code = ret;
    err = SPACE_ERR_DECODER;
  }
  if(!err) {
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, FETCH_STACK);
    r->fetching = !pthread_create(&r->fetcher, &attr, fetch_main, r);
    pthread_attr_destroy(&attr);
    err = r->fetching ? SPACE_ERR_NONE : SPACE_ERR_MEMORY;
  }

  while(!err && !halted(r)) {
    pthread_mutex_lock(&r->lock);
    if((have=r->count > 0)) {
      s = r->queue[r->head];
      r->head = (r->head + 1) % QUEUE;
      r->count--;
    }
    done = r->done;
    pthread_mutex_unlock(&r->lock);
    if(!have && done) {
      break;
    }
    if(have != !waiting) {
      waiting = !have;
      pthread_mutex_lock(&g_lock);
      if(r->gen == g_gen) {
        g_status.waiting = waiting && on;
      }
      pthread_mutex_unlock(&g_lock);
    }
    if(!have) {
      sceKernelUsleep(20000);
      continue;
    }
    if(!on) {
      on = 1;
      set_state(r, SPACE_ON, 0, 0);
    }
    err = play_segment(r, &o, ctx, pkt, f, &s);
    mem_free(s.body);
  }

  if(!halted(r)) {
    if(err) {
      set_state(r, SPACE_FAILED, err, r->code);
    } else if(r->error && !o.blocks && !r->ended) {
      set_state(r, SPACE_FAILED, r->error, r->code);
    } else {
      set_state(r, SPACE_OVER, r->ended ? 0 : r->error, r->ended ? 0 : r->code); // over, or gone from the CDN
    }
    say("space: %s after %lld s of sound, %d segments", err ? "failed" : "over",
        o.blocks * GRAIN / RATE, r->fetched);
  } else {
    say("space: stopped after %lld s of sound", o.blocks * GRAIN / RATE);
  }
  if(o.port >= 0) {
    sceAudioOutOutput(o.port, NULL);
    sceAudioOutClose(o.port);
  }
  free(o.pcm);
  swr_free(&o.swr);
  avcodec_free_context(&ctx);
  av_frame_free(&f);
  av_packet_free(&pkt);
  free_radio(r);
  return NULL;
}


void
radio_play(const char *url, int live) {
  pthread_attr_t attr;
  pthread_t thread;
  radio_t *r;
  int gen;

  pthread_mutex_lock(&g_lock);
  gen = ++g_gen; // any other radio stops
  memset(&g_status, 0, sizeof g_status);
  g_status.state = SPACE_TUNING;
  g_hush_until = 0;
  pthread_mutex_unlock(&g_lock);

  if(!url || strncmp(url, "https://", 8) || strlen(url) >= sizeof r->url || !(r=calloc(1, sizeof *r))) {
    pthread_mutex_lock(&g_lock);
    if(gen == g_gen) {
      g_status.state = SPACE_FAILED;
      g_status.error = url && !strncmp(url, "https://", 8) ? SPACE_ERR_MEMORY : SPACE_ERR_FORMAT;
    }
    pthread_mutex_unlock(&g_lock);
    return;
  }
  memcpy(r->url, url, strlen(url) + 1);
  r->live = live;
  r->gen = gen;
  pthread_mutex_init(&r->lock, NULL);
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, RADIO_STACK);
  __atomic_add_fetch(&g_alive, 1, __ATOMIC_ACQ_REL);
  if(pthread_create(&thread, &attr, radio_main, r)) {
    __atomic_sub_fetch(&g_alive, 1, __ATOMIC_ACQ_REL);
    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&r->lock);
    memset(r->url, 0, sizeof r->url);
    free(r);
    pthread_mutex_lock(&g_lock);
    if(gen == g_gen) {
      g_status.state = SPACE_FAILED;
      g_status.error = SPACE_ERR_MEMORY;
    }
    pthread_mutex_unlock(&g_lock);
    return;
  }
  pthread_attr_destroy(&attr);
  pthread_detach(thread);
  say("space: tuning in, %s", live ? "live" : "a recording");
}


void
radio_stop(void) {
  pthread_mutex_lock(&g_lock);
  g_gen++;
  memset(&g_status, 0, sizeof g_status);
  g_hush_until = 0;
  pthread_mutex_unlock(&g_lock);
}


void
radio_hush(int on) {
  pthread_mutex_lock(&g_lock);
  g_status.hushed = on && (g_status.state == SPACE_TUNING || g_status.state == SPACE_ON);
  g_hush_until = 0;
  pthread_mutex_unlock(&g_lock);
}


void
radio_hush_for(int ms) {
  pthread_mutex_lock(&g_lock);
  g_status.hushed = g_status.state == SPACE_TUNING || g_status.state == SPACE_ON;
  g_hush_until = g_status.hushed ? now_ms() + (ms > 0 ? ms : 1) : 0;
  pthread_mutex_unlock(&g_lock);
}


void
radio_status(space_status_t *s) {
  pthread_mutex_lock(&g_lock);
  if(g_status.hushed && g_hush_until && now_ms() >= g_hush_until) {
    g_status.hushed = 0;
    g_hush_until = 0;
  }
  *s = g_status;
  pthread_mutex_unlock(&g_lock);
}


int
radio_alive(void) {
  return __atomic_load_n(&g_alive, __ATOMIC_ACQUIRE);
}
