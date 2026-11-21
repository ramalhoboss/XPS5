/* The cards for a post, see cards.h. */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cards.h"
#include "draw.h"
#include "lang.h"

#define RGBA(r, g, b, a) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(g) << 8 | (r))
#define FG       RGBA(248, 245, 242, 255)
#define GREY     RGBA(200, 190, 182, 255)
#define DIM      RGBA(160, 150, 142, 255)
#define ACCENT   RGBA(242, 122, 26, 255)
#define GLASS    RGBA(255, 255, 255, 16)
#define RIM      RGBA(255, 255, 255, 44)
#define TRACK    RGBA(255, 255, 255, 30)

#define L(pt, en) lang_text(look->lang, (pt), (en))

const card_font_spec_t card_font_specs[CARD_FONTS] = {
  [CARD_F_HUGE] = {1, 124}, [CARD_F_BIG] = {1, 58}, [CARD_F_MID] = {1, 36},
  [CARD_F_TEXT] = {0, 31},  [CARD_F_SMALL] = {0, 25}, [CARD_F_LABEL] = {1, 24},
};

// the trophies' grades, bronze to platinum
static const uint32_t g_grade_colour[4] = {
  RGBA(205, 132, 72, 255), RGBA(196, 202, 212, 255), RGBA(236, 192, 64, 255), RGBA(168, 206, 240, 255),
};


text_surface_t
card_surface(uint32_t *pixels) {
  text_surface_t s = {pixels, CARD_W, CARD_H, CARD_SCALE, 0};

  return s;
}


int
card_pixels_w(void) {
  text_surface_t s = card_surface(NULL);

  return text_size(&s, CARD_W);
}


int
card_pixels_h(void) {
  text_surface_t s = card_surface(NULL);

  return text_size(&s, CARD_H);
}


static int
base_of(text_font_t *f, int top, int h) {
  return top + (h - text_line_height(f)) / 2 + text_ascent(f);
}


static int
width(text_font_t *f, const char *s) {
  return text_width(f, s, strlen(s));
}


/**
 * s with the pen at x and the baseline at base, cut with an ellipsis to
 * width; returns the x after it
 **/
static int
fitted(const text_surface_t *dst, text_font_t *f, int x, int base, const char *s, int w, uint32_t colour) {
  static const char ellipsis[] = "\xe2\x80\xa6";
  size_t len = strlen(s), fit;

  if(text_width(f, s, len) <= w) {
    return text_draw(f, dst, x, base, s, len, colour);
  }
  fit = text_fit(f, s, len, w - text_width(f, ellipsis, 3));
  x = text_draw(f, dst, x, base, s, fit, colour);
  return text_draw(f, dst, x, base, ellipsis, 3, colour);
}


/**
 * s in the middle of the card, cut to w
 **/
static void
centred(const text_surface_t *dst, text_font_t *f, int base, const char *s, int w, uint32_t colour) {
  int sw = width(f, s);

  sw = sw < w ? sw : w;
  fitted(dst, f, (CARD_W - sw) / 2, base, s, w, colour);
}


static float
clamp01(float v) {
  return v < 0 ? 0 : v > 1 ? 1 : v;
}


static uint32_t
mix(float r, float g, float b) {
  return RGBA((int)(clamp01(r / 255) * 255 + 0.5f), (int)(clamp01(g / 255) * 255 + 0.5f),
              (int)(clamp01(b / 255) * 255 + 0.5f), 255);
}


/**
 * what the card stands on: the screenshot, darker where the words go (the
 * left, and the bottom), or the app's room, warm under a lamp at the top
 * left; tint, when not 0, a light of that colour from the top middle
 **/
static void
backdrop(const text_surface_t *dst, const card_look_t *look, uint32_t tint) {
  const int pw = card_pixels_w(), ph = card_pixels_h();
  const float tr = (float)(tint & 0xff), tg = (float)(tint >> 8 & 0xff), tb = (float)(tint >> 16 & 0xff);

  if(look->photo) {
    draw_picture(dst, 0, 0, CARD_W, CARD_H, look->photo);
  }
  for(int y=0; y < ph; y++) {
    uint32_t *row = dst->pixels + (size_t)y * (size_t)pw;
    const float vy = (float)y / (float)ph;
    for(int x=0; x < pw; x++) {
      const float vx = (float)x / (float)pw;
      float r, g, b, dx, dy, lamp, glow;
      if(look->photo) {
        const float a = clamp01(0.86f - 0.5f * vx + 0.12f * vy * vy);
        const uint32_t c = row[x];
        r = (float)(c & 0xff) * (1 - a) + 14 * a;
        g = (float)(c >> 8 & 0xff) * (1 - a) + 10 * a;
        b = (float)(c >> 16 & 0xff) * (1 - a) + 8 * a;
      } else {
        dx = vx - 0.12f;
        dy = (vy - 0.02f) * 0.5625f;
        lamp = expf(-(dx * dx + dy * dy) * 5.5f);
        dx = vx - 0.95f;
        dy = (vy - 1.0f) * 0.5625f;
        glow = expf(-(dx * dx + dy * dy) * 9);
        r = 17 + 128 * lamp + 34 * glow;
        g = 12 + 58 * lamp + 18 * glow;
        b = 10 + 20 * lamp + 30 * glow;
      }
      if(tint) {
        dx = vx - 0.5f;
        dy = (vy + 0.05f) * 0.5625f;
        glow = 0.42f * expf(-(dx * dx + dy * dy) * 7);
        r += tr * glow;
        g += tg * glow;
        b += tb * glow;
      }
      row[x] = mix(r, g, b);
    }
  }
}


/**
 * a pane of glass from x, y, w x h
 **/
static void
pane(const text_surface_t *dst, int x, int y, int w, int h) {
  draw_box(dst, x, y, w, h, 40, 0, GLASS);
  draw_box(dst, x, y, w, h, 40, 1.5f, RIM);
}


/**
 * the game's icon, size points at x, y, or a controller when it did not come
 **/
static void
game_icon(const text_surface_t *dst, const uint32_t *icon, int x, int y, int size, float r) {
  if(icon) {
    draw_picture_round(dst, x, y, size, size, r, icon);
  } else {
    draw_box(dst, x, y, size, size, r, 0, RGBA(255, 255, 255, 28));
    draw_icon(dst, ICON_GAME, x + size / 5, y + size / 5, size * 3 / 5, GREY);
  }
  draw_box(dst, x, y, size, size, r, 1.5f, RGBA(255, 255, 255, 60));
}


/**
 * the day of t, as x.com writes a date
 **/
static void
day_of(const card_look_t *look, long long t, char *buf, size_t size) {
  time_t when = (time_t)t;
  const struct tm *tm;

  // gmtime, not gmtime_r, which the title's libc does not have: the cards
  // are drawn by the screen's thread alone, as ui.c's dates are
  buf[0] = 0;
  if(t > 0 && (tm=gmtime(&when))) {
    lang_date(look->lang, tm->tm_mday, tm->tm_mon, tm->tm_year + 1900, buf, size);
  }
}


/**
 * the trophies by grade from x along the baseline base, a coloured trophy
 * and a count each, and the captures; returns the x after them
 **/
static int
counts(const text_surface_t *dst, const card_look_t *look, int x, int base, const int trophies[4], int shots) {
  text_font_t *bold = look->fonts[CARD_F_MID], *f = look->fonts[CARD_F_TEXT];
  static const char *const pt[4] = {"bronze", "prata", "ouro", "platina"};
  static const char *const en[4] = {"bronze", "silver", "gold", "platinum"};
  const int is = 40, top = base - text_ascent(bold) + (text_ascent(bold) - is) / 2 - 2;
  char n[16];

  for(int g=3; g >= 0; g--) {
    if(trophies[g] <= 0) {
      continue;
    }
    draw_icon(dst, ICON_TROPHY, x, top, is, g_grade_colour[g]);
    x += is + 10;
    snprintf(n, sizeof n, "%d", trophies[g]);
    x = text_draw(bold, dst, x, base, n, strlen(n), FG) + 10;
    x = text_draw(f, dst, x, base, L(pt[g], en[g]), strlen(L(pt[g], en[g])), GREY) + 40;
  }
  if(shots > 0) {
    draw_icon(dst, ICON_PHOTO, x, top, is, GREY);
    x += is + 10;
    snprintf(n, sizeof n, "%d", shots);
    x = text_draw(bold, dst, x, base, n, strlen(n), FG) + 10;
    x = text_draw(f, dst, x, base, shots == 1 ? L("captura", "capture") : L("capturas", "captures"),
                  strlen(shots == 1 ? L("captura", "capture") : L("capturas", "captures")), GREY) + 40;
  }
  return x;
}


/**
 * a duration as the cards say it: "2 h 13 min"
 **/
static void
duration(long long seconds, char *buf, size_t size) {
  session_duration(seconds, buf, size);
}


void
card_session(const text_surface_t *dst, const card_look_t *look, const session_t *s) {
  text_font_t *label = look->fonts[CARD_F_LABEL], *big = look->fonts[CARD_F_BIG], *huge = look->fonts[CARD_F_HUGE];
  text_font_t *f = look->fonts[CARD_F_TEXT], *small = look->fonts[CARD_F_SMALL];
  const int left = 100, right = CARD_W - 100, tx = left + CARD_ICON + 40;
  char line[SESSION_BEST + 64], day[64];

  backdrop(dst, look, 0);
  pane(dst, 56, 56, CARD_W - 112, CARD_H - 112);
  game_icon(dst, look->icons[0], left, 100, CARD_ICON, 32);
  text_draw(label, dst, tx, base_of(label, 104, 34), L("SESS\xc3\x83O NA PS5", "SESSION ON PS5"),
            strlen(L("SESS\xc3\x83O NA PS5", "SESSION ON PS5")), ACCENT);
  text_draw(label, dst, right - width(label, "XPS5"), base_of(label, 104, 34), "XPS5", 4, DIM);
  fitted(dst, big, tx, base_of(big, 138, 72), s->name[0] ? s->name : s->id, right - tx, FG);
  day_of(look, s->end, day, sizeof day);
  fitted(dst, small, tx, base_of(small, 212, 36), day, right - tx, GREY);

  duration(s->end - s->start, line, sizeof line);
  text_draw(label, dst, left, base_of(label, 288, 34), L("TEMPO DE JOGO", "TIME PLAYED"),
            strlen(L("TEMPO DE JOGO", "TIME PLAYED")), GREY);
  fitted(dst, huge, left - 4, base_of(huge, 318, 140), line, right - left, FG);

  if(counts(dst, look, left, 512, s->trophies, s->shots) == left) {
    text_draw(f, dst, left, 512, L("Uma sess\xc3\xa3o s\xc3\xb3 de jogo.", "A session of just playing."),
              strlen(L("Uma sess\xc3\xa3o s\xc3\xb3 de jogo.", "A session of just playing.")), GREY);
  }
  if(s->best[0]) {
    draw_icon(dst, ICON_TROPHY, left, 546, 38, g_grade_colour[2]);
    snprintf(line, sizeof line, L("Melhor trof\xc3\xa9u: \xe2\x80\x9c%s\xe2\x80\x9d", "Best trophy: \xe2\x80\x9c%s\xe2\x80\x9d"),
             s->best);
    fitted(dst, f, left + 52, base_of(f, 546, 40), line, right - left - 52, FG);
  }
}


void
card_week(const text_surface_t *dst, const card_look_t *look, const week_t *w) {
  text_font_t *label = look->fonts[CARD_F_LABEL], *big = look->fonts[CARD_F_BIG], *huge = look->fonts[CARD_F_HUGE];
  text_font_t *mid = look->fonts[CARD_F_MID], *f = look->fonts[CARD_F_TEXT], *small = look->fonts[CARD_F_SMALL];
  const int lx = 56, lw = 548, rx = lx + lw + 32, rw = CARD_W - 56 - rx;
  const int all = w->trophies[0] + w->trophies[1] + w->trophies[2] + w->trophies[3];
  const int n = w->count < CARD_GAMES ? w->count : CARD_GAMES;
  long long most = n ? w->games[0].seconds : 0;
  char line[SESSION_NAME + 64], took[32];
  text_font_t *time_font;
  int y, x;

  backdrop(dst, look, 0);
  pane(dst, lx, 56, lw, CARD_H - 112);
  pane(dst, rx, 56, rw, CARD_H - 112);

  x = lx + 44;
  text_draw(label, dst, x, base_of(label, 100, 34), L("A MINHA SEMANA NA PS5", "MY WEEK ON PS5"),
            strlen(L("A MINHA SEMANA NA PS5", "MY WEEK ON PS5")), ACCENT);
  duration(w->seconds, took, sizeof took);
  time_font = width(huge, took) <= lw - 88 ? huge : big;
  // a long one in the smaller letters sits right over the words under it
  fitted(dst, time_font, x - 4, time_font == huge ? base_of(huge, 140, 150) : base_of(big, 154, 88), took,
         lw - 88, FG);
  text_draw(f, dst, x, base_of(f, time_font == huge ? 288 : 246, 40), L("de jogo nos \xc3\xbaltimos 7 dias", "played in the last 7 days"),
            strlen(L("de jogo nos \xc3\xbaltimos 7 dias", "played in the last 7 days")), GREY);
  // the sessions, games and trophies, a number and a word each
  {
    const struct { int n; const char *one, *many; } rows[3] = {
      {w->sessions, L("sess\xc3\xa3o", "session"), L("sess\xc3\xb5" "es", "sessions")},
      {w->count, L("jogo", "game"), L("jogos", "games")},
      {all, L("trof\xc3\xa9u", "trophy"), L("trof\xc3\xa9us", "trophies")},
    };
    y = time_font == huge ? 352 : 330;
    for(int i=0; i<3; i++, y += time_font == huge ? 58 : 70) {
      char num[16];
      snprintf(num, sizeof num, "%d", rows[i].n);
      x = text_draw(mid, dst, lx + 44, base_of(mid, y, 50), num, strlen(num), FG) + 12;
      text_draw(f, dst, x, base_of(mid, y, 50), rows[i].n == 1 ? rows[i].one : rows[i].many,
                strlen(rows[i].n == 1 ? rows[i].one : rows[i].many), GREY);
    }
  }
  text_draw(label, dst, CARD_W - 100 - width(label, "XPS5"), base_of(label, 100, 34), "XPS5", 4, DIM);

  // the most played, each with its icon, its time and a bar of it
  x = rx + 40;
  text_draw(label, dst, x, base_of(label, 100, 34), L("MAIS JOGADOS", "MOST PLAYED"),
            strlen(L("MAIS JOGADOS", "MOST PLAYED")), GREY);
  y = 156;
  for(int i=0; i<n; i++, y += 146) {
    const week_game_t *g = &w->games[i];
    const int nx = x + CARD_ROW_ICON + 26, nw = rx + rw - 40 - nx;
    int bar = most > 0 ? (int)((float)nw * (float)g->seconds / (float)most + 0.5f) : 0;
    game_icon(dst, look->icons[i], x, y, CARD_ROW_ICON, 20);
    fitted(dst, mid, nx, base_of(mid, y - 2, 44), g->name[0] ? g->name : g->id, nw, FG);
    duration(g->seconds, took, sizeof took);
    if(g->trophies > 0) {
      snprintf(line, sizeof line, g->trophies == 1 ? L("%s \xc2\xb7 1 trof\xc3\xa9u", "%s \xc2\xb7 1 trophy") :
               L("%s \xc2\xb7 %d trof\xc3\xa9us", "%s \xc2\xb7 %d trophies"), took, g->trophies);
    } else {
      snprintf(line, sizeof line, "%s", took);
    }
    fitted(dst, small, nx, base_of(small, y + 40, 32), line, nw, GREY);
    draw_box(dst, nx, y + 76, nw, 10, 5, 0, TRACK);
    bar = bar < 10 ? 10 : bar;
    draw_box(dst, nx, y + 76, bar, 10, 5, 0, i ? RGBA(242, 122, 26, 150) : ACCENT);
  }
  if(w->count > n) {
    snprintf(line, sizeof line, w->count - n == 1 ? L("e mais 1 jogo", "and 1 more game") :
             L("e mais %d jogos", "and %d more games"), w->count - n);
    fitted(dst, small, x, base_of(small, CARD_H - 56 - 64, 36), line, rw - 80, DIM);
  }
}


void
card_trophy(const text_surface_t *dst, const card_look_t *look, int grade, const char *game, const char *trophy,
            int earned, int all) {
  text_font_t *label = look->fonts[CARD_F_LABEL], *big = look->fonts[CARD_F_BIG], *f = look->fonts[CARD_F_TEXT];
  text_font_t *small = look->fonts[CARD_F_SMALL];
  const int g = grade < 0 || grade > 3 ? 0 : grade;
  const uint32_t colour = g_grade_colour[g];
  const int ix = (CARD_W - CARD_BIG_ICON) / 2, iy = 70;
  const char *kind = g == 3 ? L("TROF\xc3\x89U DE PLATINA", "PLATINUM TROPHY") :
                     g == 2 ? L("TROF\xc3\x89U DE OURO", "GOLD TROPHY") :
                     g == 1 ? L("TROF\xc3\x89U DE PRATA", "SILVER TROPHY") : L("TROF\xc3\x89U DE BRONZE", "BRONZE TROPHY");
  const char *name = g == 3 ? L("Platina", "Platinum") : g == 2 ? L("Ouro", "Gold") : g == 1 ? L("Prata", "Silver") :
                     L("Bronze", "Bronze");
  char line[160];

  backdrop(dst, look, colour);
  // the icon in a ring of the trophy's colour
  draw_box(dst, ix - 12, iy - 12, CARD_BIG_ICON + 24, CARD_BIG_ICON + 24, 56, 4, colour);
  game_icon(dst, look->icons[0], ix, iy, CARD_BIG_ICON, 44);
  draw_circle(dst, ix + CARD_BIG_ICON + 4, iy + CARD_BIG_ICON + 4, 34, RGBA(28, 24, 22, 255));
  draw_circle(dst, ix + CARD_BIG_ICON + 4, iy + CARD_BIG_ICON + 4, 30, colour);
  draw_icon(dst, ICON_TROPHY, ix + CARD_BIG_ICON + 4 - 19, iy + CARD_BIG_ICON + 4 - 19, 38, RGBA(28, 24, 22, 255));

  centred(dst, label, base_of(label, 352, 36), kind, CARD_W - 160, colour);
  centred(dst, big, base_of(big, 392, 76), trophy && trophy[0] ? trophy : name, CARD_W - 160, FG);
  centred(dst, f, base_of(f, 474, 44), game && game[0] ? game : "", CARD_W - 160, GREY);
  if(all > 0) {
    snprintf(line, sizeof line, L("%d de %d trof\xc3\xa9us", "%d of %d trophies"), earned, all);
    centred(dst, small, base_of(small, 532, 36), line, CARD_W - 160, GREY);
  }
  centred(dst, label, base_of(label, CARD_H - 76, 34), "XPS5", CARD_W, DIM);
}


void
card_voice(const text_surface_t *dst, const card_look_t *look, const char *name, const char *handle,
           const char *game, int seconds, const float *bars, int count) {
  text_font_t *label = look->fonts[CARD_F_LABEL], *big = look->fonts[CARD_F_BIG], *mid = look->fonts[CARD_F_MID];
  text_font_t *f = look->fonts[CARD_F_TEXT];
  const int left = 100, right = CARD_W - 100, tx = left + CARD_VOICE_AVATAR + 36;
  const int wave_top = 286, wave_h = 190, gap = 6;
  char line[160], took[16];
  int x;

  backdrop(dst, look, 0);
  pane(dst, 56, 56, CARD_W - 112, CARD_H - 112);
  // who speaks: their picture, their name and @
  if(look->icons[0]) {
    draw_picture_round(dst, left, 96, CARD_VOICE_AVATAR, CARD_VOICE_AVATAR, CARD_VOICE_AVATAR / 2.0f, look->icons[0]);
  } else {
    draw_circle(dst, left + CARD_VOICE_AVATAR / 2.0f, 96 + CARD_VOICE_AVATAR / 2.0f, CARD_VOICE_AVATAR / 2.0f,
                RGBA(255, 255, 255, 28));
    draw_icon(dst, ICON_PERSON, left + 30, 126, CARD_VOICE_AVATAR - 60, GREY);
  }
  text_draw(label, dst, tx, base_of(label, 100, 34), L("NOTA DE VOZ", "VOICE NOTE"),
            strlen(L("NOTA DE VOZ", "VOICE NOTE")), ACCENT);
  text_draw(label, dst, right - width(label, "XPS5"), base_of(label, 100, 34), "XPS5", 4, DIM);
  fitted(dst, big, tx, base_of(big, 134, 70), name && name[0] ? name : "XPS5", right - tx, FG);
  if(handle && handle[0]) {
    snprintf(line, sizeof line, "@%s", handle);
    fitted(dst, f, tx, base_of(f, 200, 40), line, right - tx, GREY);
  }

  // how it sounds along its length: a bar for each part, round, from the middle
  if(count > 0) {
    const float bw = (float)(right - left - gap * (count - 1)) / (float)count;
    for(int i=0; i < count; i++) {
      const float v = bars ? clamp01(bars[i]) : 0;
      const int h = (int)(10 + v * (wave_h - 10)), bx = left + (int)((bw + gap) * (float)i);
      const uint32_t c = RGBA(242, 122, 26, 130 + (int)(125 * v));
      draw_box(dst, bx, wave_top + (wave_h - h) / 2, (int)bw > 2 ? (int)bw : 2, h, bw / 2, 0, c);
    }
  }

  // how long, and the game it was said over
  snprintf(took, sizeof took, "%d:%02d", seconds / 60, seconds % 60);
  draw_icon(dst, ICON_MIC, left, 516, 44, ACCENT);
  x = text_draw(mid, dst, left + 58, base_of(mid, 512, 52), took, strlen(took), FG) + 44;
  if(game && game[0]) {
    game_icon(dst, look->icons[1], x, 512, 52, 12);
    snprintf(line, sizeof line, L("A jogar %s", "Playing %s"), game);
    fitted(dst, f, x + 70, base_of(f, 512, 52), line, right - x - 70, GREY);
  }
}
