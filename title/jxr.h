/* The PS5's screenshots, which it saves as JPEG XR (.jxr), as pictures the
screen draws and X takes: decoded by jxrlib (tools/jxrlib), the light of an
HDR one brought into SDR's range (a knee on the luminance, its white the
brightest part of the picture), then sRGB. For a post, an HDR one also gets
a gain map, which brings the SDR picture back to its light on an HDR screen
(Ultra HDR, see ultrahdr.h). */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define JXR_GAIN_SCALE 4 // a pixel of the gain map covers 4 x 4 of the picture

/**
 * whether data starts as a JPEG XR file does
 **/
int jxr_is(const void *data, size_t len);

/**
 * the size of a JPEG XR picture, and its pixel format's code (the last
 * byte of jxrlib's GUID, 0x100 more outside its main family); 0, or -1
 **/
int jxr_size(const void *data, size_t len, int *w, int *h, int *format);

/**
 * whether pixels of that format's code can be brighter than SDR's white
 * (half, float, fixed point and PQ); the PS5 saves its SDR screenshots as
 * .jxr too, 8 bits a channel
 **/
int jxr_format_hdr(int format);

/**
 * a JPEG XR picture as 0xAABBGGRR pixels in SDR: at its whole size when
 * min_w or min_h is 0, else at the smallest of 1/2, 1/4, 1/8 and 1/16 of it
 * that still covers min_w x min_h (those decode faster). Its size goes in
 * *w and *h, its pixel format's code in *format. mem_alloc'd, or NULL with
 * *error set: jxrlib's error, -1 for a format it does not turn into SDR,
 * -2 without memory
 **/
uint32_t *jxr_decode(const void *data, size_t len, int min_w, int min_h, int *w, int *h, int *format,
                     int *error);

/**
 * the same at its whole size, with its gain map in *gain (mem_alloc'd):
 * *gw x *gh bytes, the picture's size over JXR_GAIN_SCALE rounded up, each
 * the log2 of the boost a pixel's luminance gets over SDR, from 0 (none) to
 * 255 (*gain_max, in stops). *gain is NULL when the picture has no light
 * worth it over SDR's white (an SDR screenshot, a dark scene)
 **/
uint32_t *jxr_decode_hdr(const void *data, size_t len, int *w, int *h, int *format, uint8_t **gain,
                         int *gw, int *gh, float *gain_max, int *error);

/**
 * the SDR pixels from the ones jxrlib gives, for the tests: w x h of the
 * format (code as above), stride bytes a row; NULL with *error -1 or -2.
 * With gain not NULL, the gain map as jxr_decode_hdr makes it
 **/
uint32_t *jxr_to_sdr(const void *pixels, int w, int h, int stride, int format, int *error);
uint32_t *jxr_to_sdr_gain(const void *pixels, int w, int h, int stride, int format, uint8_t **gain,
                          int *gw, int *gh, float *gain_max, int *error);
