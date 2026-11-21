/* Ultra HDR (Google's format, version 1.0): a JPEG of the SDR picture,
which any viewer shows, with a second JPEG behind it, the gain map, which a
viewer on an HDR screen uses to bring the picture back to its HDR light.
The SDR JPEG carries an XMP packet (Adobe's hdrgm namespace and Google's
container, which gives the gain map's length) and an MPF segment (where the
gain map starts); the gain map's own XMP gives how its bytes read. For the
PS5's HDR screenshots (title/jxr.c makes both pictures). */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * the Ultra HDR file of sdr (a JPEG, as jpeg_encode makes it) and gain (the
 * gain map's JPEG): each byte of the gain map is log2 of the boost from 0
 * to gain_max (stops), with gamma 1 and offsets of 1/64; *out is
 * mem_alloc'd. Returns 0, or -1 (not JPEGs, no memory)
 **/
int ultrahdr_join(const uint8_t *sdr, size_t sdr_len, const uint8_t *gain, size_t gain_len, float gain_max,
                  uint8_t **out, size_t *len);

// what a JPEG keeps of Ultra HDR (counts only, for the log)
typedef struct ultrahdr_info {
  int pictures; // JPEGs in the file, one after another (2 for Ultra HDR)
  int xmp;      // the first picture's XMP names a gain map
  int mpf;      // the first picture has an MPF segment
  int gain;     // a gain map's XMP somewhere
  int iso;      // an ISO 21496-1 segment (another way to give a gain map)
} ultrahdr_info_t;

/**
 * look at a JPEG for those
 **/
void ultrahdr_inspect(const uint8_t *jpeg, size_t len, ultrahdr_info_t *info);
