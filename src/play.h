/* "Playing now", on X itself: while the user plays, their profile's location
says the game ("🎮 ASTRO BOT · PS5"), and goes back to what it was when they
stop (the notifications payload does it, src/notify.c); the app reads the
same words in the profiles of the people it shows. An invite to play is a
direct message whose words end in the game's title id, for the friend's
XPS5 to open the game ("🎮 ASTRO BOT · let's play? (XPS5 PPSA01325)"). */

#pragma once

#include <stddef.h>

#define PLAY_MARK     "\xf0\x9f\x8e\xae " // 🎮 and a space
#define PLAY_TAIL     " \xc2\xb7 PS5"
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
 * the conversation of two users (their numbers), as X names a one-to-one
 * one: the smaller number, '-', the bigger; 0 or -1
 **/
int play_conversation(const char *me, const char *them, char *out, size_t size);
