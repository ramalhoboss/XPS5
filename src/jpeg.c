/* A baseline JPEG writer, see jpeg.h. Each 16x16 square of the picture is
four 8x8 blocks of brightness and one each of the two colours, averaged;
each block goes through the DCT, the quantization tables and the Huffman
codes of annex K. */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "jpeg.h"
#include "mem.h"

static const uint8_t g_zigzag[64] = {
  0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21,
  28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61,
  54, 47, 55, 62, 63,
};

static const uint8_t g_luma_q[64] = {
  16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56,
  14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
  49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99,
};

static const uint8_t g_chroma_q[64] = {
  17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99,
  47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};

static const uint8_t g_dc_luma_bits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t g_dc_chroma_bits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t g_dc_values[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

static const uint8_t g_ac_luma_bits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
static const uint8_t g_ac_luma_values[162] = {
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
  0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
  0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37,
  0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
  0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
  0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
  0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
  0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};

static const uint8_t g_ac_chroma_bits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
static const uint8_t g_ac_chroma_values[162] = {
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
  0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
  0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
  0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
  0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
  0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
  0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};

// a Huffman table: each symbol's code and length
typedef struct huff {
  uint16_t code[256];
  uint8_t  len[256];
} huff_t;

typedef struct out {
  uint8_t *buf;
  size_t   len, cap;
  uint32_t bits;
  int      count, failed;
} out_t;


static void
put_byte(out_t *o, uint8_t b) {
  uint8_t *grown;

  if(o->failed) {
    return;
  }
  if(o->len == o->cap) {
    o->cap = o->cap ? o->cap * 2 : 1 << 16;
    if(!(grown=mem_realloc(o->buf, o->cap))) {
      o->failed = 1;
      return;
    }
    o->buf = grown;
  }
  o->buf[o->len++] = b;
}


static void
put16(out_t *o, unsigned v) {
  put_byte(o, (uint8_t)(v >> 8));
  put_byte(o, (uint8_t)v);
}


/**
 * bits of the entropy-coded data, a 0 after each 0xFF
 **/
static void
put_bits(out_t *o, unsigned value, int count) {
  o->bits = o->bits << count | (value & ((1u << count) - 1));
  o->count += count;
  while(o->count >= 8) {
    uint8_t b = (uint8_t)(o->bits >> (o->count - 8));
    put_byte(o, b);
    if(b == 0xff) {
      put_byte(o, 0);
    }
    o->count -= 8;
  }
}


static void
make_huff(huff_t *h, const uint8_t bits[16], const uint8_t *values) {
  unsigned code = 0;
  int k = 0;

  memset(h, 0, sizeof *h);
  for(int len=1; len <= 16; len++) {
    for(int i=0; i < bits[len - 1]; i++, k++) {
      h->code[values[k]] = (uint16_t)code++;
      h->len[values[k]] = (uint8_t)len;
    }
    code <<= 1;
  }
}


static void
put_dht(out_t *o, int class_id, const uint8_t bits[16], const uint8_t *values) {
  int count = 0;

  for(int i=0; i<16; i++) {
    count += bits[i];
  }
  put16(o, 0xffc4);
  put16(o, (unsigned)(3 + 16 + count));
  put_byte(o, (uint8_t)class_id);
  for(int i=0; i<16; i++) {
    put_byte(o, bits[i]);
  }
  for(int i=0; i < count; i++) {
    put_byte(o, values[i]);
  }
}


static void
scale_table(const uint8_t base[64], int quality, uint8_t out[64]) {
  int s = quality < 50 ? 5000 / quality : 200 - 2 * quality, v;

  for(int i=0; i<64; i++) {
    v = (base[i] * s + 50) / 100;
    out[i] = (uint8_t)(v < 1 ? 1 : v > 255 ? 255 : v);
  }
}


/**
 * the forward DCT of a block, in place, row by row then column by column
 **/
static void
fdct(float b[64]) {
  static float cosines[8][8];
  static int ready;
  float tmp[8];

  if(!ready) {
    for(int u=0; u<8; u++) {
      for(int x=0; x<8; x++) {
        cosines[u][x] = (u ? 0.5f : 0.5f * 0.70710678f) * cosf((2 * x + 1) * u * 3.14159265f / 16);
      }
    }
    ready = 1;
  }
  for(int pass=0; pass<2; pass++) {
    for(int i=0; i<8; i++) {
      for(int u=0; u<8; u++) {
        float s = 0;
        for(int x=0; x<8; x++) {
          s += (pass ? b[x * 8 + i] : b[i * 8 + x]) * cosines[u][x];
        }
        tmp[u] = s;
      }
      for(int u=0; u<8; u++) {
        if(pass) {
          b[u * 8 + i] = tmp[u];
        } else {
          b[i * 8 + u] = tmp[u];
        }
      }
    }
  }
}


static void
put_value(out_t *o, const huff_t *h, int symbol_high, int v) {
  int a = v < 0 ? -v : v, size = 0;

  while(a) {
    size++;
    a >>= 1;
  }
  put_bits(o, h->code[symbol_high | size], h->len[symbol_high | size]);
  if(size) {
    put_bits(o, (unsigned)(v < 0 ? v + (1 << size) - 1 : v), size);
  }
}


static void
encode_block(out_t *o, float b[64], const uint8_t q[64], const huff_t *dc, const huff_t *ac, int *last_dc) {
  int coef[64], run = 0, v;

  fdct(b);
  for(int i=0; i<64; i++) {
    v = (int)lroundf(b[g_zigzag[i]] / q[g_zigzag[i]]);
    coef[i] = v;
  }
  put_value(o, dc, 0, coef[0] - *last_dc);
  *last_dc = coef[0];
  for(int i=1; i<64; i++) {
    if(!coef[i]) {
      run++;
      continue;
    }
    while(run >= 16) {
      put_bits(o, ac->code[0xf0], ac->len[0xf0]);
      run -= 16;
    }
    put_value(o, ac, run << 4, coef[i]);
    run = 0;
  }
  if(run) {
    put_bits(o, ac->code[0x00], ac->len[0x00]);
  }
}


int
jpeg_encode(const uint32_t *pixels, int w, int h, int quality, uint8_t **out, size_t *len) {
  static huff_t dc_y, dc_c, ac_y, ac_c;
  uint8_t qy[64], qc[64];
  float y[4][64], cb[64], cr[64];
  int last[3] = {0, 0, 0};
  out_t o;

  *out = NULL;
  *len = 0;
  if(w < 1 || h < 1 || w > 65535 || h > 65535) {
    return -1;
  }
  quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
  scale_table(g_luma_q, quality, qy);
  scale_table(g_chroma_q, quality, qc);
  make_huff(&dc_y, g_dc_luma_bits, g_dc_values);
  make_huff(&dc_c, g_dc_chroma_bits, g_dc_values);
  make_huff(&ac_y, g_ac_luma_bits, g_ac_luma_values);
  make_huff(&ac_c, g_ac_chroma_bits, g_ac_chroma_values);
  memset(&o, 0, sizeof o);

  put16(&o, 0xffd8);
  put16(&o, 0xffe0); // JFIF
  put16(&o, 16);
  put_byte(&o, 'J');
  put_byte(&o, 'F');
  put_byte(&o, 'I');
  put_byte(&o, 'F');
  put_byte(&o, 0);
  put16(&o, 0x0101);
  put_byte(&o, 0);
  put16(&o, 1);
  put16(&o, 1);
  put16(&o, 0);
  for(int t=0; t<2; t++) {
    put16(&o, 0xffdb);
    put16(&o, 67);
    put_byte(&o, (uint8_t)t);
    for(int i=0; i<64; i++) {
      put_byte(&o, t ? qc[g_zigzag[i]] : qy[g_zigzag[i]]);
    }
  }
  put16(&o, 0xffc0);
  put16(&o, 17);
  put_byte(&o, 8);
  put16(&o, (unsigned)h);
  put16(&o, (unsigned)w);
  put_byte(&o, 3);
  put_byte(&o, 1);
  put_byte(&o, 0x22);
  put_byte(&o, 0);
  put_byte(&o, 2);
  put_byte(&o, 0x11);
  put_byte(&o, 1);
  put_byte(&o, 3);
  put_byte(&o, 0x11);
  put_byte(&o, 1);
  put_dht(&o, 0x00, g_dc_luma_bits, g_dc_values);
  put_dht(&o, 0x10, g_ac_luma_bits, g_ac_luma_values);
  put_dht(&o, 0x01, g_dc_chroma_bits, g_dc_values);
  put_dht(&o, 0x11, g_ac_chroma_bits, g_ac_chroma_values);
  put16(&o, 0xffda);
  put16(&o, 12);
  put_byte(&o, 3);
  put_byte(&o, 1);
  put_byte(&o, 0x00);
  put_byte(&o, 2);
  put_byte(&o, 0x11);
  put_byte(&o, 3);
  put_byte(&o, 0x11);
  put_byte(&o, 0);
  put_byte(&o, 63);
  put_byte(&o, 0);

  for(int my=0; my < h; my += 16) {
    for(int mx=0; mx < w; mx += 16) {
      memset(cb, 0, sizeof cb);
      memset(cr, 0, sizeof cr);
      for(int py=0; py<16; py++) {
        int sy = my + py < h ? my + py : h - 1;
        for(int px=0; px<16; px++) {
          int sx = mx + px < w ? mx + px : w - 1;
          uint32_t c = pixels[(size_t)sy * w + sx];
          float r = (float)(c & 0xff), g = (float)(c >> 8 & 0xff), b = (float)(c >> 16 & 0xff);
          y[(py >> 3) * 2 + (px >> 3)][(py & 7) * 8 + (px & 7)] = 0.299f * r + 0.587f * g + 0.114f * b - 128;
          cb[(py >> 1) * 8 + (px >> 1)] += (-0.168736f * r - 0.331264f * g + 0.5f * b) / 4;
          cr[(py >> 1) * 8 + (px >> 1)] += (0.5f * r - 0.418688f * g - 0.081312f * b) / 4;
        }
      }
      for(int k=0; k<4; k++) {
        encode_block(&o, y[k], qy, &dc_y, &ac_y, &last[0]);
      }
      encode_block(&o, cb, qc, &dc_c, &ac_c, &last[1]);
      encode_block(&o, cr, qc, &dc_c, &ac_c, &last[2]);
    }
  }
  if(o.count) {
    put_bits(&o, 0x7f, 7 - ((o.count - 1) & 7)); // the last byte filled with 1s
  }
  put16(&o, 0xffd9);
  if(o.failed) {
    mem_free(o.buf);
    return -1;
  }
  *out = o.buf;
  *len = o.len;
  return 0;
}
