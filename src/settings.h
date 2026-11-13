/* The app's settings, which the title's Settings screen changes and the
notifications payload (src/notify.c) reads too: a few "key=value" lines in a
text file. The title keeps them in /data/TwitterPS5, or beside its eboot when
it cannot write there; the payload looks in both. */

#pragma once

#include <stddef.h>

#define SETTINGS_PATH     "/data/TwitterPS5/settings.txt"
#define SETTINGS_APP_PATH "/app0/settings.txt"                      // as the title sees it
#define SETTINGS_DIR_PATH "/data/homebrew/TWPS00001/settings.txt"   // the same, for a payload
#define SETTINGS_TEXT     1024
#define SETTINGS_SEEN     64 // "id,id,id": the posts on top of Following the last time

// X accounts: the first one's session is SETTINGS_SESSION_DIR "session.txt",
// the others' "session2.txt" to "session5.txt"
#define SETTINGS_ACCOUNTS    5
#define SETTINGS_SESSION_DIR "/data/TwitterPS5/"

// the languages of the app, in the order the Settings screen goes through them
enum {
  LANG_SYSTEM = -1, // the PS5's own, when the app has it, else English
  LANG_EN, LANG_PT, LANG_ES, LANG_FR, LANG_DE, LANG_IT, LANG_NL,
  LANGS
};

// the app's look: the warm backdrop of the day, or the black one of the night
enum { SETTINGS_DAY, SETTINGS_NIGHT };

// the minutes of a break's reminder (pause), as the Settings screen goes
// through them after 0, never
#define SETTINGS_PAUSES {30, 60, 120}

// how often the notifications are read, in seconds
#define SETTINGS_EVERY_MIN 30
#define SETTINGS_EVERY_MAX 600

typedef struct settings {
  int lang;     // LANG_*
  int notify;   // the payload shows X's notifications over the game
  int every;    // seconds between its readings
  int sound;    // videos play with their sound
  int autoplay; // a video plays as soon as it is opened
  int saver;    // videos in a smaller size, to load faster
  int hover;    // a tick when the pointer lights something up
  int theme;    // SETTINGS_DAY or SETTINGS_NIGHT
  int screen;   // the rows the title draws: 1080, 1440, 2160, or 0 for the TV's
  int spoilers; // posts about the game that runs stay covered until opened
  int light;    // the controller's light bar takes the colour of the banner shown
  int pause;    // minutes in XPS5 after which it suggests a break, or 0 for never
  int rising;   // the payload says when one of the user's posts is rising
  int share;    // the payload offers the game's new captures and trophies for a post
  int playing;  // the payload puts the game that runs in the profile's location (play.h)
  int account;  // 1 to SETTINGS_ACCOUNTS: whose session the app and the payload read
  char users[SETTINGS_ACCOUNTS][16]; // their screen names as last seen, or empty
  // the first posts of their Following the last time it was read, for "You
  // left off here": numbers between commas, or empty
  char seen[SETTINGS_ACCOUNTS][SETTINGS_SEEN];
  char ids[SETTINGS_ACCOUNTS][24]; // their account numbers as last seen, or empty
} settings_t;

/**
 * the session file of account (1 to SETTINGS_ACCOUNTS); 0, or -1
 **/
int settings_session_path(int account, char *buf, size_t size);

/**
 * the settings of a new install
 **/
void settings_default(settings_t *s);

/**
 * read the lines of text into s, over what it holds; unknown keys and bad
 * values are left out
 **/
void settings_parse(settings_t *s, const char *text, size_t len);

/**
 * s as the lines of a file; returns the length, or -1 when it does not fit
 **/
int settings_format(const settings_t *s, char *buf, size_t size);

/**
 * which of the files was written last, among those there and not empty:
 * its index (the first on a tie), or -1
 **/
int settings_newest(const char *const *paths, int count);

/**
 * the newest of the files that can be read (settings_newest), else the
 * first that can, over the defaults; returns its index, or -1 when none
 * could (s then holds the defaults)
 **/
int settings_load(settings_t *s, const char *const *paths, int count);

/**
 * write s to the first of the files that can be written; returns its index,
 * or -1
 **/
int settings_save(const settings_t *s, const char *const *paths, int count);

/**
 * the two letters X knows a language by ("en", "pt", ...)
 **/
const char *settings_lang_code(int lang);

/**
 * LANG_* of the PS5's system language (sceSystemServiceParamGetInt 1)
 **/
int settings_lang_of_system(int sce_lang);
