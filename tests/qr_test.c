/* Host tests of src/qr.c: each code is read back the way a reader does, apart
from the encoder: the format bits (their BCH code and the mask), the data
modules in their zigzag with the mask taken off, the blocks put back
together and checked as Reed-Solomon codewords, and the bytes read. */

#include <stdio.h>
#include <string.h>

#include "qr.h"

static int g_failed;

#define CHECK(cond) do { \
  if(!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); g_failed = 1; } \
} while(0)

static const int ECC[7]    = {0, 10, 16, 26, 18, 24, 16};
static const int BLOCKS[7] = {0, 1, 1, 1, 2, 2, 4};


static unsigned
mul(unsigned x, unsigned y) {
  unsigned z = 0;

  while(y) {
    if(y & 1) {
      z ^= x;
    }
    x <<= 1;
    if(x & 0x100) {
      x ^= 0x11D;
    }
    y >>= 1;
  }
  return z;
}


/**
 * whether x, y belongs to a pattern every code of that version has
 **/
static int
patterned(int size, int version, int x, int y) {
  if((x < 9 && y < 9) || (x >= size - 8 && y < 9) || (x < 9 && y >= size - 8)) {
    return 1; // the finders, their separators and the format bits
  }
  if(x == 6 || y == 6) {
    return 1;
  }
  if(version >= 2 && x >= size - 9 && x <= size - 5 && y >= size - 9 && y <= size - 5) {
    return 1;
  }
  return 0;
}


static int
mask_bit(int mask, int x, int y) {
  switch(mask) {
  case 0: return (x + y) % 2 == 0;
  case 1: return y % 2 == 0;
  case 2: return x % 3 == 0;
  case 3: return (x + y) % 3 == 0;
  case 4: return (x / 3 + y / 2) % 2 == 0;
  case 5: return (x * y) % 2 + (x * y) % 3 == 0;
  case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
  default: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
  }
}


/**
 * the text a code carries, read back; its length, or -1
 **/
static int
read_back(const qr_t *q, char *text, size_t size) {
  static unsigned char words[200], blocks[8][128], data[200];
  int n = q->size, v = q->version, format = 0, rem, mask, bit = 0, count = 0, raw, nb, ecc, shortb, shortl;
  int at = 0, k = 0, len;

  // the format bits beside the top left finder, 14 down to 0
  for(int x = 0; x <= 5; x++) {
    format = format << 1 | q->dark[8][x];
  }
  format = format << 1 | q->dark[8][7];
  format = format << 1 | q->dark[8][8];
  format = format << 1 | q->dark[7][8];
  for(int y = 5; y >= 0; y--) {
    format = format << 1 | q->dark[y][8];
  }
  format ^= 0x5412;
  rem = format;
  for(int i = 14; i >= 10; i--) {
    if(rem >> i & 1) {
      rem ^= 0x537 << (i - 10);
    }
  }
  if(rem) {
    return -1; // not a BCH codeword
  }
  if((format >> 13) != 0) {
    return -1; // not level M
  }
  mask = format >> 10 & 7;
  if(mask != q->mask || !q->dark[n - 8][8]) {
    return -1;
  }

  // the modules in their zigzag, up and down two columns at a time from the right
  raw = ((16 * v + 128) * v + 64 - (v >= 2 ? (25 * (v / 7 + 2) - 10) * (v / 7 + 2) - 55 : 0)) / 8;
  memset(words, 0, sizeof words);
  for(int right = n - 1; right >= 1; right -= 2) {
    if(right == 6) {
      right = 5;
    }
    for(int i = 0; i < n; i++) {
      int up = ((right + 1) & 2) == 0, y = up ? n - 1 - i : i;
      for(int j = 0; j < 2; j++) {
        int x = right - j;
        if(!patterned(n, v, x, y) && bit < raw * 8) {
          words[bit >> 3] |= (unsigned char)((q->dark[y][x] ^ mask_bit(mask, x, y)) << (7 - (bit & 7)));
          bit++;
        }
      }
    }
  }

  // the blocks back, each a Reed-Solomon codeword
  nb = BLOCKS[v];
  ecc = ECC[v];
  shortb = nb - raw % nb;
  shortl = raw / nb;
  for(int i = 0; i < shortl + 1; i++) {
    for(int j = 0; j < nb; j++) {
      if(i != shortl - ecc || j >= shortb) {
        blocks[j][i] = words[at++];
      }
    }
  }
  for(int j = 0; j < nb; j++) {
    int dlen = shortl - ecc + (j < shortb ? 0 : 1);
    unsigned char cw[128];
    memcpy(cw, blocks[j], (size_t)dlen);
    memcpy(cw + dlen, blocks[j] + shortl + 1 - ecc, (size_t)ecc);
    for(int r = 0; r < ecc; r++) {
      unsigned alpha = 1, s = 0;
      for(int e = 0; e < r; e++) {
        alpha = mul(alpha, 2);
      }
      for(int i = 0; i < dlen + ecc; i++) {
        s = mul(s, alpha) ^ cw[i];
      }
      if(s) {
        return -1; // a syndrome not zero
      }
    }
    memcpy(data + k, cw, (size_t)dlen);
    k += dlen;
  }

  // byte mode, the count, the bytes
  if((data[0] >> 4) != 4) {
    return -1;
  }
  len = (data[0] & 15) << 4 | data[1] >> 4;
  if((size_t)len + 1 > size) {
    return -1;
  }
  for(int i = 0; i < len; i++) {
    text[i] = (char)((data[1 + i] & 15) << 4 | data[2 + i] >> 4);
  }
  text[len] = 0;
  (void)count;
  return len;
}


static void
round_trip(const char *text, int version) {
  static qr_t q;
  char back[200];

  CHECK(!qr_encode(text, strlen(text), &q));
  CHECK(q.version == version);
  CHECK(q.size == version * 4 + 17);
  CHECK(read_back(&q, back, sizeof back) == (int)strlen(text));
  CHECK(!strcmp(back, text));
  // the finders where a reader looks for them
  CHECK(q.dark[0][0] && q.dark[3][3] && !q.dark[1][1] && q.dark[0][q.size - 1] && q.dark[q.size - 1][0]);
  CHECK(!q.dark[7][7] && !q.dark[q.size - 8][7]);
}


int
main(void) {
  char big[200];
  static qr_t q;

  round_trip("XPS5", 1);
  round_trip("abcdefghijklmnopqrst", 2);
  round_trip("http://192.168.1.70:9070/#482913", 3);
  round_trip("http://192.168.100.200:9070/#123456?lang=pt&x=abcdefghijklmnop", 4);
  memset(big, 'a', 80);
  big[80] = 0;
  round_trip(big, 5);
  memset(big, '7', 104);
  big[104] = 0;
  round_trip(big, 6);
  memset(big, 'z', 107);
  big[107] = 0;
  CHECK(qr_encode(big, 107, &q) == -1); // more than version 6 takes
  CHECK(!qr_encode(big, QR_MAX_BYTES, &q) && q.version == 6);
  CHECK(!qr_encode("", 0, &q) && q.version == 1);

  if(g_failed) {
    return 1;
  }
  printf("all QR checks passed\n");
  return 0;
}
