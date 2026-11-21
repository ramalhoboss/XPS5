/* The cards XPS5 makes for a post: a session of play that just ended, the
week's play, a trophy, and the picture of a voice note's video. Each is drawn into a surface of
CARD_W x CARD_H points at CARD_SCALE (1800 x 1013 pixels, the 16:9 X shows
whole), from the game's icon and a screenshot when they came, and turned into
a JPEG by the title. The words come in the screen's language (lang.h). */

#pragma once

#include <stdint.h>

#include "sessions.h"
#include "text.h"

#define CARD_W     1200
#define CARD_H     675
#define CARD_SCALE 1.5f
#define CARD_ICON  150 // points of a game's icon on a session's card
#define CARD_ROW_ICON 84 // of each game on the week's
#define CARD_BIG_ICON 230 // on a trophy's
#define CARD_GAMES 3   // games the week's card names
#define CARD_VOICE_AVATAR 120 // points of the speaker's picture on a voice note's
#define CARD_VOICE_GAME   52  // and of the game's icon
#define CARD_VOICE_BARS   64  // its bars of loudness

enum { CARD_NONE, CARD_SESSION, CARD_WEEK, CARD_TROPHY, CARD_VOICE };

enum { CARD_F_HUGE, CARD_F_BIG, CARD_F_MID, CARD_F_TEXT, CARD_F_SMALL, CARD_F_LABEL, CARD_FONTS };

// what a card is drawn with
typedef struct card_look {
  text_font_t    *fonts[CARD_FONTS]; // opened at CARD_SCALE (card_font_specs)
  int             lang;              // LANG_*
  const uint32_t *photo;  // a screenshot covering the card (its pixels), or NULL
  const uint32_t *icons[CARD_GAMES]; // the games' icons at their size's pixels, or NULL
} card_look_t;

// a font of a card: bold or not, and its height in points
typedef struct card_font_spec {
  int   bold;
  float px;
} card_font_spec_t;

extern const card_font_spec_t card_font_specs[CARD_FONTS];

/**
 * the card's surface over pixels, which hold card_pixels_w() x
 * card_pixels_h()
 **/
text_surface_t card_surface(uint32_t *pixels);
int card_pixels_w(void);
int card_pixels_h(void);

/**
 * a session's card: the game, how long, the trophies and screenshots of it
 * and its best trophy; icons[0] is the game's
 **/
void card_session(const text_surface_t *dst, const card_look_t *look, const session_t *s);

/**
 * the week's card: the time played, the sessions, games and trophies, and
 * the most played games with icons[i] (w sorted, week_sort)
 **/
void card_week(const text_surface_t *dst, const card_look_t *look, const week_t *w);

/**
 * a trophy's card, in the colour of its grade (0 bronze to 3 platinum): the
 * game (icons[0] its icon), the trophy's name and, when all is above 0, how
 * many of all the game's trophies the user has
 **/
void card_trophy(const text_surface_t *dst, const card_look_t *look, int grade, const char *game, const char *trophy,
                 int earned, int all);

/**
 * a voice note's picture: who speaks (icons[0] their picture, at
 * CARD_VOICE_AVATAR), how long, count bars of its loudness (0 to 1) and the
 * game it was said over, if any (icons[1] its icon, at CARD_VOICE_GAME)
 **/
void card_voice(const text_surface_t *dst, const card_look_t *look, const char *name, const char *handle,
                const char *game, int seconds, const float *bars, int count);
