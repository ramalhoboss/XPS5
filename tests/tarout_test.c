/* src/tarout.c on the computer: a tar made here, fed in pieces of many
sizes, its files checked in a folder of their own; then a tar cut short. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tarout.h"

static int g_failed;

#define CHECK(cond) do { \
  if(!(cond)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_failed++; \
  } \
} while(0)

static unsigned char g_tar[16 << 10];
static size_t g_len;
static unsigned char g_a[1000], g_b[512];


static void
entry(const char *name, char type, const unsigned char *data, size_t size, const char *prefix) {
  unsigned char *h = g_tar + g_len;

  memset(h, 0, 512);
  snprintf((char *)h, 100, "%s", name);
  snprintf((char *)h + 100, 8, "0000644");
  snprintf((char *)h + 124, 12, "%011o", (unsigned)size);
  h[156] = (unsigned char)type;
  memcpy(h + 257, "ustar", 6);
  memcpy(h + 263, "00", 2);
  if(prefix) {
    snprintf((char *)h + 345, 155, "%s", prefix);
  }
  g_len += 512;
  if(size) {
    memcpy(g_tar + g_len, data, size);
    g_len += (size + 511) / 512 * 512;
  }
}


static void
make_tar(void) {
  for(size_t i=0; i < sizeof g_a; i++) {
    g_a[i] = (unsigned char)(i * 7 + 3);
  }
  for(size_t i=0; i < sizeof g_b; i++) {
    g_b[i] = (unsigned char)(255 - i);
  }
  entry("a.bin", '0', g_a, sizeof g_a, NULL);
  entry("../evil", '0', g_a, 10, NULL);       // a path: left out
  entry("empty", '0', NULL, 0, NULL);
  entry("sub", '5', NULL, 0, NULL);           // a folder: left out
  entry("hidden", '0', g_a, 20, "elsewhere"); // a prefix: left out
  entry("b.txt", 0, g_b, sizeof g_b, NULL);   // an old tar's regular file
  memset(g_tar + g_len, 0, 1024);             // the end
  g_len += 1024;
}


static int
same_file(const char *path, const unsigned char *data, size_t len) {
  unsigned char buf[2048];
  FILE *f = fopen(path, "rb");
  size_t n;

  if(!f) {
    return 0;
  }
  n = fread(buf, 1, sizeof buf, f);
  fclose(f);
  return n == len && (!len || !memcmp(buf, data, len));
}


static int
there(const char *dir, const char *name) {
  char path[300];
  struct stat st;

  snprintf(path, sizeof path, "%s/%s", dir, name);
  return !stat(path, &st);
}


static void
make_dir(char *dir, size_t size, const char *tmp, const char *what) {
  snprintf(dir, size, "%s/tarout_%d_%s", tmp, (int)getpid(), what);
  mkdir(dir, 0755);
}


static void
remove_dir(const char *dir) {
  const char *names[] = {"a.bin", "empty", "b.txt", "a.bin.part", "evil", "hidden"};
  char path[300];

  for(size_t i=0; i < sizeof names / sizeof names[0]; i++) {
    snprintf(path, sizeof path, "%s/%s", dir, names[i]);
    unlink(path);
  }
  rmdir(dir);
}


int
main(void) {
  const char *tmp = getenv("TMPDIR");
  static const size_t pieces[] = {1, 7, 511, 512, 513, 4096, sizeof g_tar};
  char dir[256], path[300], what[16];
  tarout_t t;

  tmp = tmp && tmp[0] ? tmp : "/tmp";
  make_tar();

  // whole, in pieces of each size
  for(size_t i=0; i < sizeof pieces / sizeof pieces[0]; i++) {
    snprintf(what, sizeof what, "p%zu", pieces[i]);
    make_dir(dir, sizeof dir, tmp, what);
    tarout_init(&t, dir, 0755);
    for(size_t off=0; off < g_len; off += pieces[i]) {
      size_t n = g_len - off < pieces[i] ? g_len - off : pieces[i];
      CHECK(!tarout_feed(&t, g_tar + off, n));
    }
    CHECK(!tarout_finish(&t));
    CHECK(t.files == 3 && t.refused == 3 && t.bytes == (long long)(sizeof g_a + sizeof g_b));
    snprintf(path, sizeof path, "%s/a.bin", dir);
    CHECK(same_file(path, g_a, sizeof g_a));
    snprintf(path, sizeof path, "%s/b.txt", dir);
    CHECK(same_file(path, g_b, sizeof g_b));
    snprintf(path, sizeof path, "%s/empty", dir);
    CHECK(same_file(path, NULL, 0));
    CHECK(!there(dir, "a.bin.part") && !there(dir, "evil") && !there(dir, "hidden"));
    remove_dir(dir);
  }

  // cut in a file's middle: an error, and no half file left
  make_dir(dir, sizeof dir, tmp, "cut");
  tarout_init(&t, dir, 0755);
  CHECK(!tarout_feed(&t, g_tar, 512 + 300));
  CHECK(tarout_finish(&t) == -1 && t.error == TAROUT_SHORT);
  CHECK(!there(dir, "a.bin") && !there(dir, "a.bin.part"));
  remove_dir(dir);

  // a folder that is not there: the first file cannot be opened
  snprintf(dir, sizeof dir, "%s/tarout_%d_none", tmp, (int)getpid());
  tarout_init(&t, dir, 0755);
  CHECK(tarout_feed(&t, g_tar, g_len) == -1 && t.error == TAROUT_OPEN && t.err_no == ENOENT);
  CHECK(tarout_finish(&t) == -1);

  if(g_failed) {
    fprintf(stderr, "tarout_test: %d failed\n", g_failed);
    return 1;
  }
  printf("tarout_test: ok\n");
  return 0;
}
