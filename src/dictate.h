/* Dictation without the internet: speech to text on the console, with
whisper.cpp (third_party/whisper.cpp, built by tools/build-whisper.sh) and a
Whisper model in ggml form (models/whisper/ggml-tiny.bin). Neither the sound
nor its words leave the console, and neither goes to the klog: whisper.cpp's
own messages are silenced. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define DICTATE_RATE    16000 // the samples a second it takes, mono, as the microphone's voice modes give
#define DICTATE_MAX_S   30    // the seconds of sound one run takes at most (Whisper's window)

typedef struct dictate dictate_t;

/**
 * the model at path, read with threads threads at work later (1 to 8); NULL
 * when it does not open or is not a Whisper model
 **/
dictate_t *dictate_open(const char *path, int threads);

/**
 * the words in n samples of sound (DICTATE_RATE a second, mono, 16-bit, at
 * most DICTATE_MAX_S seconds), spoken in lang ("pt", "en", "es"...; NULL or
 * "" to guess it), into text (UTF-8, ended, without the spaces around it);
 * stop, when not NULL, is looked at as it works and ends it early when it
 * turns non-zero. 0, 1 when nothing was said, or -1
 **/
int dictate_run(dictate_t *d, const int16_t *samples, size_t n, const char *lang, char *text, size_t size,
                const volatile int *stop);

/**
 * the model and all it holds, freed
 **/
void dictate_close(dictate_t *d);
