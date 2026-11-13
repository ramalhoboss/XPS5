/* Host test of src/rmtree.c: a folder with files, folders inside folders,
an empty folder, many entries and a link to a folder outside it is removed
whole, the link's target stays, and a folder that is not there is fine. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "rmtree.h"

static int
fail(const char *what) {
  fprintf(stderr, "rmtree_test: %s\n", what);
  return 1;
}


static int
put(const char *path, const char *text) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  if(fd < 0) {
    return -1;
  }
  if(write(fd, text, strlen(text)) != (ssize_t)strlen(text)) {
    close(fd);
    return -1;
  }
  return close(fd);
}


int
main(void) {
  const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
  char top[512], p[600], outside[512], kept[600];
  rmtree_count_t c;
  struct stat st;

  snprintf(top, sizeof top, "%s/rmtree-%d", tmp, (int)getpid());
  snprintf(outside, sizeof outside, "%s/rmtree-outside-%d", tmp, (int)getpid());
  snprintf(kept, sizeof kept, "%s/keep.txt", outside);
  if(mkdir(top, 0755) || mkdir(outside, 0755) || put(kept, "stays")) {
    return fail("cannot make the test folders");
  }
  snprintf(p, sizeof p, "%s/a.txt", top);
  put(p, "a");
  snprintf(p, sizeof p, "%s/engine", top);
  mkdir(p, 0755);
  snprintf(p, sizeof p, "%s/engine/web", top);
  mkdir(p, 0755);
  snprintf(p, sizeof p, "%s/engine/web/cookies.db", top);
  put(p, "x");
  snprintf(p, sizeof p, "%s/empty", top);
  mkdir(p, 0755);
  for(int i=0; i < 300; i++) { // more than a read of the folder gives at once
    snprintf(p, sizeof p, "%s/engine/f%03d", top, i);
    put(p, "f");
  }
  snprintf(p, sizeof p, "%s/link", top);
  if(symlink(outside, p)) {
    return fail("cannot make the link");
  }

  memset(&c, 0, sizeof c);
  if(rmtree(top, &c) || c.failed || c.err_no) {
    return fail("the folder does not go");
  }
  if(!lstat(top, &st) || errno != ENOENT) {
    return fail("the folder is still there");
  }
  if(c.files != 1 + 1 + 300 + 1 || c.dirs != 4) {
    fprintf(stderr, "files %d, folders %d\n", c.files, c.dirs);
    return fail("the counts are wrong");
  }
  if(stat(kept, &st)) {
    return fail("the link was followed: its target went");
  }

  // a folder that is not there, and a trailing slash
  memset(&c, 0, sizeof c);
  if(rmtree(top, &c) || c.failed || c.files || c.dirs) {
    return fail("a folder that is not there is not fine");
  }
  snprintf(p, sizeof p, "%s/", outside);
  memset(&c, 0, sizeof c);
  if(rmtree(p, &c) || c.files != 1 || c.dirs != 1 || !lstat(outside, &st)) {
    return fail("a trailing slash keeps the folder");
  }
  puts("all rmtree checks passed");
  return 0;
}
