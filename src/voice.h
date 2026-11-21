/* A voice note as a video X takes: the controller's microphone (16 kHz,
mono) and a still picture, made into an MP4 here, as the title's FFmpeg
only decodes. X wants H.264 video with AAC-LC sound, so both are written by
this file, as simply as the formats allow:

  - the sound: AAC-LC, mono, 48 kHz (the microphone's samples three times
    over, through a low-pass filter), long blocks only, one scalefactor for
    the whole of a frame and the escape codebook (11) for every band that is
    not silent; each frame as small as fits the bit rate. The codebook's
    and the scalefactors' Huffman codes are libavcodec's own tables (the
    title links it; the host test reads them from FFmpeg's source);
  - the picture: H.264 Constrained Baseline, 1280x720, one IDR frame of
    I_PCM macroblocks (the picture as it is, BT.709) and then a P frame of
    skipped macroblocks for each 1/30 s the sound lasts;
  - the file: ftyp, moov (one track each, the sound's 1024 samples of
    priming cut by an edit list) and one mdat, the video's chunk first.

It is all in memory: a note of VOICE_MAX_SECONDS makes about 2.5 MB. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define VOICE_RATE        16000 // the microphone's samples a second
#define VOICE_MAX_SECONDS 140   // X's longest video for most accounts
#define VOICE_MIN_SECONDS 1     // X refuses a video under half a second
#define VOICE_W           1280  // the picture
#define VOICE_H           720
#define VOICE_FPS         30
#define VOICE_BITRATE     64000 // the sound's, at most, in bits a second

#define AAC_RATE   48000
#define AAC_FRAME  1024 // samples of a frame

/**
 * n samples at VOICE_RATE as the AAC-LC frames of the same sound at
 * AAC_RATE: their bytes one after the other into *out (mem_alloc'd), the
 * size of each into *sizes (mem_alloc'd), their number into *frames (the
 * first is the encoder's delay, AAC_FRAME samples of nothing). 0, or -1
 * without memory
 **/
int voice_aac(const int16_t *pcm, size_t n, int bitrate, uint8_t **out, uint32_t **sizes, int *frames);

/**
 * the AudioSpecificConfig of voice_aac's frames (2 bytes)
 **/
void voice_aac_config(uint8_t out[2]);

/**
 * the H.264 of the VOICE_W x VOICE_H picture (0xAABBGGRR) shown for frames
 * frames: the SPS and the PPS (NAL units without start codes) into sps and
 * pps, and each frame's NAL unit with its 4-byte length before it into *out
 * (mem_alloc'd), one after the other, the size of each into *sizes
 * (mem_alloc'd). 0, or -1 without memory
 **/
int voice_h264(const uint32_t *pixels, int frames, uint8_t sps[64], size_t *sps_len, uint8_t pps[16],
               size_t *pps_len, uint8_t **out, uint32_t **sizes);

/**
 * the voice note: n samples at VOICE_RATE (at most VOICE_MAX_SECONDS of
 * them) heard over the VOICE_W x VOICE_H picture, as an MP4 into *out
 * (mem_alloc'd); its length into len. 0, or -1 without memory or with too
 * few samples
 **/
int voice_mp4(const int16_t *pcm, size_t n, const uint32_t *pixels, uint8_t **out, size_t *len);

/**
 * how loud the note is along its length: count bars of 0 to 1 (the
 * loudest at 1), for its picture
 **/
void voice_bars(const int16_t *pcm, size_t n, float *bars, int count);
