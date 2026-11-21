/* A Space heard in the background, as the title and the payload speak of
it. The radio (radio.h) plays one; while the payload runs, it is the
payload's radio, which goes on with a game in front, where the title's
would be suspended with the title. The title hands it over on the
payload's port (src/notify.c) with a line

  "space play <n> <live> <id> <host> <url>\n"

and asks how it goes with "space status\n", "space hush <n> <0|1>\n" and
"space stop <n>\n" (n 0: whichever plays). The payload answers each one
with a line of how its radio stands:

  "space <n> <state> <error> <code> <heard_ms> <waiting> <hushed> <live> <id> <host>\n"

n is the title's number for one Space it handed over (never 0), so an old
stop never stops a newer one; id and host (the Space's and its host's
screen name, "-" when not known) let a title opened again find the Space
the payload still plays. The url is the playlist on X's video CDN (https,
no X session goes with it, never the media key): it is the only part that
says where the sound is, and it never goes to the klog, nor back in the
answer. */

#pragma once

#include <stddef.h>

enum {
  SPACE_IDLE,    // nothing playing
  SPACE_TUNING,  // its playlist and first segments on their way
  SPACE_ON,      // playing
  SPACE_OVER,    // it ended (or the recording did)
  SPACE_FAILED,  // see error
};

enum {
  SPACE_ERR_NONE,
  SPACE_ERR_NETWORK, // code: the SCE error
  SPACE_ERR_HTTP,    // code: the status
  SPACE_ERR_FORMAT,  // the playlist or the sound is not what HLS of AAC is
  SPACE_ERR_DECODER, // code: FFmpeg's error
  SPACE_ERR_AUDIO,   // code: the audio port's error
  SPACE_ERR_MEMORY,
  SPACE_ERR_GONE,    // the payload that played it stopped
};

typedef struct space_status {
  int       state;     // SPACE_*
  int       error;     // SPACE_ERR_*
  int       code;
  long long heard_ms;  // of sound played
  int       waiting;   // for its next segment
  int       hushed;    // silent while a video plays its sound
} space_status_t;

#define SPACE_URL  768 // of a playlist's address, with its NUL (HLS_URL)
#define SPACE_NAME 32  // of an id or a host's screen name, with its NUL
#define SPACE_LINE 160 // of an answer, with its '\n' and NUL

/**
 * "space play" for the url: its length, or -1 when it does not fit or a
 * part may not go (url: https, no space nor control; id and host: letters,
 * digits and '_', or empty for "-"; n above 0)
 **/
int space_cmd_play(char *out, size_t size, unsigned n, int live, const char *id, const char *host,
                   const char *url);

/**
 * what comes after "space play ": 0, or -1 when it is not one space_cmd_play
 * makes; id and host empty for "-"
 **/
int space_cmd_play_args(const char *args, unsigned *n, int *live, char id[SPACE_NAME], char host[SPACE_NAME],
                        char url[SPACE_URL]);

/**
 * the answer line for how the radio stands: its length, or -1
 **/
int space_line_put(char *out, size_t size, unsigned n, const space_status_t *s, int live, const char *id,
                   const char *host);

/**
 * an answer line read back: 0, or -1 when it is none (an older payload's
 * "on 24", say); id and host empty for "-"
 **/
int space_line_get(const char *line, unsigned *n, space_status_t *s, int *live, char id[SPACE_NAME],
                   char host[SPACE_NAME]);
