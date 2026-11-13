/* The samples of an MP4 file, see mp4.h. The boxes follow ISO/IEC 14496-12
(the tables of a progressive file, and moof/traf/trun for fragments) and
14496-14/-1 for the esds descriptors of AAC. */

#include <stdlib.h>
#include <string.h>

#include "mp4.h"

#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

#define MAX_TREX 8

typedef struct span {
  const uint8_t *p;
  size_t         len;
} span_t;

// the tables of a progressive track's stbl, before its samples are made
typedef struct tables {
  span_t stts, ctts, stss, stsz, stsc, stco;
  int    co64;
} tables_t;

typedef struct trex {
  uint32_t id, duration, size, flags;
} trex_t;

typedef struct parse {
  const uint8_t *file;
  size_t         len;
  trex_t         trex[MAX_TREX];
  int            trex_count;
} parse_t;


static uint32_t
be16(const uint8_t *p) {
  return (uint32_t)p[0] << 8 | p[1];
}


static uint32_t
be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}


static uint64_t
be64(const uint8_t *p) {
  return (uint64_t)be32(p) << 32 | be32(p + 4);
}


/**
 * the box at *off of p: its type and body; moves *off past it. Returns 0 at
 * the end, or at a box that does not fit
 **/
static int
next_box(const uint8_t *p, size_t len, size_t *off, uint32_t *type, span_t *body) {
  size_t at = *off, head = 8;
  uint64_t size;

  if(at > len || len - at < 8) {
    return 0;
  }
  size = be32(p + at);
  *type = be32(p + at + 4);
  if(size == 1) {
    if(len - at < 16) {
      return 0;
    }
    size = be64(p + at + 8);
    head = 16;
  } else if(size == 0) {
    size = len - at;
  }
  if(size < head || size > len - at) {
    return 0;
  }
  body->p = p + at + head;
  body->len = (size_t)size - head;
  *off = at + (size_t)size;
  return 1;
}


/**
 * the first box of that type in span, or an empty span
 **/
static span_t
child(span_t s, uint32_t want) {
  span_t body = {NULL, 0};
  uint32_t type;
  size_t off = 0;

  while(next_box(s.p, s.len, &off, &type, &body)) {
    if(type == want) {
      return body;
    }
  }
  return (span_t){NULL, 0};
}


static int
add_sample(mp4_track_t *t, const mp4_sample_t *s) {
  mp4_sample_t *grown;
  int room;

  if(t->count >= MP4_MAX_SAMPLES) {
    return -1;
  }
  if(t->count == t->room) {
    room = t->room ? t->room * 2 : 256;
    room = room > MP4_MAX_SAMPLES ? MP4_MAX_SAMPLES : room;
    if(!(grown=realloc(t->samples, (size_t)room * sizeof *grown))) {
      return -1;
    }
    t->samples = grown;
    t->room = room;
  }
  t->samples[t->count++] = *s;
  return 0;
}


/*
 * The sample descriptions.
 */

/**
 * an MPEG-4 descriptor's tag and length at *at; moves *at to its body
 **/
static int
descriptor(span_t s, size_t *at, int *tag, size_t *len) {
  size_t n = 0;
  uint8_t b;

  if(*at >= s.len) {
    return 0;
  }
  *tag = s.p[(*at)++];
  for(int i=0; i<4; i++) {
    if(*at >= s.len) {
      return 0;
    }
    b = s.p[(*at)++];
    n = n << 7 | (b & 0x7f);
    if(!(b & 0x80)) {
      break;
    }
  }
  if(n > s.len - *at) {
    return 0;
  }
  *len = n;
  return 1;
}


/**
 * the AudioSpecificConfig in an esds box
 **/
static int
esds_config(span_t esds, mp4_track_t *t) {
  span_t es, dc;
  size_t at = 4, len, in;
  int tag, flags;

  if(esds.len < 4 || !descriptor(esds, &at, &tag, &len) || tag != 3) {
    return 0;
  }
  es = (span_t){esds.p + at, len};
  if(es.len < 3) {
    return 0;
  }
  flags = es.p[2];
  in = 3;
  if(flags & 0x80) {
    in += 2;
  }
  if(flags & 0x40) {
    if(in >= es.len) {
      return 0;
    }
    in += 1 + es.p[in];
  }
  if(flags & 0x20) {
    in += 2;
  }
  while(in < es.len && descriptor(es, &in, &tag, &len)) {
    if(tag == 4 && len >= 13) {
      dc = (span_t){es.p + in, len};
      at = 13;
      if(dc.p[0] != 0x40 && dc.p[0] != 0x66 && dc.p[0] != 0x67) { // MPEG-4 or MPEG-2 AAC
        return 0;
      }
      while(at < dc.len && descriptor(dc, &at, &tag, &len)) {
        if(tag == 5 && len) {
          t->config = dc.p + at;
          t->config_len = len;
          return 1;
        }
        at += len;
      }
      return 0;
    }
    in += len;
  }
  return 0;
}


/**
 * the first sample description: avc1 for video, mp4a for audio; returns
 * whether it is one of them
 **/
static int
sample_entry(span_t stsd, uint32_t handler, mp4_track_t *t) {
  span_t entry, avcc;
  uint32_t type;
  size_t off = 8, head;
  int version;

  if(stsd.len < 8 || !be32(stsd.p + 4) || !next_box(stsd.p, stsd.len, &off, &type, &entry)) {
    return 0;
  }
  if(handler == FOURCC('v', 'i', 'd', 'e') && (type == FOURCC('a', 'v', 'c', '1') ||
                                               type == FOURCC('a', 'v', 'c', '3'))) {
    if(entry.len < 78) {
      return 0;
    }
    t->width = (int)be16(entry.p + 24);
    t->height = (int)be16(entry.p + 26);
    avcc = child((span_t){entry.p + 78, entry.len - 78}, FOURCC('a', 'v', 'c', 'C'));
    if(avcc.len < 7) {
      return 0;
    }
    t->config = avcc.p;
    t->config_len = avcc.len;
    return 1;
  }
  if(handler == FOURCC('s', 'o', 'u', 'n') && type == FOURCC('m', 'p', '4', 'a')) {
    if(entry.len < 28) {
      return 0;
    }
    version = (int)be16(entry.p + 8);
    head = version == 1 ? 44 : version == 2 ? 64 : 28;
    t->channels = (int)be16(entry.p + 16);
    t->rate = (int)(be32(entry.p + 24) >> 16);
    if(entry.len < head) {
      return 0;
    }
    return esds_config(child((span_t){entry.p + head, entry.len - head}, FOURCC('e', 's', 'd', 's')), t);
  }
  return 0;
}


/*
 * A progressive track: its samples from the tables.
 */

static int
build_samples(mp4_track_t *t, const tables_t *tb, size_t file_len) {
  uint32_t count, fixed, chunks, entries, stsc_n, chunk_count, first, next_first, per, s = 0;
  uint32_t stts_n, stts_i = 0, stts_left = 0, delta = 0, ctts_n, ctts_i = 0, ctts_left = 0;
  int32_t cts = 0;
  uint64_t offset;
  int64_t dts = 0;
  mp4_sample_t sample;

  if(tb->stsz.len < 12 || tb->stsc.len < 8 || tb->stco.len < 8 || tb->stts.len < 8) {
    return MP4_BAD;
  }
  fixed = be32(tb->stsz.p + 4);
  count = be32(tb->stsz.p + 8);
  if(count > MP4_MAX_SAMPLES || (!fixed && (tb->stsz.len - 12) / 4 < count)) {
    return MP4_BAD;
  }
  chunk_count = be32(tb->stco.p + 4);
  if((tb->stco.len - 8) / (tb->co64 ? 8 : 4) < chunk_count) {
    return MP4_BAD;
  }
  stsc_n = be32(tb->stsc.p + 4);
  stts_n = be32(tb->stts.p + 4);
  ctts_n = tb->ctts.len >= 8 ? be32(tb->ctts.p + 4) : 0;
  if((tb->stsc.len - 8) / 12 < stsc_n || (tb->stts.len - 8) / 8 < stts_n ||
     (ctts_n && (tb->ctts.len - 8) / 8 < ctts_n)) {
    return MP4_BAD;
  }

  entries = 0;
  for(chunks=1; chunks <= chunk_count && s < count; chunks++) {
    // the stsc entry for this chunk
    while(entries + 1 < stsc_n && be32(tb->stsc.p + 8 + (entries + 1) * 12) <= chunks) {
      entries++;
    }
    if(!stsc_n) {
      return MP4_BAD;
    }
    first = be32(tb->stsc.p + 8 + entries * 12);
    next_first = entries + 1 < stsc_n ? be32(tb->stsc.p + 8 + (entries + 1) * 12) : 0xffffffffu;
    per = be32(tb->stsc.p + 8 + entries * 12 + 4);
    (void)first;
    (void)next_first;
    offset = tb->co64 ? be64(tb->stco.p + 8 + (size_t)(chunks - 1) * 8) :
                        be32(tb->stco.p + 8 + (size_t)(chunks - 1) * 4);
    for(uint32_t k=0; k < per && s < count; k++, s++) {
      memset(&sample, 0, sizeof sample);
      sample.size = fixed ? fixed : be32(tb->stsz.p + 12 + (size_t)s * 4);
      sample.offset = offset;
      offset += sample.size;
      while(!stts_left && stts_i < stts_n) {
        stts_left = be32(tb->stts.p + 8 + (size_t)stts_i * 8);
        delta = be32(tb->stts.p + 8 + (size_t)stts_i * 8 + 4);
        stts_i++;
      }
      while(ctts_n && !ctts_left && ctts_i < ctts_n) {
        ctts_left = be32(tb->ctts.p + 8 + (size_t)ctts_i * 8);
        cts = (int32_t)be32(tb->ctts.p + 8 + (size_t)ctts_i * 8 + 4);
        ctts_i++;
      }
      sample.dts = dts;
      sample.cts = ctts_left ? cts : 0;
      sample.key = tb->stss.len < 8;
      dts += delta;
      stts_left -= stts_left ? 1 : 0;
      ctts_left -= ctts_left ? 1 : 0;
      if(sample.offset + sample.size <= file_len && add_sample(t, &sample)) {
        return MP4_MEMORY;
      }
    }
  }
  t->next_dts = dts;

  // the key frames, by their numbers from 1
  if(tb->stss.len >= 8) {
    uint32_t n = be32(tb->stss.p + 4), number;
    if((tb->stss.len - 8) / 4 < n) {
      return MP4_BAD;
    }
    for(uint32_t i=0; i < n; i++) {
      number = be32(tb->stss.p + 8 + (size_t)i * 4);
      if(number >= 1 && number <= (uint32_t)t->count) {
        t->samples[number - 1].key = 1;
      }
    }
  }
  return MP4_OK;
}


/**
 * one trak: the video or the audio track, if it is the first of its kind
 **/
static int
parse_trak(mp4_t *m, const parse_t *ps, span_t trak) {
  span_t tkhd = child(trak, FOURCC('t', 'k', 'h', 'd')), mdia = child(trak, FOURCC('m', 'd', 'i', 'a'));
  span_t mdhd = child(mdia, FOURCC('m', 'd', 'h', 'd')), hdlr = child(mdia, FOURCC('h', 'd', 'l', 'r'));
  span_t stbl = child(child(mdia, FOURCC('m', 'i', 'n', 'f')), FOURCC('s', 't', 'b', 'l'));
  uint32_t handler;
  mp4_track_t t, *into;
  tables_t tb;
  int ret;

  if(tkhd.len < 24 || mdhd.len < 24 || hdlr.len < 12 || !stbl.p) {
    return MP4_OK;
  }
  handler = be32(hdlr.p + 8);
  into = handler == FOURCC('v', 'i', 'd', 'e') ? &m->video : handler == FOURCC('s', 'o', 'u', 'n') ? &m->audio : NULL;
  if(!into || into->found) {
    return MP4_OK;
  }
  memset(&t, 0, sizeof t);
  t.id = tkhd.p[0] == 1 ? (tkhd.len >= 24 ? be32(tkhd.p + 20) : 0) : be32(tkhd.p + 12);
  t.timescale = mdhd.p[0] == 1 ? (mdhd.len >= 24 ? be32(mdhd.p + 20) : 0) : be32(mdhd.p + 12);
  if(!t.timescale || !sample_entry(child(stbl, FOURCC('s', 't', 's', 'd')), handler, &t)) {
    return MP4_OK;
  }
  for(int i=0; i < ps->trex_count; i++) {
    if(ps->trex[i].id == t.id) {
      t.def_duration = ps->trex[i].duration;
      t.def_size = ps->trex[i].size;
      t.def_flags = ps->trex[i].flags;
    }
  }

  memset(&tb, 0, sizeof tb);
  tb.stts = child(stbl, FOURCC('s', 't', 't', 's'));
  tb.ctts = child(stbl, FOURCC('c', 't', 't', 's'));
  tb.stss = child(stbl, FOURCC('s', 't', 's', 's'));
  tb.stsz = child(stbl, FOURCC('s', 't', 's', 'z'));
  tb.stsc = child(stbl, FOURCC('s', 't', 's', 'c'));
  tb.stco = child(stbl, FOURCC('s', 't', 'c', 'o'));
  if(!tb.stco.p) {
    tb.stco = child(stbl, FOURCC('c', 'o', '6', '4'));
    tb.co64 = 1;
  }
  // a fragmented file's moov has empty tables
  if(tb.stsz.len >= 12 && be32(tb.stsz.p + 8) && (ret=build_samples(&t, &tb, ps->len))) {
    free(t.samples);
    return ret;
  }
  t.found = 1;
  *into = t;
  return MP4_OK;
}


static int
parse_moov(mp4_t *m, parse_t *ps, span_t moov) {
  span_t body, mvex = child(moov, FOURCC('m', 'v', 'e', 'x'));
  uint32_t type;
  size_t off = 0;
  int ret;

  // the fragments' defaults first, for the tracks
  while(mvex.p && next_box(mvex.p, mvex.len, &off, &type, &body)) {
    if(type == FOURCC('t', 'r', 'e', 'x') && body.len >= 24 && ps->trex_count < MAX_TREX) {
      ps->trex[ps->trex_count++] = (trex_t){be32(body.p + 4), be32(body.p + 12), be32(body.p + 16),
                                            be32(body.p + 20)};
    }
  }
  off = 0;
  while(next_box(moov.p, moov.len, &off, &type, &body)) {
    if(type == FOURCC('t', 'r', 'a', 'k') && (ret=parse_trak(m, ps, body))) {
      return ret;
    }
  }
  return MP4_OK;
}


/*
 * Fragments.
 */

static int
parse_traf(mp4_t *m, const parse_t *ps, span_t traf, size_t moof_at) {
  span_t tfhd = child(traf, FOURCC('t', 'f', 'h', 'd')), tfdt = child(traf, FOURCC('t', 'f', 'd', 't'));
  span_t body;
  uint32_t flags, id, duration, size, sflags, n, type, tflags, first_flags = 0;
  uint64_t base, data;
  mp4_track_t *t;
  mp4_sample_t s;
  size_t off = 0, at;
  int64_t dts;

  if(tfhd.len < 8) {
    return MP4_OK;
  }
  flags = be32(tfhd.p) & 0xffffff;
  id = be32(tfhd.p + 4);
  t = m->video.found && m->video.id == id ? &m->video : m->audio.found && m->audio.id == id ? &m->audio : NULL;
  if(!t) {
    return MP4_OK;
  }
  at = 8;
  base = moof_at;
  duration = t->def_duration;
  size = t->def_size;
  sflags = t->def_flags;
  if(flags & 0x1) {
    if(tfhd.len < at + 8) {
      return MP4_BAD;
    }
    base = be64(tfhd.p + at);
    at += 8;
  }
  at += flags & 0x2 ? 4 : 0;
  if(flags & 0x8) {
    if(tfhd.len < at + 4) {
      return MP4_BAD;
    }
    duration = be32(tfhd.p + at);
    at += 4;
  }
  if(flags & 0x10) {
    if(tfhd.len < at + 4) {
      return MP4_BAD;
    }
    size = be32(tfhd.p + at);
    at += 4;
  }
  if(flags & 0x20) {
    if(tfhd.len < at + 4) {
      return MP4_BAD;
    }
    sflags = be32(tfhd.p + at);
  }
  dts = t->next_dts;
  if(tfdt.len >= 8) {
    dts = tfdt.p[0] == 1 ? (tfdt.len >= 12 ? (int64_t)be64(tfdt.p + 4) : dts) : (int64_t)be32(tfdt.p + 4);
  }

  data = base;
  while(next_box(traf.p, traf.len, &off, &type, &body)) {
    if(type != FOURCC('t', 'r', 'u', 'n') || body.len < 8) {
      continue;
    }
    tflags = be32(body.p) & 0xffffff;
    n = be32(body.p + 4);
    at = 8;
    if(tflags & 0x1) {
      if(body.len < at + 4) {
        return MP4_BAD;
      }
      data = base + (uint64_t)(int64_t)(int32_t)be32(body.p + at);
      at += 4;
    }
    if(tflags & 0x4) {
      if(body.len < at + 4) {
        return MP4_BAD;
      }
      first_flags = be32(body.p + at);
      at += 4;
    }
    for(uint32_t i=0; i < n; i++) {
      uint32_t d = duration, sz = size, fl = i == 0 && (tflags & 0x4) ? first_flags : sflags;
      int32_t cts = 0;
      size_t need = (tflags & 0x100 ? 4 : 0) + (tflags & 0x200 ? 4 : 0) + (tflags & 0x400 ? 4 : 0) +
                    (tflags & 0x800 ? 4 : 0);
      if(body.len < at + need) {
        return MP4_BAD;
      }
      if(tflags & 0x100) {
        d = be32(body.p + at);
        at += 4;
      }
      if(tflags & 0x200) {
        sz = be32(body.p + at);
        at += 4;
      }
      if(tflags & 0x400) {
        fl = be32(body.p + at);
        at += 4;
      }
      if(tflags & 0x800) {
        cts = (int32_t)be32(body.p + at);
        at += 4;
      }
      memset(&s, 0, sizeof s);
      s.offset = data;
      s.size = sz;
      s.dts = dts;
      s.cts = cts;
      // not a sync sample, or one that depends on others: not a key frame
      s.key = t == &m->audio || (!(fl & 0x10000) && ((fl >> 24) & 3) != 1);
      data += sz;
      dts += d;
      if(s.offset + s.size <= ps->len && add_sample(t, &s)) {
        return MP4_MEMORY;
      }
    }
  }
  t->next_dts = dts;
  return MP4_OK;
}


static int
duration_ms(const mp4_track_t *t) {
  return t->found && t->timescale ? (int)(t->next_dts * 1000 / t->timescale) : 0;
}


int
mp4_parse(mp4_t *m, const uint8_t *data, size_t len) {
  parse_t ps;
  span_t body;
  uint32_t type;
  size_t off = 0, at;
  int ret = MP4_NO_MOOV, a, v;

  memset(m, 0, sizeof *m);
  memset(&ps, 0, sizeof ps);
  ps.file = data;
  ps.len = len;
  while(next_box(data, len, &off, &type, &body)) {
    if(type == FOURCC('m', 'o', 'o', 'v')) {
      ret = parse_moov(m, &ps, body);
      break;
    }
  }
  if(ret) {
    mp4_free(m);
    return ret;
  }
  if(!m->video.found && !m->audio.found) {
    mp4_free(m);
    return MP4_NO_TRACK;
  }

  // the fragments, in the file's order
  off = 0;
  for(at=off; next_box(data, len, &off, &type, &body); at=off) {
    if(type != FOURCC('m', 'o', 'o', 'f')) {
      continue;
    }
    size_t in = 0;
    span_t traf;
    while(next_box(body.p, body.len, &in, &type, &traf)) {
      if(type == FOURCC('t', 'r', 'a', 'f') && (ret=parse_traf(m, &ps, traf, at))) {
        mp4_free(m);
        return ret;
      }
    }
  }
  if(m->video.found && !m->video.count) {
    m->video.found = 0;
  }
  if(m->audio.found && !m->audio.count) {
    m->audio.found = 0;
  }
  if(!m->video.found && !m->audio.found) {
    mp4_free(m);
    return MP4_NO_TRACK;
  }
  v = duration_ms(&m->video);
  a = duration_ms(&m->audio);
  m->duration_ms = v > a ? v : a;
  return MP4_OK;
}


void
mp4_free(mp4_t *m) {
  free(m->video.samples);
  free(m->audio.samples);
  memset(m, 0, sizeof *m);
}


int
mp4_key_before(const mp4_track_t *t, int64_t ms) {
  int64_t want = ms * (int64_t)t->timescale / 1000;
  int best = 0;

  for(int i=0; i < t->count; i++) {
    if(t->samples[i].dts > want) {
      break;
    }
    if(t->samples[i].key) {
      best = i;
    }
  }
  return best;
}
