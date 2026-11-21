/* Host tests for src/clip.c: a progressive file of 3 s made here (30 frames
a second with a key frame each second, and AAC), found box by box as the
title finds it, cut, and the clip (its head and the range) read again. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clip.h"

static int g_failed;

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_failed++; } } while(0)

#define FRAMES  90    // of video, at 30000 / 1000
#define AFRAMES 130   // of audio, at 44100 / 1024

typedef struct buf {
  uint8_t d[65536];
  size_t  n;
} buf_t;


static void
put8(buf_t *b, unsigned v) {
  b->d[b->n++] = (uint8_t)v;
}


static void
put16(buf_t *b, unsigned v) {
  put8(b, v >> 8);
  put8(b, v);
}


static void
put32(buf_t *b, unsigned v) {
  put16(b, v >> 16);
  put16(b, v & 0xffff);
}


static void
put_zero(buf_t *b, size_t n) {
  memset(b->d + b->n, 0, n);
  b->n += n;
}


static size_t
open_box(buf_t *b, const char *type) {
  size_t at = b->n;

  put32(b, 0);
  memcpy(b->d + b->n, type, 4);
  b->n += 4;
  return at;
}


static void
set32(uint8_t *d, size_t at, unsigned v) {
  d[at] = (uint8_t)(v >> 24);
  d[at + 1] = (uint8_t)(v >> 16);
  d[at + 2] = (uint8_t)(v >> 8);
  d[at + 3] = (uint8_t)v;
}


static unsigned
get32(const uint8_t *d) {
  return (unsigned)d[0] << 24 | (unsigned)d[1] << 16 | (unsigned)d[2] << 8 | d[3];
}


static void
close_box(buf_t *b, size_t at) {
  set32(b->d, at, (unsigned)(b->n - at));
}


static size_t
full_box(buf_t *b, const char *type, unsigned version_flags) {
  size_t at = open_box(b, type);
  put32(b, version_flags);
  return at;
}


static unsigned
video_size(int i) {
  return 50 + (unsigned)(i % 7);
}


static unsigned
audio_size(int j) {
  return 20 + (unsigned)(j % 5);
}


static int32_t
video_cts(int i) {
  return i % 3 == 1 ? 3000 : i % 3 == 2 ? 0 : 1000;
}


static void
media(buf_t *b, unsigned id, unsigned timescale, const char *handler) {
  size_t at = full_box(b, "tkhd", 0);
  put32(b, 0);
  put32(b, 0);
  put32(b, id);
  put_zero(b, 68);
  close_box(b, at);
  (void)timescale;
  (void)handler;
}


/**
 * the file: ftyp, moov (each sample a chunk, at its place in the mdat), and
 * the mdat with the video and the audio interleaved; each sample's bytes are
 * its number
 **/
static void
make(buf_t *b) {
  static const uint8_t avcc[] = {1, 0x64, 0, 0x1f, 0xff, 0xe1, 0, 4, 0x67, 0x64, 0, 0x1f, 1, 0, 4,
                                 0x68, 0xee, 0x3c, 0x80};
  size_t vco[FRAMES], aco[AFRAMES], moov, trak, mdia, minf, stbl, at, entry, box, mdat;
  int j;

  b->n = 0;
  at = open_box(b, "ftyp");
  put32(b, 0x69736f6d);
  put32(b, 0);
  close_box(b, at);
  // something before the moov, as the title meets it
  at = open_box(b, "free");
  put_zero(b, 100);
  close_box(b, at);
  moov = open_box(b, "moov");

  trak = open_box(b, "trak");
  media(b, 1, 30000, "vide");
  mdia = open_box(b, "mdia");
  at = full_box(b, "mdhd", 0);
  put32(b, 0);
  put32(b, 0);
  put32(b, 30000);
  put32(b, FRAMES * 1000);
  put32(b, 0);
  close_box(b, at);
  at = full_box(b, "hdlr", 0);
  put32(b, 0);
  memcpy(b->d + b->n, "vide", 4);
  b->n += 4;
  put_zero(b, 13);
  close_box(b, at);
  minf = open_box(b, "minf");
  stbl = open_box(b, "stbl");
  at = full_box(b, "stsd", 0);
  put32(b, 1);
  entry = open_box(b, "avc1");
  put_zero(b, 6);
  put16(b, 1);
  put_zero(b, 16);
  put16(b, 1920);
  put16(b, 1080);
  put_zero(b, 50);
  box = open_box(b, "avcC");
  memcpy(b->d + b->n, avcc, sizeof avcc);
  b->n += sizeof avcc;
  close_box(b, box);
  close_box(b, entry);
  close_box(b, at);
  at = full_box(b, "stts", 0);
  put32(b, 1);
  put32(b, FRAMES);
  put32(b, 1000);
  close_box(b, at);
  at = full_box(b, "ctts", 0);
  put32(b, FRAMES);
  for(int i=0; i < FRAMES; i++) {
    put32(b, 1);
    put32(b, (unsigned)video_cts(i));
  }
  close_box(b, at);
  at = full_box(b, "stss", 0);
  put32(b, 3);
  put32(b, 1);
  put32(b, 31);
  put32(b, 61);
  close_box(b, at);
  at = full_box(b, "stsz", 0);
  put32(b, 0);
  put32(b, FRAMES);
  for(int i=0; i < FRAMES; i++) {
    put32(b, video_size(i));
  }
  close_box(b, at);
  at = full_box(b, "stsc", 0);
  put32(b, 1);
  put32(b, 1);
  put32(b, 1);
  put32(b, 1);
  close_box(b, at);
  at = full_box(b, "stco", 0);
  put32(b, FRAMES);
  for(int i=0; i < FRAMES; i++) {
    vco[i] = b->n;
    put32(b, 0);
  }
  close_box(b, at);
  close_box(b, stbl);
  close_box(b, minf);
  close_box(b, mdia);
  close_box(b, trak);

  trak = open_box(b, "trak");
  media(b, 2, 44100, "soun");
  mdia = open_box(b, "mdia");
  at = full_box(b, "mdhd", 0);
  put32(b, 0);
  put32(b, 0);
  put32(b, 44100);
  put32(b, AFRAMES * 1024);
  put32(b, 0);
  close_box(b, at);
  at = full_box(b, "hdlr", 0);
  put32(b, 0);
  memcpy(b->d + b->n, "soun", 4);
  b->n += 4;
  put_zero(b, 13);
  close_box(b, at);
  minf = open_box(b, "minf");
  stbl = open_box(b, "stbl");
  at = full_box(b, "stsd", 0);
  put32(b, 1);
  entry = open_box(b, "mp4a");
  put_zero(b, 6);
  put16(b, 1);
  put_zero(b, 8);
  put16(b, 2);
  put16(b, 16);
  put_zero(b, 4);
  put32(b, 44100u << 16);
  box = full_box(b, "esds", 0);
  put8(b, 3);
  put8(b, 25);
  put16(b, 0);
  put8(b, 0);
  put8(b, 4);
  put8(b, 17);
  put8(b, 0x40);
  put8(b, 0x15);
  put_zero(b, 11);
  put8(b, 5);
  put8(b, 2);
  put8(b, 0x12);
  put8(b, 0x10);
  put8(b, 6);
  put8(b, 1);
  put8(b, 2);
  close_box(b, box);
  close_box(b, entry);
  close_box(b, at);
  at = full_box(b, "stts", 0);
  put32(b, 1);
  put32(b, AFRAMES);
  put32(b, 1024);
  close_box(b, at);
  at = full_box(b, "stsz", 0);
  put32(b, 0);
  put32(b, AFRAMES);
  for(j=0; j < AFRAMES; j++) {
    put32(b, audio_size(j));
  }
  close_box(b, at);
  at = full_box(b, "stsc", 0);
  put32(b, 1);
  put32(b, 1);
  put32(b, 1);
  put32(b, 1);
  close_box(b, at);
  at = full_box(b, "stco", 0);
  put32(b, AFRAMES);
  for(j=0; j < AFRAMES; j++) {
    aco[j] = b->n;
    put32(b, 0);
  }
  close_box(b, at);
  close_box(b, stbl);
  close_box(b, minf);
  close_box(b, mdia);
  close_box(b, trak);
  close_box(b, moov);

  mdat = open_box(b, "mdat");
  j = 0;
  for(int i=0; i < FRAMES; i++) {
    set32(b->d, vco[i], (unsigned)b->n);
    memset(b->d + b->n, i, video_size(i));
    b->n += video_size(i);
    // the audio until the next frame
    for(; j < AFRAMES && (long)j * 1024 * 30 < (long)(i + 1) * 44100; j++) {
      set32(b->d, aco[j], (unsigned)b->n);
      memset(b->d + b->n, 0x80 | j, audio_size(j));
      b->n += audio_size(j);
    }
  }
  for(; j < AFRAMES; j++) {
    set32(b->d, aco[j], (unsigned)b->n);
    memset(b->d + b->n, 0x80 | j, audio_size(j));
    b->n += audio_size(j);
  }
  close_box(b, mdat);
}


/**
 * where a box of the type first is in the head, past from, or 0
 **/
static size_t
find(const clip_t *c, const char *type, size_t from) {
  for(size_t i=from + 4; i + 4 <= c->head_len; i++) {
    if(!memcmp(c->head + i, type, 4)) {
      return i - 4;
    }
  }
  return 0;
}


/**
 * the clip as a file: its head, then the old file's range
 **/
static uint8_t *
assemble(const clip_t *c, const buf_t *b, size_t *len) {
  uint8_t *f;

  *len = (size_t)clip_size(c);
  f = malloc(*len);
  memcpy(f, c->head, c->head_len);
  memcpy(f + c->head_len, b->d + c->from, (size_t)(c->to - c->from));
  return f;
}


/**
 * the new file's samples are the old ones from vfirst and afirst: their
 * times moved, their bytes the same
 **/
static void
same(const mp4_t *old, const mp4_t *m, const uint8_t *f, const buf_t *b, int vfirst, int afirst) {
  const mp4_track_t *ov = &old->video, *v = &m->video, *oa = &old->audio, *a = &m->audio;
  int keys = 0, bytes = 0, times = 0;

  for(int k=0; k < v->count; k++) {
    const mp4_sample_t *o = &ov->samples[vfirst + k], *s = &v->samples[k];
    keys += s->key != o->key;
    times += s->dts != o->dts - ov->samples[vfirst].dts || s->cts != o->cts || s->size != o->size;
    bytes += memcmp(f + s->offset, b->d + o->offset, s->size) != 0;
  }
  for(int k=0; k < a->count; k++) {
    const mp4_sample_t *o = &oa->samples[afirst + k], *s = &a->samples[k];
    times += s->dts != o->dts - oa->samples[afirst].dts || s->size != o->size;
    bytes += memcmp(f + s->offset, b->d + o->offset, s->size) != 0;
  }
  CHECK(keys == 0);
  CHECK(times == 0);
  CHECK(bytes == 0);
  CHECK(v->width == 1920 && v->height == 1080 && v->config_len == ov->config_len &&
        !memcmp(v->config, ov->config, v->config_len));
  CHECK(a->config_len == 2 && a->config[0] == 0x12 && a->channels == 2 && a->rate == 44100);
}


static void
test_boxes(void) {
  uint8_t h[16];
  uint32_t type;
  uint64_t size;
  unsigned head;

  memcpy(h, "\0\0\0\x10" "moov", 8);
  CHECK(clip_box(h, 8, 0, 100, &type, &size, &head) == 0);
  CHECK(type == CLIP_FOURCC('m', 'o', 'o', 'v') && size == 16 && head == 8);
  CHECK(clip_box(h, 8, 90, 100, &type, &size, &head) == -1); // past the file's end
  CHECK(clip_box(h, 7, 0, 100, &type, &size, &head) == -1);
  memcpy(h, "\0\0\0\0" "mdat", 8);  // to the end
  CHECK(clip_box(h, 8, 40, 100, &type, &size, &head) == 0 && size == 60 && head == 8);
  memcpy(h, "\0\0\0\x01" "mdat" "\0\0\0\x01\0\0\0\x20", 16); // 64-bit
  CHECK(clip_box(h, 16, 0, (1ull << 32) + 32, &type, &size, &head) == 0);
  CHECK(size == (1ull << 32) + 32 && head == 16);
  CHECK(clip_box(h, 16, 0, 1ull << 32, &type, &size, &head) == -1);
  CHECK(clip_box(h, 8, 0, (1ull << 32) + 32, &type, &size, &head) == -1);
  memcpy(h, "\0\0\0\x04" "free", 8); // shorter than its header
  CHECK(clip_box(h, 8, 0, 100, &type, &size, &head) == -1);
}


static void
test_clip(void) {
  static buf_t b;
  uint64_t at = 0, size = 0, moov_at = 0, moov_size = 0;
  uint32_t type;
  unsigned head;
  size_t len, elst;
  uint8_t *f;
  mp4_t old, m, whole;
  clip_t c;

  make(&b);
  CHECK(mp4_parse(&whole, b.d, b.n) == MP4_OK && whole.video.count == FRAMES && whole.audio.count == AFRAMES);

  // the moov, box by box
  while(at < b.n && !clip_box(b.d + at, 16, at, b.n, &type, &size, &head)) {
    if(type == CLIP_FOURCC('m', 'o', 'o', 'v')) {
      moov_at = at;
      moov_size = size;
    }
    at += size;
  }
  CHECK(at == b.n && moov_at > 0);
  CHECK(mp4_parse_moov(&old, b.d + moov_at, (size_t)moov_size, b.n) == MP4_OK);
  CHECK(old.video.count == FRAMES && old.audio.count == AFRAMES);
  for(int i=0; i < FRAMES && old.video.count == FRAMES; i++) {
    if(old.video.samples[i].offset != whole.video.samples[i].offset || old.video.samples[i].key != whole.video.samples[i].key) {
      CHECK(!"the moov alone gives the samples the whole file does");
      break;
    }
  }
  mp4_free(&whole);
  // too short a file for the samples' places: none
  CHECK(mp4_parse_moov(&m, b.d + moov_at, (size_t)moov_size, moov_at + moov_size) == MP4_NO_TRACK);

  // 1.5 s to 2.6 s: from the key frame of 1 s, the first shown 1 frame later
  CHECK(clip_make(&old, 1500, 2600, &c) == CLIP_OK);
  CHECK(c.start_ms == 1033 && c.length_ms == 1566);
  CHECK(c.from == old.video.samples[30].offset && c.to == old.audio.samples[111].offset + audio_size(111));
  f = assemble(&c, &b, &len);
  CHECK(mp4_parse(&m, f, len) == MP4_OK);
  CHECK(m.video.count == 48 && m.audio.count == 67);
  if(m.video.count == 48 && m.audio.count == 67) {
    same(&old, &m, f, &b, 30, 45);
    CHECK(m.video.samples[0].key && m.video.samples[30].key);
    CHECK(m.video.next_dts == 48000 && m.audio.next_dts == 67 * 1024);
  }
  // the video's edit leaves its first frame's wait out; the audio's waits
  // the 11 ms its first frame starts after the video's first shown
  elst = find(&c, "elst", 0);
  CHECK(elst && get32(c.head + elst + 12) == 1 && get32(c.head + elst + 16) == 1566 &&
        get32(c.head + elst + 20) == 1000);
  elst = elst ? find(&c, "elst", elst + 8) : 0;
  CHECK(elst && get32(c.head + elst + 12) == 2 && get32(c.head + elst + 16) == 11 &&
        get32(c.head + elst + 20) == 0xffffffffu && get32(c.head + elst + 28) == 67 * 1024 * 1000 / 44100);
  CHECK(find(&c, "co64", 0) && find(&c, "stss", 0) && find(&c, "ctts", 0));
  free(f);
  mp4_free(&m);
  clip_free(&c);
  CHECK(c.head == NULL);

  // to past the end: from the key frame of 1 s, to the last
  CHECK(clip_make(&old, 1900, 999999, &c) == CLIP_OK);
  CHECK(c.start_ms == 1033 && c.length_ms == 1966);
  f = assemble(&c, &b, &len);
  CHECK(len == clip_size(&c) && c.to == b.n);
  CHECK(mp4_parse(&m, f, len) == MP4_OK && m.video.count == 60 && m.audio.count == AFRAMES - 45);
  if(m.video.count == 60 && m.audio.count == AFRAMES - 45) {
    same(&old, &m, f, &b, 30, 45);
  }
  free(f);
  mp4_free(&m);
  clip_free(&c);

  // its first second and a frame: two key frames, in the table of them
  CHECK(clip_make(&old, 0, 1100, &c) == CLIP_OK);
  CHECK(c.start_ms == 33 && c.length_ms == 1066 && find(&c, "stss", 0));
  f = assemble(&c, &b, &len);
  CHECK(mp4_parse(&m, f, len) == MP4_OK && m.video.count == 33);
  CHECK(m.video.count == 33 && m.video.samples[0].key && !m.video.samples[29].key && m.video.samples[30].key);
  free(f);
  mp4_free(&m);
  clip_free(&c);
  // every frame a key frame: no table, which says so
  for(int i=0; i < old.video.count; i++) {
    old.video.samples[i].key = 1;
  }
  CHECK(clip_make(&old, 0, 1100, &c) == CLIP_OK && !find(&c, "stss", 0));
  f = assemble(&c, &b, &len);
  CHECK(mp4_parse(&m, f, len) == MP4_OK && m.video.count == 33);
  CHECK(m.video.count == 33 && m.video.samples[0].key && m.video.samples[29].key);
  for(int i=0; i < old.video.count; i++) {
    old.video.samples[i].key = i % 30 == 0;
  }
  free(f);
  mp4_free(&m);
  clip_free(&c);

  // the last 30 s of a file of 3: all of it, but the audio before the
  // first frame shown
  CHECK(clip_make(&old, old.duration_ms - CLIP_LAST_MS, old.duration_ms, &c) == CLIP_OK);
  CHECK(c.start_ms == 33 && c.from == old.video.samples[0].offset && c.to == b.n);
  f = assemble(&c, &b, &len);
  CHECK(mp4_parse(&m, f, len) == MP4_OK && m.video.count == FRAMES && m.audio.count == AFRAMES - 2);
  free(f);
  mp4_free(&m);
  clip_free(&c);

  // nothing, or less than a second (from the key frame of 2 s to the end)
  CHECK(clip_make(&old, 2000, 2000, &c) == CLIP_EMPTY);
  CHECK(clip_make(&old, 2500, 999999, &c) == CLIP_EMPTY && c.head == NULL);
  {
    mp4_t audio_only = old;
    audio_only.video.found = 0;
    CHECK(clip_make(&audio_only, 0, 1000, &c) == CLIP_NO_VIDEO && c.head == NULL);
  }

  // frames shown before the first decoded (negative offsets): the offsets
  // made whole, in a ctts of version 1, and no edit
  for(int i=0; i < old.video.count; i++) {
    old.video.samples[i].cts -= 2000;
  }
  CHECK(clip_make(&old, 1500, 2600, &c) == CLIP_OK);
  CHECK(c.start_ms == 966 && c.length_ms == 1600);
  at = find(&c, "ctts", 0);
  CHECK(at && c.head[at + 8] == 1);
  elst = find(&c, "elst", 0);
  CHECK(elst && get32(c.head + elst + 12) == 2); // the audio's, with its wait
  f = assemble(&c, &b, &len);
  CHECK(mp4_parse(&m, f, len) == MP4_OK && m.video.count == 48);
  CHECK(m.video.count == 48 && m.video.samples[0].cts == 0 && m.video.samples[2].cts == -1000 &&
        m.video.samples[1].cts == 2000);
  free(f);
  mp4_free(&m);
  clip_free(&c);
  mp4_free(&old);
}


int
main(void) {
  test_boxes();
  test_clip();
  if(g_failed) {
    printf("%d clip checks failed\n", g_failed);
    return 1;
  }
  printf("all clip checks passed\n");
  return 0;
}
