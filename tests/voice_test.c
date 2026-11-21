/* Checks of src/voice.c, the voice notes' MP4: its MDCT against AAC's
formula and the overlap of two blocks back to the sound, the bits of a frame
as counted and as written, and the file read back by src/mp4.c. With a path,
the note made of a tone is written there for a decoder to hear. */

#include <stdio.h>
#include <stdlib.h>

#include "../src/voice.c"
#include "mp4.h"

static int failed;

#define CHECK(x) do { if(!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failed = 1; } } while(0)


/**
 * AAC's MDCT as the standard writes it, for 2M samples already windowed
 **/
static void
mdct_slow(const float *z, double *out) {
  const int M = AAC_FRAME;

  for(int k=0; k < M; k++) {
    double sum = 0;
    for(int n=0; n < 2 * M; n++) {
      sum += z[n] * cos(M_PI / M * (n + M / 2.0 + 0.5) * (k + 0.5));
    }
    out[k] = 2 * sum;
  }
}


/**
 * the standard's IMDCT, (2 / N) sum spec[k] cos(...), N = 2M
 **/
static void
imdct_slow(const float *spec, double *out) {
  const int M = AAC_FRAME;

  for(int n=0; n < 2 * M; n++) {
    double sum = 0;
    for(int k=0; k < M; k++) {
      sum += spec[k] * cos(M_PI / M * (n + M / 2.0 + 0.5) * (k + 0.5));
    }
    out[n] = sum / M;
  }
}


static void
mdct_checks(void) {
  static float x[AAC_FRAME * 3], z[AAC_FRAME * 2], a[AAC_FRAME], b[AAC_FRAME];
  static double slow[AAC_FRAME], back_a[AAC_FRAME * 2], back_b[AAC_FRAME * 2];
  mdct_t *m = malloc(sizeof *m);
  double worst = 0, top = 0;

  mdct_init(m);
  srand(7);
  for(int i=0; i < AAC_FRAME * 3; i++) {
    x[i] = (float)(10000 * sin(i * 0.031) + 3000 * sin(i * 0.47) + (rand() % 2001 - 1000));
  }
  mdct(m, x, a);
  for(int n=0; n < AAC_FRAME * 2; n++) {
    z[n] = x[n] * m->window[n];
  }
  mdct_slow(z, slow);
  for(int k=0; k < AAC_FRAME; k++) {
    const double d = fabs(slow[k] - a[k]);
    worst = d > worst ? d : worst;
    top = fabs(slow[k]) > top ? fabs(slow[k]) : top;
  }
  CHECK(worst < top * 1e-5);

  // two blocks overlapping by half: windowed again and added, the sound
  mdct(m, x + AAC_FRAME, b);
  imdct_slow(a, back_a);
  imdct_slow(b, back_b);
  worst = 0;
  for(int n=0; n < AAC_FRAME; n++) {
    const double y = back_a[AAC_FRAME + n] * m->window[AAC_FRAME + n] + back_b[n] * m->window[n];
    const double d = fabs(y - x[AAC_FRAME + n]);
    worst = d > worst ? d : worst;
  }
  CHECK(worst < 0.5);
  free(m);
}


static void
frame_checks(void) {
  const uint16_t *swb = ff_swb_offset_1024[AAC_RATE_INDEX];
  static float coef[AAC_FRAME], mag[AAC_FRAME];
  frame_plan_t *p = malloc(sizeof *p);
  bits_t b;

  srand(11);
  p->bands = 30;
  for(int round=0; round < 50; round++) {
    // loud and quiet bands, some silent, some past the escape's 16
    for(int k=0; k < AAC_FRAME; k++) {
      const int band_loud = (k / 24 + round) % 5;
      coef[k] = (float)((rand() % 2001 - 1000) * (band_loud == 0 ? 0 : band_loud == 4 ? 3000 : band_loud * 40));
      mag[k] = powf(fabsf(coef[k]), 0.75f);
    }
    for(int gain=120; gain <= 220; gain += 25) {
      const int bits = plan_frame(p, mag, coef, swb, gain);
      memset(&b, 0, sizeof b);
      if(bits == INT32_MAX) {
        continue;
      }
      write_frame(&b, p, swb, gain);
      CHECK(!b.fail && b.len == (size_t)(bits + 7) / 8);
      CHECK(b.len && b.buf[0] >> 5 == 0);         // ID_SCE first
      CHECK((b.buf[0] & 1) << 7 | b.buf[1] >> 1); // whatever the gain, some bits
      mem_free(b.buf);
    }
  }
  free(p);
}


static void
note_checks(const char *path) {
  const size_t n = VOICE_RATE * 3 + 4321; // 3.27 s
  int16_t *pcm = malloc(n * sizeof *pcm);
  uint32_t *px = malloc((size_t)VOICE_W * VOICE_H * sizeof *px);
  uint8_t *mp4 = NULL, *frames = NULL;
  uint32_t *sizes = NULL;
  size_t len = 0;
  float bars[32];
  int count = 0;
  mp4_t m;

  for(size_t i=0; i < n; i++) {
    const double t = (double)i / VOICE_RATE, env = 0.5 + 0.5 * sin(2 * M_PI * 0.7 * t);
    pcm[i] = (int16_t)(env * (6000 * sin(2 * M_PI * 440 * t) + 3000 * sin(2 * M_PI * 1250 * t) +
                              1500 * sin(2 * M_PI * 3100 * t)));
  }
  for(int y=0; y < VOICE_H; y++) {
    for(int x=0; x < VOICE_W; x++) {
      const unsigned r = (unsigned)(x * 255 / VOICE_W), g = (unsigned)(y * 255 / VOICE_H), bl = (x / 80 + y / 80) % 2 ? 200 : 40;
      px[(size_t)y * VOICE_W + (size_t)x] = 0xff000000u | bl << 16 | g << 8 | r;
    }
  }

  CHECK(voice_aac(pcm, n, VOICE_BITRATE, &frames, &sizes, &count) == 0);
  CHECK(count == (int)((n * 3 + AAC_FRAME - 1) / AAC_FRAME) + 1);
  for(int i=0; i < count; i++) {
    CHECK(sizes[i] >= 4 && sizes[i] * 8 <= AAC_MAX_BITS);
    CHECK(i == 0 || i == count - 1 || sizes[i] * 8 <= (unsigned)(VOICE_BITRATE * AAC_FRAME / AAC_RATE) + 8);
  }
  mem_free(frames);
  mem_free(sizes);

  CHECK(voice_mp4(pcm, VOICE_RATE / 4, px, &mp4, &len) == -1); // too short for X
  CHECK(voice_mp4(pcm, n, px, &mp4, &len) == 0);
  CHECK(len > (size_t)MBS * 384 && len < (size_t)MBS * 384 + 200000);
  CHECK(mp4_parse(&m, mp4, len) == MP4_OK);
  CHECK(m.video.found && m.video.width == VOICE_W && m.video.height == VOICE_H && m.video.count >= 98 &&
        m.video.count <= 100 && m.video.samples[0].key && !m.video.samples[1].key);
  CHECK(m.audio.found && m.audio.rate == AAC_RATE && m.audio.channels == 1 && m.audio.count == count);
  CHECK(m.duration_ms >= 3260 && m.duration_ms <= 3320);
  CHECK(m.audio.config_len == 2 && m.audio.config[0] == 0x11 && m.audio.config[1] == 0x88);
  CHECK(m.video.config_len > 8 && m.video.config[1] == 66 && m.video.config[3] == 41);
  if(path) {
    FILE *f = fopen(path, "wb");
    CHECK(f && fwrite(mp4, 1, len, f) == len);
    if(f) {
      fclose(f);
    }
    printf("%s: %zu bytes\n", path, len);
  }
  mp4_free(&m);
  mem_free(mp4);

  voice_bars(pcm, n, bars, 32);
  for(int i=0; i < 32; i++) {
    CHECK(bars[i] >= 0 && bars[i] <= 1);
  }
  free(pcm);
  free(px);
}


int
main(int argc, char **argv) {
  mdct_checks();
  frame_checks();
  note_checks(argc > 1 ? argv[1] : NULL);
  if(failed) {
    return 1;
  }
  printf("all voice checks passed\n");
  return 0;
}
