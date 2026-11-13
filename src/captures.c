/* The PS5's captures, see captures.h. The folders are walked with getdents,
which libkernel has for the title and the payloads alike, and lstat; the
host tests walk theirs with readdir (CAPTURES_HOST_DIR). */

#define _DEFAULT_SOURCE // lstat on the host

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#ifdef CAPTURES_HOST_DIR
#include <dirent.h>
#endif

#include "captures.h"


static int
ends_with(const char *s, const char *end) {
  size_t a = strlen(s), b = strlen(end);

  if(a < b) {
    return 0;
  }
  for(size_t i=0; i < b; i++) {
    char c = s[a - b + i];
    if((c >= 'A' && c <= 'Z' ? c + 32 : c) != end[i]) {
      return 0;
    }
  }
  return 1;
}


static int
newer(const void *a, const void *b) {
  const capture_t *x = a, *y = b;

  return x->time < y->time ? 1 : x->time > y->time ? -1 : strcmp(x->path, y->path);
}


void
captures_sort(capture_t *list, int count) {
  qsort(list, (size_t)count, sizeof *list, newer);
}


int
captures_format(const capture_t *list, int count, char *buf, size_t size) {
  size_t n = 0;
  int k;

  if(!size) {
    return -1;
  }
  buf[0] = 0;
  for(int i=0; i < count; i++) {
    k = snprintf(buf + n, size - n, "%lld %lld %s\n", list[i].time, list[i].size, list[i].path);
    if(k < 0 || (size_t)k >= size - n) {
      return -1;
    }
    n += (size_t)k;
  }
  return (int)n;
}


int
captures_parse(const char *text, size_t len, capture_t *list, int max) {
  char line[CAPTURE_PATH + 64], *end;
  size_t start = 0, stop, n;
  int count = 0, kind;
  long long t, s;

  while(start < len && count < max) {
    for(stop=start; stop < len && text[stop] != '\n'; stop++) {
    }
    n = stop - start;
    if(n && n < sizeof line) {
      memcpy(line, text + start, n);
      line[n] = 0;
      t = strtoll(line, &end, 10);
      if(*end == ' ') {
        s = strtoll(end + 1, &end, 10);
        if(*end == ' ' && s >= 0 && (kind=captures_kind(end + 1)) >= 0) {
          memset(&list[count], 0, sizeof list[count]);
          snprintf(list[count].path, sizeof list[count].path, "%s", end + 1);
          list[count].time = t;
          list[count].size = s;
          list[count].kind = kind;
          count++;
        }
      }
    }
    start = stop + 1;
  }
  return count;
}


captures_stats_t captures_stats;


/**
 * kind of a path under root (see captures_kind)
 **/
static int
kind_under(const char *root, const char *path) {
  size_t n = strlen(root);

  if(!path || strncmp(path, root, n) || path[n] != '/' || strstr(path, "/../") || strstr(path, "/./") ||
     strlen(path) >= CAPTURE_PATH || strchr(path, '\n')) {
    return -1;
  }
  if(ends_with(path, ".jpg") || ends_with(path, ".jpeg")) {
    return CAPTURE_JPEG;
  }
  if(ends_with(path, ".png")) {
    return CAPTURE_PNG;
  }
  if(ends_with(path, ".mp4")) {
    return CAPTURE_MP4;
  }
  if(ends_with(path, ".webm")) {
    return CAPTURE_WEBM;
  }
  return -1;
}


int
captures_kind(const char *path) {
  return kind_under(CAPTURES_ROOT, path);
}


#ifndef CAPTURES_NO_DIR

#define DEPTH 9 // photo/NPXS40087/PPSA01234/035/<file>, with room to spare

typedef struct walk {
  const char *root;
  capture_t  *list;
  int         count, max;
#ifdef CAPTURES_HOST_DIR
  DIR        *dir;
#else
  int         fd, n, at;
  char        buf[16 << 10];
#endif
} walk_t;


#ifdef CAPTURES_HOST_DIR

static int
dir_open(walk_t *w, const char *dir) {
  return (w->dir=opendir(dir)) ? 0 : -1;
}


static int
dir_next(walk_t *w, const char **name, size_t *len) {
  struct dirent *e = readdir(w->dir);

  if(!e) {
    return 0;
  }
  *name = e->d_name;
  *len = strlen(e->d_name);
  return 1;
}


static void
dir_close(walk_t *w) {
  closedir(w->dir);
}

#else

int getdents(int fd, char *buf, size_t nbytes);

// FreeBSD 11's struct dirent, as getdents fills it (the SDK's sys/dirent.h):
// d_fileno 4 bytes, d_reclen 2, d_type 1, d_namlen 1, then the name. The
// type is not trusted (some file systems leave it 0): lstat says what it is.
#define DIRENT_HEAD 8

static int
dir_open(walk_t *w, const char *dir) {
  w->n = w->at = 0;
  return (w->fd=open(dir, O_RDONLY | O_DIRECTORY)) < 0 ? -1 : 0;
}


/**
 * the next name of the folder: 1, or 0 at its end (or on an error)
 **/
static int
dir_next(walk_t *w, const char **name, size_t *len) {
  for(;;) {
    if(w->at + DIRENT_HEAD > w->n) {
      if((w->n=getdents(w->fd, w->buf, sizeof w->buf)) <= 0) {
        if(w->n < 0) {
          captures_stats.error = errno;
        }
        return 0;
      }
      w->at = 0;
    }
    const unsigned char *e = (const unsigned char *)w->buf + w->at;
    unsigned reclen = e[4] | (unsigned)e[5] << 8, namlen = e[7];
    if(reclen < DIRENT_HEAD || w->at + (int)reclen > w->n) {
      w->at = w->n; // a record that does not read: the rest of this block
      continue;
    }
    w->at += (int)reclen;
    if(namlen && DIRENT_HEAD + namlen <= reclen) {
      *name = (const char *)e + DIRENT_HEAD;
      *len = namlen;
      return 1;
    }
  }
}


static void
dir_close(walk_t *w) {
  close(w->fd);
}

#endif


/**
 * a capture into the list; when it is full, in place of the oldest one if
 * this one is newer
 **/
static void
keep(walk_t *w, const char *path, const struct stat *st, int kind) {
  int at = w->count;

  if(w->count == w->max) {
    at = 0;
    for(int i=1; i < w->max; i++) {
      if(w->list[i].time < w->list[at].time) {
        at = i;
      }
    }
    if(w->list[at].time >= (long long)st->st_mtime) {
      return;
    }
  } else {
    w->count++;
  }
  memset(&w->list[at], 0, sizeof w->list[at]);
  snprintf(w->list[at].path, sizeof w->list[at].path, "%s", path);
  w->list[at].time = (long long)st->st_mtime;
  w->list[at].size = (long long)st->st_size;
  w->list[at].kind = kind;
  captures_stats.kept++;
}


/**
 * the files of a folder, then its folders (their names are kept for after
 * the walk of this one, which uses the one buffer of w)
 **/
static void
walk(walk_t *w, const char *dir, int depth, int skip_thumbnails) {
  char path[CAPTURE_PATH], *names = NULL, *grown;
  size_t used = 0, room = 0, len;
  const char *name;
  struct stat st;
  int kind;

  if(depth > DEPTH) {
    return;
  }
  if(dir_open(w, dir)) {
    captures_stats.unreadable++;
    captures_stats.error = errno;
    return;
  }
  captures_stats.folders++;
  while(dir_next(w, &name, &len)) {
    if(name[0] == '.') {
      continue;
    }
    if(snprintf(path, sizeof path, "%s/%.*s", dir, (int)len, name) >= (int)sizeof path) {
      captures_stats.long_paths++;
      continue;
    }
    if(lstat(path, &st)) {
      continue;
    }
    if(S_ISDIR(st.st_mode)) {
      if(skip_thumbnails && len >= 5 && !strncmp(name, "thumb", 5)) {
        continue;
      }
      if(used + len + 1 > room) {
        room = (used + len + 1) * 2 + 256;
        if(!(grown=realloc(names, room))) {
          continue;
        }
        names = grown;
      }
      memcpy(names + used, name, len);
      names[used + len] = 0;
      used += len + 1;
    } else if(S_ISREG(st.st_mode)) {
      captures_stats.files++;
      if((kind=kind_under(w->root, path)) >= 0) {
        keep(w, path, &st, kind);
      }
    }
  }
  dir_close(w);
  for(size_t i=0; i < used; i += strlen(names + i) + 1) {
    if(snprintf(path, sizeof path, "%s/%s", dir, names + i) < (int)sizeof path) {
      walk(w, path, depth + 1, 0);
    }
  }
  free(names);
}


int
captures_scan(const char *root, capture_t *list, int max) {
  char dir[CAPTURE_PATH];
  struct stat st;
  walk_t *w;
  int count;

  memset(&captures_stats, 0, sizeof captures_stats);
  if(stat(root, &st)) {
    captures_stats.error = errno;
    return -1;
  }
  if(!(w=calloc(1, sizeof *w)) || !(w->list=malloc(CAPTURES_ROOM * sizeof *w->list))) {
    free(w);
    return -1;
  }
  w->root = root;
  w->max = CAPTURES_ROOM;
  // the PS5 keeps them under photo/ and video/ (thumbnails/ has small copies)
  snprintf(dir, sizeof dir, "%s/photo", root);
  walk(w, dir, 1, 0);
  snprintf(dir, sizeof dir, "%s/video", root);
  walk(w, dir, 1, 0);
  if(!captures_stats.folders) {
    walk(w, root, 0, 1);
  }
  captures_sort(w->list, w->count);
  count = w->count < max ? w->count : max;
  memcpy(list, w->list, (size_t)count * sizeof *list);
  free(w->list);
  free(w);
  return count;
}


int
captures_list(capture_t *list, int max) {
  return captures_scan(CAPTURES_ROOT, list, max);
}


int
captures_newest_of(const char *root, const char *game, capture_t *out) {
  static const char *const kinds[] = {"photo", "video"};
  size_t len = strlen(game);
  char dir[CAPTURE_PATH];
  walk_t *w;
  int found;

  memset(out, 0, sizeof *out);
  if(len < 4 || len > 16 || strspn(game, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != len) {
    return -1;
  }
  if(!(w=calloc(1, sizeof *w)) || !(w->list=malloc(sizeof *w->list))) {
    free(w);
    return -1;
  }
  w->root = root;
  w->max = 1; // keep() holds the newest
  memset(&captures_stats, 0, sizeof captures_stats);
  for(size_t i=0; i < sizeof kinds / sizeof kinds[0]; i++) {
    snprintf(dir, sizeof dir, "%s/%s/%s/%s", root, kinds[i], CAPTURES_APP, game);
    walk(w, dir, 3, 0);
  }
  found = w->count > 0;
  if(found) {
    *out = w->list[0];
  }
  free(w->list);
  free(w);
  return found;
}


int
captures_folder_names(const char *dir, char *names, size_t size) {
  size_t used = 0, len;
  const char *name;
  walk_t *w;
  int n = 0;

  if(!(w=calloc(1, sizeof *w))) {
    return -1;
  }
  if(dir_open(w, dir)) {
    free(w);
    return -1;
  }
  while(dir_next(w, &name, &len)) {
    if((len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.')) {
      continue;
    }
    if(used + len + 1 > size) {
      break;
    }
    memcpy(names + used, name, len);
    names[used + len] = 0;
    used += len + 1;
    n++;
  }
  dir_close(w);
  free(w);
  return n;
}

#endif
