/* A tar (ustar) taken as it comes, in pieces of any size, with its regular
files written into one folder: XPS5's installer unpacks the login engine
with it (src/install.c), as liblzma hands the tar over. Only plain names are
taken (letters, digits, '.', '_', '-'): nothing outside that folder is ever
written; other entries (folders, links, names with a path) are left out and
counted. Each file goes to a ".part" beside it and is renamed once whole,
so a cut install never leaves half a library. */

#pragma once

#include <stddef.h>

enum { TAROUT_FINE, TAROUT_OPEN, TAROUT_WRITE, TAROUT_RENAME, TAROUT_SHORT };

typedef struct tarout {
  char          dir[128];
  unsigned      mode;       // of the files written
  unsigned char block[512]; // a header as it comes
  size_t        have;       // its bytes so far
  long long     left;       // the entry's data still to come
  long long     pad;        // then the bytes up to the next block
  long long     size;       // the entry's
  int           fd;         // the file being written, or -1
  char          path[256], part[264];
  int           ended;      // the empty block at the end came
  int           files, refused;
  long long     bytes;      // of the files written
  int           error;      // TAROUT_*: the first failure
  int           err_no;     // and its errno
} tarout_t;

/**
 * a new tar into dir (which is there), its files with mode
 **/
void tarout_init(tarout_t *t, const char *dir, unsigned mode);

/**
 * the next len bytes of the tar; 0, or -1 once something failed (t's error)
 **/
int tarout_feed(tarout_t *t, const void *buf, size_t len);

/**
 * the tar's end: 0 when it was whole, else -1 (TAROUT_SHORT, or the error
 * before), with the file it was writing removed
 **/
int tarout_finish(tarout_t *t);
