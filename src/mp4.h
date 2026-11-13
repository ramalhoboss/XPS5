/* The samples of an MP4 file held whole in memory: X's videos, which come
either as one moov with the tables of every sample (progressive) or as a
moov followed by fragments (moof and mdat, each with a few seconds). For
the first H.264 video track and the first AAC audio track it gives their
decoder configuration (avcC, AudioSpecificConfig) and every sample's place,
time and whether it is a key frame. Everything read from the file is
checked against its length. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define MP4_MAX_SAMPLES 200000 // of a track; past that the file is refused

typedef struct mp4_sample {
  uint64_t offset;   // in the file
  uint32_t size;
  int64_t  dts;      // in the track's timescale
  int32_t  cts;      // presentation = dts + cts
  uint8_t  key;
} mp4_sample_t;

typedef struct mp4_track {
  int            found;
  uint32_t       id;
  uint32_t       timescale;
  const uint8_t *config;     // avcC, or the AudioSpecificConfig; in the file
  size_t         config_len;
  int            width, height; // of the video
  int            channels, rate; // of the audio
  mp4_sample_t  *samples;    // malloc'd, in decoding order
  int            count, room;
  // what a fragment's samples are when it does not say (trex)
  uint32_t       def_duration, def_size, def_flags;
  int64_t        next_dts;   // where the next fragment starts, when it has no tfdt
} mp4_track_t;

typedef struct mp4 {
  mp4_track_t video, audio;
  int64_t     duration_ms;  // of the longer track
} mp4_t;

enum {
  MP4_OK = 0,
  MP4_NO_MOOV = -1,   // not an MP4, or one cut short
  MP4_NO_TRACK = -2,  // no H.264 video nor AAC audio
  MP4_BAD = -3,       // a box or table that does not fit
  MP4_MEMORY = -4,
};

/**
 * read the file's tracks into m; returns MP4_OK, or why not (m is then
 * empty)
 **/
int mp4_parse(mp4_t *m, const uint8_t *data, size_t len);

/**
 * let go of what mp4_parse took
 **/
void mp4_free(mp4_t *m);

/**
 * the index of the key frame at or before t milliseconds of the track, or 0
 **/
int mp4_key_before(const mp4_track_t *t, int64_t ms);
