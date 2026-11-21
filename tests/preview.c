/* Draws the title's screen on a computer into PNG files, to see how it
looks without a PS5. The posts and the people in them are made up here, and
the pictures are drawn on the spot; nothing comes from X.
Run with tests/preview.sh <out-dir>. */

#define _DEFAULT_SOURCE

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cards.h"
#include "clip.h"
#include "dictproto.h"
#include "game.h"
#include "images.h"
#include "loginproto.h"
#include "mem.h"
#include "play.h"
#include "seen.h"
#include "sessions.h"
#include "player.h"
#include "space.h"
#include "text.h"
#include "ui.h"
#include "workers.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "fonts.h" // for the made-up login page
#pragma GCC diagnostic pop

#define NOW 1793793600LL // 4 November 2026, 12:00 UTC

// the login window's page, where ui.c puts it (LG_PX, LG_PY, LG_PW, LG_PH), in points
#define LG_PAGE_X 884
#define LG_PAGE_Y 120
#define LG_PAGE_W 728
#define LG_PAGE_H 840

// the screen, at PREVIEW_SCALE pixels a point (1, 1.3333 for 1440p, 2 for 4K)
static uint32_t g_pixels[UI_WIDTH * 2 * UI_HEIGHT * 2];
static float    g_scale = 1;
static int      g_pw = UI_WIDTH, g_ph = UI_HEIGHT;
static unsigned g_frame;


/*
 * Pictures.
 */

static unsigned
hash(const char *s) {
  unsigned h = 2166136261u;

  while(*s) {
    h = (h ^ (unsigned char)*s++) * 16777619u;
  }
  return h;
}


static uint32_t
rgb(float r, float g, float b) {
  r = r < 0 ? 0 : r > 1 ? 1 : r;
  g = g < 0 ? 0 : g > 1 ? 1 : g;
  b = b < 0 ? 0 : b > 1 ? 1 : b;
  return 0xFF000000u | (uint32_t)(b * 255) << 16 | (uint32_t)(g * 255) << 8 | (uint32_t)(r * 255);
}


/**
 * a head and shoulders on a colored ground
 **/
static void
avatar(uint32_t *px, int w, int h, unsigned seed) {
  float hue = (seed % 360) / 360.0f;
  float br = 0.45f + 0.35f * sinf(6.283f * hue), bg = 0.45f + 0.35f * sinf(6.283f * (hue + 0.33f));
  float bb = 0.45f + 0.35f * sinf(6.283f * (hue + 0.66f));

  for(int y=0; y<h; y++) {
    for(int x=0; x<w; x++) {
      float u = (x + 0.5f) / w, v = (y + 0.5f) / h, k = 0.75f + 0.35f * (1 - v);
      float head = hypotf(u - 0.5f, v - 0.4f), body = hypotf((u - 0.5f) * 0.8f, v - 1.05f);
      if(head < 0.19f || body < 0.36f) {
        px[y * w + x] = rgb(0.95f, 0.88f, 0.8f);
      } else {
        px[y * w + x] = rgb(br * k, bg * k, bb * k);
      }
    }
  }
}


/**
 * a sky, a sun, hills and water
 **/
static void
landscape(uint32_t *px, int w, int h, unsigned seed) {
  float sun_x = 0.25f + (seed % 50) / 100.0f, phase = (seed % 97) / 15.0f;
  float warm = (seed >> 8) % 2 ? 1 : 0.4f;

  for(int y=0; y<h; y++) {
    for(int x=0; x<w; x++) {
      float u = (x + 0.5f) / w, v = (y + 0.5f) / h;
      float hill = 0.58f + 0.06f * sinf(u * 7 + phase) + 0.03f * sinf(u * 19 + phase * 2);
      float sun = hypotf((u - sun_x) * w / h, v - 0.38f);
      uint32_t c;
      if(v > 0.74f) {
        float ripple = 0.04f * sinf(v * 180 + u * 6);
        c = rgb(0.10f + 0.3f * warm * (1 - v) + ripple, 0.18f + ripple, 0.32f + 0.2f * (1 - v));
      } else if(v > hill) {
        c = rgb(0.10f + 0.05f * u, 0.22f + 0.1f * (v - hill), 0.14f);
      } else if(sun < 0.07f) {
        c = rgb(1, 0.92f, 0.7f);
      } else {
        float glow = 0.25f / (1 + 12 * sun);
        c = rgb(0.25f + 0.6f * warm * v + glow, 0.35f + 0.3f * v * warm + glow, 0.65f - 0.3f * v * warm + glow);
      }
      px[y * w + x] = c;
    }
  }
}


/**
 * a yellow face on nothing, for any emoji
 **/
static void
emoji(uint32_t *px, int w, int h) {
  for(int y=0; y<h; y++) {
    for(int x=0; x<w; x++) {
      float u = (x + 0.5f) / w - 0.5f, v = (y + 0.5f) / h - 0.5f, r = hypotf(u, v);
      float eye = fminf(hypotf(u + 0.15f, v + 0.1f), hypotf(u - 0.15f, v + 0.1f));
      float mouth = fabsf(hypotf(u, v + 0.05f) - 0.25f);
      if(r > 0.47f) {
        px[y * w + x] = 0;
      } else if(eye < 0.07f || (mouth < 0.04f && v > 0.05f)) {
        px[y * w + x] = rgb(0.4f, 0.25f, 0.1f);
      } else {
        px[y * w + x] = rgb(1, 0.8f, 0.2f);
      }
    }
  }
}


/**
 * a banner of two colours that the seed picks, with soft rings: for the
 * backdrop's light to take its colour
 **/
static void
banner(uint32_t *px, int w, int h, unsigned seed) {
  static const float hues[][2][3] = {
    {{0.10f, 0.30f, 0.70f}, {0.45f, 0.20f, 0.65f}}, // blue into violet
    {{0.80f, 0.20f, 0.35f}, {0.95f, 0.55f, 0.20f}}, // crimson into orange
    {{0.05f, 0.50f, 0.45f}, {0.15f, 0.25f, 0.55f}}, // teal into navy
  };
  const float (*c)[3] = hues[seed % 3];

  for(int y=0; y<h; y++) {
    for(int x=0; x<w; x++) {
      float u = (x + 0.5f) / w, v = (y + 0.5f) / h, k = 0.7f * u + 0.3f * v;
      float ring = 0.06f * sinf(hypotf((u - 0.7f) * w / h, v - 0.4f) * 18);
      px[y * w + x] = rgb(c[0][0] + (c[1][0] - c[0][0]) * k + ring, c[0][1] + (c[1][1] - c[0][1]) * k + ring,
                          c[0][2] + (c[1][2] - c[0][2]) * k + ring);
    }
  }
}


/**
 * when set, what draws the pictures it knows (tests/promo.c's own), NULL
 * for the others
 **/
static uint32_t *(*g_picture_hook)(const char *url, int w, int h);


static uint32_t *
picture(const char *url, int w, int h) {
  uint32_t *px = g_picture_hook ? g_picture_hook(url, w, h) : NULL;

  if(px) {
    return px;
  }
  px = mem_alloc((size_t)w * h * sizeof *px);
  if(px && strstr(url, "/emoji/")) {
    emoji(px, w, h);
  } else if(px && strstr(url, "profile_banners")) {
    banner(px, w, h, hash(url));
  } else if(px && strstr(url, "profile_images")) {
    avatar(px, w, h, hash(url));
  } else if(px) {
    landscape(px, w, h, hash(url));
  }
  return px;
}


/**
 * what the title's loader threads do, with the pictures made here
 **/
static void
load_pictures(void) {
  char url[IMAGES_URL_MAX];
  int w, h;

  while(images_next(url, sizeof url, &w, &h)) {
    images_done_pixels(url, w, h, picture(url, w, h));
  }
  if(images_arrived()) {
    ui_pictures_arrived();
  }
}


/*
 * The video player, with a still picture of the video instead of FFmpeg.
 */

static char      g_video[X_VIDEO_URL];
static int       g_video_w, g_video_h, g_paused, g_sound, g_video_changed;
static uint32_t *g_still;

void
player_init(void (*log)(const char *fmt, ...)) {
  (void)log;
}


void
player_open(const char *url, int w, int h, int sound, int len_ms) {
  (void)len_ms;
  snprintf(g_video, sizeof g_video, "%s", url);
  g_video_w = w;
  g_video_h = h;
  g_sound = sound;
  g_paused = 0;
  mem_free(g_still);
  g_still = picture(url, w, h);
  g_video_changed = 1;
}


void
player_close(void) {
  g_video[0] = 0;
  mem_free(g_still);
  g_still = NULL;
}


int
player_is(const char *url) {
  return g_video[0] && !strcmp(url, g_video);
}


void
player_resize(int w, int h) {
  if(g_video[0] && (w != g_video_w || h != g_video_h)) {
    g_video_w = w;
    g_video_h = h;
    mem_free(g_still);
    g_still = picture(g_video, w, h);
  }
}


void
player_pause(int paused) {
  g_paused = paused;
}


void
player_seek(int delta_ms) {
  (void)delta_ms;
}


void
player_sound(int on) {
  g_sound = on;
}


void
player_speaker(int on) {
  (void)on;
}


void
player_status(player_status_t *st) {
  memset(st, 0, sizeof *st);
  st->state = !g_video[0] ? PLAYER_IDLE : g_paused ? PLAYER_PAUSED : PLAYER_PLAYING;
  st->pos_ms = 12000;
  st->len_ms = 45000;
  st->sound = g_sound;
  st->has_audio = 1;
}


const uint32_t *
player_frame(int *w, int *h) {
  *w = g_video_w;
  *h = g_video_h;
  return g_still;
}


void
player_frame_done(void) {
}


int
player_changed(void) {
  int changed = g_video_changed;

  g_video_changed = 0;
  return changed;
}


/*
 * A Space in the background (space.h): main.c plays it; here it is on the
 * air at once, 1 min 23 s in, until it is stopped.
 */

static int g_space_state, g_space_live;

void
space_init(void (*log)(const char *fmt, ...), int (*ask)(const char *cmd, char *answer, size_t size)) {
  (void)log;
  (void)ask;
}


void
space_play(const char *url, int live, const char *id, const char *host) {
  (void)id;
  (void)host;
  g_space_state = strncmp(url, "https://", 8) ? SPACE_FAILED : SPACE_ON;
  g_space_live = live;
}


void
space_stop(void) {
  g_space_state = SPACE_IDLE;
}


void
space_hush(int on) {
  (void)on;
}


void
space_status(space_status_t *s) {
  memset(s, 0, sizeof *s);
  s->state = g_space_state;
  s->heard_ms = g_space_state == SPACE_ON ? 83000 : 0;
}


int
space_found(x_space_t *s) {
  (void)s;
  return 0;
}


/*
 * Posts.
 */

static x_post_t *
post(x_post_t *p, const char *name, const char *user, int badge, long long ago,
     const char *text, long long replies, long long reposts, long long likes, long long views) {
  static int ids;

  memset(p, 0, sizeof *p);
  snprintf(p->id, sizeof p->id, "%d", 3000 + ids++);
  snprintf(p->name, sizeof p->name, "%s", name);
  snprintf(p->user, sizeof p->user, "%s", user);
  snprintf(p->user_id, sizeof p->user_id, "%u", 1000 + hash(user) % 9000);
  snprintf(p->avatar, sizeof p->avatar, "https://pbs.twimg.com/profile_images/1/%s_normal.jpg", user);
  snprintf(p->text, sizeof p->text, "%s", text);
  p->badge = badge;
  p->time = NOW - ago;
  p->replies = replies;
  p->reposts = reposts;
  p->likes = likes;
  p->views = views;
  return p;
}


static void
media(x_post_t *p, const char *name, int w, int h, int video) {
  x_media_t *m = &p->media[p->media_count++];

  snprintf(m->url, sizeof m->url, "https://pbs.twimg.com/media/%s.jpg", name);
  m->width = w;
  m->height = h;
  m->video = video;
  if(video) {
    snprintf(m->video_url, sizeof m->video_url,
             "https://video.twimg.com/ext_tw_video/1/pu/vid/%dx%d/%s.mp4", w, h, name);
    snprintf(m->video_small, sizeof m->video_small,
             "https://video.twimg.com/ext_tw_video/1/pu/vid/480x270/%s.mp4", name);
    m->duration_ms = 45000;
  }
}


/**
 * the PS5's captures the gallery shows, newest first
 **/
static int
make_captures(capture_t *list) {
  static const struct { const char *path; int kind; long long size; } made[] = {
    {"/user/av_contents/photo/NPXS40087/PPSA21000/001/Astro Bot_20261104_114210.jpg", CAPTURE_JPEG, 2400000},
    {"/user/av_contents/video/NPXS40087/PPSA21000/001/Astro Bot_20261104_113501.mp4", CAPTURE_MP4, 48200000},
    {"/user/av_contents/photo/NPXS40087/PPSA01316/001/Gran Turismo 7_20261103_221530.png", CAPTURE_PNG, 9800000},
    {"/user/av_contents/photo/NPXS40087/PPSA01316/001/Gran Turismo 7_20261103_221102.jpg", CAPTURE_JPEG, 1900000},
    {"/user/av_contents/video/NPXS40087/PPSA01316/001/Gran Turismo 7_20261103_215840.webm", CAPTURE_WEBM, 31000000},
    {"/user/av_contents/photo/NPXS40087/PPSA01284/001/Returnal_20261102_200012.jpg", CAPTURE_JPEG, 2100000},
    {"/user/av_contents/photo/NPXS40087/PPSA01284/001/Returnal_20261102_195845.jpg", CAPTURE_JPEG, 2050000},
    {"/user/av_contents/video/NPXS40087/PPSA01284/001/Returnal_20261102_195010.mp4", CAPTURE_MP4, 120400000},
    {"/user/av_contents/photo/NPXS40087/PPSA21000/002/Astro Bot_20261101_181500.jpg", CAPTURE_JPEG, 2600000},
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


static int
make_posts(x_post_t *posts) {
  x_post_t *p;
  int n = 0;

  p = post(&posts[n++], "Rita Sousa", "ritasousa", X_BADGE_BLUE, 12 * 60,
           "Finalmente acabei o jogo! \xf0\x9f\x8e\xae Depois de 60 horas, o final valeu cada minuto. "
           "Algu\xc3\xa9m mais ficou a olhar para os cr\xc3\xa9" "ditos sem conseguir largar o comando?",
           48, 12, 1250, 15342);
  p->liked = 1;
  media(p, "fim-do-jogo", 1600, 900, 0);

  p = post(&posts[n++], "Clube de Fotografia do Porto", "fotoporto", X_BADGE_GOLD, 3 * 3600,
           "P\xc3\xb4r do sol na Ribeira ontem \xc3\xa0 noite. Obrigado a todos os que vieram ao passeio!",
           32, 210, 3400, 98000);
  snprintf(p->reposter, sizeof p->reposter, "hugoreis");
  snprintf(p->reposter_name, sizeof p->reposter_name, "Hugo Reis");
  media(p, "ribeira-1", 1200, 800, 0);
  media(p, "ribeira-2", 1200, 800, 0);
  media(p, "ribeira-3", 1200, 800, 0);
  media(p, "ribeira-4", 1200, 800, 0);

  p = post(&posts[n++], "Tiago Martins", "tiagom", X_BADGE_NONE, 5 * 3600,
           "Isto explica tudo \xf0\x9f\x91\x87", 5, 2, 87, 2100);
  snprintf(p->playing, sizeof p->playing, "EA SPORTS FC\xe2\x84\xa2 26");
  snprintf(p->quote_user, sizeof p->quote_user, "inescarv");
  snprintf(p->quote_name, sizeof p->quote_name, "In\xc3\xaas Carvalho");
  snprintf(p->quote_avatar, sizeof p->quote_avatar, "https://pbs.twimg.com/profile_images/2/inescarv_normal.jpg");
  p->quote_badge = X_BADGE_BLUE;
  snprintf(p->quote_text, sizeof p->quote_text,
           "Dica: na PS5 podes manter o bot\xc3\xa3o PS carregado para ir direto ao centro de "
           "controlo. Poupa imenso tempo quando est\xc3\xa1s a meio de um jogo.");
  // a Community Note under it, as x.com shows one
  snprintf(p->note, sizeof p->note, "Manter o bot\xc3\xa3o PS carregado abre o menu inicial. O centro de "
           "controlo abre com um toque curto. https://t.co/ps5ajuda");

  p = post(&posts[n++], "Ana Lopes \xf0\x9f\x90\xb1", "analopes", X_BADGE_NONE, 26 * 3600,
           "O meu gato descobriu o comando \xf0\x9f\x98\x82", 120, 940, 15200, 410000);
  p->reposted = 1;
  media(p, "gato", 1920, 1080, 1);

  post(&posts[n++], "Not\xc3\xad" "cias de Jogos", "noticiasjogos", X_BADGE_GREY, 2 * 86400,
       "O que muda na atualiza\xc3\xa7\xc3\xa3o desta semana:\n\n"
       "\xe2\x80\xa2 Novo modo de foto com controlo de profundidade de campo\n"
       "\xe2\x80\xa2 Menos tempo de carregamento entre zonas do mapa\n"
       "\xe2\x80\xa2 Corre\xc3\xa7\xc3\xb5" "es no modo cooperativo, que \xc3\xa0s vezes perdia a liga\xc3\xa7\xc3\xa3o\n"
       "\xe2\x80\xa2 Legendas maiores e com fundo opcional\n"
       "\xe2\x80\xa2 Tr\xc3\xaas trajes novos para quem acabou a campanha\n\n"
       "A atualiza\xc3\xa7\xc3\xa3o tem cerca de 4 GB e instala-se sozinha se a consola estiver em "
       "modo de repouso com a internet ligada. Quem jogar no modo de desempenho vai notar a "
       "maior diferen\xc3\xa7" "a, sobretudo nas zonas com muita vegeta\xc3\xa7\xc3\xa3o, onde a imagem "
       "deixa de tremer. A equipa diz que h\xc3\xa1 mais novidades a caminho antes do fim do ano.",
       64, 310, 2900, 120500);

  post(&posts[n++], "Marta Ferreira", "martaf", X_BADGE_NONE, 40 * 86400,
       "Algu\xc3\xa9m quer jogar logo \xc3\xa0 noite? Tenho o fim de semana livre.", 3, 0, 9, 0);
  return n;
}


/**
 * a notification: who it is from, what about, and the post it is about
 **/
static x_post_t *
notice(x_post_t *p, int icon, long long ago, const char *text, const char *about,
       const char *const *users, int count) {
  memset(p, 0, sizeof *p);
  p->kind = X_KIND_NOTICE;
  p->icon = icon;
  p->time = NOW - ago;
  p->replies = p->reposts = p->likes = p->views = -1;
  snprintf(p->text, sizeof p->text, "%s", text);
  for(int i=0; i < count && i < X_MAX_FACES; i++) {
    snprintf(p->faces[i], sizeof p->faces[i], "https://pbs.twimg.com/profile_images/1/%s_normal.jpg", users[i]);
  }
  p->face_count = count;
  snprintf(p->user, sizeof p->user, "%s", users[0]);
  snprintf(p->name, sizeof p->name, "%s", users[0]);
  snprintf(p->user_id, sizeof p->user_id, "%u", 1000 + hash(users[0]) % 9000);
  snprintf(p->avatar, sizeof p->avatar, "%s", p->faces[0]);
  if(about) {
    snprintf(p->id, sizeof p->id, "5001");
    snprintf(p->quote_user, sizeof p->quote_user, "eu");
    snprintf(p->quote_text, sizeof p->quote_text, "%s", about);
  }
  return p;
}


static int
make_notices(x_post_t *posts) {
  static const char *const likes[] = {"analopes", "hugoreis", "martaf"};
  static const char *const follow[] = {"joaomatos"};
  static const char *const reposts[] = {"tiagom", "ritasousa"};
  static const char about[] = "A minha nova sala de jogos est\xc3\xa1 finalmente pronta \xf0\x9f\x8e\xae";
  int n = 0;

  notice(&posts[n++], X_ICON_LIKE, 25 * 60, "Ana Lopes, Hugo Reis e 1 outra pessoa gostaram do teu post",
         about, likes, 3);
  notice(&posts[n++], X_ICON_FOLLOW, 3 * 3600, "Jo\xc3\xa3o Matos come\xc3\xa7ou a seguir-te", NULL, follow, 1);
  notice(&posts[n++], X_ICON_REPOST, 8 * 3600, "Tiago Martins e Rita Sousa repostaram o teu post",
         about, reposts, 2);
  post(&posts[n++], "In\xc3\xaas Carvalho", "inescarv", X_BADGE_BLUE, 9 * 3600,
       "@eu Ficou espetacular! Onde compraste a estante?", 1, 0, 4, 120);
  return n;
}


/**
 * what a search for "ps5 pro" finds, the second post bookmarked already
 **/
static int
make_search(x_post_t *posts) {
  x_post_t *p;
  int n = 0;

  p = post(&posts[n++], "Hugo Reis", "hugoreis", X_BADGE_NONE, 40 * 60,
           "A PS5 Pro chegou hoje! A diferen\xc3\xa7" "a nos jogos com ray tracing \xc3\xa9 enorme \xf0\x9f\x94\xa5",
           34, 12, 410, 8800);
  media(p, "ps5pro", 1600, 900, 0);
  p = post(&posts[n++], "Marta Ferreira", "martaf", X_BADGE_BLUE, 5 * 3600,
           "Vale a pena trocar a PS5 normal pela Pro? Digam-me o que acham, estou indecisa.",
           88, 3, 120, 5400);
  p->bookmarked = 1;
  post(&posts[n++], "Loja Jogos", "lojajogos", X_BADGE_GOLD, 26 * 3600,
       "PS5 Pro em stock outra vez, com entrega amanh\xc3\xa3 em Lisboa e no Porto.", 5, 20, 64, 31000);
  return n;
}


static x_post_t *
trend(x_post_t *p, int rank, const char *where, const char *name, const char *query, const char *posts) {
  memset(p, 0, sizeof *p);
  p->kind = X_KIND_TREND;
  p->icon = rank;
  snprintf(p->quote_name, sizeof p->quote_name, "%s", where);
  snprintf(p->name, sizeof p->name, "%s", name);
  snprintf(p->text, sizeof p->text, "%s", query);
  snprintf(p->quote_text, sizeof p->quote_text, "%s", posts);
  p->replies = p->reposts = p->likes = p->views = -1;
  return p;
}


/**
 * Explore's trends, as GenericTimelineById gives them
 **/
static int
make_trends(x_post_t *posts) {
  int n = 0;

  trend(&posts[n++], 1, "Tend\xc3\xaa" "ncia em Portugal", "#PS5Pro", "#PS5Pro", "12,3 mil posts");
  trend(&posts[n++], 2, "Jogos \xc2\xb7 Tend\xc3\xaa" "ncia", "GTA VI", "\"GTA VI\"", "48,1 mil posts");
  trend(&posts[n++], 3, "Desporto \xc2\xb7 Tend\xc3\xaa" "ncia", "Benfica", "Benfica", "21 mil posts");
  trend(&posts[n++], 4, "Tend\xc3\xaa" "ncia em Portugal", "#StateOfPlay", "#StateOfPlay", "5302 posts");
  trend(&posts[n++], 5, "M\xc3\xbasica \xc2\xb7 Tend\xc3\xaa" "ncia", "Taylor Swift", "\"Taylor Swift\"",
        "102 mil posts");
  trend(&posts[n++], 6, "Tecnologia \xc2\xb7 Tend\xc3\xaa" "ncia", "Grok 5", "\"Grok 5\"", "9870 posts");
  return n;
}


/**
 * who to follow, as ConnectTabTimeline gives them
 **/
static int
make_people(x_post_t *posts) {
  static const struct { const char *user, *name; int badge, following; } made[] = {
    {"PlayStation", "PlayStation", X_BADGE_GOLD, 0},
    {"insomniacgames", "Insomniac Games", X_BADGE_GOLD, 0},
    {"anaplays", "Ana \xc2\xb7 speedruns e caf\xc3\xa9 com um nome bem comprido", X_BADGE_BLUE, 1},
  };
  int n = 0;

  for(size_t i=0; i < sizeof made / sizeof made[0]; i++, n++) {
    x_post_t *p = &posts[n];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_USER;
    snprintf(p->user_id, sizeof p->user_id, "%u", 5000 + (unsigned)i);
    snprintf(p->user, sizeof p->user, "%s", made[i].user);
    snprintf(p->name, sizeof p->name, "%s", made[i].name);
    snprintf(p->avatar, sizeof p->avatar, "https://pbs.twimg.com/profile_images/9/%s_normal.jpg", made[i].user);
    p->badge = made[i].badge;
    p->following = made[i].following;
  }
  return n;
}


/**
 * the people the user follows, as Following gives them: two play now, and
 * one is on XPS5 with no game
 **/
static int
make_friends(x_post_t *posts) {
  static const struct { const char *user, *name, *game; int badge; } made[] = {
    {"joaomarques", "Jo\xc3\xa3o Marques", "ASTRO BOT", X_BADGE_BLUE},
    {"PlayStation", "PlayStation", "", X_BADGE_GOLD},
    {"anaplays", "Ana \xc2\xb7 speedruns e caf\xc3\xa9 com um nome bem comprido",
     "Marvel's Spider-Man\xe2\x80\xa6", X_BADGE_BLUE},
    {"pedrosilva", "Pedro Silva", "", X_BADGE_NONE},
    {"martaf", "Marta Ferreira", "", X_BADGE_NONE},
  };
  int n = 0;

  for(size_t i=0; i < sizeof made / sizeof made[0]; i++, n++) {
    x_post_t *p = &posts[n];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_USER;
    snprintf(p->user_id, sizeof p->user_id, "%u", 1000 + hash(made[i].user) % 9000);
    snprintf(p->user, sizeof p->user, "%s", made[i].user);
    snprintf(p->name, sizeof p->name, "%s", made[i].name);
    snprintf(p->avatar, sizeof p->avatar, "https://pbs.twimg.com/profile_images/1/%s_normal.jpg", made[i].user);
    snprintf(p->playing, sizeof p->playing, "%s", made[i].game);
    p->online = !strcmp(made[i].user, "pedrosilva");
    p->badge = made[i].badge;
    p->following = 1;
  }
  return n;
}


/**
 * Explore's news: stories with their pictures
 **/
static int
make_news(x_post_t *posts) {
  x_post_t *p;
  int n = 0;

  p = trend(&posts[n++], 0, "Not\xc3\xad" "cias \xc2\xb7 h\xc3\xa1 2 horas",
            "Sony anuncia um State of Play para esta semana, com novidades de GTA VI e de exclusivos da PS5",
            "State of Play", "18 mil posts");
  p->group = 1;
  snprintf(p->avatar, sizeof p->avatar, "https://pbs.twimg.com/media/news1.jpg");
  p = trend(&posts[n++], 0, "Desporto \xc2\xb7 h\xc3\xa1 5 horas",
            "Benfica vence no Drag\xc3\xa3o e volta a liderar o campeonato", "Benfica", "33 mil posts");
  p->group = 1;
  snprintf(p->avatar, sizeof p->avatar, "https://pbs.twimg.com/media/news2.jpg");
  p = trend(&posts[n++], 0, "Tecnologia \xc2\xb7 ontem", "A nova vers\xc3\xa3o do Grok chega a todos no X",
            "Grok", "7,4 mil posts");
  p->group = 1;
  return n;
}


/**
 * two posts of someone else's with polls: one open, one closed that the
 * user voted in
 **/
static int
make_polls(x_post_t *posts, const char *uid, const char *user, const char *name) {
  static const struct {
    const char *id, *text, *labels[3];
    long long votes[3], ago, ends;
    int final, voted;
  } made[] = {
    {"3101", "Que jogo querem ver na live de s\xc3\xa1" "bado?",
     {"Astro Bot", "Gran Turismo 7", "Ghost of Y\xc5\x8dtei"}, {120, 340, 95}, 3600, 2 * 86400 + 5 * 3600, 0, 0},
    {"3102", "O melhor comando de sempre \xc3\xa9\xe2\x80\xa6", {"DualSense", "DualShock 4", NULL}, {820, 410, 0},
     3 * 86400, -3600, 1, 1},
  };
  int n = 0;

  for(size_t i=0; i < sizeof made / sizeof made[0]; i++) {
    x_post_t *p = &posts[n++];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_POST;
    snprintf(p->id, sizeof p->id, "%s", made[i].id);
    snprintf(p->user_id, sizeof p->user_id, "%s", uid);
    snprintf(p->user, sizeof p->user, "%s", user);
    snprintf(p->name, sizeof p->name, "%s", name);
    snprintf(p->text, sizeof p->text, "%s", made[i].text);
    p->time = NOW - made[i].ago;
    p->replies = 14;
    p->reposts = 3;
    p->likes = 88;
    p->views = 5100;
    p->bookmarks = -1;
    for(int k=0; k < 3 && made[i].labels[k]; k++) {
      snprintf(p->poll_label[k], sizeof p->poll_label[k], "%s", made[i].labels[k]);
      p->poll_votes[k] = made[i].votes[k];
      p->poll_count = k + 1;
    }
    p->poll_end = NOW + made[i].ends;
    p->poll_final = made[i].final;
    p->poll_voted = made[i].voted;
    snprintf(p->poll_card, sizeof p->poll_card, "card://19000000000000000%s", made[i].id + 2);
    snprintf(p->poll_name, sizeof p->poll_name, "poll%dchoice_text_only", p->poll_count);
  }
  return n;
}


/**
 * the user's scheduled posts, as FetchScheduledTweets gives them: one in
 * 40 minutes, one tomorrow, one in five days and one X could not send
 **/
static int
make_scheduled(x_post_t *posts) {
  static const struct {
    const char *id, *text;
    long long in;
    int failed;
  } made[] = {
    {"1900000000000000001", "Hoje \xc3\xa0 noite h\xc3\xa1 torneio de Gran Turismo 7: quem entra? #GT7", 40 * 60, 0},
    {"1900000000000000002", "Bom dia! O novo patch chega amanh\xc3\xa3 \xc3\xa0s 9h, com fotos da pista nova.", 20 * 3600, 0},
    {"1900000000000000003", "Lembrete: a live de s\xc3\xa1" "bado come\xc3\xa7" "a \xc3\xa0s 21h.", 5 * 86400 + 3600, 0},
    {"1900000000000000004", "Fotos do fim de semana na pista", -3600, 1},
  };
  int n = 0;

  for(size_t i=0; i < sizeof made / sizeof made[0]; i++) {
    x_post_t *p = &posts[n++];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_SCHEDULED;
    snprintf(p->id, sizeof p->id, "%s", made[i].id);
    snprintf(p->text, sizeof p->text, "%s", made[i].text);
    p->time = NOW + made[i].in;
    p->icon = made[i].failed;
  }
  return n;
}


/**
 * the user's lists, as ListsManagementPageTimeline gives them
 **/
static int
make_lists(x_post_t *posts) {
  static const struct {
    const char *id, *name, *about, *user, *owner, *banner;
    long long members, followers;
    int private, following, pinned, badge;
  } made[] = {
    {"1500", "Jogos PS5", "Est\xc3\xba" "dios, lan\xc3\xa7" "amentos e quem joga bem.", "ritasousa", "Rita Sousa",
     "https://pbs.twimg.com/list_banner_img/1/jogos", 42, 7, 1, 0, 1, X_BADGE_BLUE},
    {"1600", "Not\xc3\xad" "cias de tecnologia", "As melhores fontes de tecnologia em portugu\xc3\xaas.", "techpt",
     "Tech PT", "https://pbs.twimg.com/media/listtech.jpg", 120, 3400, 0, 1, 0, X_BADGE_GOLD},
    {"1700", "Amigos", "", "ritasousa", "Rita Sousa", "", 1, 0, 1, 0, 0, X_BADGE_BLUE},
  };
  int n = 0;

  for(size_t i=0; i < sizeof made / sizeof made[0]; i++) {
    x_post_t *p = &posts[n++];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_LIST;
    snprintf(p->id, sizeof p->id, "%s", made[i].id);
    snprintf(p->name, sizeof p->name, "%s", made[i].name);
    snprintf(p->quote_text, sizeof p->quote_text, "%s", made[i].about);
    snprintf(p->user, sizeof p->user, "%s", made[i].user);
    snprintf(p->quote_name, sizeof p->quote_name, "%s", made[i].owner);
    snprintf(p->quote_avatar, sizeof p->quote_avatar, "https://pbs.twimg.com/profile_images/1/%s_normal.jpg",
             made[i].user);
    snprintf(p->avatar, sizeof p->avatar, "%s", made[i].banner);
    p->likes = made[i].members;
    p->reposts = made[i].followers;
    p->request = made[i].private;
    p->following = made[i].following;
    p->pinned = made[i].pinned;
    p->badge = made[i].badge;
    p->replies = p->views = -1;
  }
  return n;
}


/**
 * a post and the replies to it, the way TweetDetail gives them
 **/
static int
make_thread(x_post_t *posts, const x_post_t *focal) {
  x_post_t *p;
  int n = 0;

  posts[n++] = *focal;
  post(&posts[n++], "Hugo Reis", "hugoreis", X_BADGE_NONE, 10 * 60,
       "@ritasousa Eu tamb\xc3\xa9m! O final apanhou-me completamente de surpresa.", 2, 0, 41, 900);
  p = post(&posts[n++], "Marta Ferreira", "martaf", X_BADGE_NONE, 8 * 60,
           "@ritasousa Ainda vou a meio, sem spoilers por favor \xf0\x9f\x99\x88", 1, 0, 18, 400);
  media(p, "meio-do-jogo", 1280, 720, 0);
  post(&posts[n++], "Tiago Martins", "tiagom", X_BADGE_NONE, 5 * 60,
       "@ritasousa Qual foi a tua parte preferida?", 0, 0, 3, 120);
  return n;
}


/**
 * the posts of a profile, the pinned one first
 **/
static int
make_profile(x_post_t *posts, x_user_t *u) {
  x_post_t *p;
  int n = 0;

  memset(u, 0, sizeof *u);
  snprintf(u->id, sizeof u->id, "%u", 1000 + hash("ritasousa") % 9000);
  snprintf(u->user, sizeof u->user, "ritasousa");
  snprintf(u->name, sizeof u->name, "Rita Sousa");
  snprintf(u->avatar, sizeof u->avatar, "https://pbs.twimg.com/profile_images/1/ritasousa_normal.jpg");
  snprintf(u->banner, sizeof u->banner, "https://pbs.twimg.com/profile_banners/1/ritasousa");
  u->badge = X_BADGE_BLUE;
  snprintf(u->bio, sizeof u->bio, "Jogos, fotografia e caf\xc3\xa9 \xe2\x98\x95 A jogar tudo o que tenha "
           "uma boa hist\xc3\xb3ria. Streams \xc3\xa0s sextas em rita.pt");
  snprintf(u->location, sizeof u->location, "Porto, Portugal");
  u->following = 312;
  u->followers = 4580;
  u->posts = 2210;
  u->joined = 1243973549LL; // June 2009

  p = post(&posts[n++], "Rita Sousa", "ritasousa", X_BADGE_BLUE, 90 * 86400,
           "Bem-vindos! Aqui falo de jogos, fotografia e de tudo o que me apetecer \xf0\x9f\x98\x84", 12, 30, 640, 22000);
  p->pinned = 1;
  media(p, "estudio", 1600, 900, 0);
  p = post(&posts[n++], "Rita Sousa", "ritasousa", X_BADGE_BLUE, 12 * 60,
           "Finalmente acabei o jogo! \xf0\x9f\x8e\xae", 48, 12, 1250, 15342);
  post(&posts[n++], "Rita Sousa", "ritasousa", X_BADGE_BLUE, 2 * 86400,
       "Hoje h\xc3\xa1 stream a partir das 21h.", 4, 1, 77, 3100);
  return n;
}


/*
 * Frames.
 */

/**
 * a frame, as the title draws one each time: what the pointer clicks is
 * what the last frame showed
 **/
static void
frame(void) {
  text_surface_t screen = {g_pixels, UI_WIDTH, UI_HEIGHT, g_scale, 0};

  for(int i=0; i<100 && ui_animate(); i++) {
  }
  ui_draw(&screen, NOW, g_frame++);
}


/**
 * the pictures asked for, and the frames after them, as on the PS5 a moment
 * later: the backdrop's light in the colour of a banner that came
 **/
static void
settle(void) {
  frame();
  load_pictures();
  frame();
}


static int  card_drawn(const char *dir, const char *name);
static void card_made(int n);


static int
draw(const char *dir, const char *name) {
  text_surface_t screen = {g_pixels, UI_WIDTH, UI_HEIGHT, g_scale, 0};
  char path[512];

  frame();
  load_pictures();
  ui_draw(&screen, NOW, g_frame++);
  snprintf(path, sizeof path, "%s/%s", dir, name);
  if(!stbi_write_png(path, g_pw, g_ph, 4, g_pixels, g_pw * 4)) {
    fprintf(stderr, "cannot write %s\n", path);
    return -1;
  }
  printf("%s\n", path);
  return 0;
}


/**
 * the left stick steers the pointer to x, y, the way a hand would: fast
 * while far, slow near it
 **/
static int
point_at(int x, int y) {
  int px, py;
  float dx, dy, d, m;

  for(int i=0; i<4000; i++) {
    ui_pointer(&px, &py);
    if(px < 0) {
      ui_sticks(40, 0, 0, 0);
      continue;
    }
    if(abs(px - x) <= 2 && abs(py - y) <= 2) {
      frame();
      return 0;
    }
    dx = (float)(x - px);
    dy = (float)(y - py);
    d = hypotf(dx, dy);
    m = d > 40 ? 127 : 40;
    ui_sticks((int)lroundf(dx / d * m), (int)lroundf(dy / d * m), 0, 0);
  }
  fprintf(stderr, "the pointer does not get to %d, %d\n", x, y);
  return -1;
}


/**
 * who is signed in, for the bar and the menu of their own posts: the
 * author of the timeline's first post
 **/
static void
make_me(x_user_t *u) {
  memset(u, 0, sizeof *u);
  snprintf(u->id, sizeof u->id, "%u", 1000 + hash("ritasousa") % 9000);
  snprintf(u->user, sizeof u->user, "ritasousa");
  snprintf(u->name, sizeof u->name, "Rita Sousa");
  snprintf(u->avatar, sizeof u->avatar, "https://pbs.twimg.com/profile_images/1/ritasousa_normal.jpg");
  snprintf(u->banner, sizeof u->banner, "https://pbs.twimg.com/profile_banners/1/ritasousa");
  u->badge = X_BADGE_BLUE;
  u->following = 312;
  u->followers = 4580;
  u->posts = 2210;
}


/**
 * the user's conversations: one unread, a request, a group, one read
 **/
static int
make_chats(x_post_t *p) {
  static const struct { const char *id, *user, *name, *text; int unread, request, mine, group, icon, ago; } c[] = {
    {"1-77", "joaomarques", "Jo\xc3\xa3o Marques", "\xf0\x9f\x8e\xae ASTRO BOT \xc2\xb7 bora jogar? (XPS5 PPSA07399)",
     1, 0, 0, 0, X_DM_INVITE, 120},
    {"55", "", "Squad PS5", "Eu levo o comando extra", 0, 0, 0, 1, X_DM_TEXT, 3600},
    {"1-91", "anacosta", "Ana Costa", "", 1, 1, 0, 0, X_DM_PHOTO, 7200},
    {"1-42", "pedrosilva", "Pedro Silva", "Obrigado pela dica do trof\xc3\xa9u!", 0, 0, 1, 0, X_DM_TEXT, 86400 * 3},
    {"g8812", "inesgomes", "In\xc3\xaas Gomes", "", 1, 0, 0, 0, X_DM_TEXT, 86400 * 4},
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
      snprintf(p[i].faces[0], sizeof p[i].faces[0], "https://pbs.twimg.com/profile_images/1/joaomarques_normal.jpg");
      snprintf(p[i].faces[1], sizeof p[i].faces[1], "https://pbs.twimg.com/profile_images/1/anacosta_normal.jpg");
      p[i].face_count = 2;
      snprintf(p[i].quote_name, sizeof p[i].quote_name, "Ana Costa");
    }
    p[i].badge = i == 0 ? X_BADGE_BLUE : X_BADGE_NONE;
    p[i].unread = c[i].unread;
    p[i].request = c[i].request;
    p[i].mine = c[i].mine;
    p[i].group = c[i].group;
    p[i].icon = c[i].icon;
    p[i].time = NOW - c[i].ago;
    p[i].replies = p[i].reposts = p[i].likes = p[i].views = -1;
  }
  snprintf(p[0].playing, sizeof p[0].playing, "ASTRO BOT");
  p[3].online = 1; // Pedro, on XPS5
  p[4].xchat = p[4].sealed = 1; // Inês wrote on X Chat: encrypted
  return n;
}


/**
 * the people of the group "Squad PS5" as its answer lists them, the user
 * (me) among them: two play, one is on XPS5, two are not around
 **/
static int
make_party(x_post_t *p, const char *me) {
  static const struct { const char *user, *name, *game; int online; } made[] = {
    {"ritasousa", "Rita Sousa", "", 1},
    {"pedrosilva", "Pedro Silva", "", 0},
    {"tiagolopes", "Tiago Lopes", "", 1},
    {"anacosta", "Ana Costa", "ASTRO BOT", 0},
    {"joaomarques", "Jo\xc3\xa3o Marques", "Gran Turismo 7", 0},
    {"martaf", "Marta Ferreira", "", 0},
  };
  int n = sizeof made / sizeof made[0];

  memset(p, 0, n * sizeof *p);
  for(int i=0; i < n; i++) {
    p[i].kind = X_KIND_USER;
    if(i) {
      snprintf(p[i].user_id, sizeof p[i].user_id, "%u", 1000 + hash(made[i].user) % 9000);
    } else {
      snprintf(p[i].user_id, sizeof p[i].user_id, "%s", me);
    }
    snprintf(p[i].user, sizeof p[i].user, "%s", made[i].user);
    snprintf(p[i].name, sizeof p[i].name, "%s", made[i].name);
    snprintf(p[i].avatar, sizeof p[i].avatar, "https://pbs.twimg.com/profile_images/1/%s_normal.jpg", made[i].user);
    snprintf(p[i].playing, sizeof p[i].playing, "%s", made[i].game);
    p[i].online = made[i].online;
    p[i].badge = i == 4 ? X_BADGE_BLUE : X_BADGE_NONE;
    p[i].replies = p[i].reposts = p[i].likes = p[i].views = -1;
  }
  return n;
}


/**
 * the group's messages, oldest first, the user's (me) among them
 **/
static int
make_group(x_post_t *p, const char *me) {
  static const struct { const char *user, *name, *text; int ago; } m[] = {
    {"anacosta", "Ana Costa", "Quem entra hoje \xc3\xa0 noite?", 5400},
    {"tiagolopes", "Tiago Lopes", "Eu! S\xc3\xb3 tenho de acabar o jantar", 5000},
    {"", "Rita Sousa", "Contem comigo \xf0\x9f\x8e\xae", 4200},
    {"anacosta", "Ana Costa", "Eu levo o comando extra", 3600},
  };
  int n = sizeof m / sizeof m[0];

  memset(p, 0, n * sizeof *p);
  for(int i=0; i < n; i++) {
    p[i].kind = X_KIND_DM;
    p[i].group = 1;
    snprintf(p[i].id, sizeof p[i].id, "%d", 5500 + i);
    p[i].mine = !m[i].user[0];
    if(p[i].mine) {
      snprintf(p[i].user_id, sizeof p[i].user_id, "%s", me);
      snprintf(p[i].user, sizeof p[i].user, "ritasousa");
    } else {
      snprintf(p[i].user_id, sizeof p[i].user_id, "%u", 1000 + hash(m[i].user) % 9000);
      snprintf(p[i].user, sizeof p[i].user, "%s", m[i].user);
      snprintf(p[i].avatar, sizeof p[i].avatar, "https://pbs.twimg.com/profile_images/1/%s_normal.jpg", m[i].user);
    }
    snprintf(p[i].name, sizeof p[i].name, "%s", m[i].name);
    snprintf(p[i].text, sizeof p[i].text, "%s", m[i].text);
    p[i].icon = X_DM_TEXT;
    p[i].time = NOW - m[i].ago;
    p[i].replies = p[i].reposts = p[i].likes = p[i].views = -1;
  }
  return n;
}


/**
 * the group on X Chat, oldest first: who made it and added people, a
 * message with reactions, the user's reply to it (edited), one taken
 * back, a missed call, a file and a reply to it, a screenshot, a video
 * call and messages that disappear
 **/
static int
make_xchat_group(x_post_t *p, const char *me) {
  static const struct { const char *user, *name; int icon, event; long long n; const char *text; int ago; } m[] = {
    {"anacosta", "Ana Costa", X_DM_EVENT, X_EV_GROUP_MADE, 0, "Squad PS5", 9000},
    {"anacosta", "Ana Costa", X_DM_EVENT, X_EV_ADDED, 2, "", 8990},
    {"anacosta", "Ana Costa", X_DM_TEXT, 0, 0, "Quem alinha no torneio de s\xc3\xa1" "bado?", 7200},
    {"", "Rita Sousa", X_DM_TEXT, 0, 0, "Eu! Levo o comando extra \xf0\x9f\x8e\xae", 7000},
    {"tiagolopes", "Tiago Lopes", X_DM_DELETED, 0, 0, "", 6800},
    {"anacosta", "Ana Costa", X_DM_CALL, X_EV_CALL_MISSED, 0, "", 5000},
    {"anacosta", "Ana Costa", X_DM_FILE, 0, 0, "As regras, para lerem antes", 4000},
    {"tiagolopes", "Tiago Lopes", X_DM_TEXT, 0, 0, "Perfeito, li tudo \xf0\x9f\x91\x8c", 3500},
    {"tiagolopes", "Tiago Lopes", X_DM_EVENT, X_EV_SCREENSHOT, 1, "", 3400},
    {"", "Rita Sousa", X_DM_CALL, X_EV_CALL_ENDED, 754, "", 600},
    {"", "Rita Sousa", X_DM_EVENT, X_EV_DISAPPEAR, 86400, "", 300},
  };
  int n = sizeof m / sizeof m[0];

  memset(p, 0, n * sizeof *p);
  for(int i=0; i < n; i++) {
    p[i].kind = X_KIND_DM;
    p[i].group = 1;
    p[i].xchat = 1;
    snprintf(p[i].id, sizeof p[i].id, "%d", 5600 + i);
    p[i].mine = !m[i].user[0];
    if(p[i].mine) {
      snprintf(p[i].user_id, sizeof p[i].user_id, "%s", me);
      snprintf(p[i].user, sizeof p[i].user, "ritasousa");
    } else {
      snprintf(p[i].user_id, sizeof p[i].user_id, "%u", 1000 + hash(m[i].user) % 9000);
      snprintf(p[i].user, sizeof p[i].user, "%s", m[i].user);
      snprintf(p[i].avatar, sizeof p[i].avatar, "https://pbs.twimg.com/profile_images/1/%s_normal.jpg", m[i].user);
    }
    snprintf(p[i].name, sizeof p[i].name, "%s", m[i].name);
    snprintf(p[i].text, sizeof p[i].text, "%s", m[i].text);
    p[i].icon = m[i].icon;
    p[i].event = m[i].event;
    p[i].event_n = m[i].n;
    p[i].time = NOW - m[i].ago;
    p[i].replies = p[i].reposts = p[i].likes = p[i].views = -1;
  }
  // reactions on Ana's question, the user's among them
  snprintf(p[2].reactions, sizeof p[2].reactions, "\xf0\x9f\x94\xa5\xf0\x9f\x91\x8d");
  p[2].reaction_count = 3;
  p[2].reacted = 1;
  // the user's reply to it, edited since
  snprintf(p[3].quote_name, sizeof p[3].quote_name, "Ana Costa");
  snprintf(p[3].quote_user, sizeof p[3].quote_user, "%s", p[2].user_id);
  snprintf(p[3].quote_text, sizeof p[3].quote_text, "%s", p[2].text);
  p[3].edited = 1;
  // the file, and Tiago's reply to it
  snprintf(p[6].file_name, sizeof p[6].file_name, "regras-do-torneio.pdf");
  p[6].file_size = 2400000;
  snprintf(p[7].quote_name, sizeof p[7].quote_name, "Ana Costa");
  p[7].quote_badge = X_DM_FILE;
  snprintf(p[7].reactions, sizeof p[7].reactions, "\xe2\x9d\xa4\xef\xb8\x8f");
  p[7].reaction_count = 1;
  // a video call
  p[9].event_video = 1;
  return n;
}


/**
 * a conversation with João, oldest first: his, the user's (me) and his (a
 * photo, a video and a voice note of X Chat among them),
 * the newest an invite to the game he plays
 **/
static int
make_messages(x_post_t *p, const char *me) {
  static const struct { int mine; const char *text; int icon, ago; } m[] = {
    {0, "Ol\xc3\xa1! J\xc3\xa1 viste o patch novo?", X_DM_TEXT, 7200},
    {1, "Vi sim, mudaram o mapa todo", X_DM_TEXT, 7000},
    {0, "", X_DM_PHOTO, 6900},
    {1, "Que loucura \xf0\x9f\x98\x82 Logo \xc3\xa0 noite jogamos, combinado?", X_DM_TEXT, 600},
    {0, "Bora jogar logo \xc3\xa0 noite? Tenho o novo mapa", X_DM_TEXT, 240},
    {1, "", X_DM_VOICE, 200},
    {0, "O mapa novo \xf0\x9f\x91\x80", X_DM_VIDEO, 160},
    {0, "\xf0\x9f\x8e\xae ASTRO BOT \xc2\xb7 bora jogar? (XPS5 PPSA07399)", X_DM_INVITE, 120},
  };
  int n = sizeof m / sizeof m[0];

  memset(p, 0, n * sizeof *p);
  for(int i=0; i < n; i++) {
    p[i].kind = X_KIND_DM;
    snprintf(p[i].id, sizeof p[i].id, "%d", 5000 + i);
    if(m[i].mine) {
      snprintf(p[i].user_id, sizeof p[i].user_id, "%s", me);
      snprintf(p[i].user, sizeof p[i].user, "ritasousa");
      snprintf(p[i].name, sizeof p[i].name, "Rita Sousa");
    } else {
      snprintf(p[i].user_id, sizeof p[i].user_id, "%u", 1000 + hash("joaomarques") % 9000);
      snprintf(p[i].user, sizeof p[i].user, "joaomarques");
      snprintf(p[i].name, sizeof p[i].name, "Jo\xc3\xa3o Marques");
      snprintf(p[i].avatar, sizeof p[i].avatar, "https://pbs.twimg.com/profile_images/1/joaomarques_normal.jpg");
      snprintf(p[i].playing, sizeof p[i].playing, "ASTRO BOT");
    }
    snprintf(p[i].text, sizeof p[i].text, "%s", m[i].text);
    p[i].icon = m[i].icon;
    p[i].time = NOW - m[i].ago;
    p[i].replies = p[i].reposts = p[i].likes = p[i].views = -1;
    if(m[i].icon == X_DM_PHOTO || m[i].icon == X_DM_VIDEO || m[i].icon == X_DM_VOICE) {
      // X Chat's, encrypted: what xchat.c's put_media makes of it
      x_media_t *media = &p[i].media[0];
      p[i].xchat = 1;
      p[i].media_count = 1;
      snprintf(media->url, sizeof media->url, "https://ton.x.com/xchat/media/%d", 5000 + i);
      media->width = 1280;
      media->height = 720;
      media->xchat = 1;
      memset(media->xchat_key, i, sizeof media->xchat_key);
      if(m[i].icon != X_DM_PHOTO) {
        snprintf(media->video_url, sizeof media->video_url, "file://xchat/%016x", 5000 + i);
        media->video = m[i].icon == X_DM_VIDEO;
        media->voice = m[i].icon == X_DM_VOICE;
        media->duration_ms = media->voice ? 14000 : 31000;
      }
    }
  }
  return n;
}


/**
 * who XPS5 saw around before the look of the Friends scene, five minutes
 * before it: Ana and Tiago already played, João was seen yesterday, Inês
 * three hours ago in the game that runs, Hugo two days ago on XPS5
 **/
static void
make_seen(seen_t *s) {
  static const struct {
    const char *user, *name, *game, *games[SEEN_GAMES];
    int state;
    long long ago;
  } made[] = {
    {"joaomarques", "Jo\xc3\xa3o Marques", "ASTRO BOT", {"ASTRO BOT", "Gran Turismo\xe2\x84\xa2 7", "Returnal"},
     SEEN_AWAY, 86400 + 4000},
    {"anaplays", "Ana", "Marvel's Spider-Man\xe2\x80\xa6", {"Marvel's Spider-Man\xe2\x80\xa6", "ASTRO BOT", ""},
     SEEN_PLAYING, 300},
    {"tiagolopes", "Tiago Lopes", "Gran Turismo\xe2\x84\xa2 7", {"Gran Turismo\xe2\x84\xa2 7", "", ""}, SEEN_PLAYING, 300},
    {"pedrosilva", "Pedro Silva", "", {"Returnal", "", ""}, SEEN_AWAY, 3 * 86400},
    {"inesgomes", "In\xc3\xaas Gomes", "Gran Turismo\xe2\x84\xa2 7", {"Gran Turismo\xe2\x84\xa2 7", "ASTRO BOT", ""},
     SEEN_AWAY, 3 * 3600},
    {"hugoreis", "Hugo Reis", "", {"Returnal", "", ""}, SEEN_AWAY, 2 * 86400 + 600},
  };
  static const struct { const char *user, *name, *game; long long ago; } news[] = {
    {"inesgomes", "In\xc3\xaas Gomes", "Gran Turismo\xe2\x84\xa2 7", 3 * 3600 + 1800},
    {"joaomarques", "Jo\xc3\xa3o Marques", "ASTRO BOT", 86400 + 9000},
    {"hugoreis", "Hugo Reis", "", 2 * 86400 + 4000},
  };

  memset(s, 0, sizeof *s);
  s->looked = NOW - 300;
  for(size_t i=0; i < sizeof made / sizeof made[0]; i++) {
    seen_person_t *p = &s->people[s->count++];
    snprintf(p->id, sizeof p->id, "%u", 1000 + hash(made[i].user) % 9000);
    p->at = NOW - made[i].ago;
    p->state = made[i].state;
    p->in = s->looked;
    snprintf(p->game, sizeof p->game, "%s", made[i].game);
    for(int g=0; g < SEEN_GAMES; g++) {
      snprintf(p->games[g], sizeof p->games[g], "%s", made[i].games[g]);
    }
  }
  for(size_t i=0; i < sizeof news / sizeof news[0]; i++) {
    seen_event_t *e = &s->event[s->events++];
    e->at = NOW - news[i].ago;
    snprintf(e->id, sizeof e->id, "%u", 1000 + hash(news[i].user) % 9000);
    snprintf(e->game, sizeof e->game, "%s", news[i].game);
    snprintf(e->name, sizeof e->name, "%s", news[i].name);
    snprintf(e->user, sizeof e->user, "%s", news[i].user);
    snprintf(e->avatar, sizeof e->avatar, "https://pbs.twimg.com/profile_images/1/%s_normal.jpg", news[i].user);
  }
}


/**
 * the people the user follows as the Friends tab reads them: Following's
 * five, and three more (one plays the game that runs here)
 **/
static int
make_all_friends(x_post_t *posts) {
  static const struct { const char *user, *name, *game, *bio; } more[] = {
    {"inesgomes", "In\xc3\xaas Gomes", "", "Fotos em modo foto e platinas lentas \xf0\x9f\x93\xb8"},
    {"tiagolopes", "Tiago Lopes", "Gran Turismo\xe2\x84\xa2 7", ""},
    {"hugoreis", "Hugo Reis", "", "Roguelikes, caf\xc3\xa9 e pouco sono"},
  };
  int n = make_friends(posts);

  snprintf(posts[4].text, sizeof posts[4].text, "Jogos de ritmo e speedruns ao domingo");
  for(size_t i=0; i < sizeof more / sizeof more[0]; i++, n++) {
    x_post_t *p = &posts[n];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_USER;
    snprintf(p->user_id, sizeof p->user_id, "%u", 1000 + hash(more[i].user) % 9000);
    snprintf(p->user, sizeof p->user, "%s", more[i].user);
    snprintf(p->name, sizeof p->name, "%s", more[i].name);
    snprintf(p->avatar, sizeof p->avatar, "https://pbs.twimg.com/profile_images/1/%s_normal.jpg", more[i].user);
    snprintf(p->playing, sizeof p->playing, "%s", more[i].game);
    snprintf(p->text, sizeof p->text, "%s", more[i].bio);
    p->following = 1;
  }
  return n;
}


/**
 * the user's week of play, as the payload sums it
 **/
static void
make_week_info(week_t *w) {
  static const struct { const char *id, *name; int hours, minutes, trophies, times; } g[] = {
    {"PPSA01316", "Gran Turismo\xe2\x84\xa2 7", 5, 40, 4, 3}, {"PPSA07399", "ASTRO BOT", 3, 15, 7, 2},
    {"PPSA01399", "Uncharted\xe2\x84\xa2: Drake's Fortune Remastered", 1, 5, 1, 1},
  };

  memset(w, 0, sizeof *w);
  for(size_t i=0; i < sizeof g / sizeof g[0]; i++) {
    for(int k=0; k < g[i].times; k++) {
      session_t one;
      memset(&one, 0, sizeof one);
      snprintf(one.id, sizeof one.id, "%s", g[i].id);
      snprintf(one.name, sizeof one.name, "%s", g[i].name);
      one.start = NOW - 86400 * (k + 1);
      one.end = one.start + (g[i].hours * 3600 + g[i].minutes * 60) / g[i].times;
      one.trophies[0] = k ? 0 : g[i].trophies;
      week_add(w, &one);
    }
  }
}


static int
fail(const char *what) {
  fprintf(stderr, "%s\n", what);
  return 1;
}


/**
 * the question of a hint for Grok's page, as main.c takes it; -1 when what
 * goes is not a hint
 **/
static int
grok_get(char *question, size_t qsize, char *game, size_t gsize, int *fresh) {
  char author[64];
  int what = -1, n = ui_grok_get(question, qsize, game, gsize, fresh, &what, author, sizeof author);

  return what == UI_GROK_HINT && !author[0] ? n : -1;
}


static void settled(void);
static int card_drawn(const char *dir, const char *name);
static int translate_scenes(const char *dir);
static int catchup_scenes(const char *dir);
static int friends_scenes(const char *dir, const char *me);
static int make_game_feed(x_post_t *posts);


/**
 * Chat's second tab, Friends: the people the user follows as a PS5's
 * friends, with who XPS5 saw around; R2 asks João to let the user into his
 * game, L2 opens his conversation, ✕ his profile with its player card
 **/
static int
friends_scenes(const char *dir, const char *me) {
  static seen_t seen;
  char id[X_ID_MAX], user[64], text[X_REPLY_MAX + 1], conv[X_ID_MAX], joao[16];
  x_post_t *more;
  ui_request_t req;
  const seen_t *kept;
  int count;

  (void)me;
  snprintf(joao, sizeof joao, "%u", 1000 + hash("joaomarques") % 9000);
  ui_keys(UI_CIRCLE); // back to Chat
  if(ui_wanted(&req) == 1 && req.feed == UI_CHAT && (more=mem_calloc(8, sizeof *more))) {
    ui_result(&req, more, make_chats(more), "", NULL, ERR_NONE, 200, -1, NOW);
  }
  make_seen(&seen);
  ui_seen_set(&seen);
  ui_keys(UI_RIGHT); // Chat's second tab
  if(ui_wanted(&req) != 1 || req.feed != UI_FRIENDS || !req.id[0] || !(more=mem_calloc(12, sizeof *more))) {
    return fail("Friends does not read the people the user follows");
  }
  count = make_all_friends(more);
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
  if(!ui_seen_get(&kept) || kept->events < 5 || !seen_find(kept, joao) ||
     seen_find(kept, joao)->state != SEEN_PLAYING) {
    return fail("the look of Friends is not noted");
  }
  if(strcmp(kept->event[0].user, "joaomarques") && strcmp(kept->event[1].user, "joaomarques")) {
    return fail("João's game is not in the friends' activity");
  }
  settled();
  if(draw(dir, "preview-friends.png")) {
    return 1;
  }
  // a minute and a half on, the side column's look at the same people: Hugo
  // is on XPS5 now, and Friends shows him with the others on it at once
  {
    text_surface_t screen = {g_pixels, UI_WIDTH, UI_HEIGHT, g_scale, 0};
    frame();
    ui_draw(&screen, NOW + 90, g_frame++);
  }
  if(ui_wanted(&req) != 1 || req.side != UI_SIDE_PLAYING || !(more=mem_calloc(12, sizeof *more))) {
    return fail("who plays now is not asked for again a minute on");
  }
  count = make_all_friends(more);
  for(int i=0; i < count; i++) {
    more[i].online = !strcmp(more[i].user, "hugoreis") || more[i].online;
  }
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW + 90);
  settled();
  if(draw(dir, "preview-friends-fresh.png")) {
    return 1;
  }
  // João, the second: R2 asks to join his game
  ui_keys(UI_DOWN);
  ui_keys(UI_R2);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_INVITE || strcmp(user, "joaomarques") ||
     ui_invite_get(text, sizeof text) <= 0 || strncmp(text, PLAY_JOIN "ASTRO BOT", strlen(PLAY_JOIN "ASTRO BOT")) ||
     !strstr(text, "(XPS5)") || play_conversation(me, joao, conv, sizeof conv) || strcmp(id, conv)) {
    return fail("R2 on a friend who plays does not ask to join");
  }
  ui_action_done(ERR_NONE, 200, -1);
  settled();
  if(draw(dir, "preview-friends-join.png")) {
    return 1;
  }
  if(ui_wanted(&req) == 1 && req.feed == UI_CHAT && (more=mem_calloc(8, sizeof *more))) {
    ui_result(&req, more, make_chats(more), "", NULL, ERR_NONE, 200, -1, NOW);
  }
  // L2: his conversation, even before it has a message
  ui_keys(UI_L2);
  if(ui_wanted(&req) != 1 || req.feed != UI_DM || strcmp(req.id, conv) || !(more=mem_calloc(12, sizeof *more))) {
    return fail("L2 on a friend does not open their conversation");
  }
  ui_result(&req, more, make_messages(more, me), "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_CIRCLE);
  // ✕: his profile, with the player card
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_USER || !(more=mem_calloc(6, sizeof *more))) {
    return fail("the cross on a friend does not open their profile");
  }
  {
    static x_user_t u;
    memset(&u, 0, sizeof u);
    snprintf(u.id, sizeof u.id, "%s", joao);
    snprintf(u.user, sizeof u.user, "joaomarques");
    snprintf(u.name, sizeof u.name, "Jo\xc3\xa3o Marques");
    snprintf(u.avatar, sizeof u.avatar, "https://pbs.twimg.com/profile_images/1/joaomarques_normal.jpg");
    snprintf(u.bio, sizeof u.bio, "Plataformas, corridas e tudo o que tiver trof\xc3\xa9us \xf0\x9f\x8f\x86");
    if(play_status("ASTRO BOT", u.location, sizeof u.location)) {
      return fail("no location for João's game");
    }
    u.badge = X_BADGE_BLUE;
    u.followed = 1;
    u.follows_you = 1;
    u.following = 240;
    u.followers = 812;
    u.posts = 1630;
    snprintf(u.banner, sizeof u.banner, "https://pbs.twimg.com/profile_banners/1/joaomarques");
    count = make_game_feed(more);
    for(int i=0; i < count; i++) { // his
      snprintf(more[i].user_id, sizeof more[i].user_id, "%s", u.id);
      snprintf(more[i].user, sizeof more[i].user, "%s", u.user);
      snprintf(more[i].name, sizeof more[i].name, "%s", u.name);
      snprintf(more[i].avatar, sizeof more[i].avatar, "%s", u.avatar);
      more[i].badge = u.badge;
    }
    ui_result(&req, more, count, "", &u, ERR_NONE, 200, -1, NOW);
  }
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  settled();
  if(draw(dir, "preview-player-card.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);
  return 0;
}


/**
 * PSN's social side on X, from João's conversation (me the user's number):
 * R2's voice note to it, then the group's conversation with its side card
 * of who plays, and R1's invite to the game for all of it
 **/
static int
psn_scenes(const char *dir, const char *me) {
  char id[X_ID_MAX], user[64], text[X_REPLY_MAX + 1];
  float bars[CARD_VOICE_BARS];
  capture_t c, media[UI_MEDIA_MAX];
  x_post_t people[UI_PARTY_MAX], *more;
  ui_request_t req;
  int retry = 0, count;

  ui_keys(UI_R2);
  if(!ui_voice_wanted(&retry) || retry) {
    return fail("R2 in a conversation does not tape a voice note");
  }
  for(int i=0; i < 40; i++) {
    const float s = sinf(i * 0.5f);
    ui_elapsed(100000);
    ui_voice_heard(UI_MIC_ON, 0, 0, 0.2f + 0.7f * s * s, (size_t)(16000 + i * 1600));
  }
  settled();
  if(draw(dir, "preview-voice-dm.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(!ui_voice_take()) {
    return fail("the cross does not stop the conversation's voice note");
  }
  for(int i=0; i < CARD_VOICE_BARS; i++) {
    const float s = sinf(i * 0.29f) * cosf(i * 0.13f);
    bars[i] = 0.12f + 0.88f * s * s;
  }
  if(!ui_voice_card(bars, CARD_VOICE_BARS, 5) || card_drawn(dir, "preview-card-voice-dm.png")) {
    return fail("the conversation's voice note gets no card");
  }
  memset(&c, 0, sizeof c);
  snprintf(c.path, sizeof c.path, "/xps5-cards/voice-5.mp4");
  c.kind = CAPTURE_MP4;
  c.size = 900000;
  c.time = NOW;
  ui_card_done(&c);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_VOICE_DM || strcmp(id, "1-77") ||
     strcmp(user, "@joaomarques")) {
    return fail("the voice note does not go to the conversation");
  }
  memset(&c, 0, sizeof c);
  if(ui_voice_dm_get(&c) != 1 || strcmp(c.path, "/xps5-cards/voice-5.mp4") || c.kind != CAPTURE_MP4) {
    return fail("the voice note sent is not the one made");
  }
  count = ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX);
  for(int i=0; i < count; i++) {
    if(!strcmp(media[i].path, "/xps5-cards/voice-5.mp4")) {
      return fail("the conversation's voice note went into the post");
    }
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(draw(dir, "preview-voice-dm-sent.png")) {
    return 1;
  }
  if(ui_wanted(&req) != 1 || req.feed != UI_DM || strcmp(req.id, "1-77") || !(more=mem_calloc(12, sizeof *more))) {
    return fail("the conversation is not read again with the voice note");
  }
  count = make_messages(more, me);
  more[count] = more[3]; // the user's: the note
  snprintf(more[count].id, sizeof more[count].id, "5100");
  more[count].text[0] = 0;
  more[count].icon = X_DM_VIDEO;
  more[count].time = NOW;
  ui_result(&req, more, count + 1, "", NULL, ERR_NONE, 200, -1, NOW);

  // the group's conversation, from Chat
  ui_keys(UI_CIRCLE);
  if(ui_wanted(&req) != 1 || req.feed != UI_CHAT || !(more=mem_calloc(8, sizeof *more))) {
    return fail("Chat is not read again after the voice note");
  }
  count = make_chats(more);
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN);
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_DM || strcmp(req.id, "55") || !(more=mem_calloc(8, sizeof *more))) {
    return fail("the group's conversation is not read");
  }
  ui_party(req.id, people, make_party(people, me));
  count = make_group(more, me);
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
  settled();
  if(draw(dir, "preview-party.png")) {
    return 1;
  }
  // R1: one invite to the game that runs, to the whole group
  ui_keys(UI_R1);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_INVITE || strcmp(id, "55") || strcmp(user, "Squad PS5") ||
     ui_invite_get(text, sizeof text) <= 0 || !strstr(text, "(XPS5 PPSA01316)")) {
    return fail("R1 does not invite the group to the game");
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(draw(dir, "preview-party-invited.png")) {
    return 1;
  }
  if(ui_wanted(&req) != 1 || req.feed != UI_DM || strcmp(req.id, "55") || !(more=mem_calloc(8, sizeof *more))) {
    return fail("the group's conversation is not read again with the invite");
  }
  ui_party(req.id, people, make_party(people, me));
  count = make_group(more, me);
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
  // the same group as X Chat reads it: its events, reactions, a reply, an
  // edit, one taken back, calls and a file
  if(!(more=mem_calloc(16, sizeof *more))) {
    return fail("no memory for X Chat's group");
  }
  ui_result(&req, more, make_xchat_group(more, me), "", NULL, ERR_NONE, 200, -1, NOW);
  settled();
  if(draw(dir, "preview-xchat-group.png")) {
    return 1;
  }
  for(int i=0; i < 7; i++) {
    ui_keys(UI_UP);
  }
  settled();
  if(draw(dir, "preview-xchat-group-top.png")) {
    return 1;
  }
  // a one-to-one conversation has no group's card: R1 does not invite
  ui_keys(UI_CIRCLE);
  if(ui_wanted(&req) == 1 && req.feed == UI_CHAT && (more=mem_calloc(8, sizeof *more))) {
    ui_result(&req, more, make_chats(more), "", NULL, ERR_NONE, 200, -1, NOW);
  }
  ui_keys(UI_UP);
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_DM || strcmp(req.id, "1-77") || !(more=mem_calloc(12, sizeof *more))) {
    return fail("João's conversation is not read again");
  }
  ui_result(&req, more, make_messages(more, me), "", NULL, ERR_NONE, 200, -1, NOW);
  // R1 in a one-to-one conversation: the invite goes into it
  ui_keys(UI_R1);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_INVITE || strcmp(id, "1-77") ||
     ui_invite_get(text, sizeof text) <= 0 || !strstr(text, "(XPS5 PPSA01316)")) {
    return fail("R1 does not invite João in his conversation");
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(ui_wanted(&req) != 1 || req.feed != UI_DM || strcmp(req.id, "1-77") || !(more=mem_calloc(12, sizeof *more))) {
    return fail("João's conversation is not read again with the invite");
  }
  // his answer: a request to join the game he plays
  count = make_messages(more, me);
  more[count] = more[4];
  snprintf(more[count].id, sizeof more[count].id, "5200");
  if(play_join("ASTRO BOT", "posso entrar?", more[count].text, sizeof more[count].text)) {
    return fail("no request to join");
  }
  more[count].icon = X_DM_JOIN;
  more[count].time = NOW - 30;
  ui_result(&req, more, count + 1, "", NULL, ERR_NONE, 200, -1, NOW);
  settled();
  if(draw(dir, "preview-dm-join.png")) {
    return 1;
  }
  // ✕ on it: an invite back, to the game that runs here
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_INVITE || strcmp(id, "1-77") ||
     ui_invite_get(text, sizeof text) <= 0 || !strstr(text, "(XPS5 PPSA01316)")) {
    return fail("the cross on a request to join does not invite back");
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(ui_wanted(&req) == 1 && req.feed == UI_DM && (more=mem_calloc(12, sizeof *more))) {
    ui_result(&req, more, make_messages(more, me), "", NULL, ERR_NONE, 200, -1, NOW);
  }
  // his newest message came on X Chat, encrypted: the conversation ends with
  // a bubble that says so; then as it was
  for(int sealed=1; sealed >= 0; sealed--) {
    ui_keys(UI_CIRCLE);
    if(!sealed) {
      ui_keys(UI_SQUARE); // the conversations read again, as they are now
    }
    if(ui_wanted(&req) == 1 && req.feed == UI_CHAT && (more=mem_calloc(8, sizeof *more))) {
      count = make_chats(more);
      if(sealed) {
        more[0].sealed = 1; // João's
        more[0].text[0] = 0;
        more[0].icon = X_DM_TEXT;
        more[0].time = NOW - 20;
        snprintf(more[0].reposter, sizeof more[0].reposter, "77:1"); // its number on X Chat (xchat_merge)
      }
      ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
    }
    ui_keys(UI_UP);
    ui_keys(UI_CROSS);
    // it opens as before, answered there, its X Chat messages read too
    if(ui_wanted(&req) != 1 || req.feed != UI_DM || strcmp(req.id, "1-77") || req.xchat ||
       strcmp(req.xchat_id, sealed ? "77:1" : "") || !(more=mem_calloc(12, sizeof *more))) {
      return fail("João's conversation does not open again");
    }
    count = make_messages(more, me);
    ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
    // with its X Chat messages locked, the keyboard for the PIN comes up by itself (cancelled here)
    if((ui_action(id, sizeof id, user, sizeof user) == UI_ACT_XCHAT_PIN) != sealed) {
      return fail(sealed ? "João's X Chat messages do not ask for the PIN" : "a plain conversation asks for the PIN");
    }
    if(sealed) {
      ui_action_done(ERR_CANCELLED, 0, -1);
    }
    if(sealed) {
      settled();
      if(draw(dir, "preview-dm-sealed.png")) {
        return 1;
      }
    }
  }
  return friends_scenes(dir, me);
}


/**
 * the request of the list feed, answering the side column's on the way;
 * 0, or -1 when it does not come
 **/
static int
want_feed(int feed, ui_request_t *req) {
  for(int i=0; i<6; i++) {
    if(ui_wanted(req) != 1) {
      return -1;
    }
    if(req->feed == feed && req->side == UI_SIDE_NONE) {
      return 0;
    }
    ui_result(req, NULL, 0, "", NULL, ERR_EMPTY, 200, -1, NOW);
  }
  return -1;
}


/**
 * Following again, with posts about the game that runs (ASTRO BOT), a
 * video and the start of a thread
 **/
static int
make_game_day(x_post_t *posts) {
  x_post_t *p;
  int n = 0;

  p = post(&posts[n++], "Hugo Reis", "hugoreis", X_BADGE_NONE, 20 * 60,
           "Acabei o ASTRO BOT a 100%! O \xc3\xbaltimo n\xc3\xadvel \xc3\xa9 uma loucura e o boss final... "
           "nem acredito no que acontece \xf0\x9f\xa4\xaf", 30, 12, 410, 9800);
  media(p, "astro-final", 1600, 900, 0);
  post(&posts[n++], "Marta Ferreira", "martaf", X_BADGE_NONE, 45 * 60,
       "Algu\xc3\xa9m sabe se o patch de hoje melhora o modo foto?", 4, 0, 22, 800);
  post(&posts[n++], "Tiago Martins", "tiagom", X_BADGE_NONE, 2 * 3600,
       "#AstroBot tem o melhor final do ano. N\xc3\xa3o digo mais nada \xf0\x9f\xa4\x90", 9, 3, 150, 5200);
  p = post(&posts[n++], "Ana Lopes \xf0\x9f\x90\xb1", "analopes", X_BADGE_NONE, 3 * 3600,
           "O meu gato descobriu o comando \xf0\x9f\x98\x82", 120, 940, 15200, 410000);
  media(p, "gato", 1920, 1080, 1);
  post(&posts[n++], "Not\xc3\xad" "cias de Jogos", "noticiasjogos", X_BADGE_GREY, 4 * 3600,
       "\xf0\x9f\xa7\xb5 5 coisas que mudam na PS5 com a atualiza\xc3\xa7\xc3\xa3o desta semana. Um fio:", 40, 80, 900,
       61000);
  post(&posts[n++], "Clube de Fotografia do Porto", "fotoporto", X_BADGE_GOLD, 6 * 3600,
       "Passeio fotogr\xc3\xa1" "fico no s\xc3\xa1" "bado de manh\xc3\xa3, partida da Ribeira.", 2, 5, 61, 3000);
  post(&posts[n++], "In\xc3\xaas Carvalho", "inescarv", X_BADGE_NONE, 7 * 3600,
       "Algu\xc3\xa9m para uma partida online logo \xc3\xa0 noite?", 6, 0, 18, 700);
  post(&posts[n++], "Loja Jogos", "lojajogos", X_BADGE_GOLD, 8 * 3600,
       "Comandos DualSense com 20% de desconto at\xc3\xa9 domingo.", 3, 4, 40, 9000);
  post(&posts[n++], "Marta Ferreira", "martaf", X_BADGE_NONE, 9 * 3600,
       "J\xc3\xa1 experimentaram jogar com o som 3D nos auscultadores? Muda tudo.", 11, 2, 95, 2400);
  return n;
}


/**
 * Home's tab of the game that runs: the latest posts about ASTRO BOT, two
 * of them with words that give it away
 **/
static int
make_game_feed(x_post_t *posts) {
  x_post_t *p;
  int n = 0;

  post(&posts[n++], "Rui Costa", "ruicosta", X_BADGE_NONE, 4 * 60,
       "Finalmente platinei o ASTRO BOT! \xf0\x9f\x8f\x86 Que jogo incr\xc3\xadvel", 12, 4, 230, 5100);
  post(&posts[n++], "Sofia Mendes", "sofiamendes", X_BADGE_NONE, 9 * 60,
       "O chefe final do #AstroBot \xc3\xa9 brutal, n\xc3\xa3o acredito no que acontece", 8, 2, 140, 3900);
  p = post(&posts[n++], "Not\xc3\xad" "cias de Jogos", "noticiasjogos", X_BADGE_GREY, 25 * 60,
           "ASTRO BOT recebe hoje cinco n\xc3\xadveis novos, gr\xc3\xa1tis para todos.", 30, 90, 1800, 74000);
  media(p, "astro-final", 1600, 900, 0);
  post(&posts[n++], "Hugo Reis", "hugoreis", X_BADGE_NONE, 40 * 60,
       "Dica para o n\xc3\xadvel do gelo no ASTRO BOT: salta duas vezes antes da plataforma.", 5, 1, 64, 1500);
  post(&posts[n++], "Tiago Martins", "tiagom", X_BADGE_NONE, 55 * 60,
       "The ending of Astro Bot made me cry \xf0\x9f\x98\xad", 3, 0, 41, 1200);
  post(&posts[n++], "Marta Ferreira", "martaf", X_BADGE_NONE, 70 * 60,
       "Algu\xc3\xa9m joga ASTRO BOT com o som 3D nos auscultadores? Muda tudo.", 6, 1, 52, 1400);
  return n;
}


/**
 * the thread of noticiasjogos, as TweetDetail gives it: the post, the
 * author's own replies under it, and someone else's
 **/
static int
make_news_thread(x_post_t *posts, const x_post_t *focal) {
  x_post_t *p;
  int n = 0;

  posts[n++] = *focal;
  post(&posts[n++], "Not\xc3\xad" "cias de Jogos", "noticiasjogos", X_BADGE_GREY, 4 * 3600 - 60,
       "1. O modo de repouso gasta menos: a consola desce para menos de 1 W com os downloads "
       "parados, e acorda mais depressa quando carregas no bot\xc3\xa3o PS.", 3, 10, 210, 30000);
  p = post(&posts[n++], "Not\xc3\xad" "cias de Jogos", "noticiasjogos", X_BADGE_GREY, 4 * 3600 - 120,
           "2. As capturas de ecr\xc3\xa3 passam a guardar-se em HDR, com uma vers\xc3\xa3o SDR ao lado "
           "para partilhar sem as cores ficarem lavadas.", 2, 8, 180, 28000);
  media(p, "hdr-capturas", 1600, 900, 0);
  post(&posts[n++], "Not\xc3\xad" "cias de Jogos", "noticiasjogos", X_BADGE_GREY, 4 * 3600 - 180,
       "3. O centro de controlo deixa mudar a ordem dos cart\xc3\xb5" "es.\n\n"
       "4. Os perfis de som 3D por jogo ficam guardados na nuvem.\n\n"
       "5. E finalmente: a lista de amigos tem pesquisa! \xf0\x9f\x8e\x89", 6, 20, 340, 35000);
  post(&posts[n++], "Hugo Reis", "hugoreis", X_BADGE_NONE, 3 * 3600,
       "@noticiasjogos A 5 \xc3\xa9 a melhor, finalmente!", 1, 0, 12, 400);
  return n;
}


/**
 * the user's own posts of the week, and one older, for "Your week on X"
 **/
static int
make_my_week(x_post_t *posts) {
  x_post_t *p;
  int n = 0;

  post(&posts[n++], "Rita Sousa", "ritasousa", X_BADGE_BLUE, 5 * 3600,
       "Primeira run do Astro Bot feita, que jogo bonito \xf0\x9f\xa4\x96\xe2\x9c\xa8", 18, 9, 320, 12400);
  p = post(&posts[n++], "Rita Sousa", "ritasousa", X_BADGE_BLUE, 2 * 86400,
           "Fotos do fim de semana no Ger\xc3\xaas \xf0\x9f\x8c\xb2 a luz da manh\xc3\xa3 estava incr\xc3\xadvel",
           42, 31, 1250, 48000);
  media(p, "geres", 1600, 1067, 0);
  post(&posts[n++], "Rita Sousa", "ritasousa", X_BADGE_BLUE, 4 * 86400,
       "Stream de sexta: votem no pr\xc3\xb3ximo jogo!", 25, 2, 140, 6100);
  post(&posts[n++], "Rita Sousa", "ritasousa", X_BADGE_BLUE, 12 * 86400,
       "Bom fim de semana a todos!", 3, 0, 60, 2000);
  return n;
}


/**
 * the new things: posts covered for the game that runs, and one uncovered;
 * the video going on small in a corner, and R3 closing it; a thread read as
 * one text; the user's week; Settings' new rows
 **/
static int
extras(const char *dir) {
  ui_request_t req;
  x_post_t *list, focal;
  x_user_t me;
  int count;

  // Home: Following, read again with today's posts
  ui_keys(UI_RIGHT);
  ui_keys(UI_SQUARE);
  if(want_feed(UI_FOLLOWING, &req)) {
    return fail("Following is not read again");
  }
  list = mem_calloc(10, sizeof *list);
  count = make_game_day(list);
  ui_result(&req, list, count, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_game("PPSA07399", "ASTRO BOT");
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  if(draw(dir, "preview-spoiler.png")) {
    return 1;
  }
  ui_keys(UI_CROSS); // the first one, uncovered
  if(ui_wanted(&req) || draw(dir, "preview-spoiler-shown.png")) {
    return fail("the cross does not uncover the post");
  }

  // the video plays in its post a while, then goes on in the corner
  for(int i=0; i<3; i++) {
    ui_keys(UI_DOWN);
  }
  frame();
  frame();
  ui_elapsed(5000000);
  ui_elapsed(16667); // and frames of a sixtieth of a second again
  ui_keys(UI_DOWN); // it goes on in its post while that shows
  frame();
  if(!g_video[0]) {
    return fail("the video stops when the selection leaves it");
  }
  for(int i=0; i<3; i++) { // and small in the corner once that is gone
    ui_keys(UI_DOWN);
  }
  if(draw(dir, "preview-mini.png") || !g_video[0]) {
    return fail("the video does not go on in the corner");
  }
  ui_keys(UI_R3);
  frame();
  if(g_video[0]) {
    return fail("R3 does not close the small video");
  }

  // the thread, read as one text from its menu
  for(int i=0; i<3; i++) {
    ui_keys(UI_UP);
  }
  ui_keys(UI_CROSS);
  if(want_feed(UI_POST, &req)) {
    return fail("the thread's post does not open");
  }
  focal = list[4];
  list = mem_calloc(6, sizeof *list);
  count = make_news_thread(list, &focal);
  ui_result(&req, list, count, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_TRIANGLE);
  frame();
  if(point_at(1342, 277)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(draw(dir, "preview-thread-menu.png")) {
    return 1;
  }
  ui_keys(UI_UP); // the pointer goes away; the thread is the first row
  ui_keys(UI_CROSS);
  if(draw(dir, "preview-reader.png")) {
    return 1;
  }
  for(int i=0; i<4; i++) {
    ui_keys(UI_DOWN);
  }
  settle();
  if(draw(dir, "preview-reader-down.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);
  ui_keys(UI_CIRCLE);

  // the user's own profile, with their week
  if(point_at(120, 724)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_SQUARE);
  if(want_feed(UI_ME, &req)) {
    return fail("the user's profile is not read");
  }
  make_me(&me);
  // while a game with a long name runs: its name whole (X keeps the
  // location's first 30 characters), on a line of its own over the date
  ui_game("PPSA01399", "Uncharted\xe2\x84\xa2: Drake's Fortune Remastered");
  if(play_status("Uncharted\xe2\x84\xa2: Drake's Fortune Remastered", me.location, sizeof me.location)) {
    return fail("no location for the game");
  }
  me.joined = 1788220800LL; // September 2026
  list = mem_calloc(6, sizeof *list);
  count = make_my_week(list);
  ui_result(&req, list, count, "", &me, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  settle();
  if(draw(dir, "preview-week.png")) {
    return 1;
  }
  // the card of the user's own profile asks for the week, once
  if(ui_week_info_wanted() != 1 || ui_week_info_wanted()) {
    return fail("the user's profile does not ask for the week");
  }
  {
    week_t w;
    make_week_info(&w);
    ui_week_info(&w);
  }
  settle();
  if(draw(dir, "preview-my-week.png")) {
    return 1;
  }
  ui_game("PPSA07399", "ASTRO BOT");

  // Settings' new rows, further down
  if(point_at(120, 788)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(point_at(200, 706)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_DOWN); // the pointer goes away, and the second row
  for(int i=0; i<16; i++) { // to the captures' row (past the controller's vibration and speaker)
    ui_keys(UI_DOWN);
  }
  if(draw(dir, "preview-settings-more.png")) {
    return 1;
  }
  ui_keys(UI_DOWN); // what the profile says while a game runs
  if(draw(dir, "preview-settings-playing.png")) {
    return 1;
  }
  ui_keys(UI_DOWN); // the profile's game, then what it says while XPS5 is open with no game:
  ui_keys(UI_DOWN); // on, then off again
  ui_keys(UI_CROSS);
  {
    settings_t changed;
    if(!ui_settings_changed(&changed) || !changed.online) {
      return fail("Online on your profile does not turn on");
    }
  }
  if(draw(dir, "preview-settings-online.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  {
    settings_t changed;
    if(!ui_settings_changed(&changed) || changed.online) {
      return fail("Online on your profile does not turn off");
    }
  }
  for(int i=0; i<19; i++) {
    ui_keys(UI_UP);
  }
  ui_keys(UI_CIRCLE);

  // a new capture of the game the payload offered: the composer with it and the hashtag
  {
    capture_t shot;
    char text[64];
    capture_t media[4];

    memset(&shot, 0, sizeof shot);
    snprintf(shot.path, sizeof shot.path, "%s", "/user/av_contents/photo/NPXS40087/PPSA07399/012/ASTRO BOT_20261113.jpg");
    shot.time = 1794500000;
    shot.size = 812345;
    shot.kind = CAPTURE_JPEG;
    ui_shot(&shot);
    if(ui_compose_get(text, sizeof text, media, 4) != 1 || strcmp(text, "#ASTROBOT") ||
       strcmp(media[0].path, shot.path)) {
      return fail("the capture offered is not in the composer with the game's hashtag");
    }
    if(draw(dir, "preview-shot.png")) {
      return 1;
    }
    // a gold trophy, before what the composer holds, and its card first
    // of its pictures
    ui_trophy(2, "Bot Master");
    if(ui_compose_get(text, sizeof text, media, 4) != 1 ||
       strcmp(text, "Trof\xc3\xa9u de ouro desbloqueado: \xe2\x80\x9c" "Bot Master\xe2\x80\x9d\n#ASTROBOT")) {
      return fail("the trophy is not in the composer");
    }
    if(card_drawn(dir, "preview-card-gold.png")) {
      return 1;
    }
    card_made(9);
    if(ui_compose_get(text, sizeof text, media, 4) != 2 || strcmp(media[0].path, "/xps5-cards/card-9.jpg") ||
       strcmp(media[1].path, shot.path) || strncmp(text, "Trof\xc3\xa9u de ouro desbloqueado", 28)) {
      return fail("the gold trophy's card is not first in the composer");
    }
    if(draw(dir, "preview-trophy.png")) {
      return 1;
    }
    // □ on the card takes it out: the capture stays, for the scenes after
    ui_keys(UI_RIGHT);
    ui_keys(UI_SQUARE);
    if(ui_compose_get(text, sizeof text, media, 4) != 1 || strcmp(media[0].path, shot.path)) {
      return fail("\xe2\x96\xa1 does not take the trophy's card out");
    }
    ui_keys(UI_CIRCLE);
  }

  // Home's tab of the game: its latest posts, those with words that give it away covered
  ui_keys(UI_CIRCLE); // from the profile to Home
  ui_keys(UI_RIGHT);
  if(want_feed(UI_GAME, &req) || !req.latest ||
     strcmp(req.query, "(\"astro bot\" OR #astrobot) -spoiler -spoilers -filter:replies")) {
    return fail("the game's tab does not look for its posts");
  }
  list = mem_calloc(6, sizeof *list);
  count = make_game_feed(list);
  ui_result(&req, list, count, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  ui_badges(3, 12, 0); // the bar's counts: new notices and messages
  if(draw(dir, "preview-game-feed.png")) {
    return 1;
  }
  ui_badges(0, 0, 0);
  // the game stops: the tab stays while it is shown, and goes once it is left
  ui_game("", "");
  ui_keys(UI_LEFT);
  ui_keys(UI_RIGHT);
  if(want_feed(UI_SEARCH, &req)) {
    return fail("the game's tab stays without a game");
  }

  // Calls' test of the microphone: off, hearing, failed and asked again,
  // and over once Calls is left
  ui_game("PPSA07399", "ASTRO BOT");
  for(int i=0; i<4; i++) { // Explore, Notifications, Chat, Friends, Calls
    ui_keys(UI_RIGHT);
  }
  {
    int retry;
    if(ui_mic_wanted(&retry) || draw(dir, "preview-calls.png")) {
      return fail("the microphone is on before it is asked");
    }
    ui_keys(UI_CROSS);
    if(!ui_mic_wanted(&retry) || retry) {
      return fail("the cross does not test the microphone");
    }
    ui_mic(UI_MIC_ON, 0, 0, 0.72f, 0.86f);
    if(draw(dir, "preview-calls-mic.png")) {
      return 1;
    }
    ui_mic(UI_MIC_ON, 0, UI_MIC_PRIORITY_LOW, 0, 0);
    if(draw(dir, "preview-calls-mic-busy.png")) {
      return 1;
    }
    ui_mic(UI_MIC_FAILED, (int)0x80260105, 0, 0, 0);
    if(draw(dir, "preview-calls-mic-failed.png")) {
      return 1;
    }
    ui_keys(UI_CROSS);
    if(!ui_mic_wanted(&retry) || !retry || (ui_mic_wanted(&retry), retry)) {
      return fail("the cross does not try the microphone again, once");
    }
    ui_mic(UI_MIC_OPENING, 0, 0, 0, 0);
    ui_keys(UI_RIGHT); // Grok
    if(ui_mic_wanted(&retry)) {
      return fail("the microphone stays on away from Calls");
    }
    ui_mic(UI_MIC_OFF, 0, 0, 0, 0);
  }

  // Grok's hints without spoilers: asked with ✕, thinking, answered
  if(draw(dir, "preview-grok.png")) {
    return 1;
  }
  {
    char id[64], user[64], question[X_GROK_QUESTION], game[GAME_NAME];
    int fresh = 0;
    ui_keys(UI_CROSS);
    if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_GROK_TEXT) {
      return fail("the cross does not ask Grok");
    }
    ui_grok_text("  Como passo o n\xc3\xadvel do gelo?");
    if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_GROK ||
       grok_get(question, sizeof question, game, sizeof game, &fresh) <= 0 ||
       strcmp(question, "Como passo o n\xc3\xadvel do gelo?") || strcmp(game, "ASTRO BOT") || !fresh) {
      return fail("the question to Grok is not what was written");
    }
    if(draw(dir, "preview-grok-asking.png")) {
      return 1;
    }
    ui_grok_answer("Repara nas plataformas que brilham: s\xc3\xb3 essas aguentam o teu peso. "
                   "Experimenta abrandar antes de cada salto e usa o pairar para corrigir no ar.");
    ui_action_done(ERR_NONE, 200, -1);
    if(draw(dir, "preview-grok-answer.png")) {
      return 1;
    }
    if(grok_get(question, sizeof question, game, sizeof game, &fresh) <= 0 || fresh) {
      return fail("the second question starts a new conversation");
    }
    ui_keys(UI_SQUARE); // a new conversation
    if(grok_get(question, sizeof question, game, sizeof game, &fresh) != 0 || !fresh) {
      return fail("the square does not start a new conversation");
    }
  }

  // a new XPS5 found at start, and Settings' Updates row installing it
  ui_update(UI_UPDATE_CHECKING, NULL, 0, ERR_NONE, 0);
  ui_update(UI_UPDATE_FOUND, "1.1.0", 0, ERR_NONE, 0);
  if(draw(dir, "preview-update-toast.png")) {
    return 1;
  }
  if(point_at(120, 788)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(point_at(200, 706)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_DOWN); // the pointer goes away, and the second row
  for(int i=0; i<21; i++) { // to the last one
    ui_keys(UI_DOWN);
  }
  ui_keys(UI_LEFT); // ← does not install
  if(ui_update_wanted()) {
    return fail("← on the Updates row does something");
  }
  ui_update(UI_UPDATE_GETTING, NULL, 42, ERR_NONE, 0);
  if(draw(dir, "preview-update.png")) {
    return 1;
  }
  ui_update(UI_UPDATE_FOUND, "1.1.0", 0, ERR_NONE, 0);
  ui_keys(UI_CROSS);
  if(ui_update_wanted() != 2 || ui_update_wanted()) {
    return fail("the cross does not install the update found");
  }
  ui_keys(UI_CIRCLE);
  return 0;
}


/**
 * a capture in a message: the composer's button, Chat to pick the
 * conversation, and it sent
 **/
static int
share_scenes(const char *dir) {
  ui_request_t req;
  capture_t shot, media[UI_MEDIA_MAX];
  char id[X_ID_MAX], user[64], text[X_REPLY_MAX + 1];
  x_post_t *list;

  memset(&shot, 0, sizeof shot);
  snprintf(shot.path, sizeof shot.path, "%s",
           "/user/av_contents/photo/NPXS40087/PPSA07399/013/ASTRO BOT_20261113.jpg");
  shot.time = 1794500100;
  shot.size = 798000;
  shot.kind = CAPTURE_JPEG;
  ui_shot(&shot);
  // the draft kept the capture before it: □ takes it out, and this one stays
  for(int i=0; i<8; i++) {
    ui_keys(UI_LEFT);
  }
  ui_keys(UI_RIGHT);
  ui_keys(UI_SQUARE);
  for(int i=0; i<8; i++) { // to Post, the last
    ui_keys(UI_RIGHT);
  }
  ui_keys(UI_LEFT);
  if(draw(dir, "preview-share-button.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) == 1) {
    if(req.feed != UI_CHAT || !(list=mem_calloc(8, sizeof *list))) {
      return fail("the conversations to send to are not read");
    }
    ui_result(&req, list, make_chats(list), "", NULL, ERR_NONE, 200, -1, NOW);
  }
  if(ui_wanted(&req) || draw(dir, "preview-share-pick.png")) {
    return 1;
  }
  ui_keys(UI_CROSS); // João's
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_SHARE_DM || strcmp(id, "1-77") ||
     ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || strcmp(media[0].path, shot.path)) {
    return fail("the capture does not go to the conversation picked");
  }
  if(draw(dir, "preview-share-sending.png")) {
    return 1;
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(draw(dir, "preview-share-sent.png")) {
    return 1;
  }
  if(ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX)) {
    return fail("the composer keeps what was sent");
  }

  // the side column's Invite, with the pointer: to the game that runs here, with its title id
  if(point_at(1782, 404) || draw(dir, "preview-invite-side.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  {
    char want[96];
    unsigned me = 1000 + hash("ritasousa") % 9000, him = 1000 + hash("joaomarques") % 9000;

    snprintf(want, sizeof want, "%u-%u", me < him ? me : him, me < him ? him : me);
    if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_INVITE || strcmp(id, want) ||
       strcmp(user, "joaomarques") || ui_invite_get(text, sizeof text) <= 0 ||
       strcmp(text, "\xf0\x9f\x8e\xae ASTRO BOT \xc2\xb7 bora jogar? (XPS5 PPSA07399)")) {
      return fail("Invite does not invite to the game that runs");
    }
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(draw(dir, "preview-invite-sent.png")) {
    return 1;
  }
  return 0;
}


/**
 * the linking screen in the language Settings shows, opened from its X
 * account row with a wrong code, and closed again on the language row
 **/
static int
link_in_language(const char *dir, const char *name) {
  enum { S_SESSION_ROW = 21 }; // the X account's row of Settings (ui.c, S_SESSION), below the language
  ui_link_t st;

  for(int i=0; i<S_SESSION_ROW; i++) { // from the language to the X account
    ui_keys(UI_DOWN);
  }
  ui_keys(UI_CROSS);
  if(!ui_link_wanted()) {
    return fail("the X account row does not open the linking");
  }
  memset(&st, 0, sizeof st);
  st.state = UI_LINK_WAITING;
  st.last = UI_LINK_WRONG_CODE;
  st.wrong = 2;
  snprintf(st.ip, sizeof st.ip, "192.168.1.70");
  snprintf(st.code, sizeof st.code, "482913");
  ui_link_status(&st);
  if(draw(dir, name)) {
    return 1;
  }
  ui_keys(UI_CIRCLE);
  if(ui_link_wanted()) {
    return fail("the circle does not close the linking");
  }
  for(int i=0; i<S_SESSION_ROW; i++) {
    ui_keys(UI_UP);
  }
  return 0;
}


/**
 * linking the account from a PC, opened by the cross over a session X
 * refused, as main.c reports it: getting ready, waiting with the address
 * and the code, another account and a wrong code, X checking it, and
 * linked, which reads the lists again
 **/
static int
link_scenes(const char *dir) {
  settings_t changed;
  ui_request_t req;
  ui_link_t st;

  ui_keys(UI_CROSS);
  if(ui_link_wanted() != 1) {
    return fail("the cross does not open the linking");
  }
  if(draw(dir, "preview-link-starting.png")) {
    return 1;
  }
  memset(&st, 0, sizeof st);
  st.state = UI_LINK_NO_HELPER; // the payload does not run
  ui_link_status(&st);
  if(draw(dir, "preview-link-no-payload.png")) {
    return 1;
  }
  memset(&st, 0, sizeof st);
  st.state = UI_LINK_WAITING;
  snprintf(st.ip, sizeof st.ip, "192.168.1.70");
  snprintf(st.code, sizeof st.code, "482913");
  ui_link_status(&st);
  if(draw(dir, "preview-link-waiting.png")) {
    return 1;
  }
  st.seen = 1; // the QR's page opened on a phone
  ui_link_status(&st);
  if(draw(dir, "preview-link-seen.png")) {
    return 1;
  }
  st.seen = 0;
  ui_keys(UI_RIGHT);
  if(ui_link_wanted() != 2) {
    return fail("right does not choose account 2");
  }
  st.last = UI_LINK_WRONG_CODE;
  st.wrong = 2;
  ui_link_status(&st);
  if(draw(dir, "preview-link-wrong.png")) {
    return 1;
  }
  ui_keys(UI_LEFT);
  st.state = UI_LINK_CHECKING;
  st.last = UI_LINK_FINE;
  ui_link_status(&st);
  ui_keys(UI_RIGHT); // nothing while X checks it
  if(ui_link_wanted() != 1) {
    return fail("the account changes while X checks the session");
  }
  if(draw(dir, "preview-link-checking.png")) {
    return 1;
  }
  st.state = UI_LINK_DONE;
  st.account = 1;
  snprintf(st.user, sizeof st.user, "joaomarques");
  ui_link_status(&st);
  if(draw(dir, "preview-link-done.png")) {
    return 1;
  }
  if(!ui_settings_changed(&changed) || changed.account != 1 || strcmp(changed.users[0], "joaomarques")) {
    return fail("the linked account is not kept");
  }
  ui_keys(UI_CROSS);
  if(ui_link_wanted() || ui_wanted(&req) != 1) {
    return fail("the lists are not read again once linked");
  }
  return draw(dir, "preview-link-after.png");
}


/*
 * a login page made up here, the way the engine would hand one over: a
 * heading, a field, two buttons and a line, no X artwork
 */

static uint32_t *g_page;
static int       g_page_w, g_page_h;


static void
page_box(float x, float y, float w, float h, float r, uint32_t c) {
  const int x0 = (int)(x * g_scale), y0 = (int)(y * g_scale), x1 = (int)((x + w) * g_scale),
            y1 = (int)((y + h) * g_scale);
  const float rs = r * g_scale;

  for(int py=y0 < 0 ? 0 : y0; py < y1 && py < g_page_h; py++) {
    for(int px=x0 < 0 ? 0 : x0; px < x1 && px < g_page_w; px++) {
      float cx = px + 0.5f < x0 + rs ? x0 + rs : px + 0.5f > x1 - rs ? x1 - rs : px + 0.5f;
      float cy = py + 0.5f < y0 + rs ? y0 + rs : py + 0.5f > y1 - rs ? y1 - rs : py + 0.5f;
      if(hypotf(px + 0.5f - cx, py + 0.5f - cy) <= rs) {
        g_page[(size_t)py * g_page_w + px] = c;
      }
    }
  }
}


static void
page_text(text_font_t *font, float x, float y, const char *s, uint32_t c, int centred) {
  text_surface_t page = {g_page, LG_PAGE_W, LG_PAGE_H, g_scale, 0};

  if(centred) {
    x -= text_width(font, s, strlen(s)) / 2.0f;
  }
  text_draw(font, &page, (int)x, (int)y, s, strlen(s), c);
}


static int
make_page(int width, int height) {
  const uint32_t white = 0xffffffffu, ink = 0xff19140fu, grey = 0xff716453u, rim = 0xffded9cfu,
                 blue = 0xfff09b1du;
  text_font_t *big = text_font_open_scaled(inter_bold, 40, g_scale),
              *body = text_font_open_scaled(inter_regular, 22, g_scale),
              *bold = text_font_open_scaled(inter_bold, 22, g_scale);

  g_page_w = width;
  g_page_h = height;
  if(!big || !body || !bold || !(g_page=mem_alloc((size_t)width * height * 4))) {
    return -1;
  }
  for(size_t i=0; i < (size_t)width * height; i++) {
    g_page[i] = white;
  }
  page_text(big, 84, 150, "Sign in", ink, 0);
  page_box(84, 230, 560, 82, 6, rim);
  page_box(86, 232, 556, 78, 5, white);
  page_text(body, 106, 281, "Phone, email address or username", grey, 0);
  page_box(84, 356, 560, 56, 28, ink);
  page_text(bold, 364, 392, "Next", white, 1);
  page_box(84, 436, 560, 56, 28, rim);
  page_box(86, 438, 556, 52, 26, white);
  page_text(bold, 364, 472, "Forgot password?", ink, 1);
  page_text(body, 84, 580, "No account yet?", grey, 0);
  page_text(body, 84 + text_width(body, "No account yet? ", 16), 580, "Sign up", blue, 0);
  text_font_close(big);
  text_font_close(body);
  text_font_close(bold);
  return 0;
}


/**
 * what the window sent main.c since the last call, as kinds in order
 **/
static int
login_inputs(ui_login_input_t *list, int max) {
  int n = 0;
  while(n < max && ui_login_input(&list[n])) {
    n++;
  }
  return n;
}


/**
 * a frame as it is now, without the steps frame() takes first
 **/
static int
shot(const char *dir, const char *name) {
  text_surface_t screen = {g_pixels, UI_WIDTH, UI_HEIGHT, g_scale, 0};
  char path[512];

  ui_draw(&screen, NOW, g_frame++);
  snprintf(path, sizeof path, "%s/%s", dir, name);
  if(!stbi_write_png(path, g_pw, g_ph, 4, g_pixels, g_pw * 4)) {
    fprintf(stderr, "cannot write %s\n", path);
    return -1;
  }
  printf("%s\n", path);
  return 0;
}


/**
 * time going by in frames of a sixtieth of a second
 **/
static void
pass(unsigned us) {
  for(unsigned gone=0; gone < us; gone += 16667) {
    ui_elapsed(16667);
  }
}


/**
 * a clip the payload offered: the composer plays it in its tile, on the
 * text, so ✕ writes about it
 **/
static int
clip_scene(const char *dir) {
  capture_t clip;

  for(int i=0; i < 3; i++) { // the sheets the scenes before left open
    ui_keys(UI_CIRCLE);
    frame();
  }
  memset(&clip, 0, sizeof clip);
  snprintf(clip.path, sizeof clip.path, "%s",
           "/user/av_contents/video/NPXS40087/PPSA07399/014/ASTRO BOT_20261114_183000.mp4");
  clip.time = 1794600000;
  clip.size = 23400000;
  clip.kind = CAPTURE_MP4;
  ui_shot(&clip);
  frame(); // the first frame opens the player
  if(draw(dir, "preview-shot-clip.png")) {
    return 1;
  }
  // on it, ✕ goes to its last 30 s, then the last 15; □ would take it out
  ui_keys(UI_DOWN);
  ui_keys(UI_CROSS);
  {
    capture_t media[UI_MEDIA_MAX];
    char text[64];

    if(ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || media[0].clip != CLIP_LAST_30) {
      return fail("\xe2\x9c\x95 on the clip does not take its last 30 s");
    }
    if(draw(dir, "preview-shot-trim.png")) {
      return 1;
    }
    ui_keys(UI_CROSS);
    if(ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || media[0].clip != CLIP_LAST_15) {
      return fail("\xe2\x9c\x95 again does not take its last 15 s");
    }
  }
  return 0;
}


/**
 * the way in at five moments (XPS5's letters small, growing, filling the
 * screen, going through it, gone), then Following coming in over For you,
 * a moment in and settled
 **/
static int
motion_scenes(const char *dir) {
  static const struct {
    unsigned    us;
    const char *name;
  } moments[] = {
    {150000, "preview-intro-start.png"}, {800000, "preview-intro-growing.png"},
    {500000, "preview-intro-fill.png"}, {300000, "preview-intro-through.png"},
    {400000, "preview-intro-gone.png"},
  };
  text_surface_t screen = {g_pixels, UI_WIDTH, UI_HEIGHT, g_scale, 0};
  ui_request_t req;

  for(int i=0; i < 3; i++) { // the sheets the scenes before left open
    ui_keys(UI_CIRCLE);
    frame();
  }
  ui_motion();
  ui_draw(&screen, NOW, g_frame++); // the first frame starts the letters
  for(size_t i=0; i < sizeof moments / sizeof moments[0]; i++) {
    pass(moments[i].us);
    if(shot(dir, moments[i].name)) {
      return 1;
    }
  }
  // Following, from For you: it slides in from the right as its tab sits
  ui_keys(UI_RIGHT);
  for(int i=0; i < 8 && ui_wanted(&req) == 1; i++) {
    ui_result(&req, NULL, 0, "", NULL, ERR_NONE, 200, -1, NOW);
  }
  ui_draw(&screen, NOW, g_frame++); // the frame before stays at first
  pass(100000);
  if(shot(dir, "preview-page-coming.png")) {
    return 1;
  }
  pass(400000);
  return shot(dir, "preview-page-come.png");
}


/**
 * the login window inside XPS5, opened by the cross over a session X
 * refused once the engine is on the console: starting, the page loading
 * and shown, the pointer's click, the keyboard asked for a field that is
 * not there, the next field, the page scrolled, the engine failing and □
 * to try again, and ○ closing it
 **/
static int
login_scenes(const char *dir) {
  enum { S_SESSION_ROW = 21 }; // the X account's row of Settings, below the language
  ui_login_t st;
  ui_login_input_t in[64]; // as many as the window keeps
  int w, h, zoom, n, x, y, account;

  // Settings, from More on the bar, with the engine on the console
  ui_login_ready(1);
  if(point_at(120, 788)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(point_at(200, 706)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_DOWN); // the pointer goes away, and the language's row
  ui_keys(UI_UP);

  // in English first, as most will see it: the X account's row opens it
  ui_keys(UI_RIGHT);
  if(ui_lang() != LANG_EN) {
    return fail("English does not come");
  }
  for(int i=0; i<S_SESSION_ROW; i++) {
    ui_keys(UI_DOWN);
  }
  if(draw(dir, "preview-login-row-en.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_login_wanted(&w, &h, &zoom) < 1) {
    return fail("the X account row does not open the login window");
  }
  if(w != (int)floorf(LG_PAGE_W * g_scale + 0.5f) || h != (int)floorf(LG_PAGE_H * g_scale + 0.5f) ||
     zoom != (int)(140 * g_scale + 0.5f)) {
    return fail("the login page is not of its size");
  }
  if(make_page(w, h)) {
    return fail("no made-up login page");
  }
  memset(&st, 0, sizeof st);
  st.state = UI_LOGIN_SHOWN;
  st.on_x = 1;
  ui_login_status(&st);
  ui_login_pixels(g_page, w, h);
  if(draw(dir, "preview-login-en.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);
  if(ui_login_wanted(&w, &h, &zoom)) {
    return fail("the circle does not close the login window");
  }
  for(int i=0; i<S_SESSION_ROW; i++) {
    ui_keys(UI_UP);
  }
  ui_keys(UI_LEFT);
  if(ui_lang() != LANG_PT) {
    return fail("the PS5's language does not come back");
  }

  // then each way it goes, in Portuguese
  for(int i=0; i<S_SESSION_ROW; i++) {
    ui_keys(UI_DOWN);
  }
  ui_keys(UI_CROSS);
  if(ui_login_wanted(&w, &h, &zoom) < 1) {
    return fail("the X account row does not open the login window again");
  }
  if(draw(dir, "preview-login-starting.png")) {
    return 1;
  }
  memset(&st, 0, sizeof st);
  st.state = UI_LOGIN_SHOWN;
  st.loading = 1;
  st.progress = 60;
  st.on_x = 1;
  ui_login_status(&st);
  ui_login_pixels(g_page, w, h);
  if(draw(dir, "preview-login-loading.png")) {
    return 1;
  }
  st.loading = 0;
  st.progress = 100;
  ui_login_status(&st);
  login_inputs(in, 64);

  // the pointer on the field: a click there, in the page's pixels
  if(point_at(LG_PAGE_X + 300, LG_PAGE_Y + 270)) {
    return 1;
  }
  login_inputs(in, 64); // its moves
  ui_keys(UI_CROSS);
  n = login_inputs(in, 64);
  x = (int)(300 * g_scale);
  y = (int)(270 * g_scale);
  if(n != 2 || in[0].kind != UI_LOGIN_MOVE || in[1].kind != UI_LOGIN_CLICK || abs(in[1].x - x) > 4 * g_scale ||
     abs(in[1].y - y) > 4 * g_scale) {
    return fail("the cross does not click the page where the pointer is");
  }
  if(draw(dir, "preview-login-shown.png")) {
    return 1;
  }
  st.keyboard = 1; // the PS5's keyboard up for that field
  ui_login_status(&st);
  if(draw(dir, "preview-login-keyboard.png")) {
    return 1;
  }
  st.keyboard = 0;
  ui_login_status(&st);

  // △ asks for the field in focus; none, so a note says how
  ui_keys(UI_TRIANGLE);
  n = login_inputs(in, 64);
  if(n != 1 || in[0].kind != UI_LOGIN_FIELD) {
    return fail("the triangle does not ask for the field");
  }
  st.no_field = 1;
  ui_login_status(&st);
  if(draw(dir, "preview-login-no-field.png")) {
    return 1;
  }
  ui_keys(UI_DOWN);
  ui_keys(UI_UP);
  n = login_inputs(in, 64);
  if(n != 2 || in[0].kind != UI_LOGIN_KEY || in[0].d != LOGIN_KEY_TAB || in[1].kind != UI_LOGIN_KEY ||
     in[1].d != LOGIN_KEY_BACKTAB) {
    return fail("↓ and ↑ do not go to the next and the last field");
  }
  for(int i=0; i < 20; i++) {
    ui_sticks(0, 0, 0, 120);
  }
  n = login_inputs(in, 64);
  if(n < 1 || in[n - 1].kind != UI_LOGIN_WHEEL || in[n - 1].d <= 0) {
    return fail("the right stick does not scroll the page down");
  }
  ui_keys(UI_LEFT); // another account for it
  if(ui_login_wanted(&w, &h, &zoom) < 1) {
    return fail("the account's arrows close the window");
  }

  // the page signed in: X's session tried on X, then refused (the window stays)
  account = ui_login_wanted(&w, &h, &zoom);
  ui_login_session(account, UI_SESSION_CHECKING, NULL);
  if(draw(dir, "preview-login-checking.png")) {
    return 1;
  }
  ui_login_session(account, 401, NULL);
  if(ui_login_wanted(&w, &h, &zoom) != account) {
    return fail("a session X refused closes the login window");
  }
  if(draw(dir, "preview-login-refused.png")) {
    return 1;
  }

  // a page X did not give
  st.why = UI_LOGIN_PAGE;
  st.code = -1001;
  ui_login_status(&st);
  if(draw(dir, "preview-login-page-error.png")) {
    return 1;
  }

  // its launcher there but not read (errno 12)
  memset(&st, 0, sizeof st);
  st.state = UI_LOGIN_FAILED;
  st.why = UI_LOGIN_NO_READ;
  st.code = 12;
  ui_login_status(&st);
  ui_login_pixels(NULL, 0, 0);
  if(draw(dir, "preview-login-no-read.png")) {
    return 1;
  }

  // the engine gone: □ starts it again
  memset(&st, 0, sizeof st);
  st.state = UI_LOGIN_FAILED;
  st.why = UI_LOGIN_NO_START;
  st.code = 3;
  ui_login_status(&st);
  ui_login_pixels(NULL, 0, 0);
  if(draw(dir, "preview-login-failed.png")) {
    return 1;
  }
  ui_keys(UI_SQUARE);
  n = login_inputs(in, 64);
  if(n != 1 || in[0].kind != UI_LOGIN_RETRY) {
    return fail("□ does not start the login engine again");
  }
  ui_keys(UI_CROSS); // the pointer on the page's "Try again"
  n = login_inputs(in, 64);
  if(n != 1 || in[0].kind != UI_LOGIN_RETRY) {
    return fail("the cross on Try again does not start the login engine again");
  }
  ui_keys(UI_CIRCLE);
  if(ui_login_wanted(&w, &h, &zoom)) {
    return fail("the circle does not close the login window");
  }
  mem_free(g_page);
  g_page = NULL;
  if(draw(dir, "preview-login-closed.png")) {
    return 1;
  }

  // a session X took, after the window closed: kept, and its account shown
  ui_login_session(account, 0, "xps5_test");
  if(ui_account() != account) {
    return fail("a session X took does not go to its account");
  }
  return draw(dir, "preview-login-signed.png");
}


/**
 * the bar's account: its menu as x.com has it (the accounts here, the one
 * in use ticked, adding one, logging out), adding one in the login window
 * for the first account without a session, and logging out after the
 * question: the account's file is to be emptied, and the next one is read
 **/
static int
account_scenes(const char *dir) {
  enum { ME_X = 188, ME_Y = 1008 }; // the bar's account
  int w, h, zoom;

  ui_login_ready(1);
  ui_accounts(3); // accounts 1 and 2 have a session
  if(point_at(ME_X, ME_Y)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(draw(dir, "preview-account-menu.png")) {
    return 1;
  }
  ui_keys(UI_DOWN); // the pointer goes away, and the second row: account 2
  ui_keys(UI_DOWN); // Add an existing account
  ui_keys(UI_CROSS);
  if(ui_login_wanted(&w, &h, &zoom) != 3) {
    return fail("Add an existing account does not log in to account 3");
  }
  ui_keys(UI_CIRCLE);
  if(ui_login_wanted(&w, &h, &zoom)) {
    return fail("the circle does not close the login window");
  }

  if(point_at(ME_X, ME_Y)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  for(int i=0; i<3; i++) { // Log out
    ui_keys(UI_DOWN);
  }
  ui_keys(UI_CROSS);
  if(ui_logout_take()) {
    return fail("logged out before the question is answered");
  }
  if(draw(dir, "preview-logout-ask.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_logout_take() != 1 || ui_account() != 2 || ui_logout_take()) {
    return fail("Log out does not log out of account 1, once, and go to account 2");
  }
  return draw(dir, "preview-logged-out.png");
}


/**
 * a frame drawn, and the transition it began (the scenes before turned
 * them on) over, for the next one to show what stays
 **/
static void
settled(void) {
  text_surface_t screen = {g_pixels, UI_WIDTH, UI_HEIGHT, g_scale, 0};

  frame();
  ui_draw(&screen, NOW, g_frame++);
  ui_elapsed(1000000);
  ui_elapsed(16667);
}


/**
 * frames until the card asked for is drawn (its pictures come as the
 * loader's would), and it written as a picture of its own; 0, or -1
 **/
static int
card_drawn(const char *dir, const char *name) {
  uint32_t *px = NULL;
  char path[512];
  int w = 0, h = 0;

  for(int i=0; i < 20 && !ui_card_take(&px, &w, &h); i++) {
    frame();
    load_pictures();
  }
  if(!px) {
    fprintf(stderr, "the card %s is not drawn\n", name);
    return -1;
  }
  snprintf(path, sizeof path, "%s/%s", dir, name);
  if(!stbi_write_png(path, w, h, 4, px, w * 4)) {
    fprintf(stderr, "cannot write %s\n", path);
    mem_free(px);
    return -1;
  }
  printf("%s\n", path);
  mem_free(px);
  return 0;
}


/**
 * the card made a JPEG by main.c, under the path it keeps it by
 **/
static void
card_made(int n) {
  capture_t c;

  memset(&c, 0, sizeof c);
  snprintf(c.path, sizeof c.path, "/xps5-cards/card-%d.jpg", n);
  c.kind = CAPTURE_JPEG;
  c.size = 512000;
  c.time = NOW;
  ui_card_done(&c);
}


// what Grok answers in the preview: the post (in Portuguese already, so as
// it is), and the three lines of what the user missed
#define TRANSLATED "P\xc3\xb4r do sol na Ribeira ontem \xc3\xa0 noite. Obrigado a todos os que vieram ao passeio!"
#define CAUGHT_UP \
  "A @ritasousa acabou o jogo depois de 60 horas e diz que o final valeu cada minuto.\n" \
  "O @noticiasjogos resumiu a atualiza\xc3\xa7\xc3\xa3o da semana: modo de foto novo, menos carregamentos e " \
  "corre\xc3\xa7\xc3\xb5" "es no cooperativo.\n" \
  "A @martaf procura quem jogue logo \xc3\xa0 noite, e o @joaomatos chama malta para o torneio de sexta \xf0\x9f\x8f\x86"


/**
 * a post translated by Grok from its •••: asked in a conversation of its
 * own, Grok at it, its words; a failure and ✕ asking again; the same post
 * opened again without asking
 **/
static int
translate_scenes(const char *dir) {
  char id[64], user[64], question[X_GROK_INPUT + 64], game[GAME_NAME], author[64];
  int fresh = 0, what = -1;

  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_GROK ||
     ui_grok_get(question, sizeof question, game, sizeof game, &fresh, &what, author, sizeof author) <= 0 ||
     what != UI_GROK_TRANSLATE || !fresh || !author[0] || game[0]) {
    return fail("Translate with Grok does not ask Grok");
  }
  if(draw(dir, "preview-translate-asking.png")) {
    return 1;
  }
  // it fails, and ✕ asks again
  ui_action_done(ERR_HTTP, 503, -1);
  if(draw(dir, "preview-translate-failed.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_GROK ||
     ui_grok_get(question, sizeof question, game, sizeof game, &fresh, &what, author, sizeof author) <= 0 ||
     what != UI_GROK_TRANSLATE) {
    return fail("✕ does not ask Grok again for the translation");
  }
  ui_grok_answer(TRANSLATED);
  ui_action_done(ERR_NONE, 200, -1);
  if(draw(dir, "preview-translate.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_NONE) {
    return fail("closing the translation asks for something");
  }
  return 0;
}


/**
 * More's "What you missed": Grok sums up the newest posts of Following (only
 * two came since last time), its card while it reads them and with its
 * three lines
 **/
static int
catchup_scenes(const char *dir) {
  char id[64], user[64], question[X_GROK_INPUT + 64], game[GAME_NAME], author[64];
  int fresh = 0, what = -1, lines = 0;

  if(point_at(120, 788)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(draw(dir, "preview-more.png") || point_at(200, 402)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_GROK ||
     ui_grok_get(question, sizeof question, game, sizeof game, &fresh, &what, author, sizeof author) <= 0 ||
     what != UI_GROK_CATCHUP || !fresh || author[0] || question[0] != '@') {
    return fail("What you missed does not ask Grok");
  }
  for(const char *s=question; s; s=strchr(s + 1, '\n')) {
    lines++;
  }
  if(lines < 3 || lines > X_CATCHUP_MOST || strstr(question, "\n\n")) {
    return fail("What you missed does not send a line a post");
  }
  if(draw(dir, "preview-catchup-asking.png")) {
    return 1;
  }
  ui_grok_answer(CAUGHT_UP);
  ui_action_done(ERR_NONE, 200, -1);
  if(draw(dir, "preview-catchup.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);
  return 0;
}


/**
 * writing on the phone, from the composer the voice note left (its text and
 * its MP4): the phone's button, its window with the QR, the words as they
 * come, ✕ putting them in the post, ○ keeping what was there, and the
 * page's Send
 **/
static int
phone_scenes(const char *dir) {
  static const char typed[] = "Escrito no telem\xc3\xb3vel: o boss final cai com tr\xc3\xaas saltos "
                              "e um ataque carregado \xf0\x9f\x8e\xae";
  char draft[4001], what[8], text[X_REPLY_MAX + 1], before[X_REPLY_MAX + 1];
  capture_t media[UI_MEDIA_MAX];
  ui_phone_t st;
  unsigned serial;
  int max = 0;

  ui_keys(UI_OPTIONS); // the draft comes back, on Post
  if(ui_compose_get(before, sizeof before, media, UI_MEDIA_MAX) != 1 || !before[0]) {
    return fail("the composer does not keep the voice note's draft");
  }
  ui_keys(UI_LEFT); // the message's button, then the phone's
  ui_keys(UI_LEFT);
  settled();
  if(draw(dir, "preview-phone-button.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  serial = ui_phone_wanted(draft, sizeof draft, &max, what, sizeof what);
  if(!serial || strcmp(draft, before) || strcmp(what, "post") || max < 280) {
    return fail("the phone's button does not open its window with the draft");
  }
  memset(&st, 0, sizeof st);
  st.state = UI_PHONE_STARTING;
  ui_phone_status(&st);
  settled();
  if(draw(dir, "preview-phone-starting.png")) {
    return 1;
  }
  st.state = UI_PHONE_WAITING;
  snprintf(st.ip, sizeof st.ip, "192.168.1.70");
  snprintf(st.token, sizeof st.token, "3f9a0c7e51b2d846");
  ui_phone_status(&st);
  settled();
  if(draw(dir, "preview-phone.png")) {
    return 1;
  }
  st.seen = 1;
  ui_phone_status(&st);
  ui_phone_text(typed, 0);
  settled();
  if(draw(dir, "preview-phone-typing.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_phone_wanted(draft, sizeof draft, &max, what, sizeof what) ||
     ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || strcmp(text, typed)) {
    return fail("\xe2\x9c\x95 does not put the phone's words in the post");
  }

  // again, let go of with ○: the post keeps its words
  ui_keys(UI_LEFT);
  ui_keys(UI_LEFT);
  ui_keys(UI_CROSS);
  if(ui_phone_wanted(draft, sizeof draft, &max, what, sizeof what) <= serial || strcmp(draft, typed)) {
    return fail("the phone's window does not open again, as a new one");
  }
  ui_phone_text("outra coisa", 0);
  ui_keys(UI_CIRCLE);
  if(ui_phone_wanted(draft, sizeof draft, &max, what, sizeof what) ||
     ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || strcmp(text, typed)) {
    return fail("\xe2\x97\x8b does not keep what the post had");
  }

  // the page's Send: the words go in, and the composer comes back
  ui_keys(UI_CROSS); // the phone's button keeps the focus
  if(!ui_phone_wanted(draft, sizeof draft, &max, what, sizeof what)) {
    return fail("\xe2\x9c\x95 on the phone's button does not open it");
  }
  ui_phone_text(before, 1);
  if(ui_phone_wanted(draft, sizeof draft, &max, what, sizeof what) ||
     ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || strcmp(text, before)) {
    return fail("the page's Send does not put its words in the post");
  }
  settled();
  if(draw(dir, "preview-phone-sent.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);
  return 0;
}


/**
 * a voice note: the composer's microphone button, its sheet taping (too
 * short at first, then the trail of its loudness, and another app with the
 * microphone), its card, and the MP4 in the post; then one let go of
 **/
static int
voice_scenes(const char *dir) {
  char id[X_ID_MAX], user[64], text[X_REPLY_MAX + 1];
  capture_t media[UI_MEDIA_MAX], c;
  float bars[CARD_VOICE_BARS];
  int retry = 0;

  ui_keys(UI_OPTIONS);
  if(ui_action(id, sizeof id, user, sizeof user) == UI_ACT_COMPOSE_TEXT) {
    ui_compose_text("A dica para o boss final \xf0\x9f\x8e\xae");
  }
  for(int i=0; i < 12; i++) { // the text, then the bar: photos, poll, microphone
    ui_keys(UI_UP);
  }
  for(int i=0; i < 3; i++) {
    ui_keys(UI_RIGHT);
  }
  settled();
  if(draw(dir, "preview-voice-button.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(!ui_voice_wanted(&retry) || retry) {
    return fail("the microphone button does not tape a voice note");
  }
  ui_voice_heard(UI_MIC_OPENING, 0, 0, 0, 0);
  settled();
  if(draw(dir, "preview-voice-opening.png")) {
    return 1;
  }
  ui_voice_heard(UI_MIC_ON, 0, 0, 0.4f, 8000); // half a second
  ui_keys(UI_CROSS);
  if(ui_voice_take() || !ui_voice_wanted(&retry)) {
    return fail("a voice note of half a second is made");
  }
  for(int i=0; i < 60; i++) {
    const float s = sinf(i * 0.55f);
    ui_elapsed(100000);
    ui_voice_heard(UI_MIC_ON, 0, 0, i % 9 == 8 ? 0.05f : 0.25f + 0.6f * s * s, (size_t)(16000 * 2 + i * 1600));
  }
  settled();
  if(draw(dir, "preview-voice.png")) {
    return 1;
  }
  ui_voice_heard(UI_MIC_ON, 0, UI_MIC_PRIORITY_LOW, 0, 16000 * 8);
  settled();
  if(draw(dir, "preview-voice-busy.png")) {
    return 1;
  }
  ui_voice_heard(UI_MIC_ON, 0, 0, 0.5f, 16000 * 8 + 4000);
  ui_keys(UI_CROSS);
  if(!ui_voice_take() || ui_voice_wanted(&retry)) {
    return fail("the cross does not stop the voice note");
  }
  for(int i=0; i < CARD_VOICE_BARS; i++) {
    const float s = sinf(i * 0.37f) * cosf(i * 0.11f);
    bars[i] = i % 11 == 10 ? 0.04f : 0.15f + 0.85f * s * s;
  }
  if(!ui_voice_card(bars, CARD_VOICE_BARS, 8)) {
    return fail("the voice note's card is not made");
  }
  settled();
  if(draw(dir, "preview-voice-making.png") || card_drawn(dir, "preview-card-voice.png")) {
    return 1;
  }
  memset(&c, 0, sizeof c);
  snprintf(c.path, sizeof c.path, "/xps5-cards/voice-4.mp4");
  c.kind = CAPTURE_MP4;
  c.size = 1500000;
  c.time = NOW;
  ui_card_done(&c);
  if(ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || strcmp(media[0].path, c.path) ||
     media[0].kind != CAPTURE_MP4 || !text[0]) {
    return fail("the voice note is not the post's video");
  }
  settled();
  if(draw(dir, "preview-voice-composer.png")) {
    return 1;
  }

  // another, let go of with ○ at once: no tape, the post keeps the first
  ui_keys(UI_LEFT); // from Post: the message's button, the phone's, dictation's, then the microphone
  ui_keys(UI_LEFT);
  ui_keys(UI_LEFT);
  ui_keys(UI_LEFT);
  ui_keys(UI_CROSS);
  if(!ui_voice_wanted(&retry)) {
    return fail("the microphone button is not reached again");
  }
  ui_keys(UI_CIRCLE);
  if(ui_voice_wanted(&retry) || ui_voice_take() || ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 ||
     strcmp(media[0].path, "/xps5-cards/voice-4.mp4")) {
    return fail("\xe2\x97\x8b does not let the voice note go");
  }
  ui_keys(UI_CIRCLE);
  return phone_scenes(dir);
}


/**
 * one dictation from the composer's button to ✕: the microphone heard for
 * seconds, then stopped; whether it was taken to be made
 **/
static int
dictate_once(float seconds) {
  int retry = 0;

  ui_keys(UI_CROSS);
  if(!ui_voice_wanted(&retry) || retry || !ui_voice_dictating()) {
    return 0;
  }
  ui_voice_heard(UI_MIC_ON, 0, 0, 0.5f, (size_t)(16000 * seconds));
  ui_keys(UI_CROSS);
  return ui_voice_take();
}


/**
 * dictation: the composer's button, its sheet listening, the words being
 * made on the console, and the words at the end of the post's text; then
 * one too short, one without its model, one stopped at its longest and let
 * go of while its words were made
 **/
static int
dictate_scene(const char *dir) {
  static const char said[] = "e depois salta para a plataforma da esquerda antes do segundo ataque";
  char id[X_ID_MAX], user[64], before[X_REPLY_MAX + 1], text[X_REPLY_MAX + 1], want[X_REPLY_MAX + 1];
  capture_t media[UI_MEDIA_MAX];
  int retry = 0;

  ui_keys(UI_OPTIONS);
  if(ui_action(id, sizeof id, user, sizeof user) == UI_ACT_COMPOSE_TEXT) {
    ui_compose_text("A dica para o boss final:");
  }
  if(ui_compose_get(before, sizeof before, media, UI_MEDIA_MAX) != 1) {
    return fail("the composer does not open for dictation");
  }
  for(int i=0; i < 12; i++) { // the text, then the draft's video and the bar: photos, poll, microphone, dictation
    ui_keys(UI_UP);
  }
  for(int i=0; i < 5; i++) {
    ui_keys(UI_RIGHT);
  }
  settled();
  if(draw(dir, "preview-dictate-button.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(!ui_voice_wanted(&retry) || retry || !ui_voice_dictating()) {
    return fail("the dictation button does not listen");
  }
  ui_voice_heard(UI_MIC_OPENING, 0, 0, 0, 0);
  ui_voice_heard(UI_MIC_ON, 0, 0, 0.4f, 8000); // half a second
  ui_keys(UI_CROSS);
  if(ui_voice_take() || !ui_voice_wanted(&retry)) {
    return fail("a dictation of half a second is made");
  }
  for(int i=0; i < 50; i++) {
    const float v = sinf(i * 0.7f);
    ui_elapsed(100000);
    ui_voice_heard(UI_MIC_ON, 0, 0, i % 7 == 6 ? 0.04f : 0.2f + 0.7f * v * v, (size_t)(16000 + i * 1600));
  }
  settled();
  if(draw(dir, "preview-dictate.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(!ui_voice_take() || ui_voice_wanted(&retry) || !ui_voice_dictating()) {
    return fail("the cross does not stop the dictation to be made");
  }
  settled();
  if(draw(dir, "preview-dictate-making.png")) {
    return 1;
  }
  ui_dictated(DICT_OK, said);
  snprintf(want, sizeof want, "%s%s%s", before, before[0] ? " " : "", said);
  if(ui_voice_dictating() || ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || strcmp(text, want)) {
    return fail("the dictation's words do not go at the end of the post");
  }
  settled();
  if(draw(dir, "preview-dictate-done.png")) {
    return 1;
  }

  // without its model: the post as it was, and why
  if(!dictate_once(3)) {
    return fail("the dictation button is not reached again");
  }
  ui_dictated(DICT_NO_MODEL, NULL);
  if(ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || strcmp(text, want)) {
    return fail("a dictation without its model changes the post");
  }
  settled();
  if(draw(dir, "preview-dictate-no-model.png")) {
    return 1;
  }

  // at its longest it stops by itself; let go of while made, its words do
  // not come
  ui_keys(UI_CROSS);
  ui_voice_heard(UI_MIC_ON, 0, 0, 0.5f, (size_t)16000 * DICT_MAX_S);
  if(!ui_voice_take()) {
    return fail("a dictation does not stop at its longest");
  }
  ui_keys(UI_CIRCLE);
  if(ui_voice_dictating()) {
    return fail("\xe2\x97\x8b does not let the dictation go");
  }
  ui_dictated(DICT_OK, "isto n\xc3\xa3o");
  if(ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || strcmp(text, want)) {
    return fail("a dictation let go of still writes");
  }
  ui_keys(UI_CIRCLE);
  return 0;
}


/**
 * a post that shares a Space: its ••• hears it, the top line says so while
 * it plays (and the rest goes on), and the ••• stops it
 **/
static int
space_scene(const char *dir) {
  char id[X_ID_MAX], user[64];
  ui_request_t req;
  x_post_t *list;
  x_space_t s;

  for(int i=0; i < 4; i++) { // the sheets and pages the scenes before left open
    ui_keys(UI_CIRCLE);
  }
  for(int i=0; i < 20; i++) { // For you, the first section
    ui_keys(UI_LEFT);
  }
  ui_keys(UI_SQUARE);
  if(want_feed(UI_FOR_YOU, &req) || !(list=mem_calloc(2, sizeof *list))) {
    return fail("For you is not read again for the Space");
  }
  for(int i=0; i < 2; i++) {
    x_post_t *p = &list[i];
    p->kind = X_KIND_POST;
    snprintf(p->id, sizeof p->id, "%d", 3900 + i);
    snprintf(p->user, sizeof p->user, "%s", i ? "joaomarques" : "astroclube");
    snprintf(p->user_id, sizeof p->user_id, "%d", 900 + i);
    snprintf(p->name, sizeof p->name, "%s", i ? "Jo\xc3\xa3o Marques" : "Clube ASTRO BOT");
    snprintf(p->text, sizeof p->text, "%s", i ? "Algu\xc3\xa9m para o modo cooperativo logo \xc3\xa0 noite?" :
             "Estamos no ar: truques para os n\xc3\xadveis secretos. Entra e pergunta! x.com/i/spaces/1OdKrBnaEPXKX");
    p->time = NOW - 600 * (i + 1);
    p->replies = 12;
    p->reposts = 4;
    p->likes = 61;
    p->views = 2300;
    p->bookmarks = -1;
  }
  snprintf(list[0].space, sizeof list[0].space, "%s", "1OdKrBnaEPXKX");
  ui_result(&req, list, 2, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  frame();
  if(point_at(1342, 277)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  settled();
  if(draw(dir, "preview-space-menu.png")) {
    return 1;
  }
  ui_keys(UI_UP); // the pointer goes away; Listen to the Space is the first row
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_SPACE || strcmp(id, "1OdKrBnaEPXKX") ||
     strcmp(user, "astroclube")) {
    return fail("the \xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2 does not ask for the Space");
  }
  // as main.c does once X said where its sound is
  memset(&s, 0, sizeof s);
  snprintf(s.id, sizeof s.id, "%s", id);
  snprintf(s.host, sizeof s.host, "%s", user);
  snprintf(s.title, sizeof s.title, "%s", "Segredos do ASTRO BOT");
  s.state = X_SPACE_LIVE;
  space_play("https://prod-fastly-eu-west-3.video.pscp.tv/Transcoding/v1/hls/a/playlist_16.m3u8", 1, id, user);
  ui_space(&s);
  ui_action_done(ERR_NONE, 200, -1);
  settled();
  if(draw(dir, "preview-space.png")) {
    return 1;
  }
  if(g_space_state != SPACE_ON || !g_space_live) {
    return fail("the Space does not play");
  }
  // its ••• again: Stop the Space
  frame();
  if(point_at(1342, 277)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_UP);
  ui_keys(UI_CROSS);
  if(g_space_state != SPACE_IDLE || ui_action(id, sizeof id, user, sizeof user) != UI_ACT_NONE) {
    return fail("the \xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2 does not stop the Space");
  }
  settled();
  return draw(dir, "preview-space-stopped.png");
}


/**
 * Notifications with two accounts linked: its tabs, this account's or
 * every account's, the latter in one list with a stripe of each account's
 * colour and whose colour is whose on top
 **/
static int later_scene(const char *dir);

static int
inbox_scene(const char *dir) {
  enum { TAB_Y = 90, THIS_X = 880, EVERY_X = 1060 };
  static const char *const likes[] = {"analopes", "hugoreis"};
  static const char *const follow[] = {"joaomatos"};
  static const char *const reposts[] = {"ritasousa"};
  ui_request_t req;
  x_post_t *list;

  for(int i=0; i < 4; i++) { // the sheets and pages the scenes before left open
    ui_keys(UI_CIRCLE);
  }
  ui_accounts(3); // accounts 1 and 2 have a session
  ui_open_told(1);
  if(want_feed(UI_NOTIFICATIONS, &req) || req.latest || !(list=mem_calloc(2, sizeof *list))) {
    return fail("Notifications is not read with two accounts");
  }
  notice(&list[0], X_ICON_LIKE, 20 * 60, "Ana Lopes e Hugo Reis gostaram do teu post", "Platina no ASTRO BOT!",
         likes, 2);
  notice(&list[1], X_ICON_FOLLOW, 2 * 3600, "Jo\xc3\xa3o Matos come\xc3\xa7ou a seguir-te", NULL, follow, 1);
  ui_result(&req, list, 2, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  settled();
  if(draw(dir, "preview-inbox-tabs.png")) {
    return 1;
  }
  if(point_at(EVERY_X, TAB_Y)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_NOTIFICATIONS || !req.latest || req.cursor[0]) {
    return fail("Every account's notifications are not read");
  }
  // as main.c merges them: by time, each with its account
  if(!(list=mem_calloc(4, sizeof *list))) {
    return fail("no memory");
  }
  notice(&list[0], X_ICON_LIKE, 20 * 60, "Ana Lopes e Hugo Reis gostaram do teu post", "Platina no ASTRO BOT!",
         likes, 2);
  list[0].account = 1;
  notice(&list[1], X_ICON_REPOST, 50 * 60, "Rita Sousa repostou o teu post", "Clip do boss final, sem cortes",
         reposts, 1);
  list[1].account = 2;
  notice(&list[2], X_ICON_FOLLOW, 2 * 3600, "Jo\xc3\xa3o Matos come\xc3\xa7ou a seguir-te", NULL, follow, 1);
  list[2].account = 1;
  post(&list[3], "Hugo Reis", "hugoreis", X_BADGE_NONE, 3 * 3600,
       "@xps5_segunda bora jogar Rocket League logo \xc3\xa0 noite?", 1, 0, 3, 120);
  list[3].account = 2;
  ui_result(&req, list, 4, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  settled();
  if(draw(dir, "preview-inbox.png")) {
    return 1;
  }
  // and this account's again
  if(point_at(THIS_X, TAB_Y)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_NOTIFICATIONS || req.latest) {
    return fail("this account's notifications are not read again");
  }
  ui_result(&req, NULL, 0, "", NULL, ERR_EMPTY, 200, -1, NOW);
  return later_scene(dir);
}


/**
 * a post kept for later from its •••, in History's second tab, and taken
 * out of it again
 **/
static int
later_scene(const char *dir) {
  enum { HISTORY_X = 170, HISTORY_Y = 596, TAB_Y = 90, LATER_X = 1080 };
  const x_post_t *kept;
  ui_request_t req;
  x_post_t *list;
  int changed, n;

  for(int i=0; i < 20; i++) { // For you, the first section
    ui_keys(UI_LEFT);
  }
  ui_keys(UI_SQUARE);
  if(want_feed(UI_FOR_YOU, &req) || !(list=mem_calloc(1, sizeof *list))) {
    return fail("For you is not read again for Read later");
  }
  post(&list[0], "Marta Fonseca", "martaf", X_BADGE_NONE, 40 * 60,
       "Guia completo dos n\xc3\xadveis secretos do ASTRO BOT, com mapas de cada mundo. Vale a pena ler com calma.",
       30, 80, 900, 12000);
  ui_result(&req, list, 1, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  frame();
  if(point_at(1342, 277)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_UP);   // Open the post, Quote, Translate with Grok, Read later
  for(int i=0; i < 3; i++) {
    ui_keys(UI_DOWN);
  }
  ui_keys(UI_CROSS);
  n = ui_later_get(&kept, &changed);
  if(n != 1 || !changed || strcmp(kept[0].user, "martaf")) {
    return fail("Read later does not keep the post");
  }
  // History, and its Read later tab
  if(point_at(HISTORY_X, HISTORY_Y)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(point_at(LATER_X, TAB_Y)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  settled();
  if(draw(dir, "preview-later.png")) {
    return 1;
  }
  // its ••• there: Remove from Read later
  frame();
  if(point_at(1342, 277)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_UP);
  for(int i=0; i < 3; i++) {
    ui_keys(UI_DOWN);
  }
  ui_keys(UI_CROSS);
  if(ui_later_get(&kept, &changed) != 0 || !changed) {
    return fail("Remove from Read later does not take it out");
  }
  settled();
  return draw(dir, "preview-later-empty.png");
}


/**
 * the game's corner: its box on its tab of Home, the trophies' sheet and
 * Grok's tip for one, and the cards of a session, of the week and of a
 * platinum in the composer
 **/
static int
game_corner_scenes(const char *dir) {
  static const ui_trophy_row_t rows[] = {
    {1, 1, 0, 1, "Primeiros passos", "Acaba o primeiro n\xc3\xadvel."},
    {0, 3, 0, 0, "Mestre dos Bots", "Ganha todos os outros trof\xc3\xa9us."},
    {2, 2, 0, 1, "Bot Master", "Salva 300 bots."},
    {3, 1, 0, 0, "Salto perfeito", "Acaba o n\xc3\xadvel do gelo sem cair."},
    {4, 0, 1, 0, "", ""},
    {5, 0, 0, 1, "Colecionador", "Encontra 20 pe\xc3\xa7" "as do puzzle."},
  };
  char id[X_ID_MAX], user[64], text[X_REPLY_MAX + 1], question[X_GROK_QUESTION], game[GAME_NAME];
  capture_t media[UI_MEDIA_MAX];
  ui_request_t req;
  session_t s;
  week_t w;
  x_post_t *list;
  int fresh = 0;

  for(int i=0; i < 4; i++) { // the sheets and pages the scenes before left open
    ui_keys(UI_CIRCLE);
  }
  ui_game("PPSA07399", "ASTRO BOT");
  for(int i=0; i < 20; i++) { // For you, the first section
    ui_keys(UI_LEFT);
  }
  ui_keys(UI_RIGHT);
  ui_keys(UI_RIGHT); // Home's tab of the game
  if(ui_wanted(&req) == 1 && req.feed == UI_GAME && (list=mem_calloc(6, sizeof *list))) {
    ui_result(&req, list, make_game_feed(list), "", NULL, ERR_NONE, 200, -1, NOW);
  }
  // the box, before the trophies came and with them
  if(draw(dir, "preview-game-side-reading.png")) {
    return 1;
  }
  ui_trophies("PPSA07399", rows, (int)(sizeof rows / sizeof rows[0]));
  if(draw(dir, "preview-game-side.png")) {
    return 1;
  }
  // the week's button asks main.c for the week
  if(point_at(1662, 291)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(!ui_week_wanted() || ui_week_wanted()) {
    return fail("the week's button does not ask for the week once");
  }
  // the trophies' button: their sheet, read again, the ones to get first
  if(point_at(1662, 231)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(!ui_trophies_wanted()) {
    return fail("the trophies' sheet does not read them again");
  }
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  settled();
  if(draw(dir, "preview-trophies.png")) {
    return 1;
  }
  // ✕: Grok's tip for the first one still to get, asked at once on its page
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_GROK ||
     grok_get(question, sizeof question, game, sizeof game, &fresh) <= 0 || !fresh || strcmp(game, "ASTRO BOT") ||
     strcmp(question, "Como consigo o trof\xc3\xa9u \xe2\x80\x9cMestre dos Bots\xe2\x80\x9d em ASTRO BOT? Diz: "
                      "\xe2\x80\x9cGanha todos os outros trof\xc3\xa9us.\xe2\x80\x9d")) {
    return fail("the trophy's tip is not asked of Grok");
  }
  settled();
  if(draw(dir, "preview-grok-trophy.png")) {
    return 1;
  }
  ui_grok_answer("O Mestre dos Bots chega sozinho quando tiveres todos os outros: falta-te o Salto perfeito e um escondido.");
  ui_action_done(ERR_NONE, 200, -1);
  settled();
  if(draw(dir, "preview-grok-trophy-answer.png")) {
    return 1;
  }

  // a session that ended: its card, then the composer with it first
  memset(&s, 0, sizeof s);
  snprintf(s.id, sizeof s.id, "PPSA07399");
  snprintf(s.name, sizeof s.name, "ASTRO BOT");
  s.start = NOW - 2 * 3600 - 13 * 60;
  s.end = NOW;
  s.trophies[0] = 3;
  s.trophies[1] = 1;
  s.trophies[2] = 1;
  s.shots = 5;
  snprintf(s.best, sizeof s.best, "Bot Master");
  snprintf(s.pics[0], sizeof s.pics[0], "/user/av_contents/photo/NPXS40087/PPSA07399/014/ASTRO BOT_20261115.jpg");
  s.pic_count = 1;
  ui_session(&s);
  if(card_drawn(dir, "preview-card-session.png")) {
    return 1;
  }
  card_made(1);
  if(ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) < 1 || strcmp(media[0].path, "/xps5-cards/card-1.jpg") ||
     strncmp(text, "2 h 13 min de ASTRO BOT \xc2\xb7 5 trof\xc3\xa9us \xf0\x9f\x8f\x86", 40) || !strstr(text, "#ASTROBOT")) {
    return fail("the session's card is not in the composer");
  }
  settled();
  if(draw(dir, "preview-card-composer.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);

  // the week: four games, the most played first
  memset(&w, 0, sizeof w);
  {
    static const struct { const char *id, *name; int hours, minutes, trophies; } g[] = {
      {"PPSA07399", "ASTRO BOT", 4, 10, 6}, {"PPSA01325", "Gran Turismo 7", 6, 45, 3},
      {"PPSA01399", "Uncharted: Drake's Fortune", 1, 20, 0}, {"PPSA00001", "Returnal", 0, 40, 1},
    };
    for(size_t i=0; i < sizeof g / sizeof g[0]; i++) {
      session_t one;
      memset(&one, 0, sizeof one);
      snprintf(one.id, sizeof one.id, "%s", g[i].id);
      snprintf(one.name, sizeof one.name, "%s", g[i].name);
      one.start = NOW - 86400;
      one.end = one.start + g[i].hours * 3600 + g[i].minutes * 60;
      one.trophies[0] = g[i].trophies;
      week_add(&w, &one);
      week_add(&w, &one); // twice each
    }
  }
  ui_week(&w);
  if(card_drawn(dir, "preview-card-week.png")) {
    return 1;
  }
  card_made(2);
  if(ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || strcmp(media[0].path, "/xps5-cards/card-2.jpg") ||
     strncmp(text, "A minha semana na PS5: 25 h 50 min em 4 jogos \xc2\xb7 20 trof\xc3\xa9us", 55) ||
     strstr(text, "2 h 13 min de")) {
    return fail("the week's card is not alone in the composer");
  }
  ui_keys(UI_CIRCLE);

  // a platinum: its line at once, and its card after
  ui_trophy(3, "Mestre dos Bots");
  if(card_drawn(dir, "preview-card-platinum.png")) {
    return 1;
  }
  card_made(3);
  if(ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 1 || strcmp(media[0].path, "/xps5-cards/card-3.jpg") ||
     strncmp(text, "Trof\xc3\xa9u de platina desbloqueado", 30)) {
    return fail("the platinum's card is not in the composer");
  }
  settled();
  if(draw(dir, "preview-card-platinum-composer.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);

  // looking for a group: a post of it in the composer, and the posts of
  // others looking for one in Explore
  for(int i=0; i < 20; i++) {
    ui_keys(UI_LEFT);
  }
  ui_keys(UI_RIGHT);
  ui_keys(UI_RIGHT);
  for(int i=0; i < 10 && ui_wanted(&req) == 1; i++) { // the sections on the way, and the tab kept
    ui_result(&req, NULL, 0, "", NULL, ERR_EMPTY, 200, -1, NOW);
  }
  if(point_at(1662, 351)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) < 0 ||
     strncmp(text, "\xc3\x80 procura de grupo para ASTRO BOT", 34) || !strstr(text, "#LFG") ||
     !strstr(text, "#ASTROBOT") || strstr(text, "platina")) {
    return fail("the button does not write a post looking for a group");
  }
  settled();
  if(draw(dir, "preview-lfg-post.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);
  if(point_at(1662, 411)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(want_feed(UI_SEARCH, &req) || !req.latest || !strstr(req.query, "\"ASTRO BOT\"") || !strstr(req.query, "#LFG") ||
     !strstr(req.query, "#ASTROBOT")) {
    return fail("the button does not look for groups in Explore");
  }
  // the groups found, where R1 invites a post's author to the game that
  // runs (the invite itself is the friends' scenes'; the account here has
  // no number by now, after its switches)
  list = mem_calloc(6, sizeof *list);
  ui_result(&req, list, make_game_feed(list), "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  settled();
  if(draw(dir, "preview-lfg-groups.png")) {
    return 1;
  }
  return voice_scenes(dir) || dictate_scene(dir) || space_scene(dir) || inbox_scene(dir);
}


int
main(int argc, char **argv) {
  x_post_t *posts = mem_calloc(16, sizeof *posts), *more, first;
  capture_t captures[CAPTURES_MAX], media[UI_MEDIA_MAX];
  char id[X_ID_MAX], user[64], text[X_REPLY_MAX + 1];
  settings_t settings, changed;
  ui_request_t req;
  x_user_t profile;
  int count, x, y;
  float pan;

  if(argc != 2 || !posts) {
    fprintf(stderr, "usage: preview <out-dir>\n");
    return 2;
  }
  images_init(64 << 20);
  settings_default(&settings);
  settings.lang = LANG_SYSTEM; // the PS5's: Portuguese
  snprintf(settings.users[1], sizeof settings.users[1], "%s", "xps5_segunda"); // a second account's name
  // Following was left at its third post the last time: "You left off here" over it
  snprintf(settings.seen[0], sizeof settings.seen[0], "3002,3003,3004");
  if(getenv("PREVIEW_SCALE")) {
    g_scale = (float)atof(getenv("PREVIEW_SCALE"));
    g_scale = g_scale < 1 ? 1 : g_scale > 2 ? 2 : g_scale;
    g_pw = (int)floorf(UI_WIDTH * g_scale + 0.5f);
    g_ph = (int)floorf(UI_HEIGHT * g_scale + 0.5f);
  }
  workers_init(3); // the backdrop and the copies in parts, as on the PS5
  ui_scale(g_scale);
  if(ui_init(&settings, LANG_PT)) {
    return fail("fonts do not open");
  }
  ui_session_info(1, "/data/TwitterPS5/session.txt");

  // the first frame, while Following loads
  if(ui_wanted(&req) != 1 || req.feed != UI_FOLLOWING || req.cursor[0] || req.profile ||
     draw(argv[1], "preview-loading.png")) {
    return fail("Following is not wanted first");
  }

  // X's error 326 in an answer without posts: the account locked; □ reads
  // Following again
  ui_result(&req, NULL, 0, "", NULL, ERR_EMPTY, 200, 326, NOW - 130);
  if(draw(argv[1], "preview-locked.png")) {
    return 1;
  }
  ui_keys(UI_SQUARE);
  if(want_feed(UI_FOLLOWING, &req)) {
    return fail("□ does not read Following again after error 326");
  }

  count = make_posts(posts);
  first = posts[0];
  ui_result(&req, posts, count, "DAABCgABG2", NULL, ERR_NONE, 200, -1, NOW - 120);
  // then who is signed in, once, as x.com reads its Viewer
  if(ui_wanted(&req) != 1 || req.feed != UI_ME || !req.profile || req.id[0] || req.cursor[0]) {
    return fail("the signed-in user is not asked for");
  }
  more = mem_calloc(1, sizeof *more);
  more[0] = first;
  make_me(&profile);
  ui_result(&req, more, 1, "", &profile, ERR_NONE, 200, -1, NOW - 100);
  // and the side column's trends, as x.com's side column reads them, then who to follow
  if(ui_wanted(&req) != 1 || req.side != UI_SIDE_TRENDS || req.query[0] || req.cursor[0] || req.profile) {
    return fail("the side column's trends are not asked for");
  }
  more = mem_calloc(8, sizeof *more);
  ui_result(&req, more, make_trends(more), "", NULL, ERR_NONE, 200, -1, NOW - 90);
  if(ui_wanted(&req) != 1 || req.side != UI_SIDE_WHO) {
    return fail("who to follow is not asked for");
  }
  more = mem_calloc(4, sizeof *more);
  ui_result(&req, more, make_people(more), "", NULL, ERR_NONE, 200, -1, NOW - 80);
  // and who of the people the user follows plays now
  if(ui_wanted(&req) != 1 || req.side != UI_SIDE_PLAYING || strcmp(req.id, profile.id) || req.cursor[0]) {
    return fail("who plays now is not asked for");
  }
  more = mem_calloc(6, sizeof *more);
  ui_result(&req, more, make_friends(more), "", NULL, ERR_NONE, 200, -1, NOW - 70);
  if(ui_wanted(&req)) {
    return fail("more posts wanted at the top");
  }
  // and the posts on top now are the next time's
  if(!ui_settings_changed(&changed) || strcmp(changed.seen[0], "3000,3001,3002")) {
    return fail("where Following was left is not kept");
  }
  if(draw(argv[1], "preview-timeline.png")) {
    return 1;
  }

  // R2 takes back the like of the first post at once; L2 replies, and the toast says so
  ui_keys(UI_R2);
  ui_keys(UI_R2); // nothing while the like goes out
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_UNLIKE || strcmp(id, "3000") ||
     strcmp(user, "ritasousa") || ui_action(id, sizeof id, user, sizeof user) != UI_ACT_NONE) {
    return fail("R2 does not take the like back");
  }
  ui_action_done(ERR_NONE, 200, -1);
  // L2 opens the composer for a reply, whose keyboard comes up at once
  ui_keys(UI_L2);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_COMPOSE_TEXT || strcmp(id, "3000") ||
     strcmp(user, "ritasousa")) {
    return fail("L2 does not open the composer's keyboard");
  }
  ui_compose_text("Que corrida! Amanh\xc3\xa3 tento bater esse tempo \xf0\x9f\x8f\x81");
  if(draw(argv[1], "preview-compose-reply.png")) {
    return 1;
  }
  ui_keys(UI_R2);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_REPLY || strcmp(id, "3000") ||
     ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX) != 0 || strncmp(text, "Que corrida!", 12)) {
    return fail("R2 does not send the reply");
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(draw(argv[1], "preview-reply.png")) {
    return 1;
  }
  // liking it again fails: the heart goes back to grey and the toast is red
  ui_keys(UI_R2);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_LIKE) {
    return fail("R2 again does not like it");
  }
  ui_action_done(ERR_HTTP, 200, 226);
  if(draw(argv[1], "preview-like-error.png")) {
    return 1;
  }
  for(int i=0; i<300; i++) {
    ui_animate();
  }

  ui_keys(UI_DOWN);
  ui_keys(UI_DOWN);
  if(draw(argv[1], "preview-quote.png")) {
    return 1;
  }
  // the video plays where the post is, without its sound, as on x.com
  ui_keys(UI_DOWN);
  if(draw(argv[1], "preview-video-inline.png")) {
    return 1;
  }
  // its sound button turns the sound on there, without opening the viewer,
  // and off again
  if(point_at(1255, 880)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(!g_sound || draw(argv[1], "preview-video-sound.png")) {
    return fail("the timeline's sound button does not turn the sound on");
  }
  ui_keys(UI_CROSS);
  if(g_sound) {
    return fail("the timeline's sound button does not turn the sound off");
  }
  // opened, the same MP4 goes on over the whole screen, 1080p pixel for
  // pixel, with its controls over it
  if(point_at(900, 650)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(draw(argv[1], "preview-video-full.png") || g_video_w != 1920 || g_video_h != 1080 ||
     !strstr(g_video, "/1920x1080/")) {
    fprintf(stderr, "%dx%d %s\n", g_video_w, g_video_h, g_video);
    return fail("the video does not fill the screen");
  }
  // after a few seconds without the buttons or the stick, the picture alone
  for(int i=0; i<4; i++) {
    ui_elapsed(1000000);
  }
  ui_elapsed(16667); // and frames of a sixtieth of a second again
  frame();
  ui_pointer(&x, &y);
  if(x >= 0 || draw(argv[1], "preview-video-clean.png")) {
    return fail("the video's controls and the pointer do not go away");
  }
  ui_keys(UI_CIRCLE);
  ui_keys(UI_DOWN);
  if(draw(argv[1], "preview-more.png")) {
    return 1;
  }

  // the end of the list: the posts after it load, and X asks to wait
  for(int i=0; i<40; i++) {
    ui_keys(UI_DOWN);
  }
  if(ui_wanted(&req) != 1 || req.feed != UI_FOLLOWING || strcmp(req.cursor, "DAABCgABG2") ||
     draw(argv[1], "preview-end.png")) {
    return fail("no more posts wanted at the end");
  }
  ui_result(&req, NULL, 0, "", NULL, ERR_HTTP, 429, 88, NOW);
  if(ui_wanted(&req) || draw(argv[1], "preview-end-error.png")) {
    return 1;
  }
  ui_keys(UI_DOWN);
  if(ui_wanted(&req) != 1 || !req.cursor[0]) {
    return fail("down at the end does not try again");
  }
  // the next page: a post already shown, which stays out, and a new one
  more = mem_calloc(2, sizeof *more);
  more[0] = first;
  post(&more[1], "Jo\xc3\xa3o Matos", "joaomatos", X_BADGE_NONE, 2 * 86400,
       "Sexta h\xc3\xa1 torneio no servidor do costume. Quem alinha? \xf0\x9f\x8f\x86", 12, 2, 31, 0);
  ui_result(&req, more, 2, "", NULL, ERR_NONE, 200, -1, NOW);
  for(int i=0; i<10; i++) {
    ui_keys(UI_DOWN);
  }
  if(ui_wanted(&req) || draw(argv[1], "preview-end-none.png")) {
    return 1;
  }

  // the pointer on the bell names it, and ✕ opens the notifications; it
  // ticks once, on the left, as the bell lights up, and not again while it
  // stays on it
  ui_hover_take(&pan);
  if(point_at(120, 276) || !ui_hover_take(&pan) || pan > -0.2f) {
    return fail("no tick as the pointer lights the bell");
  }
  if(point_at(123, 279) || ui_hover_take(&pan) || draw(argv[1], "preview-pointer.png") ||
     ui_hover_take(&pan)) {
    return fail("the bell ticks again");
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_NOTIFICATIONS || req.profile) {
    return fail("the bell does not open the notifications");
  }
  more = mem_calloc(8, sizeof *more);
  count = make_notices(more);
  ui_result(&req, more, count, "NOTIF+1", NULL, ERR_NONE, 200, -1, NOW);
  if(draw(argv[1], "preview-notifications.png")) {
    return 1;
  }

  // the glass on the bar: Explore, with X's trends
  if(point_at(120, 212)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_NONE || ui_wanted(&req) != 1 ||
     req.feed != UI_SEARCH || req.query[0] || req.latest || req.cursor[0]) {
    return fail("the glass does not read the trends");
  }
  if(draw(argv[1], "preview-explore-loading.png")) {
    return 1;
  }
  more = mem_calloc(8, sizeof *more);
  count = make_trends(more);
  ui_result(&req, more, count, "TRENDS+1", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away, and the second trend
  if(draw(argv[1], "preview-explore.png")) {
    return 1;
  }
  // its News tab
  if(point_at(1109, 90)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_SEARCH || req.query[0] || !req.latest) {
    return fail("News is not read");
  }
  more = mem_calloc(4, sizeof *more);
  count = make_news(more);
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  if(draw(argv[1], "preview-explore-news.png")) {
    return 1;
  }
  // ✕ on a story: its top posts; ○ back to Explore's trends
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_SEARCH || strcmp(req.query, "State of Play") || req.latest) {
    return fail("a story is not searched for");
  }
  ui_result(&req, NULL, 0, "", NULL, ERR_EMPTY, 200, -1, NOW);
  ui_keys(UI_CIRCLE);
  if(ui_wanted(&req) != 1 || req.feed != UI_SEARCH || req.query[0] || req.latest) {
    return fail("○ does not go back to the trends");
  }
  ui_result(&req, NULL, 0, "", NULL, ERR_EMPTY, 200, -1, NOW);
  if(draw(argv[1], "preview-search-empty.png")) {
    return 1;
  }

  // △ asks for words, and what was typed is looked for
  ui_keys(UI_TRIANGLE);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_SEARCH || id[0] || user[0] || ui_wanted(&req)) {
    return fail("△ does not ask for the search keyboard");
  }
  ui_search("  ps5 pro ");
  if(ui_wanted(&req) != 1 || req.feed != UI_SEARCH || strcmp(req.query, "ps5 pro") || req.latest ||
     req.cursor[0] || req.profile || draw(argv[1], "preview-search-loading.png")) {
    return fail("the search is not read");
  }
  more = mem_calloc(6, sizeof *more);
  count = make_search(more);
  ui_result(&req, more, count, "DAADDAABCgAB", NULL, ERR_NONE, 200, -1, NOW);
  if(draw(argv[1], "preview-search.png")) {
    return 1;
  }

  // the pointer on the first post's bookmark keeps it, at once on the screen
  if(point_at(1350, 860)) { // the bookmark, last of the buttons
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_BOOKMARK || !id[0] ||
     strcmp(user, "hugoreis")) {
    return fail("the bookmark does not keep the post");
  }
  ui_action_done(ERR_NONE, 200, -1);
  ui_keys(UI_UP); // the pointer goes away
  if(draw(argv[1], "preview-bookmark.png")) {
    return 1;
  }
  // the latest posts instead of the top ones, of which there are none
  if(point_at(1093, 90)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_SEARCH || strcmp(req.query, "ps5 pro") || !req.latest ||
     req.cursor[0]) {
    return fail("Latest is not read");
  }
  ui_result(&req, NULL, 0, "", NULL, ERR_EMPTY, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away
  if(draw(argv[1], "preview-search-none.png")) {
    return 1;
  }
  // △ at the top of the search asks for other words
  ui_keys(UI_TRIANGLE);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_SEARCH) {
    return fail("△ does not ask for other words");
  }
  ui_action_done(ERR_CANCELLED, 0, -1);
  // ○ goes back to Explore, then to the timeline; the first post's author
  // opens their profile
  ui_keys(UI_CIRCLE);
  ui_keys(UI_CIRCLE);
  ui_keys(UI_TRIANGLE);
  frame();
  if(point_at(472, 288)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_USER || strcmp(req.id, first.user_id) || !req.profile) {
    return fail("the avatar does not open the profile");
  }
  if(draw(argv[1], "preview-profile-loading.png")) {
    return 1;
  }
  more = mem_calloc(4, sizeof *more);
  count = make_profile(more, &profile);
  ui_result(&req, more, count, "", &profile, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_UP); // the pointer goes away
  settle();
  if(draw(argv[1], "preview-profile.png")) {
    return 1;
  }
  // the pointer in the banner's colour, over Refresh
  if(point_at(1290, 90) || draw(argv[1], "preview-pointer-banner.png")) {
    return 1;
  }
  ui_keys(UI_UP);

  // the first post's picture, over the whole screen
  ui_keys(UI_CIRCLE);
  frame();
  if(point_at(600, 600)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(draw(argv[1], "preview-viewer.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);

  // ✕ without the pointer: the post with its replies
  ui_keys(UI_TRIANGLE);
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_POST || strcmp(req.id, "3000") || req.profile) {
    return fail("✕ does not open the post");
  }
  more = mem_calloc(6, sizeof *more);
  count = make_thread(more, &first);
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
  if(draw(argv[1], "preview-post.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);

  // the options button writes a post of the user's own, with captures of
  // the PS5 from the gallery
  ui_keys(UI_OPTIONS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_COMPOSE_TEXT || id[0] || user[0]) {
    return fail("options does not open the composer's keyboard");
  }
  ui_compose_text("Finalmente platinado \xf0\x9f\x8f\x86 #AstroBot #PS5Share");
  ui_keys(UI_TRIANGLE);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_CAPTURES ||
     draw(argv[1], "preview-gallery-loading.png")) {
    return fail("△ does not ask for the captures");
  }
  count = make_captures(captures);
  ui_game("PPSA01316", "Gran Turismo\xe2\x84\xa2 7"); // its captures first
  ui_captures(captures, count, ERR_NONE, 0);
  ui_keys(UI_DOWN);                  // the pointer goes away
  ui_keys(UI_UP);
  ui_keys(UI_CROSS);                 // the newest picture
  ui_keys(UI_RIGHT);
  ui_keys(UI_CROSS);                 // a video, which goes alone: it takes the picture's place
  ui_keys(UI_RIGHT);
  ui_keys(UI_RIGHT);
  ui_keys(UI_CROSS);                 // a picture, which takes the video's
  ui_keys(UI_DOWN);
  ui_keys(UI_LEFT);
  ui_keys(UI_CROSS);                 // another
  ui_keys(UI_UP);
  ui_keys(UI_LEFT);
  ui_keys(UI_LEFT);
  ui_keys(UI_CROSS);                 // and the newest again
  if(draw(argv[1], "preview-gallery.png")) {
    return 1;
  }
  ui_keys(UI_R2);
  if((count=ui_compose_get(text, sizeof text, media, UI_MEDIA_MAX)) != 3 ||
     draw(argv[1], "preview-compose.png")) {
    fprintf(stderr, "%d captures picked\n", count);
    return fail("the gallery does not give the composer three pictures");
  }
  ui_keys(UI_R2);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_POST || id[0] || user[0]) {
    return fail("R2 does not post");
  }
  ui_progress(5200000, 13800000);
  if(draw(argv[1], "preview-compose-sending.png")) {
    return 1;
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(draw(argv[1], "preview-posted.png")) {
    return 1;
  }
  for(int i=0; i<300; i++) {
    ui_animate();
  }

  // More on the bar: its menu, and the Settings in it; English, and a
  // minute between readings
  if(point_at(120, 788)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(draw(argv[1], "preview-more-menu.png") || point_at(200, 706)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_DOWN); // the pointer goes away, and the second row
  ui_keys(UI_UP);
  if(draw(argv[1], "preview-settings.png")) {
    return 1;
  }
  ui_keys(UI_RIGHT);                 // the PS5's (Portuguese) -> English
  if(!ui_settings_changed(&changed) || changed.lang != LANG_EN || ui_lang() != LANG_EN ||
     ui_settings_changed(&changed)) {
    return fail("the language does not change");
  }
  if(draw(argv[1], "preview-settings-en.png") || link_in_language(argv[1], "preview-link-en.png")) {
    return 1;
  }
  for(int i=0; i<4; i++) {           // Portuguese, Spanish, French, German
    ui_keys(UI_RIGHT);
  }
  if(ui_lang() != LANG_DE || draw(argv[1], "preview-settings-de.png")) {
    return fail("German is not there");
  }
  if(link_in_language(argv[1], "preview-link-de.png")) {
    return 1;
  }
  for(int i=0; i<5; i++) {           // back to the PS5's
    ui_keys(UI_LEFT);
  }
  if(!ui_settings_changed(&changed) || changed.lang != LANG_SYSTEM || ui_lang() != LANG_PT) {
    return fail("the PS5's language does not come back");
  }
  ui_keys(UI_DOWN);                  // the theme: the night
  ui_keys(UI_RIGHT);
  if(!ui_settings_changed(&changed) || changed.theme != SETTINGS_NIGHT) {
    return fail("the night does not come");
  }
  if(draw(argv[1], "preview-settings-night.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);
  if(draw(argv[1], "preview-night.png")) {
    return 1;
  }
  if(point_at(120, 788)) {           // and the day again
    return 1;
  }
  ui_keys(UI_CROSS);
  if(point_at(200, 706)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_DOWN);
  ui_keys(UI_RIGHT);
  if(!ui_settings_changed(&changed) || changed.theme != SETTINGS_DAY) {
    return fail("the day does not come back");
  }
  // the resolution, below the theme: from Auto to 1080p, back, and round
  // the other way to 4K, which the app takes when it opens again
  ui_keys(UI_DOWN);
  ui_keys(UI_RIGHT);
  if(!ui_settings_changed(&changed) || changed.screen != 1080) {
    return fail("the resolution does not go to 1080p");
  }
  ui_keys(UI_LEFT);
  ui_keys(UI_LEFT);
  if(!ui_settings_changed(&changed) || changed.screen != 2160 || draw(argv[1], "preview-settings-4k.png")) {
    return fail("the resolution does not go round to 4K");
  }
  ui_keys(UI_RIGHT);
  if(!ui_settings_changed(&changed) || changed.screen != 0) {
    return fail("the resolution does not come back to Auto");
  }
  ui_keys(UI_UP);
  // the pointer's sound: a tick as the pointer lights its row, none on
  // the row above once it is off, and on again
  if(point_at(800, 663) || !ui_hover_take(&pan)) {
    return fail("no tick on the pointer sound's row");
  }
  ui_keys(UI_CROSS);
  if(!ui_settings_changed(&changed) || changed.hover || draw(argv[1], "preview-settings-hover-off.png") ||
     point_at(800, 585) || ui_hover_take(&pan)) {
    return fail("the pointer's sound does not go off");
  }
  if(point_at(800, 663)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(!ui_settings_changed(&changed) || !changed.hover || point_at(800, 585) || !ui_hover_take(&pan)) {
    return fail("the pointer's sound does not come back");
  }
  ui_keys(UI_CIRCLE);

  if(catchup_scenes(argv[1])) {
    return 1;
  }

  // More's scheduled posts: loading, the list, the question and one taken back
  if(point_at(120, 788)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(point_at(200, 630)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.side != UI_SIDE_SCHEDULED) {
    return fail("the scheduled posts are not read");
  }
  if(draw(argv[1], "preview-scheduled-loading.png")) {
    return 1;
  }
  more = mem_calloc(8, sizeof *more);
  count = make_scheduled(more);
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away, and the second is chosen
  if(draw(argv[1], "preview-scheduled-list.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(draw(argv[1], "preview-scheduled-confirm.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_UNSCHEDULE || strcmp(id, "1900000000000000002")) {
    return fail("Delete does not take back the scheduled post");
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(draw(argv[1], "preview-scheduled-deleted.png")) {
    return 1;
  }
  ui_keys(UI_SQUARE); // read again: none left
  if(ui_wanted(&req) != 1 || req.side != UI_SIDE_SCHEDULED) {
    return fail("□ does not read the scheduled posts again");
  }
  ui_result(&req, NULL, 0, "", NULL, ERR_EMPTY, 200, -1, NOW);
  if(draw(argv[1], "preview-scheduled-empty.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);

  // More's Lists: the user's lists, and a list's posts
  if(point_at(120, 788)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(point_at(200, 554)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_LISTS || req.id[0] || req.query[0]) {
    return fail("Lists is not read");
  }
  more = mem_calloc(4, sizeof *more);
  count = make_lists(more);
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  if(draw(argv[1], "preview-lists.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_LIST || strcmp(req.id, "1500")) {
    return fail("a list does not open");
  }
  more = mem_calloc(6, sizeof *more);
  count = make_search(more);
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
  if(draw(argv[1], "preview-list.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);
  ui_keys(UI_CIRCLE);

  // the bar's Chat: the user's conversations
  if(point_at(120, 340)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_CHAT || strcmp(req.id, profile.id) || req.profile ||
     !(more=mem_calloc(8, sizeof *more))) {
    return fail("Chat does not read the user's conversations");
  }
  count = make_chats(more);
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  if(ui_wanted(&req) || draw(argv[1], "preview-chat.png")) {
    return 1;
  }
  // ✕ opens João's, the newest message at the bottom
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_DM || strcmp(req.id, "1-77") || !(more=mem_calloc(12, sizeof *more))) {
    return fail("the conversation is not read");
  }
  count = make_messages(more, profile.id);
  ui_result(&req, more, count, "", NULL, ERR_NONE, 200, -1, NOW);
  if(ui_wanted(&req) || draw(argv[1], "preview-dm.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  {
    char game[GAME_NAME];
    if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_LAUNCH || strcmp(id, "PPSA07399") ||
       ui_launch_get(id, sizeof id, game, sizeof game) || strcmp(id, "PPSA07399") || strcmp(game, "ASTRO BOT")) {
      return fail("the cross on an invite does not open its game");
    }
  }
  ui_action_done(ERR_NO_GAME, 0, -1);
  if(draw(argv[1], "preview-invite-no-game.png")) {
    return 1;
  }
  // L2 writes, and the message goes to it
  ui_keys(UI_L2);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_DM_TEXT || strcmp(id, "1-77") ||
     strcmp(user, "joaomarques")) {
    return fail("L2 does not ask for the keyboard of a message");
  }
  ui_dm_text("Combinado, \xc3\xa0s 22h!");
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_DM || strcmp(id, "1-77") ||
     ui_dm_get(text, sizeof text) != (int)strlen("Combinado, \xc3\xa0s 22h!")) {
    return fail("the message is not sent");
  }
  // its bubble at once, while it goes; nothing is read meanwhile
  if(ui_wanted(&req) || draw(argv[1], "preview-dm-sending.png")) {
    return fail("the conversation is read while a message goes");
  }
  // it did not go: its bubble says so, and ✕ on it writes it again
  ui_action_done(ERR_NETWORK, 0, -1);
  settled();
  if(ui_wanted(&req) || draw(argv[1], "preview-dm-failed.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_DM_TEXT || strcmp(id, "1-77") ||
     ui_dm_get(text, sizeof text) != (int)strlen("Combinado, \xc3\xa0s 22h!")) {
    return fail("the cross on a message that did not go does not write it again");
  }
  ui_dm_text("Combinado, \xc3\xa0s 22h!");
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_DM || strcmp(id, "1-77")) {
    return fail("the message is not sent again");
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(ui_dm_get(text, sizeof text) || ui_wanted(&req) != 1 || req.feed != UI_DM ||
     !(more=mem_calloc(12, sizeof *more))) {
    return fail("the conversation is not read again");
  }
  count = make_messages(more, profile.id);
  more[count] = more[3]; // the user's
  snprintf(more[count].id, sizeof more[count].id, "5099");
  snprintf(more[count].text, sizeof more[count].text, "Combinado, \xc3\xa0s 22h!");
  more[count].time = NOW;
  ui_result(&req, more, count + 1, "", NULL, ERR_NONE, 200, -1, NOW);
  if(draw(argv[1], "preview-dm-sent.png") || psn_scenes(argv[1], profile.id)) {
    return 1;
  }
  ui_keys(UI_CIRCLE);
  // History: the posts opened, newest first
  if(point_at(120, 596)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_DOWN); // the pointer goes away
  ui_keys(UI_UP);
  if(ui_wanted(&req) || draw(argv[1], "preview-history.png")) {
    return fail("History reads something");
  }
  // what main.c keeps of it between runs, and gives back at the start
  {
    const x_post_t *kept;
    x_post_t *copy;
    int changed, n = ui_history_get(&kept, &changed), again;

    if(n < 1 || n > UI_HISTORY_MAX || !changed || ui_history_get(&kept, &again) != n || again ||
       !(copy=mem_calloc(n + 1, sizeof *copy))) {
      return fail("History is not given to be kept");
    }
    memcpy(copy, kept, (size_t)n * sizeof *copy);
    copy[n] = copy[0];
    copy[n].kind = X_KIND_TREND; // not a post: left out
    ui_history_set(copy, n + 1);
    if(ui_history_get(&kept, &changed) != n || changed || strcmp(kept[0].id, copy[0].id)) {
      return fail("History does not come back");
    }
    mem_free(copy);
  }
  // Articles: the signed-in user's
  if(point_at(120, 660)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_ARTICLES || strcmp(req.id, profile.id) || req.profile) {
    return fail("Articles are not the user's");
  }
  ui_result(&req, NULL, 0, "", NULL, ERR_EMPTY, 200, -1, NOW);
  if(draw(argv[1], "preview-articles.png")) {
    return 1;
  }

  // Home, and the ••• of the user's own post: its menu, Delete, the
  // question, and the post goes
  if(point_at(120, 148)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_TRIANGLE);
  frame();
  if(point_at(1342, 277)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(draw(argv[1], "preview-menu.png")) {
    return 1;
  }
  ui_keys(UI_UP); // the pointer goes away; Delete is the first row
  ui_keys(UI_CROSS);
  if(draw(argv[1], "preview-confirm.png")) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_DELETE || strcmp(id, "3000")) {
    return fail("Delete does not delete the post");
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(draw(argv[1], "preview-deleted.png")) {
    return 1;
  }
  for(int i=0; i<300; i++) {
    ui_animate();
  }

  // the ••• of someone else's post: Follow, and the toast
  frame();
  if(point_at(1342, 315)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  if(draw(argv[1], "preview-menu-other.png")) {
    return 1;
  }
  ui_keys(UI_UP);   // Open the post, Quote, Translate with Grok
  ui_keys(UI_DOWN);
  ui_keys(UI_DOWN);
  ui_keys(UI_CROSS);
  if(translate_scenes(argv[1])) {
    return 1;
  }
  frame();
  if(point_at(1342, 315)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_UP);   // Open the post, Quote, Translate with Grok, Read later, Follow
  ui_keys(UI_DOWN);
  ui_keys(UI_DOWN);
  ui_keys(UI_DOWN);
  ui_keys(UI_DOWN);
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_FOLLOW || !id[0] || !user[0]) {
    return fail("Follow does not follow");
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(draw(argv[1], "preview-followed.png")) {
    return 1;
  }
  for(int i=0; i<300; i++) {
    ui_animate();
  }
  // Quote: the composer with the post under the words
  frame();
  if(point_at(1342, 315)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_UP);
  ui_keys(UI_DOWN);
  ui_keys(UI_CROSS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_COMPOSE_TEXT) {
    return fail("Quote does not open the composer");
  }
  ui_compose_text("Isto \xc3\xa9 mesmo assim \xf0\x9f\x91\x80");
  ui_keys(UI_DOWN); // the pointer goes away
  if(draw(argv[1], "preview-compose-quote.png")) {
    return 1;
  }
  ui_keys(UI_R2);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_QUOTE || !id[0] || !user[0]) {
    return fail("the quote is not sent");
  }
  ui_action_done(ERR_NONE, 200, -1);
  for(int i=0; i<300; i++) {
    ui_animate();
  }
  // a post for later: R1 picks when (X Premium), R2 schedules it
  ui_keys(UI_OPTIONS);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_COMPOSE_TEXT) {
    return fail("the composer does not open");
  }
  ui_compose_text("Live de GTA VI hoje \xc3\xa0s 22h, venham ver! \xf0\x9f\x8e\xae");
  ui_keys(UI_R1); // in 30 min
  ui_keys(UI_R1); // in an hour
  if(ui_schedule_delay() != 3600 || draw(argv[1], "preview-compose-schedule.png")) {
    return fail("R1 does not schedule the post");
  }
  ui_keys(UI_R2);
  if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_SCHEDULE || id[0] || ui_schedule_delay() != 3600) {
    return fail("the post is not scheduled");
  }
  ui_action_done(ERR_NONE, 200, -1);
  if(ui_schedule_delay() || draw(argv[1], "preview-scheduled.png")) {
    return fail("the composer keeps the time");
  }
  for(int i=0; i<300; i++) {
    ui_animate();
  }
  // a poll: the bar's button, two choices written, a third added, three
  // days, and R2 posts it
  {
    char labels[X_POLL_MAX][X_POLL_LABEL];
    int minutes;

    ui_keys(UI_OPTIONS);
    if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_COMPOSE_TEXT) {
      return fail("the composer does not open for the poll");
    }
    ui_compose_text("Qual \xc3\xa9 o melhor exclusivo da PS5? \xf0\x9f\x8e\xae");
    ui_keys(UI_LEFT); // from Post, past the phone's, dictation's and the voice note's buttons, to the poll's
    ui_keys(UI_LEFT);
    ui_keys(UI_LEFT);
    ui_keys(UI_LEFT);
    ui_keys(UI_CROSS);
    if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_POLL_TEXT || ui_poll_choice(text, sizeof text) != 1) {
      return fail("the poll does not start with its first choice");
    }
    ui_poll_text("Astro Bot");
    ui_keys(UI_DOWN);
    ui_keys(UI_CROSS);
    if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_POLL_TEXT || ui_poll_choice(text, sizeof text) != 2) {
      return fail("the second choice is not written");
    }
    ui_poll_text("Gran Turismo 7");
    ui_keys(UI_DOWN); // adding one
    ui_keys(UI_CROSS);
    if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_POLL_TEXT || ui_poll_choice(text, sizeof text) != 3) {
      return fail("a third choice is not added");
    }
    ui_poll_text("  Ghost of Y\xc5\x8dtei\n");
    ui_keys(UI_DOWN); // adding one
    ui_keys(UI_DOWN); // how long
    ui_keys(UI_RIGHT); // three days
    if(draw(argv[1], "preview-compose-poll.png")) {
      return 1;
    }
    ui_keys(UI_R2);
    if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_POST || ui_poll_get(labels, &minutes) != 3 ||
       minutes != 3 * 24 * 60 || strcmp(labels[2], "Ghost of Y\xc5\x8dtei")) {
      return fail("the poll is not posted");
    }
    ui_action_done(ERR_NONE, 200, -1);
    if(ui_poll_get(labels, &minutes)) {
      return fail("the composer keeps the poll");
    }
    for(int i=0; i<300; i++) {
      ui_animate();
    }
  }
  // their profile, from the menu: Follow, and that they follow the user
  frame();
  if(point_at(1342, 315)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  ui_keys(UI_UP);
  for(int i=0; i<5; i++) { // past Translate with Grok and Read later, to the profile
    ui_keys(UI_DOWN);
  }
  ui_keys(UI_CROSS);
  if(ui_wanted(&req) != 1 || req.feed != UI_USER || !req.profile) {
    return fail("the menu does not open the profile");
  }
  {
    x_user_t other;
    memset(&other, 0, sizeof other);
    snprintf(other.id, sizeof other.id, "%.23s", req.id);
    snprintf(other.user, sizeof other.user, "%s", user[0] ? user : "joaomarques");
    snprintf(other.name, sizeof other.name, "Jo\xc3\xa3o Marques");
    snprintf(other.bio, sizeof other.bio, "Trof\xc3\xa9us, speedruns e muito caf\xc3\xa9.");
    snprintf(other.location, sizeof other.location, "\xf0\x9f\x8e\xae ASTRO BOT \xc2\xb7 PS5");
    other.following = 180;
    other.followers = 920;
    other.posts = 3400;
    other.follows_you = 1;
    snprintf(other.banner, sizeof other.banner, "https://pbs.twimg.com/profile_banners/2/joaomarques");
    more = mem_calloc(4, sizeof *more);
    count = make_polls(more, other.id, other.user, other.name);
    ui_result(&req, more, count, "", &other, ERR_NONE, 200, -1, NOW);
  }
  ui_keys(UI_UP);
  settle();
  if(draw(argv[1], "preview-profile-other.png")) {
    return 1;
  }
  // the pointer in this banner's colours: resting, and over Refresh
  if(point_at(1100, 90) || draw(argv[1], "preview-pointer-teal-idle.png") ||
     point_at(1290, 90) || draw(argv[1], "preview-pointer-teal.png")) {
    return 1;
  }
  ui_keys(UI_UP); // the pointer goes away
  // their posts with polls: one open, one the user voted in that closed
  ui_keys(UI_DOWN);
  ui_keys(UI_DOWN);
  for(int i=0; i<300; i++) {
    ui_animate();
  }
  settle();
  if(draw(argv[1], "preview-poll.png")) {
    return 1;
  }
  // the pointer on Gran Turismo 7: the vote, at once on the screen
  if(point_at(947, 267)) {
    return 1;
  }
  ui_keys(UI_CROSS);
  {
    char card[48], name[32];
    if(ui_action(id, sizeof id, user, sizeof user) != UI_ACT_VOTE || strcmp(id, "3101") ||
       ui_vote_get(card, sizeof card, name, sizeof name) != 2 || strcmp(card, "card://1900000000000000001") ||
       strcmp(name, "poll3choice_text_only")) {
      return fail("the pointer does not vote");
    }
  }
  ui_action_done(ERR_NONE, 200, -1);
  ui_keys(UI_UP); // the pointer goes away
  ui_keys(UI_DOWN);
  if(draw(argv[1], "preview-poll-voted.png")) {
    return 1;
  }
  ui_keys(UI_CIRCLE);

  // For you, which the session did not get
  ui_keys(UI_LEFT);
  if(ui_wanted(&req) != 1 || req.feed != UI_FOR_YOU) {
    return fail("← does not go to For you");
  }
  ui_result(&req, NULL, 0, "", NULL, ERR_HTTP, 403, -1, 0);
  if(draw(argv[1], "preview-error.png")) {
    return 1;
  }
  if(link_scenes(argv[1])) {
    return 1;
  }
  return extras(argv[1]) || share_scenes(argv[1]) || account_scenes(argv[1]) || login_scenes(argv[1]) ||
         clip_scene(argv[1]) || motion_scenes(argv[1]) || game_corner_scenes(argv[1]);
}
