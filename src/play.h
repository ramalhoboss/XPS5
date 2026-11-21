/* "Playing now", on X itself: while the user plays, their profile's location
says the game ("🎮 ASTRO BOT · PS5"), and goes back to what it was when they
stop (the notifications payload does it, src/notify.c); the app reads the
same words in the profiles of the people it shows. An invite to play is a
direct message whose words end in the game's title id, for the friend's
XPS5 to open the game ("🎮 ASTRO BOT · let's play? (XPS5 PPSA01325)").
While XPS5 is open with no game, the location can say so instead
("🟢 On XPS5 · PS5", in the user's language), for the friends' XPS5 to say
that they came online. A friend who plays can be asked to let the user in,
as PSN's "Request to join" does: a message the friend's XPS5 knows, to
invite them back ("🙋 ASTRO BOT · can I join? (XPS5)"). */

#pragma once

#include <stddef.h>

#define PLAY_MARK     "\xf0\x9f\x8e\xae " // 🎮 and a space
#define PLAY_TAIL     " \xc2\xb7 PS5"
#define PLAY_ONLINE   "\xf0\x9f\x9f\xa2 " // 🟢 and a space: on XPS5, with no game
#define PLAY_XPS5_ID  "TWPS00001"         // XPS5's own title id: "on XPS5" where a game's would be
#define PLAY_JOIN     "\xf0\x9f\x99\x8b " // 🙋 and a space: a request to join a friend's game
#define PLAY_LOCATION 30  // what X takes in a profile's location, counted as UTF-16
#define PLAY_STATUS   128 // bytes of one, with room
#define PLAY_GAME     64  // bytes of a game's name read back from one
#define PLAY_INVITE   400 // bytes of an invite

/**
 * a text's length as UTF-16 counts it (an emoji 2, most characters 1)
 **/
int play_units(const char *s);

/**
 * the location that says the user plays game: PLAY_MARK, the name (cut
 * with "…" to fit PLAY_LOCATION), PLAY_TAIL; 0, or -1 without a name
 **/
int play_status(const char *game, char *out, size_t size);

/**
 * whether a profile's location says its user plays (play_status's shape),
 * and the game into game (cut at a whole character to fit size); 1 or 0
 **/
int play_game_of(const char *location, char *game, size_t size);

/**
 * the location that says the user is on XPS5: PLAY_ONLINE, words (the
 * user's language's "On XPS5", on one line, cut to fit), PLAY_TAIL; 0, or -1
 **/
int play_online_status(const char *words, char *out, size_t size);

/**
 * whether a profile's location says its user is on XPS5 (play_online_status's
 * shape, in any language); 1 or 0
 **/
int play_online_of(const char *location);

/**
 * an invite to play game (its title id, or NULL when it is not known), with
 * the words of the user's language (phrase, "let's play?"); 0 or -1
 **/
int play_invite(const char *game, const char *id, const char *phrase, char *out, size_t size);

/**
 * whether a message is an invite of play_invite's: the game into game and
 * its title id into id (empty when it has none); 1 or 0
 **/
int play_invite_parse(const char *text, char *game, size_t gsize, char *id, size_t isize);

/**
 * a request to join the game a friend plays, with the words of the user's
 * language (phrase, "can I join?"): PLAY_JOIN, the game, the phrase and
 * "(XPS5)"; 0 or -1
 **/
int play_join(const char *game, const char *phrase, char *out, size_t size);

/**
 * whether a message is a request of play_join's: the game into game; 1 or 0
 **/
int play_join_parse(const char *text, char *game, size_t gsize);

/**
 * the conversation of two users (their numbers), as X names a one-to-one
 * one: the smaller number, '-', the bigger; 0 or -1
 **/
int play_conversation(const char *me, const char *them, char *out, size_t size);
