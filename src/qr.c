/* QR codes (src/qr.h): the data codewords of byte mode, their Reed-Solomon
error correction in blocks interleaved, the patterns every code has (the
three finders, the timing lines, the alignment pattern, the format bits),
the codewords in their zigzag, and the eight masks tried. */

#include <string.h>

#include "qr.h"

// level M, by version: error correction codewords in each block, and blocks
static const int ECC_PER_BLOCK[QR_MAX_VERSION + 1] = {0, 10, 16, 26, 18, 24, 16};
static const int BLOCKS[QR_MAX_VERSION + 1]        = {0, 1, 1, 1, 2, 2, 4};

#define FORMAT_M 0 // level M's two bits in the format information

typedef struct grid {
  qr_t          *qr;
  unsigned char  function[QR_MAX_SIZE][QR_MAX_SIZE]; // the patterns', not the data's
} grid_t;


/**
 * the modules of a version that carry data and error correction, in bits
 **/
static int
raw_modules(int ver) {
  int n = (16 * ver + 128) * ver + 64, align;

  if(ver >= 2) {
    align = ver / 7 + 2;
    n -= (25 * align - 10) * align - 55;
  }
  return n;
}


static int
data_codewords(int ver) {
  return raw_modules(ver) / 8 - ECC_PER_BLOCK[ver] * BLOCKS[ver];
}


/**
 * x times y in GF(2^8) with the QR polynomial 0x11D
 **/
static unsigned char
gf_mul(unsigned char x, unsigned char y) {
  unsigned z = 0;

  for(int i = 7; i >= 0; i--) {
    z = (z << 1) ^ ((z >> 7) * 0x11D);
    z ^= ((y >> i) & 1u) * x;
  }
  return (unsigned char)z;
}


/**
 * the generator of degree n, its coefficients highest first without the leading 1
 **/
static void
rs_divisor(int n, unsigned char *div) {
  unsigned char root = 1;

  memset(div, 0, (size_t)n);
  div[n - 1] = 1;
  for(int i = 0; i < n; i++) {
    for(int j = 0; j < n; j++) {
      div[j] = gf_mul(div[j], root);
      if(j + 1 < n) {
        div[j] ^= div[j + 1];
      }
    }
    root = gf_mul(root, 0x02);
  }
}


/**
 * the n error correction codewords of len data codewords
 **/
static void
rs_remainder(const unsigned char *data, int len, const unsigned char *div, int n, unsigned char *out) {
  unsigned char factor;

  memset(out, 0, (size_t)n);
  for(int i = 0; i < len; i++) {
    factor = data[i] ^ out[0];
    memmove(out, out + 1, (size_t)(n - 1));
    out[n - 1] = 0;
    for(int j = 0; j < n; j++) {
      out[j] ^= gf_mul(div[j], factor);
    }
  }
}


static void
set_function(grid_t *g, int x, int y, int dark) {
  g->qr->dark[y][x] = (unsigned char)(dark != 0);
  g->function[y][x] = 1;
}


static int
max_abs(int a, int b) {
  a = a < 0 ? -a : a;
  b = b < 0 ? -b : b;
  return a > b ? a : b;
}


static void
finder(grid_t *g, int cx, int cy) {
  int size = g->qr->size, d;

  for(int dy = -4; dy <= 4; dy++) {
    for(int dx = -4; dx <= 4; dx++) {
      d = max_abs(dx, dy);
      if(cx + dx >= 0 && cx + dx < size && cy + dy >= 0 && cy + dy < size) {
        set_function(g, cx + dx, cy + dy, d != 2 && d != 4);
      }
    }
  }
}


/**
 * the 15 format bits for the mask (level M), and the dark module beside them
 **/
static void
format_bits(grid_t *g, int mask) {
  int size = g->qr->size, data = FORMAT_M << 3 | mask, rem = data, bits;

  for(int i = 0; i < 10; i++) {
    rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  }
  bits = (data << 10 | rem) ^ 0x5412;
#define BIT(i) ((bits >> (i)) & 1)
  for(int i = 0; i <= 5; i++) {
    set_function(g, 8, i, BIT(i));
  }
  set_function(g, 8, 7, BIT(6));
  set_function(g, 8, 8, BIT(7));
  set_function(g, 7, 8, BIT(8));
  for(int i = 9; i < 15; i++) {
    set_function(g, 14 - i, 8, BIT(i));
  }
  for(int i = 0; i < 8; i++) {
    set_function(g, size - 1 - i, 8, BIT(i));
  }
  for(int i = 8; i < 15; i++) {
    set_function(g, 8, size - 15 + i, BIT(i));
  }
#undef BIT
  set_function(g, 8, size - 8, 1);
}


static void
function_patterns(grid_t *g) {
  int size = g->qr->size, ver = g->qr->version, at;

  for(int i = 0; i < size; i++) {
    set_function(g, 6, i, i % 2 == 0);
    set_function(g, i, 6, i % 2 == 0);
  }
  finder(g, 3, 3);
  finder(g, size - 4, 3);
  finder(g, 3, size - 4);
  if(ver >= 2) {
    // up to version 6 a single one, at the corner the finders leave
    at = size - 7;
    for(int dy = -2; dy <= 2; dy++) {
      for(int dx = -2; dx <= 2; dx++) {
        set_function(g, at + dx, at + dy, max_abs(dx, dy) != 1);
      }
    }
  }
  format_bits(g, 0); // the place kept; the mask's go in at the end
}


static void
place_codewords(grid_t *g, const unsigned char *data, int len) {
  int size = g->qr->size, i = 0, x, y, up;

  for(int right = size - 1; right >= 1; right -= 2) {
    if(right == 6) {
      right = 5; // the vertical timing line
    }
    for(int vert = 0; vert < size; vert++) {
      for(int j = 0; j < 2; j++) {
        x = right - j;
        up = ((right + 1) & 2) == 0;
        y = up ? size - 1 - vert : vert;
        if(!g->function[y][x] && i < len * 8) {
          g->qr->dark[y][x] = (data[i >> 3] >> (7 - (i & 7))) & 1;
          i++;
        }
      }
    }
  }
}


static int
masked(int mask, int x, int y) {
  switch(mask) {
  case 0: return (x + y) % 2 == 0;
  case 1: return y % 2 == 0;
  case 2: return x % 3 == 0;
  case 3: return (x + y) % 3 == 0;
  case 4: return (x / 3 + y / 2) % 2 == 0;
  case 5: return x * y % 2 + x * y % 3 == 0;
  case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
  default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
  }
}


static void
apply_mask(grid_t *g, int mask) {
  for(int y = 0; y < g->qr->size; y++) {
    for(int x = 0; x < g->qr->size; x++) {
      if(!g->function[y][x] && masked(mask, x, y)) {
        g->qr->dark[y][x] ^= 1;
      }
    }
  }
}


/**
 * whether the 11 modules from x, y along a line (dx, dy) are 1:1:3:1:1 with
 * four light ones on one side, the look of a finder the readers mind
 **/
static int
finder_like(const qr_t *q, int x, int y, int dx, int dy) {
  static const unsigned char a[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
  int same_a = 1, same_b = 1;

  for(int i = 0; i < 11; i++) {
    int m = q->dark[y + i * dy][x + i * dx];
    same_a &= m == a[i];
    same_b &= m == a[10 - i];
  }
  return same_a || same_b;
}


/**
 * how bad the code looks to a reader: the standard's four rules
 **/
static long
penalty(const qr_t *q) {
  int size = q->size, run, dark = 0, total = size * size, k;
  long score = 0;

  for(int y = 0; y < size; y++) {
    // runs of five or more alike, along the rows and down the columns
    for(int pass = 0; pass < 2; pass++) {
      run = 1;
      for(int x = 1; x <= size; x++) {
        int same = x < size && (pass ? q->dark[x][y] == q->dark[x - 1][y] : q->dark[y][x] == q->dark[y][x - 1]);
        if(same) {
          run++;
        } else {
          score += run >= 5 ? run - 2 : 0;
          run = 1;
        }
      }
    }
  }
  for(int y = 0; y + 1 < size; y++) {
    for(int x = 0; x + 1 < size; x++) {
      int c = q->dark[y][x];
      score += 3 * (c == q->dark[y][x + 1] && c == q->dark[y + 1][x] && c == q->dark[y + 1][x + 1]);
    }
  }
  for(int y = 0; y < size; y++) {
    for(int x = 0; x < size; x++) {
      dark += q->dark[y][x];
      score += 40 * (x + 11 <= size && finder_like(q, x, y, 1, 0));
      score += 40 * (y + 11 <= size && finder_like(q, x, y, 0, 1));
    }
  }
  // dark and light balanced: 10 for each 5% away from half
  k = dark * 20 - total * 10;
  k = k < 0 ? -k : k;
  k = (k + total - 1) / total - 1;
  score += 10L * (k > 0 ? k : 0);
  return score;
}


int
qr_encode(const char *text, size_t len, qr_t *qr) {
  static unsigned char data[256], blocks[8][128], out[400], div[32];
  int ver, cap, bits = 0, n, ecc, nblocks, raw, short_blocks, short_len, k, at;
  long best = -1, p;
  grid_t g;

  for(ver = 1; ver <= QR_MAX_VERSION && (int)len + 2 > data_codewords(ver); ver++) {
  }
  if(ver > QR_MAX_VERSION) {
    return -1;
  }
  cap = data_codewords(ver);

  // byte mode, the count in 8 bits, the bytes, the terminator, the pad
  memset(data, 0, sizeof data);
#define PUT(value, count) \
  for(int b_ = (count) - 1; b_ >= 0; b_--, bits++) \
    data[bits >> 3] |= (unsigned char)((((value) >> b_) & 1) << (7 - (bits & 7)))
  PUT(4, 4);
  PUT((int)len, 8);
  for(size_t i = 0; i < len; i++) {
    PUT((unsigned char)text[i], 8);
  }
  n = cap * 8 - bits < 4 ? cap * 8 - bits : 4;
  PUT(0, n);
  bits = (bits + 7) & ~7;
#undef PUT
  for(int i = bits / 8, pad = 0xEC; i < cap; i++, pad ^= 0xEC ^ 0x11) {
    data[i] = (unsigned char)pad;
  }

  // in blocks, each with its error correction, then interleaved
  ecc = ECC_PER_BLOCK[ver];
  nblocks = BLOCKS[ver];
  raw = raw_modules(ver) / 8;
  short_blocks = nblocks - raw % nblocks;
  short_len = raw / nblocks;
  rs_divisor(ecc, div);
  k = 0;
  for(int i = 0; i < nblocks; i++) {
    int dlen = short_len - ecc + (i < short_blocks ? 0 : 1);
    memset(blocks[i], 0, sizeof blocks[i]);
    memcpy(blocks[i], data + k, (size_t)dlen);
    k += dlen;
    // a short block keeps a place, left out below, where the long ones have a byte more
    rs_remainder(data + k - dlen, dlen, div, ecc, blocks[i] + short_len + 1 - ecc);
  }
  at = 0;
  for(int i = 0; i < short_len + 1; i++) {
    for(int j = 0; j < nblocks; j++) {
      if(i != short_len - ecc || j >= short_blocks) {
        out[at++] = blocks[j][i];
      }
    }
  }

  memset(qr, 0, sizeof *qr);
  memset(&g, 0, sizeof g);
  g.qr = qr;
  qr->version = ver;
  qr->size = ver * 4 + 17;
  function_patterns(&g);
  place_codewords(&g, out, at);
  for(int m = 0; m < 8; m++) {
    apply_mask(&g, m);
    format_bits(&g, m);
    p = penalty(qr);
    if(best < 0 || p < best) {
      best = p;
      qr->mask = m;
    }
    apply_mask(&g, m); // undone
  }
  apply_mask(&g, qr->mask);
  format_bits(&g, qr->mask);
  return 0;
}
