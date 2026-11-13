/* TwitterPS5 smplog: shows why ShadowMountPlus did or did not add the
title. It lists the title's folder and /data/shadowmount, says whether the
title is mounted under /system_ex/app, and prints ShadowMountPlus's config
and its log lines about TWPS00001 (/data/shadowmount/debug.log). It only
reads, and prints to stdout (send.ps1 shows it), not to the klog. */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#define TITLE_ID   "TWPS00001"
#define TITLE_DIR  "/data/homebrew/" TITLE_ID
#define SMP_DIR    "/data/shadowmount"
#define MAX_LOG    (32 << 20)
#define TITLE_TAIL 60
#define LOG_TAIL   25


static void
list_dir(const char *path, int depth) {
  char child[512];
  struct dirent *e;
  struct stat st;
  DIR *dir;

  if(!(dir=opendir(path))) {
    printf("  %s: cannot open\n", path);
    return;
  }
  while((e=readdir(dir))) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
      continue;
    }
    snprintf(child, sizeof child, "%s/%s", path, e->d_name);
    if(stat(child, &st)) {
      printf("  %s: stat failed\n", child);
      continue;
    }
    printf("  %06o %10lld %s\n", (unsigned)st.st_mode, (long long)st.st_size, child);
    if(S_ISDIR(st.st_mode) && depth > 1) {
      list_dir(child, depth - 1);
    }
  }
  closedir(dir);
}


static void
show_stat(const char *path) {
  struct stat st;

  if(stat(path, &st)) {
    printf("  %s: missing\n", path);
  } else {
    printf("  %06o %10lld %s\n", (unsigned)st.st_mode, (long long)st.st_size, path);
  }
}


/**
 * the whole of a file, NUL-terminated; NULL when it cannot be read
 **/
static char *
read_all(const char *path, size_t *len) {
  struct stat st;
  char *buf;
  ssize_t n;
  size_t off = 0, size;
  int fd;

  if((fd=open(path, O_RDONLY)) < 0) {
    return NULL;
  }
  if(fstat(fd, &st) || st.st_size < 0) {
    close(fd);
    return NULL;
  }
  size = st.st_size > MAX_LOG ? MAX_LOG : (size_t)st.st_size;
  if(st.st_size > MAX_LOG) {
    lseek(fd, st.st_size - MAX_LOG, SEEK_SET);
  }
  if(!(buf=malloc(size + 1))) {
    close(fd);
    return NULL;
  }
  while(off < size && (n=read(fd, buf + off, size - off)) > 0) {
    off += n;
  }
  close(fd);
  buf[off] = 0;
  *len = off;
  return buf;
}


/**
 * print the last count lines of text, only those with needle when it is
 * not NULL
 **/
static void
print_tail(char *text, size_t len, const char *needle, int count) {
  char **lines;
  int n = 0, first;
  char *s = text, *nl;

  if(!(lines=calloc(count, sizeof *lines))) {
    return;
  }
  while(s < text + len) {
    if((nl=strchr(s, '\n'))) {
      *nl = 0;
    }
    if(!needle || strstr(s, needle)) {
      lines[n++ % count] = s;
    }
    if(!nl) {
      break;
    }
    s = nl + 1;
  }
  first = n > count ? n - count : 0;
  for(int i=first; i<n; i++) {
    printf("  | %s\n", lines[i % count]);
  }
  if(!n) {
    printf("  (none)\n");
  }
  // put the newlines back for the next pass
  for(size_t i=0; i<len; i++) {
    if(!text[i]) {
      text[i] = '\n';
    }
  }
  free(lines);
}


int
main(void) {
  size_t len;
  char *text;

  printf("title folder " TITLE_DIR ":\n");
  list_dir(TITLE_DIR, 2);

  printf("mounted title /system_ex/app/" TITLE_ID ":\n");
  show_stat("/system_ex/app/" TITLE_ID);
  show_stat("/system_ex/app/" TITLE_ID "/eboot.bin");
  show_stat("/system_ex/app/" TITLE_ID "/sce_sys/param.json");

  printf(SMP_DIR ":\n");
  list_dir(SMP_DIR, 1);

  printf(SMP_DIR "/config.ini:\n");
  if((text=read_all(SMP_DIR "/config.ini", &len))) {
    print_tail(text, len, NULL, 80);
    free(text);
  } else {
    printf("  (none)\n");
  }

  if(!(text=read_all(SMP_DIR "/debug.log", &len))) {
    printf("no " SMP_DIR "/debug.log\n");
    return EXIT_SUCCESS;
  }
  printf("debug.log, %zu bytes; last %d lines about " TITLE_ID ":\n", len, TITLE_TAIL);
  print_tail(text, len, TITLE_ID, TITLE_TAIL);
  printf("debug.log, last %d lines:\n", LOG_TAIL);
  print_tail(text, len, NULL, LOG_TAIL);
  free(text);
  return EXIT_SUCCESS;
}
