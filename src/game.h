/* The game that runs beside the title: its name, from the PS5's own data
about it (the param.json of a PS5 game, the param.sfo of a PS4 one, under
/user/appmeta/<title id>/), and a hashtag made of it. The notifications
payload reads them, since it runs outside the title's sandbox, and answers
the title's "game" command with them (src/notify.c). */

#pragma once

#include <stddef.h>

#define GAME_ID   16  // "PPSA01325", with room
#define GAME_NAME 128

/**
 * whether id is a game's title id: four capitals and five digits, not the
 * system's (NPXS) and not this app's own
 **/
int game_id_ok(const char *id);

/**
 * the name in a PS5 game's param.json (text, len bytes, changed in place):
 * its titleName in the language lang ("pt-PT"), else in its default one,
 * else in English; on one line, cut to fit size; 0, or -1
 **/
int game_name_json(char *text, size_t len, const char *lang, char *name, size_t size);

/**
 * the applicationCategoryType of a PS5 app's param.json (text, len bytes,
 * changed in place): 0 for a game, 65536 for a media app (YouTube, XPS5
 * itself); -1 when it says none or is not JSON
 **/
int game_category_json(char *text, size_t len);

/**
 * the TITLE of a PS4 game's param.sfo; 0, or -1
 **/
int game_name_sfo(const unsigned char *buf, size_t len, char *name, size_t size);

/**
 * name as a hashtag ("ASTRO BOT" gives "#ASTROBOT"): its letters (also the
 * accented Latin ones) and digits; 0, or -1 when no letter is left
 **/
int game_hashtag(const char *name, char *tag, size_t size);

#define GAME_KEYS 12
#define GAME_KEY  64

// how a text has to write one of game_keys_t's
enum {
  GAME_KEY_ANY,      // in any case
  GAME_KEY_CAPITAL,  // with a capital, or as a hashtag: one word, which could be an everyday one
  GAME_KEY_CAPITALS, // in capitals, or as a hashtag: initials of letters alone
};

// the ways posts would name a game (see game_keys)
typedef struct game_keys {
  int  count;
  char key[GAME_KEYS][GAME_KEY]; // small letters without accents, the words one space apart
  int  how[GAME_KEYS];           // GAME_KEY_*
} game_keys_t;

/**
 * how posts about the game named name would name it: the whole name, without
 * its edition ("Director's Cut"), its number, its subtitle (after ":") or
 * whose it is ("Marvel's"), as one word (its hashtag) and its initials
 * ("TLOU2", "FF7"); none under 4 letters but initials of 3. Their number
 **/
int game_keys(const char *name, game_keys_t *k);

/**
 * whether text names the game of k as words of their own ("#EldenRing",
 * "elden ring's", "TLOU2", not "goldenring")
 **/
int game_named(const char *text, const game_keys_t *k);

/**
 * what X's search looks for to find the posts about the game named name
 * (Home's tab of the game that runs): its name as words or as a hashtag
 * ("astro bot" or #astrobot; a name of one word along with PS5 or game),
 * without the posts that say they have spoilers, and without replies; 0, or
 * -1 when there is no name or it does not fit in size
 **/
int game_query(const char *name, char *out, size_t size);

/**
 * whether text has words that tend to give a game away ("ending", "final
 * boss", "dies", in the languages of LANG_*), but not as words of the game's
 * own name (k, of game_keys): the game's tab covers those of its posts
 **/
int game_spoils(const char *text, const game_keys_t *k);

/**
 * the language of LANG_* (settings.h) as param.json names it ("pt-PT")
 **/
const char *game_locale(int lang);
