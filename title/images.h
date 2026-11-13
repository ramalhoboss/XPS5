/* The pictures of the timeline, avatars and media: the screen asks for them
by URL and size on every frame it draws, a loader (threads of the title's
main.c, or the preview's stand-in) fetches what was asked, and they are
decoded with stb_image, scaled to that size and kept while they fit in a
memory budget.

A picture images_get returns stays valid until the next images_frame, which
only the thread that draws calls. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define IMAGES_URL_MAX 256


/**
 * the most memory the pictures may take, in bytes
 **/
void images_init(size_t budget);

/**
 * the start of a frame: lets go of pictures over the budget
 **/
void images_frame(void);

/**
 * the picture for url scaled and cropped to w x h (0xAABBGGRR); NULL while
 * it loads, which asks for it, or when it could not be had
 **/
const uint32_t *images_get(const char *url, int w, int h);

/**
 * for the loader: the next picture wanted, the most recently asked first;
 * returns 0 when there is none
 **/
int images_next(char *url, size_t size, int *w, int *h);

/**
 * for the loader: the file of a picture images_next gave, or NULL when it
 * could not be fetched
 **/
void images_done(const char *url, int w, int h, const void *data, size_t len);

/**
 * the same with the w x h pixels already made (malloc'd, the cache keeps
 * them), or NULL
 **/
void images_done_pixels(const char *url, int w, int h, uint32_t *pixels);

/**
 * ask again for the pictures that could not be had
 **/
void images_retry(void);

/**
 * whether a picture came in since the last call
 **/
int images_arrived(void);

/**
 * a JPEG, PNG or GIF (its first frame) scaled to cover w x h and cropped
 * around its middle; malloc'd, or NULL
 **/
uint32_t *image_fit(const void *data, size_t len, int w, int h);

/**
 * the size of a JPEG, PNG or GIF that can be decoded; returns 0, or -1 when
 * it cannot
 **/
int image_size(const void *data, size_t len, int *w, int *h);

/**
 * the whole picture, its longer side made at most max_side, and its new
 * size in w and h; malloc'd, or NULL
 **/
uint32_t *image_scaled(const void *data, size_t len, int max_side, int *w, int *h);
