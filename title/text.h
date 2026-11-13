/* TrueType text for the title: UTF-8 decoding, glyphs rasterized with
stb_truetype and cached, line breaking, and drawing into a 32-bit buffer.
Emoji are drawn as pictures once text_set_emoji says where to get them;
characters neither the font nor its fallback has are skipped. */

#pragma once

#include <stddef.h>
#include <stdint.h>


typedef struct text_font text_font_t;

/**
 * the picture of an emoji, size x size pixels in 0xAABBGGRR with alpha,
 * or NULL while there is none; name is its code points in lowercase hex
 * joined by "-", the way Twemoji names its files
 **/
typedef const uint32_t *(*text_emoji_t)(const char *name, int size);

/**
 * where text and shapes are drawn. Places and sizes are given in points:
 * a point is scale pixels (0 counts as 1), so the screen keeps its layout
 * of 1920 x 1080 points at 1440p or 4K. A band of a taller surface (see
 * text_band) starts pixels at its first row and keeps top, the point that
 * row is at in the whole, so what is drawn in it lands on the same pixels
 * as it would in the whole
 **/
typedef struct text_surface {
  uint32_t *pixels; // 0xAABBGGRR, one row after the other
  int       width;  // in points
  int       height;
  float     scale;
  int       top;
} text_surface_t;


/**
 * the pixels a point of s is
 **/
float text_scale(const text_surface_t *s);

/**
 * the column of pixels point x starts at, and a width of w points in
 * pixels (as pictures are made for it)
 **/
int text_px(const text_surface_t *s, float x);
int text_size(const text_surface_t *s, float w);

/**
 * the row of pixels of s point y starts at
 **/
int text_py(const text_surface_t *s, float y);

/**
 * where point x, y falls in pixels, not rounded, for curves
 **/
float text_fx(const text_surface_t *s, float x);
float text_fy(const text_surface_t *s, float y);

/**
 * s counted in pixels: scale 1, its own rows from 0
 **/
text_surface_t text_pixels(const text_surface_t *s);

/**
 * the rows of s from point top down, height points high
 **/
text_surface_t text_band(const text_surface_t *s, int top, int height);

/**
 * a font at a pixel height, from TrueType data that must stay alive
 **/
text_font_t *text_font_open(const unsigned char *ttf, float pixel_height);

/**
 * the same for surfaces of scale (above 1): measured in points, as at
 * pixel_height, and drawn from glyphs of scale times that height
 **/
text_font_t *text_font_open_scaled(const unsigned char *ttf, float pixel_height, float scale);
void text_font_close(text_font_t *font);

/**
 * take the characters font lacks from fallback, which must outlive it;
 * NULL for none
 **/
void text_font_set_fallback(text_font_t *font, text_font_t *fallback);

/**
 * measure emoji sequences as squares of about the font's height and draw
 * them with the pictures emoji gives; NULL draws them as characters again
 **/
void text_set_emoji(text_emoji_t emoji);

int text_ascent(const text_font_t *font);
int text_line_height(const text_font_t *font);

/**
 * next code point of s, advancing *s; U+FFFD for a bad byte, 0 at end
 **/
uint32_t text_next(const char **s, const char *end);

/**
 * width in pixels of len bytes of UTF-8
 **/
int text_width(text_font_t *font, const char *s, size_t len);

/**
 * how many of the first len bytes of s fit in width pixels, in whole
 * characters
 **/
size_t text_fit(text_font_t *font, const char *s, size_t len, int width);

/**
 * the next line of the NUL-terminated s that fits in width pixels, broken
 * at a space when it can and always at '\n'; returns its length and sets
 * *next to where the line after it starts, which is past s unless *s is 0
 **/
size_t text_break(text_font_t *font, const char *s, int width, const char **next);

/**
 * draw len bytes with the pen at x and the baseline at y, clipped to the
 * surface, which has the font's scale; returns the x after them
 **/
int text_draw(text_font_t *font, const text_surface_t *dst, int x, int y,
              const char *s, size_t len, uint32_t color);

/**
 * UTF-16, as the PS5's keyboard gives text, to UTF-8 in buf (size bytes,
 * at least 1), cut at a whole character; a lone surrogate becomes U+FFFD;
 * returns the length
 **/
size_t text_from_utf16(const uint16_t *s, char *buf, size_t size);

/**
 * UTF-8 to UTF-16 in buf (count units, at least 1), cut at a whole
 * character and ending with 0
 **/
void text_to_utf16(const char *s, uint16_t *buf, size_t count);
