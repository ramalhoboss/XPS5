/* A clip cut out of an MP4 without encoding it again, see clip.h. The boxes
written follow ISO/IEC 14496-12 (a progressive file: each sample a chunk of
its own, with 64-bit offsets) and 14496-1 for the esds of AAC. */

#include <stdlib.h>
#include <string.h>

#include "clip.h"

typedef struct out {
  uint8_t *p;
  size_t   len, room;
  int      fail;
} out_t;

// a track's kept samples [first, end) as they go in the new file
typedef struct kept {
  const mp4_track_t *t;
  int                first, end;
  int64_t            end_dts;  // where the last of them ends
  int32_t            cts_add;  // added to each one's composition offset
  size_t             co64_at;  // its offsets in the head, made whole once the head is
} kept_t;


static void
put(out_t *o, const void *data, size_t n) {
  if(o->fail) {
    return;
  }
  if(o->room - o->len < n) {
    size_t room = o->room ? o->room : 4096;
    uint8_t *p;
    while(room - o->len < n) {
      room *= 2;
    }
    if(!(p=realloc(o->p, room))) {
      o->fail = 1;
      return;
    }
    o->p = p;
    o->room = room;
  }
  memcpy(o->p + o->len, data, n);
  o->len += n;
}


static void
u8(out_t *o, unsigned v) {
  uint8_t b = (uint8_t)v;
  put(o, &b, 1);
}


static void
u16(out_t *o, unsigned v) {
  uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
  put(o, b, 2);
}


static void
u32(out_t *o, uint32_t v) {
  uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
  put(o, b, 4);
}


static void
u64(out_t *o, uint64_t v) {
  u32(o, (uint32_t)(v >> 32));
  u32(o, (uint32_t)v);
}


static void
zeros(out_t *o, size_t n) {
  static const uint8_t none[32];
  while(n) {
    size_t k = n < sizeof none ? n : sizeof none;
    put(o, none, k);
    n -= k;
  }
}


static void
set32(out_t *o, size_t at, uint32_t v) {
  if(!o->fail) {
    o->p[at] = (uint8_t)(v >> 24);
    o->p[at + 1] = (uint8_t)(v >> 16);
    o->p[at + 2] = (uint8_t)(v >> 8);
    o->p[at + 3] = (uint8_t)v;
  }
}


static uint64_t
get64(const out_t *o, size_t at) {
  uint64_t v = 0;
  for(int i=0; i < 8; i++) {
    v = v << 8 | o->p[at + i];
  }
  return v;
}


static void
set64(out_t *o, size_t at, uint64_t v) {
  set32(o, at, (uint32_t)(v >> 32));
  set32(o, at + 4, (uint32_t)v);
}


/**
 * a box begun: its size written when it ends (box_end)
 **/
static size_t
box(out_t *o, const char *type) {
  const size_t at = o->len;
  u32(o, 0);
  put(o, type, 4);
  return at;
}


static size_t
full_box(out_t *o, const char *type, unsigned version, uint32_t flags) {
  const size_t at = box(o, type);
  u32(o, (uint32_t)version << 24 | flags);
  return at;
}


static void
box_end(out_t *o, size_t at) {
  set32(o, at, (uint32_t)(o->len - at));
}


static void
matrix(out_t *o) {
  static const uint32_t unit[9] = {0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000};
  for(int i=0; i < 9; i++) {
    u32(o, unit[i]);
  }
}


int
clip_box(const uint8_t *head, size_t got, uint64_t at, uint64_t file_len, uint32_t *type, uint64_t *size,
         unsigned *head_len) {
  uint64_t n;

  if(got < 8 || at > file_len || file_len - at < 8) {
    return -1;
  }
  n = (uint64_t)head[0] << 24 | (uint64_t)head[1] << 16 | (uint64_t)head[2] << 8 | head[3];
  *type = CLIP_FOURCC(head[4], head[5], head[6], head[7]);
  *head_len = 8;
  if(n == 1) {
    if(got < 16 || file_len - at < 16) {
      return -1;
    }
    n = 0;
    for(int i=8; i < 16; i++) {
      n = n << 8 | head[i];
    }
    *head_len = 16;
  } else if(n == 0) {
    n = file_len - at;
  }
  if(n < *head_len || n > file_len - at) {
    return -1;
  }
  *size = n;
  return 0;
}


/**
 * where sample i of k ends in its track's time
 **/
static int64_t
sample_end(const kept_t *k, int i) {
  return i + 1 < k->end ? k->t->samples[i + 1].dts : k->end_dts;
}


/**
 * the tables of k's samples, each a chunk of its own; its offsets are made
 * whole once the head is
 **/
static void
sample_tables(out_t *o, kept_t *k, int video, uint64_t from) {
  const mp4_sample_t *s = k->t->samples;
  const int n = k->end - k->first;
  size_t at, count_at;
  uint32_t entries = 0, run = 0;
  int64_t delta = -1, d;
  int32_t cts = 0;
  int any = 0, negative = 0, all_keys = 1;

  // how long each is, in runs
  at = full_box(o, "stts", 0, 0);
  count_at = o->len;
  u32(o, 0);
  for(int i=k->first; i < k->end; i++) {
    d = sample_end(k, i) - s[i].dts;
    d = d < 0 ? 0 : d > 0xffffffff ? 0xffffffff : d;
    if(run && d == delta) {
      run++;
      continue;
    }
    if(run) {
      u32(o, run);
      u32(o, (uint32_t)delta);
      entries++;
    }
    run = 1;
    delta = d;
  }
  u32(o, run);
  u32(o, (uint32_t)delta);
  set32(o, count_at, entries + 1);
  box_end(o, at);

  // when each shows, past when it is decoded, when that differs
  for(int i=k->first; i < k->end; i++) {
    any |= s[i].cts + k->cts_add != 0;
    negative |= s[i].cts + k->cts_add < 0;
    all_keys &= s[i].key;
  }
  if(any) {
    at = full_box(o, "ctts", negative ? 1 : 0, 0);
    count_at = o->len;
    u32(o, 0);
    entries = run = 0;
    for(int i=k->first; i < k->end; i++) {
      const int32_t c = s[i].cts + k->cts_add;
      if(run && c == cts) {
        run++;
        continue;
      }
      if(run) {
        u32(o, run);
        u32(o, (uint32_t)cts);
        entries++;
      }
      run = 1;
      cts = c;
    }
    u32(o, run);
    u32(o, (uint32_t)cts);
    set32(o, count_at, entries + 1);
    box_end(o, at);
  }

  // where playing may start: its key frames (none listed, all are)
  if(video && !all_keys) {
    at = full_box(o, "stss", 0, 0);
    count_at = o->len;
    u32(o, 0);
    entries = 0;
    for(int i=k->first; i < k->end; i++) {
      if(s[i].key) {
        u32(o, (uint32_t)(i - k->first + 1));
        entries++;
      }
    }
    set32(o, count_at, entries);
    box_end(o, at);
  }

  at = full_box(o, "stsz", 0, 0);
  u32(o, 0);
  u32(o, (uint32_t)n);
  for(int i=k->first; i < k->end; i++) {
    u32(o, s[i].size);
  }
  box_end(o, at);
  at = full_box(o, "stsc", 0, 0);
  u32(o, 1);
  u32(o, 1); // from the first chunk on,
  u32(o, 1); // a sample each,
  u32(o, 1); // of the first sample entry
  box_end(o, at);
  at = full_box(o, "co64", 0, 0);
  u32(o, (uint32_t)n);
  k->co64_at = o->len;
  for(int i=k->first; i < k->end; i++) {
    u64(o, s[i].offset - from); // in the range; the head's length added later
  }
  box_end(o, at);
}


static void
handler(out_t *o, const char *type, const char *name) {
  const size_t at = full_box(o, "hdlr", 0, 0);
  u32(o, 0);
  put(o, type, 4);
  zeros(o, 12);
  put(o, name, strlen(name) + 1);
  box_end(o, at);
}


static void
data_info(out_t *o) {
  const size_t dinf = box(o, "dinf"), dref = full_box(o, "dref", 0, 0);
  size_t url;
  u32(o, 1);
  url = full_box(o, "url ", 0, 1); // in this file
  box_end(o, url);
  box_end(o, dref);
  box_end(o, dinf);
}


static void
track_head(out_t *o, unsigned id, int64_t ms, int audio, int width, int height) {
  const size_t at = full_box(o, "tkhd", 0, 3); // enabled, in the movie
  u32(o, 0);
  u32(o, 0);
  u32(o, id);
  u32(o, 0);
  u32(o, (uint32_t)ms);
  zeros(o, 8);
  u16(o, 0);           // layer
  u16(o, 0);           // alternate_group
  u16(o, audio ? 0x100 : 0);
  u16(o, 0);
  matrix(o);
  u32(o, (uint32_t)width << 16);
  u32(o, (uint32_t)height << 16);
  box_end(o, at);
}


static void
media_head(out_t *o, uint32_t timescale, int64_t duration) {
  const size_t at = full_box(o, "mdhd", 1, 0);
  u64(o, 0);
  u64(o, 0);
  u32(o, timescale);
  u64(o, (uint64_t)(duration < 0 ? 0 : duration));
  u16(o, 0x55c4); // "und"
  u16(o, 0);
  box_end(o, at);
}


/**
 * an edit list: empty for wait_ms first (when above 0), then the media
 * from media_time for ms
 **/
static void
edits(out_t *o, int64_t wait_ms, int64_t media_time, int64_t ms) {
  const size_t edts = box(o, "edts"), at = full_box(o, "elst", 0, 0);
  u32(o, wait_ms > 0 ? 2 : 1);
  if(wait_ms > 0) {
    u32(o, (uint32_t)wait_ms);
    u32(o, 0xffffffffu); // nothing
    u32(o, 0x00010000);
  }
  u32(o, (uint32_t)ms);
  u32(o, (uint32_t)media_time);
  u32(o, 0x00010000);
  box_end(o, at);
  box_end(o, edts);
}


static void
video_track(out_t *o, kept_t *k, int64_t media_time, int64_t ms, uint64_t from) {
  const mp4_track_t *t = k->t;
  size_t trak = box(o, "trak"), mdia, minf, stbl, stsd, avc1, at;

  track_head(o, 1, ms, 0, t->width, t->height);
  if(media_time > 0) {
    edits(o, 0, media_time, ms); // what B-frames put off, left out
  }
  mdia = box(o, "mdia");
  media_head(o, t->timescale, k->end_dts - t->samples[k->first].dts);
  handler(o, "vide", "VideoHandler");
  minf = box(o, "minf");
  at = full_box(o, "vmhd", 0, 1);
  zeros(o, 8);
  box_end(o, at);
  data_info(o);
  stbl = box(o, "stbl");
  stsd = full_box(o, "stsd", 0, 0);
  u32(o, 1);
  avc1 = box(o, "avc1");
  zeros(o, 6);          // reserved
  u16(o, 1);            // data_reference_index
  zeros(o, 16);         // pre_defined, reserved
  u16(o, (unsigned)t->width);
  u16(o, (unsigned)t->height);
  u32(o, 0x00480000);   // 72 dpi
  u32(o, 0x00480000);
  u32(o, 0);
  u16(o, 1);            // frame_count
  zeros(o, 32);         // compressorname
  u16(o, 0x18);         // depth
  u16(o, 0xffff);       // pre_defined -1
  at = box(o, "avcC");
  put(o, t->config, t->config_len); // as the old file has it
  box_end(o, at);
  box_end(o, avc1);
  box_end(o, stsd);
  sample_tables(o, k, 1, from);
  box_end(o, stbl);
  box_end(o, minf);
  box_end(o, mdia);
  box_end(o, trak);
}


static void
audio_track(out_t *o, kept_t *k, int64_t wait_ms, int64_t ms, uint64_t from) {
  const mp4_track_t *t = k->t;
  const unsigned n = (unsigned)t->config_len;
  size_t trak = box(o, "trak"), mdia, minf, stbl, stsd, mp4a, at;

  track_head(o, 2, wait_ms + ms, 1, 0, 0);
  if(wait_ms > 0) {
    edits(o, wait_ms, 0, ms); // starts as late as its first frame does after the video's
  }
  mdia = box(o, "mdia");
  media_head(o, t->timescale, k->end_dts - t->samples[k->first].dts);
  handler(o, "soun", "SoundHandler");
  minf = box(o, "minf");
  at = full_box(o, "smhd", 0, 0);
  u32(o, 0);
  box_end(o, at);
  data_info(o);
  stbl = box(o, "stbl");
  stsd = full_box(o, "stsd", 0, 0);
  u32(o, 1);
  mp4a = box(o, "mp4a");
  zeros(o, 6);
  u16(o, 1);            // data_reference_index
  zeros(o, 8);
  u16(o, t->channels > 0 ? (unsigned)t->channels : 2);
  u16(o, 16);
  u32(o, 0);
  u32(o, (t->rate > 0 && t->rate < 65536 ? (uint32_t)t->rate : t->timescale < 65536 ? t->timescale : 0) << 16);
  at = full_box(o, "esds", 0, 0);
  u8(o, 3);             // ES_Descriptor
  u8(o, 3 + 2 + 13 + 2 + n + 3);
  u16(o, 2);            // ES_ID
  u8(o, 0);
  u8(o, 4);             // DecoderConfigDescriptor
  u8(o, 13 + 2 + n);
  u8(o, 0x40);          // MPEG-4 audio
  u8(o, 0x15);          // an audio stream
  u8(o, 0);             // bufferSizeDB
  u16(o, 0);
  u32(o, 0);            // maxBitrate, avgBitrate: not said
  u32(o, 0);
  u8(o, 5);             // DecoderSpecificInfo: the old file's AudioSpecificConfig
  u8(o, n);
  put(o, t->config, n);
  u8(o, 6);             // SLConfigDescriptor
  u8(o, 1);
  u8(o, 2);
  box_end(o, at);
  box_end(o, mp4a);
  box_end(o, stsd);
  sample_tables(o, k, 0, from);
  box_end(o, stbl);
  box_end(o, minf);
  box_end(o, mdia);
  box_end(o, trak);
}


int
clip_make(const mp4_t *m, int64_t from_ms, int64_t to_ms, clip_t *c) {
  const mp4_track_t *v = &m->video, *a = &m->audio;
  kept_t kv, ka;
  int64_t d0, p0, span, want, media_time, a_from, a_to, wait_ms = 0, audio_ms = 0;
  uint64_t from, to;
  int audio;
  size_t moov, at;
  out_t o;

  memset(c, 0, sizeof *c);
  memset(&kv, 0, sizeof kv);
  memset(&ka, 0, sizeof ka);
  memset(&o, 0, sizeof o);
  if(!v->found || v->count <= 0 || !v->timescale || !v->config || v->config_len > 0xffff ||
     v->width <= 0 || v->height <= 0) {
    return CLIP_NO_VIDEO;
  }
  from_ms = from_ms < 0 ? 0 : from_ms;
  if(to_ms <= from_ms) {
    return CLIP_EMPTY;
  }

  // the video: from its key frame at or before the start, in decoding
  // order to the end (each frame's references come before it in that order)
  kv.t = v;
  kv.first = mp4_key_before(v, from_ms);
  want = to_ms > INT64_MAX / v->timescale ? INT64_MAX : to_ms * v->timescale / 1000;
  for(kv.end=kv.first + 1; kv.end < v->count && v->samples[kv.end].dts < want; kv.end++) {
  }
  kv.end_dts = kv.end < v->count ? v->samples[kv.end].dts : v->next_dts;
  if(kv.end_dts <= v->samples[kv.end - 1].dts) {
    kv.end_dts = v->samples[kv.end - 1].dts + (kv.end - 1 > kv.first ?
                 v->samples[kv.end - 1].dts - v->samples[kv.end - 2].dts : v->timescale / 30);
  }
  d0 = v->samples[kv.first].dts;
  p0 = INT64_MAX;
  for(int i=kv.first; i < kv.end; i++) {
    p0 = v->samples[i].dts + v->samples[i].cts < p0 ? v->samples[i].dts + v->samples[i].cts : p0;
  }
  // the first frame shown starts it: put off by an edit, or offsets made
  // whole when they were below the first decoded
  media_time = p0 - d0;
  if(media_time < 0) {
    kv.cts_add = (int32_t)-media_time;
    media_time = 0;
  }
  span = kv.end_dts - d0 - media_time;
  if(span * 1000 < (int64_t)CLIP_SHORTEST * v->timescale) {
    return CLIP_EMPTY;
  }
  c->start_ms = p0 * 1000 / v->timescale;
  c->length_ms = span * 1000 / v->timescale;

  // the audio of the same time: from its first frame at or after the first
  // shown, which starts that much later
  audio = a->found && a->count > 0 && a->timescale && a->config && a->config_len > 0 && a->config_len < 100;
  if(audio) {
    ka.t = a;
    a_from = p0 * a->timescale / v->timescale;
    a_to = (p0 + span) * a->timescale / v->timescale;
    for(ka.first=0; ka.first < a->count && a->samples[ka.first].dts < a_from; ka.first++) {
    }
    for(ka.end=ka.first; ka.end < a->count && a->samples[ka.end].dts < a_to; ka.end++) {
    }
    audio = ka.end > ka.first;
  }
  if(audio) {
    ka.end_dts = ka.end < a->count ? a->samples[ka.end].dts : a->next_dts;
    if(ka.end_dts <= a->samples[ka.end - 1].dts) {
      ka.end_dts = a->samples[ka.end - 1].dts + 1024; // an AAC frame
    }
    wait_ms = a->samples[ka.first].dts * 1000 / a->timescale - c->start_ms;
    wait_ms = wait_ms < 0 ? 0 : wait_ms;
    audio_ms = (ka.end_dts - a->samples[ka.first].dts) * 1000 / a->timescale;
  }

  // the old file's bytes that hold them all
  from = UINT64_MAX;
  to = 0;
  for(int i=kv.first; i < kv.end; i++) {
    from = v->samples[i].offset < from ? v->samples[i].offset : from;
    to = v->samples[i].offset + v->samples[i].size > to ? v->samples[i].offset + v->samples[i].size : to;
  }
  for(int i=ka.first; audio && i < ka.end; i++) {
    from = a->samples[i].offset < from ? a->samples[i].offset : from;
    to = a->samples[i].offset + a->samples[i].size > to ? a->samples[i].offset + a->samples[i].size : to;
  }

  // the head
  put(&o, "\0\0\0\x1c" "ftyp" "isom" "\0\0\x02\0" "isom" "iso2" "avc1", 28);
  moov = box(&o, "moov");
  at = full_box(&o, "mvhd", 0, 0);
  u32(&o, 0);
  u32(&o, 0);
  u32(&o, 1000);        // milliseconds
  u32(&o, (uint32_t)(c->length_ms > wait_ms + audio_ms ? c->length_ms : wait_ms + audio_ms));
  u32(&o, 0x00010000);  // rate
  u16(&o, 0x0100);      // volume
  zeros(&o, 10);
  matrix(&o);
  zeros(&o, 24);
  u32(&o, audio ? 3 : 2); // next_track_ID
  box_end(&o, at);
  video_track(&o, &kv, media_time, c->length_ms, from);
  if(audio) {
    audio_track(&o, &ka, wait_ms, audio_ms, from);
  }
  box_end(&o, moov);
  if(to - from + 8 <= 0xffffffffu) {
    u32(&o, (uint32_t)(to - from + 8));
    put(&o, "mdat", 4);
  } else {
    u32(&o, 1);
    put(&o, "mdat", 4);
    u64(&o, to - from + 16);
  }
  if(o.fail) {
    free(o.p);
    return CLIP_MEMORY;
  }
  // the samples' offsets, past the head
  for(int i=0; i < kv.end - kv.first; i++) {
    set64(&o, kv.co64_at + (size_t)i * 8, get64(&o, kv.co64_at + (size_t)i * 8) + o.len);
  }
  for(int i=0; audio && i < ka.end - ka.first; i++) {
    set64(&o, ka.co64_at + (size_t)i * 8, get64(&o, ka.co64_at + (size_t)i * 8) + o.len);
  }
  c->head = o.p;
  c->head_len = o.len;
  c->from = from;
  c->to = to;
  return CLIP_OK;
}


uint64_t
clip_size(const clip_t *c) {
  return c->head_len + (c->to - c->from);
}


void
clip_free(clip_t *c) {
  free(c->head);
  memset(c, 0, sizeof *c);
}


void
clip_span(int which, int64_t duration_ms, int64_t *from, int64_t *to) {
  const int64_t last = which == CLIP_LAST_30 ? 30000 : which == CLIP_LAST_15 ? 15000 : 0;

  *from = 0;
  *to = duration_ms;
  if(last && duration_ms > last) {
    *from = duration_ms - last;
  } else if(which == CLIP_FIRST_30 && duration_ms > 30000) {
    *to = 30000;
  }
}
