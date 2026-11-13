/* Host tests for src/mp4.c: a progressive file and a fragmented one, made
box by box here, and files cut short or made up. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mp4.h"

static int g_failed;

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_failed++; } } while(0)

typedef struct buf {
  uint8_t d[8192];
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
close_box(buf_t *b, size_t at) {
  size_t len = b->n - at;

  b->d[at] = (uint8_t)(len >> 24);
  b->d[at + 1] = (uint8_t)(len >> 16);
  b->d[at + 2] = (uint8_t)(len >> 8);
  b->d[at + 3] = (uint8_t)len;
}


static void
set32(buf_t *b, size_t at, unsigned v) {
  b->d[at] = (uint8_t)(v >> 24);
  b->d[at + 1] = (uint8_t)(v >> 16);
  b->d[at + 2] = (uint8_t)(v >> 8);
  b->d[at + 3] = (uint8_t)v;
}


static void
full_box(buf_t *b, const char *type, unsigned version_flags, size_t *at) {
  *at = open_box(b, type);
  put32(b, version_flags);
}


/**
 * a track's tkhd
 **/
static void
track_head(buf_t *b, unsigned id) {
  size_t at;

  full_box(b, "tkhd", 0, &at);
  put32(b, 0);
  put32(b, 0);
  put32(b, id);
  put_zero(b, 68);
  close_box(b, at);
}


static void
avc1(buf_t *b) {
  static const uint8_t avcc[] = {1, 0x64, 0, 0x1f, 0xff, 0xe1, 0, 4, 0x67, 0x64, 0, 0x1f, 1, 0, 4,
                                 0x68, 0xee, 0x3c, 0x80};
  size_t at, entry, box;

  full_box(b, "stsd", 0, &at);
  put32(b, 1);
  entry = open_box(b, "avc1");
  put_zero(b, 6);
  put16(b, 1);
  put_zero(b, 16);
  put16(b, 1280);
  put16(b, 720);
  put_zero(b, 50);
  box = open_box(b, "avcC");
  memcpy(b->d + b->n, avcc, sizeof avcc);
  b->n += sizeof avcc;
  close_box(b, box);
  close_box(b, entry);
  close_box(b, at);
}


static void
mp4a(buf_t *b) {
  size_t at, entry, esds;

  full_box(b, "stsd", 0, &at);
  put32(b, 1);
  entry = open_box(b, "mp4a");
  put_zero(b, 6);
  put16(b, 1);
  put_zero(b, 8);
  put16(b, 2);
  put16(b, 16);
  put_zero(b, 4);
  put32(b, 44100u << 16);
  full_box(b, "esds", 0, &esds);
  put8(b, 3);    // ES_Descriptor
  put8(b, 25);
  put16(b, 0);
  put8(b, 0);
  put8(b, 4);    // DecoderConfigDescriptor
  put8(b, 17);
  put8(b, 0x40); // MPEG-4 audio
  put8(b, 0x15);
  put_zero(b, 11);
  put8(b, 5);    // DecoderSpecificInfo: AAC LC, 44.1 kHz, stereo
  put8(b, 2);
  put8(b, 0x12);
  put8(b, 0x10);
  put8(b, 6);    // SLConfigDescriptor
  put8(b, 1);
  put8(b, 2);
  close_box(b, esds);
  close_box(b, entry);
  close_box(b, at);
}


static void
media_head(buf_t *b, unsigned timescale, const char *handler) {
  size_t at;

  full_box(b, "mdhd", 0, &at);
  put32(b, 0);
  put32(b, 0);
  put32(b, timescale);
  put32(b, 0);
  put32(b, 0);
  close_box(b, at);
  full_box(b, "hdlr", 0, &at);
  put32(b, 0);
  memcpy(b->d + b->n, handler, 4);
  b->n += 4;
  put_zero(b, 13);
  close_box(b, at);
}


/**
 * a progressive file: 3 video samples in 2 chunks, 2 audio samples
 **/
static size_t
progressive(buf_t *b, size_t *chunk_at) {
  size_t moov, trak, mdia, minf, stbl, at, mdat;

  b->n = 0;
  at = open_box(b, "ftyp");
  put32(b, 0x69736f6d);
  put32(b, 0);
  close_box(b, at);
  moov = open_box(b, "moov");

  trak = open_box(b, "trak");
  track_head(b, 1);
  mdia = open_box(b, "mdia");
  media_head(b, 30000, "vide");
  minf = open_box(b, "minf");
  stbl = open_box(b, "stbl");
  avc1(b);
  full_box(b, "stts", 0, &at);
  put32(b, 1);
  put32(b, 3);
  put32(b, 1001);
  close_box(b, at);
  full_box(b, "ctts", 0, &at);
  put32(b, 1);
  put32(b, 3);
  put32(b, 2002);
  close_box(b, at);
  full_box(b, "stss", 0, &at);
  put32(b, 1);
  put32(b, 1);
  close_box(b, at);
  full_box(b, "stsz", 0, &at);
  put32(b, 0);
  put32(b, 3);
  put32(b, 10);
  put32(b, 20);
  put32(b, 30);
  close_box(b, at);
  full_box(b, "stsc", 0, &at);
  put32(b, 1);
  put32(b, 1);
  put32(b, 2);
  put32(b, 1);
  close_box(b, at);
  full_box(b, "stco", 0, &at);
  put32(b, 2);
  chunk_at[0] = b->n;
  put32(b, 0);
  chunk_at[1] = b->n;
  put32(b, 0);
  close_box(b, at);
  close_box(b, stbl);
  close_box(b, minf);
  close_box(b, mdia);
  close_box(b, trak);

  trak = open_box(b, "trak");
  track_head(b, 2);
  mdia = open_box(b, "mdia");
  media_head(b, 44100, "soun");
  minf = open_box(b, "minf");
  stbl = open_box(b, "stbl");
  mp4a(b);
  full_box(b, "stts", 0, &at);
  put32(b, 1);
  put32(b, 2);
  put32(b, 1024);
  close_box(b, at);
  full_box(b, "stsz", 0, &at);
  put32(b, 7);
  put32(b, 2);
  close_box(b, at);
  full_box(b, "stsc", 0, &at);
  put32(b, 1);
  put32(b, 1);
  put32(b, 2);
  put32(b, 1);
  close_box(b, at);
  full_box(b, "stco", 0, &at);
  put32(b, 1);
  chunk_at[2] = b->n;
  put32(b, 0);
  close_box(b, at);
  close_box(b, stbl);
  close_box(b, minf);
  close_box(b, mdia);
  close_box(b, trak);
  close_box(b, moov);

  mdat = open_box(b, "mdat");
  set32(b, chunk_at[0], (unsigned)b->n);
  put_zero(b, 30);
  set32(b, chunk_at[1], (unsigned)b->n);
  put_zero(b, 30);
  set32(b, chunk_at[2], (unsigned)b->n);
  put_zero(b, 14);
  close_box(b, mdat);
  return b->n;
}


static void
test_progressive(void) {
  static buf_t b;
  size_t chunk_at[3], len = progressive(&b, chunk_at);
  unsigned first;
  mp4_t m;

  CHECK(mp4_parse(&m, b.d, len) == MP4_OK);
  CHECK(m.video.found && m.video.count == 3 && m.video.timescale == 30000);
  CHECK(m.video.width == 1280 && m.video.height == 720 && m.video.config_len == 19);
  first = (unsigned)b.d[chunk_at[0]] << 24 | (unsigned)b.d[chunk_at[0] + 1] << 16 |
          (unsigned)b.d[chunk_at[0] + 2] << 8 | b.d[chunk_at[0] + 3];
  if(m.video.count == 3) {
    CHECK(m.video.samples[0].offset == first && m.video.samples[1].offset == first + 10);
    CHECK(m.video.samples[2].size == 30 && m.video.samples[2].offset == first + 30);
    CHECK(m.video.samples[1].dts == 1001 && m.video.samples[2].cts == 2002);
    CHECK(m.video.samples[0].key && !m.video.samples[1].key && !m.video.samples[2].key);
  }
  CHECK(m.audio.found && m.audio.count == 2 && m.audio.rate == 44100 && m.audio.channels == 2);
  CHECK(m.audio.config_len == 2 && m.audio.config[0] == 0x12 && m.audio.config[1] == 0x10);
  CHECK(m.audio.count == 2 && m.audio.samples[1].size == 7 && m.audio.samples[1].key);
  CHECK(m.duration_ms == 100); // 3 x 1001 / 30000
  CHECK(mp4_key_before(&m.video, 90) == 0);
  mp4_free(&m);

  // cut in the last video chunk: the samples that are there
  CHECK(mp4_parse(&m, b.d, first + 45) == MP4_OK && m.video.count == 2 && !m.audio.found);
  mp4_free(&m);
  // cut in the moov
  CHECK(mp4_parse(&m, b.d, 200) == MP4_NO_MOOV);
  CHECK(mp4_parse(&m, (const uint8_t *)"not an mp4 at all", 17) == MP4_NO_MOOV);
}


/**
 * a fragmented file: a moov with empty tables and trex, then one fragment
 * of 3 video samples
 **/
static void
test_fragments(void) {
  static buf_t b;
  size_t moov, trak, mdia, minf, stbl, at, mvex, moof, traf, offset_at, mdat;
  mp4_t m;

  b.n = 0;
  moov = open_box(&b, "moov");
  mvex = open_box(&b, "mvex");
  full_box(&b, "trex", 0, &at);
  put32(&b, 1);
  put32(&b, 1);
  put32(&b, 512);
  put32(&b, 0);
  put32(&b, 0x10000);
  close_box(&b, at);
  close_box(&b, mvex);
  trak = open_box(&b, "trak");
  track_head(&b, 1);
  mdia = open_box(&b, "mdia");
  media_head(&b, 12800, "vide");
  minf = open_box(&b, "minf");
  stbl = open_box(&b, "stbl");
  avc1(&b);
  full_box(&b, "stts", 0, &at);
  put32(&b, 0);
  close_box(&b, at);
  full_box(&b, "stsz", 0, &at);
  put32(&b, 0);
  put32(&b, 0);
  close_box(&b, at);
  full_box(&b, "stsc", 0, &at);
  put32(&b, 0);
  close_box(&b, at);
  full_box(&b, "stco", 0, &at);
  put32(&b, 0);
  close_box(&b, at);
  close_box(&b, stbl);
  close_box(&b, minf);
  close_box(&b, mdia);
  close_box(&b, trak);
  close_box(&b, moov);

  moof = open_box(&b, "moof");
  full_box(&b, "mfhd", 0, &at);
  put32(&b, 1);
  close_box(&b, at);
  traf = open_box(&b, "traf");
  full_box(&b, "tfhd", 0x20000, &at);
  put32(&b, 1);
  close_box(&b, at);
  full_box(&b, "tfdt", 0x01000000, &at);
  put32(&b, 0);
  put32(&b, 1000);
  close_box(&b, at);
  full_box(&b, "trun", 0x000205, &at);
  put32(&b, 3);
  offset_at = b.n;
  put32(&b, 0);
  put32(&b, 0x02000000); // the first: depends on no other
  put32(&b, 100);
  put32(&b, 50);
  put32(&b, 25);
  close_box(&b, at);
  close_box(&b, traf);
  close_box(&b, moof);
  mdat = open_box(&b, "mdat");
  set32(&b, offset_at, (unsigned)(b.n - moof));
  put_zero(&b, 175);
  close_box(&b, mdat);

  CHECK(mp4_parse(&m, b.d, b.n) == MP4_OK);
  CHECK(m.video.found && m.video.count == 3 && !m.audio.found);
  if(m.video.count == 3) {
    CHECK(m.video.samples[0].offset == mdat + 8 && m.video.samples[1].offset == mdat + 108);
    CHECK(m.video.samples[2].size == 25 && m.video.samples[2].dts == 1000 + 2 * 512);
    CHECK(m.video.samples[0].key && !m.video.samples[1].key && !m.video.samples[2].key);
  }
  CHECK(m.duration_ms == (1000 + 3 * 512) * 1000 / 12800);
  mp4_free(&m);
}


int
main(void) {
  test_progressive();
  test_fragments();
  if(g_failed) {
    printf("%d mp4 checks failed\n", g_failed);
    return 1;
  }
  printf("all mp4 checks passed\n");
  return 0;
}
