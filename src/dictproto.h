/* What the title and the dictation payload (src/dictated.c) say to each
other, on 127.0.0.1:DICT_PORT only, one request a connection, in the
console's own byte order (little-endian at both ends):

  the title:   DICT_MAGIC, u32 DICT_VERSION, char lang[DICT_LANG] ("pt",
               "en"..., or "" to guess it), u32 samples, then the samples:
               16-bit, mono, DICT_RATE a second, at most DICT_MAX_SAMPLES
  the payload: u32 its version, i32 status (DICT_*), u32 length, then the
               text, UTF-8, at most DICT_TEXT_MAX bytes

The title closing its end while the words are made stops them. Neither end
writes the sound or the words anywhere, the klog included: only counts and
codes go there. */

#pragma once

#include <stdint.h>

#define DICT_PORT        9049       // beside the notifications' 9047
#define DICT_VERSION     2          // a payload of another version answers DICT_OLD and goes
                                    // (2: one thread, the lowest priority; 1 took the system's cores)
#define DICT_MAGIC       "XPS5DICT" // its 8 bytes, without the 0
#define DICT_LANG        8
#define DICT_RATE        16000
#define DICT_MAX_S       30         // Whisper's window
#define DICT_MAX_SAMPLES (DICT_RATE * DICT_MAX_S)
#define DICT_TEXT_MAX    2048
#define DICT_HEAD        (8 + 4 + DICT_LANG + 4) // the title's bytes before the samples
#define DICT_ANSWER      12                      // the payload's before the text

// where the installer puts the model (src/install.c), for the payload: the
// tiny one in 8 bits (models/whisper/ggml-tiny-q8_0.bin), under the name it
// had in 16
#define DICT_MODEL_DIR   "/data/TwitterPS5/whisper"
#define DICT_MODEL       DICT_MODEL_DIR "/ggml-tiny.bin"

enum {
  DICT_OK       = 0,  // the words, in the text
  DICT_NOTHING  = 1,  // nothing was said
  DICT_FAILED   = -1, // the model did not run, or the title stopped it
  DICT_NO_MODEL = -2, // the model is not there: the installer did not carry it
  DICT_OLD      = -3, // the title is of another version
  DICT_BAD      = -4, // not a request
};
