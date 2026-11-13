// The home screen's background of the title: pic0.png (raw RGB 1920x1080)
// as the 3840x2160 BC7 .dds the PS5 menu shows (DX10 header, format 98, as
// the system's own), every block in mode 6. Copied from the same author's
// BrowserPS5-WebKit (poc/tests-v1/bc7dds.c).
//
//   python -c "from PIL import Image; open('pic0.raw','wb').write(Image.open('title/art/pic0.png').convert('RGB').tobytes())"
//   clang -O2 -o bc7dds tools/bc7dds.c -lm && ./bc7dds pic0.raw title/art/pic0.dds
//
// 2026-11-09: raw RGB 1920x1080 -> the PS5 home screen's background: a
// 3840x2160 BC7 .dds (DX10 header, format 98, as the system's own pic0.dds),
// every block in mode 6. bc7dds in.raw out.dds   (also: bc7dds -d in.dds out.raw)
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW 1920
#define SH 1080
#define W 3840
#define H 2160

static const uint8_t HDR[148] = {
  0x44,0x44,0x53,0x20,0x7c,0,0,0,0x07,0x10,0x0a,0,0x70,0x08,0,0,0,0x0f,0,0,0,0x90,0x7e,0,0,0,0,0,1,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0x20,0,0,0,0x04,0,0,0,0x44,0x58,0x31,0x30,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x62,0,0,0,0x03,0,0,0,0,0,0,0,0x01,0,0,0,0,0,0,0,
};
static const int WT[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

static uint8_t* src;

static void
sample(int x, int y, int* rgb) {  // bilinear, 2x up
  const float fx = (x + 0.5f) / 2.0f - 0.5f, fy = (y + 0.5f) / 2.0f - 0.5f;
  int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
  const float ax = fx - x0, ay = fy - y0;
  int x1 = x0 + 1, y1 = y0 + 1;
  x0 = x0 < 0 ? 0 : x0;
  y0 = y0 < 0 ? 0 : y0;
  x1 = x1 >= SW ? SW - 1 : x1;
  y1 = y1 >= SH ? SH - 1 : y1;
  for(int c = 0; c < 3; c++) {
    const float a = src[(y0 * SW + x0) * 3 + c], b = src[(y0 * SW + x1) * 3 + c];
    const float d = src[(y1 * SW + x0) * 3 + c], e = src[(y1 * SW + x1) * 3 + c];
    rgb[c] = (int)lrintf((a * (1 - ax) + b * ax) * (1 - ay) + (d * (1 - ax) + e * ax) * ay);
  }
}

typedef struct { uint64_t lo, hi; int at; } bits_t;

static void
put(bits_t* b, uint32_t v, int n) {
  for(int i = 0; i < n; i++, b->at++) {
    const uint64_t bit = (v >> i) & 1;
    if(b->at < 64) {
      b->lo |= bit << b->at;
    } else {
      b->hi |= bit << (b->at - 64);
    }
  }
}

static void
block(int px[16][3], uint8_t out[16]) {
  float m[3] = {0}, cov[6] = {0};
  for(int i = 0; i < 16; i++) {
    for(int c = 0; c < 3; c++) {
      m[c] += px[i][c] / 16.0f;
    }
  }
  for(int i = 0; i < 16; i++) {
    const float r = px[i][0] - m[0], g = px[i][1] - m[1], b = px[i][2] - m[2];
    cov[0] += r * r; cov[1] += r * g; cov[2] += r * b; cov[3] += g * g; cov[4] += g * b; cov[5] += b * b;
  }
  float ax[3] = {1, 1, 1};
  for(int it = 0; it < 8; it++) {
    const float x = cov[0] * ax[0] + cov[1] * ax[1] + cov[2] * ax[2];
    const float y = cov[1] * ax[0] + cov[3] * ax[1] + cov[4] * ax[2];
    const float z = cov[2] * ax[0] + cov[4] * ax[1] + cov[5] * ax[2];
    const float n = sqrtf(x * x + y * y + z * z);
    if(n < 1e-6f) {
      break;
    }
    ax[0] = x / n; ax[1] = y / n; ax[2] = z / n;
  }
  float lo = 1e9f, hi = -1e9f;
  for(int i = 0; i < 16; i++) {
    const float t = (px[i][0] - m[0]) * ax[0] + (px[i][1] - m[1]) * ax[1] + (px[i][2] - m[2]) * ax[2];
    lo = t < lo ? t : lo;
    hi = t > hi ? t : hi;
  }
  int q[2][3], e[2][3];
  for(int k = 0; k < 2; k++) {
    const float t = k ? hi : lo;
    for(int c = 0; c < 3; c++) {
      int v = (int)lrintf(m[c] + t * ax[c]);
      v = v < 0 ? 0 : v > 255 ? 255 : v;
      int qq = (v - 1 + 1) / 2;  // nearest 2q+1
      qq = qq > 127 ? 127 : qq < 0 ? 0 : qq;
      if(abs(2 * qq + 1 - v) > abs(2 * (qq - 1) + 1 - v) && qq > 0) {
        qq--;
      }
      q[k][c] = qq;
      e[k][c] = 2 * qq + 1;
    }
  }
  int idx[16];
  for(int i = 0; i < 16; i++) {
    int best = 0, bd = 1 << 30;
    for(int j = 0; j < 16; j++) {
      int d = 0;
      for(int c = 0; c < 3; c++) {
        const int v = ((64 - WT[j]) * e[0][c] + WT[j] * e[1][c] + 32) >> 6;
        d += (v - px[i][c]) * (v - px[i][c]);
      }
      if(d < bd) {
        bd = d;
        best = j;
      }
    }
    idx[i] = best;
  }
  if(idx[0] >= 8) {  // the anchor's top bit is left out: 0
    for(int c = 0; c < 3; c++) {
      const int t = q[0][c];
      q[0][c] = q[1][c];
      q[1][c] = t;
    }
    for(int i = 0; i < 16; i++) {
      idx[i] = 15 - idx[i];
    }
  }
  bits_t b = {0, 0, 0};
  put(&b, 1u << 6, 7);
  for(int c = 0; c < 3; c++) {
    put(&b, (uint32_t)q[0][c], 7);
    put(&b, (uint32_t)q[1][c], 7);
  }
  put(&b, 127, 7);
  put(&b, 127, 7);
  put(&b, 1, 1);
  put(&b, 1, 1);
  put(&b, (uint32_t)idx[0], 3);
  for(int i = 1; i < 16; i++) {
    put(&b, (uint32_t)idx[i], 4);
  }
  memcpy(out, &b.lo, 8);
  memcpy(out + 8, &b.hi, 8);
}

// mode 6 only, for the check: 16 RGB pixels
static int
decode(const uint8_t in[16], int px[16][3]) {
  uint64_t lo, hi;
  memcpy(&lo, in, 8);
  memcpy(&hi, in + 8, 8);
  int at = 0;
#define GET(n) ({ uint32_t v_ = 0; for(int i_ = 0; i_ < (n); i_++, at++) { const uint64_t w_ = at < 64 ? lo >> at : hi >> (at - 64); v_ |= (uint32_t)(w_ & 1) << i_; } v_; })
  if(GET(7) != 1u << 6) {
    return -1;
  }
  int q[2][4];
  for(int c = 0; c < 4; c++) {
    q[0][c] = (int)GET(7);
    q[1][c] = (int)GET(7);
  }
  const int p0 = (int)GET(1), p1 = (int)GET(1);
  for(int i = 0; i < 16; i++) {
    const int j = (int)GET(i ? 4 : 3);
    for(int c = 0; c < 3; c++) {
      const int a = q[0][c] * 2 + p0, b = q[1][c] * 2 + p1;
      px[i][c] = ((64 - WT[j]) * a + WT[j] * b + 32) >> 6;
    }
  }
  return 0;
}

int
main(int argc, char** argv) {
  if(argc != 3) {
    return 2;
  }
  FILE* f = fopen(argv[1], "rb");
  src = malloc((size_t)SW * SH * 3);
  if(!f || fread(src, 1, (size_t)SW * SH * 3, f) != (size_t)SW * SH * 3) {
    fprintf(stderr, "bad input\n");
    return 1;
  }
  fclose(f);
  uint8_t* out = malloc((size_t)W * H);
  double err = 0;
  for(int by = 0; by < H / 4; by++) {
    for(int bx = 0; bx < W / 4; bx++) {
      int px[16][3], back[16][3];
      for(int i = 0; i < 16; i++) {
        sample(bx * 4 + i % 4, by * 4 + i / 4, px[i]);
      }
      uint8_t* o = out + ((size_t)by * (W / 4) + (size_t)bx) * 16;
      block(px, o);
      if(decode(o, back)) {
        fprintf(stderr, "bad block\n");
        return 1;
      }
      for(int i = 0; i < 16; i++) {
        for(int c = 0; c < 3; c++) {
          err += (double)(back[i][c] - px[i][c]) * (back[i][c] - px[i][c]);
        }
      }
    }
  }
  const double mse = err / ((double)W * H * 3);
  printf("PSNR %.2f dB\n", 10 * log10(255.0 * 255.0 / (mse > 1e-9 ? mse : 1e-9)));
  // a small look: every 8th pixel of the decoded picture, raw RGB 480x270
  f = fopen(argv[2], "wb");
  fwrite(HDR, 1, sizeof(HDR), f);
  fwrite(out, 1, (size_t)W * H, f);
  fclose(f);
  return 0;
}
