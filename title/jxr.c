/* The PS5's HDR screenshots (JPEG XR) in SDR, see jxr.h. The pixels come
as the file keeps them; each format is read as light relative to SDR's
white (1.0):
- half and float (scRGB, BT.709, 1.0 = 80 nits): times 80/203, 203 nits
  being HDR's reference white (BT.2408), where a game's paper white sits;
- 10 bits a channel (the PS5's own output: PQ, BT.2020): PQ's nits over
  203, then BT.709's primaries;
- fixed point: as float;
- 8 and 16 bits: already SDR, sRGB.
Then the luminance keeps its line up to a knee and bends above it so the
brightest part of the picture (its 99.9th percentile) reaches white; the
colours follow it. A colour with a channel still over white comes into SDR
darkened, its hue kept, when it was no brighter than the reference white (a
colour outside BT.709), and towards white the brighter it was (a highlight,
as bright light looks); sRGB's curve makes the bytes.

The gain map (Ultra HDR's, one channel) keeps, for each square of
JXR_GAIN_SCALE pixels, the mean log2 of (HDR luminance + 1/64) / (SDR
luminance + 1/64), from 0 up to the picture's white, so an HDR screen can
bring the SDR picture back to the screenshot's light. */

#include <math.h>
#include <string.h>

#include "jxr.h"
#include "mem.h"

// tools/jxrlib/xps5_jxr.c, in the jxrlib archive
int xps5_jxr_info(const void *data, size_t len, int *w, int *h, int *format, int *bits);
int xps5_jxr_decode(const void *data, size_t len, int shrink, void *out, size_t room,
                    int *w, int *h, int *stride);

#define REF_NITS  203.0f
#define KNEE      0.75f   // up to here the light stays as it is
#define WHITE_MAX 40.0f   // the brightest a picture's white is taken as (8000 nits)
#define HIST_BINS 512
#define HIST_LOW  -12.0f  // log2 of the dimmest luminance counted
#define HIST_HIGH 8.0f
#define OETF_SIZE 4096
#define GAIN_OFFSET (1.0f / 64) // Ultra HDR's offsets of the SDR and HDR light
#define GAIN_LEAST  1.1f        // the least white (over SDR's) worth a gain map

enum {
  F_GRAY8 = 0x08, F_BGR24 = 0x0c, F_RGB24 = 0x0d, F_BGR32 = 0x0e, F_BGRA32 = 0x0f, F_PBGRA32 = 0x10,
  F_RGB48_FIXED = 0x12, F_RGB101010 = 0x14, F_RGB48 = 0x15, F_RGBA64 = 0x16, F_PRGBA64 = 0x17,
  F_RGB96_FIXED = 0x18, F_RGBA128_FLOAT = 0x19, F_PRGBA128_FLOAT = 0x1a, F_RGB128_FLOAT = 0x1b,
  F_RGBA64_FIXED = 0x1d, F_RGBA128_FIXED = 0x1e, F_RGBA64_HALF = 0x3a, F_RGB48_HALF = 0x3b,
  F_RGB64_FIXED = 0x40, F_RGB128_FIXED = 0x41, F_RGB64_HALF = 0x42, F_RGB96_FLOAT = 0x127,
};


int
jxr_is(const void *data, size_t len) {
  const unsigned char *p = data;

  return data && len >= 8 && p[0] == 'I' && p[1] == 'I' && p[2] == 0xbc;
}


int
jxr_size(const void *data, size_t len, int *w, int *h, int *format) {
  int bits;

  return jxr_is(data, len) && !xps5_jxr_info(data, len, w, h, format, &bits) && *w > 0 && *h > 0 ? 0 : -1;
}


static float
half(uint16_t h) {
  uint32_t s = (uint32_t)(h & 0x8000) << 16, e = (h >> 10) & 0x1f, m = h & 0x3ff, bits;
  float f;

  if(!e) {
    f = (float)m * (1.0f / 16777216.0f); // subnormal: m * 2^-24
    return s ? -f : f;
  }
  bits = e == 31 ? s | 0x7f800000u | (m << 13) : s | ((e + 112) << 23) | (m << 13);
  memcpy(&f, &bits, sizeof f);
  return f;
}


static float
f32(const unsigned char *p) {
  float f;

  memcpy(&f, p, sizeof f);
  return f;
}


static int32_t
i32(const unsigned char *p) {
  int32_t v;

  memcpy(&v, p, sizeof v);
  return v;
}


static uint16_t
u16(const unsigned char *p) {
  return (uint16_t)(p[0] | p[1] << 8);
}


typedef struct look {
  int   format;
  int   bytes;      // of a pixel
  int   hdr;        // its light goes past SDR's white
  float pq[1024];   // 10-bit PQ code: light relative to SDR's white
  unsigned hist[HIST_BINS];
  unsigned char oetf[OETF_SIZE]; // sRGB's curve, light to bytes
} look_t;


/**
 * a format's bytes a pixel and whether its light goes past SDR's white; 0,
 * or -1 for one this does not read
 **/
static int
known_format(int format, int *bytes, int *hdr) {
  static const struct {
    int format, bytes, hdr;
  } known[] = {
    {F_GRAY8, 1, 0}, {F_BGR24, 3, 0}, {F_RGB24, 3, 0}, {F_BGR32, 4, 0}, {F_BGRA32, 4, 0},
    {F_PBGRA32, 4, 0}, {F_RGB48, 6, 0}, {F_RGBA64, 8, 0}, {F_PRGBA64, 8, 0},
    {F_RGB101010, 4, 1}, {F_RGB48_FIXED, 6, 1}, {F_RGB64_FIXED, 8, 1}, {F_RGBA64_FIXED, 8, 1},
    {F_RGB96_FIXED, 12, 1}, {F_RGB128_FIXED, 16, 1}, {F_RGBA128_FIXED, 16, 1},
    {F_RGB48_HALF, 6, 1}, {F_RGB64_HALF, 8, 1}, {F_RGBA64_HALF, 8, 1},
    {F_RGB96_FLOAT, 12, 1}, {F_RGB128_FLOAT, 16, 1}, {F_RGBA128_FLOAT, 16, 1}, {F_PRGBA128_FLOAT, 16, 1},
  };

  for(size_t i=0; i < sizeof known / sizeof known[0]; i++) {
    if(known[i].format == format) {
      *bytes = known[i].bytes;
      *hdr = known[i].hdr;
      return 0;
    }
  }
  return -1;
}


/**
 * how a format is read; 0, or -1 for one this does not read
 **/
static int
look_for(look_t *l, int format) {
  if(known_format(format, &l->bytes, &l->hdr)) {
    return -1;
  }
  l->format = format;
  if(format == F_RGB101010) {
    // SMPTE ST 2084
    const float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
    for(int c=0; c < 1024; c++) {
      float p = powf((float)c / 1023.0f, 1.0f / m2);
      float d = fmaxf(p - c1, 0) / (c2 - c3 * p);
      l->pq[c] = 10000.0f * powf(d, 1.0f / m1) / REF_NITS;
    }
  }
  return 0;
}


/**
 * one pixel's light, relative to SDR's white, in BT.709's primaries (HDR),
 * or its sRGB bytes over 255 (SDR)
 **/
static void
pixel(const look_t *l, const unsigned char *p, float rgb[3]) {
  const float scrgb = 80.0f / REF_NITS;

  switch(l->format) {
  case F_GRAY8:
    rgb[0] = rgb[1] = rgb[2] = p[0] / 255.0f;
    return;
  case F_BGR24: case F_BGR32: case F_BGRA32: case F_PBGRA32:
    rgb[0] = p[2] / 255.0f;
    rgb[1] = p[1] / 255.0f;
    rgb[2] = p[0] / 255.0f;
    return;
  case F_RGB24:
    rgb[0] = p[0] / 255.0f;
    rgb[1] = p[1] / 255.0f;
    rgb[2] = p[2] / 255.0f;
    return;
  case F_RGB48: case F_RGBA64: case F_PRGBA64:
    for(int i=0; i<3; i++) {
      rgb[i] = u16(p + 2 * i) / 65535.0f;
    }
    return;
  case F_RGB101010: {
    uint32_t v = (uint32_t)i32(p);
    float r = l->pq[(v >> 20) & 0x3ff], g = l->pq[(v >> 10) & 0x3ff], b = l->pq[v & 0x3ff];
    // BT.2020 to BT.709
    rgb[0] = 1.6605f * r - 0.5876f * g - 0.0728f * b;
    rgb[1] = -0.1246f * r + 1.1329f * g - 0.0083f * b;
    rgb[2] = -0.0182f * r - 0.1006f * g + 1.1187f * b;
    return;
  }
  case F_RGB48_FIXED: case F_RGB64_FIXED: case F_RGBA64_FIXED:
    for(int i=0; i<3; i++) {
      rgb[i] = (int16_t)u16(p + 2 * i) * (1.0f / 8192.0f) * scrgb;
    }
    return;
  case F_RGB96_FIXED: case F_RGB128_FIXED: case F_RGBA128_FIXED:
    for(int i=0; i<3; i++) {
      rgb[i] = (float)i32(p + 4 * i) * (1.0f / 16777216.0f) * scrgb;
    }
    return;
  case F_RGB48_HALF: case F_RGB64_HALF: case F_RGBA64_HALF:
    for(int i=0; i<3; i++) {
      rgb[i] = half(u16(p + 2 * i)) * scrgb;
    }
    return;
  default: // the floats
    for(int i=0; i<3; i++) {
      rgb[i] = f32(p + 4 * i) * scrgb;
    }
    return;
  }
}


static float
luma(const float rgb[3]) {
  return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}


/**
 * the picture's white: the luminance 99.9 % of its pixels stay under, from
 * one pixel in four
 **/
static float
white_of(look_t *l, const unsigned char *pixels, int w, int h, int stride) {
  unsigned *hist = l->hist;
  unsigned long long count = 0, seen = 0;
  float rgb[3], y;
  int bin;

  memset(l->hist, 0, sizeof l->hist);
  for(int r=0; r < h; r += 2) {
    const unsigned char *row = pixels + (size_t)r * stride;
    for(int c=0; c < w; c += 2) {
      pixel(l, row + (size_t)c * l->bytes, rgb);
      y = luma(rgb);
      if(!(y > 0) || !__builtin_isfinite(y)) {
        continue;
      }
      bin = (int)((log2f(y) - HIST_LOW) / (HIST_HIGH - HIST_LOW) * HIST_BINS);
      hist[bin < 0 ? 0 : bin >= HIST_BINS ? HIST_BINS - 1 : bin]++;
      count++;
    }
  }
  if(!count) {
    return 1;
  }
  for(bin=0; bin < HIST_BINS; bin++) {
    seen += hist[bin];
    if(seen * 1000 >= count * 999) {
      break;
    }
  }
  // the bin's lower edge: what is over it, by less than a bin, is white too
  y = exp2f(HIST_LOW + bin * (HIST_HIGH - HIST_LOW) / HIST_BINS);
  return y < 1 ? 1 : y > WHITE_MAX ? WHITE_MAX : y;
}


/**
 * the luminance bent into SDR: the same up to KNEE, then Reinhard's
 * extended curve, its slope at the knee 1, so the white reaches 1
 **/
static float
bend(float y, float white) {
  float x, wx;

  if(y <= KNEE) {
    return y;
  }
  if(white <= 1) {
    return y < 1 ? y : 1;
  }
  x = (y - KNEE) / (1 - KNEE);
  wx = (white - KNEE) / (1 - KNEE);
  y = KNEE + (1 - KNEE) * (x * (1 + x / (wx * wx)) / (1 + x));
  return y < 1 ? y : 1;
}


int
jxr_format_hdr(int format) {
  int bytes, hdr;

  return !known_format(format, &bytes, &hdr) && hdr;
}


// the gain map being made
typedef struct gain {
  uint8_t *map;
  int      w, h;
  float    max;   // log2 of the most boost
  float   *sum;   // of the logs in the row of squares being made
} gain_t;


/**
 * a row of the gain map's squares made, from the sums of their logs; rows
 * of the picture in it
 **/
static void
gain_row(gain_t *g, int row, int rows, int w) {
  for(int x=0; x < g->w; x++) {
    int cols = w - x * JXR_GAIN_SCALE < JXR_GAIN_SCALE ? w - x * JXR_GAIN_SCALE : JXR_GAIN_SCALE;
    float v = g->sum[x] / (float)(cols * rows) / g->max * 255 + 0.5f;
    g->map[(size_t)row * g->w + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    g->sum[x] = 0;
  }
}


static uint32_t *
convert(const void *data, int w, int h, int stride, int format, gain_t *g, int *error) {
  const unsigned char *pixels = data;
  uint32_t *out;
  look_t *l;
  float white = 1, rgb[3], y, was, k, top, pale, t, sdr;
  unsigned char c[3];

  *error = 0;
  if(g) {
    g->map = NULL;
    g->sum = NULL;
  }
  if(w <= 0 || h <= 0 || !(l=mem_alloc(sizeof *l))) {
    *error = -2;
    return NULL;
  }
  if(look_for(l, format)) {
    mem_free(l);
    *error = -1;
    return NULL;
  }
  if(!(out=mem_alloc((size_t)w * h * sizeof *out))) {
    mem_free(l);
    *error = -2;
    return NULL;
  }
  for(int i=0; i < OETF_SIZE; i++) {
    float v = (float)i / (OETF_SIZE - 1);
    v = v <= 0.0031308f ? 12.92f * v : 1.055f * powf(v, 1 / 2.4f) - 0.055f;
    l->oetf[i] = (unsigned char)(v * 255 + 0.5f);
  }
  if(l->hdr) {
    white = white_of(l, pixels, w, h, stride);
  }
  // a gain map only for light worth it over SDR's white
  if(g && l->hdr && white >= GAIN_LEAST) {
    g->w = (w + JXR_GAIN_SCALE - 1) / JXR_GAIN_SCALE;
    g->h = (h + JXR_GAIN_SCALE - 1) / JXR_GAIN_SCALE;
    g->max = log2f((white + GAIN_OFFSET) / (1 + GAIN_OFFSET));
    g->map = mem_alloc((size_t)g->w * g->h);
    g->sum = mem_calloc((size_t)g->w, sizeof *g->sum);
    if(!g->map || !g->sum) {
      mem_free(g->map);
      mem_free(g->sum);
      mem_free(out);
      mem_free(l);
      g->map = NULL;
      g->sum = NULL;
      *error = -2;
      return NULL;
    }
  }
  for(int r=0; r < h; r++) {
    const unsigned char *row = pixels + (size_t)r * stride;
    uint32_t *dst = out + (size_t)r * w;
    for(int x=0; x < w; x++) {
      pixel(l, row + (size_t)x * l->bytes, rgb);
      if(!l->hdr) {
        for(int i=0; i<3; i++) {
          c[i] = (unsigned char)(rgb[i] * 255 + 0.5f);
        }
      } else {
        for(int i=0; i<3; i++) {
          rgb[i] = rgb[i] > 0 && __builtin_isfinite(rgb[i]) ? rgb[i] : 0;
        }
        was = y = luma(rgb);
        if(y > 0) {
          k = bend(y, white) / y;
          rgb[0] *= k;
          rgb[1] *= k;
          rgb[2] *= k;
          y *= k;
          // a channel still over white: darkened (its hue kept) or towards
          // white (its luminance kept), by how bright it was
          top = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2]));
          if(top > 1 && top > y) {
            t = white > 1 ? (was - 1) / (white - 1) : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            k = (1 - y) / (top - y);
            for(int i=0; i<3; i++) {
              pale = y + (rgb[i] - y) * k;
              rgb[i] = rgb[i] / top + (pale - rgb[i] / top) * t;
            }
          }
        }
        for(int i=0; i<3; i++) {
          rgb[i] = rgb[i] < 0 ? 0 : rgb[i] > 1 ? 1 : rgb[i];
          c[i] = l->oetf[(int)(rgb[i] * (OETF_SIZE - 1) + 0.5f)];
        }
        if(g && g->map) {
          // the boost that takes the SDR luminance back to the HDR one, as
          // Ultra HDR counts it: (HDR + offset) / (SDR + offset), in stops
          sdr = luma(rgb);
          k = log2f((was + GAIN_OFFSET) / (sdr + GAIN_OFFSET));
          g->sum[x / JXR_GAIN_SCALE] += k < 0 ? 0 : k > g->max ? g->max : k;
        }
      }
      dst[x] = 0xFF000000u | (uint32_t)c[2] << 16 | (uint32_t)c[1] << 8 | c[0];
    }
    if(g && g->map && (r % JXR_GAIN_SCALE == JXR_GAIN_SCALE - 1 || r == h - 1)) {
      gain_row(g, r / JXR_GAIN_SCALE, r % JXR_GAIN_SCALE + 1, w);
    }
  }
  mem_free(l);
  if(g) {
    mem_free(g->sum);
    g->sum = NULL;
  }
  return out;
}


uint32_t *
jxr_to_sdr(const void *data, int w, int h, int stride, int format, int *error) {
  return convert(data, w, h, stride, format, NULL, error);
}


uint32_t *
jxr_to_sdr_gain(const void *data, int w, int h, int stride, int format, uint8_t **gain, int *gw, int *gh,
                float *gain_max, int *error) {
  gain_t g;
  uint32_t *out = convert(data, w, h, stride, format, &g, error);

  *gain = out ? g.map : NULL;
  *gw = *gain ? g.w : 0;
  *gh = *gain ? g.h : 0;
  *gain_max = *gain ? g.max : 0;
  return out;
}


static uint32_t *
decode(const void *data, size_t len, int min_w, int min_h, int *w, int *h, int *format, gain_t *g, int *error) {
  int iw, ih, bits, shrink = 0, stride, ret, bytes, hdr;
  uint32_t *out;
  size_t room;
  void *raw;

  *error = 0;
  *format = 0;
  if(g) {
    g->map = NULL;
  }
  if(!jxr_is(data, len)) {
    *error = -1;
    return NULL;
  }
  if((ret=xps5_jxr_info(data, len, &iw, &ih, format, &bits)) || iw <= 0 || ih <= 0 || bits <= 0) {
    *error = ret ? ret : -1;
    return NULL;
  }
  if(known_format(*format, &bytes, &hdr)) {
    *error = -1;
    return NULL;
  }
  while(min_w > 0 && min_h > 0 && shrink < 4 && ((iw >> (shrink + 1)) >= min_w && (ih >> (shrink + 1)) >= min_h)) {
    shrink++;
  }
  room = (size_t)((bits + 7) / 8) * (size_t)((iw + 15) >> shrink) * (size_t)((ih + 15) >> shrink);
  if(!(raw=mem_alloc(room))) {
    *error = -2;
    return NULL;
  }
  if((ret=xps5_jxr_decode(data, len, shrink, raw, room, w, h, &stride))) {
    mem_free(raw);
    *error = ret;
    return NULL;
  }
  out = convert(raw, *w, *h, stride, *format, g, error);
  mem_free(raw);
  return out;
}


uint32_t *
jxr_decode(const void *data, size_t len, int min_w, int min_h, int *w, int *h, int *format, int *error) {
  return decode(data, len, min_w, min_h, w, h, format, NULL, error);
}


uint32_t *
jxr_decode_hdr(const void *data, size_t len, int *w, int *h, int *format, uint8_t **gain, int *gw, int *gh,
               float *gain_max, int *error) {
  gain_t g;
  uint32_t *out = decode(data, len, 0, 0, w, h, format, &g, error);

  *gain = out ? g.map : NULL;
  *gw = *gain ? g.w : 0;
  *gh = *gain ? g.h : 0;
  *gain_max = *gain ? g.max : 0;
  return out;
}
