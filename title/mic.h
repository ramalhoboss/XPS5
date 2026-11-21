/* The controller's microphone: the system's libSceAudioIn, which games open
for voice chat. Each block gives its loudness; nothing it hears is kept
except while a voice note is taped (mic_tape), into the title's own memory.

main.c starts it while the Calls page tests it and stops it when the test
ends; a thread of its own reads the blocks, which wait for the microphone.
The log gets codes and counts only. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define MIC_GRAIN 256 // samples a block: 16 ms at 16 kHz

enum { MIC_OFF, MIC_OPENING, MIC_ON, MIC_FAILED };

// sceAudioInGetSilentState's flags
#define MIC_NO_DEVICE    0x01 // nothing to hear with
#define MIC_PRIORITY_LOW 0x02 // another app has the microphone
#define MIC_NO_FORMAT    0x08 // not in the format asked

typedef struct mic_status {
  int      state;  // MIC_*
  int      code;   // why it failed: the SCE error
  int      silent; // MIC_NO_DEVICE and the others, while on
  int      rate;   // samples a second, once open
  float    level;  // 0..1 on the meter
  float    peak;   // 0..1 on the meter, the loudest of late
  unsigned blocks; // blocks heard
} mic_status_t;

/**
 * the loudness of a block: its RMS and its peak, both 0..1 of full scale
 **/
void mic_measure(const int16_t *samples, int n, float *rms, float *peak);

/**
 * where a loudness, 0..1 of full scale, sits on the meter: -60 dBFS and
 * less at 0, full scale at 1
 **/
float mic_meter(float loudness);

/**
 * the thread that opens user's microphone and hears it; 0, or -1 while
 * the last one is still ending
 **/
int mic_start(int user, void (*log)(const char *fmt, ...));

/**
 * the microphone closed by its thread, after the block it waits for
 **/
void mic_stop(void);

/**
 * how it is now
 **/
void mic_get(mic_status_t *status);

#define MIC_TAPE_RATE 16000 // what the tape keeps, whatever the microphone gives

/**
 * the blocks heard from now on kept in tape, at MIC_TAPE_RATE, until room
 * samples are there or mic_tape_end
 **/
void mic_tape(int16_t *tape, size_t room);

/**
 * the samples taped so far
 **/
size_t mic_taped(void);

/**
 * the tape let go of (no block goes into it after this); the samples in it
 **/
size_t mic_tape_end(void);
