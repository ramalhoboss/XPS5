/* A clip cut out of an MP4 without encoding it again: from the video's key
frame at or before where it starts to where it ends, with the audio of the
same time. The new file is a head written here (ftyp, a moov with the kept
samples' tables, and the mdat's header) followed by one range of the old
file's bytes, which holds every sample kept: nothing is copied, and the
samples of that range not kept stay in it unreferenced. Nothing here reads a
file: the caller finds the old file's moov (clip_box), parses it
(mp4_parse_moov), and sends clip_make's head and then the range. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mp4.h"

#define CLIP_LAST_MS   30000  // "the last 30 s"
#define CLIP_SHORTEST  1000   // ms a clip lasts at least
#define CLIP_MOOV_MOST (24u << 20) // bytes of an old file's moov read at most

typedef struct clip {
  uint8_t  *head;      // malloc'd: ftyp, moov and the mdat's header
  size_t    head_len;
  uint64_t  from, to;  // the old file's bytes that follow the head: [from, to)
  int64_t   start_ms;  // where the clip starts in the old file (its first key frame)
  int64_t   length_ms; // and how long it plays
} clip_t;

enum {
  CLIP_OK = 0,
  CLIP_NO_VIDEO = -1, // no H.264 video track with samples
  CLIP_EMPTY = -2,    // nothing in the time asked for
  CLIP_MEMORY = -3,
};

/**
 * a top-level box's header at at in a file of file_len bytes, whose first
 * got bytes (16 at most, 8 at least) are in head: its type, its whole size
 * (to the file's end for a size of 0) and how long its header is; 0, or -1
 * when it does not fit the file
 **/
int clip_box(const uint8_t *head, size_t got, uint64_t at, uint64_t file_len, uint32_t *type, uint64_t *size,
             unsigned *head_len);

#define CLIP_FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

/**
 * the clip of m (from mp4_parse_moov) from from_ms to to_ms of the old file
 * (to_ms past its end: to the end), into c; CLIP_OK, or why not
 **/
int clip_make(const mp4_t *m, int64_t from_ms, int64_t to_ms, clip_t *c);

/**
 * how long the new file is: its head and the range
 **/
uint64_t clip_size(const clip_t *c);

void clip_free(clip_t *c);

// the parts of a video a post may carry (capture_t's clip)
enum { CLIP_WHOLE, CLIP_LAST_30, CLIP_LAST_15, CLIP_FIRST_30, CLIP_KINDS };

/**
 * the times of the part which (CLIP_*) of a video duration_ms long: from
 * *from to *to milliseconds; the whole of it when it is no longer
 **/
void clip_span(int which, int64_t duration_ms, int64_t *from, int64_t *to);
