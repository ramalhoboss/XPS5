/* A Whisper model in ggml form with its big matrices in 8 bits (q8_0), as
whisper.cpp's own quantize example makes its *-q8_0.bin models: the 2D
weights become Q8_0, while the positional embeddings, the convolutions'
biases, the convolutions (3D) and the norms (1D) stay as they are, which is
what whisper.cpp's loader then expects of a model whose ftype says Q8_0.
The header, the mel filters and the vocabulary are copied as they come.

tools/whisper-q8.sh builds it with tools/build-whisper.sh's ggml and makes
models/whisper/ggml-tiny-q8_0.bin, the model the installer carries, from
models/whisper/ggml-tiny.bin.

Use: whisper-quantize <model in, 16 or 32-bit> <model out> */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ggml.h"

#define MAGIC 0x67676d6c // "ggml"


static FILE *in, *out;
static int   failed;

static void
get(void *buf, size_t len) {
  if(!failed && fread(buf, 1, len, in) != len) {
    fprintf(stderr, "whisper-quantize: the model ends early\n");
    failed = 1;
  }
}

static void
put(const void *buf, size_t len) {
  if(!failed && fwrite(buf, 1, len, out) != len) {
    fprintf(stderr, "whisper-quantize: cannot write\n");
    failed = 1;
  }
}

/**
 * len bytes copied from in to out, a block at a time
 **/
static void
copy(size_t len) {
  static unsigned char buf[1 << 16];

  while(len && !failed) {
    size_t n = len < sizeof buf ? len : sizeof buf;
    get(buf, n);
    put(buf, n);
    len -= n;
  }
}

static int32_t
get32(void) {
  int32_t v = 0;

  get(&v, 4);
  return v;
}

static void
put32(int32_t v) {
  put(&v, 4);
}


/**
 * whether a tensor of the model goes to 8 bits: whisper.cpp makes its 2D
 * weights in the model's type, and these few in 32 bits
 **/
static int
quantized(const char *name, int n_dims, const int32_t *ne, int32_t ttype) {
  static const char *const kept[] = {
    "encoder.conv1.bias", "encoder.conv2.bias", "encoder.positional_embedding", "decoder.positional_embedding",
  };

  if(n_dims != 2 || (ttype != GGML_TYPE_F32 && ttype != GGML_TYPE_F16) || ne[0] % ggml_blck_size(GGML_TYPE_Q8_0)) {
    return 0;
  }
  for(size_t i=0; i < sizeof kept / sizeof kept[0]; i++) {
    if(!strcmp(name, kept[i])) {
      return 0;
    }
  }
  return 1;
}


int
main(int argc, char **argv) {
  int32_t hparams[11], n_mel, n_fft, n_vocab, n_dims, length, ttype, ne[4];
  long long bytes_in = 0, bytes_out = 0;
  int tensors = 0, eights = 0;
  uint32_t magic = 0;
  char name[256];

  if(argc != 3) {
    fprintf(stderr, "use: whisper-quantize <model in> <model out>\n");
    return 2;
  }
  if(!(in=fopen(argv[1], "rb"))) {
    perror(argv[1]);
    return 1;
  }
  if(!(out=fopen(argv[2], "wb"))) {
    perror(argv[2]);
    return 1;
  }

  // the header: its ftype says Q8_0, in ggml's quantization version
  get(&magic, 4);
  if(failed || magic != MAGIC) {
    fprintf(stderr, "whisper-quantize: %s is not a ggml model\n", argv[1]);
    return 1;
  }
  put(&magic, 4);
  for(int i=0; i < 11; i++) {
    hparams[i] = get32();
  }
  if(hparams[10] % GGML_QNT_VERSION_FACTOR != GGML_FTYPE_ALL_F32 &&
     hparams[10] % GGML_QNT_VERSION_FACTOR != GGML_FTYPE_MOSTLY_F16) {
    fprintf(stderr, "whisper-quantize: the model is quantized already (ftype %d)\n", (int)hparams[10]);
    return 1;
  }
  hparams[10] = GGML_QNT_VERSION * GGML_QNT_VERSION_FACTOR + GGML_FTYPE_MOSTLY_Q8_0;
  for(int i=0; i < 11; i++) {
    put32(hparams[i]);
  }

  // the mel filters and the vocabulary, as they are
  n_mel = get32();
  n_fft = get32();
  if(n_mel <= 0 || n_fft <= 0 || n_mel > 1024 || n_fft > 4096) {
    fprintf(stderr, "whisper-quantize: mel filters of %d x %d\n", (int)n_mel, (int)n_fft);
    return 1;
  }
  put32(n_mel);
  put32(n_fft);
  copy((size_t)n_mel * (size_t)n_fft * sizeof(float));
  n_vocab = get32();
  if(n_vocab <= 0 || n_vocab > 1 << 20) {
    fprintf(stderr, "whisper-quantize: a vocabulary of %d\n", (int)n_vocab);
    return 1;
  }
  put32(n_vocab);
  for(int32_t i=0; i < n_vocab && !failed; i++) {
    uint32_t len = 0;
    get(&len, 4);
    if(len > 4096) {
      fprintf(stderr, "whisper-quantize: a word of %u bytes\n", (unsigned)len);
      return 1;
    }
    put(&len, 4);
    copy(len);
  }

  // the tensors, to the end of the file
  while(!failed && fread(&n_dims, 4, 1, in) == 1) {
    size_t nelements = 1, size;
    void *data;

    length = get32();
    ttype = get32();
    if(n_dims < 1 || n_dims > 4 || length <= 0 || length >= (int32_t)sizeof name ||
       (ttype != GGML_TYPE_F32 && ttype != GGML_TYPE_F16)) {
      fprintf(stderr, "whisper-quantize: a tensor of %d dimensions, name of %d, type %d\n", (int)n_dims, (int)length,
              (int)ttype);
      return 1;
    }
    for(int i=0; i < n_dims; i++) {
      ne[i] = get32();
      if(ne[i] <= 0) {
        fprintf(stderr, "whisper-quantize: a dimension of %d\n", (int)ne[i]);
        return 1;
      }
      nelements *= (size_t)ne[i];
    }
    get(name, (size_t)length);
    name[length] = 0;
    size = nelements * (ttype == GGML_TYPE_F32 ? sizeof(float) : sizeof(ggml_fp16_t));
    if(failed || !(data=malloc(size))) {
      return 1;
    }
    get(data, size);
    bytes_in += (long long)size;
    tensors++;

    if(quantized(name, n_dims, ne, ttype)) {
      float *f32 = ttype == GGML_TYPE_F32 ? data : malloc(nelements * sizeof *f32);
      void *q = malloc(ggml_row_size(GGML_TYPE_Q8_0, ne[0]) * (nelements / (size_t)ne[0]));
      size_t qsize;

      if(!f32 || !q) {
        return 1;
      }
      if(ttype == GGML_TYPE_F16) {
        ggml_fp16_to_fp32_row(data, f32, (int64_t)nelements);
      }
      qsize = ggml_quantize_chunk(GGML_TYPE_Q8_0, f32, q, 0, (int64_t)(nelements / (size_t)ne[0]), ne[0], NULL);
      put32(n_dims);
      put32(length);
      put32(GGML_TYPE_Q8_0);
      put(ne, (size_t)n_dims * 4);
      put(name, (size_t)length);
      put(q, qsize);
      bytes_out += (long long)qsize;
      eights++;
      if(f32 != data) {
        free(f32);
      }
      free(q);
    } else {
      put32(n_dims);
      put32(length);
      put32(ttype);
      put(ne, (size_t)n_dims * 4);
      put(name, (size_t)length);
      put(data, size);
      bytes_out += (long long)size;
    }
    free(data);
  }
  fclose(in);
  if(fclose(out) || failed) {
    fprintf(stderr, "whisper-quantize: %s is not whole\n", argv[2]);
    return 1;
  }
  printf("whisper-quantize: %d tensors, %d of them in 8 bits; %.1f MiB of weights became %.1f MiB\n", tensors, eights,
         bytes_in / 1048576.0, bytes_out / 1048576.0);
  return 0;
}
