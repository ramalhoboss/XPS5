/* The x-client-transaction-id header, without the network, see xtid.h.

The numbers have to come out exactly as a browser's do: the easing is
solved the way Chromium solves CSS easing, the matrix entries are read back
from their printed form (six significant digits) and rounded the way
JavaScript's toFixed(2) rounds, and Math.round rounds halves up. */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "xtid.h"

#define KEYWORD    "obfiowerehiring"
#define EXTRA_BYTE 3
#define EPOCH      1682924400LL // 2023-05-01 07:00 UTC, where X's clock starts
#define TOTAL_TIME 4096.0
#define ROW_MIN    11 // colours (6), rotation, then the easing curve (4)
#define ROW_MAX    32
#define PI         3.14159265358979323846


/*
 * SHA-256.
 */

static const uint32_t sha_k[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};


static uint32_t
ror(uint32_t x, int n) {
  return x >> n | x << (32 - n);
}


static void
sha_block(uint32_t h[8], const unsigned char *p) {
  uint32_t w[64], a, b, c, d, e, f, g, k, t1, t2;

  for(int i=0; i<16; i++) {
    w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
  }
  for(int i=16; i<64; i++) {
    w[i] = w[i - 16] + (ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ w[i - 15] >> 3) + w[i - 7] +
           (ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ w[i - 2] >> 10);
  }
  a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; k = h[7];
  for(int i=0; i<64; i++) {
    t1 = k + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + sha_k[i] + w[i];
    t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}


static void sha256(const void *data, size_t len, unsigned char out[32]);


void
xtid_sha256(const void *data, size_t len, unsigned char out[32]) {
  sha256(data, len, out);
}


static void
sha256(const void *data, size_t len, unsigned char out[32]) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const unsigned char *p = data;
  unsigned char last[128] = {0};
  size_t rest = len % 64, n = rest < 56 ? 64 : 128;
  uint64_t bits = (uint64_t)len * 8;

  for(size_t i=0; i + 64 <= len; i += 64) {
    sha_block(h, p + i);
  }
  memcpy(last, p + len - rest, rest);
  last[rest] = 0x80;
  for(int i=0; i<8; i++) {
    last[n - 1 - i] = (unsigned char)(bits >> (8 * i));
  }
  for(size_t i=0; i<n; i += 64) {
    sha_block(h, last + i);
  }
  for(int i=0; i<8; i++) {
    out[4 * i] = (unsigned char)(h[i] >> 24);
    out[4 * i + 1] = (unsigned char)(h[i] >> 16);
    out[4 * i + 2] = (unsigned char)(h[i] >> 8);
    out[4 * i + 3] = (unsigned char)h[i];
  }
}


/*
 * Base64.
 */

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


/**
 * the bytes of base64 text of len characters (padding allowed); returns
 * how many, or -1
 **/
static int
b64_decode(const char *s, size_t len, unsigned char *out, int max) {
  unsigned acc = 0;
  int bits = 0, n = 0;
  const char *c;

  for(size_t i=0; i<len && s[i] != '='; i++) {
    if(!s[i] || !(c=strchr(b64, s[i]))) {
      return -1;
    }
    acc = (acc << 6 | (unsigned)(c - b64)) & 0xFFFFFF;
    if((bits += 6) >= 8) {
      bits -= 8;
      if(n == max) {
        return -1;
      }
      out[n++] = (unsigned char)(acc >> bits);
    }
  }
  return n;
}


/**
 * base64 without the padding
 **/
static int
b64_encode(const unsigned char *in, int len, char *out, size_t size) {
  size_t n = 0;
  unsigned acc = 0;
  int bits = 0;

  for(int i=0; i<len; i++) {
    acc = (acc << 8 | in[i]) & 0xFFFF;
    bits += 8;
    while(bits >= 6) {
      bits -= 6;
      if(n + 1 >= size) {
        return -1;
      }
      out[n++] = b64[acc >> bits & 63];
    }
  }
  if(bits) {
    if(n + 1 >= size) {
      return -1;
    }
    out[n++] = b64[acc << (6 - bits) & 63];
  }
  out[n] = 0;
  return 0;
}


/*
 * The page.
 */

static int
is_word(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}


static int
is_space(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}


/**
 * past the '>' of the tag at p, minding quoted attribute values; NULL when
 * it does not end
 **/
static const char *
tag_end(const char *p) {
  char quote = 0;

  for(p++; *p; p++) {
    if(quote) {
      quote = *p == quote ? 0 : quote;
    } else if(*p == '"' || *p == '\'') {
      quote = *p;
    } else if(*p == '>') {
      return p + 1;
    }
  }
  return NULL;
}


/**
 * the value of attribute name of the tag at p, and its length; NULL when
 * the tag has none
 **/
static const char *
attr(const char *p, const char *name, size_t *len) {
  const char *end = tag_end(p), *n, *v;
  size_t nlen = strlen(name), l;
  char quote;

  if(!end) {
    return NULL;
  }
  end--; // at the '>'
  for(p++; p < end && !is_space(*p) && *p != '/'; p++) {
  }
  while(p < end) {
    if(is_space(*p) || *p == '/') {
      p++;
      continue;
    }
    for(n=p; p < end && !is_space(*p) && *p != '=' && *p != '/'; p++) {
    }
    l = (size_t)(p - n);
    while(p < end && is_space(*p)) {
      p++;
    }
    v = NULL;
    *len = 0;
    if(p < end && *p == '=') {
      for(p++; p < end && is_space(*p); p++) {
      }
      if(p < end && (*p == '"' || *p == '\'')) {
        quote = *p++;
        for(v=p; p < end && *p != quote; p++) {
        }
        *len = (size_t)(p - v);
        p += p < end;
      } else {
        for(v=p; p < end && !is_space(*p); p++) {
        }
        *len = (size_t)(p - v);
      }
    }
    if(v && l == nlen && !strncasecmp(n, name, l)) {
      return v;
    }
  }
  return NULL;
}


static int
void_tag(const char *p) {
  static const char *const tags[] = {"meta", "link", "br", "img", "input", "hr", "source", NULL};
  size_t l;

  for(int i=0; tags[i]; i++) {
    l = strlen(tags[i]);
    if(!strncasecmp(p + 1, tags[i], l) && !is_word(p[1 + l])) {
      return 1;
    }
  }
  return 0;
}


/**
 * the n-th element (from 0) among those right inside an element whose
 * content starts at p; NULL when there are fewer
 **/
static const char *
child(const char *p, int n) {
  const char *e;
  int depth = 0, k = 0;

  while((p=strchr(p, '<'))) {
    if(p[1] == '/') {
      if(!depth--) {
        return NULL; // the end of the parent
      }
      if(!(p=strchr(p, '>'))) {
        return NULL;
      }
      p++;
    } else if(!strncmp(p, "<!--", 4)) {
      if(!(e=strstr(p + 4, "-->"))) {
        return NULL;
      }
      p = e + 3;
    } else if(is_word((unsigned char)p[1])) {
      if(!(e=tag_end(p))) {
        return NULL;
      }
      if(!depth && k++ == n) {
        return p;
      }
      if(e[-2] != '/' && !void_tag(p)) {
        depth++;
      }
      p = e;
    } else {
      p++;
    }
  }
  return NULL;
}


/**
 * the start of the n-th element (from 0) whose id begins with prefix
 **/
static const char *
element_with_id(const char *html, const char *prefix, int n) {
  const char *p, *q, *id;
  size_t len, plen = strlen(prefix);

  for(p=html; (p=strstr(p, prefix)); p += plen) {
    for(q=p; q > html && *q != '<' && *q != '>'; q--) {
    }
    if(*q == '<' && is_word((unsigned char)q[1]) && (id=attr(q, "id", &len)) == p && !n--) {
      return q;
    }
  }
  return NULL;
}


/**
 * the first <id>:"<hash>" at from or after it, not inside a longer number,
 * as the chunk's address
 **/
static int
chunk_url(const char *html, const char *from, const char *id, size_t idlen, char *url, size_t size) {
  const char *q, *h;
  size_t hlen;
  char quote;

  for(q=from; (q=strchr(q, *id)); q++) {
    if((q > html && is_word((unsigned char)q[-1])) || strncmp(q, id, idlen)) {
      continue;
    }
    for(h=q + idlen; is_space(*h); h++) {
    }
    if(*h++ != ':') {
      continue;
    }
    while(is_space(*h)) {
      h++;
    }
    if(*h != '"' && *h != '\'') {
      continue;
    }
    quote = *h++;
    for(hlen=0; is_word((unsigned char)h[hlen]) || h[hlen] == '-'; hlen++) {
    }
    if(!hlen || h[hlen] != quote || hlen > 64) {
      continue;
    }
    return snprintf(url, size, "https://abs.twimg.com/responsive-web/client-web/ondemand.s.%.*sa.js",
                    (int)hlen, h) < (int)size ? 0 : -1;
  }
  return -1;
}


int
xtid_ondemand_url(const char *html, char *url, size_t size) {
  static const char *const builder[] = {"}", ")", "[", "e", "]", "||", "e", ")", "+", "\"", ".", "\"",
                                        "+", "(", "{", NULL};
  const char *p = html, *q, *id, *s;
  size_t idlen;
  int ok;

  while((p=strstr(p, "ondemand.s"))) {
    q = p;
    p += 10;
    // quoted, after "<number>:"
    if(q == html || (q[-1] != '"' && q[-1] != '\'') || *p != q[-1]) {
      continue;
    }
    for(q -= 2; q >= html && is_space(*q); q--) {
    }
    if(q < html || *q != ':') {
      continue;
    }
    for(q--; q >= html && is_space(*q); q--) {
    }
    for(idlen=0; q >= html && *q >= '0' && *q <= '9'; q--, idlen++) {
    }
    if(!idlen) {
      continue;
    }
    id = q + 1;

    // the runtime's chunk address, ...})[e]||e)+"."+({<id>:"<hash>"
    for(s=p; (s=strchr(s, '}')); s++) {
      q = s;
      ok = 1;
      for(int i=0; builder[i] && ok; i++) {
        while(is_space(*q)) {
          q++;
        }
        if(builder[i][0] == '"') {
          ok = *q == '"' || *q == '\'';
          q += ok;
        } else {
          ok = !strncmp(q, builder[i], strlen(builder[i]));
          q += ok ? strlen(builder[i]) : 0;
        }
      }
      if(ok) {
        break;
      }
    }
    if((s && !chunk_url(html, q, id, idlen, url, size)) || !chunk_url(html, html, id, idlen, url, size)) {
      return 0;
    }
  }
  return -1;
}


int
xtid_indices(const char *js, int *indices, int max) {
  const char *p = js;
  int n = 0, v, digits;

  // (x[12], 16)
  while(n < max && (p=strchr(p, '('))) {
    p++;
    if(!is_word((unsigned char)p[0]) || p[1] != '[') {
      continue;
    }
    for(v=0, digits=0; p[2 + digits] >= '0' && p[2 + digits] <= '9' && digits < 3; digits++) {
      v = v * 10 + p[2 + digits] - '0';
    }
    if(digits < 1 || digits > 2 || p[2 + digits] != ']' || p[3 + digits] != ',') {
      continue;
    }
    for(p += 4 + digits; is_space(*p); p++) {
    }
    if(!strncmp(p, "16)", 3)) {
      indices[n++] = v;
    }
  }
  return n ? n : -1;
}


/*
 * The animation.
 */

/**
 * JavaScript's Math.round: halves go up
 **/
static double
js_round(double x) {
  double r = floor(x);

  return x - r >= 0.5 ? r + 1 : r;
}


typedef struct cubic {
  double ax, bx, cx, ay, by, cy, samples[11];
} cubic_t;

#define SPLINE_SAMPLES 11
#define SPLINE_STEP    (1.0 / (SPLINE_SAMPLES - 1))
#define CUBIC_EPSILON  1e-7


static double
sample_x(const cubic_t *c, double t) {
  return ((c->ax * t + c->bx) * t + c->cx) * t;
}


static double
sample_y(const cubic_t *c, double t) {
  return ((c->ay * t + c->by) * t + c->cy) * t;
}


/**
 * the eased value at time t (0 to 1) of the curve x1, y1, x2, y2, solved
 * like Chromium's gfx::CubicBezier
 **/
static double
ease(double x1, double y1, double x2, double y2, double x) {
  double t0 = 0, t1 = 1, t2 = x, dx = 0, d;
  cubic_t c;

  c.cx = 3.0 * x1;
  c.bx = 3.0 * (x2 - x1) - c.cx;
  c.ax = 1.0 - c.cx - c.bx;
  c.cy = 3.0 * y1;
  c.by = 3.0 * (y2 - y1) - c.cy;
  c.ay = 1.0 - c.cy - c.by;
  if(x < 0) {
    d = x1 > 0 ? y1 / x1 : y1 == 0 && x2 > 0 ? y2 / x2 : y1 == 0 && y2 == 0 ? 1 : 0;
    return d * x;
  }
  if(x > 1) {
    d = x2 < 1 ? (y2 - 1) / (x2 - 1) : y2 == 1 && x1 < 1 ? (y1 - 1) / (x1 - 1) :
        y2 == 1 && y1 == 1 ? 1 : 0;
    return 1.0 + d * (x - 1.0);
  }
  for(int i=0; i < SPLINE_SAMPLES; i++) {
    c.samples[i] = sample_x(&c, i * SPLINE_STEP);
  }
  for(int i=1; i < SPLINE_SAMPLES; i++) {
    if(x <= c.samples[i]) {
      t1 = SPLINE_STEP * i;
      t0 = t1 - SPLINE_STEP;
      t2 = t0 + (t1 - t0) * (x - c.samples[i - 1]) / (c.samples[i] - c.samples[i - 1]);
      break;
    }
  }
  for(int i=0; i<4; i++) {
    dx = sample_x(&c, t2) - x;
    if(fabs(dx) < CUBIC_EPSILON) {
      return sample_y(&c, t2);
    }
    d = (3.0 * c.ax * t2 + 2.0 * c.bx) * t2 + c.cx;
    if(fabs(d) < CUBIC_EPSILON) {
      break;
    }
    t2 -= dx / d;
  }
  if(fabs(dx) < CUBIC_EPSILON) {
    return sample_y(&c, t2);
  }
  for(int i=0; t0 < t1 && i < 200; i++) {
    dx = sample_x(&c, t2);
    if(fabs(dx - x) < CUBIC_EPSILON) {
      break;
    }
    if(x > dx) {
      t0 = t2;
    } else {
      t1 = t2;
    }
    t2 = (t1 + t0) * 0.5;
  }
  return sample_y(&c, t2);
}


/**
 * a byte scaled into min..max: floored, or to two decimals
 **/
static double
solve(double value, double min, double max, int floored) {
  double r = value * (max - min) / 255 + min;

  return floored ? floor(r) : js_round(r * 100) / 100;
}


/**
 * JavaScript's Number(x.toFixed(2)) for x >= 0: the hundredths nearest
 * the exact value of x, the larger of two as near
 **/
static double
to_fixed2(double x) {
  double y = x * 100, err = fma(x, 100, -y), n = floor(y), f = y - n;

  if(f > 0.5 || (f == 0.5 && err >= 0) || (f < 0.5 && f + err >= 0.5)) {
    n += 1;
  }
  return n / 100;
}


/**
 * x >= 0 in hexadecimal, its fraction to the last digit, with no 0 before
 * the point
 **/
static void
float_hex(double x, char *buf, size_t size) {
  static const char digits[] = "0123456789abcdef";
  double integer = floor(x), fraction = x - integer;
  char tmp[32];
  size_t n = 0, t = 0;
  unsigned long long i = (unsigned long long)integer;

  while(i && t < sizeof tmp) {
    tmp[t++] = digits[i % 16];
    i /= 16;
  }
  while(t && n + 1 < size) {
    buf[n++] = tmp[--t];
  }
  if(fraction > 0 && n + 1 < size) {
    buf[n++] = '.';
    while(fraction > 0 && n + 1 < size) {
      fraction *= 16;
      i = (unsigned long long)floor(fraction);
      fraction -= (double)i;
      buf[n++] = digits[i];
    }
  }
  buf[n] = 0;
}


static int
append(char *out, size_t size, size_t *n, const char *s) {
  for(; *s; s++) {
    if(*s == '.' || *s == '-') {
      continue;
    }
    if(*n + 1 >= size) {
      return -1;
    }
    out[(*n)++] = *s;
  }
  out[*n] = 0;
  return 0;
}


/**
 * the colour and the rotation matrix of a row at time t, written the way
 * X's script writes them
 **/
static int
animate(const int *row, double t, char *out, size_t size) {
  double curves[4], progress, rotation, rad, matrix[4], v;
  char printed[32], run[32], hex[64], piece[72], *end;
  const char *p;
  size_t n = 0, r;

  for(int i=0; i<4; i++) {
    curves[i] = solve(row[7 + i], i % 2 ? -1.0 : 0.0, 1.0, 0);
  }
  progress = ease(curves[0], curves[1], curves[2], curves[3], t);

  for(int i=0; i<3; i++) {
    v = row[i] * (1 - progress) + row[3 + i] * progress;
    v = v < 0 ? 0 : v > 255 ? 255 : v;
    snprintf(piece, sizeof piece, "%x", (unsigned)js_round(v));
    if(append(out, size, &n, piece)) {
      return -1;
    }
  }

  rotation = 0 * (1 - progress) + solve(row[6], 60.0, 360.0, 1) * progress;
  rad = rotation * PI / 180;
  matrix[0] = cos(rad);
  matrix[1] = -sin(rad);
  matrix[2] = sin(rad);
  matrix[3] = cos(rad);
  for(int i=0; i<4; i++) {
    // printed with six significant digits, then each number in the print
    // ("5.68575e-05" holds two)
    snprintf(printed, sizeof printed, "%.6g", matrix[i]);
    for(p=printed; *p; ) {
      for(r=0; r + 1 < sizeof run && ((*p >= '0' && *p <= '9') || *p == '.' || *p == '-'); p++) {
        run[r++] = *p;
      }
      if(!r) {
        p++;
        continue;
      }
      run[r] = 0;
      v = strtod(run, &end);
      if(*end) {
        snprintf(piece, sizeof piece, "0"); // not a number, which JavaScript writes "0."
      } else {
        float_hex(to_fixed2(fabs(v)), hex, sizeof hex);
        snprintf(piece, sizeof piece, "%s%s", hex[0] == '.' ? "0" : "", hex[0] ? hex : "0");
      }
      if(append(out, size, &n, piece)) {
        return -1;
      }
    }
  }
  return append(out, size, &n, "00");
}


/**
 * the numbers of the row-th segment of the path, after its first 9
 * characters; returns how many, or -1 when it has no such segment
 **/
static int
path_row(const char *d, size_t len, int row, int *out, int max) {
  const char *p = d + 9, *end = d + len;
  int n = 0;
  long v;

  if(len < 9) {
    return -1;
  }
  for(; row > 0 && p < end; p++) {
    row -= *p == 'C';
  }
  if(row > 0) {
    return -1;
  }
  while(p < end && *p != 'C') {
    if(*p >= '0' && *p <= '9') {
      for(v=0; p < end && *p >= '0' && *p <= '9'; p++) {
        v = v < 100000 ? v * 10 + *p - '0' : v;
      }
      if(n < max) {
        out[n++] = (int)v;
      }
    } else {
      p++;
    }
  }
  return n;
}


int
xtid_init(xtid_t *t, const char *html, const int *indices, int count) {
  const char *p, *key, *d;
  size_t len;
  int row[ROW_MAX], n;
  double frame_time = 1;

  memset(t, 0, sizeof *t);
  // <meta name="twitter-site-verification" content="...">
  for(p=html, key=NULL; !key && (p=strstr(p, "<meta")); p++) {
    if((key=attr(p, "name", &len)) && len == 25 && !strncmp(key, "twitter-site-verification", 25)) {
      key = attr(p, "content", &len);
    } else {
      key = NULL;
    }
  }
  if(!key || (t->key_len=b64_decode(key, len, t->key, sizeof t->key)) < 6 || count < 1) {
    t->key_len = 0;
    return -1;
  }
  for(int i=0; i<count; i++) {
    if(indices[i] < 0 || indices[i] >= t->key_len) {
      t->key_len = 0;
      return -2;
    }
  }

  // the second path of the first group of loading-x-anim-<key[5] % 4>
  if(!(p=element_with_id(html, "loading-x-anim", t->key[5] % 4)) || !(p=tag_end(p)) ||
     !(p=child(p, 0)) || !(p=tag_end(p)) || !(p=child(p, 1)) || !(d=attr(p, "d", &len)) ||
     (n=path_row(d, len, t->key[indices[0]] % 16, row, ROW_MAX)) < ROW_MIN) {
    t->key_len = 0;
    return -2;
  }
  for(int i=1; i<count; i++) {
    frame_time *= t->key[indices[i]] % 16;
  }
  if(animate(row, js_round(frame_time / 10) * 10 / TOTAL_TIME, t->animation, sizeof t->animation)) {
    t->key_len = 0;
    return -2;
  }
  return 0;
}


int
xtid_make(const xtid_t *t, const char *method, const char *path, long long now,
          unsigned r, char *out, size_t size) {
  unsigned char bytes[1 + XTID_KEY_MAX + 4 + 16 + 1], hash[32];
  char data[1024];
  long long when = now - EPOCH;
  int n = 0, len;

  if(!t->key_len || when < 0 || when > 0xFFFFFFFFLL ||
     (len=snprintf(data, sizeof data, "%s!%s!%lld" KEYWORD "%s", method, path, when,
                   t->animation)) < 0 || len >= (int)sizeof data) {
    return -1;
  }
  sha256(data, (size_t)len, hash);
  r &= 0xFF;
  bytes[n++] = (unsigned char)r;
  for(int i=0; i < t->key_len; i++) {
    bytes[n++] = t->key[i] ^ r;
  }
  for(int i=0; i<4; i++) {
    bytes[n++] = (unsigned char)(when >> (8 * i)) ^ r;
  }
  for(int i=0; i<16; i++) {
    bytes[n++] = hash[i] ^ r;
  }
  bytes[n++] = EXTRA_BYTE ^ r;
  return b64_encode(bytes, n, out, size);
}
