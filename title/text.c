/* TrueType text for the title, see text.h. Each font keeps the glyphs it
has rasterized in a small open-addressing table, emptied when it fills up.
Text is measured and drawn a unit at a time: a character, or a whole emoji
sequence once text_set_emoji has given a way to draw emoji. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define STBTT_STATIC
#define STBTT_assert(x) ((void)0) // the title's libc has no __assert
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#pragma GCC diagnostic pop

#define CACHE_SLOTS 1024 // a power of two
#define CACHE_LIMIT (CACHE_SLOTS * 3 / 4)
#define EMOJI_MAX   12   // code points in one emoji sequence

#define VS15 0xFE0E // show the character before as text
#define VS16 0xFE0F // as an emoji
#define ZWJ  0x200D
#define KEYCAP 0x20E3


typedef struct glyph {
  uint32_t           cp;    // 0 marks an empty slot
  int                index; // glyph in from, 0 when neither font has one
  const text_font_t *from;  // the font or its fallback
  float              advance; // in points
  int                x0, y0, w, h; // the bitmap, in pixels
  unsigned char     *bitmap;
} glyph_t;

// measured with scale (font units to points) and drawn with pscale (font
// units to pixels), k times it
struct text_font {
  stbtt_fontinfo info;
  float          scale;
  float          pscale;
  float          k;
  float          px;
  int            ascent;
  int            line_height;
  text_font_t   *fallback;
  int            used;
  glyph_t        cache[CACHE_SLOTS];
};

// the glyph before, for kerning, which only applies inside one font
typedef struct prev {
  int                index;
  const text_font_t *from;
} prev_t;

// what the text is measured and drawn by
typedef struct unit {
  uint32_t       cp;    // the character, or the first of an emoji's
  const glyph_t *glyph; // NULL for an emoji
  char           name[EMOJI_MAX * 6]; // an emoji's code points, as its picture is named
} unit_t;

static text_emoji_t g_emoji;


static void
cache_clear(text_font_t *f) {
  for(int i=0; i<CACHE_SLOTS; i++) {
    free(f->cache[i].bitmap);
  }
  memset(f->cache, 0, sizeof f->cache);
  f->used = 0;
}


static const glyph_t *
glyph(text_font_t *f, uint32_t cp) {
  unsigned slot = (cp * 2654435761u) & (CACHE_SLOTS - 1);
  int advance, lsb;
  glyph_t *g;

  if(!cp) {
    cp = 0xFFFD; // 0 marks an empty slot
  }
  for(;; slot = (slot + 1) & (CACHE_SLOTS - 1)) {
    g = &f->cache[slot];
    if(g->cp == cp) {
      return g;
    }
    if(!g->cp) {
      break;
    }
  }

  if(f->used >= CACHE_LIMIT) {
    cache_clear(f);
    return glyph(f, cp);
  }
  f->used++;
  g->cp = cp;
  g->from = f;
  if(!(g->index=stbtt_FindGlyphIndex(&f->info, cp)) && f->fallback &&
     (g->index=stbtt_FindGlyphIndex(&f->fallback->info, cp))) {
    g->from = f->fallback;
  }
  if(g->index) {
    stbtt_GetGlyphHMetrics(&g->from->info, g->index, &advance, &lsb);
    g->advance = advance * g->from->scale;
    g->bitmap = stbtt_GetGlyphBitmap(&g->from->info, g->from->pscale, g->from->pscale,
                                     g->index, &g->w, &g->h, &g->x0, &g->y0);
  }
  return g;
}


text_font_t *
text_font_open(const unsigned char *ttf, float pixel_height) {
  return text_font_open_scaled(ttf, pixel_height, 1);
}


text_font_t *
text_font_open_scaled(const unsigned char *ttf, float pixel_height, float scale) {
  text_font_t *f = calloc(1, sizeof *f);
  int ascent, descent, gap;

  if(!f || !stbtt_InitFont(&f->info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
    free(f);
    return NULL;
  }
  f->px = pixel_height;
  f->k = scale > 1 ? scale : 1;
  f->scale = stbtt_ScaleForPixelHeight(&f->info, pixel_height);
  f->pscale = f->k > 1 ? stbtt_ScaleForPixelHeight(&f->info, pixel_height * f->k) : f->scale;
  stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &gap);
  f->ascent = (int)ceilf(ascent * f->scale);
  f->line_height = (int)ceilf((ascent - descent + gap) * f->scale);
  return f;
}


void
text_font_set_fallback(text_font_t *font, text_font_t *fallback) {
  cache_clear(font);
  font->fallback = fallback;
}


void
text_font_close(text_font_t *font) {
  if(font) {
    cache_clear(font);
    free(font);
  }
}


void
text_set_emoji(text_emoji_t emoji) {
  g_emoji = emoji;
}


float
text_scale(const text_surface_t *s) {
  return s->scale > 0 ? s->scale : 1;
}


int
text_px(const text_surface_t *s, float x) {
  return (int)floorf(x * text_scale(s) + 0.5f);
}


int
text_size(const text_surface_t *s, float w) {
  return (int)floorf(w * text_scale(s) + 0.5f);
}


int
text_py(const text_surface_t *s, float y) {
  const float k = text_scale(s);

  return (int)floorf((s->top + y) * k + 0.5f) - (int)floorf(s->top * k + 0.5f);
}


float
text_fx(const text_surface_t *s, float x) {
  return x * text_scale(s);
}


float
text_fy(const text_surface_t *s, float y) {
  const float k = text_scale(s);

  return (s->top + y) * k - floorf(s->top * k + 0.5f);
}


text_surface_t
text_pixels(const text_surface_t *s) {
  text_surface_t p = {s->pixels, text_px(s, s->width), text_py(s, s->height), 1, 0};

  return p;
}


text_surface_t
text_band(const text_surface_t *s, int top, int height) {
  text_surface_t b = *s;

  b.pixels = s->pixels + (size_t)text_py(s, top) * text_px(s, s->width);
  b.top = s->top + top;
  b.height = height;
  return b;
}


int
text_ascent(const text_font_t *font) {
  return font->ascent;
}


int
text_line_height(const text_font_t *font) {
  return font->line_height;
}


uint32_t
text_next(const char **s, const char *end) {
  const unsigned char *p = (const unsigned char*)*s;
  uint32_t cp;
  int more;

  if(*s >= end) {
    return 0;
  }
  if(p[0] < 0x80) {
    *s += 1;
    return p[0];
  }
  if((p[0] & 0xE0) == 0xC0) {
    cp = p[0] & 0x1F;
    more = 1;
  } else if((p[0] & 0xF0) == 0xE0) {
    cp = p[0] & 0x0F;
    more = 2;
  } else if((p[0] & 0xF8) == 0xF0) {
    cp = p[0] & 0x07;
    more = 3;
  } else {
    *s += 1;
    return 0xFFFD;
  }
  if(end - *s <= more) {
    *s = end;
    return 0xFFFD;
  }
  for(int i=1; i<=more; i++) {
    if((p[i] & 0xC0) != 0x80) {
      *s += i;
      return 0xFFFD;
    }
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  *s += more + 1;
  return cp;
}


/*
 * Emoji.
 */

static int
in(uint32_t cp, uint32_t lo, uint32_t hi) {
  return cp >= lo && cp <= hi;
}


static int
regional(uint32_t cp) {
  return in(cp, 0x1F1E6, 0x1F1FF);
}


/**
 * whether cp shows as an emoji with nothing after it: the pictographs
 * above U+1F000, the BMP characters that have Emoji_Presentation, and the
 * few hearts and hands people write without a VS16
 **/
static int
emoji_alone(uint32_t cp) {
  static const uint32_t ranges[][2] = {
    {0x231A, 0x231B}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0}, {0x23F3, 0x23F3}, {0x25FD, 0x25FE},
    {0x2614, 0x2615}, {0x261D, 0x261D}, {0x263A, 0x263A}, {0x2648, 0x2653}, {0x2665, 0x2665},
    {0x267F, 0x267F}, {0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB}, {0x26BD, 0x26BE},
    {0x26C4, 0x26C5}, {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA}, {0x26F2, 0x26F3},
    {0x26F5, 0x26F5}, {0x26FA, 0x26FA}, {0x26FD, 0x26FD}, {0x2705, 0x2705}, {0x270A, 0x270C},
    {0x2728, 0x2728}, {0x274C, 0x274C}, {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757},
    {0x2764, 0x2764}, {0x2795, 0x2797}, {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C},
    {0x2B50, 0x2B50}, {0x2B55, 0x2B55},
    {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F170, 0x1F251}, {0x1F300, 0x1F6FF},
    {0x1F7E0, 0x1F7EB}, {0x1F900, 0x1F9FF}, {0x1FA70, 0x1FAFF},
  };

  for(size_t i=0; i < sizeof ranges / sizeof ranges[0]; i++) {
    if(in(cp, ranges[i][0], ranges[i][1])) {
      return 1;
    }
  }
  return 0;
}


/**
 * the length of the emoji sequence at s, 0 when none starts there; name
 * gets its code points in hex joined by "-", the way the pictures are
 * named, without the VS16s unless it has a ZWJ
 **/
static size_t
emoji_at(const char *s, const char *end, char *name, size_t size) {
  uint32_t cps[EMOJI_MAX], cp, next;
  const char *p = s, *q;
  int n = 0, zwj = 0, used = 0;

  cp = text_next(&p, end);
  q = p;
  next = text_next(&q, end);
  if(next == VS15) {
    return 0; // asked for as text
  }
  if((cp == '#' || cp == '*' || in(cp, '0', '9'))) {
    // a keycap: the character, perhaps a VS16, then U+20E3
    if(next == VS16) {
      next = text_next(&q, end);
    }
    if(next != KEYCAP) {
      return 0;
    }
  } else if(!emoji_alone(cp) && !(next == VS16 && in(cp, 0x00A9, 0x3299))) {
    return 0;
  }
  cps[n++] = cp;

  for(;;) {
    q = p;
    next = text_next(&q, end);
    if(n == 1 && regional(cp) && regional(next)) {
      cps[n++] = next; // a flag is two of them
      p = q;
      break;
    }
    if(next == VS16 || next == KEYCAP || in(next, 0x1F3FB, 0x1F3FF) || in(next, 0xE0020, 0xE007F)) {
      if(n == EMOJI_MAX) {
        break;
      }
      cps[n++] = next;
      p = q;
      continue;
    }
    if(next == ZWJ && n + 2 <= EMOJI_MAX) {
      const char *r = q;
      uint32_t joined = text_next(&r, end);
      if(joined >= 0x2000) {
        cps[n++] = next;
        cps[n++] = joined;
        zwj = 1;
        p = r;
        continue;
      }
    }
    break;
  }

  name[0] = 0;
  for(int i=0; i<n; i++) {
    if(cps[i] == VS16 && !zwj) {
      continue;
    }
    used += snprintf(name + used, size - used, used ? "-%x" : "%x", (unsigned)cps[i]);
    if((size_t)used >= size) {
      return 0;
    }
  }
  return p - s;
}


/**
 * the side of an emoji's square, about 1.2 em as x.com draws them
 **/
static int
emoji_size(const text_font_t *f) {
  return (int)(f->px + 0.5f);
}


/*
 * Measuring.
 */

/**
 * the unit at *s, advancing *s past it
 **/
static void
next_unit(text_font_t *f, const char **s, const char *end, unit_t *u) {
  const char *first = *s;
  size_t len;

  if(g_emoji && (len=emoji_at(*s, end, u->name, sizeof u->name))) {
    u->cp = text_next(&first, end);
    u->glyph = NULL;
    *s += len;
    return;
  }
  u->cp = text_next(s, end);
  u->glyph = glyph(f, u->cp);
}


/**
 * the advance of a unit after the glyph prev, with kerning
 **/
static float
advance(const text_font_t *f, const unit_t *u, prev_t prev) {
  const glyph_t *g = u->glyph;

  if(!g) {
    return emoji_size(f) * 1.125f; // with a little room on either side
  }
  if(!g->index) {
    return 0;
  }
  if(!prev.index || prev.from != g->from) {
    return g->advance;
  }
  return g->advance + stbtt_GetGlyphKernAdvance(&g->from->info, prev.index, g->index) * g->from->scale;
}


/**
 * remember the unit for the kerning of the next one; by value, since
 * looking up the next one can empty the cache
 **/
static void
remember(prev_t *prev, const unit_t *u) {
  if(!u->glyph) {
    prev->index = 0;
    prev->from = NULL;
  } else if(u->glyph->index) {
    prev->index = u->glyph->index;
    prev->from = u->glyph->from;
  }
}


int
text_width(text_font_t *font, const char *s, size_t len) {
  const char *end = s + len;
  prev_t prev = {0, NULL};
  float x = 0;
  unit_t u;

  while(s < end) {
    next_unit(font, &s, end, &u);
    x += advance(font, &u, prev);
    remember(&prev, &u);
  }
  return (int)ceilf(x);
}


size_t
text_fit(text_font_t *font, const char *s, size_t len, int width) {
  const char *p = s, *at, *end = s + len;
  prev_t prev = {0, NULL};
  float x = 0, step;
  unit_t u;

  while(p < end) {
    at = p;
    next_unit(font, &p, end, &u);
    step = advance(font, &u, prev);
    if(x + step > width) {
      return at - s;
    }
    x += step;
    remember(&prev, &u);
  }
  return len;
}


size_t
text_break(text_font_t *font, const char *s, int width, const char **next) {
  const char *end = s + strlen(s);
  const char *p = s, *at, *space = NULL;
  prev_t prev = {0, NULL};
  float x = 0, step;
  uint32_t last = 0;
  unit_t u;

  while(p < end) {
    at = p;
    next_unit(font, &p, end, &u);
    if(u.cp == '\n' && u.glyph) {
      *next = p;
      return at - s;
    }
    step = advance(font, &u, prev);
    if(u.cp == ' ' && u.glyph) {
      // where a run of spaces after a word starts
      if(last != ' ' && at > s) {
        space = at;
      }
    } else if(x + step > width && at > s) {
      if(space) {
        // the line ends before the spaces, the next one after them
        for(p=space; *p == ' '; p++) {
        }
        *next = p;
        return space - s;
      }
      *next = at; // a word longer than the line
      return at - s;
    }
    last = u.glyph ? u.cp : 0;
    x += step;
    remember(&prev, &u);
  }
  *next = end;
  return end - s;
}


/*
 * Drawing.
 */

static uint32_t
blend(uint32_t dst, uint32_t color, unsigned a) {
  unsigned na = 255 - a;
  uint32_t rb = ((dst & 0xFF00FF) * na + (color & 0xFF00FF) * a) >> 8;
  uint32_t g = ((dst & 0xFF00) * na + (color & 0xFF00) * a) >> 8;

  return 0xFF000000 | (rb & 0xFF00FF) | (g & 0xFF00);
}


static void
draw_glyph(const text_surface_t *dst, const glyph_t *g, int gx, int gy, uint32_t color) {
  unsigned a;

  for(int row=0; g->bitmap && row < g->h; row++) {
    if(gy + row < 0 || gy + row >= dst->height) {
      continue;
    }
    uint32_t *line = dst->pixels + (size_t)(gy + row) * dst->width;
    for(int col=0; col < g->w; col++) {
      if(gx + col < 0 || gx + col >= dst->width ||
         !(a=g->bitmap[row * g->w + col])) {
        continue;
      }
      line[gx + col] = a == 255 ? color : blend(line[gx + col], color, a);
    }
  }
}


/**
 * an emoji's picture, size x size, with its own alpha
 **/
static void
draw_emoji(const text_surface_t *dst, const uint32_t *pixels, int size, int x, int y) {
  uint32_t px;
  unsigned a;

  for(int row=0; row < size; row++) {
    if(y + row < 0 || y + row >= dst->height) {
      continue;
    }
    uint32_t *line = dst->pixels + (size_t)(y + row) * dst->width;
    for(int col=0; col < size; col++) {
      px = pixels[row * size + col];
      if(x + col < 0 || x + col >= dst->width || !(a=px >> 24)) {
        continue;
      }
      line[x + col] = a == 255 ? px : blend(line[x + col], px, a);
    }
  }
}


int
text_draw(text_font_t *font, const text_surface_t *dst, int x, int y,
          const char *s, size_t len, uint32_t color) {
  // measured in points, drawn in pixels: the pen in points, k pixels each
  const text_surface_t px = text_pixels(dst);
  const char *end = s + len;
  const uint32_t *pixels;
  const float k = font->k;
  const int base = text_py(dst, y);
  prev_t prev = {0, NULL};
  float pen = x;
  int size, side;
  unit_t u;

  while(s < end) {
    next_unit(font, &s, end, &u);
    if(!u.glyph) {
      // sitting a little below the baseline, the way x.com puts them
      size = emoji_size(font);
      side = (int)floorf(size * k + 0.5f);
      if((pixels=g_emoji(u.name, side))) {
        draw_emoji(&px, pixels, side, (int)floorf((pen + size * 0.0833f) * k + 0.5f),
                   base - (int)(side * 0.8f + 0.5f));
      }
      pen += advance(font, &u, prev);
      remember(&prev, &u);
      continue;
    }
    if(!u.glyph->index) {
      continue;
    }
    pen += advance(font, &u, prev) - u.glyph->advance; // kerning before the glyph
    draw_glyph(&px, u.glyph, (int)floorf(pen * k + 0.5f) + u.glyph->x0, base + u.glyph->y0, color);
    pen += u.glyph->advance;
    remember(&prev, &u);
  }
  return (int)ceilf(pen);
}


size_t
text_from_utf16(const uint16_t *s, char *buf, size_t size) {
  size_t n = 0, len;
  uint32_t cp;
  char ch[4];

  for(; *s; s++) {
    cp = *s;
    if(cp >= 0xD800 && cp < 0xDC00 && s[1] >= 0xDC00 && s[1] < 0xE000) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (s[1] - 0xDC00);
      s++;
    } else if(cp >= 0xD800 && cp < 0xE000) {
      cp = 0xFFFD;
    }
    if(cp < 0x80) {
      ch[0] = cp;
      len = 1;
    } else if(cp < 0x800) {
      ch[0] = 0xC0 | (cp >> 6);
      ch[1] = 0x80 | (cp & 0x3F);
      len = 2;
    } else if(cp < 0x10000) {
      ch[0] = 0xE0 | (cp >> 12);
      ch[1] = 0x80 | ((cp >> 6) & 0x3F);
      ch[2] = 0x80 | (cp & 0x3F);
      len = 3;
    } else {
      ch[0] = 0xF0 | (cp >> 18);
      ch[1] = 0x80 | ((cp >> 12) & 0x3F);
      ch[2] = 0x80 | ((cp >> 6) & 0x3F);
      ch[3] = 0x80 | (cp & 0x3F);
      len = 4;
    }
    if(n + len >= size) {
      break;
    }
    memcpy(buf + n, ch, len);
    n += len;
  }
  buf[n] = 0;
  return n;
}


void
text_to_utf16(const char *s, uint16_t *buf, size_t count) {
  const char *end = s + strlen(s);
  size_t n = 0;
  uint32_t cp;

  while((cp=text_next(&s, end))) {
    if(cp >= 0x10000) {
      if(n + 2 >= count) {
        break;
      }
      buf[n++] = 0xD800 + ((cp - 0x10000) >> 10);
      buf[n++] = 0xDC00 + ((cp - 0x10000) & 0x3FF);
    } else {
      if(n + 1 >= count) {
        break;
      }
      buf[n++] = cp;
    }
  }
  buf[n] = 0;
}
