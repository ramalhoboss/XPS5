/* A baseline JPEG writer, for the screenshots a new post carries: X takes
pictures of up to 5 MB, and a PS5 screenshot in PNG is often bigger. 4:2:0,
the standard tables of ITU T.81 annex K, quality as libjpeg counts it. */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * w x h pixels (0xAABBGGRR, the alpha left out) as a JPEG at quality 1 to
 * 100; *out is mem_alloc'd (mem_free it). Returns 0, or -1 without memory
 **/
int jpeg_encode(const uint32_t *pixels, int w, int h, int quality, uint8_t **out, size_t *len);

/**
 * w x h bytes of one channel (an Ultra HDR gain map) as a greyscale JPEG,
 * the same way
 **/
int jpeg_encode_gray(const uint8_t *gray, int w, int h, int quality, uint8_t **out, size_t *len);
