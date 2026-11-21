/* XPS5's promotional video: its own screens, drawn by title/ui.c as on the
PS5, with made-up people and posts in English, one frame at a time at 60
frames a second, the controller's buttons and sticks pressed as a hand
would, and captions over them; the PS5 home screen's picture of XPS5
(title/art/pic0.png) opens and closes it. Nothing comes from X and no real
account is in it: the pictures are drawn on the spot, as in the preview
(tests/preview.c, whose made-up world this reuses).

The frames go to stdout, raw (RGBA, 1920x1080, 60 a second), for ffmpeg:
tests/promo.sh. PROMO_LINK changes the last card's line about where to get
it. */

#define main preview_main
#include "preview.c"
#undef main

#include "draw.h"
#include "promo_art.h" // pic0_png (promo.sh)

#define FPS   60
#define W     1920
#define H     1080
#define DELAY 14 // frames before a list asked for comes, as X answers

#define WHITE_A(a) ((uint32_t)(a) << 24 | 0xFFFFFFu)


/*
 * The frames.
 */

static long long    g_t;          // frames out
static int          g_broken;     // stdout gone
static text_font_t *g_big, *g_mid, *g_small, *g_cap;
static uint32_t    *g_art;        // pic0, 1920x1080

// the caption over the app, between two frames
static const char *g_caption;
static long long   g_cap_from, g_cap_to;

// the black over everything: from a to b between two frames
static float     g_fade_a, g_fade_b;
static long long g_fade_from, g_fade_to;


static float
clamp01(float v) {
  return v < 0 ? 0 : v > 1 ? 1 : v;
}


static float
ease(float v) {
  v = clamp01(v);
  return v * v * (3 - 2 * v);
}


static uint32_t
with_alpha(uint32_t color, float a) {
  return (uint32_t)lroundf((color >> 24) * clamp01(a)) << 24 | (color & 0xFFFFFFu);
}


/**
 * the frame out: to stdout, or with PROMO_SHOTS=<dir> one a second (and
 * PROMO_EVERY=<frames>) into PNG files there, to look at
 **/
static void
emit(void) {
  static const char *shots;
  static int every, asked;
  size_t n = (size_t)g_pw * g_ph;
  char path[512];

  if(!asked) {
    asked = 1;
    shots = getenv("PROMO_SHOTS");
    every = getenv("PROMO_EVERY") ? atoi(getenv("PROMO_EVERY")) : FPS;
    every = every < 1 ? 1 : every;
  }
  if(shots && shots[0]) {
    if(g_t % every == 0) {
      snprintf(path, sizeof path, "%s/promo-%05lld.png", shots, g_t);
      if(!stbi_write_png(path, g_pw, g_ph, 4, g_pixels, g_pw * 4)) {
        g_broken = 1;
      }
    }
  } else if(!g_broken && fwrite(g_pixels, 4, n, stdout) != n) {
    g_broken = 1;
  }
  g_t++;
}


static void
fade(float from, float to, float seconds) {
  g_fade_a = from;
  g_fade_b = to;
  g_fade_from = g_t;
  g_fade_to = g_t + (long long)(seconds * FPS);
}


static void
caption(const char *text, float seconds) {
  g_caption = text;
  g_cap_from = g_t;
  g_cap_to = g_t + (long long)(seconds * FPS);
}


static void
draw_text_at(text_font_t *f, const text_surface_t *s, int x, int y, const char *text, uint32_t color) {
  text_draw(f, s, x, y, text, strlen(text), color);
}


/**
 * the caption: a dark pill at the bottom, its light blue rim like the
 * home screen's picture, fading in and out
 **/
static void
draw_caption(const text_surface_t *s) {
  float in, out, a;
  int w, h = 84, x, y;

  if(!g_caption || g_t < g_cap_from || g_t >= g_cap_to) {
    return;
  }
  in = ease((g_t - g_cap_from) / 14.0f);
  out = ease((g_cap_to - g_t) / 14.0f);
  a = in < out ? in : out;
  w = text_width(g_cap, g_caption, strlen(g_caption)) + 2 * 46;
  x = (W - w) / 2;
  y = 944 + (int)lroundf((1 - in) * 18);
  draw_shadow(s, x, y, w, h, h / 2.0f, 28, with_alpha(0x90000000u, a));
  draw_box(s, x, y, w, h, h / 2.0f, 0, with_alpha(0xEB1A1008u, a));
  draw_box(s, x, y, w, h, h / 2.0f, 2, with_alpha(0xFFFF7A2Eu, a));
  draw_text_at(g_cap, s, x + 46, y + 56, g_caption, WHITE_A(lroundf(255 * a)));
}


static void
draw_fade(const text_surface_t *s) {
  float a = g_fade_b;

  if(g_t < g_fade_to && g_fade_to > g_fade_from) {
    a = g_fade_a + (g_fade_b - g_fade_a) * ease((float)(g_t - g_fade_from) / (g_fade_to - g_fade_from));
  }
  if(a > 0.002f) {
    draw_rect(s, 0, 0, W, H, (uint32_t)lroundf(255 * clamp01(a)) << 24);
  }
}


/*
 * The pictures: made-up game screens and photos, drawn here, each its own
 * scene (the preview's sunsets for the rest).
 */

typedef struct { float r, g, b; } col_t;

// a scene: the colour at (X, Y), Y from 0 to 1 down, X from 0 to a across,
// px the size of a pixel in those units
typedef col_t scene_t(float X, float Y, float a, float px, unsigned seed);


static col_t
col(float r, float g, float b) {
  col_t c = {r, g, b};
  return c;
}


static col_t
mix(col_t a, col_t b, float t) {
  t = clamp01(t);
  return col(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t);
}


static col_t
add(col_t a, col_t b, float k) {
  return col(a.r + b.r * k, a.g + b.g * k, a.b + b.b * k);
}


static col_t
scale(col_t a, float k) {
  return col(a.r * k, a.g * k, a.b * k);
}


static float
smooth(float e0, float e1, float x) {
  return ease((x - e0) / (e1 - e0));
}


static float
fract(float v) {
  return v - floorf(v);
}


static unsigned
cell_hash(int x, int y, unsigned seed) {
  unsigned n = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ seed * 83492791u;

  n = (n ^ (n >> 13)) * 1274126177u;
  return n ^ (n >> 16);
}


static float
cell_rand(int x, int y, unsigned seed) {
  return (cell_hash(x, y, seed) & 0xFFFF) / 65535.0f;
}


/**
 * stars on a grid of cells, so that a small and a big copy show one sky
 **/
static float
stars(float X, float Y, unsigned seed) {
  float gx = X * 70, gy = Y * 70;
  int cx = (int)floorf(gx), cy = (int)floorf(gy);
  unsigned n = cell_hash(cx, cy, seed);
  float sx, sy;

  if(n % 5) {
    return 0;
  }
  sx = cx + 0.2f + ((n >> 8) & 255) / 255.0f * 0.6f;
  sy = cy + 0.2f + ((n >> 16) & 255) / 255.0f * 0.6f;
  return (1 - smooth(0.04f, 0.2f, hypotf(gx - sx, gy - sy))) * (0.35f + (n >> 24) / 255.0f * 0.65f);
}


/**
 * a ringed planet over a nebula, a moon: a space game
 **/
static col_t
space_at(float X, float Y, float a, float px, unsigned seed) {
  float neb = 0.5f + 0.5f * sinf(X * 3.1f + sinf(Y * 4.3f + seed % 7) * 1.7f) * sinf(Y * 2.3f - X * 1.3f);
  // the seed picks the planet's colours, its side, its size and how its ring leans
  static const col_t dark[] = {{0.92f, 0.45f, 0.28f}, {0.12f, 0.50f, 0.62f}, {0.50f, 0.28f, 0.78f}};
  static const col_t light[] = {{0.99f, 0.84f, 0.62f}, {0.74f, 0.95f, 0.95f}, {0.96f, 0.72f, 0.88f}};
  static const col_t rings[] = {{0.95f, 0.82f, 0.62f}, {0.80f, 0.90f, 1.00f}, {1.00f, 0.86f, 0.95f}};
  static const col_t airs[] = {{0.35f, 0.55f, 1.00f}, {0.30f, 0.95f, 1.00f}, {1.00f, 0.45f, 0.85f}};
  int look = (seed >> 5) % 3, right = (seed >> 3) & 1;
  float PX = a * (right ? 0.62f : 0.36f) + (seed % 17) / 200.0f, PY = 0.54f + ((seed >> 11) % 9) / 100.0f;
  float pr = 0.24f + 0.03f * ((seed >> 9) % 3), tilt = (seed >> 7) & 1 ? -0.32f : 0.24f;
  float dx = X - PX, dy = Y - PY, d = hypotf(dx, dy);
  float rx = dx * cosf(tilt) + dy * sinf(tilt), ry = -dx * sinf(tilt) + dy * cosf(tilt);
  float rd = hypotf(rx, ry / 0.2f), ring = 0, planet, glow;
  float mx = X - a * (right ? 0.2f : 0.82f), my = Y - 0.26f, md = hypotf(mx, my);
  col_t c = col(0.03f + 0.08f * Y + 0.30f * neb * neb, 0.02f + 0.06f * neb * neb, 0.09f + 0.14f * (1 - Y) + 0.24f * neb);

  c = add(c, col(1, 1, 1), stars(X, Y, seed));
  // the moon, lit from the left
  if(md < 0.06f + px) {
    float lit = clamp01(0.35f + 0.9f * (-mx / 0.06f) * 0.7f + 0.3f * (-my / 0.06f));
    c = mix(c, scale(col(0.78f, 0.76f, 0.82f), 0.25f + 0.75f * lit), 1 - smooth(0.06f - px, 0.06f + px, md));
  }
  // the ring: its far half behind the planet, its near half before it
  if(rd > 1.3f * pr && rd < 2.05f * pr) {
    ring = (1 - smooth(1.95f * pr, 2.05f * pr, rd)) * smooth(1.3f * pr, 1.4f * pr, rd) *
           (0.55f + 0.35f * sinf(rd * 95));
  }
  col_t ring_c = rings[look];
  if(ry < 0) {
    c = mix(c, ring_c, ring);
  }
  planet = 1 - smooth(pr - px, pr + px, d);
  if(planet > 0) {
    float nx = dx / pr, ny = dy / pr, nz = sqrtf(fmaxf(0, 1 - nx * nx - ny * ny));
    float lam = fmaxf(0, -0.55f * nx - 0.45f * ny + 0.70f * nz) + 0.06f;
    float band = 0.5f + 0.5f * sinf((ny + 0.12f * sinf(nx * 5)) * 15 + seed % 5);
    col_t surf = mix(dark[look], light[look], band);
    c = mix(c, scale(surf, lam), planet);
  }
  if(ry >= 0) {
    c = mix(c, scale(ring_c, 0.9f), ring);
  }
  // the air's light around it
  glow = d > pr ? expf(-(d - pr) * 26) : 0;
  return add(c, airs[look], 0.45f * glow);
}


/**
 * a road into the sunset, a car from behind: a racing game
 **/
static col_t
racing_at(float X, float Y, float a, float px, unsigned seed) {
  float hz = 0.52f, cx = X - a * 0.5f, sx = a * (0.32f + (seed % 30) / 100.0f), sun;
  col_t c;

  if(Y < hz) {
    float mount = hz - 0.07f - 0.05f * sinf(X * 5 + seed % 9) - 0.025f * sinf(X * 13 + seed % 11);
    float t = Y / hz;
    c = mix(col(0.20f, 0.10f, 0.36f), col(1.0f, 0.55f, 0.30f), t * t);
    sun = hypotf(X - sx, Y - (hz - 0.07f));
    c = add(c, col(1, 0.75f, 0.45f), 0.35f * expf(-sun * 9));
    c = mix(c, col(1, 0.92f, 0.70f), 1 - smooth(0.075f - px, 0.075f + px, sun));
    if(Y > mount) {
      c = mix(col(0.24f, 0.12f, 0.28f), col(0.62f, 0.36f, 0.38f), (Y - mount) / (hz - mount + 0.001f));
    }
  } else {
    float depth = (Y - hz) / (1 - hz), z = 0.12f / (Y - hz + 0.002f);
    float bend = 0.22f * (1 - depth) * (1 - depth) * ((seed % 2) ? 1 : -1);
    float half = 0.015f + 0.80f * depth, off = cx - bend, edge = fabsf(off) / half;
    if(edge < 1) {
      c = mix(col(0.16f, 0.15f, 0.19f), col(0.36f, 0.27f, 0.30f), 1 - depth);
      if(edge < 0.018f && fract(z * 0.8f) < 0.5f) {
        c = col(0.92f, 0.90f, 0.85f);
      }
      if(edge > 0.90f) {
        c = fract(z * 1.6f) < 0.5f ? col(0.85f, 0.15f, 0.12f) : col(0.92f, 0.90f, 0.88f);
      }
    } else {
      c = mix(col(0.48f, 0.34f, 0.20f), col(0.70f, 0.45f, 0.30f), 1 - depth);
      c = scale(c, fract(z * 0.5f) < 0.5f ? 1 : 0.88f);
    }
    // the car, from behind
    {
      float bx = fabsf(cx), shadow = hypotf(cx / 0.36f, (Y - 0.905f) / 0.03f);
      if(shadow < 1) {
        c = scale(c, 0.45f + 0.55f * shadow);
      }
      if(Y > 0.86f && Y < 0.93f && bx > 0.19f && bx < 0.29f) {
        c = col(0.04f, 0.04f, 0.05f);
      }
      if(Y > 0.70f && Y < 0.885f && bx < 0.31f - fmaxf(0, 0.74f - Y) * 0.8f) {
        float shine = 1 - (Y - 0.70f) / 0.185f;
        c = mix(col(0.06f, 0.14f, 0.42f), col(0.25f, 0.45f, 0.95f), shine * shine);
        if(Y > 0.755f && Y < 0.79f && bx > 0.17f && bx < 0.28f) {
          c = col(1.0f, 0.16f, 0.12f);
        }
        if(Y > 0.82f && Y < 0.85f && bx < 0.07f) {
          c = col(0.85f, 0.85f, 0.8f);
        }
      }
      if(Y > 0.60f && Y <= 0.70f && bx < 0.20f - (0.70f - Y) * 0.7f) {
        c = mix(col(0.05f, 0.07f, 0.12f), col(0.40f, 0.30f, 0.40f), (0.70f - Y) * 6);
      }
      if(Y > 0.74f && Y < 0.81f) {
        float glow = fminf(hypotf(bx - 0.225f, (Y - 0.772f) * 2), 1);
        c = add(c, col(1, 0.1f, 0.05f), 0.25f * expf(-glow * 30));
      }
    }
  }
  return c;
}


/**
 * a striped sun, neon mountains and a glowing grid: a retro shooter
 **/
static col_t
neon_at(float X, float Y, float a, float px, unsigned seed) {
  float hz = 0.62f, cx = X - a * (0.45f + (seed % 20) / 100.0f), sy = hz - 0.2f, sr = 0.22f;
  col_t c;

  if(Y < hz) {
    float t = Y / hz, sd = hypotf(cx, Y - sy), tri = fabsf(fract(X * 1.6f + (seed % 13) / 13.0f) - 0.5f) * 2;
    float mount = hz - 0.03f - 0.14f * (1 - tri) * (0.6f + 0.4f * sinf(X * 2.3f));
    c = mix(col(0.04f, 0.02f, 0.12f), col(0.50f, 0.08f, 0.42f), t * t);
    c = add(c, col(1, 1, 1), stars(X, Y, seed) * (1 - t));
    c = add(c, col(1, 0.3f, 0.6f), 0.3f * expf(-fmaxf(0, sd - sr) * 9));
    if(sd < sr + px) {
      float k = (Y - (sy - sr)) / (2 * sr);
      if(Y <= sy || fract((Y - sy) * 34) >= (Y - sy) / sr * 0.7f) {
        c = mix(c, mix(col(1, 0.92f, 0.35f), col(1, 0.22f, 0.58f), k), 1 - smooth(sr - px, sr + px, sd));
      }
    }
    if(Y > mount) {
      c = col(0.08f, 0.02f, 0.16f);
      c = add(c, col(0.2f, 0.95f, 1.0f), (1 - smooth(0, 3 * px, Y - mount)) * 0.9f);
    }
  } else {
    float d = Y - hz + 0.0005f, z = 0.22f / d, xw = cx * z * 1.4f, zl = z * 1.2f + (seed % 10) / 10.0f;
    float per_px_z = 0.22f / (d * d) * 1.2f * px, per_px_x = z * 1.4f * px;
    float lz = fminf(fract(zl), 1 - fract(zl)) / per_px_z, lx = fminf(fract(xw), 1 - fract(xw)) / per_px_x;
    float line = fmaxf(1 - smooth(0.6f, 1.8f, lz), 1 - smooth(0.6f, 1.8f, lx));
    float near = smooth(0, 0.25f, d);
    c = mix(col(0.30f, 0.05f, 0.30f), col(0.03f, 0.0f, 0.07f), near);
    c = add(c, col(1.0f, 0.25f, 0.85f), line * (0.35f + 0.65f * near));
    c = add(c, col(1, 0.3f, 0.7f), 0.5f * expf(-d * 40));
  }
  return c;
}


/**
 * floating islands, clouds and coins in a bright sky: a platformer
 **/
static col_t
islands_at(float X, float Y, float a, float px, unsigned seed) {
  col_t c = mix(col(0.36f, 0.66f, 1.0f), col(0.78f, 0.92f, 1.0f), Y);

  for(int i=0; i<4; i++) {
    float cxl = a * cell_rand(i, 1, seed), cyl = 0.15f + 0.5f * cell_rand(i, 2, seed), cr = 0.06f;
    float cl = fminf(fminf(hypotf((X - cxl) / 2.2f, Y - cyl), hypotf((X - cxl - 0.06f) / 1.6f, Y - cyl + 0.03f)),
                     hypotf((X - cxl + 0.07f) / 1.8f, Y - cyl + 0.01f));
    c = mix(c, col(1, 1, 1), 0.9f * (1 - smooth(cr - 2 * px, cr + 2 * px, cl)));
  }
  for(int i=0; i<3; i++) {
    float ix = a * (0.18f + 0.33f * i + 0.08f * (cell_rand(i, 3, seed) - 0.5f));
    float iy = 0.50f + 0.25f * cell_rand(i, 4, seed) - (i == 1 ? 0.12f : 0), iw = 0.16f + 0.06f * cell_rand(i, 5, seed);
    float top = hypotf((X - ix) / iw, (Y - iy) / 0.035f), dy = Y - iy;
    if(dy > 0 && dy < 0.26f) {
      float half = iw * powf(1 - dy / 0.26f, 1.6f);
      if(fabsf(X - ix) < half) {
        c = mix(col(0.62f, 0.42f, 0.28f), col(0.36f, 0.24f, 0.18f), dy / 0.26f + 0.3f * (X - ix) / iw);
      }
    }
    if(top < 1 + px * 30) {
      c = mix(c, mix(col(0.40f, 0.85f, 0.35f), col(0.25f, 0.62f, 0.25f), (Y - iy + 0.035f) / 0.07f),
              1 - smooth(1 - px * 30, 1 + px * 30, top));
    }
    for(int k=-1; k<=1; k++) {
      float coin = hypotf(X - ix - k * 0.06f, Y - iy + 0.09f + 0.02f * (k == 0));
      if(coin < 0.022f + px) {
        col_t gold = coin < 0.012f ? col(1, 0.95f, 0.55f) : col(1, 0.78f, 0.15f);
        c = mix(c, gold, 1 - smooth(0.022f - px, 0.022f + px, coin));
      }
    }
  }
  return c;
}


/**
 * a skyline over a river at dusk, lit windows and their light on the water:
 * a photo walk
 **/
static col_t
city_above(float X, float Y, float hz, unsigned seed) {
  static const col_t tops[] = {{0.16f, 0.20f, 0.42f}, {0.30f, 0.16f, 0.40f}, {0.05f, 0.08f, 0.20f}};
  static const col_t lows[] = {{1.00f, 0.66f, 0.36f}, {0.98f, 0.52f, 0.52f}, {0.25f, 0.36f, 0.62f}};
  int look = seed % 3, b;
  float t = Y / hz, off = (seed % 97) / 13.0f, bx, bh;
  col_t c = mix(tops[look], lows[look], t * t);

  bx = (X + off) * 11;
  b = (int)floorf(bx);
  bh = 0.10f + 0.22f * cell_rand(b, 0, seed) + (cell_hash(b, 1, seed) % 7 == 0 ? 0.15f : 0);
  if(Y > hz - bh) {
    float wx = fract(bx * 4), wy = fract((hz - Y) * 42);
    c = scale(mix(tops[look], col(0.05f, 0.05f, 0.08f), 0.7f), 0.9f + 0.1f * fract(bx));
    if(fract(bx) > 0.1f && fract(bx) < 0.9f && wx > 0.3f && wx < 0.7f && wy > 0.35f && wy < 0.65f &&
       cell_hash((int)floorf(bx * 4), (int)floorf((hz - Y) * 42), seed) % 5 == 0) {
      c = mix(c, col(1.0f, 0.80f, 0.50f), 0.75f);
    }
  }
  return c;
}


static col_t
city_at(float X, float Y, float a, float px, unsigned seed) {
  float hz = 0.56f + 0.08f * ((seed >> 4) % 3) / 2.0f, back, ripple;
  col_t c;

  (void)a;
  (void)px;
  if(Y < hz) {
    return city_above(X, Y, hz, seed);
  }
  ripple = 0.006f * sinf(Y * 260 + sinf(X * 18) * 2) + 0.003f * sinf(X * 90 + Y * 40);
  back = hz - (Y - hz) * 1.1f + ripple;
  c = city_above(X + ripple, back < 0 ? 0 : back, hz, seed);
  return mix(scale(c, 0.62f), col(0.05f, 0.08f, 0.16f), 0.25f + 0.4f * (Y - hz) / (1 - hz));
}


/**
 * an orange cat sitting up in a warm room, a controller on the rug before it
 **/
static col_t
cat_at(float X, float Y, float a, float px, unsigned seed) {
  float C = a * 0.40f, lamp = hypotf(X - a * 0.86f, Y - 0.22f), dx = X - C;
  col_t c = mix(col(0.62f, 0.44f, 0.32f), col(0.36f, 0.24f, 0.20f), Y);
  float body, head, tail, e1, e2;

  (void)seed;
  c = add(c, col(1, 0.75f, 0.45f), 0.45f * expf(-lamp * 3.2f));
  c = mix(c, col(1, 0.93f, 0.75f), 1 - smooth(0.045f - px, 0.045f + px, lamp));
  if(Y > 0.72f) {
    float plank = fract(X * 5 + (fract(Y * 9) < 0.5f ? 0.5f : 0));
    c = mix(col(0.46f, 0.30f, 0.19f), col(0.36f, 0.22f, 0.14f), (Y - 0.72f) / 0.28f);
    if(plank < 0.012f || fract(Y * 9) < 0.03f) {
      c = scale(c, 0.8f);
    }
    if(hypotf((X - a * 0.52f) / 0.62f, (Y - 0.86f) / 0.11f) < 1) {
      c = col(0.16f, 0.40f, 0.44f);
    }
  }
  // its shadow on the floor
  if(hypotf(dx / 0.18f, (Y - 0.835f) / 0.03f) < 1) {
    c = scale(c, 0.6f);
  }
  body = hypotf(dx / 0.14f, (Y - 0.66f) / 0.175f);
  head = hypotf(dx, Y - 0.42f) / 0.105f;
  tail = fabsf(hypotf(X - (C + 0.17f), Y - 0.75f) - 0.075f);
  if((tail < 0.018f && X > C + 0.11f && Y > 0.7f) || body < 1 || head < 1) {
    float stripe = sinf(Y * 75 + sinf(dx * 40) * 2) > 0.55f && (body > 0.55f || head < 1) ? 0.72f : 1;
    float lit = 0.8f + 0.25f * clamp01((dx + 0.1f) / 0.25f);
    c = scale(col(0.90f, 0.52f, 0.22f), stripe * lit);
    if(hypotf(dx / 0.075f, (Y - 0.70f) / 0.11f) < 1 || hypotf(dx / 0.05f, (Y - 0.47f) / 0.035f) < 1) {
      c = col(0.97f, 0.88f, 0.74f);
    }
  }
  // the ears, above the head
  for(int s=-1; s<=1 && head >= 1; s+=2) {
    float ex = dx * s, ey = Y;
    if(ey > 0.29f && ey < 0.40f && ex > 0.03f + (0.40f - ey) * 0.45f && ex < 0.105f - (0.40f - ey) * 0.15f) {
      c = scale(col(0.90f, 0.52f, 0.22f), 0.9f);
      if(ex > 0.05f + (0.40f - ey) * 0.45f && ex < 0.09f - (0.40f - ey) * 0.15f && ey > 0.32f) {
        c = col(0.95f, 0.62f, 0.60f);
      }
    }
  }
  // the eyes and the nose
  e1 = hypotf((dx + 0.042f) / 0.022f, (Y - 0.415f) / 0.027f);
  e2 = hypotf((dx - 0.042f) / 0.022f, (Y - 0.415f) / 0.027f);
  if(e1 < 1 || e2 < 1) {
    c = col(0.50f, 0.80f, 0.30f);
    if(fabsf(fabsf(dx) - 0.042f) < 0.006f) {
      c = col(0.05f, 0.05f, 0.05f);
    }
  }
  if(Y > 0.452f && Y < 0.47f && fabsf(dx) < (0.47f - Y) * 0.9f) {
    c = col(0.90f, 0.50f, 0.52f);
  }
  // the controller on the rug
  {
    float kx = X - (C + 0.33f), ky = Y - 0.86f;
    float boxd = fmaxf(fabsf(kx) - 0.10f, fabsf(ky) - 0.02f);
    float grip = fminf(hypotf(kx + 0.09f, ky - 0.035f), hypotf(kx - 0.09f, ky - 0.035f)) - 0.042f;
    float shape = fminf(boxd - 0.02f, grip);
    if(shape < px) {
      c = mix(c, mix(col(0.24f, 0.24f, 0.28f), col(0.12f, 0.12f, 0.14f), (ky + 0.04f) / 0.12f), 1 - smooth(-px, px, shape));
      if(fminf(hypotf(kx + 0.04f, ky - 0.02f), hypotf(kx - 0.04f, ky - 0.02f)) < 0.018f) {
        c = col(0.06f, 0.06f, 0.07f);
      }
      for(int k=0; k<4; k++) {
        float bxk = 0.075f + (k == 1 ? 0.018f : k == 3 ? -0.018f : 0), byk = -0.01f + (k == 0 ? -0.016f : k == 2 ? 0.016f : 0);
        if(hypotf(kx - bxk, ky - byk) < 0.008f) {
          c = col(0.75f, 0.76f, 0.80f);
        }
      }
    }
  }
  return c;
}


/**
 * which scene a picture's address is, and its seed
 **/
static scene_t *
scene_of(const char *url, unsigned *seed) {
  *seed = hash(url);
  if(strstr(url, "Starfall Odyssey") || strstr(url, "promo-ending")) {
    return space_at;
  } else if(strstr(url, "Skyline Drift")) {
    return racing_at;
  } else if(strstr(url, "Neon Rift")) {
    return neon_at;
  } else if(strstr(url, "Pocket Planets")) {
    return islands_at;
  } else if(strstr(url, "promo-river")) {
    return city_at;
  } else if(strstr(url, "promo-cat")) {
    return cat_at;
  }
  return NULL;
}


static uint32_t *
promo_picture(const char *url, int w, int h) {
  unsigned seed;
  scene_t *scene = scene_of(url, &seed);
  uint32_t *px;
  float a = (float)w / h, one = 1.0f / h;

  if(!scene || !(px=mem_alloc((size_t)w * h * sizeof *px))) {
    return NULL;
  }
  for(int y=0; y<h; y++) {
    for(int x=0; x<w; x++) {
      col_t c = scene((x + 0.5f) * one, (y + 0.5f) * one, a, one, seed);
      px[y * w + x] = rgb(c.r, c.g, c.b);
    }
  }
  return px;
}


/**
 * the PS5's captures the gallery shows, of made-up games, newest first
 **/
static int
promo_captures(capture_t *list) {
  static const struct { const char *path; int kind; long long size; } made[] = {
    {"/user/av_contents/photo/NPXS40087/PPSA99001/001/Starfall Odyssey_20261112_214210.jpg", CAPTURE_JPEG, 2400000},
    {"/user/av_contents/video/NPXS40087/PPSA99001/001/Starfall Odyssey_20261112_213501.mp4", CAPTURE_MP4, 48200000},
    {"/user/av_contents/photo/NPXS40087/PPSA99001/001/Starfall Odyssey_20261112_212830.jpg", CAPTURE_JPEG, 2300000},
    {"/user/av_contents/photo/NPXS40087/PPSA99002/001/Skyline Drift_20261111_221530.png", CAPTURE_PNG, 9800000},
    {"/user/av_contents/photo/NPXS40087/PPSA99002/001/Skyline Drift_20261111_221102.jpg", CAPTURE_JPEG, 1900000},
    {"/user/av_contents/video/NPXS40087/PPSA99002/001/Skyline Drift_20261111_215840.webm", CAPTURE_WEBM, 31000000},
    {"/user/av_contents/photo/NPXS40087/PPSA99003/001/Neon Rift_20261110_200012.jpg", CAPTURE_JPEG, 2100000},
    {"/user/av_contents/photo/NPXS40087/PPSA99003/001/Neon Rift_20261110_195845.jpg", CAPTURE_JPEG, 2050000},
    {"/user/av_contents/photo/NPXS40087/PPSA99004/001/Pocket Planets_20261109_181500.jpg", CAPTURE_JPEG, 2600000},
  };
  int n = (int)(sizeof made / sizeof *made);

  for(int i=0; i<n; i++) {
    memset(&list[i], 0, sizeof list[i]);
    snprintf(list[i].path, sizeof list[i].path, "%s", made[i].path);
    list[i].kind = made[i].kind;
    list[i].size = made[i].size;
    list[i].time = NOW - 1800 - i * 20000LL;
  }
  return n;
}


/*
 * What X would answer, made up.
 */

static x_post_t *
promo_post(x_post_t *p, const char *name, const char *user, int badge, long long ago, const char *text,
           long long replies, long long reposts, long long likes, long long views) {
  return post(p, name, user, badge, ago, text, replies, reposts, likes, views);
}


static int
promo_posts(x_post_t *posts) {
  x_post_t *p;
  int n = 0;

  p = promo_post(&posts[n++], "Mia Torres", "miatorres_gg", X_BADGE_BLUE, 12 * 60,
                 "Finally finished it! 60 hours in and the ending was worth every minute. "
                 "Anyone else just sat through the credits, still holding the controller?",
                 48, 12, 1250, 15342);
  media(p, "promo-ending", 1600, 900, 0);

  p = promo_post(&posts[n++], "City Photo Walks", "cityphotowalks", X_BADGE_GOLD, 3 * 3600,
                 "Sunset over the river last night. Thanks to everyone who came on the photo walk!",
                 32, 210, 3400, 98000);
  snprintf(p->reposter, sizeof p->reposter, "leopark_ps");
  snprintf(p->reposter_name, sizeof p->reposter_name, "Leo Park");
  media(p, "promo-river-1", 1200, 800, 0);
  media(p, "promo-river-2", 1200, 800, 0);
  media(p, "promo-river-3", 1200, 800, 0);
  media(p, "promo-river-4", 1200, 800, 0);

  p = promo_post(&posts[n++], "Sam Okafor", "samokafor_plays", X_BADGE_NONE, 5 * 3600,
                 "This is so true.", 5, 2, 87, 2100);
  snprintf(p->playing, sizeof p->playing, "Starfall Odyssey");
  snprintf(p->quote_user, sizeof p->quote_user, "ninacarter_gg");
  snprintf(p->quote_name, sizeof p->quote_name, "Nina Carter");
  snprintf(p->quote_avatar, sizeof p->quote_avatar, "https://pbs.twimg.com/profile_images/2/ninacarter_gg_normal.jpg");
  p->quote_badge = X_BADGE_BLUE;
  snprintf(p->quote_text, sizeof p->quote_text,
           "Hot take: the best part of any big game is the photo mode. I spend more time there than in the "
           "story.");

  p = promo_post(&posts[n++], "Jade Rivers", "jade_and_whiskers", X_BADGE_NONE, 26 * 3600,
                 "My cat discovered the controller. Send help.", 120, 940, 15200, 410000);
  media(p, "promo-cat", 1920, 1080, 1);

  promo_post(&posts[n++], "Game Patch Notes", "gamepatchnotes", X_BADGE_GOLD, 2 * 86400,
             "What changes in this week's update:\n\n"
             "\xe2\x80\xa2 New photo mode with depth of field\n"
             "\xe2\x80\xa2 Shorter loading between map areas\n"
             "\xe2\x80\xa2 Co-op fixes\n"
             "\xe2\x80\xa2 Bigger subtitles, with an optional background\n"
             "\xe2\x80\xa2 Three new outfits for everyone who finished the campaign",
             64, 310, 2900, 120500);

  promo_post(&posts[n++], "Ben Ortiz", "benortiz_88", X_BADGE_NONE, 40 * 86400,
             "Anyone up for a match tonight? Free all weekend.", 3, 0, 9, 0);
  return n;
}


static void
promo_me(x_user_t *u) {
  memset(u, 0, sizeof *u);
  snprintf(u->id, sizeof u->id, "%u", 1000 + hash("xps5_demo") % 9000);
  snprintf(u->user, sizeof u->user, "xps5_demo");
  snprintf(u->name, sizeof u->name, "Alex");
  snprintf(u->avatar, sizeof u->avatar, "https://pbs.twimg.com/profile_images/1/xps5_demo_normal.jpg");
  snprintf(u->banner, sizeof u->banner, "https://pbs.twimg.com/profile_banners/1/xps5_demo");
  u->badge = X_BADGE_BLUE;
  snprintf(u->bio, sizeof u->bio, "Games, photo mode and coffee.");
  u->following = 312;
  u->followers = 4580;
  u->posts = 2210;
  u->joined = 1243973549LL;
}


static int
promo_own(x_post_t *posts) {
  promo_post(&posts[0], "Alex", "xps5_demo", X_BADGE_BLUE, 2 * 86400,
             "My new game room is finally ready!", 12, 30, 640, 22000);
  return 1;
}


static int
promo_notices(x_post_t *posts) {
  static const char *const likes[] = {"miatorres_gg", "leopark_ps", "benortiz_88"};
  static const char *const follow[] = {"jordanlee_gg"};
  static const char *const reposts[] = {"samokafor_plays", "miatorres_gg"};
  static const char about[] = "My new game room is finally ready!";
  int n = 0;

  notice(&posts[n++], X_ICON_LIKE, 25 * 60, "Mia Torres, Leo Park and 1 other liked your post", about, likes, 3);
  notice(&posts[n++], X_ICON_FOLLOW, 3 * 3600, "Jordan Lee followed you", NULL, follow, 1);
  notice(&posts[n++], X_ICON_REPOST, 8 * 3600, "Sam Okafor and Mia Torres reposted your post", about, reposts, 2);
  promo_post(&posts[n++], "Nina Carter", "ninacarter_gg", X_BADGE_BLUE, 9 * 3600,
             "@xps5_demo That looks amazing! Where did you get the shelf?", 1, 0, 4, 120);
  return n;
}


static int
promo_trends(x_post_t *posts) {
  int n = 0;

  trend(&posts[n++], 1, "Gaming \xc2\xb7 Trending", "#PS5Share", "#PS5Share", "12.3K posts");
  trend(&posts[n++], 2, "Gaming \xc2\xb7 Trending", "#PhotoMode", "#PhotoMode", "8,410 posts");
  trend(&posts[n++], 3, "Trending", "Speedrunning", "Speedrunning", "21K posts");
  trend(&posts[n++], 4, "Gaming \xc2\xb7 Trending", "#IndieGames", "#IndieGames", "5,302 posts");
  trend(&posts[n++], 5, "Trending", "Retro Gaming", "\"Retro Gaming\"", "9,870 posts");
  trend(&posts[n++], 6, "Gaming \xc2\xb7 Trending", "#NowPlaying", "#NowPlaying", "44K posts");
  return n;
}


static int
promo_users(x_post_t *posts, int playing) {
  static const struct { const char *user, *name, *game; int badge, following; } who[] = {
    {"indie_spotlight", "Indie Spotlight", "", X_BADGE_GOLD, 0},
    {"retro_corner", "Retro Corner", "", X_BADGE_NONE, 0},
    {"nina_speedruns", "Nina \xc2\xb7 speedruns and coffee", "", X_BADGE_BLUE, 1},
  }, friends[] = {
    {"miatorres_gg", "Mia Torres", "Starfall Odyssey", X_BADGE_BLUE, 1},
    {"benortiz_88", "Ben Ortiz", "Skyline Drift", X_BADGE_NONE, 1},
    {"jade_and_whiskers", "Jade Rivers", "Neon Rift", X_BADGE_NONE, 1},
    {"leopark_ps", "Leo Park", "", X_BADGE_NONE, 1},
  };
  int n = playing ? (int)(sizeof friends / sizeof *friends) : (int)(sizeof who / sizeof *who);

  for(int i=0; i < n; i++) {
    const char *user = playing ? friends[i].user : who[i].user;
    x_post_t *p = &posts[i];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_USER;
    snprintf(p->user_id, sizeof p->user_id, "%u", 1000 + hash(user) % 9000);
    snprintf(p->user, sizeof p->user, "%s", user);
    snprintf(p->name, sizeof p->name, "%s", playing ? friends[i].name : who[i].name);
    snprintf(p->avatar, sizeof p->avatar, "https://pbs.twimg.com/profile_images/1/%s_normal.jpg", user);
    snprintf(p->playing, sizeof p->playing, "%s", playing ? friends[i].game : "");
    p->badge = playing ? friends[i].badge : who[i].badge;
    p->following = playing ? friends[i].following : who[i].following;
  }
  return n;
}


static int
promo_chats(x_post_t *p) {
  static const struct { const char *id, *user, *name, *text; int unread, mine, group, icon, ago; } c[] = {
    {"1-77", "miatorres_gg", "Mia Torres", "See you online at 9?", 1, 0, 0, X_DM_TEXT, 120},
    {"55", "", "Weekend Squad", "I'll bring the extra controller", 0, 0, 1, X_DM_TEXT, 3600},
    {"1-91", "leopark_ps", "Leo Park", "", 1, 0, 0, X_DM_PHOTO, 7200},
    {"1-42", "benortiz_88", "Ben Ortiz", "Thanks for the trophy tip!", 0, 1, 0, X_DM_TEXT, 86400 * 3},
  };
  int n = sizeof c / sizeof c[0];

  memset(p, 0, n * sizeof *p);
  for(int i=0; i < n; i++) {
    p[i].kind = X_KIND_CHAT;
    snprintf(p[i].id, sizeof p[i].id, "%s", c[i].id);
    snprintf(p[i].user, sizeof p[i].user, "%s", c[i].user);
    snprintf(p[i].name, sizeof p[i].name, "%s", c[i].name);
    snprintf(p[i].text, sizeof p[i].text, "%s", c[i].text);
    if(c[i].user[0]) {
      snprintf(p[i].user_id, sizeof p[i].user_id, "%u", 1000 + hash(c[i].user) % 9000);
      snprintf(p[i].avatar, sizeof p[i].avatar, "https://pbs.twimg.com/profile_images/1/%s_normal.jpg", c[i].user);
    } else {
      snprintf(p[i].faces[0], sizeof p[i].faces[0], "https://pbs.twimg.com/profile_images/1/miatorres_gg_normal.jpg");
      snprintf(p[i].faces[1], sizeof p[i].faces[1], "https://pbs.twimg.com/profile_images/1/benortiz_88_normal.jpg");
      p[i].face_count = 2;
      snprintf(p[i].quote_name, sizeof p[i].quote_name, "Ben Ortiz");
    }
    p[i].badge = i == 0 ? X_BADGE_BLUE : X_BADGE_NONE;
    p[i].unread = c[i].unread;
    p[i].mine = c[i].mine;
    p[i].group = c[i].group;
    p[i].icon = c[i].icon;
    p[i].time = NOW - c[i].ago;
    p[i].replies = p[i].reposts = p[i].likes = p[i].views = -1;
  }
  return n;
}


/**
 * a list the title asked for, as X would answer it
 **/
static void
answer(const ui_request_t *req) {
  x_post_t *list = mem_calloc(16, sizeof *list);
  x_user_t me;
  int n = 0;

  if(!list) {
    return;
  }
  if(req->side == UI_SIDE_TRENDS || req->side == UI_SIDE_EXPLORE) {
    n = promo_trends(list);
  } else if(req->side == UI_SIDE_WHO) {
    n = promo_users(list, 0);
  } else if(req->side == UI_SIDE_PLAYING) {
    n = promo_users(list, 1);
  } else if(req->side == UI_SIDE_NONE && !req->cursor[0]) {
    switch(req->feed) {
    case UI_FOLLOWING:
    case UI_FOR_YOU:
    case UI_GAME:
      n = promo_posts(list);
      break;
    case UI_ME:
      promo_me(&me);
      ui_result(req, list, promo_own(list), "", &me, ERR_NONE, 200, -1, NOW);
      return;
    case UI_NOTIFICATIONS:
      n = promo_notices(list);
      break;
    case UI_SEARCH:
      n = req->query[0] ? 0 : promo_trends(list);
      break;
    case UI_CHAT:
      n = promo_chats(list);
      break;
    default:
      break;
    }
  }
  if(n) {
    ui_result(req, list, n, "", NULL, ERR_NONE, 200, -1, NOW);
  } else {
    mem_free(list);
    ui_result(req, NULL, 0, "", NULL, ERR_EMPTY, 200, -1, NOW);
  }
}


// the lists asked for, answered DELAY frames later
static struct {
  ui_request_t req;
  long long    due;
} g_asked[16];
static int g_asked_n;
static int g_delay = DELAY;

// the action going out, done at g_act_due; a post's upload shown on the way
static int       g_act, g_keyboard;
static long long g_act_from, g_act_due;


static void
take_requests(void) {
  ui_request_t req;

  while(g_asked_n < 16 && ui_wanted(&req) == 1) {
    g_asked[g_asked_n].req = req;
    g_asked[g_asked_n].due = g_t + g_delay;
    g_asked_n++;
  }
  for(int i=0; i < g_asked_n;) {
    if(g_asked[i].due <= g_t) {
      req = g_asked[i].req;
      g_asked[i] = g_asked[--g_asked_n];
      answer(&req);
    } else {
      i++;
    }
  }
}


static void
take_actions(void) {
  static capture_t captures[CAPTURES_MAX];
  char id[X_ID_MAX], user[64];
  int act, n;

  if(g_act == UI_ACT_POST && g_t < g_act_due) {
    ui_progress((g_t - g_act_from) * 400000LL, (g_act_due - g_act_from) * 400000LL);
  }
  if(g_act && g_t >= g_act_due) {
    ui_action_done(ERR_NONE, 200, -1);
    g_act = 0;
  }
  while((act=ui_action(id, sizeof id, user, sizeof user)) != UI_ACT_NONE) {
    if(act == UI_ACT_COMPOSE_TEXT) {
      g_keyboard = 1;
    } else if(act == UI_ACT_CAPTURES) {
      n = promo_captures(captures);
      ui_captures(captures, n, ERR_NONE, 0);
    } else {
      g_act = act;
      g_act_from = g_t;
      g_act_due = g_t + (act == UI_ACT_POST ? 80 : 10);
    }
  }
}


/**
 * one frame of the app, as the title's loop makes one: the buttons and
 * sticks of the frame, what X answered, a step of the animations, then the
 * screen with the caption and the black over it
 **/
static void
tick(unsigned keys, int lx, int ly, int rx, int ry) {
  text_surface_t screen = {g_pixels, UI_WIDTH, UI_HEIGHT, g_scale, 0};

  ui_elapsed(1000000 / FPS);
  if(keys) {
    ui_keys(keys);
  }
  ui_sticks(lx, ly, rx, ry);
  take_requests();
  take_actions();
  ui_animate();
  load_pictures();
  ui_draw(&screen, NOW, g_frame++);
  draw_caption(&screen);
  draw_fade(&screen);
  emit();
}


static void
hold(float seconds) {
  for(int i = (int)(seconds * FPS); i > 0; i--) {
    tick(0, 0, 0, 0, 0);
  }
}


static void
press(unsigned key, float then) {
  tick(key, 0, 0, 0, 0);
  hold(then);
}


/**
 * the pointer to x, y with the left stick, as a hand moves it: fast while
 * far, slowing down near it
 **/
static void
glide_to(int x, int y) {
  int px, py;

  for(int i=0; i < 4 * FPS; i++) {
    float dx, dy, d, m;
    ui_pointer(&px, &py);
    if(px < 0) {
      tick(0, 30, 0, 0, 0);
      continue;
    }
    dx = (float)(x - px);
    dy = (float)(y - py);
    d = hypotf(dx, dy);
    if(d <= 3) {
      break;
    }
    m = d > 260 ? 127 : 34 + d * 0.36f;
    tick(0, (int)lroundf(dx / d * m), (int)lroundf(dy / d * m), 0, 0);
  }
  tick(0, 0, 0, 0, 0);
}


/**
 * text typed into the composer a word at a time
 **/
static void
type_text(const char *text) {
  char part[X_REPLY_MAX + 1];
  size_t len = strlen(text);

  for(size_t i=1; i <= len; i++) {
    if(i < len && text[i] != ' ') {
      continue;
    }
    snprintf(part, sizeof part, "%.*s", (int)i, text);
    ui_compose_text(part);
    hold(0.16f);
  }
}


/*
 * The cards at the start and the end: the home screen's picture, slowly
 * closer, with words on its left.
 */

static void
draw_art(const text_surface_t *s, float zoom, float dx) {
  // the pan stays inside the picture: past its edge the last column would smear
  const float margin = W / 2.0f * (1 - 1 / (zoom < 1 ? 1 : zoom));
  const float cx = W / 2.0f + (dx < -margin ? -margin : dx > margin ? margin : dx), cy = H / 2.0f;

  for(int y=0; y < H; y++) {
    float sy = cy + (y - H / 2.0f) / zoom;
    int y0 = (int)sy;
    float fy = sy - y0;
    y0 = y0 < 0 ? 0 : y0 > H - 2 ? H - 2 : y0;
    for(int x=0; x < W; x++) {
      float sx = cx + (x - W / 2.0f) / zoom;
      int x0 = (int)sx;
      float fx = sx - x0;
      uint32_t a, b, c, d, out = 0xFF000000u;
      x0 = x0 < 0 ? 0 : x0 > W - 2 ? W - 2 : x0;
      a = g_art[y0 * W + x0];
      b = g_art[y0 * W + x0 + 1];
      c = g_art[(y0 + 1) * W + x0];
      d = g_art[(y0 + 1) * W + x0 + 1];
      for(int k=0; k < 24; k += 8) {
        float top = ((a >> k) & 255) * (1 - fx) + ((b >> k) & 255) * fx;
        float bottom = ((c >> k) & 255) * (1 - fx) + ((d >> k) & 255) * fx;
        out |= (uint32_t)lroundf(top * (1 - fy) + bottom * fy) << k;
      }
      s->pixels[y * W + x] = out;
    }
  }
}


typedef struct line {
  text_font_t *font;
  const char  *text;
  int          y;      // baseline
  uint32_t     color;
  float        at;     // seconds into the card when it comes in
} line_t;


/**
 * a card of seconds, coming in from black and going to black in its last
 * out seconds
 **/
static void
card(const line_t *lines, int count, float seconds, float zoom_from, float zoom_to, float out) {
  text_surface_t s = {g_pixels, W, H, 1, 0};
  int frames = (int)(seconds * FPS);

  fade(1, 0, 0.6f);
  for(int f=0; f < frames; f++) {
    float t = (float)f / FPS, k = (float)f / frames;
    if(f == frames - (int)(out * FPS)) {
      fade(0, 1, out);
    }
    draw_art(&s, zoom_from + (zoom_to - zoom_from) * k, -40 * k);
    // a shade on the left, for the words
    for(int x=0; x < 1100; x += 4) {
      draw_rect(&s, x, 0, 4, H, (uint32_t)lroundf(150 * (1 - x / 1100.0f)) << 24);
    }
    for(int i=0; i < count; i++) {
      float a = ease((t - lines[i].at) / 0.5f);
      if(a > 0) {
        draw_text_at(lines[i].font, &s, 150, lines[i].y + (int)lroundf((1 - a) * 22), lines[i].text,
                     with_alpha(lines[i].color, a));
      }
    }
    draw_fade(&s);
    emit();
  }
}


int
main(void) {
  const char *link = getenv("PROMO_LINK");
  settings_t settings;

  static const line_t intro[] = {
    {NULL, "X on your PS5.", 450, 0xFFFFFFFFu, 0.5f},
    {NULL, "A native app, made for the controller.", 530, 0xFFFFE2B8u, 1.0f},
    {NULL, "Unofficial  \xc2\xb7  Free  \xc2\xb7  Open source", 600, 0xFFC8B4A0u, 1.5f},
  };
  line_t lines[3];

  images_init(64 << 20);
  g_picture_hook = promo_picture; // its own game screens and photos
  settings_default(&settings);
  settings.lang = LANG_EN;
  workers_init(3);
  ui_scale(1);
  if(ui_init(&settings, LANG_EN)) {
    return fail("fonts do not open");
  }
  ui_session_info(1, "/data/TwitterPS5/session.txt");
  g_big = text_font_open(inter_bold, 104);
  g_mid = text_font_open(inter_regular, 46);
  g_small = text_font_open(inter_regular, 34);
  g_cap = text_font_open(inter_bold, 40);
  g_art = image_fit(pic0_png, sizeof pic0_png, W, H);
  if(!g_big || !g_mid || !g_small || !g_cap || !g_art) {
    return fail("the promo's fonts or picture do not open");
  }

  // 1. the card: what it is
  memcpy(lines, intro, sizeof intro);
  lines[0].font = g_big;
  lines[1].font = g_mid;
  lines[2].font = g_small;
  card(lines, 3, 4.4f, 1.0f, 1.06f, 0.4f);

  // the app comes in from black, its timeline already there
  g_delay = 0;
  for(int i=0; i < 40; i++) {
    text_surface_t screen = {g_pixels, UI_WIDTH, UI_HEIGHT, g_scale, 0};
    ui_elapsed(1000000 / FPS);
    take_requests();
    take_actions();
    ui_animate();
    load_pictures();
    ui_draw(&screen, NOW, g_frame++);
    g_t++; // not out: the lists come in before the first frame
  }
  g_delay = DELAY;
  fade(1, 0, 0.5f);

  // 2. the timeline, a post at a time, and a like
  caption("Your X timeline, made for the controller", 7.4f);
  hold(1.3f);
  press(UI_DOWN, 1.1f);
  press(UI_DOWN, 1.0f);
  press(UI_R2, 1.3f);
  press(UI_DOWN, 1.2f);
  press(UI_DOWN, 1.2f);

  // 3. a post with the game's captures
  ui_game("PPSA99001", "Starfall Odyssey");
  press(UI_TRIANGLE, 0.6f); // back to the top
  caption("Post with your game captures", 8.6f);
  press(UI_OPTIONS, 0.5f);
  if(g_keyboard) {
    type_text("Finally got the platinum! #StarfallOdyssey #PS5Share");
  }
  hold(0.6f);
  press(UI_TRIANGLE, 1.0f); // the gallery
  press(UI_DOWN, 0.2f);
  press(UI_UP, 0.4f);
  press(UI_CROSS, 0.5f);    // the newest picture
  press(UI_RIGHT, 0.3f);
  press(UI_RIGHT, 0.3f);
  press(UI_CROSS, 0.8f);    // and another
  press(UI_R2, 1.2f);       // into the post
  press(UI_R2, 2.2f);       // out it goes

  // 4. notifications, chat and explore, with the pointer
  caption("Notifications", 2.9f);
  glide_to(74, 277);
  press(UI_CROSS, 2.2f);
  caption("Chat", 2.7f);
  glide_to(74, 341);
  press(UI_CROSS, 2.0f);
  caption("Explore and search", 2.8f);
  glide_to(74, 213);
  press(UI_CROSS, 1.4f);
  press(UI_DOWN, 1.0f);
  fade(0, 1, 0.45f);
  hold(0.45f);

  // 5. the card: where to get it
  lines[0] = (line_t){g_big, "Get XPS5.", 430, 0xFFFFFFFFu, 0.3f};
  lines[1] = (line_t){g_mid, "Free and open source  \xc2\xb7  GPL-3.0", 515, 0xFFFFE2B8u, 0.8f};
  lines[2] = (line_t){g_small, link && link[0] ? link : "Link in the first reply \xe2\x86\x93", 590,
                      0xFFC8B4A0u, 1.3f};
  card(lines, 3, 5.4f, 1.06f, 1.0f, 0.7f);

  fprintf(stderr, "promo: %lld frames, %.1f s%s\n", g_t - 40, (g_t - 40) / (float)FPS,
          g_broken ? ", stdout broke" : "");
  return g_broken;
}
