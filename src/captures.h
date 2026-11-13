/* The PS5's own screenshots and video clips (/user/av_contents), which a
new post can carry: listed newest first. The title reads them itself when
its sandbox lets it; else the notifications payload, which runs outside it,
lists and reads them for it over its port on 127.0.0.1 (src/notify.c), in
the lines captures_format writes. Only files under CAPTURES_ROOT with a
picture's or a video's extension are ever listed or read. */

#pragma once

#include <stddef.h>

#define CAPTURES_ROOT  "/user/av_contents"
#define CAPTURES_APP   "NPXS40087" // the folder of the PS5's own captures, under photo/ and video/
#define CAPTURES_MAX   240
#define CAPTURES_ROOM  2048 // the newest kept while the folders are walked
#define CAPTURE_PATH   256
#define CAPTURE_PHOTO_MAX (40LL << 20)   // bytes of a picture read
#define CAPTURE_VIDEO_MAX (512LL << 20)  // X takes up to 512 MB

enum { CAPTURE_JPEG, CAPTURE_PNG, CAPTURE_MP4, CAPTURE_WEBM };

typedef struct capture {
  char      path[CAPTURE_PATH];
  long long time; // Unix time it was written
  long long size;
  int       kind; // CAPTURE_*
} capture_t;

/**
 * CAPTURE_* of a path that may be read (under CAPTURES_ROOT, no "..", one
 * of the extensions), or -1
 **/
int captures_kind(const char *path);

/**
 * newest first
 **/
void captures_sort(capture_t *list, int count);

/**
 * the list as lines "time size path\n"; returns the length, or -1
 **/
int captures_format(const capture_t *list, int count, char *buf, size_t size);

/**
 * those lines back; returns how many, leaving out lines that do not read
 * or whose path may not be read
 **/
int captures_parse(const char *text, size_t len, capture_t *list, int max);

/**
 * the captures under CAPTURES_ROOT (photo/ and video/, nine folders deep:
 * the PS5 puts them in photo/NPXS40087/<title id>/<number>/), newest first;
 * returns how many, or -1 when the folder cannot be read
 **/
int captures_list(capture_t *list, int max);

/**
 * the same under another folder (the tests')
 **/
int captures_scan(const char *root, capture_t *list, int max);

/**
 * the newest capture of one game (its title id), under root's
 * photo/CAPTURES_APP/<game>/ and video/CAPTURES_APP/<game>/: returns 1 with
 * it in out, 0 when it has none, -1 when game is not a title id or memory
 * lacks
 **/
int captures_newest_of(const char *root, const char *game, capture_t *out);

/**
 * the names in the folder dir (not "." and ".."), each with its NUL, into
 * names, as many as fit in size bytes; returns how many, or -1 when it does
 * not open (the payload's way to /user/home's users)
 **/
int captures_folder_names(const char *dir, char *names, size_t size);

/**
 * what the last walk saw, for the log (counts only)
 **/
typedef struct captures_stats {
  int folders;    // read
  int unreadable; // that would not open
  int files;      // regular files seen
  int kept;       // pictures and videos among them
  int long_paths; // left out, longer than CAPTURE_PATH
  int error;      // the last errno
} captures_stats_t;

extern captures_stats_t captures_stats;
