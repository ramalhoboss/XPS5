/* Draws the title's screen on a computer into PNG files, to see how it
looks without a PS5. The posts and the people in them are made up here, and
the pictures are drawn on the spot; nothing comes from X.
Run with tests/preview.sh <out-dir>. */

#define _DEFAULT_SOURCE

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "images.h"
#include "loginproto.h"
#include "mem.h"
#include "play.h"
#include "player.h"
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
 * the people the user follows, as Following gives them: three play now
 **/
static int
make_friends(x_post_t *posts) {
  static const struct { const char *user, *name, *game; int badge; } made[] = {
    {"joaomarques", "Jo\xc3\xa3o Marques", "ASTRO BOT", X_BADGE_BLUE},
    {"PlayStation", "PlayStation", "", X_BADGE_GOLD},
    {"anaplays", "Ana \xc2\xb7 speedruns e caf\xc3\xa9 com um nome bem comprido",
     "Marvel's Spider-Man\xe2\x80\xa6", X_BADGE_BLUE},
    {"pedrosilva", "Pedro Silva", "Gran Turismo 7", X_BADGE_NONE},
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
  return n;
}


/**
 * a conversation with João, oldest first: his, the user's (me) and his,
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
  }
  return n;
}


static int
fail(const char *what) {
  fprintf(stderr, "%s\n", what);
  return 1;
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
  for(int i=0; i<12; i++) { // to the captures' row
    ui_keys(UI_DOWN);
  }
  if(draw(dir, "preview-settings-more.png")) {
    return 1;
  }
  ui_keys(UI_DOWN); // what the profile says while a game runs
  if(draw(dir, "preview-settings-playing.png")) {
    return 1;
  }
  for(int i=0; i<14; i++) {
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
    // a gold trophy, before what the composer holds
    ui_trophy(2, "Bot Master");
    if(ui_compose_get(text, sizeof text, media, 4) != 1 ||
       strcmp(text, "Trof\xc3\xa9u de ouro desbloqueado: \xe2\x80\x9c" "Bot Master\xe2\x80\x9d\n#ASTROBOT")) {
      return fail("the trophy is not in the composer");
    }
    if(draw(dir, "preview-trophy.png")) {
      return 1;
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
  if(draw(dir, "preview-game-feed.png")) {
    return 1;
  }
  // the game stops: the tab stays while it is shown, and goes once it is left
  ui_game("", "");
  ui_keys(UI_LEFT);
  ui_keys(UI_RIGHT);
  if(want_feed(UI_SEARCH, &req)) {
    return fail("the game's tab stays without a game");
  }

  // Grok's hints without spoilers: asked with ✕, thinking, answered
  ui_game("PPSA07399", "ASTRO BOT");
  for(int i=0; i<4; i++) { // Explore, Notifications, Chat, Calls, Grok
    ui_keys(UI_RIGHT);
  }
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
       ui_grok_get(question, sizeof question, game, sizeof game, &fresh) <= 0 ||
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
    if(ui_grok_get(question, sizeof question, game, sizeof game, &fresh) <= 0 || fresh) {
      return fail("the second question starts a new conversation");
    }
    ui_keys(UI_SQUARE); // a new conversation
    if(ui_grok_get(question, sizeof question, game, sizeof game, &fresh) != 0 || !fresh) {
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
  for(int i=0; i<15; i++) { // to the last one
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
  enum { S_SESSION_ROW = 15 }; // the X account's row of Settings (ui.c, S_SESSION), below the language
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
 * the login window inside XPS5, opened by the cross over a session X
 * refused once the engine is on the console: starting, the page loading
 * and shown, the pointer's click, the keyboard asked for a field that is
 * not there, the next field, the page scrolled, the engine failing and □
 * to try again, and ○ closing it
 **/
static int
login_scenes(const char *dir) {
  enum { S_SESSION_ROW = 15 }; // the X account's row of Settings, below the language
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
  if(ui_wanted(&req) != 1 || req.feed != UI_DM || strcmp(req.id, "1-77") || !(more=mem_calloc(8, sizeof *more))) {
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
  ui_action_done(ERR_NONE, 200, -1);
  if(ui_dm_get(text, sizeof text) || ui_wanted(&req) != 1 || req.feed != UI_DM ||
     !(more=mem_calloc(8, sizeof *more))) {
    return fail("the conversation is not read again");
  }
  count = make_messages(more, profile.id);
  more[count] = more[3]; // the user's
  snprintf(more[count].id, sizeof more[count].id, "5099");
  snprintf(more[count].text, sizeof more[count].text, "Combinado, \xc3\xa0s 22h!");
  more[count].time = NOW;
  ui_result(&req, more, count + 1, "", NULL, ERR_NONE, 200, -1, NOW);
  if(draw(argv[1], "preview-dm-sent.png")) {
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
  ui_keys(UI_UP);   // Open the post, Quote, Follow
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
    ui_keys(UI_LEFT); // from Post to the poll's button
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
  for(int i=0; i<3; i++) {
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
  return extras(argv[1]) || share_scenes(argv[1]) || account_scenes(argv[1]) || login_scenes(argv[1]);
}
