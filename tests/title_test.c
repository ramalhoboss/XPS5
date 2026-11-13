/* Host tests for the title's text and screen code: UTF-8 decoding, line
breaking and drawing with the DejaVu font and Inter falling back to it, the
shapes and icons, the pictures' decoding and cache, and the copy of the
picture into the PS5's tiled buffers, checked pixel by pixel against
PS5_Vulkan's tile formula; the JPEG writer, the settings file, the captures'
lines and the languages. video.c and sound.c (the pointer's tick) are
included whole, with the PS5 calls they make stubbed. Run with
tests/host-test.sh. */

#define _DEFAULT_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

#include "captures.h"
#include "draw.h"
#include "images.h"
#include "jpeg.h"
#include "lang.h"
#include "mem.h"
#include "game.h"
#include "settings.h"
#include "text.h"
#include "../title/video.c"
#include "../title/sound.c"
#include "workers.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

static int g_failed;

#define CHECK(cond) do { \
  if(!(cond)) { \
    printf("%s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_failed++; \
  } \
} while(0)


/*
 * The PS5 calls of video.c.
 */

static void *g_memory;
static int g_flipped = -1;
static int g_tv_lines = 2160;            // the stub TV's rows
static unsigned g_attr_w, g_attr_h;      // the buffers' size VideoOut was given
static unsigned g_register_max = 2160;   // the most rows it takes

size_t sceKernelGetDirectMemorySize(void) { return 1ull << 32; }
int sceKernelAllocateDirectMemory(int64_t start, int64_t end, size_t len, size_t align,
                                  int type, int64_t *phys) {
  (void)start; (void)end; (void)len; (void)align; (void)type;
  *phys = 0x10000000;
  return 0;
}
int sceKernelMapDirectMemory(void **addr, size_t len, int prot, int flags, int64_t phys,
                             size_t align) {
  (void)prot; (void)flags; (void)phys;
  *addr = g_memory = aligned_alloc(align, len);
  return g_memory ? 0 : -1;
}
int sceSystemServiceHideSplashScreen(void) { return 0; }
int sceVideoOutOpen(int32_t user, int32_t bus, int32_t index, const void *param) {
  (void)user; (void)bus; (void)index; (void)param;
  return 1;
}
int sceVideoOutSetFlipRate(int32_t handle, int32_t rate) { (void)handle; (void)rate; return 0; }
void sceVideoOutSetBufferAttribute2(video_attribute_t *attr, uint64_t format, uint32_t tiling,
                                    uint32_t width, uint32_t height, uint64_t option,
                                    uint32_t dcc_control, uint64_t dcc_clear_color) {
  (void)attr; (void)format; (void)tiling; (void)option; (void)dcc_control; (void)dcc_clear_color;
  g_attr_w = width;
  g_attr_h = height;
}
int sceVideoOutRegisterBuffers2(int32_t handle, int32_t set, int32_t start,
                                video_buffer_t *buffers, int32_t count,
                                video_attribute_t *attr, int32_t category, void *option) {
  (void)handle; (void)set; (void)start; (void)attr; (void)category; (void)option;
  return count == 2 && buffers[0].data == g_memory && g_attr_h <= g_register_max ? 0 : -1;
}
int sceVideoOutSubmitFlip(int32_t handle, int32_t index, uint32_t mode, int64_t arg) {
  (void)handle; (void)mode; (void)arg;
  g_flipped = index;
  return 0;
}
int sceVideoOutWaitVblank(int32_t handle) { (void)handle; return 0; }
int sceVideoOutGetResolutionStatus(int32_t handle, void *status) {
  uint32_t *s = status;
  (void)handle;
  s[0] = (uint32_t)g_tv_lines * 16 / 9;
  s[1] = (uint32_t)g_tv_lines;
  return 0;
}


/*
 * The PS5 calls of sound.c: a port that keeps how many blocks it was
 * given, how many were silent and the loudest sample of each side, and
 * a clock the test moves.
 */

static pthread_mutex_t g_audio_lock = PTHREAD_MUTEX_INITIALIZER;
static int      g_audio_opens, g_audio_blocks, g_audio_quiet, g_audio_fail = 1;
static int      g_audio_left, g_audio_right;
static uint64_t g_audio_us = 1000000;

int sceAudioOutInit(void) { return 0; }
int sceAudioOutOpen(int user, int type, int index, unsigned len, unsigned freq, unsigned param) {
  int fail;

  pthread_mutex_lock(&g_audio_lock);
  g_audio_opens++;
  fail = g_audio_fail;
  pthread_mutex_unlock(&g_audio_lock);
  if(fail) {
    return (int)0x80260005;
  }
  return user == 0xff && !type && !index && len == SOUND_GRAIN && freq == SOUND_RATE && param == 1 ? 7 : -1;
}
int sceAudioOutOutput(int handle, const void *ptr) {
  const int16_t *block = ptr;
  int quiet = 1;

  pthread_mutex_lock(&g_audio_lock);
  for(int i=0; i < SOUND_GRAIN; i++) {
    int l = abs(block[2 * i]), r = abs(block[2 * i + 1]);
    g_audio_left = l > g_audio_left ? l : g_audio_left;
    g_audio_right = r > g_audio_right ? r : g_audio_right;
    quiet &= !l && !r;
  }
  g_audio_blocks++;
  g_audio_quiet += quiet;
  pthread_mutex_unlock(&g_audio_lock);
  return handle == 7 ? 0 : -1;
}
uint64_t sceKernelGetProcessTime(void) {
  uint64_t us;

  pthread_mutex_lock(&g_audio_lock);
  us = g_audio_us;
  pthread_mutex_unlock(&g_audio_lock);
  return us;
}


static unsigned char *
read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  unsigned char *buf;
  long n;

  if(!f || fseek(f, 0, SEEK_END) || (n=ftell(f)) < 0 || fseek(f, 0, SEEK_SET) ||
     !(buf=malloc(n)) || fread(buf, 1, n, f) != (size_t)n) {
    fprintf(stderr, "cannot read %s\n", path);
    exit(2);
  }
  fclose(f);
  return buf;
}


/**
 * the keyboard's UTF-16 to UTF-8 and back, with a pair for the emoji
 **/
static void
test_utf16(void) {
  static const char s[] = "a\xc3\xa7\xe2\x80\xa6\xf0\x9f\x98\x80";
  static const uint16_t u[] = {'a', 0xE7, 0x2026, 0xD83D, 0xDE00, 0};
  static const uint16_t lone[] = {'x', 0xDE00, 'y', 0xD83D, 0};
  uint16_t back[8];
  char buf[16];

  CHECK(text_from_utf16(u, buf, sizeof buf) == sizeof s - 1 && !strcmp(buf, s));
  CHECK(text_from_utf16(lone, buf, sizeof buf) == 8 && !strcmp(buf, "x\xef\xbf\xbdy\xef\xbf\xbd"));
  // cut before the emoji, which would not fit whole
  CHECK(text_from_utf16(u, buf, 8) == 6 && !strcmp(buf, "a\xc3\xa7\xe2\x80\xa6"));
  CHECK(text_from_utf16(u, buf, 1) == 0 && !buf[0]);

  text_to_utf16(s, back, 8);
  CHECK(!memcmp(back, u, sizeof u));
  text_to_utf16(s, back, 5);
  CHECK(back[0] == 'a' && back[2] == 0x2026 && back[3] == 0);
  text_to_utf16("", back, 1);
  CHECK(back[0] == 0);
}


static void
test_utf8(void) {
  static const char s[] = "a\xc3\xa7\xe2\x80\xa6\xf0\x9f\x98\x80";
  const char *p = s, *end = s + sizeof s - 1;

  CHECK(text_next(&p, end) == 'a');
  CHECK(text_next(&p, end) == 0xE7);
  CHECK(text_next(&p, end) == 0x2026);
  CHECK(text_next(&p, end) == 0x1F600);
  CHECK(p == end && text_next(&p, end) == 0);

  // a bad lead byte, a cut sequence and a missing continuation
  p = "\xff" "b";
  CHECK(text_next(&p, p + 2) == 0xFFFD && *p == 'b');
  p = "\xe2\x80";
  end = p + 2;
  CHECK(text_next(&p, end) == 0xFFFD && p == end);
  p = "\xe2" "ab";
  CHECK(text_next(&p, p + 3) == 0xFFFD && *p == 'a');
}


static void
test_measure(text_font_t *f) {
  static const char word[] = "a\xc3\xa7\xc3\xa3o";
  int w = text_width(f, word, sizeof word - 1);

  CHECK(text_width(f, "", 0) == 0);
  CHECK(w > text_width(f, "a", 1) && w < 200);
  CHECK(text_line_height(f) >= 34 && text_ascent(f) > 0);

  // whole characters only
  CHECK(text_fit(f, word, sizeof word - 1, 1000) == sizeof word - 1);
  CHECK(text_fit(f, word, sizeof word - 1, 0) == 0);
  for(int width=0; width < w + 5; width++) {
    size_t n = text_fit(f, word, sizeof word - 1, width);
    CHECK(n == 0 || n == 1 || n == 3 || n == 5 || n == 6);
    CHECK(text_width(f, word, n) <= width + 1);
  }

  // a character the font lacks (DejaVu has no CJK) takes no room
  CHECK(text_width(f, "\xe4\xb8\xad", 3) == 0);
  CHECK(text_width(f, "a\xe4\xb8\xad" "b", 5) == text_width(f, "ab", 2));
}


/**
 * break s into lines of width and check each one; returns how many
 **/
static int
check_lines(text_font_t *f, const char *s, int width) {
  const char *next, *at = s, *p;
  int lines = 0;
  size_t len, ink;

  while(*at) {
    len = text_break(f, at, width, &next);
    CHECK(next > at);
    CHECK(len <= strlen(at));
    // without the spaces it ends with, a line is wider than asked only
    // when it is a single character
    for(ink=len; ink && at[ink - 1] == ' '; ink--) {
    }
    if(text_width(f, at, ink) > width + 1) {
      for(p=at; *p == ' '; p++) {
      }
      text_next(&p, at + ink);
      CHECK(p == at + ink);
    }
    // a line broken for width ends before its spaces
    CHECK(at[len] == 0 || at[len] == '\n' || len == ink || !ink);
    at = next;
    if(++lines > 10000) {
      CHECK(!"text_break makes no progress");
      break;
    }
  }
  return lines;
}


static void
test_break(text_font_t *f) {
  const char *next, *s;
  char buf[2048];
  size_t len;
  int w = text_width(f, "hello", 5);

  s = "hello world";
  CHECK(text_break(f, s, 1000, &next) == 11 && *next == 0);
  CHECK(text_break(f, s, w + 2, &next) == 5 && !strcmp(next, "world"));
  s = "hello    world";
  CHECK(text_break(f, s, w + 2, &next) == 5 && !strcmp(next, "world"));
  s = "a\nb";
  CHECK(text_break(f, s, 1000, &next) == 1 && !strcmp(next, "b"));
  s = "\n\nb";
  CHECK(text_break(f, s, 1000, &next) == 0 && !strcmp(next, "\nb"));
  s = "";
  CHECK(text_break(f, s, 1000, &next) == 0 && next == s);

  // a word longer than the line breaks at a whole character
  s = "\xc3\xa7\xc3\xa7\xc3\xa7\xc3\xa7\xc3\xa7\xc3\xa7\xc3\xa7\xc3\xa7";
  len = text_break(f, s, w, &next);
  CHECK(len > 0 && len < strlen(s) && len % 2 == 0 && next == s + len);
  CHECK(check_lines(f, s, w) > 1);

  // narrower than one character: one per line
  CHECK(check_lines(f, "abc", 1) == 3);
  CHECK(check_lines(f, "abc def", 0) >= 6);

  // made-up posts of words, spaces, newlines, accents and emoji
  srand(7);
  for(int run=0; run < 300; run++) {
    static const char *const parts[] = {
      "a", "bb", "palavra", " ", "  ", "\n", "\xc3\xa7\xc3\xa3o", "\xf0\x9f\x98\x80",
      "https://t.co/abcdefghij", "\xe2\x80\xa6", "W",
    };
    size_t n = 0;
    while(n < sizeof buf - 64) {
      const char *p = parts[rand() % (int)(sizeof parts / sizeof *parts)];
      memcpy(buf + n, p, strlen(p));
      n += strlen(p);
      if(rand() % 50 == 0) {
        break;
      }
    }
    buf[n] = 0;
    check_lines(f, buf, 40 + rand() % 900);
  }
}


static void
test_draw(text_font_t *f) {
  static uint32_t pixels[100 * 50];
  text_surface_t s = {pixels, 100, 50, 1, 0};
  int changed = 0, x;

  memset(pixels, 0, sizeof pixels);
  x = text_draw(f, &s, 10, 35, "Ola", 3, 0xFFFFFFFF);
  CHECK(x > 10 && x <= 10 + text_width(f, "Ola", 3) + 1);
  for(int i=0; i<100*50; i++) {
    changed += pixels[i] != 0;
  }
  CHECK(changed > 20);

  // clipped at every edge; the sanitizer catches a write outside
  text_draw(f, &s, -15, 20, "WWWW", 4, 0xFF00FF00);
  text_draw(f, &s, 90, 20, "WWWW", 4, 0xFF00FF00);
  text_draw(f, &s, 10, 5, "Wg", 2, 0xFF00FF00);
  text_draw(f, &s, 10, 60, "Wg", 2, 0xFF00FF00);
  text_draw(f, &s, -10000, -10000, "W", 1, 0xFF00FF00);
  text_draw(f, &s, 10000, 10000, "W", 1, 0xFF00FF00);

  // blending keeps the alpha byte opaque
  for(int i=0; i<100*50; i++) {
    CHECK(!pixels[i] || (pixels[i] >> 24) == 0xFF);
  }
}


/**
 * more glyphs than the cache holds, then the first ones again
 **/
static void
test_cache(text_font_t *f) {
  char buf[8];
  int before = text_width(f, "Wg", 2), w;

  for(unsigned cp=0x100; cp < 0x800; cp++) {
    buf[0] = 0xC0 | (cp >> 6);
    buf[1] = 0x80 | (cp & 0x3F);
    w = text_width(f, buf, 2);
    CHECK(w >= 0 && w < 200);
  }
  CHECK(text_width(f, "Wg", 2) == before);
}


/**
 * what Inter lacks comes from DejaVu, and kerning stays inside one font
 **/
static void
test_fallback(const unsigned char *inter_ttf, const unsigned char *dejavu_ttf) {
  text_font_t *inter = text_font_open(inter_ttf, 34), *dejavu = text_font_open(dejavu_ttf, 34);
  const char *alef = "\xd7\x90"; // Hebrew, which Inter does not have
  int a, alone, both;

  CHECK(inter && dejavu);
  if(!inter || !dejavu) {
    return;
  }
  CHECK(text_width(inter, alef, 2) == 0);
  text_font_set_fallback(inter, dejavu);
  CHECK((alone=text_width(inter, alef, 2)) > 0 && alone == text_width(dejavu, alef, 2));
  a = text_width(inter, "A", 1);
  both = text_width(inter, "A\xd7\x90", 3);
  CHECK(both >= a + alone - 1 && both <= a + alone + 1);
  // and Inter's own letters still come from Inter
  CHECK(text_width(inter, "Wg", 2) != text_width(dejavu, "Wg", 2));
  text_font_close(inter);
  text_font_close(dejavu);
}


static char g_emoji_names[8][80];
static int  g_emoji_count;


static const uint32_t *
record_emoji(const char *name, int size) {
  static uint32_t square[64 * 64];

  CHECK(size > 0 && size <= 64);
  if(g_emoji_count < 8) {
    snprintf(g_emoji_names[g_emoji_count], sizeof g_emoji_names[0], "%s", name);
  }
  g_emoji_count++;
  for(int i=0; i<64*64; i++) {
    square[i] = 0xFF00FFFF;
  }
  return square;
}


/**
 * the emoji text_draw asks for in s, joined by spaces
 **/
static const char *
emoji_in(text_font_t *f, const char *s) {
  static uint32_t pixels[400 * 60];
  static char names[400];
  text_surface_t dst = {pixels, 400, 60, 1, 0};
  size_t used = 0;

  g_emoji_count = 0;
  names[0] = 0;
  text_draw(f, &dst, 0, 45, s, strlen(s), 0xFFFFFFFF);
  for(int i=0; i < g_emoji_count && i < 8; i++) {
    used += snprintf(names + used, sizeof names - used, i ? " %s" : "%s", g_emoji_names[i]);
  }
  return names;
}


/**
 * emoji sequences named the way Twemoji names its pictures, measured as
 * one square each
 **/
static void
test_emoji(text_font_t *f) {
  static uint32_t pixels[100 * 60];
  text_surface_t dst = {pixels, 100, 60, 1, 0};
  const char *next;
  int w;

  text_set_emoji(record_emoji);
  CHECK(!strcmp(emoji_in(f, "ri \xf0\x9f\x98\x82 e"), "1f602"));
  CHECK(!strcmp(emoji_in(f, "\xe2\x9d\xa4\xef\xb8\x8f\xe2\x9d\xa4"), "2764 2764"));
  CHECK(!strcmp(emoji_in(f, "\xf0\x9f\x87\xb5\xf0\x9f\x87\xb9\xf0\x9f\x87\xa7"), "1f1f5-1f1f9 1f1e7"));
  CHECK(!strcmp(emoji_in(f, "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd"), "1f44d-1f3fd"));
  CHECK(!strcmp(emoji_in(f, "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7"),
                "1f468-200d-1f469-200d-1f467"));
  CHECK(!strcmp(emoji_in(f, "\xf0\x9f\x8f\xb3\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x8c\x88"),
                "1f3f3-fe0f-200d-1f308"));
  CHECK(!strcmp(emoji_in(f, "#\xef\xb8\x8f\xe2\x83\xa3 1\xe2\x83\xa3"), "23-20e3 31-20e3"));
  CHECK(!strcmp(emoji_in(f, "\xc2\xa9\xef\xb8\x8f \xc2\xa9"), "a9"));
  // text, not emoji: hashtags, digits, a star, and what asks for text
  CHECK(!strcmp(emoji_in(f, "#ps5 1 2 * \xe2\x98\x85 \xe2\x98\xba\xef\xb8\x8e \xe2\x80\x8d"), ""));
  // a ZWJ at the end, and sequences cut short
  CHECK(!strcmp(emoji_in(f, "\xf0\x9f\x91\xa8\xe2\x80\x8d"), "1f468"));
  CHECK(!strcmp(emoji_in(f, "\xf0\x9f\x98"), ""));

  // a square of about the font's height, and a line breaks around it
  w = text_width(f, "\xf0\x9f\x98\x82", 4);
  CHECK(w >= text_line_height(f) * 0.9 && w <= text_line_height(f) * 1.4);
  CHECK(text_width(f, "a\xf0\x9f\x98\x82", 5) == (int)ceilf(text_width(f, "a", 1) + 34 * 1.125f) ||
        text_width(f, "a\xf0\x9f\x98\x82", 5) == text_width(f, "a", 1) + w);
  CHECK(text_break(f, "\xf0\x9f\x98\x82\xf0\x9f\x98\x82 \xf0\x9f\x98\x82", w * 2 + 5, &next) == 8 &&
        !strcmp(next, "\xf0\x9f\x98\x82"));
  CHECK(text_fit(f, "\xf0\x9f\x98\x82\xf0\x9f\x98\x82", 8, w + 1) == 4);
  // drawn as the picture, clipped at the edges
  for(int i=0; i<100*60; i++) {
    pixels[i] = 0xFF000000;
  }
  text_draw(f, &dst, 10, 45, "\xf0\x9f\x98\x82", 4, 0xFFFFFFFF);
  CHECK(pixels[30 * 100 + 25] == 0xFF00FFFF);
  text_draw(f, &dst, -20, 10, "\xf0\x9f\x98\x82", 4, 0xFFFFFFFF);
  text_draw(f, &dst, 90, 70, "\xf0\x9f\x98\x82", 4, 0xFFFFFFFF);
  text_set_emoji(NULL);
  CHECK(!strcmp(emoji_in(f, "\xf0\x9f\x98\x82"), ""));
}


static uint32_t
at(const text_surface_t *s, int x, int y) {
  return s->pixels[y * s->width + x];
}


static void
fill(uint32_t *pixels, int count, uint32_t color) {
  for(int i=0; i<count; i++) {
    pixels[i] = color;
  }
}


static void
test_shapes(void) {
  static uint32_t pixels[64 * 64];
  text_surface_t s = {pixels, 64, 64, 1, 0};
  int changed = 0;

  // a border two pixels wide, nothing inside it
  fill(pixels, 64 * 64, 0xFF000000);
  draw_box(&s, 8, 8, 40, 30, 0, 2, 0xFFFFFFFF);
  CHECK(at(&s, 8, 8) == 0xFFFFFFFF && at(&s, 9, 20) == 0xFFFFFFFF);
  CHECK(at(&s, 10, 20) == 0xFF000000 && at(&s, 20, 20) == 0xFF000000);
  CHECK(at(&s, 7, 20) == 0xFF000000 && at(&s, 48, 20) == 0xFF000000);

  // a filled box with round corners leaves its corners out
  fill(pixels, 64 * 64, 0xFF000000);
  draw_box(&s, 8, 8, 40, 30, 10, 0, 0xFFFFFFFF);
  CHECK(at(&s, 8, 8) == 0xFF000000 && at(&s, 28, 23) == 0xFFFFFFFF);
  CHECK(at(&s, 8, 23) == 0xFFFFFFFF && at(&s, 47, 37) == 0xFF000000);

  // corners painted round a circle, and its middle kept
  fill(pixels, 64 * 64, 0xFFFFFFFF);
  draw_corners(&s, 0, 0, 64, 64, 32, 0xFF0000FF);
  CHECK(at(&s, 0, 0) == 0xFF0000FF && at(&s, 63, 63) == 0xFF0000FF);
  CHECK(at(&s, 32, 32) == 0xFFFFFFFF && at(&s, 32, 1) == 0xFFFFFFFF);

  // half see-through
  pixels[0] = 0xFF000000;
  draw_rect(&s, 0, 0, 1, 1, 0x80FFFFFF);
  CHECK((at(&s, 0, 0) & 0xFF) >= 0x70 && (at(&s, 0, 0) & 0xFF) <= 0x90);

  // the icons draw something, and clip at every edge
  fill(pixels, 64 * 64, 0xFF000000);
  for(int icon=0; icon < ICON_COUNT; icon++) {
    draw_icon(&s, icon, 0, 0, 32, 0xFFFFFFFF);
    draw_icon(&s, icon, -20, -20, 40, 0xFFFFFFFF);
    draw_icon(&s, icon, 50, 50, 40, 0xFFFFFFFF);
  }
  draw_icon(&s, ICON_COUNT, 0, 0, 32, 0xFFFFFFFF);
  draw_icon(&s, ICON_LIKE, 0, 0, 500, 0xFFFFFFFF);
  for(int i=0; i<64*64; i++) {
    changed += pixels[i] != 0xFF000000;
  }
  CHECK(changed > 100);
  fill(pixels, 64 * 64, 0xFF000000);
  draw_icon(&s, ICON_SQUARE, 0, 0, 24, 0xFFFFFFFF);
  CHECK(at(&s, 5, 12) != 0xFF000000 && at(&s, 12, 12) == 0xFF000000);

  draw_circle(&s, 0, 0, 40, 0xFFFF0000);
  draw_arc(&s, 32, 32, 40, 5, 1, 8, 0xFF00FF00);
  draw_arc(&s, 32, 32, 10, 4, 0, 6.2832f, 0xFF00FF00);
  draw_picture(&s, -2, -2, 4, 4, pixels + 100);
  draw_picture(&s, 62, 62, 4, 4, pixels + 100);

  // a picture cut to a circle over what is under it: the corners keep that,
  // the middle is the picture's; and clipped at the edges
  {
    static uint32_t picture[20 * 20];
    fill(picture, 20 * 20, 0xFF00FF00);
    fill(pixels, 64 * 64, 0xFF0000FF);
    draw_picture_round(&s, 10, 10, 20, 20, 10, picture);
    CHECK(at(&s, 10, 10) == 0xFF0000FF && at(&s, 29, 29) == 0xFF0000FF);
    CHECK(at(&s, 20, 20) == 0xFF00FF00 && at(&s, 20, 12) == 0xFF00FF00);
    draw_picture_round(&s, -5, -5, 20, 20, 6, picture);
    draw_picture_round(&s, 55, 55, 20, 20, 6, picture);
  }

  // below 0 inside a box, above 0 outside, 0 on its edge
  CHECK(draw_distance(20, 20, 10, 10, 20, 20, 4) < -9);
  CHECK(draw_distance(40, 20, 10, 10, 20, 20, 4) > 9);
  CHECK(fabsf(draw_distance(10, 20, 10, 10, 20, 20, 4)) < 0.01f);
}


typedef struct bytes {
  unsigned char buf[16384];
  int           len;
} bytes_t;


static void
add_bytes(void *ctx, void *data, int size) {
  bytes_t *out = ctx;

  if(out->len + size <= (int)sizeof out->buf) {
    memcpy(out->buf + out->len, data, size);
    out->len += size;
  }
}


static void
test_images(void) {
  static const uint32_t red = 0xFF0000FF, blue = 0xFFFF0000;
  uint32_t source[8] = {red, red, blue, blue, red, red, blue, blue}, *fit;
  bytes_t png = {{0}, 0};
  const uint32_t *got;
  char url[IMAGES_URL_MAX];
  int w, h;

  // a 4 x 2 picture, red on the left, cut to its middle 2 x 2
  CHECK(stbi_write_png_to_func(add_bytes, &png, 4, 2, 4, source, 16) && png.len > 0);
  if((fit=image_fit(png.buf, png.len, 2, 2))) {
    CHECK(fit[0] == red && fit[1] == blue && fit[2] == red && fit[3] == blue);
    mem_free(fit);
  } else {
    CHECK(fit != NULL);
  }
  // shrunk, its colors mix
  if((fit=image_fit(png.buf, png.len, 1, 1))) {
    CHECK((fit[0] & 0xFF) > 0x60 && (fit[0] >> 16 & 0xFF) > 0x60);
    mem_free(fit);
  }
  CHECK(!image_fit(png.buf, 10, 2, 2) && !image_fit("not a picture", 13, 2, 2));
  CHECK(!image_fit(png.buf, png.len, 0, 2) && !image_fit(NULL, 0, 2, 2));

  // asked, fetched, then there; 32 bytes of budget is two 2 x 2 pictures
  images_init(32);
  CHECK(!images_get("https://x/a", 2, 2) && !images_get("", 2, 2));
  CHECK(images_next(url, sizeof url, &w, &h) && !strcmp(url, "https://x/a") && w == 2 && h == 2);
  CHECK(!images_next(url, sizeof url, &w, &h)); // a loader has it
  images_done(url, w, h, png.buf, png.len);
  CHECK(images_arrived() && !images_arrived());
  CHECK((got=images_get("https://x/a", 2, 2)) && got[0] == red && got[1] == blue);

  // over the budget, the one least recently asked for goes
  images_frame();
  images_get("https://x/b", 2, 2);
  CHECK(images_next(url, sizeof url, &w, &h) && !strcmp(url, "https://x/b"));
  images_done_pixels(url, w, h, mem_calloc(4, sizeof(uint32_t)));
  images_frame();
  images_get("https://x/a", 2, 2);
  images_get("https://x/c", 2, 2);
  CHECK(images_next(url, sizeof url, &w, &h) && !strcmp(url, "https://x/c"));
  images_done_pixels(url, w, h, mem_calloc(4, sizeof(uint32_t)));
  images_frame();
  CHECK(images_get("https://x/a", 2, 2) && images_get("https://x/c", 2, 2));
  CHECK(!images_get("https://x/b", 2, 2));

  // nobody waits for what scrolled away long ago, like b just now
  for(int i=0; i<200; i++) {
    images_frame();
  }
  CHECK(!images_next(url, sizeof url, &w, &h));

  // another size is another picture; what failed is asked again after a retry
  CHECK(!images_get("https://x/a", 3, 2));
  CHECK(images_next(url, sizeof url, &w, &h) && !strcmp(url, "https://x/a") && w == 3);
  images_done_pixels(url, w, h, NULL);
  CHECK(!images_get("https://x/a", 3, 2) && !images_next(url, sizeof url, &w, &h));
  images_retry();
  CHECK(!images_get("https://x/a", 3, 2));
  CHECK(images_next(url, sizeof url, &w, &h) && w == 3);
  images_done_pixels(url, w, h, NULL);
  images_retry();
}


/**
 * a JPEG of a picture decodes to about the same, at its size; a picture too
 * big for X comes out smaller, its sides in proportion
 **/
static void
test_jpeg(void) {
  enum { W = 48, H = 32 };
  static uint32_t pixels[W * H];
  static bytes_t png;
  uint32_t *back, *small;
  uint8_t *jpeg = NULL;
  size_t len = 0;
  int w = 0, h = 0, worst = 0, d;

  for(int y=0; y<H; y++) {
    for(int x=0; x<W; x++) {
      // red across, green down, blue in a block
      pixels[y * W + x] = 0xFF000000u | (uint32_t)(x * 255 / (W - 1)) | (uint32_t)(y * 255 / (H - 1)) << 8 |
                          (uint32_t)(x >= 16 && x < 32 ? 200 : 40) << 16;
    }
  }
  CHECK(!jpeg_encode(pixels, W, H, 90, &jpeg, &len) && jpeg && len > 300);
  CHECK(jpeg && jpeg[0] == 0xFF && jpeg[1] == 0xD8 && jpeg[len - 2] == 0xFF && jpeg[len - 1] == 0xD9);
  CHECK(!image_size(jpeg, len, &w, &h) && w == W && h == H);
  if((back=image_fit(jpeg, len, W, H))) {
    for(int i=0; i < W * H; i++) {
      for(int c=0; c<24; c+=8) {
        d = abs((int)(back[i] >> c & 0xFF) - (int)(pixels[i] >> c & 0xFF));
        worst = d > worst ? d : worst;
      }
    }
    CHECK(worst < 48); // 4:2:0 blurs the block's edges a little
    mem_free(back);
  } else {
    CHECK(back != NULL);
  }
  mem_free(jpeg);
  CHECK(jpeg_encode(pixels, 0, H, 90, &jpeg, &len) && !jpeg && !len);
  CHECK(jpeg_encode(pixels, W, 70000, 90, &jpeg, &len) && !jpeg);

  // a picture made smaller keeps its proportions; a small one stays as it is
  CHECK(stbi_write_png_to_func(add_bytes, &png, W, H, 4, pixels, W * 4) && png.len > 0);
  if((small=image_scaled(png.buf, png.len, 12, &w, &h))) {
    CHECK(w == 12 && h == 8);
    mem_free(small);
  } else {
    CHECK(small != NULL);
  }
  if((small=image_scaled(png.buf, png.len, 4000, &w, &h))) {
    CHECK(w == W && h == H);
    mem_free(small);
  }
  CHECK(!image_scaled(png.buf, png.len, 0, &w, &h) && image_size("nothing", 7, &w, &h));
}


/**
 * the game that runs: its name from a PS5 game's param.json and a PS4 one's
 * param.sfo, and the hashtag made of it
 **/
static void
test_game(void) {
  char json[] = "{\"titleId\":\"PPSA01325\",\"localizedParameters\":{\"defaultLanguage\":\"en-US\","
                "\"en-US\":{\"titleName\":\"ASTRO's PLAYROOM\"},\"pt-PT\":{\"titleName\":\"ASTRO\\u2019s\\nPLAYROOM \"}}}";
  char other[] = "{\"localizedParameters\":{\"defaultLanguage\":\"ja-JP\",\"ja-JP\":{\"titleName\":\"Gran Turismo 7\"}}}";
  char broken[] = "{\"localizedParameters\":{";
  unsigned char sfo[96];
  char name[GAME_NAME], tag[GAME_NAME + 2], small[8];

  {
    // a game, a media app (YouTube, XPS5) and a param.json without a category
    char g[] = "{\"applicationCategoryType\": 0, \"titleId\": \"PPSA01325\"}";
    char m[] = "{\"applicationCategoryType\": 65536, \"titleId\": \"PPSA01650\"}";
    char none[] = "{\"titleId\": \"PPSA01325\"}", bad[] = "not json";
    CHECK(game_category_json(g, sizeof g - 1) == 0 && game_category_json(m, sizeof m - 1) == 65536 &&
          game_category_json(none, sizeof none - 1) == -1 && game_category_json(bad, sizeof bad - 1) == -1);
  }
  CHECK(game_id_ok("PPSA01325") && game_id_ok("CUSA00001") && !game_id_ok("NPXS40087") &&
        !game_id_ok("TWPS00001") && !game_id_ok("ppsa01325") && !game_id_ok("PPSA0132") &&
        !game_id_ok("PPSA013255"));
  CHECK(!game_name_json(json, sizeof json - 1, "pt-PT", name, sizeof name) &&
        !strcmp(name, "ASTRO\xe2\x80\x99s PLAYROOM"));
  CHECK(!game_name_json(other, sizeof other - 1, "fr-FR", name, sizeof name) && !strcmp(name, "Gran Turismo 7"));
  CHECK(game_name_json(broken, sizeof broken - 1, "en-US", name, sizeof name) == -1);
  // cut to fit, never in the middle of a character
  {
    char cut[] = "{\"localizedParameters\":{\"en-US\":{\"titleName\":\"Pok\xc3\xa9mon\"}}}";
    CHECK(!game_name_json(cut, sizeof cut - 1, "en-US", small, 5) && !strcmp(small, "Pok"));
  }

  // a param.sfo: the header, two entries, their keys, then their data
  memset(sfo, 0, sizeof sfo);
  memcpy(sfo, "\0PSF", 4);
  sfo[4] = 1; sfo[5] = 1;
  sfo[8] = 52;      // keys
  sfo[12] = 68;     // data
  sfo[16] = 2;      // entries
  sfo[20] = 0;  sfo[22] = 0x04; sfo[23] = 0x02; sfo[24] = 9;  sfo[28] = 16; sfo[32] = 0;  // "CATEGORY"
  sfo[36] = 9;  sfo[38] = 0x04; sfo[39] = 0x02; sfo[40] = 10; sfo[44] = 16; sfo[48] = 16; // "TITLE"
  memcpy(sfo + 52, "CATEGORY\0TITLE", 15);
  memcpy(sfo + 68, "gd", 3);
  memcpy(sfo + 84, "Bloodborne", 10);
  CHECK(!game_name_sfo(sfo, sizeof sfo, name, sizeof name) && !strcmp(name, "Bloodborne"));
  CHECK(game_name_sfo(sfo, 40, name, sizeof name) == -1);
  sfo[48] = 90;     // its data past the end
  CHECK(game_name_sfo(sfo, sizeof sfo, name, sizeof name) == -1);

  CHECK(!game_hashtag("ASTRO BOT", tag, sizeof tag) && !strcmp(tag, "#ASTROBOT"));
  game_keys_t keys;
  CHECK(!game_hashtag("Marvel's Spider-Man 2\xe2\x84\xa2", tag, sizeof tag) && !strcmp(tag, "#MarvelsSpiderMan2"));
  CHECK(!game_hashtag("Pok\xc3\xa9mon \xc3\x97 2", tag, sizeof tag) && !strcmp(tag, "#Pok\xc3\xa9mon2"));
  CHECK(game_hashtag("2077", tag, sizeof tag) == -1 && game_hashtag("\xe2\x84\xa2", tag, sizeof tag) == -1);
  CHECK(!game_hashtag("Pok\xc3\xa9mon", tag, 5) && !strcmp(tag, "#Pok"));
  CHECK(!strcmp(game_locale(LANG_PT), "pt-PT") && !strcmp(game_locale(LANG_SYSTEM), "en-US"));

  // what X's search looks for, for the tab of the game that runs
  {
    char q[200];
    game_keys_t astro;
    CHECK(!game_query("ASTRO BOT", q, sizeof q) &&
          !strcmp(q, "(\"astro bot\" OR #astrobot) -spoiler -spoilers -filter:replies"));
    CHECK(!game_query("Ghost of Tsushima DIRECTOR'S CUT", q, sizeof q) &&
          !strcmp(q, "(\"ghost of tsushima\" OR #ghostoftsushima) -spoiler -spoilers -filter:replies"));
    CHECK(!game_query("Bloodborne\xe2\x84\xa2", q, sizeof q) &&
          !strcmp(q, "(#bloodborne OR (bloodborne (PS5 OR PlayStation OR game OR gameplay))) "
                     "-spoiler -spoilers -filter:replies"));
    CHECK(!game_query("Pok\xc3\xa9mon Legends: Z-A", q, sizeof q) && strstr(q, "\"pokemon legends z a\""));
    CHECK(game_query("", q, sizeof q) == -1 && !q[0]);
    CHECK(game_query("ASTRO BOT", q, 20) == -1 && !q[0]);
    CHECK(game_query("ASTRO BOT", q, 0) == -1);

    // and which of its posts it covers
    CHECK(game_keys("ASTRO BOT", &astro) > 0);
    CHECK(game_spoils("The ending of Astro Bot made me cry", &astro) &&
          game_spoils("O chefe final do #AstroBot \xc3\xa9 brutal", &astro) &&
          game_spoils("Nem acredito que ele morre", &astro) && game_spoils("#Spoilers ahead", &astro) &&
          game_spoils("Der Endboss ist hart", &astro) && game_spoils("El FINAL es incre\xc3\xad" "ble", &astro));
    CHECK(!game_spoils("Astro Bot is so much fun", &astro) && !game_spoils("Which level is the hardest?", &astro) &&
          !game_spoils("I died 50 times on this level", &astro) && !game_spoils("finalmente!", &astro) &&
          !game_spoils("", &astro) && !game_spoils(NULL, &astro));
    CHECK(game_keys("FINAL FANTASY VII REBIRTH", &keys) > 0);
    CHECK(!game_spoils("FINAL FANTASY VII REBIRTH photo mode is great", &keys) &&
          game_spoils("FF7 Rebirth ending was wild", &keys) && game_spoils("the final boss of FF7", &keys));
    CHECK(game_keys("Secret Ending", &keys) > 0 && !game_spoils("Secret Ending is out today", &keys) &&
          game_spoils("Secret Ending: the final boss is hard", &keys));
    CHECK(game_spoils("ending", NULL));
  }

  // the posts that name the game that runs, for its spoilers
  CHECK(game_keys("ELDEN RING\xe2\x84\xa2", &keys) >= 2 && !strcmp(keys.key[0], "elden ring") &&
        !strcmp(keys.key[1], "eldenring"));
  CHECK(game_named("Just beat Elden Ring, wow", &keys) && game_named("#EldenRing ending!!", &keys));
  CHECK(game_named("ELDEN RING's last boss", &keys) && game_named("eldenring2 when", &keys));
  CHECK(!game_named("goldenring", &keys) && !game_named("golden ring", &keys) && !game_named("elden rings2x", &keys));
  CHECK(!game_named("elden", &keys) && !game_named("", &keys));
  CHECK(game_keys("The Last of Us\xe2\x84\xa2 Part II", &keys) > 0);
  CHECK(game_named("The Last of Us 2 had me crying", &keys) && game_named("tlou2 ending", &keys));
  CHECK(game_named("TLOU spoilers", &keys) && game_named("#tlou is art", &keys) &&
        game_named("#TheLastOfUs2", &keys));
  CHECK(!game_named("tlou spoilers", &keys) && !game_named("the last one of us", &keys));
  CHECK(game_keys("Marvel's Spider-Man 2", &keys) > 0);
  CHECK(game_named("Spider-Man 2 is so good", &keys) && game_named("MSM2", &keys) &&
        game_named("Marvel\xe2\x80\x99s Spider-Man 2", &keys) && game_named("#MarvelsSpiderMan2", &keys));
  CHECK(!game_named("spider", &keys));
  CHECK(game_keys("FINAL FANTASY VII REBIRTH", &keys) > 0);
  CHECK(game_named("FF7 Rebirth's ending", &keys) && game_named("ff7r", &keys) && !game_named("FF16", &keys));
  CHECK(game_keys("Ghost of Tsushima DIRECTOR'S CUT", &keys) > 0);
  CHECK(game_named("ghost of tsushima", &keys) && game_named("GoT ending", &keys) == 0 &&
        game_named("GOT ending", &keys));
  CHECK(game_keys("Uncharted: Legacy of Thieves Collection", &keys) > 0);
  CHECK(game_named("Uncharted 4 ending", &keys) && !game_named("uncharted waters", &keys) &&
        game_named("uncharted legacy of thieves", &keys));
  CHECK(game_keys("Control", &keys) == 1 && keys.how[0] == GAME_KEY_CAPITAL);
  CHECK(game_named("Control is a great game", &keys) && !game_named("we lost control of it", &keys));
  CHECK(game_keys("Pok\xc3\xa9mon Legends: Z-A", &keys) > 0 && game_named("POKEMON LEGENDS spoilers", &keys) &&
        game_named("pok\xc3\xa9mon legends", &keys));
  CHECK(game_keys("Gran Turismo\xc2\xae 7", &keys) > 0 && game_named("GT7 update", &keys) &&
        game_named("gran turismo", &keys) && !game_named("gt", &keys));
  CHECK(game_keys("", &keys) == 0 && !game_named("anything", &keys) && game_keys("Ico", &keys) == 0);
}


/**
 * the newest settings file wins: back from a game the title can lose /data
 * and save beside its eboot, and that copy is then the one to read
 **/
static void
write_text(const char *path, const char *text, long long when) {
  struct timespec times[2] = {{(time_t)when, 0}, {(time_t)when, 0}};
  FILE *f = fopen(path, "w");

  CHECK(f != NULL);
  if(f) {
    fputs(text, f);
    fclose(f);
  }
  CHECK(!utimensat(AT_FDCWD, path, times, 0));
}

static void
test_settings_files(void) {
  char dir[] = "/tmp/xps5-settings-XXXXXX", a[64], b[64];
  const char *paths[2] = {a, b};
  settings_t s;

  if(!mkdtemp(dir)) {
    CHECK(!"mkdtemp");
    return;
  }
  snprintf(a, sizeof a, "%s/data.txt", dir);
  snprintf(b, sizeof b, "%s/app0.txt", dir);
  CHECK(settings_newest(paths, 2) == -1 && settings_load(&s, paths, 2) == -1 && s.lang == LANG_SYSTEM);
  // only the second
  write_text(b, "lang=nl\n", 1000);
  CHECK(settings_newest(paths, 2) == 1 && settings_load(&s, paths, 2) == 1 && s.lang == LANG_NL);
  // the first newer
  write_text(a, "lang=de\n", 2000);
  CHECK(settings_newest(paths, 2) == 0 && settings_load(&s, paths, 2) == 0 && s.lang == LANG_DE);
  // the second newer: saved beside the eboot after /data was lost
  write_text(b, "lang=nl\n", 3000);
  CHECK(settings_newest(paths, 2) == 1 && settings_load(&s, paths, 2) == 1 && s.lang == LANG_NL);
  // a tie: the first
  write_text(b, "lang=nl\n", 2000);
  CHECK(settings_newest(paths, 2) == 0 && settings_load(&s, paths, 2) == 0 && s.lang == LANG_DE);
  // an empty newer file does not count
  write_text(b, "", 4000);
  CHECK(settings_newest(paths, 2) == 0 && settings_load(&s, paths, 2) == 0 && s.lang == LANG_DE);
  // the newer one cannot be read: the other one
  write_text(b, "lang=nl\n", 5000);
  CHECK(!chmod(b, 0));
  if(access(b, R_OK)) { // root reads it anyway
    CHECK(settings_load(&s, paths, 2) == 0 && s.lang == LANG_DE);
  }
  chmod(b, 0644);
  // the save goes to the first that can be written, which is then the newest
  s.lang = LANG_FR;
  CHECK(settings_save(&s, paths, 2) == 0);
  write_text(b, "lang=nl\n", 1000);
  CHECK(settings_load(&s, paths, 2) == 0 && s.lang == LANG_FR);
  unlink(a);
  unlink(b);
  rmdir(dir);
}


/**
 * the settings file, both ways, with lines it does not know and bad values
 **/
static void
test_settings(void) {
  static const char text[] = "# made by hand\r\nlang=de\nnotify=off\nevery=5\nsound=maybe\nautoplay=off\n"
                             "colour=blue\nsaver=on\nhover=off\ntheme=night\nscreen=1440p\n=\nlang\n";
  settings_t s, t;
  char buf[SETTINGS_TEXT];
  const char *line;
  int n;

  settings_default(&s);
  CHECK(s.lang == LANG_SYSTEM && s.notify && s.every == 60 && s.sound && s.autoplay && !s.saver &&
        s.hover && s.theme == SETTINGS_DAY && s.screen == 0 && s.spoilers && s.light && !s.pause && s.rising);
  // the extras: covered spoilers, the light bar, the break and the posts rising
  line = "spoilers=off\nlight=off\nbreak=60\nrising=off\nid2=1234567890\nid1=12a\nid3=1,2\n";
  settings_parse(&s, line, strlen(line));
  CHECK(!s.spoilers && !s.light && s.pause == 60 && !s.rising && !strcmp(s.ids[1], "1234567890") &&
        !s.ids[0][0] && !s.ids[2][0]);
  line = "break=45\n";
  settings_parse(&s, line, strlen(line));
  CHECK(s.pause == 60);
  line = "break=off\n";
  settings_parse(&s, line, strlen(line));
  CHECK(s.pause == 0);
  line = "break=120\nspoilers=on\n";
  settings_parse(&s, line, strlen(line));
  CHECK(s.pause == 120 && s.spoilers);
  settings_parse(&s, text, sizeof text - 1);
  CHECK(s.lang == LANG_DE && !s.notify && s.every == SETTINGS_EVERY_MIN && s.sound && !s.autoplay &&
        s.saver && !s.hover && s.theme == SETTINGS_NIGHT && s.screen == 1440);
  settings_parse(&s, "theme=dusk\n", 11);
  CHECK(s.theme == SETTINGS_NIGHT);
  settings_parse(&s, "screen=8k\n", 10);
  CHECK(s.screen == 1440);
  settings_parse(&s, "screen=4k\n", 10);
  CHECK(s.screen == 2160);
  settings_parse(&s, "every=100000\nlang=klingon\n", 26);
  CHECK(s.every == SETTINGS_EVERY_MAX && s.lang == LANG_DE);
  settings_parse(&s, "every=120\nlang=system", 21);
  CHECK(s.every == 120 && s.lang == LANG_SYSTEM);
  // where Following was left, per account; numbers between commas alone
  line = "seen2=1855,1854,1850\nseen1=12,x\nseen3=,1\nseen4=1,,2\nseen6=1\nseen5=9\n";
  settings_parse(&s, line, strlen(line));
  CHECK(!strcmp(s.seen[1], "1855,1854,1850") && !s.seen[0][0] && !s.seen[2][0] && !s.seen[3][0] &&
        !strcmp(s.seen[4], "9"));
  line = "seen1=1234567890123456789,1234567890123456788,1234567890123456787\n";
  settings_parse(&s, line, strlen(line));
  CHECK(!strcmp(s.seen[0], "1234567890123456789,1234567890123456788,1234567890123456787"));
  line = "seen1=12345678901234567890123456789012345678901234567890123456789012345\n";
  settings_parse(&s, line, strlen(line));
  CHECK(!strcmp(s.seen[0], "1234567890123456789,1234567890123456788,1234567890123456787"));

  // written and read back, the same
  s.lang = LANG_NL;
  CHECK((n=settings_format(&s, buf, sizeof buf)) > 0 && strstr(buf, "lang=nl\n") && strstr(buf, "theme=night\n") &&
        strstr(buf, "hover=off\n") && strstr(buf, "screen=4k\n") && strstr(buf, "seen2=1855,1854,1850\n") &&
        strstr(buf, "break=120\n") && strstr(buf, "light=off\n") && strstr(buf, "id2=1234567890\n"));
  settings_default(&t);
  settings_parse(&t, buf, (size_t)n);
  CHECK(!memcmp(&s, &t, sizeof s));
  CHECK(settings_format(&s, buf, 20) == -1);
  // all five accounts with their names and three long numbers fit
  for(int i=0; i < SETTINGS_ACCOUNTS; i++) {
    snprintf(s.users[i], sizeof s.users[i], "abcdefghijklmn%d", i);
    snprintf(s.ids[i], sizeof s.ids[i], "123456789012345678%d", i);
    snprintf(s.seen[i], sizeof s.seen[i], "%s", "1234567890123456789,1234567890123456788,1234567890123456787");
  }
  CHECK((n=settings_format(&s, buf, sizeof buf)) > 0);
  settings_default(&t);
  settings_parse(&t, buf, (size_t)n);
  CHECK(!memcmp(&s, &t, sizeof s));

  CHECK(!strcmp(settings_lang_code(LANG_PT), "pt") && !strcmp(settings_lang_code(LANG_SYSTEM), "en"));
  CHECK(settings_lang_of_system(7) == LANG_PT && settings_lang_of_system(17) == LANG_PT &&
        settings_lang_of_system(4) == LANG_DE && settings_lang_of_system(99) == LANG_EN);
}


/**
 * which paths may be read, and the lines the payload sends them in
 **/
static void
test_captures(void) {
  static capture_t list[4], back[4];
  char buf[2048];
  int n;

  CHECK(captures_kind("/user/av_contents/photo/Astro Bot_20261104.jpg") == CAPTURE_JPEG);
  CHECK(captures_kind("/user/av_contents/a/b/clip.mp4") == CAPTURE_MP4);
  CHECK(captures_kind("/user/av_contents/clip.webm") == CAPTURE_WEBM);
  CHECK(captures_kind("/user/av_contents/shot.png") == CAPTURE_PNG);
  CHECK(captures_kind("/user/av_contents/../home/secret.jpg") == -1);
  CHECK(captures_kind("/user/av_contents/./x.jpg") == -1);
  CHECK(captures_kind("/user/av_contentsX/x.jpg") == -1);
  CHECK(captures_kind("/data/TwitterPS5/session.txt") == -1);
  CHECK(captures_kind("/user/av_contents/notes.txt") == -1);
  CHECK(captures_kind("/user/av_contents/a\nb.jpg") == -1 && captures_kind(NULL) == -1);

  snprintf(list[0].path, sizeof list[0].path, "/user/av_contents/photo/old shot.jpg");
  list[0].time = 100;
  list[0].size = 2000;
  snprintf(list[1].path, sizeof list[1].path, "/user/av_contents/video/new clip.mp4");
  list[1].time = 300;
  list[1].size = 9000000;
  snprintf(list[2].path, sizeof list[2].path, "/user/av_contents/photo/middle.png");
  list[2].time = 200;
  list[2].size = 5;
  captures_sort(list, 3);
  CHECK(list[0].time == 300 && list[1].time == 200 && list[2].time == 100);
  CHECK((n=captures_format(list, 3, buf, sizeof buf)) > 0);
  CHECK(captures_parse(buf, (size_t)n, back, 4) == 3);
  CHECK(!strcmp(back[0].path, list[0].path) && back[0].size == 9000000 && back[0].kind == CAPTURE_MP4);
  CHECK(!strcmp(back[2].path, "/user/av_contents/photo/old shot.jpg") && back[2].kind == CAPTURE_JPEG);
  CHECK(captures_parse(buf, (size_t)n, back, 2) == 2);
  CHECK(captures_format(list, 3, buf, 40) == -1);
  // lines that do not read, or whose path may not be read, stay out
  CHECK(captures_parse("1 2 /etc/passwd.jpg\nx y /user/av_contents/a.jpg\n5 -1 /user/av_contents/b.jpg\n"
                       "7 8 /user/av_contents/c.jpg", 104, back, 4) == 1 && back[0].time == 7);
}


/**
 * a file of the walk's test tree, written at a time
 **/
static void
put_file(const char *root, const char *rel, long long when) {
  char path[512], dir[512], *slash;
  struct timespec times[2];
  FILE *f;

  snprintf(path, sizeof path, "%s/%s", root, rel);
  snprintf(dir, sizeof dir, "%s", path);
  for(slash=strchr(dir + 1, '/'); slash; slash=strchr(slash + 1, '/')) {
    *slash = 0;
    mkdir(dir, 0755);
    *slash = '/';
  }
  CHECK((f=fopen(path, "wb")) != NULL);
  if(f) {
    fputs("x", f);
    fclose(f);
  }
  times[0].tv_sec = times[1].tv_sec = (time_t)when;
  times[0].tv_nsec = times[1].tv_nsec = 0;
  utimensat(AT_FDCWD, path, times, 0);
}


/**
 * the walk finds the PS5's layout (photo/NPXS40087/<title>/<n>/), five
 * folders deep, leaves the thumbnails and other files out, and keeps the
 * newest ones
 **/
static void
test_captures_walk(const char *root) {
  static capture_t list[8];
  char path[512];

  mkdir(root, 0755);
  put_file(root, "photo/NPXS40087/PPSA01234/035/Astro Bot_20261104120000.jpg", 1000);
  put_file(root, "photo/NPXS40087/PPSA01234/036/Astro Bot_20261105120000.png", 3000);
  put_file(root, "video/NPXS40087/PPSA01234/035/Astro Bot_20261106120000.mp4", 5000);
  put_file(root, "video/NPXS40087/PPSA05678/001/clip.webm", 4000);
  put_file(root, "photo/NPXS40087/PPSA01234/035/Astro Bot_20261104120000.dat", 6000);
  put_file(root, "thumbnail/NPXS40087/PPSA01234/035/small.jpg", 7000);
  put_file(root, "photo/a/b/c/d/e/f/g/h/i/j/too deep.jpg", 8000);
  CHECK(captures_scan(root, list, 8) == 4);
  CHECK(list[0].kind == CAPTURE_MP4 && list[0].time == 5000 && list[0].size == 1);
  CHECK(list[1].kind == CAPTURE_WEBM && list[2].kind == CAPTURE_PNG && list[3].kind == CAPTURE_JPEG);
  snprintf(path, sizeof path, "%s/photo/NPXS40087/PPSA01234/035/Astro Bot_20261104120000.jpg", root);
  CHECK(!strcmp(list[3].path, path));
  CHECK(captures_stats.files == 5 && captures_stats.kept == 4 && !captures_stats.unreadable);
  // more than fit: the newest stay
  CHECK(captures_scan(root, list, 2) == 2 && list[0].time == 5000 && list[1].time == 4000);
  // a folder without photo/ and video/ is walked itself, without thumbnails
  snprintf(path, sizeof path, "%s/thumbnail", root);
  CHECK(captures_scan(path, list, 8) == 1 && list[0].time == 7000);
  snprintf(path, sizeof path, "%s/none", root);
  CHECK(captures_scan(path, list, 8) == -1);

  // the newest of one game, a clip or a picture, for the payload's toast
  CHECK(captures_newest_of(root, "PPSA01234", &list[0]) == 1 && list[0].time == 5000 &&
        list[0].kind == CAPTURE_MP4 && strstr(list[0].path, "/video/NPXS40087/PPSA01234/035/"));
  CHECK(captures_newest_of(root, "PPSA05678", &list[0]) == 1 && list[0].kind == CAPTURE_WEBM);
  CHECK(captures_newest_of(root, "PPSA09999", &list[0]) == 0 && !list[0].path[0]);
  CHECK(captures_newest_of(root, "../PPSA01234", &list[0]) == -1);
  CHECK(captures_newest_of(root, "ppsa01234", &list[0]) == -1);
  put_file(root, "photo/NPXS40087/PPSA01234/037/Astro Bot_20261107120000.jpg", 9000);
  CHECK(captures_newest_of(root, "PPSA01234", &list[0]) == 1 && list[0].time == 9000 &&
        list[0].kind == CAPTURE_JPEG);

  // a folder's names, as the payload finds the users under /user/home
  {
    char names[64], *at = names;
    int n, photo = 0, video = 0;
    snprintf(path, sizeof path, "%s", root);
    CHECK((n=captures_folder_names(path, names, sizeof names)) >= 3);
    for(int i=0; i < n; i++, at += strlen(at) + 1) {
      photo |= !strcmp(at, "photo");
      video |= !strcmp(at, "video");
      CHECK(strcmp(at, ".") && strcmp(at, ".."));
    }
    CHECK(photo && video);
    CHECK(captures_folder_names(path, names, 4) == 0); // none fits
    snprintf(path, sizeof path, "%s/none", root);
    CHECK(captures_folder_names(path, names, sizeof names) == -1);
  }
}


/**
 * every language has the texts, months and words, with the English for a
 * text the table lacks
 **/
static void
test_lang(void) {
  char buf[64];

  CHECK(!strcmp(lang_text(LANG_PT, "Defini\xc3\xa7\xc3\xb5" "es", "Settings"), "Defini\xc3\xa7\xc3\xb5" "es"));
  CHECK(!strcmp(lang_text(LANG_EN, "Defini\xc3\xa7\xc3\xb5" "es", "Settings"), "Settings"));
  CHECK(!strcmp(lang_text(LANG_DE, "", "Settings"), "Einstellungen"));
  CHECK(!strcmp(lang_text(LANG_ES, "", "Settings"), "Configuraci\xc3\xb3n"));
  CHECK(!strcmp(lang_text(LANG_NL, "", "Not in the table"), "Not in the table"));
  CHECK(!strcmp(lang_text(LANG_SYSTEM, "", "Settings"), "Settings"));
  for(int l=0; l < LANGS; l++) {
    CHECK(lang_name(l)[0] && lang_month(l, 0)[0] && lang_month(l, 11)[0]);
    CHECK(lang_count_word(l, 2, 1)[0] && lang_unit(l, 1)[0]);
    CHECK(strcmp(lang_text(l, "Seguir", "Back to game"), "") != 0);
  }
  lang_date(LANG_PT, 4, 10, 2026, buf, sizeof buf);
  CHECK(!strcmp(buf, "4 de nov de 2026"));
  lang_date(LANG_DE, 4, 10, 0, buf, sizeof buf);
  CHECK(!strcmp(buf, "4. Nov."));
  lang_date(LANG_EN, 4, 10, 2026, buf, sizeof buf);
  CHECK(!strcmp(buf, "Nov 4, 2026"));
  CHECK(lang_decimal(LANG_EN) == '.' && lang_decimal(LANG_FR) == ',');
}


/**
 * the screen opens at height rows (0 for the TV's) as w x h, and the copy
 * into the tiled buffer puts every pixel where the formula says
 **/
static void
tiles_at(int height, unsigned w, unsigned h) {
  const char *step = "";
  uint32_t *pixels;
  const uint8_t *frame;
  int bad = 0;

  CHECK(video_open(height, &step) == 0);
  CHECK(video_width() == (int)w && video_height() == (int)h);
  CHECK(g_attr_w == w && g_attr_h == h);
  pixels = video_pixels();
  for(unsigned y=0; y < h; y++) {
    for(unsigned x=0; x < w; x++) {
      pixels[(size_t)y * w + x] = (y << 16) | x;
    }
  }
  video_present();
  CHECK(g_flipped == 0);
  frame = g_memory;
  for(unsigned y=0; y < h; y++) {
    for(unsigned x=0; x < w; x++) {
      uint32_t v;
      if(tiled_offset(x, y) + 4 > g_frame_used) {
        bad++;
        continue;
      }
      memcpy(&v, frame + tiled_offset(x, y), 4);
      bad += v != ((y << 16) | x);
    }
  }
  CHECK(bad == 0);
  // the second buffer starts past the first one's blocks
  CHECK(g_frames[1] >= g_frames[0] + g_frame_used);

  video_present();
  CHECK(g_flipped == 1);
  free(g_memory);
  g_memory = NULL;
  mem_free(g_pixels);
  g_pixels = NULL;
}


/*
 * The big shapes drawn by the workers in parts, and the same shapes drawn
 * whole by another thread (workers_run runs a thread's that is not the
 * drawing one where it is): the same pixels.
 */

#define SHARED_W 1000
#define SHARED_H 600
#define SHARED_K 2

static uint32_t g_shared_pic[800 * 600], g_shared_small[333 * 211];

static void
shared_shapes(uint32_t *pixels) {
  const text_surface_t s = {pixels, SHARED_W, SHARED_H, SHARED_K, 0};
  const text_surface_t band = text_band(&s, 37, 501);

  for(size_t i=0; i < (size_t)SHARED_W * SHARED_H * SHARED_K * SHARED_K; i++) {
    pixels[i] = 0xFF000000u | (uint32_t)(i * 2654435761u >> 8);
  }
  draw_rect(&s, -20, 10, 900, 500, 0x80336699);
  draw_box(&s, 40, 30, 800, 420, 24, 0, 0xC0FFFFFF);
  draw_box(&s, 60, 50, 700, 380, 18, 3, 0xFF2080F0);
  draw_shadow(&s, 100, 80, 600, 300, 20, 40, 0x60000000);
  draw_rim(&s, 30, 20, 900, 520, 26, 1.5f, 0xA0FFFFFF, 0.3f);
  draw_picture(&s, 120, 90, 400, 300, g_shared_pic);
  draw_picture_round(&band, 500, -10, 400, 300, 30, g_shared_pic);
  draw_picture_scaled(&band, 20, 200, 640, 360, 16, g_shared_small, 333, 211);
  draw_box(&band, -30, 300, 1100, 400, 12, 0, 0x90102030);
}

static void *
shared_thread(void *arg) {
  shared_shapes(arg);
  return NULL;
}

static void
test_shared(void) {
  const size_t count = (size_t)SHARED_W * SHARED_H * SHARED_K * SHARED_K;
  uint32_t *a = malloc(count * 4), *b = malloc(count * 4);
  pthread_t thread;

  for(size_t i=0; i < sizeof g_shared_pic / 4; i++) {
    g_shared_pic[i] = (uint32_t)(i * 2246822519u) | 0x40000000u;
  }
  for(size_t i=0; i < sizeof g_shared_small / 4; i++) {
    g_shared_small[i] = (uint32_t)(i * 3266489917u) | 0xC0000000u;
  }
  shared_shapes(a);
  CHECK(!pthread_create(&thread, NULL, shared_thread, b) && !pthread_join(thread, NULL));
  CHECK(!memcmp(a, b, count * 4));
  free(a);
  free(b);
}


/**
 * the screen at the TV's size, at the size asked, at 1080p when VideoOut
 * does not take more, on one thread and with workers
 **/
static void
test_tiles(void) {
  const char *step = "";
  int w = 0, h = 0, code = 0;

  tiles_at(0, 3840, 2160); // a 4K TV
  CHECK(video_tv(&w, &h) >= 0 && w == 3840 && h == 2160);
  CHECK(!video_refused(&h, &code, &step));
  CHECK(workers_init(3) == 3);
  test_shared();
  tiles_at(1440, 2560, 1440);
  tiles_at(2160, 3840, 2160);
  g_register_max = 1080;
  tiles_at(2160, 1920, 1080);
  CHECK(video_refused(&h, &code, &step) && h == 2160 && code == -1 &&
        !strcmp(step, "sceVideoOutRegisterBuffers2"));
  g_register_max = 2160;
  g_tv_lines = 1080;
  tiles_at(0, 1920, 1080);
  g_tv_lines = 1440;
  tiles_at(0, 2560, 1440);
  tiles_at(900, 1920, 1080);
}


/*
 * mem.c, with pages from aligned_alloc in place of the PS5's flexible memory.
 */

static int g_maps; // mapped and not yet unmapped

static void *
fake_map(size_t len) {
  void *p;

  CHECK(len % MEM_PAGE == 0);
  if((p=aligned_alloc(MEM_PAGE, len))) {
    g_maps++;
  }
  return p;
}


static void
fake_unmap(void *p, size_t len) {
  CHECK(len % MEM_PAGE == 0 && (uintptr_t)p % MEM_PAGE == 0);
  g_maps--;
  free(p);
}


/**
 * small blocks come from malloc, big ones are mapped and grow in place
 * into their last page; the rest of the tests then run on it too
 **/
static void
test_mem(void) {
  unsigned char *small, *big, *moved;

  mem_use(fake_map, fake_unmap);
  CHECK((small=mem_calloc(100, 1)) != NULL && g_maps == 0 && !small[99]);
  CHECK((uintptr_t)small % 16 == 0);
  CHECK((big=mem_calloc(MEM_MAP_MIN, 1)) != NULL && g_maps == 1 && !big[MEM_MAP_MIN - 1]);
  CHECK((uintptr_t)big % 16 == 0);
  big[0] = 7;
  big[MEM_MAP_MIN - 1] = 9;

  // 64 KiB and its header take five 16 KiB pages, with room left in the last
  CHECK(mem_realloc(big, MEM_MAP_MIN + 1000) == big && g_maps == 1);
  CHECK((moved=mem_realloc(big, 4 * MEM_MAP_MIN)) != NULL && moved != big && g_maps == 1);
  CHECK(moved[0] == 7 && moved[MEM_MAP_MIN - 1] == 9);

  // a small block that grows big moves to the pages, and back when it shrinks
  small[0] = 5;
  CHECK((small=mem_realloc(small, 2 * MEM_MAP_MIN)) != NULL && g_maps == 2 && small[0] == 5);
  CHECK((small=mem_realloc(small, 10)) != NULL && g_maps == 1 && small[0] == 5);

  CHECK(mem_calloc((size_t)-1 / 2, 4) == NULL && mem_alloc((size_t)-1) == NULL);
  mem_free(small);
  mem_free(moved);
  mem_free(NULL);
  CHECK(g_maps == 0);
}


/**
 * one of the port's counters once it reaches want, or after two seconds
 **/
static int
audio_wait(const int *what, int want) {
  const struct timespec step = {0, 5 * 1000000};
  int got = 0;

  for(int i=0; i < 400; i++) {
    pthread_mutex_lock(&g_audio_lock);
    got = *what;
    pthread_mutex_unlock(&g_audio_lock);
    if(got >= want) {
      break;
    }
    nanosleep(&step, NULL);
  }
  return got;
}


/**
 * the pointer's tick: its shape, and its thread with a port that does not
 * open at first, then plays it panned
 **/
static void
test_sound(void) {
  static float tick[SOUND_FRAMES + 64];
  const struct timespec settle = {0, 60 * 1000000};
  const int side = (int)lrintf(TICK_GAIN * 1.41421f * 32767);
  float peak = 0, early = 0, all = 0;

  sound_make(tick, SOUND_FRAMES + 64);
  for(int i=0; i < SOUND_FRAMES; i++) {
    peak = fabsf(tick[i]) > peak ? fabsf(tick[i]) : peak;
    all += tick[i] * tick[i];
    early += i < SOUND_RATE / 50 ? tick[i] * tick[i] : 0;
  }
  CHECK(peak > 0.999f && peak < 1.001f);
  CHECK(fabsf(tick[0]) < 1e-6f && fabsf(tick[SOUND_FRAMES - 1]) < 0.01f); // no click at either end
  CHECK(early > 0.9f * all); // a tick, not a tone: nearly all of it in the first 20 ms
  for(int i=SOUND_FRAMES; i < SOUND_FRAMES + 64; i++) {
    CHECK(tick[i] == 0);
  }

  // nothing before the thread, and no port tried again within 5 s
  sound_tick(0);
  CHECK(!sound_init(NULL));
  sound_tick(0);
  CHECK(audio_wait(&g_audio_opens, 1) == 1);
  sound_tick(0);
  nanosleep(&settle, NULL);
  pthread_mutex_lock(&g_audio_lock);
  CHECK(g_audio_opens == 1 && g_audio_blocks == 0);
  g_audio_us += 6 * 1000000ull;
  g_audio_fail = 0;
  pthread_mutex_unlock(&g_audio_lock);

  // all left: the tick's blocks, then one silent one
  sound_tick(-5);
  CHECK(audio_wait(&g_audio_blocks, SOUND_BLOCKS + 1) == SOUND_BLOCKS + 1);
  nanosleep(&settle, NULL);
  pthread_mutex_lock(&g_audio_lock);
  CHECK(g_audio_opens == 2 && g_audio_blocks == SOUND_BLOCKS + 1 && g_audio_quiet == 1);
  CHECK(abs(g_audio_left - side) <= side / 50 && g_audio_right == 0);
  g_audio_left = g_audio_right = 0;
  pthread_mutex_unlock(&g_audio_lock);

  // the middle: both sides, each 3 dB under one side alone
  sound_tick(0);
  CHECK(audio_wait(&g_audio_blocks, 2 * (SOUND_BLOCKS + 1)) == 2 * (SOUND_BLOCKS + 1));
  pthread_mutex_lock(&g_audio_lock);
  CHECK(g_audio_opens == 2);
  CHECK(abs(g_audio_left - side * 7071 / 10000) <= side / 50 && abs(g_audio_right - g_audio_left) <= 2);
  pthread_mutex_unlock(&g_audio_lock);
}


int
main(int argc, char **argv) {
  unsigned char *ttf, *inter;
  text_font_t *f;

  if(argc != 4) {
    fprintf(stderr, "usage: title_test <DejaVuSans.ttf> <Inter-Regular.ttf> <empty folder>\n");
    return 2;
  }
  ttf = read_file(argv[1]);
  inter = read_file(argv[2]);
  if(!(f=text_font_open(ttf, 34))) {
    fprintf(stderr, "cannot open the font\n");
    return 2;
  }
  test_mem();
  test_utf8();
  test_utf16();
  test_measure(f);
  test_break(f);
  test_draw(f);
  test_cache(f);
  test_fallback(inter, ttf);
  test_emoji(f);
  test_shapes();
  test_images();
  test_jpeg();
  test_settings();
  test_settings_files();
  test_sound();
  test_game();
  test_captures();
  test_captures_walk(argv[3]);
  test_lang();
  test_tiles();

  text_font_close(f);
  free(inter);
  free(ttf);
  printf(g_failed ? "%d checks failed\n" : "all title checks passed\n", g_failed);
  return g_failed != 0;
}
