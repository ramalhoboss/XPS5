/* src/tarout.h: a tar's regular files into one folder, as the tar comes
(tools/login-engine/probe/files-install.c read one from stdin the same way). */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "tarout.h"

#define BLOCK 512


static int
plain_name(const char *name) {
  if(!name[0] || !strcmp(name, ".") || !strcmp(name, "..")) {
    return 0;
  }
  for(const char *c=name; *c; c++) {
    if(!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
         *c == '.' || *c == '_' || *c == '-')) {
      return 0;
    }
  }
  return 1;
}


static long long
octal(const unsigned char *s, size_t len) {
  long long v = 0;

  for(size_t i=0; i < len && s[i]; i++) {
    if(s[i] >= '0' && s[i] <= '7') {
      v = v * 8 + (s[i] - '0');
    }
  }
  return v;
}


static int
fail(tarout_t *t, int error) {
  if(!t->error) {
    t->error = error;
    t->err_no = errno;
  }
  if(t->fd >= 0) {
    close(t->fd);
    t->fd = -1;
    unlink(t->part);
  }
  return -1;
}


// the file written whole: in its place
static int
done_file(tarout_t *t) {
  int ret;

  if(t->fd < 0) {
    return 0;
  }
  ret = close(t->fd);
  t->fd = -1;
  if(ret) {
    unlink(t->part);
    return fail(t, TAROUT_WRITE);
  }
  if(rename(t->part, t->path)) {
    unlink(t->part);
    return fail(t, TAROUT_RENAME);
  }
  t->files++;
  t->bytes += t->size;
  return 0;
}


// a header whole in t's block: its entry begins
static int
header(tarout_t *t) {
  char name[101];
  int keep;

  if(!t->block[0]) {
    t->ended = 1; // the empty blocks at the end
    return 0;
  }
  memcpy(name, t->block, 100);
  name[100] = 0;
  t->size = octal(t->block + 124, 12);
  t->left = t->size;
  t->pad = (t->size + BLOCK - 1) / BLOCK * BLOCK - t->size;
  // a regular file with a plain name, and no prefix (ustar's path before it)
  keep = (t->block[156] == '0' || !t->block[156]) && plain_name(name) && !t->block[345];
  if(!keep) {
    t->refused++;
    return 0;
  }
  snprintf(t->path, sizeof t->path, "%s/%s", t->dir, name);
  snprintf(t->part, sizeof t->part, "%s.part", t->path);
  if((t->fd=open(t->part, O_WRONLY | O_CREAT | O_TRUNC, t->mode)) < 0) {
    return fail(t, TAROUT_OPEN);
  }
  if(fchmod(t->fd, t->mode)) {
    return fail(t, TAROUT_OPEN);
  }
  return t->left ? 0 : done_file(t);
}


void
tarout_init(tarout_t *t, const char *dir, unsigned mode) {
  memset(t, 0, sizeof *t);
  snprintf(t->dir, sizeof t->dir, "%s", dir);
  t->mode = mode;
  t->fd = -1;
}


int
tarout_feed(tarout_t *t, const void *buf, size_t len) {
  const unsigned char *p = buf;
  size_t n;

  if(t->error) {
    return -1;
  }
  while(len && !t->ended) {
    if(t->left) {
      n = (unsigned long long)t->left < len ? (size_t)t->left : len;
      if(t->fd >= 0) {
        for(size_t off=0; off < n;) {
          ssize_t w = write(t->fd, p + off, n - off);
          if(w <= 0) {
            return fail(t, TAROUT_WRITE);
          }
          off += (size_t)w;
        }
      }
      p += n;
      len -= n;
      t->left -= (long long)n;
      if(!t->left && done_file(t)) {
        return -1;
      }
    } else if(t->pad) {
      n = (unsigned long long)t->pad < len ? (size_t)t->pad : len;
      p += n;
      len -= n;
      t->pad -= (long long)n;
    } else {
      n = BLOCK - t->have < len ? BLOCK - t->have : len;
      memcpy(t->block + t->have, p, n);
      t->have += n;
      p += n;
      len -= n;
      if(t->have == BLOCK) {
        t->have = 0;
        if(header(t)) {
          return -1;
        }
      }
    }
  }
  return 0;
}


int
tarout_finish(tarout_t *t) {
  if(t->error) {
    return -1;
  }
  if(!t->ended || t->left || t->pad || t->have) {
    errno = 0;
    return fail(t, TAROUT_SHORT);
  }
  return 0;
}
