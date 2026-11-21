/* A Space heard while the rest of XPS5 goes on, and over a game too. Its
sound is played by a radio (src/radio.h): the payload's while the payload
runs (src/notify.c), which goes on while a game is in front and the PS5
suspends the title; else the title's own, which stops with the title then
and goes on when XPS5 comes back. The title hands it over with the
playlist's address only (src/spacecmd.h): no X session, no media key.

The playlist and its segments are on X's video CDN, not on X's API: they
are fetched with no X session at all (no cookies, no tokens), and their
addresses never go to the klog.

main.c starts and stops it; ui.c reads how it stands. */

#pragma once

#include <stddef.h>

#include "spacecmd.h"
#include "xapi.h"

/**
 * where its lines go (codes and counts only, never an address), and how
 * the payload is asked: a line sent to its port, the line it answers into
 * answer (empty when it said nothing), and 0 when it could not be reached.
 * A Space the payload was playing already is looked for (space_found)
 **/
void space_init(void (*log)(const char *fmt, ...), int (*ask)(const char *cmd, char *answer, size_t size));

/**
 * play the HLS playlist at url (https, from live_video_stream/status) of
 * the Space id by host: live, from near its newest segment; else from its
 * start. One playing already stops. It goes to the payload when one that
 * plays Spaces answers, else it plays here; it is TUNING meanwhile
 **/
void space_play(const char *url, int live, const char *id, const char *host);

/**
 * stop it; never waits
 **/
void space_stop(void);

/**
 * silent while on (a video plays its sound): a live one goes on, quietly,
 * so it stays live; a recording waits where it was. Asked of the payload,
 * it lasts a few seconds unless asked again, so a title suspended in the
 * middle of a video does not keep it quiet during the game
 **/
void space_hush(int on);

void space_status(space_status_t *s);

/**
 * once: the Space the payload was still playing when the title opened
 * (its id, host and whether it is live, in s), which now counts as the one
 * playing; 1, or 0
 **/
int space_found(x_space_t *s);
