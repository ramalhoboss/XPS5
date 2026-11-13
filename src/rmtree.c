/* rmtree: see rmtree.h. A folder is read again after a pass that removed
something, since entries removed while it is read may hide others. */

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "rmtree.h"


static int
failed(rmtree_count_t *c) {
  c->failed++;
  if(!c->err_no) {
    c->err_no = errno ? errno : EIO;
  }
  return -1;
}


/**
 * path (len long, in a buffer of size) removed, at depth below the top
 **/
static int
rm_at(char *path, size_t len, size_t size, int depth, rmtree_count_t *c) {
  struct dirent *e;
  struct stat st;
  DIR *d;

  if(lstat(path, &st)) {
    return errno == ENOENT ? 0 : failed(c);
  }
  if(!S_ISDIR(st.st_mode)) {
    if(unlink(path)) {
      return errno == ENOENT ? 0 : failed(c);
    }
    c->files++;
    return 0;
  }
  if(depth >= RMTREE_DEPTH) {
    errno = ELOOP;
    return failed(c);
  }
  for(int pass=0; pass < 8; pass++) {
    int seen = 0, gone = 0;

    if(!(d=opendir(path))) {
      return failed(c);
    }
    while((e=readdir(d))) {
      size_t n = strlen(e->d_name);

      if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
        continue;
      }
      seen++;
      if(len + 1 + n >= size) {
        errno = ENAMETOOLONG;
        failed(c);
        continue;
      }
      path[len] = '/';
      memcpy(path + len + 1, e->d_name, n + 1);
      if(!rm_at(path, len + 1 + n, size, depth + 1, c)) {
        gone++;
      }
      path[len] = 0;
    }
    closedir(d);
    if(!seen || !gone) {
      break;
    }
  }
  if(rmdir(path)) {
    return failed(c);
  }
  c->dirs++;
  return 0;
}


int
rmtree(const char *path, rmtree_count_t *c) {
  char buf[1024];
  size_t len = strlen(path);

  if(!len || len >= sizeof buf) {
    errno = ENAMETOOLONG;
    return failed(c);
  }
  memcpy(buf, path, len + 1);
  while(len > 1 && buf[len - 1] == '/') {
    buf[--len] = 0;
  }
  return rm_at(buf, len, sizeof buf, 0, c);
}
