/* A voice note as an MP4 of AAC-LC sound and H.264 video (see voice.h). */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "voice.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// libavcodec's tables (libavcodec/aactab.h): the spectral codebooks' and
// the scalefactors' Huffman codes, and the bands of a long block
extern const uint16_t *const ff_aac_spectral_codes[11];
extern const uint8_t *const  ff_aac_spectral_bits[11];
extern const uint32_t        ff_aac_scalefactor_code[121];
extern const uint8_t         ff_aac_scalefactor_bits[121];
extern const uint16_t *const ff_swb_offset_1024[13];
extern const uint8_t         ff_aac_num_swb_1024[];

#define AAC_RATE_INDEX 3     // 48000 in the sampling frequency table
#define AAC_HALF       (AAC_FRAME / 2)
#define AAC_FFT        (AAC_FRAME / 2) // the complex FFT under the MDCT
#define AAC_TOP_HZ     8000  // the microphone's sound goes no higher
#define AAC_ESC_CB     11
#define AAC_MAX_BITS   6144  // of a channel's frame
#define AAC_SF_ZERO    60    // the scalefactors' code of no change
#define UP_TAPS        48    // the low-pass filter's, 16 for each of the three phases
#define UP_CUT         0.15  // its cut, of AAC_RATE: 7.2 kHz

#define MB_W           (VOICE_W / 16)
#define MB_H           (VOICE_H / 16)
#define MBS            (MB_W * MB_H)


/*
 * Bits written first-to-last into a growing buffer.
 */

typedef struct bits {
  uint8_t *buf;
  size_t   len, room;
  unsigned cur;  // the bits of the byte not written yet
  int      n;    // how many
  int      fail; // out of memory
} bits_t;


static void
put_byte(bits_t *b, unsigned byte) {
  if(b->len == b->room) {
    size_t room = b->room ? b->room * 2 : 4096;
    uint8_t *buf = b->fail ? NULL : mem_realloc(b->buf, room);
    if(!buf) {
      b->fail = 1;
      return;
    }
    b->buf = buf;
    b->room = room;
  }
  b->buf[b->len++] = (uint8_t)byte;
}


static void
put(bits_t *b, uint32_t value, int count) {
  for(int i=count - 1; i >= 0; i--) {
    b->cur = (b->cur << 1 | (value >> i & 1)) & 0xff;
    if(++b->n == 8) {
      put_byte(b, b->cur);
      b->n = 0;
      b->cur = 0;
    }
  }
}


static void
align(bits_t *b) {
  if(b->n) {
    put(b, 0, 8 - b->n);
  }
}


static void
put_ue(bits_t *b, uint32_t v) {
  int len = 0;

  for(uint64_t x=(uint64_t)v + 1; x; x >>= 1) {
    len++;
  }
  put(b, 0, len - 1);
  if(len > 32) { // not with the values written here
    put(b, 1, 1);
    put(b, v + 1, 32);
  } else {
    put(b, v + 1, len);
  }
}


static void
put_se(bits_t *b, int v) {
  put_ue(b, v > 0 ? (uint32_t)(2 * v - 1) : (uint32_t)(-2 * v));
}


/*
 * The sound.
 */

/**
 * the samples three times over, through the low-pass filter: n * 3 of them
 * into out
 **/
static void
upsample(const int16_t *in, size_t n, float *out) {
  float h[UP_TAPS];
  const double mid = (UP_TAPS - 1) / 2.0;

  for(int t=0; t < UP_TAPS; t++) {
    // a windowed sinc (Blackman), three times as loud: two in three of the
    // samples it sums are the zeros between the microphone's
    const double x = t - mid, w = 0.42 - 0.5 * cos(2 * M_PI * t / (UP_TAPS - 1)) +
                                  0.08 * cos(4 * M_PI * t / (UP_TAPS - 1));
    const double s = x == 0 ? 2 * UP_CUT : sin(2 * M_PI * UP_CUT * x) / (M_PI * x);
    h[t] = (float)(3 * s * w);
  }
  for(size_t i=0; i < n * 3; i++) {
    float sum = 0;
    // the taps that land on one of the microphone's samples: (i - t) % 3 == 0
    for(int t=(int)(i % 3); t < UP_TAPS; t += 3) {
      const size_t at = (i - (size_t)t) / 3;
      if(i >= (size_t)t && at < n) {
        sum += h[t] * in[at];
      }
    }
    out[i] = sum;
  }
}


typedef struct mdct {
  float window[AAC_FRAME * 2];
  float pre_c[AAC_FFT], pre_s[AAC_FFT];   // exp(-i pi (n + 1/8) / AAC_FRAME)
  float fft_c[AAC_FFT / 2], fft_s[AAC_FFT / 2];
  int   rev[AAC_FFT];
} mdct_t;


static void
mdct_init(mdct_t *m) {
  int bits = 0;

  for(int n=0; n < AAC_FRAME * 2; n++) {
    m->window[n] = (float)sin(M_PI / (AAC_FRAME * 2) * (n + 0.5)); // the sine window
  }
  for(int n=0; n < AAC_FFT; n++) {
    m->pre_c[n] = (float)cos(M_PI * (n + 0.125) / AAC_FRAME);
    m->pre_s[n] = (float)-sin(M_PI * (n + 0.125) / AAC_FRAME);
  }
  for(int k=0; k < AAC_FFT / 2; k++) {
    m->fft_c[k] = (float)cos(2 * M_PI * k / AAC_FFT);
    m->fft_s[k] = (float)-sin(2 * M_PI * k / AAC_FFT);
  }
  while((1 << bits) < AAC_FFT) {
    bits++;
  }
  for(int n=0; n < AAC_FFT; n++) {
    int r = 0;
    for(int i=0; i < bits; i++) {
      r |= (n >> i & 1) << (bits - 1 - i);
    }
    m->rev[n] = r;
  }
}


/**
 * the forward FFT of AAC_FFT points, in place
 **/
static void
fft(const mdct_t *m, float *re, float *im) {
  for(int n=0; n < AAC_FFT; n++) {
    const int r = m->rev[n];
    if(r > n) {
      float t = re[n];
      re[n] = re[r];
      re[r] = t;
      t = im[n];
      im[n] = im[r];
      im[r] = t;
    }
  }
  for(int size=2; size <= AAC_FFT; size *= 2) {
    const int half = size / 2, step = AAC_FFT / size;
    for(int start=0; start < AAC_FFT; start += size) {
      for(int k=0; k < half; k++) {
        const float wr = m->fft_c[k * step], wi = m->fft_s[k * step];
        const int a = start + k, b = a + half;
        const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
      }
    }
  }
}


/**
 * the MDCT of 2 * AAC_FRAME samples (windowed here) as AAC defines it,
 * 2 * sum z[n] cos(pi / M (n + M/2 + 1/2)(k + 1/2)), into AAC_FRAME
 * coefficients: the input folded into a DCT-IV, done with a complex FFT of
 * half its size
 **/
static void
mdct(const mdct_t *m, const float *x, float *out) {
  float z[AAC_FRAME * 2], v[AAC_FRAME], re[AAC_FFT], im[AAC_FFT];
  const int M = AAC_FRAME;

  for(int n=0; n < 2 * M; n++) {
    z[n] = x[n] * m->window[n];
  }
  for(int n=0; n < M / 2; n++) {
    v[n] = -z[3 * M / 2 - 1 - n] - z[3 * M / 2 + n];
    v[M / 2 + n] = z[n] - z[M - 1 - n];
  }
  for(int n=0; n < AAC_FFT; n++) {
    const float a = v[2 * n], b = v[M - 1 - 2 * n];
    re[n] = a * m->pre_c[n] - b * m->pre_s[n];
    im[n] = a * m->pre_s[n] + b * m->pre_c[n];
  }
  fft(m, re, im);
  for(int k=0; k < AAC_FFT; k++) {
    const float r = re[k] * m->pre_c[k] - im[k] * m->pre_s[k];
    const float i = re[k] * m->pre_s[k] + im[k] * m->pre_c[k];
    out[2 * k] = 2 * r;
    out[M - 1 - 2 * k] = -2 * i;
  }
}


/**
 * the escape sequence of a value of 16 or more, and its length
 **/
static int
escape_len(int v) {
  int top = 4;

  while((v >> (top + 1)) > 0) {
    top++;
  }
  return (top - 4) + 1 + top;
}


static void
put_escape(bits_t *b, int v) {
  int top = 4;

  while((v >> (top + 1)) > 0) {
    top++;
  }
  for(int i=4; i < top; i++) {
    put(b, 1, 1);
  }
  put(b, 0, 1);
  put(b, (uint32_t)(v - (1 << top)), top);
}


typedef struct frame_plan {
  int q[AAC_FRAME];
  int bands, top; // the bands that may hold sound; the last with some, + 1
  int zero[64];   // a band all of zeros
  int bits;
} frame_plan_t;


/**
 * the frame's coefficients quantized with the scalefactor gain, and the
 * bits they take written as voice_aac writes them; INT32_MAX when one is
 * too loud for AAC's 8191
 **/
static int
plan_frame(frame_plan_t *p, const float *mag, const float *coef, const uint16_t *swb, int gain) {
  const float step = powf(2.0f, -0.1875f * (float)(gain - 100));
  const uint8_t *cb_bits = ff_aac_spectral_bits[AAC_ESC_CB - 1];
  int bits = 3 + 4 + 8 + 1 + 2 + 1 + 6 + 1 + 3 + 3; // the element, the gain, ics_info, the flags, the end
  int run = 0, cb = -1;

  p->top = 0;
  for(int b=0; b < p->bands; b++) {
    int any = 0;
    for(int k=swb[b]; k < swb[b + 1]; k++) {
      const int q = (int)(mag[k] * step + 0.4054f);
      if(q > 8191) {
        return INT32_MAX;
      }
      p->q[k] = coef[k] < 0 ? -q : q;
      any |= q;
    }
    p->zero[b] = !any;
    p->top = any ? b + 1 : p->top;
  }
  for(int b=0; b < p->top; b++) {
    const int this_cb = p->zero[b] ? 0 : AAC_ESC_CB;
    // the sections: a codebook and a length in 5-bit steps
    if(this_cb != cb) {
      if(cb >= 0) {
        bits += 4 + 5 * (run / 31 + 1);
      }
      cb = this_cb;
      run = 0;
    }
    run++;
    if(p->zero[b]) {
      continue;
    }
    bits += ff_aac_scalefactor_bits[AAC_SF_ZERO];
    for(int k=swb[b]; k < swb[b + 1]; k += 2) {
      const int y = abs(p->q[k]), z = abs(p->q[k + 1]);
      const int ay = y < 16 ? y : 16, az = z < 16 ? z : 16;
      bits += cb_bits[ay * 17 + az] + (y != 0) + (z != 0);
      bits += (ay == 16 ? escape_len(y) : 0) + (az == 16 ? escape_len(z) : 0);
    }
  }
  if(cb >= 0) {
    bits += 4 + 5 * (run / 31 + 1);
  }
  p->bits = bits;
  return bits;
}


static void
write_frame(bits_t *b, const frame_plan_t *p, const uint16_t *swb, int gain) {
  const uint16_t *codes = ff_aac_spectral_codes[AAC_ESC_CB - 1];
  const uint8_t *lens = ff_aac_spectral_bits[AAC_ESC_CB - 1];

  put(b, 0, 3);              // ID_SCE
  put(b, 0, 4);              // its tag
  put(b, (uint32_t)gain, 8); // global_gain
  put(b, 0, 1);              // ics_reserved_bit
  put(b, 0, 2);              // ONLY_LONG_SEQUENCE
  put(b, 0, 1);              // the sine window
  put(b, (uint32_t)p->top, 6);
  put(b, 0, 1);              // no prediction
  for(int at=0; at < p->top;) {
    const int cb = p->zero[at] ? 0 : AAC_ESC_CB;
    int len = 0;
    while(at + len < p->top && (p->zero[at + len] ? 0 : AAC_ESC_CB) == cb) {
      len++;
    }
    put(b, (uint32_t)cb, 4);
    at += len;
    while(len >= 31) {
      put(b, 31, 5);
      len -= 31;
    }
    put(b, (uint32_t)len, 5);
  }
  for(int band=0; band < p->top; band++) {
    if(!p->zero[band]) {
      put(b, ff_aac_scalefactor_code[AAC_SF_ZERO], ff_aac_scalefactor_bits[AAC_SF_ZERO]); // the gain all along
    }
  }
  put(b, 0, 1); // no pulses
  put(b, 0, 1); // no TNS
  put(b, 0, 1); // no gain control
  for(int band=0; band < p->top; band++) {
    if(p->zero[band]) {
      continue;
    }
    for(int k=swb[band]; k < swb[band + 1]; k += 2) {
      const int y = abs(p->q[k]), z = abs(p->q[k + 1]);
      const int ay = y < 16 ? y : 16, az = z < 16 ? z : 16;
      put(b, codes[ay * 17 + az], lens[ay * 17 + az]);
      if(y) {
        put(b, p->q[k] < 0, 1);
      }
      if(z) {
        put(b, p->q[k + 1] < 0, 1);
      }
      if(ay == 16) {
        put_escape(b, y);
      }
      if(az == 16) {
        put_escape(b, z);
      }
    }
  }
  put(b, 7, 3); // ID_END
  align(b);
}


void
voice_aac_config(uint8_t out[2]) {
  // AAC-LC (2), 48 kHz (3), mono (1), 1024 samples a frame
  out[0] = (uint8_t)(2 << 3 | AAC_RATE_INDEX >> 1);
  out[1] = (uint8_t)((AAC_RATE_INDEX & 1) << 7 | 1 << 3);
}


int
voice_aac(const int16_t *pcm, size_t n, int bitrate, uint8_t **out, uint32_t **sizes, int *frames) {
  const uint16_t *swb = ff_swb_offset_1024[AAC_RATE_INDEX];
  const int target = (int)((long long)bitrate * AAC_FRAME / AAC_RATE);
  const size_t n48 = n * 3;
  const int count = (int)((n48 + AAC_FRAME - 1) / AAC_FRAME) + 1;
  float *up = mem_calloc(n48 + 3 * AAC_FRAME, sizeof *up), block[AAC_FRAME * 2], coef[AAC_FRAME], mag[AAC_FRAME];
  mdct_t *m = mem_alloc(sizeof *m);
  frame_plan_t *plan = mem_alloc(sizeof *plan);
  uint32_t *each = mem_calloc((size_t)count, sizeof *each);
  bits_t b;
  int bands = 0;

  memset(&b, 0, sizeof b);
  if(!up || !m || !plan || !each) {
    mem_free(up);
    mem_free(m);
    mem_free(plan);
    mem_free(each);
    return -1;
  }
  upsample(pcm, n, up + AAC_FRAME); // a frame of nothing first: the encoder's delay
  mdct_init(m);
  while(bands < ff_aac_num_swb_1024[AAC_RATE_INDEX] &&
        swb[bands] < AAC_TOP_HZ * 2 * AAC_FRAME / AAC_RATE) {
    bands++;
  }
  plan->bands = bands;

  for(int f=0; f < count && !b.fail; f++) {
    int lo = 0, hi = 255, gain;
    const size_t before = b.len;
    // the block of this frame and the next: frame f's sound comes out of
    // the decoder with frame f + 1
    memcpy(block, up + (size_t)f * AAC_FRAME, sizeof block);
    mdct(m, block, coef);
    for(int k=0; k < AAC_FRAME; k++) {
      mag[k] = powf(fabsf(coef[k]), 0.75f);
    }
    // the finest gain whose bits fit (more gain, coarser steps, fewer bits)
    while(lo < hi) {
      const int mid = (lo + hi) / 2;
      if(plan_frame(plan, mag, coef, swb, mid) <= target) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    gain = lo;
    if(plan_frame(plan, mag, coef, swb, gain) > AAC_MAX_BITS) {
      gain = 255; // nothing fits: as coarse as AAC goes
      plan_frame(plan, mag, coef, swb, gain);
    }
    write_frame(&b, plan, swb, gain);
    each[f] = (uint32_t)(b.len - before);
  }
  mem_free(up);
  mem_free(m);
  mem_free(plan);
  if(b.fail) {
    mem_free(b.buf);
    mem_free(each);
    return -1;
  }
  *out = b.buf;
  *sizes = each;
  *frames = count;
  return 0;
}


/*
 * The picture.
 */

/**
 * the RBSP of a NAL unit with its header byte, the emulation prevention
 * bytes put in, into out; its length
 **/
static size_t
nal_escape(unsigned header, const uint8_t *rbsp, size_t len, uint8_t *out) {
  size_t o = 0;
  int zeros = 0;

  out[o++] = (uint8_t)header;
  for(size_t i=0; i < len; i++) {
    if(zeros >= 2 && rbsp[i] <= 3) {
      out[o++] = 3;
      zeros = 0;
    }
    out[o++] = rbsp[i];
    zeros = rbsp[i] ? 0 : zeros + 1;
  }
  return o;
}


static void
trailing(bits_t *b) {
  put(b, 1, 1);
  align(b);
}


static void
write_sps(bits_t *b) {
  put(b, 66, 8);   // Baseline
  put(b, 0xc0, 8); // constraint_set0 and 1: Constrained Baseline
  put(b, 41, 8);   // level 4.1
  put_ue(b, 0);    // seq_parameter_set_id
  put_ue(b, 12);   // log2_max_frame_num_minus4: 16 bits
  put_ue(b, 2);    // pic_order_cnt_type: the order of decoding
  put_ue(b, 1);    // max_num_ref_frames
  put(b, 0, 1);    // gaps_in_frame_num_value_allowed_flag
  put_ue(b, MB_W - 1);
  put_ue(b, MB_H - 1);
  put(b, 1, 1);    // frame_mbs_only_flag
  put(b, 1, 1);    // direct_8x8_inference_flag
  put(b, 0, 1);    // frame_cropping_flag
  put(b, 1, 1);    // vui_parameters_present_flag
  put(b, 1, 1);    // aspect_ratio_info_present_flag
  put(b, 1, 8);    // square pixels
  put(b, 0, 1);    // overscan_info_present_flag
  put(b, 1, 1);    // video_signal_type_present_flag
  put(b, 5, 3);    // video_format: unspecified
  put(b, 0, 1);    // limited range
  put(b, 1, 1);    // colour_description_present_flag
  put(b, 1, 8);    // BT.709 primaries,
  put(b, 1, 8);    // transfer
  put(b, 1, 8);    // and matrix
  put(b, 0, 1);    // chroma_loc_info_present_flag
  put(b, 1, 1);    // timing_info_present_flag
  put(b, 1, 32);   // num_units_in_tick
  put(b, 2 * VOICE_FPS, 32); // time_scale: two ticks a frame
  put(b, 1, 1);    // fixed_frame_rate_flag
  put(b, 0, 1);    // nal_hrd_parameters_present_flag
  put(b, 0, 1);    // vcl_hrd_parameters_present_flag
  put(b, 0, 1);    // pic_struct_present_flag
  put(b, 1, 1);    // bitstream_restriction_flag
  put(b, 1, 1);    // motion_vectors_over_pic_boundaries_flag
  put_ue(b, 0);    // max_bytes_per_pic_denom
  put_ue(b, 0);    // max_bits_per_mb_denom
  put_ue(b, 16);   // log2_max_mv_length_horizontal
  put_ue(b, 16);   // log2_max_mv_length_vertical
  put_ue(b, 0);    // max_num_reorder_frames: shown as decoded
  put_ue(b, 1);    // max_dec_frame_buffering
  trailing(b);
}


static void
write_pps(bits_t *b) {
  put_ue(b, 0);  // pic_parameter_set_id
  put_ue(b, 0);  // seq_parameter_set_id
  put(b, 0, 1);  // CAVLC
  put(b, 0, 1);  // bottom_field_pic_order_in_frame_present_flag
  put_ue(b, 0);  // num_slice_groups_minus1
  put_ue(b, 0);  // num_ref_idx_l0_default_active_minus1
  put_ue(b, 0);  // num_ref_idx_l1_default_active_minus1
  put(b, 0, 1);  // weighted_pred_flag
  put(b, 0, 2);  // weighted_bipred_idc
  put_se(b, 0);  // pic_init_qp_minus26
  put_se(b, 0);  // pic_init_qs_minus26
  put_se(b, 0);  // chroma_qp_index_offset
  put(b, 1, 1);  // deblocking_filter_control_present_flag
  put(b, 0, 1);  // constrained_intra_pred_flag
  put(b, 0, 1);  // redundant_pic_cnt_present_flag
  trailing(b);
}


static uint8_t
clamp_sample(float v) {
  return (uint8_t)(v < 1 ? 1 : v > 254 ? 254 : v + 0.5f);
}


/**
 * the IDR frame: the picture in I_PCM macroblocks, as BT.709 limited range
 * 4:2:0
 **/
static void
write_idr(bits_t *b, const uint32_t *px) {
  put_ue(b, 0);  // first_mb_in_slice
  put_ue(b, 7);  // I, as every slice of the picture
  put_ue(b, 0);  // pic_parameter_set_id
  put(b, 0, 16); // frame_num
  put_ue(b, 0);  // idr_pic_id
  put(b, 0, 1);  // no_output_of_prior_pics_flag
  put(b, 0, 1);  // long_term_reference_flag
  put_se(b, 0);  // slice_qp_delta
  put_ue(b, 1);  // no deblocking
  for(int mb=0; mb < MBS && !b->fail; mb++) {
    const int x0 = mb % MB_W * 16, y0 = mb / MB_W * 16;
    put_ue(b, 25); // I_PCM
    align(b);
    for(int y=0; y < 16; y++) {
      for(int x=0; x < 16; x++) {
        const uint32_t c = px[(size_t)(y0 + y) * VOICE_W + (size_t)(x0 + x)];
        const float r = (float)(c & 0xff), g = (float)(c >> 8 & 0xff), bl = (float)(c >> 16 & 0xff);
        put_byte(b, clamp_sample(16 + (0.2126f * r + 0.7152f * g + 0.0722f * bl) * 219.0f / 255.0f));
      }
    }
    for(int plane=0; plane < 2; plane++) {
      for(int y=0; y < 8; y++) {
        for(int x=0; x < 8; x++) {
          float r = 0, g = 0, bl = 0, luma, v;
          for(int i=0; i < 4; i++) {
            const uint32_t c = px[(size_t)(y0 + 2 * y + i / 2) * VOICE_W + (size_t)(x0 + 2 * x + i % 2)];
            r += (float)(c & 0xff) / 4;
            g += (float)(c >> 8 & 0xff) / 4;
            bl += (float)(c >> 16 & 0xff) / 4;
          }
          luma = 0.2126f * r + 0.7152f * g + 0.0722f * bl;
          v = plane == 0 ? (bl - luma) / 1.8556f : (r - luma) / 1.5748f;
          put_byte(b, clamp_sample(128 + v * 224.0f / 255.0f));
        }
      }
    }
  }
  trailing(b);
}


/**
 * a P frame of nothing but skipped macroblocks: the picture before again
 **/
static void
write_skip(bits_t *b, int frame) {
  put_ue(b, 0);                      // first_mb_in_slice
  put_ue(b, 5);                      // P, as every slice of the picture
  put_ue(b, 0);                      // pic_parameter_set_id
  put(b, (uint32_t)frame & 0xffff, 16); // frame_num
  put(b, 0, 1);                      // num_ref_idx_active_override_flag
  put(b, 0, 1);                      // ref_pic_list_modification_flag_l0
  put(b, 0, 1);                      // adaptive_ref_pic_marking_mode_flag
  put_se(b, 0);                      // slice_qp_delta
  put_ue(b, 1);                      // no deblocking
  put_ue(b, MBS);                    // mb_skip_run: all of them
  trailing(b);
}


/**
 * a NAL unit with its 4-byte length into the frames' buffer, from the RBSP
 * in b; 0, or -1 without memory
 **/
static int
add_nal(bits_t *out, unsigned header, bits_t *b) {
  uint8_t *nal = b->fail ? NULL : mem_alloc(b->len + b->len / 2 + 8);
  size_t len;

  if(!nal) {
    return -1;
  }
  len = nal_escape(header, b->buf, b->len, nal);
  put(out, (uint32_t)len, 32);
  for(size_t i=0; i < len; i++) {
    put_byte(out, nal[i]);
  }
  mem_free(nal);
  return out->fail ? -1 : 0;
}


int
voice_h264(const uint32_t *pixels, int frames, uint8_t sps[64], size_t *sps_len, uint8_t pps[16],
           size_t *pps_len, uint8_t **out, uint32_t **sizes) {
  uint32_t *each = mem_calloc((size_t)(frames > 0 ? frames : 1), sizeof *each);
  bits_t all, b;
  int ret = 0;

  memset(&all, 0, sizeof all);
  memset(&b, 0, sizeof b);
  if(!each || frames < 1) {
    mem_free(each);
    return -1;
  }
  write_sps(&b);
  if(b.fail || b.len * 2 + 1 > 64) {
    ret = -1;
  } else {
    *sps_len = nal_escape(0x67, b.buf, b.len, sps); // nal_ref_idc 3, SPS
  }
  b.len = 0;
  write_pps(&b);
  if(b.fail || b.len * 2 + 1 > 16) {
    ret = -1;
  } else {
    *pps_len = nal_escape(0x68, b.buf, b.len, pps); // nal_ref_idc 3, PPS
  }
  for(int f=0; f < frames && !ret; f++) {
    const size_t before = all.len;
    b.len = 0;
    if(f == 0) {
      write_idr(&b, pixels);
      ret = add_nal(&all, 0x65, &b); // nal_ref_idc 3, IDR slice
    } else {
      write_skip(&b, f);
      ret = add_nal(&all, 0x41, &b); // nal_ref_idc 2, slice
    }
    each[f] = (uint32_t)(all.len - before);
  }
  mem_free(b.buf);
  if(ret || all.fail) {
    mem_free(all.buf);
    mem_free(each);
    return -1;
  }
  *out = all.buf;
  *sizes = each;
  return 0;
}


/*
 * The file.
 */

static void
u8(bits_t *b, unsigned v) {
  put_byte(b, v & 0xff);
}


static void
u16(bits_t *b, unsigned v) {
  put_byte(b, v >> 8 & 0xff);
  put_byte(b, v & 0xff);
}


static void
u32(bits_t *b, uint32_t v) {
  put_byte(b, v >> 24);
  put_byte(b, v >> 16 & 0xff);
  put_byte(b, v >> 8 & 0xff);
  put_byte(b, v & 0xff);
}


static void
bytes(bits_t *b, const void *data, size_t len) {
  for(size_t i=0; i < len; i++) {
    put_byte(b, ((const uint8_t *)data)[i]);
  }
}


/**
 * a box begun: its size written once it ends (box_end)
 **/
static size_t
box(bits_t *b, const char *type) {
  const size_t at = b->len;

  u32(b, 0);
  bytes(b, type, 4);
  return at;
}


static size_t
full_box(bits_t *b, const char *type, unsigned version, uint32_t flags) {
  const size_t at = box(b, type);

  u32(b, (uint32_t)version << 24 | flags);
  return at;
}


static void
box_end(bits_t *b, size_t at) {
  const uint32_t size = (uint32_t)(b->len - at);

  if(!b->fail) {
    b->buf[at] = (uint8_t)(size >> 24);
    b->buf[at + 1] = (uint8_t)(size >> 16);
    b->buf[at + 2] = (uint8_t)(size >> 8);
    b->buf[at + 3] = (uint8_t)size;
  }
}


static void
matrix(bits_t *b) {
  static const uint32_t unit[9] = {0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000};

  for(int i=0; i < 9; i++) {
    u32(b, unit[i]);
  }
}


static void
patch32(bits_t *b, size_t at, uint32_t v) {
  if(!b->fail) {
    b->buf[at] = (uint8_t)(v >> 24);
    b->buf[at + 1] = (uint8_t)(v >> 16);
    b->buf[at + 2] = (uint8_t)(v >> 8);
    b->buf[at + 3] = (uint8_t)v;
  }
}


typedef struct track_out {
  const uint32_t *sizes;
  int             count;
  uint32_t        timescale, delta; // every sample as long
  size_t          offset_at;        // where its chunk's offset goes, once known
} track_out_t;


/**
 * the tables of a track's samples, all in one chunk
 **/
static void
sample_tables(bits_t *b, track_out_t *t, int video) {
  size_t at = full_box(b, "stts", 0, 0);
  u32(b, 1);
  u32(b, (uint32_t)t->count);
  u32(b, t->delta);
  box_end(b, at);
  if(video) {
    at = full_box(b, "stss", 0, 0); // the first frame is the one to start at
    u32(b, 1);
    u32(b, 1);
    box_end(b, at);
  }
  at = full_box(b, "stsc", 0, 0);
  u32(b, 1);
  u32(b, 1);
  u32(b, (uint32_t)t->count);
  u32(b, 1);
  box_end(b, at);
  at = full_box(b, "stsz", 0, 0);
  u32(b, 0);
  u32(b, (uint32_t)t->count);
  for(int i=0; i < t->count; i++) {
    u32(b, t->sizes[i]);
  }
  box_end(b, at);
  at = full_box(b, "stco", 0, 0);
  u32(b, 1);
  t->offset_at = b->len;
  u32(b, 0);
  box_end(b, at);
}


static void
handler(bits_t *b, const char *type, const char *name) {
  const size_t at = full_box(b, "hdlr", 0, 0);

  u32(b, 0);
  bytes(b, type, 4);
  u32(b, 0);
  u32(b, 0);
  u32(b, 0);
  bytes(b, name, strlen(name) + 1);
  box_end(b, at);
}


static void
data_info(bits_t *b) {
  const size_t dinf = box(b, "dinf"), dref = full_box(b, "dref", 0, 0);
  size_t url;

  u32(b, 1);
  url = full_box(b, "url ", 0, 1); // in this file
  box_end(b, url);
  box_end(b, dref);
  box_end(b, dinf);
}


static void
track_head(bits_t *b, unsigned id, uint32_t duration_ms, int audio) {
  const size_t at = full_box(b, "tkhd", 0, 3); // enabled, in the movie

  u32(b, 0);
  u32(b, 0);
  u32(b, id);
  u32(b, 0);
  u32(b, duration_ms);
  u32(b, 0);
  u32(b, 0);
  u16(b, 0);           // layer
  u16(b, 0);           // alternate_group
  u16(b, audio ? 0x100 : 0);
  u16(b, 0);
  matrix(b);
  u32(b, audio ? 0 : (uint32_t)VOICE_W << 16);
  u32(b, audio ? 0 : (uint32_t)VOICE_H << 16);
  box_end(b, at);
}


static void
media_head(bits_t *b, uint32_t timescale, uint32_t duration) {
  const size_t at = full_box(b, "mdhd", 0, 0);

  u32(b, 0);
  u32(b, 0);
  u32(b, timescale);
  u32(b, duration);
  u16(b, 0x55c4); // "und"
  u16(b, 0);
  box_end(b, at);
}


static void
video_track(bits_t *b, track_out_t *t, const uint8_t *sps, size_t sps_len, const uint8_t *pps, size_t pps_len,
            uint32_t duration_ms) {
  static const uint8_t name[32] = {0};
  size_t trak = box(b, "trak"), mdia, minf, stbl, stsd, avc1, avcc, at;

  track_head(b, 1, duration_ms, 0);
  mdia = box(b, "mdia");
  media_head(b, t->timescale, (uint32_t)t->count * t->delta);
  handler(b, "vide", "VideoHandler");
  minf = box(b, "minf");
  at = full_box(b, "vmhd", 0, 1);
  u16(b, 0);
  u16(b, 0);
  u16(b, 0);
  u16(b, 0);
  box_end(b, at);
  data_info(b);
  stbl = box(b, "stbl");
  stsd = full_box(b, "stsd", 0, 0);
  u32(b, 1);
  avc1 = box(b, "avc1");
  bytes(b, name, 6);   // reserved
  u16(b, 1);           // data_reference_index
  bytes(b, name, 16);  // pre_defined, reserved
  u16(b, VOICE_W);
  u16(b, VOICE_H);
  u32(b, 0x00480000);  // 72 dpi
  u32(b, 0x00480000);
  u32(b, 0);
  u16(b, 1);           // frame_count
  bytes(b, name, 32);  // compressorname
  u16(b, 0x18);        // depth
  u16(b, 0xffff);      // pre_defined -1
  avcc = box(b, "avcC");
  u8(b, 1);            // configurationVersion
  u8(b, sps[1]);       // its profile, compatibility and level
  u8(b, sps[2]);
  u8(b, sps[3]);
  u8(b, 0xff);         // lengths of 4 bytes
  u8(b, 0xe1);         // one SPS
  u16(b, (unsigned)sps_len);
  bytes(b, sps, sps_len);
  u8(b, 1);            // one PPS
  u16(b, (unsigned)pps_len);
  bytes(b, pps, pps_len);
  box_end(b, avcc);
  box_end(b, avc1);
  box_end(b, stsd);
  sample_tables(b, t, 1);
  box_end(b, stbl);
  box_end(b, minf);
  box_end(b, mdia);
  box_end(b, trak);
}


static void
audio_track(bits_t *b, track_out_t *t, uint32_t duration_ms, uint32_t bitrate) {
  uint8_t config[2];
  size_t trak = box(b, "trak"), edts, mdia, minf, stbl, stsd, mp4a, esds, at;

  voice_aac_config(config);
  track_head(b, 2, duration_ms, 1);
  // the encoder's frame of delay left out of what plays
  edts = box(b, "edts");
  at = full_box(b, "elst", 0, 0);
  u32(b, 1);
  u32(b, duration_ms);
  u32(b, AAC_FRAME);
  u32(b, 0x00010000);
  box_end(b, at);
  box_end(b, edts);
  mdia = box(b, "mdia");
  media_head(b, t->timescale, (uint32_t)t->count * t->delta);
  handler(b, "soun", "SoundHandler");
  minf = box(b, "minf");
  at = full_box(b, "smhd", 0, 0);
  u16(b, 0);
  u16(b, 0);
  box_end(b, at);
  data_info(b);
  stbl = box(b, "stbl");
  stsd = full_box(b, "stsd", 0, 0);
  u32(b, 1);
  mp4a = box(b, "mp4a");
  u32(b, 0);
  u16(b, 0);
  u16(b, 1);            // data_reference_index
  u32(b, 0);
  u32(b, 0);
  u16(b, 1);            // mono
  u16(b, 16);
  u16(b, 0);
  u16(b, 0);
  u32(b, (uint32_t)AAC_RATE << 16);
  esds = full_box(b, "esds", 0, 0);
  u8(b, 3);             // ES_Descriptor
  u8(b, 25);
  u16(b, 2);            // ES_ID
  u8(b, 0);
  u8(b, 4);             // DecoderConfigDescriptor
  u8(b, 17);
  u8(b, 0x40);          // MPEG-4 audio
  u8(b, 0x15);          // an audio stream
  u8(b, 0);             // bufferSizeDB: a frame at most
  u16(b, AAC_MAX_BITS / 8);
  u32(b, bitrate);      // maxBitrate
  u32(b, bitrate);      // avgBitrate
  u8(b, 5);             // DecoderSpecificInfo
  u8(b, 2);
  u8(b, config[0]);
  u8(b, config[1]);
  u8(b, 6);             // SLConfigDescriptor
  u8(b, 1);
  u8(b, 2);
  box_end(b, esds);
  box_end(b, mp4a);
  box_end(b, stsd);
  sample_tables(b, t, 0);
  box_end(b, stbl);
  box_end(b, minf);
  box_end(b, mdia);
  box_end(b, trak);
}


int
voice_mp4(const int16_t *pcm, size_t n, const uint32_t *pixels, uint8_t **out, size_t *len) {
  uint8_t *sound = NULL, *video = NULL, sps[64], pps[16];
  uint32_t *sound_sizes = NULL, *video_sizes = NULL, duration_ms, avg;
  size_t sps_len = 0, pps_len = 0, sound_len = 0, video_len = 0, moov, at;
  int frames = 0, video_frames;
  track_out_t vt, at_;
  bits_t b;

  memset(&b, 0, sizeof b);
  if(n < (size_t)VOICE_RATE * VOICE_MIN_SECONDS) {
    return -1;
  }
  if(n > (size_t)VOICE_RATE * VOICE_MAX_SECONDS) {
    n = (size_t)VOICE_RATE * VOICE_MAX_SECONDS;
  }
  if(voice_aac(pcm, n, VOICE_BITRATE, &sound, &sound_sizes, &frames)) {
    return -1;
  }
  for(int i=0; i < frames; i++) {
    sound_len += sound_sizes[i];
  }
  // as long as the sound, the encoder's delay left out
  duration_ms = (uint32_t)((uint64_t)(frames - 1) * AAC_FRAME * 1000 / AAC_RATE);
  video_frames = (int)(((uint64_t)(frames - 1) * AAC_FRAME * VOICE_FPS + AAC_RATE - 1) / AAC_RATE);
  if(voice_h264(pixels, video_frames, sps, &sps_len, pps, &pps_len, &video, &video_sizes)) {
    mem_free(sound);
    mem_free(sound_sizes);
    return -1;
  }
  for(int i=0; i < video_frames; i++) {
    video_len += video_sizes[i];
  }
  avg = duration_ms ? (uint32_t)((uint64_t)sound_len * 8000 / duration_ms) : VOICE_BITRATE;

  at = box(&b, "ftyp");
  bytes(&b, "isom", 4);
  u32(&b, 0x200);
  bytes(&b, "isomiso2avc1mp41", 16);
  box_end(&b, at);

  moov = box(&b, "moov");
  at = full_box(&b, "mvhd", 0, 0);
  u32(&b, 0);
  u32(&b, 0);
  u32(&b, 1000);
  u32(&b, duration_ms);
  u32(&b, 0x00010000); // rate
  u16(&b, 0x100);      // volume
  u16(&b, 0);
  u32(&b, 0);
  u32(&b, 0);
  matrix(&b);
  for(int i=0; i < 6; i++) {
    u32(&b, 0);
  }
  u32(&b, 3);          // next_track_ID
  box_end(&b, at);
  vt = (track_out_t){video_sizes, video_frames, VOICE_FPS * 1000, 1000, 0};
  at_ = (track_out_t){sound_sizes, frames, AAC_RATE, AAC_FRAME, 0};
  video_track(&b, &vt, sps, sps_len, pps, pps_len, duration_ms);
  audio_track(&b, &at_, duration_ms, avg > VOICE_BITRATE ? avg : VOICE_BITRATE);
  box_end(&b, moov);

  // one mdat: the video's chunk, then the sound's
  if(!b.fail) {
    const uint64_t total = b.len + 8 + video_len + sound_len;
    patch32(&b, vt.offset_at, (uint32_t)(b.len + 8));
    patch32(&b, at_.offset_at, (uint32_t)(b.len + 8 + video_len));
    u32(&b, total - b.len > UINT32_MAX ? 0 : (uint32_t)(video_len + sound_len + 8));
    bytes(&b, "mdat", 4);
    bytes(&b, video, video_len);
    bytes(&b, sound, sound_len);
  }
  mem_free(sound);
  mem_free(sound_sizes);
  mem_free(video);
  mem_free(video_sizes);
  if(b.fail) {
    mem_free(b.buf);
    return -1;
  }
  *out = b.buf;
  *len = b.len;
  return 0;
}


void
voice_bars(const int16_t *pcm, size_t n, float *bars, int count) {
  float top = 0;

  for(int i=0; i < count; i++) {
    const size_t from = n * (size_t)i / (size_t)count, to = n * (size_t)(i + 1) / (size_t)count;
    double sum = 0;
    for(size_t k=from; k < to; k++) {
      sum += (double)pcm[k] * pcm[k];
    }
    bars[i] = to > from ? (float)sqrt(sqrt(sum / (double)(to - from))) : 0; // a root of the RMS: quiet words show
    top = bars[i] > top ? bars[i] : top;
  }
  for(int i=0; i < count; i++) {
    bars[i] = top > 0 ? bars[i] / top : 0;
  }
}
