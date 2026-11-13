/* The login engine's files onto the PS5, for its tests (phase 2): a tar
on stdin (send-files.ps1 sends it right after this ELF; elfldr reads only
the ELF), each regular file written into ENGINE_DIR. Only plain names are
taken (letters, digits, '.', '_', '-'): nothing outside that folder is ever
written. Each file goes to a ".part" next to it and is renamed once whole,
so a cut connection never leaves half a library.

The klog gets counts and sizes only. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps5/klog.h>

#define ENGINE_DIR "/data/TwitterPS5/engine"
#define BLOCK      512
#define CHUNK      (1 << 20)

static void
say(const char *fmt, const char *what, long long n) {
  char line[160];

  snprintf(line, sizeof line, fmt, what, n);
  klog_printf("[XPS5 login] install: %s\n", line);
  printf("%s\n", line);
  fflush(stdout);
}


/**
 * exactly len bytes; 0, 1 at the end before the first byte, -1 otherwise
 **/
static int
read_full(void *buf, size_t len) {
  size_t got = 0;
  ssize_t n;

  while(got < len) {
    if((n=read(STDIN_FILENO, (char *)buf + got, len - got)) < 0) {
      if(errno == EINTR) {
        continue;
      }
      return -1;
    }
    if(!n) {
      return got ? -1 : 1;
    }
    got += (size_t)n;
  }
  return 0;
}


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
octal(const char *s, size_t len) {
  long long v = 0;

  for(size_t i=0; i < len && s[i]; i++) {
    if(s[i] >= '0' && s[i] <= '7') {
      v = v * 8 + (s[i] - '0');
    }
  }
  return v;
}


int
main(void) {
  static char block[BLOCK], buf[CHUNK];
  char name[101], path[256], part[264];
  long long total = 0;
  int files = 0, refused = 0, err;

  mkdir("/data/TwitterPS5", 0755);
  if(mkdir(ENGINE_DIR, 0755) && errno != EEXIST) {
    say("no folder (%s), errno %lld", "engine", errno);
    return 1;
  }
  for(;;) {
    long long size, left;
    int fd, keep;

    if((err=read_full(block, BLOCK))) {
      if(err > 0) {
        break;
      }
      say("%s cut short, errno %lld", "the tar", errno);
      return 1;
    }
    if(!block[0]) {
      break; // the two empty blocks at the end
    }
    memcpy(name, block, 100);
    name[100] = 0;
    size = octal(block + 124, 12);
    keep = (block[156] == '0' || block[156] == 0) && plain_name(name);
    refused += !keep;
    fd = -1;
    if(keep) {
      snprintf(path, sizeof path, "%s/%s", ENGINE_DIR, name);
      snprintf(part, sizeof part, "%s.part", path);
      if((fd=open(part, O_WRONLY | O_CREAT | O_TRUNC, 0755)) < 0) {
        say("a file not written (%s), errno %lld", "open", errno);
        return 1;
      }
    }
    // the data, in whole blocks
    for(left=(size + BLOCK - 1) / BLOCK * BLOCK; left > 0;) {
      size_t n = left < CHUNK ? (size_t)left : CHUNK;
      if(read_full(buf, n)) {
        say("%s cut short, errno %lld", "a file", errno);
        if(fd >= 0) {
          close(fd);
          unlink(part);
        }
        return 1;
      }
      if(fd >= 0) {
        long long data = size - ((size + BLOCK - 1) / BLOCK * BLOCK - left);
        size_t w = data <= 0 ? 0 : data < (long long)n ? (size_t)data : n;
        if(w && write(fd, buf, w) != (ssize_t)w) {
          say("a file not written (%s), errno %lld", "write", errno);
          close(fd);
          unlink(part);
          return 1;
        }
      }
      left -= (long long)n;
    }
    if(fd >= 0) {
      close(fd);
      if(rename(part, path)) {
        say("a file not written (%s), errno %lld", "rename", errno);
        unlink(part);
        return 1;
      }
      files++;
      total += size;
    }
  }
  say("%s: %lld files", "done", files);
  say("%s: %lld MiB", "done", total >> 20);
  if(refused) {
    say("%s: %lld entries left out", "done", refused);
  }
  return 0;
}
