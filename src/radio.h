/* A Space's radio: its HLS playlist read again and again (hls.h), each new
segment fetched, its ADTS frames decoded by FFmpeg's AAC decoder and played
on an audio port of its own, of the PS5's system user. A live Space starts
a few segments behind its newest, so the network has room; a recording
plays from its start.

The title has one, and so has the payload (src/notify.c): the payload's
goes on while a game is in front, when the PS5 suspends the title, so the
title hands its Spaces over to it while it runs (title/space.c,
spacecmd.h). The playlist and its segments are on X's video CDN, not on
X's API: they are fetched with no X session at all (no cookies, no tokens),
and their addresses never go to the log. */

#pragma once

#include "spacecmd.h"

/**
 * where its lines go (codes and counts only, never an address)
 **/
void radio_init(void (*log)(const char *fmt, ...));

/**
 * play the HLS playlist at url (https, from live_video_stream/status):
 * live, from near its newest segment; else from its start. One playing
 * already stops
 **/
void radio_play(const char *url, int live);

/**
 * stop it; it frees itself, so this never waits
 **/
void radio_stop(void);

/**
 * silent while on (a video plays its sound): a live one goes on, quietly,
 * so it stays live; a recording waits where it was
 **/
void radio_hush(int on);

/**
 * silent for the next ms only, unless asked again: for a hush asked by a
 * title the PS5 may suspend before it says the video is over
 **/
void radio_hush_for(int ms);

void radio_status(space_status_t *s);

/**
 * radios not gone yet (one stopped frees itself a moment later)
 **/
int radio_alive(void);
