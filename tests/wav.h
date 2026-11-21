/* What the dictation tests share: a WAV file of 16 kHz mono 16-bit PCM read
into samples, and text flattened for a comparison. */

#pragma once

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static unsigned
le16(const unsigned char *p) {
  return p[0] | p[1] << 8;
}

static unsigned
le32(const unsigned char *p) {
  return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
}


/**
 * the samples of a WAV file of 16-bit PCM, 16 kHz, mono; NULL if it is not
 * one
 **/
static int16_t *
read_wav(const char *path, size_t *count) {
  unsigned char *buf = NULL, *p, *end;
  int16_t *samples = NULL;
  int fmt_ok = 0;
  long len;
  FILE *f;

  if(!(f=fopen(path, "rb")) || fseek(f, 0, SEEK_END) || (len=ftell(f)) < 44 || fseek(f, 0, SEEK_SET) ||
     !(buf=malloc((size_t)len)) || fread(buf, 1, (size_t)len, f) != (size_t)len ||
     memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) {
    if(f) {
      fclose(f);
    }
    free(buf);
    return NULL;
  }
  fclose(f);
  end = buf + len;
  for(p=buf + 12; p + 8 <= end; p += 8 + ((le32(p + 4) + 1) & ~1u)) {
    unsigned size = le32(p + 4);
    if(p + 8 + size > end) {
      break;
    }
    if(!memcmp(p, "fmt ", 4) && size >= 16) {
      fmt_ok = le16(p + 8) == 1 && le16(p + 10) == 1 && le32(p + 12) == 16000 && le16(p + 22) == 16;
    } else if(!memcmp(p, "data", 4) && fmt_ok && (samples=malloc(size))) {
      *count = size / 2;
      for(size_t i=0; i < *count; i++) {
        samples[i] = (int16_t)le16(p + 8 + 2 * i);
      }
      break;
    }
  }
  free(buf);
  return samples;
}


/**
 * text in small letters, its punctuation as spaces, for a comparison that
 * does not hang on Whisper's commas
 **/
static void
plain(const char *text, char *out, size_t size) {
  size_t n = 0;

  for(; *text && n + 1 < size; text++) {
    out[n++] = isalpha((unsigned char)*text) ? (char)tolower((unsigned char)*text) : ' ';
  }
  out[n] = 0;
}
