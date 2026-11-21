/* X's videos and GIFs in the picture viewer: the MP4 comes from
video.twimg.com and plays while it comes, its samples found by src/mp4.c in
what came, the H.264 pictures and the
AAC sound decoded by FFmpeg on threads of their own, the pictures scaled to
the size the screen asked for and the sound played on the PS5's audio port.
A video plays to its end and starts again, like on x.com.

The screen (ui.c) calls these from the thread that draws; tests/preview.c
has a stand-in without FFmpeg. */

#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
  PLAYER_IDLE,     // nothing open
  PLAYER_LOADING,  // fetching the file
  PLAYER_PLAYING,
  PLAYER_PAUSED,
  PLAYER_FAILED,   // see the status's error
};

enum {
  PLAYER_ERR_NONE,
  PLAYER_ERR_NETWORK, // code: the SCE error
  PLAYER_ERR_HTTP,    // code: the status
  PLAYER_ERR_FORMAT,  // code: MP4_* of mp4.h
  PLAYER_ERR_DECODER, // code: FFmpeg's error
  PLAYER_ERR_MEMORY,
  PLAYER_ERR_CUT,     // code: the SCE error; the file stopped coming partway, and
                      // opened again it goes on from where it stopped
};

typedef struct player_status {
  int state;    // PLAYER_*
  int error;    // PLAYER_ERR_*
  int code;
  int pos_ms;   // where it is
  int len_ms;   // how long it is, 0 while unknown
  int sound;    // it has sound and plays it
  int has_audio;
  int buffering; // waiting for more of the file
} player_status_t;

/**
 * where the player's lines go (codes and counts only, never a URL)
 **/
void player_init(void (*log)(const char *fmt, ...));

/**
 * play the MP4 at url, its pictures scaled to w x h; with sound when sound
 * is set; len_ms is how long X says it is (0 when it did not). A video
 * already open stops
 **/
void player_open(const char *url, int w, int h, int sound, int len_ms);

/**
 * the next player_open plays its sound on the controller's speaker (a voice
 * note), when that opens, and on the TV when it does not; for that one only
 **/
void player_speaker(int on);

/**
 * how a url "file://<path>" (a capture of the PS5's, in the composer) is
 * read: up to len bytes at off into buf, the file's size into *total
 * unless it is NULL; the count, or a negative error
 **/
void player_files(long long (*read)(const char *path, long long off, void *buf, size_t len, long long *total));

/**
 * stop the video open, if any
 **/
void player_close(void);

/**
 * whether url is the video open
 **/
int player_is(const char *url);

/**
 * the pictures of the video open at another size from the next one on (the
 * timeline's tile, then the whole screen), without fetching it again
 **/
void player_resize(int w, int h);

void player_pause(int paused);
void player_seek(int delta_ms);
void player_sound(int on);
void player_status(player_status_t *s);

/**
 * the latest picture (0xAABBGGRR) and its size, or NULL before the first;
 * it stays put until player_frame_done, which must follow at once
 **/
const uint32_t *player_frame(int *w, int *h);
void player_frame_done(void);

/**
 * whether a picture came, or the state changed, since the last call
 **/
int player_changed(void);
