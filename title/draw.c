/* Drawing for the title's screen, see draw.h. Edges are smooth because each
pixel gets the part of it a shape covers, from the pixel centre's distance to
the shape's outline. Icons are kept as alpha masks once drawn at a size. */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"
#include "workers.h"

#define MAX_POINTS 128
#define MAX_SHAPES 6
#define MASKS      128
#define PI         3.14159265f
#define SHARED_MIN (128 << 10) // pixels a shape covers for the workers to share it

enum { LINE, LOOP, FILL }; // an open line, a closed one, a filled shape

typedef struct shape {
  int   kind;
  int   count;
  float xy[MAX_POINTS * 2];
} shape_t;

typedef struct icon {
  float   width; // of its lines, in 24ths of the icon
  int     count;
  shape_t shapes[MAX_SHAPES];
} icon_t;

typedef struct mask {
  int            icon;
  int            size;
  unsigned char *alpha;
} mask_t;

static icon_t g_icons[ICON_COUNT];
static int    g_icons_made;
static mask_t g_masks[MASKS];
static int    g_next_mask;


static float
clamp01(float v) {
  return v < 0 ? 0 : v > 1 ? 1 : v;
}


/**
 * blend color over *px; a is 0..255
 **/
static void
put(uint32_t *px, uint32_t color, unsigned a) {
  uint32_t d = *px, rb, g;
  unsigned na;

  if(a >= 255) {
    *px = color | 0xFF000000u;
    return;
  }
  if(!a) {
    return;
  }
  na = 255 - a;
  rb = ((d & 0xFF00FF) * na + (color & 0xFF00FF) * a) >> 8;
  g = ((d & 0xFF00) * na + (color & 0xFF00) * a) >> 8;
  *px = 0xFF000000u | (rb & 0xFF00FF) | (g & 0xFF00);
}


/**
 * the alpha for covering cover of a pixel with color
 **/
static unsigned
alpha(uint32_t color, float cover) {
  return (unsigned)(cover * (color >> 24) + 0.5f);
}


static uint32_t *
pixel(const text_surface_t *dst, int x, int y) {
  return dst->pixels + (size_t)y * dst->width + x;
}


static void
rect_px(const text_surface_t *dst, int x, int y, int w, int h, uint32_t color) {
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + w > dst->width ? dst->width : x + w;
  int y1 = y + h > dst->height ? dst->height : y + h;
  unsigned a = color >> 24;

  for(int row=y0; row < y1; row++) {
    uint32_t *line = pixel(dst, 0, row);
    for(int col=x0; col < x1; col++) {
      if(a == 255) {
        line[col] = color;
      } else {
        put(&line[col], color, a);
      }
    }
  }
}


/**
 * signed distance from (px, py) to a box of half sizes hw and hh around
 * (0, 0), with corners of radius r; below 0 inside
 **/
static float
box_distance(float px, float py, float hw, float hh, float r) {
  float qx = fabsf(px) - hw + r, qy = fabsf(py) - hh + r;
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float in = qx > qy ? qx : qy;

  return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}


static float
radius(float r, int w, int h) {
  r = r > w / 2.0f ? w / 2.0f : r;
  r = r > h / 2.0f ? h / 2.0f : r;
  return r < 0 ? 0 : r;
}


/**
 * how much of a pixel at distance d from a box's edge it covers: all of it
 * inside, or with a border t wide, the border's part
 **/
static float
edge_cover(float d, float t) {
  float cover = clamp01(0.5f - d);

  if(t > 0) {
    cover -= clamp01(0.5f - d - t);
  }
  return cover;
}


/*
 * Along a box's straight edges the distance to it is that of a straight
 * line: |px| - hw beside its sides, |py| - hh above and below them; only
 * its four corners need box_distance. Its rows within edge of the top or
 * bottom are its bands, its columns within edge of a side its sides.
 */

static void
box_px_draw(const text_surface_t *dst, int x, int y, int w, int h, float r, float t,
         uint32_t color) {
  float hw = w / 2.0f, hh = h / 2.0f, cx = x + hw, cy = y + hh, d, py;
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + w > dst->width ? dst->width : x + w;
  int y1 = y + h > dst->height ? dst->height : y + h;
  int edge, band, end;
  unsigned a;

  r = radius(r, w, h);
  // how far in from a side its corner and its border reach
  edge = (int)ceilf(r > t ? r : t) + 1;
  for(int row=y0; row < y1; row++) {
    uint32_t *line = pixel(dst, 0, row);
    py = row + 0.5f - cy;
    band = row < y + edge || row >= y + h - edge;
    for(int col=x0; col < x1; col++) {
      if(col >= x + edge && col < x + w - edge) {
        // between the sides: the same all along the row
        end = x + w - edge < x1 ? x + w - edge : x1;
        if(band) {
          a = alpha(color, edge_cover(fabsf(py) - hh, t));
        } else {
          a = t > 0 ? 0 : color >> 24; // nothing of a border inside
        }
        for(; a && col < end; col++) {
          put(&line[col], color, a);
        }
        col = end - 1;
        continue;
      }
      d = band ? box_distance(col + 0.5f - cx, py, hw, hh, r) : fabsf(col + 0.5f - cx) - hw;
      put(&line[col], color, alpha(color, edge_cover(d, t)));
    }
  }
}


static void
shadow_px(const text_surface_t *dst, int x, int y, int w, int h, float r, float spread,
            uint32_t color) {
  float hw = w / 2.0f, hh = h / 2.0f, cx = x + hw, cy = y + hh, d, k;
  int s = (int)ceilf(spread), in, middle;
  int x0 = x - s < 0 ? 0 : x - s, y0 = y - s < 0 ? 0 : y - s;
  int x1 = x + w + s > dst->width ? dst->width : x + w + s;
  int y1 = y + h + s > dst->height ? dst->height : y + h + s;

  r = radius(r, w, h);
  in = (int)ceilf(r) + 1; // past the corners inside, it is all shadow, and covered
  for(int row=y0; row < y1; row++) {
    uint32_t *line = pixel(dst, 0, row);
    const float py = row + 0.5f - cy;
    middle = row >= y + in && row < y + h - in;
    for(int col=x0; col < x1; col++) {
      if(col >= x + in && col < x + w - in) {
        // above or below the box, away from its corners: the same all along
        const int end = x + w - in < x1 ? x + w - in : x1;
        d = fabsf(py) - hh;
        if(!middle && d < spread) {
          k = d <= 0 ? 1 : 1 - d / spread;
          const unsigned a = alpha(color, k * k * (3 - 2 * k));
          for(; col < end; col++) {
            put(&line[col], color, a);
          }
        }
        col = end - 1;
        continue;
      }
      d = middle ? fabsf(col + 0.5f - cx) - hw : box_distance(col + 0.5f - cx, py, hw, hh, r);
      if(d >= spread) {
        continue;
      }
      k = d <= 0 ? 1 : 1 - d / spread;
      put(&line[col], color, alpha(color, k * k * (3 - 2 * k)));
    }
  }
}


static void
rim_px(const text_surface_t *dst, int x, int y, int w, int h, float r, float t,
         uint32_t color, float bottom) {
  float hw = w / 2.0f, hh = h / 2.0f, cx = x + hw, cy = y + hh, d, cover, down, lit;
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + w > dst->width ? dst->width : x + w;
  int y1 = y + h > dst->height ? dst->height : y + h;
  int edge, band;

  r = radius(r, w, h);
  edge = (int)ceilf(r > t ? r : t) + 1;
  for(int row=y0; row < y1; row++) {
    uint32_t *line = pixel(dst, 0, row);
    const float py = row + 0.5f - cy;
    band = row < y + edge || row >= y + h - edge;
    down = h > 1 ? 1 - (float)(row - y) / (h - 1) : 1;
    lit = bottom + (1 - bottom) * down * down;
    for(int col=x0; col < x1; col++) {
      if(col >= x + edge && col < x + w - edge) {
        // between the sides: on a band the same all along, else nothing
        const int end = x + w - edge < x1 ? x + w - edge : x1;
        if(band) {
          cover = clamp01(0.5f - (fabsf(py) - hh)) - clamp01(0.5f - (fabsf(py) - hh) - t);
          const unsigned a = alpha(color, cover * lit);
          for(; a && col < end; col++) {
            put(&line[col], color, a);
          }
        }
        col = end - 1;
        continue;
      }
      d = band ? box_distance(col + 0.5f - cx, py, hw, hh, r) : fabsf(col + 0.5f - cx) - hw;
      cover = clamp01(0.5f - d) - clamp01(0.5f - d - t);
      put(&line[col], color, alpha(color, cover * lit));
    }
  }
}


static void
corners_px(const text_surface_t *dst, int x, int y, int w, int h, float r,
             uint32_t color) {
  float hw = w / 2.0f, hh = h / 2.0f, cx = x + hw, cy = y + hh, d;
  int ex, ey, cols[2], rows[2];

  r = radius(r, w, h);
  ex = (int)ceilf(r) + 1;
  ey = ex;
  ex = ex > w / 2 ? w / 2 : ex;
  ey = ey > h / 2 ? h / 2 : ey;
  cols[0] = x;
  cols[1] = x + w - ex;
  rows[0] = y;
  rows[1] = y + h - ey;

  for(int corner=0; corner < 4; corner++) {
    int left = cols[corner & 1], top = rows[corner >> 1];
    for(int row=top; row < top + ey; row++) {
      if(row < 0 || row >= dst->height) {
        continue;
      }
      for(int col=left; col < left + ex; col++) {
        if(col < 0 || col >= dst->width) {
          continue;
        }
        d = box_distance(col + 0.5f - cx, row + 0.5f - cy, hw, hh, r);
        put(pixel(dst, col, row), color, alpha(color, 1 - clamp01(0.5f - d)));
      }
    }
  }
}


static void
picture_px(const text_surface_t *dst, int x, int y, int w, int h,
             const uint32_t *pixels) {
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + w > dst->width ? dst->width : x + w;
  int y1 = y + h > dst->height ? dst->height : y + h;

  for(int row=y0; row < y1; row++) {
    uint32_t *line = pixel(dst, 0, row);
    const uint32_t *src = pixels + (size_t)(row - y) * w - x;
    for(int col=x0; col < x1; col++) {
      put(&line[col], src[col], src[col] >> 24);
    }
  }
}


static void
picture_round_px(const text_surface_t *dst, int x, int y, int w, int h, float r,
                   const uint32_t *pixels) {
  float hw = w / 2.0f, hh = h / 2.0f, cx = x + hw, cy = y + hh, cover;
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + w > dst->width ? dst->width : x + w;
  int y1 = y + h > dst->height ? dst->height : y + h;
  int edge, band;
  unsigned a;

  r = radius(r, w, h);
  edge = (int)ceilf(r) + 1;
  for(int row=y0; row < y1; row++) {
    uint32_t *line = pixel(dst, 0, row);
    const uint32_t *src = pixels + (size_t)(row - y) * w - x;
    band = row < y + edge || row >= y + h - edge;
    for(int col=x0; col < x1; col++) {
      a = src[col] >> 24;
      if(band && (col < x + edge || col >= x + w - edge)) {
        cover = clamp01(0.5f - box_distance(col + 0.5f - cx, row + 0.5f - cy, hw, hh, r));
        a = (unsigned)(a * cover + 0.5f);
      }
      put(&line[col], src[col], a);
    }
  }
}


float
draw_distance(float px, float py, int x, int y, int w, int h, float r) {
  return box_distance(px - (x + w / 2.0f), py - (y + h / 2.0f), w / 2.0f, h / 2.0f,
                      radius(r, w, h));
}


static void
circle_px(const text_surface_t *dst, float cx, float cy, float r, uint32_t color) {
  int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
  int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
  float dx, dy;

  x0 = x0 < 0 ? 0 : x0;
  y0 = y0 < 0 ? 0 : y0;
  x1 = x1 > dst->width ? dst->width : x1;
  y1 = y1 > dst->height ? dst->height : y1;
  for(int row=y0; row < y1; row++) {
    for(int col=x0; col < x1; col++) {
      dx = col + 0.5f - cx;
      dy = row + 0.5f - cy;
      put(pixel(dst, col, row), color, alpha(color, clamp01(r - sqrtf(dx * dx + dy * dy) + 0.5f)));
    }
  }
}


static void
arc_px(const text_surface_t *dst, float cx, float cy, float r, float t,
         float from, float to, uint32_t color) {
  float span, rel, dx, dy, d, ex, ey, e;
  float ends[2][2];
  int x0 = (int)floorf(cx - r - t), x1 = (int)ceilf(cx + r + t);
  int y0 = (int)floorf(cy - r - t), y1 = (int)ceilf(cy + r + t);

  span = to - from;
  span = span < 0 ? 0 : span > 2 * PI ? 2 * PI : span;
  from = fmodf(from, 2 * PI);
  from = from < 0 ? from + 2 * PI : from;
  for(int i=0; i<2; i++) {
    float a = i ? from + span : from;
    ends[i][0] = cx + r * sinf(a);
    ends[i][1] = cy - r * cosf(a);
  }

  x0 = x0 < 0 ? 0 : x0;
  y0 = y0 < 0 ? 0 : y0;
  x1 = x1 > dst->width ? dst->width : x1;
  y1 = y1 > dst->height ? dst->height : y1;
  for(int row=y0; row < y1; row++) {
    for(int col=x0; col < x1; col++) {
      dx = col + 0.5f - cx;
      dy = row + 0.5f - cy;
      rel = atan2f(dx, -dy) - from;
      rel = rel < 0 ? rel + 2 * PI : rel;
      rel = rel < 0 ? rel + 2 * PI : rel;
      if(rel <= span) {
        d = fabsf(sqrtf(dx * dx + dy * dy) - r);
      } else {
        d = 1e9f;
        for(int i=0; i<2; i++) {
          ex = col + 0.5f - ends[i][0];
          ey = row + 0.5f - ends[i][1];
          e = sqrtf(ex * ex + ey * ey);
          d = e < d ? e : d;
        }
      }
      put(pixel(dst, col, row), color, alpha(color, clamp01(t / 2 - d + 0.5f)));
    }
  }
}


/*
 * The icons, on a 24-unit grid with y going down.
 */

static shape_t *
add_shape(icon_t *icon, int kind) {
  shape_t *s = &icon->shapes[icon->count++];

  s->kind = kind;
  s->count = 0;
  return s;
}


static void
add_point(shape_t *s, float x, float y) {
  if(s->count < MAX_POINTS) {
    s->xy[2 * s->count] = x;
    s->xy[2 * s->count + 1] = y;
    s->count++;
  }
}


static void
add_points(shape_t *s, int count, const float *xy) {
  for(int i=0; i<count; i++) {
    add_point(s, xy[2 * i], xy[2 * i + 1]);
  }
}


/**
 * points of a circle's arc from degree from to degree to, where 0 is to the
 * right and 90 down
 **/
static void
add_arc(shape_t *s, float cx, float cy, float r, float from, float to, int steps) {
  for(int i=0; i <= steps; i++) {
    float a = (from + (to - from) * i / steps) * PI / 180;
    add_point(s, cx + r * cosf(a), cy + r * sinf(a));
  }
}


static icon_t *
icon_with(int id, float width) {
  icon_t *icon = &g_icons[id];

  icon->width = width;
  icon->count = 0;
  return icon;
}


#define POINTS(s, ...) do { \
  static const float xy_[] = {__VA_ARGS__}; \
  add_points(s, sizeof xy_ / sizeof xy_[0] / 2, xy_); \
} while(0)


static void
make_icons(void) {
  icon_t *icon;
  shape_t *s;

  // a speech bubble with its tail down on the left
  icon = icon_with(ICON_REPLY, 1.9f);
  s = add_shape(icon, LOOP);
  add_point(s, 9, 17);
  add_arc(s, 15, 10.5f, 6.5f, 90, -90, 12);
  add_arc(s, 9, 10.5f, 6.5f, -90, -225, 9);
  add_point(s, 4.1f, 20.6f);

  // two arrows going round
  icon = icon_with(ICON_REPOST, 1.9f);
  s = add_shape(icon, LINE);
  POINTS(s, 1.9f, 7.6f, 5, 4.5f, 8.1f, 7.6f);
  s = add_shape(icon, LINE);
  add_point(s, 5, 4.5f);
  add_arc(s, 8, 14, 3, 180, 90, 3);
  add_point(s, 13, 17);
  s = add_shape(icon, LINE);
  POINTS(s, 15.9f, 16.4f, 19, 19.5f, 22.1f, 16.4f);
  s = add_shape(icon, LINE);
  add_point(s, 19, 19.5f);
  add_arc(s, 16, 10, 3, 0, -90, 3);
  add_point(s, 11, 7);

  // a heart, from the curve x = 16 sin^3 t, y = 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t
  icon = icon_with(ICON_LIKE, 1.9f);
  s = add_shape(icon, LOOP);
  for(int i=0; i<64; i++) {
    float t = 2 * PI * i / 64, st = sinf(t);
    float hy = 13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t);
    add_point(s, 12 + 0.6f * 16 * st * st * st, 12 - 0.6f * (hy + 2.5f));
  }
  g_icons[ICON_LIKED] = g_icons[ICON_LIKE];
  g_icons[ICON_LIKED].shapes[0].kind = FILL;

  // bars of a chart
  icon = icon_with(ICON_VIEWS, 2.0f);
  POINTS(add_shape(icon, LINE), 4.5f, 20, 4.5f, 13);
  POINTS(add_shape(icon, LINE), 9.5f, 20, 9.5f, 4.5f);
  POINTS(add_shape(icon, LINE), 14.5f, 20, 14.5f, 9);
  POINTS(add_shape(icon, LINE), 19.5f, 20, 19.5f, 15);

  icon = icon_with(ICON_BOOKMARK, 1.9f);
  POINTS(add_shape(icon, LOOP), 5.5f, 4, 18.5f, 4, 18.5f, 20.5f, 12, 16, 5.5f, 20.5f);
  g_icons[ICON_BOOKMARKED] = g_icons[ICON_BOOKMARK];
  g_icons[ICON_BOOKMARKED].shapes[0].kind = FILL;

  // an arrow up out of a tray
  icon = icon_with(ICON_SHARE, 1.9f);
  POINTS(add_shape(icon, LINE), 12, 4, 12, 15);
  POINTS(add_shape(icon, LINE), 7.5f, 8.5f, 12, 4, 16.5f, 8.5f);
  POINTS(add_shape(icon, LINE), 4, 13.5f, 4, 20.5f, 20, 20.5f, 20, 13.5f);

  // a house with a door
  icon = icon_with(ICON_HOME, 1.9f);
  POINTS(add_shape(icon, FILL), 12, 2.6f, 22, 10.4f, 19.4f, 10.4f, 19.4f, 21, 14.4f, 21,
         14.4f, 15, 9.6f, 15, 9.6f, 21, 4.6f, 21, 4.6f, 10.4f, 2, 10.4f);

  // a rosette with eight bumps
  icon = icon_with(ICON_BADGE, 0);
  s = add_shape(icon, FILL);
  for(int i=0; i<96; i++) {
    float a = 2 * PI * i / 96, r = 10 + 1.05f * cosf(8 * a);
    add_point(s, 12 + r * cosf(a), 12 + r * sinf(a));
  }

  icon = icon_with(ICON_CHECK, 2.4f);
  POINTS(add_shape(icon, LINE), 7.4f, 12.3f, 10.6f, 15.4f, 16.7f, 8.9f);

  icon = icon_with(ICON_PLAY, 0);
  POINTS(add_shape(icon, FILL), 8.6f, 5.8f, 19, 12, 8.6f, 18.2f);

  icon = icon_with(ICON_TRIANGLE, 2.0f);
  POINTS(add_shape(icon, LOOP), 12, 4, 20.6f, 19, 3.4f, 19);

  icon = icon_with(ICON_CIRCLE, 2.0f);
  add_arc(add_shape(icon, LOOP), 12, 12, 8, 0, 360 - 7.5f, 47);

  icon = icon_with(ICON_CROSS, 2.0f);
  POINTS(add_shape(icon, LINE), 5.5f, 5.5f, 18.5f, 18.5f);
  POINTS(add_shape(icon, LINE), 18.5f, 5.5f, 5.5f, 18.5f);

  icon = icon_with(ICON_SQUARE, 2.0f);
  POINTS(add_shape(icon, LOOP), 5, 5, 19, 5, 19, 19, 5, 19);

  icon = icon_with(ICON_UP_DOWN, 2.0f);
  POINTS(add_shape(icon, LINE), 12, 3.5f, 12, 20.5f);
  POINTS(add_shape(icon, LINE), 7.5f, 8, 12, 3.5f, 16.5f, 8);
  POINTS(add_shape(icon, LINE), 7.5f, 16, 12, 20.5f, 16.5f, 16);

  // the same turned on its side
  g_icons[ICON_LEFT_RIGHT] = g_icons[ICON_UP_DOWN];
  for(int i=0; i < g_icons[ICON_LEFT_RIGHT].count; i++) {
    s = &g_icons[ICON_LEFT_RIGHT].shapes[i];
    for(int j=0; j < s->count; j++) {
      float x = s->xy[2 * j];
      s->xy[2 * j] = s->xy[2 * j + 1];
      s->xy[2 * j + 1] = x;
    }
  }

  // most of a circle, clockwise, with a head at its end up on the right
  icon = icon_with(ICON_REFRESH, 2.0f);
  add_arc(add_shape(icon, LINE), 12, 12, 7.5f, 20, 315, 30);
  {
    float a = 315 * PI / 180, ex = 12 + 7.5f * cosf(a), ey = 12 + 7.5f * sinf(a);
    float dx = -sinf(a), dy = cosf(a); // the way the circle goes there
    s = add_shape(icon, FILL);
    add_point(s, ex + 3.2f * dx, ey + 3.2f * dy);
    add_point(s, ex - 1.6f * dx - 3.6f * dy, ey - 1.6f * dy + 3.6f * dx);
    add_point(s, ex - 1.6f * dx + 3.6f * dy, ey - 1.6f * dy - 3.6f * dx);
  }

  icon = icon_with(ICON_TOP, 2.0f);
  POINTS(add_shape(icon, LINE), 12, 20.5f, 12, 8);
  POINTS(add_shape(icon, LINE), 6.5f, 13.5f, 12, 8, 17.5f, 13.5f);
  POINTS(add_shape(icon, LINE), 5, 3.5f, 19, 3.5f);

  icon = icon_with(ICON_SEND, 1.9f);
  POINTS(add_shape(icon, LOOP), 2.8f, 11.2f, 21.2f, 2.8f, 13.4f, 21.2f, 10.9f, 13.1f);
  POINTS(add_shape(icon, LINE), 10.9f, 13.1f, 21.2f, 2.8f);

  // a bell: a dome flaring out at the bottom, its knob and its clapper
  icon = icon_with(ICON_BELL, 1.9f);
  s = add_shape(icon, LOOP);
  POINTS(s, 4.2f, 18, 6.4f, 15.2f);
  add_arc(s, 12, 10.6f, 5.6f, 180, 360, 16);
  POINTS(s, 17.6f, 15.2f, 19.8f, 18);
  POINTS(add_shape(icon, LINE), 12, 3, 12, 5);
  add_arc(add_shape(icon, LINE), 12, 19.6f, 2.4f, 0, 180, 8);

  // a head over shoulders
  icon = icon_with(ICON_PERSON, 1.9f);
  add_arc(add_shape(icon, LOOP), 12, 8, 4.2f, 0, 360 - 7.5f, 47);
  add_arc(add_shape(icon, LOOP), 12, 21, 7.6f, 180, 360, 20);

  icon = icon_with(ICON_PLUS, 2.4f);
  POINTS(add_shape(icon, LINE), 12, 4.5f, 12, 19.5f);
  POINTS(add_shape(icon, LINE), 4.5f, 12, 19.5f, 12);

  icon = icon_with(ICON_BACK, 2.2f);
  POINTS(add_shape(icon, LINE), 19.5f, 12, 4.8f, 12);
  POINTS(add_shape(icon, LINE), 10.8f, 6, 4.8f, 12, 10.8f, 18);

  // a magnifying glass, its handle down on the right
  icon = icon_with(ICON_SEARCH, 2.0f);
  add_arc(add_shape(icon, LOOP), 10.5f, 10.5f, 6.8f, 0, 360 - 7.5f, 47);
  POINTS(add_shape(icon, LINE), 15.5f, 15.5f, 20.5f, 20.5f);

  // a wheel with eight teeth round a hole
  icon = icon_with(ICON_GEAR, 1.9f);
  s = add_shape(icon, LOOP);
  for(int i=0; i<120; i++) {
    float a = 2 * PI * i / 120, t = cosf(8 * a);
    float r = 7.4f + 2.2f * (t > 0.35f ? 1 : t < -0.35f ? 0 : (t + 0.35f) / 0.7f);
    add_point(s, 12 + r * cosf(a), 12 + r * sinf(a));
  }
  add_arc(add_shape(icon, LOOP), 12, 12, 3.2f, 0, 360 - 7.5f, 47);

  // a frame with mountains and a sun
  icon = icon_with(ICON_PHOTO, 1.9f);
  POINTS(add_shape(icon, LOOP), 3, 4.5f, 21, 4.5f, 21, 19.5f, 3, 19.5f);
  POINTS(add_shape(icon, LINE), 3.5f, 17.5f, 9, 11.5f, 13, 15.5f, 15.5f, 13, 20.5f, 18);
  add_arc(add_shape(icon, LOOP), 15.6f, 8.8f, 1.7f, 0, 360 - 12, 29);

  icon = icon_with(ICON_VIDEO, 1.9f);
  POINTS(add_shape(icon, LOOP), 2.5f, 5, 21.5f, 5, 21.5f, 19, 2.5f, 19);
  POINTS(add_shape(icon, FILL), 9.6f, 8.4f, 15.6f, 12, 9.6f, 15.6f);

  icon = icon_with(ICON_PAUSE, 0);
  POINTS(add_shape(icon, FILL), 6, 4.5f, 10, 4.5f, 10, 19.5f, 6, 19.5f);
  POINTS(add_shape(icon, FILL), 14, 4.5f, 18, 4.5f, 18, 19.5f, 14, 19.5f);

  icon = icon_with(ICON_SOUND, 1.9f);
  POINTS(add_shape(icon, FILL), 2.5f, 9, 6.5f, 9, 11.5f, 4.5f, 11.5f, 19.5f, 6.5f, 15, 2.5f, 15);
  add_arc(add_shape(icon, LINE), 12.5f, 12, 3.6f, -50, 50, 10);
  add_arc(add_shape(icon, LINE), 12.5f, 12, 7.6f, -52, 52, 16);

  icon = icon_with(ICON_MUTE, 1.9f);
  POINTS(add_shape(icon, FILL), 2.5f, 9, 6.5f, 9, 11.5f, 4.5f, 11.5f, 19.5f, 6.5f, 15, 2.5f, 15);
  POINTS(add_shape(icon, LINE), 15, 9, 21, 15);
  POINTS(add_shape(icon, LINE), 21, 9, 15, 15);

  // a ball with its meridian and the equator
  icon = icon_with(ICON_GLOBE, 1.7f);
  add_arc(add_shape(icon, LOOP), 12, 12, 8.6f, 0, 360 - 7.5f, 47);
  s = add_shape(icon, LOOP);
  for(int i=0; i<48; i++) {
    float a = 2 * PI * i / 48;
    add_point(s, 12 + 3.8f * cosf(a), 12 + 8.6f * sinf(a));
  }
  POINTS(add_shape(icon, LINE), 3.6f, 12, 20.4f, 12);

  // an envelope, its flap folded in
  icon = icon_with(ICON_CHAT, 1.9f);
  POINTS(add_shape(icon, LOOP), 2.8f, 5.2f, 21.2f, 5.2f, 21.2f, 18.8f, 2.8f, 18.8f);
  POINTS(add_shape(icon, LINE), 3.4f, 6, 12, 12.6f, 20.6f, 6);

  // a handset, the ear up on the left and the mouth down on the right
  icon = icon_with(ICON_CALL, 1.8f);
  s = add_shape(icon, LOOP);
  POINTS(s, 4.6f, 3.2f, 8.2f, 3.2f, 10.2f, 8, 8, 10.2f, 13.8f, 16, 16, 13.8f, 20.8f, 15.8f, 20.8f, 19.4f,
         19, 20.8f);
  add_arc(s, 19, 5, 15.8f, 90, 180, 14);

  // a star of four points, for the assistant tab
  icon = icon_with(ICON_SPARK, 1.8f);
  s = add_shape(icon, LOOP);
  for(int i=0; i<8; i++) {
    float a = PI / 4 * i - PI / 2, r = i % 2 ? 2.6f : 9.2f;
    add_point(s, 12 + r * cosf(a), 12 + r * sinf(a));
  }
  POINTS(add_shape(icon, LINE), 19, 2.8f, 19, 6.2f);
  POINTS(add_shape(icon, LINE), 17.3f, 4.5f, 20.7f, 4.5f);

  // a seal with twelve soft points round a tick
  icon = icon_with(ICON_PREMIUM, 1.8f);
  s = add_shape(icon, LOOP);
  for(int i=0; i<96; i++) {
    float a = 2 * PI * i / 96;
    add_point(s, 12 + (8.6f + 0.9f * cosf(12 * a)) * cosf(a), 12 + (8.6f + 0.9f * cosf(12 * a)) * sinf(a));
  }
  POINTS(add_shape(icon, LINE), 8.2f, 12.2f, 10.9f, 14.9f, 16, 9.4f);

  // a clock whose ring goes back to an arrow
  icon = icon_with(ICON_HISTORY, 1.9f);
  add_arc(add_shape(icon, LINE), 12, 12, 8.4f, 200, 520, 40);
  POINTS(add_shape(icon, LINE), 2.6f, 5.6f, 4.2f, 9.2f, 7.8f, 7.8f);
  POINTS(add_shape(icon, LINE), 12, 7.4f, 12, 12.4f, 15.2f, 14.4f);

  // a page with three lines of text
  icon = icon_with(ICON_ARTICLE, 1.9f);
  POINTS(add_shape(icon, LOOP), 5, 2.8f, 19, 2.8f, 19, 21.2f, 5, 21.2f);
  POINTS(add_shape(icon, LINE), 8.4f, 8, 15.6f, 8);
  POINTS(add_shape(icon, LINE), 8.4f, 12, 15.6f, 12);
  POINTS(add_shape(icon, LINE), 8.4f, 16, 12.8f, 16);

  // three dots in a ring
  icon = icon_with(ICON_MORE, 1.9f);
  add_arc(add_shape(icon, LOOP), 12, 12, 9, 0, 360 - 7.5f, 47);
  for(int i=0; i<3; i++) {
    add_arc(add_shape(icon, FILL), 7.6f + 4.4f * i, 12, 1.35f, 0, 360 - 30, 11);
  }

  // three dots in a row
  icon = icon_with(ICON_DOTS, 0);
  for(int i=0; i<3; i++) {
    add_arc(add_shape(icon, FILL), 5 + 7 * i, 12, 1.9f, 0, 360 - 24, 14);
  }

  // a bin with its lid and handle
  icon = icon_with(ICON_TRASH, 1.8f);
  POINTS(add_shape(icon, LINE), 3.6f, 6.4f, 20.4f, 6.4f);
  POINTS(add_shape(icon, LINE), 9.2f, 6.4f, 9.2f, 3.6f, 14.8f, 3.6f, 14.8f, 6.4f);
  POINTS(add_shape(icon, LOOP), 5.8f, 6.4f, 18.2f, 6.4f, 17, 20.8f, 7, 20.8f);
  POINTS(add_shape(icon, LINE), 10, 10.4f, 10, 16.8f);
  POINTS(add_shape(icon, LINE), 14, 10.4f, 14, 16.8f);

  // a page with three lines, a dot before each
  icon = icon_with(ICON_LIST, 1.9f);
  POINTS(add_shape(icon, LOOP), 3.6f, 3, 20.4f, 3, 20.4f, 21, 3.6f, 21);
  for(int i=0; i<3; i++) {
    add_arc(add_shape(icon, FILL), 7.6f, 7.8f + 4.2f * i, 1.1f, 0, 360 - 30, 11);
    s = add_shape(icon, LINE);
    add_point(s, 10.4f, 7.8f + 4.2f * i);
    add_point(s, 16.8f, 7.8f + 4.2f * i);
  }

  // a padlock: its shackle over its body
  icon = icon_with(ICON_LOCK, 2.0f);
  POINTS(add_shape(icon, LOOP), 5.2f, 10.6f, 18.8f, 10.6f, 18.8f, 20.6f, 5.2f, 20.6f);
  s = add_shape(icon, LINE);
  add_point(s, 8.2f, 10.6f);
  add_arc(s, 12, 7.6f, 3.8f, 180, 360, 12);
  add_point(s, 15.8f, 10.6f);

  // a clock at ten past ten
  icon = icon_with(ICON_CLOCK, 1.9f);
  add_arc(add_shape(icon, LOOP), 12, 12, 9, 0, 360 - 7.5f, 47);
  POINTS(add_shape(icon, LINE), 12, 6.6f, 12, 12, 15.6f, 14.2f);

  // a crescent: a circle with a smaller one taken from its top right
  icon = icon_with(ICON_MOON, 1.9f);
  s = add_shape(icon, LOOP);
  add_arc(s, 12, 12, 8.6f, 7.15f, 262.85f, 30);
  add_arc(s, 16, 8, 6.8f, 221.8f, 48.2f, 20);

  // a controller: a body with its grips, a cross on the left, two buttons
  icon = icon_with(ICON_GAME, 1.8f);
  s = add_shape(icon, LOOP);
  add_arc(s, 16.4f, 13, 5.6f, -90, 90, 14);
  add_arc(s, 7.6f, 13, 5.6f, 90, 270, 14);
  POINTS(add_shape(icon, LINE), 5.4f, 13, 9.8f, 13);
  POINTS(add_shape(icon, LINE), 7.6f, 10.8f, 7.6f, 15.2f);
  add_arc(add_shape(icon, LOOP), 15.4f, 11.6f, 0.9f, 0, 330, 8);
  add_arc(add_shape(icon, LOOP), 18, 14.2f, 0.9f, 0, 330, 8);

  // a DualSense-like controller seen from the front, in layers. Its body:
  // the shoulders, the wide top over the touchpad, the two grips; the left
  // half, then the right one mirrored
  {
    static const float half[] = {
      12, 5.5f, 9, 5.4f, 6.2f, 5.2f, 4.6f, 5.3f, 3.4f, 5.9f, 2.4f, 7, 1.6f, 8.8f,
      1, 11, 0.8f, 13.2f, 1, 15.4f, 1.6f, 17.4f, 2.5f, 18.9f, 3.6f, 19.7f, 4.9f, 19.8f,
      6, 19.3f, 6.9f, 18.2f, 7.8f, 16.9f, 9, 15.9f, 10.5f, 15.4f, 12, 15.3f,
    };
    const int n = sizeof half / sizeof half[0] / 2;

    icon = icon_with(ICON_PAD_BODY, 1);
    s = add_shape(icon, FILL);
    add_points(s, n, half);
    for(int i=n - 2; i > 0; i--) {
      add_point(s, 24 - half[2 * i], half[2 * i + 1]);
    }
  }
  // the black middle: under the touchpad, around the sticks, down to the
  // body's own edge between the grips
  icon = icon_with(ICON_PAD_CORE, 1);
  POINTS(add_shape(icon, FILL), 7.6f, 9, 9.8f, 9.5f, 12, 9.65f, 14.2f, 9.5f, 16.4f, 9,
         17.3f, 11.4f, 17.7f, 14, 16.6f, 16.4f, 16.2f, 16.9f, 15, 15.9f, 13.5f, 15.4f, 12, 15.3f,
         10.5f, 15.4f, 9, 15.9f, 7.8f, 16.9f, 7.4f, 16.4f, 6.3f, 14, 6.7f, 11.4f);
  // the two sticks and the PS button between them
  icon = icon_with(ICON_PAD_STICKS, 1);
  add_arc(add_shape(icon, FILL), 8.8f, 13.1f, 1.6f, 0, 345, 23);
  add_arc(add_shape(icon, FILL), 15.2f, 13.1f, 1.6f, 0, 345, 23);
  add_arc(add_shape(icon, FILL), 12, 14.5f, 0.55f, 0, 330, 11);
  // the D-pad on the left wing, the four face buttons on the right one
  icon = icon_with(ICON_PAD_MARKS, 1);
  POINTS(add_shape(icon, FILL), 4.2f, 9.2f, 5.2f, 9.2f, 5.2f, 10.1f, 6.1f, 10.1f, 6.1f, 11.1f,
         5.2f, 11.1f, 5.2f, 12, 4.2f, 12, 4.2f, 11.1f, 3.3f, 11.1f, 3.3f, 10.1f, 4.2f, 10.1f);
  for(int i=0; i<4; i++) {
    static const float at[4][2] = {{19.3f, 9.2f}, {20.7f, 10.6f}, {19.3f, 12}, {17.9f, 10.6f}};
    add_arc(add_shape(icon, FILL), at[i][0], at[i][1], 0.6f, 0, 330, 11);
  }
  // the light bar, down each side of the touchpad
  icon = icon_with(ICON_PAD_LIGHT, 0.8f);
  POINTS(add_shape(icon, LINE), 7.4f, 6.4f, 7.75f, 8.9f);
  POINTS(add_shape(icon, LINE), 16.6f, 6.4f, 16.25f, 8.9f);

  // a poll: three bars of its results, the middle one ahead
  icon = icon_with(ICON_POLL, 2.1f);
  POINTS(add_shape(icon, LINE), 4.5f, 6.4f, 14.5f, 6.4f);
  POINTS(add_shape(icon, LINE), 4.5f, 12, 19.5f, 12);
  POINTS(add_shape(icon, LINE), 4.5f, 17.6f, 10.5f, 17.6f);

  // a screen with round corners on its stand
  icon = icon_with(ICON_SCREEN, 1.9f);
  s = add_shape(icon, LOOP);
  add_arc(s, 19.2f, 6.2f, 1.8f, -90, 0, 4);
  add_arc(s, 19.2f, 14.6f, 1.8f, 0, 90, 4);
  add_arc(s, 4.8f, 14.6f, 1.8f, 90, 180, 4);
  add_arc(s, 4.8f, 6.2f, 1.8f, 180, 270, 4);
  POINTS(add_shape(icon, LINE), 12, 16.4f, 12, 20.2f);
  POINTS(add_shape(icon, LINE), 8, 20.2f, 16, 20.2f);

  // a line climbing to the right, with its arrow: a post rising
  icon = icon_with(ICON_RISING, 2.1f);
  POINTS(add_shape(icon, LINE), 3, 17.5f, 9.2f, 11.3f, 13.2f, 15.3f, 20.5f, 8);
  POINTS(add_shape(icon, LINE), 14.4f, 8, 20.5f, 8, 20.5f, 14.1f);

  // an eye, two arcs round its pupil, crossed out
  icon = icon_with(ICON_EYE_OFF, 1.9f);
  s = add_shape(icon, LOOP);
  add_arc(s, 12, 19.5f, 12, 218.7f, 321.3f, 16);
  add_arc(s, 12, 4.5f, 12, 38.7f, 141.3f, 16);
  add_arc(add_shape(icon, LOOP), 12, 12, 2.7f, 0, 360 - 15, 23);
  POINTS(add_shape(icon, LINE), 4, 3.5f, 20, 20.5f);

  // a bulb over its base
  icon = icon_with(ICON_LIGHT, 1.9f);
  s = add_shape(icon, LOOP);
  add_arc(s, 12, 9.4f, 6.4f, 135, 405, 30);
  POINTS(s, 14.6f, 16.6f, 9.4f, 16.6f);
  POINTS(add_shape(icon, LINE), 9.4f, 19.4f, 14.6f, 19.4f);
  POINTS(add_shape(icon, LINE), 10.6f, 21.8f, 13.4f, 21.8f);

  // a cup with its handle, and its steam
  icon = icon_with(ICON_CUP, 1.9f);
  POINTS(add_shape(icon, LOOP), 4, 9.5f, 16.5f, 9.5f, 15.4f, 20.5f, 5.1f, 20.5f);
  add_arc(add_shape(icon, LINE), 16.6f, 13.6f, 2.8f, -90, 90, 10);
  POINTS(add_shape(icon, LINE), 8, 6.5f, 8.8f, 4.8f, 8, 3);
  POINTS(add_shape(icon, LINE), 12.4f, 6.5f, 13.2f, 4.8f, 12.4f, 3);

  g_icons_made = 1;
}


static float
segment_distance(float px, float py, float ax, float ay, float bx, float by) {
  float dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
  float t = len > 0 ? clamp01(((px - ax) * dx + (py - ay) * dy) / len) : 0;
  float ex = px - ax - t * dx, ey = py - ay - t * dy;

  return sqrtf(ex * ex + ey * ey);
}


/**
 * distance from (px, py) to the shape's line; for a filled shape, below 0
 * inside it
 **/
static float
shape_distance(const shape_t *s, float px, float py) {
  const float *p = s->xy;
  float d = 1e9f, e;
  int inside = 0, n = s->count;

  for(int i=s->kind == LINE ? 1 : 0, j=s->kind == LINE ? 0 : n - 1; i < n; j = i++) {
    e = segment_distance(px, py, p[2 * j], p[2 * j + 1], p[2 * i], p[2 * i + 1]);
    d = e < d ? e : d;
    if(s->kind == FILL && (p[2 * j + 1] > py) != (p[2 * i + 1] > py) &&
       px < (p[2 * i] - p[2 * j]) * (py - p[2 * j + 1]) / (p[2 * i + 1] - p[2 * j + 1]) + p[2 * j]) {
      inside = !inside;
    }
  }
  return inside ? -d : d;
}


static unsigned char *
make_mask(const icon_t *icon, int size) {
  unsigned char *mask = malloc((size_t)size * size);
  float unit = 24.0f / size, u, v, cover, c;

  if(!mask) {
    return NULL;
  }
  for(int row=0; row < size; row++) {
    for(int col=0; col < size; col++) {
      u = (col + 0.5f) * unit;
      v = (row + 0.5f) * unit;
      cover = 0;
      for(int i=0; i < icon->count; i++) {
        const shape_t *s = &icon->shapes[i];
        if(s->kind == FILL) {
          c = clamp01(0.5f - shape_distance(s, u, v) / unit);
        } else {
          c = clamp01((icon->width / 2 - shape_distance(s, u, v)) / unit + 0.5f);
        }
        cover = c > cover ? c : cover;
      }
      mask[row * size + col] = (unsigned char)(cover * 255 + 0.5f);
    }
  }
  return mask;
}


static const unsigned char *
icon_mask(int icon, int size) {
  mask_t *m;

  for(int i=0; i<MASKS; i++) {
    if(g_masks[i].alpha && g_masks[i].icon == icon && g_masks[i].size == size) {
      return g_masks[i].alpha;
    }
  }
  if(!g_icons_made) {
    make_icons();
  }
  m = &g_masks[g_next_mask];
  g_next_mask = (g_next_mask + 1) % MASKS;
  free(m->alpha);
  m->icon = icon;
  m->size = size;
  m->alpha = make_mask(&g_icons[icon], size);
  return m->alpha;
}


static void
icon_px(const text_surface_t *dst, int icon, int x, int y, int size, uint32_t color) {
  const unsigned char *mask;

  if(icon < 0 || icon >= ICON_COUNT || size <= 0 || size > 256 ||
     !(mask=icon_mask(icon, size))) {
    return;
  }
  for(int row=0; row < size; row++) {
    if(y + row < 0 || y + row >= dst->height) {
      continue;
    }
    uint32_t *line = pixel(dst, 0, y + row);
    for(int col=0; col < size; col++) {
      if(x + col >= 0 && x + col < dst->width && mask[row * size + col]) {
        put(&line[x + col], color, mask[row * size + col] * (color >> 24) / 255);
      }
    }
  }
}


/*
 * The big shapes in parts: a shape's rows split among the workers (see
 * workers.h), each drawing into a band of the screen's rows with the shape
 * moved up by the band's top, which leaves every pixel as one thread would.
 */

enum { JOB_RECT, JOB_BOX, JOB_SHADOW, JOB_RIM, JOB_PICTURE, JOB_PICTURE_ROUND, JOB_SCALED };

typedef struct job {
  int             kind;
  text_surface_t  dst;           // in pixels
  int             x, y, w, h;    // the shape, in pixels
  float           r, t, bottom;  // t: a border, or a shadow's spread
  uint32_t        color;
  const uint32_t *pixels;        // a picture
  int             pw, ph;        // its size, when scaled
  const int      *cols;          // the picture's column for each pixel's, x0 to x1
  int             x0, x1;
  int             top, end;      // the rows of dst it touches
} job_t;


/**
 * a picture of pw x ph scaled into the box: each pixel takes the picture's
 * pixel under its middle
 **/
static void
scaled_px(const text_surface_t *p, const job_t *j, int dy) {
  const int bx = j->x, by = j->y - dy, bw = j->w, bh = j->h;
  const float hw = bw / 2.0f, hh = bh / 2.0f, cx = bx + hw, cy = by + hh;
  const int y0 = by < 0 ? 0 : by, y1 = by + bh > p->height ? p->height : by + bh;
  const int edge = (int)ceilf(j->r) + 1;

  for(int row=y0; row < y1; row++) {
    const int sy = (int)(((long long)(row - by) * 2 + 1) * j->ph / (2LL * bh));
    const uint32_t *src = j->pixels + (size_t)(sy < 0 ? 0 : sy >= j->ph ? j->ph - 1 : sy) * j->pw;
    uint32_t *line = pixel(p, 0, row);
    const int band = row < by + edge || row >= by + bh - edge;
    for(int col=j->x0; col < j->x1; col++) {
      const uint32_t px = src[j->cols[col - j->x0]];
      unsigned a = px >> 24;
      if(band && (col < bx + edge || col >= bx + bw - edge)) {
        const float cover = clamp01(0.5f - box_distance(col + 0.5f - cx, row + 0.5f - cy, hw, hh, j->r));
        a = (unsigned)(a * cover + 0.5f);
      }
      put(&line[col], px, a);
    }
  }
}


/**
 * the job drawn into dst, whose row 0 is the screen's row dy
 **/
static void
job_draw(const job_t *j, const text_surface_t *dst, int dy) {
  const int y = j->y - dy;

  switch(j->kind) {
  case JOB_RECT:
    rect_px(dst, j->x, y, j->w, j->h, j->color);
    break;
  case JOB_BOX:
    box_px_draw(dst, j->x, y, j->w, j->h, j->r, j->t, j->color);
    break;
  case JOB_SHADOW:
    shadow_px(dst, j->x, y, j->w, j->h, j->r, j->t, j->color);
    break;
  case JOB_RIM:
    rim_px(dst, j->x, y, j->w, j->h, j->r, j->t, j->color, j->bottom);
    break;
  case JOB_PICTURE:
    picture_px(dst, j->x, y, j->w, j->h, j->pixels);
    break;
  case JOB_PICTURE_ROUND:
    picture_round_px(dst, j->x, y, j->w, j->h, j->r, j->pixels);
    break;
  case JOB_SCALED:
    scaled_px(dst, j, dy);
    break;
  }
}


static void
job_part(void *arg, int part, int parts) {
  const job_t *j = arg;
  int from, to;

  workers_rows(j->end - j->top, part, parts, &from, &to);
  from += j->top;
  to += j->top;
  if(from < to) {
    const text_surface_t band = {j->dst.pixels + (size_t)from * j->dst.width, j->dst.width,
                                 to - from, 1, 0};
    job_draw(j, &band, from);
  }
}


/**
 * draw the job, which touches the rows top to end and width columns: in
 * parts when that is many pixels
 **/
static void
job_run(job_t *j, int top, int end, int width) {
  j->top = top < 0 ? 0 : top;
  j->end = end > j->dst.height ? j->dst.height : end;
  if(j->end <= j->top || width <= 0) {
    return;
  }
  if((long long)(j->end - j->top) * width >= SHARED_MIN) {
    workers_run(job_part, j);
  } else {
    job_draw(j, &j->dst, 0);
  }
}


/*
 * In points: where a shape falls in pixels, and the shape drawn there.
 */

/**
 * the box of points x, y, w, h in pixels, its edges where its sides' points
 * fall, so boxes side by side stay side by side
 **/
static void
box_px(const text_surface_t *dst, int x, int y, int w, int h, int *bx, int *by, int *bw, int *bh) {
  *bx = text_px(dst, x);
  *by = text_py(dst, y);
  *bw = text_px(dst, x + w) - *bx;
  *bh = text_py(dst, y + h) - *by;
}


/**
 * a job of kind for the box of points x, y, w, h
 **/
static job_t
job_box(const text_surface_t *dst, int kind, int x, int y, int w, int h, uint32_t color) {
  job_t j;

  memset(&j, 0, sizeof j);
  j.kind = kind;
  j.dst = text_pixels(dst);
  j.color = color;
  box_px(dst, x, y, w, h, &j.x, &j.y, &j.w, &j.h);
  return j;
}


void
draw_rect(const text_surface_t *dst, int x, int y, int w, int h, uint32_t color) {
  job_t j = job_box(dst, JOB_RECT, x, y, w, h, color);

  job_run(&j, j.y, j.y + j.h, j.w);
}


void
draw_box(const text_surface_t *dst, int x, int y, int w, int h, float r, float t,
         uint32_t color) {
  job_t j = job_box(dst, JOB_BOX, x, y, w, h, color);

  j.r = r * text_scale(dst);
  j.t = t * text_scale(dst);
  job_run(&j, j.y, j.y + j.h, j.w);
}


void
draw_shadow(const text_surface_t *dst, int x, int y, int w, int h, float r, float spread,
            uint32_t color) {
  job_t j = job_box(dst, JOB_SHADOW, x, y, w, h, color);
  int s;

  j.r = r * text_scale(dst);
  j.t = spread * text_scale(dst);
  s = (int)ceilf(j.t);
  job_run(&j, j.y - s, j.y + j.h + s, j.w + 2 * s);
}


void
draw_rim(const text_surface_t *dst, int x, int y, int w, int h, float r, float t,
         uint32_t color, float bottom) {
  job_t j = job_box(dst, JOB_RIM, x, y, w, h, color);

  j.r = r * text_scale(dst);
  j.t = t * text_scale(dst);
  j.bottom = bottom;
  job_run(&j, j.y, j.y + j.h, j.w);
}


void
draw_corners(const text_surface_t *dst, int x, int y, int w, int h, float r,
             uint32_t color) {
  const text_surface_t p = text_pixels(dst);
  int bx, by, bw, bh;

  box_px(dst, x, y, w, h, &bx, &by, &bw, &bh);
  corners_px(&p, bx, by, bw, bh, r * text_scale(dst), color);
}


/**
 * a picture's job: its box from x, y and its size in pixels
 **/
static job_t
job_picture(const text_surface_t *dst, int kind, int x, int y, int w, int h,
            const uint32_t *pixels) {
  job_t j;

  memset(&j, 0, sizeof j);
  j.kind = kind;
  j.dst = text_pixels(dst);
  j.x = text_px(dst, x);
  j.y = text_py(dst, y);
  j.w = text_size(dst, w);
  j.h = text_size(dst, h);
  j.pixels = pixels;
  return j;
}


void
draw_picture(const text_surface_t *dst, int x, int y, int w, int h,
             const uint32_t *pixels) {
  job_t j = job_picture(dst, JOB_PICTURE, x, y, w, h, pixels);

  job_run(&j, j.y, j.y + j.h, j.w);
}


void
draw_picture_round(const text_surface_t *dst, int x, int y, int w, int h, float r,
                   const uint32_t *pixels) {
  job_t j = job_picture(dst, JOB_PICTURE_ROUND, x, y, w, h, pixels);

  j.r = r * text_scale(dst);
  job_run(&j, j.y, j.y + j.h, j.w);
}


void
draw_picture_scaled(const text_surface_t *dst, int x, int y, int w, int h, float r,
                    const uint32_t *pixels, int pw, int ph) {
  static int cols[4096];
  job_t j = job_picture(dst, JOB_SCALED, x, y, w, h, pixels);

  if(pw == j.w && ph == j.h) {
    j.kind = JOB_PICTURE_ROUND;
    j.r = r * text_scale(dst);
    job_run(&j, j.y, j.y + j.h, j.w);
    return;
  }
  j.x0 = j.x < 0 ? 0 : j.x;
  j.x1 = j.x + j.w > j.dst.width ? j.dst.width : j.x + j.w;
  if(pw <= 0 || ph <= 0 || j.w <= 0 || j.h <= 0 || j.x1 - j.x0 > 4096) {
    return;
  }
  // each pixel's column takes the picture's column under its middle
  for(int col=j.x0; col < j.x1; col++) {
    int sx = (int)(((long long)(col - j.x) * 2 + 1) * pw / (2LL * j.w));
    cols[col - j.x0] = sx < 0 ? 0 : sx >= pw ? pw - 1 : sx;
  }
  j.cols = cols;
  j.pw = pw;
  j.ph = ph;
  j.r = radius(r * text_scale(dst), j.w, j.h);
  job_run(&j, j.y, j.y + j.h, j.x1 - j.x0);
}


void
draw_circle(const text_surface_t *dst, float cx, float cy, float r, uint32_t color) {
  const text_surface_t p = text_pixels(dst);

  circle_px(&p, text_fx(dst, cx), text_fy(dst, cy), r * text_scale(dst), color);
}


void
draw_arc(const text_surface_t *dst, float cx, float cy, float r, float t,
         float from, float to, uint32_t color) {
  const text_surface_t p = text_pixels(dst);
  const float k = text_scale(dst);

  arc_px(&p, text_fx(dst, cx), text_fy(dst, cy), r * k, t * k, from, to, color);
}


void
draw_icon(const text_surface_t *dst, int icon, int x, int y, int size, uint32_t color) {
  const text_surface_t p = text_pixels(dst);

  icon_px(&p, icon, text_px(dst, x), text_py(dst, y), text_size(dst, size), color);
}
