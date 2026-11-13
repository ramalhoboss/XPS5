/* The screen's own sound: a short tick of glass when the pointer moves onto
something it can press and lights it up. It is made here (sound_make), not
read from a file, and plays on an audio port of its own, beside the one a
video's sound has, so the two never wait on each other.

main.c calls sound_tick from the thread that draws; the port and its writing
live on a thread of their own. */

#pragma once

#include <stdint.h>

#define SOUND_RATE   48000
#define SOUND_GRAIN  256   // audio frames a block of the port: 5.3 ms
#define SOUND_BLOCKS 9     // the tick's length in blocks: 48 ms
#define SOUND_FRAMES (SOUND_GRAIN * SOUND_BLOCKS)

/**
 * the tick, one channel, peak at most 1; frames past its end are 0
 **/
void sound_make(float *out, int frames);

/**
 * the port's thread; log takes codes only. Returns 0, or -1 when the
 * thread did not start (then sound_tick does nothing)
 **/
int sound_init(void (*log)(const char *fmt, ...));

/**
 * play the tick, from -1 (all left) to 1 (all right); a tick still
 * playing starts again from the top
 **/
void sound_tick(float pan);
