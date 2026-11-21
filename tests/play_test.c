/* Host tests of src/play.c: the profile's "playing" location, its "on
XPS5" one, invites to play and one-to-one conversations. */

#include <stdio.h>
#include <string.h>

#include "play.h"

static int g_failed;

#define CHECK(cond) do { \
  if(!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); g_failed = 1; } \
} while(0)

#define PAD "\xf0\x9f\x8e\xae " // 🎮 and a space


static void
test_status(void) {
  char out[PLAY_STATUS], game[PLAY_GAME];

  CHECK(play_units(PAD "a") == 4 && play_units("\xc3\xa9") == 1 && play_units("") == 0 && play_units(NULL) == 0);
  CHECK(!play_status("ASTRO BOT", out, sizeof out) && !strcmp(out, PAD "ASTRO BOT \xc2\xb7 PS5"));
  CHECK(play_game_of(out, game, sizeof game) == 1 && !strcmp(game, "ASTRO BOT"));
  // 20 letters and an accent fit; the location stays within X's 30
  CHECK(!play_status("Pok\xc3\xa9mon Legends: Z-A", out, sizeof out) && play_units(out) <= PLAY_LOCATION &&
        !strcmp(out, PAD "Pok\xc3\xa9mon Legends: Z-A \xc2\xb7 PS5"));
  // a long one is cut, with "…", and read back as it was cut
  CHECK(!play_status("Marvel's Spider-Man 2 Digital Deluxe Edition", out, sizeof out));
  CHECK(play_units(out) <= PLAY_LOCATION && !strcmp(out, PAD "Marvel's Spider-Man\xe2\x80\xa6 \xc2\xb7 PS5"));
  CHECK(play_game_of(out, game, sizeof game) == 1 && !strncmp(game, "Marvel's Spider-Man", 19));
  // its name on one line, without spaces around it
  CHECK(!play_status("  Gran\nTurismo 7 ", out, sizeof out) && !strcmp(out, PAD "Gran Turismo 7 \xc2\xb7 PS5"));
  // an emoji in the name counts 2
  CHECK(!play_status("\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80"
                     "\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80"
                     "\xf0\x9f\x9a\x80", out, sizeof out) && play_units(out) <= PLAY_LOCATION);
  CHECK(play_status("", out, sizeof out) == -1 && !out[0] && play_status("   ", out, sizeof out) == -1 &&
        play_status(NULL, out, sizeof out) == -1);
  CHECK(play_status("ASTRO BOT", out, 8) == -1 && !out[0]);

  // what is not a status
  CHECK(!play_game_of("Lisboa", game, sizeof game) && !game[0]);
  CHECK(!play_game_of(PAD "Twitch", game, sizeof game));
  CHECK(!play_game_of(PAD " \xc2\xb7 PS5", game, sizeof game));
  CHECK(!play_game_of(PAD "   \xc2\xb7 PS5", game, sizeof game) && !game[0]);
  CHECK(!play_game_of("", game, sizeof game) && !play_game_of(NULL, game, sizeof game));
  // a small buffer gets whole characters
  CHECK(play_game_of(PAD "\xc3\xa9\xc3\xa9\xc3\xa9 \xc2\xb7 PS5", game, 4) == 1 && !strcmp(game, "\xc3\xa9"));
}


#define DOT "\xf0\x9f\x9f\xa2 " // 🟢 and a space


static void
test_online(void) {
  char out[PLAY_STATUS], game[PLAY_GAME];

  // in the user's language, whatever it is, and read back in any
  CHECK(!play_online_status("Online on XPS5", out, sizeof out) && !strcmp(out, DOT "Online on XPS5 \xc2\xb7 PS5"));
  CHECK(play_online_of(out) && !play_game_of(out, game, sizeof game) && play_units(out) <= PLAY_LOCATION);
  CHECK(!play_online_status("En l\xc3\xadnea en XPS5", out, sizeof out) && play_online_of(out) &&
        play_units(out) <= PLAY_LOCATION);
  // on one line, and cut to fit on a whole character
  CHECK(!play_online_status("  Online\non XPS5 ", out, sizeof out) && !strcmp(out, DOT "Online on XPS5 \xc2\xb7 PS5"));
  CHECK(!play_online_status("Online on XPS5 and on a very long line", out, sizeof out) &&
        play_units(out) == PLAY_LOCATION && play_online_of(out));
  CHECK(!play_online_status("\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80"
                            "\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80\xf0\x9f\x9a\x80"
                            "\xf0\x9f\x9a\x80", out, sizeof out) && play_units(out) <= PLAY_LOCATION);
  CHECK(play_online_status("", out, sizeof out) == -1 && !out[0] && play_online_status("  ", out, sizeof out) == -1);
  CHECK(play_online_status(NULL, out, sizeof out) == -1 && play_online_status("Online", out, 8) == -1 && !out[0]);
  // what is not one: a game, another dot, no tail, the dot alone
  CHECK(!play_online_of(PAD "ASTRO BOT \xc2\xb7 PS5") && !play_online_of("\xf0\x9f\x94\xb4 Online \xc2\xb7 PS5"));
  CHECK(!play_online_of(DOT "Online") && !play_online_of(DOT "\xc2\xb7 PS5") && !play_online_of("Lisboa"));
  CHECK(!play_online_of("") && !play_online_of(NULL));
  // XPS5's own title id is not a game's
  CHECK(!strcmp(PLAY_XPS5_ID, "TWPS00001"));
}


static void
test_invite(void) {
  char out[PLAY_INVITE], game[PLAY_GAME], id[16];

  CHECK(!play_invite("ASTRO BOT", "PPSA01325", "bora jogar?", out, sizeof out) &&
        !strcmp(out, PAD "ASTRO BOT \xc2\xb7 bora jogar? (XPS5 PPSA01325)"));
  CHECK(play_invite_parse(out, game, sizeof game, id, sizeof id) == 1 && !strcmp(game, "ASTRO BOT") &&
        !strcmp(id, "PPSA01325"));
  // without a title id: the friend's game, which their profile named
  CHECK(!play_invite("Ghost of Y\xc5\x8dtei", NULL, "let's play?", out, sizeof out) &&
        !strcmp(out, PAD "Ghost of Y\xc5\x8dtei \xc2\xb7 let's play? (XPS5)"));
  CHECK(play_invite_parse(out, game, sizeof game, id, sizeof id) == 1 && !strcmp(game, "Ghost of Y\xc5\x8dtei") &&
        !id[0]);
  // brackets in the name, and a trailing line end
  CHECK(!play_invite("Tetris (2025)", "CUSA00001", "bora?", out, sizeof out));
  strcat(out, "\n");
  CHECK(play_invite_parse(out, game, sizeof game, id, sizeof id) == 1 && !strcmp(game, "Tetris (2025)") &&
        !strcmp(id, "CUSA00001"));
  CHECK(play_invite("ASTRO BOT", "ppsa01325", "bora?", out, sizeof out) == -1);
  CHECK(play_invite("ASTRO BOT", "PPSA0132", "bora?", out, sizeof out) == -1);
  CHECK(play_invite("A \xc2\xb7 B", NULL, "bora?", out, sizeof out) == -1);
  CHECK(play_invite("", NULL, "bora?", out, sizeof out) == -1 && play_invite("A", NULL, "", out, sizeof out) == -1);
  CHECK(play_invite("ASTRO BOT", "PPSA01325", "bora jogar?", out, 20) == -1 && !out[0]);

  // what is not one
  CHECK(!play_invite_parse(PAD "ASTRO BOT \xc2\xb7 bora jogar?", game, sizeof game, id, sizeof id));
  CHECK(!play_invite_parse(PAD "ASTRO BOT \xc2\xb7 bora jogar? (XPS5 PPSA0132)", game, sizeof game, id, sizeof id));
  CHECK(!play_invite_parse(PAD "ASTRO BOT \xc2\xb7 bora jogar? (XPS6 PPSA01325)", game, sizeof game, id, sizeof id));
  CHECK(!play_invite_parse(PAD "ASTRO BOT \xc2\xb7 (XPS5 PPSA01325) e mais", game, sizeof game, id, sizeof id));
  CHECK(!play_invite_parse("Ol\xc3\xa1 \xc2\xb7 bora? (XPS5 PPSA01325)", game, sizeof game, id, sizeof id));
  CHECK(!play_invite_parse(PAD " \xc2\xb7 bora? (XPS5)", game, sizeof game, id, sizeof id) && !game[0] && !id[0]);
  CHECK(!play_invite_parse("", game, sizeof game, id, sizeof id) && !play_invite_parse(NULL, game, sizeof game, id,
                                                                                       sizeof id));
}


static void
test_join(void) {
  char out[PLAY_INVITE], game[PLAY_GAME];

  CHECK(!play_join("ASTRO BOT", "posso entrar?", out, sizeof out) &&
        !strcmp(out, PLAY_JOIN "ASTRO BOT \xc2\xb7 posso entrar? (XPS5)"));
  CHECK(play_join_parse(out, game, sizeof game) == 1 && !strcmp(game, "ASTRO BOT"));
  CHECK(!play_join("Tetris (2025)", "can I join?", out, sizeof out));
  strcat(out, "\r\n");
  CHECK(play_join_parse(out, game, sizeof game) == 1 && !strcmp(game, "Tetris (2025)"));
  CHECK(play_join("A \xc2\xb7 B", "posso?", out, sizeof out) == -1 && !out[0]);
  CHECK(play_join("", "posso?", out, sizeof out) == -1 && play_join("A", "", out, sizeof out) == -1);
  CHECK(play_join(NULL, "posso?", out, sizeof out) == -1);
  CHECK(play_join("ASTRO BOT", "posso entrar?", out, 12) == -1 && !out[0]);

  // what is not one: an invite, a message without the tag or the dot
  CHECK(!play_join_parse(PAD "ASTRO BOT \xc2\xb7 bora jogar? (XPS5 PPSA01325)", game, sizeof game) && !game[0]);
  CHECK(!play_join_parse(PLAY_JOIN "ASTRO BOT \xc2\xb7 posso entrar?", game, sizeof game));
  CHECK(!play_join_parse(PLAY_JOIN "ASTRO BOT posso entrar? (XPS5)", game, sizeof game));
  CHECK(!play_join_parse(PLAY_JOIN " \xc2\xb7 posso? (XPS5)", game, sizeof game) && !game[0]);
  CHECK(!play_join_parse("", game, sizeof game) && !play_join_parse(NULL, game, sizeof game));
}


static void
test_conversation(void) {
  char out[64];

  CHECK(!play_conversation("123", "45", out, sizeof out) && !strcmp(out, "45-123"));
  CHECK(!play_conversation("45", "123", out, sizeof out) && !strcmp(out, "45-123"));
  CHECK(!play_conversation("1500", "1400", out, sizeof out) && !strcmp(out, "1400-1500"));
  CHECK(!play_conversation("1876543210987654321", "44196397", out, sizeof out) &&
        !strcmp(out, "44196397-1876543210987654321"));
  CHECK(play_conversation("12", "12", out, sizeof out) == -1 && !out[0]);
  CHECK(play_conversation("", "12", out, sizeof out) == -1 && play_conversation("12", "1a", out, sizeof out) == -1);
  CHECK(play_conversation("012", "13", out, sizeof out) == -1 && play_conversation(NULL, "13", out, sizeof out) == -1);
  CHECK(play_conversation("123", "45", out, 6) == -1 && !out[0]);
}


int
main(void) {
  test_status();
  test_online();
  test_invite();
  test_join();
  test_conversation();
  if(g_failed) {
    return 1;
  }
  printf("all playing checks passed\n");
  return 0;
}
