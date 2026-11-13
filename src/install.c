/* TwitterPS5 install: writes the title (TWPS00001) to
/data/homebrew/TWPS00001, where ShadowMountPlus finds it and adds it to the
home screen; its param.json makes it a media app, in the Media tab. The
title's files are embedded at build time (build/title_files.h); param.json
is written last. notify.elf, the X notifications payload, goes beside the
eboot; the title starts it (src/notify.c).

The installer does not add the title to the home screen itself: on FW 12
and later the system's sceAppInstUtilAppInstallTitleDir is gone (FW 13.60:
"Unable to resolve" when the payload loads, 2026-11-13), and what
ShadowMountPlus does instead patches ShellCore with offsets for each
firmware.

The icon (icon0.png, 512x512) and the background of the home screen go
into sce_sys: pic0.png (1920x1080), and the 3840x2160 BC7 pic0.dds the menu
shows (title/art, made by tools/bc7dds.c), as pic0.dds, pic1.dds and
pic2.dds, the names the PS5's own titles have it under (the menu took no
pic0.png alone, 2026-11-12). ShadowMountPlus copies them for the home
screen (/user/appmeta/TWPS00001 and /user/app/TWPS00001) only when it adds
the title, so on an update they are written again there, in those folders
that are there (none are made). Nothing else outside the title's folder is
written.

When XPS5 is open, it is closed first (sceLncUtilKillApp), so its files are
not written under it: the title alone, never the game.

Built with XPS5_ENGINE (build.ps1, when build/engine has it), it carries the
login engine too (tools/login-engine, packed with xz by pack-engine.sh) and
writes it to /data/TwitterPS5/engine, where the title's login window finds
it. The engine (142 MiB) is written again only when it differs from the one
there (engine.sha256, the tar's SHA-256, written last); XPS5 still installs
and works without it, with the linking from a phone or a PC. */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include <ps5/klog.h>

#include "title_files.h"

#ifdef XPS5_ENGINE
#include <lzma.h>

#include "engine_pack.h" // ENGINE_PACK_PATH, ENGINE_PACK_SHA256 (build.ps1)
#include "tarout.h"

#define ENGINE_DIR   "/data/TwitterPS5/engine"
#define ENGINE_STAMP ENGINE_DIR "/engine.sha256"
#define ENGINE_LIB   ENGINE_DIR "/libWebKitXps5.so.SOVERSION"

// the packed engine, as the file build.ps1 names
__asm__(".section .rodata\n"
        ".p2align 4\n"
        ".globl engine_pack\n"
        ".hidden engine_pack\n"
        "engine_pack:\n"
        ".incbin \"" ENGINE_PACK_PATH "\"\n"
        ".globl engine_pack_end\n"
        ".hidden engine_pack_end\n"
        "engine_pack_end:\n"
        ".previous\n");
extern const unsigned char engine_pack[] __asm__("engine_pack");
extern const unsigned char engine_pack_end[] __asm__("engine_pack_end");
#endif

#define TITLE_ID     "TWPS00001"
#define TITLE_DIR    "/data/homebrew/" TITLE_ID
#define APPMETA_DIR  "/user/appmeta/" TITLE_ID
#define APP_DIR      "/user/app/" TITLE_ID


typedef struct notify_request {
  char unused[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);
int sceLncUtilGetAppId(const char *title_id);
int sceLncUtilKillApp(int app_id);


/**
 * print to stdout and klog, and show it as a notification
 **/
static void
say(const char *fmt, ...) {
  notify_request_t req;
  va_list args;

  memset(&req, 0, sizeof req);
  va_start(args, fmt);
  vsnprintf(req.message, sizeof req.message, fmt, args);
  va_end(args);

  printf("%s\n", req.message);
  fflush(stdout);
  klog_printf("[TwitterPS5 install] %s\n", req.message);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}


static int
make_dir(const char *path) {
  if(mkdir(path, 0777) && errno != EEXIST) {
    say("mkdir %s: errno %d", path, errno);
    return -1;
  }
  return 0;
}


/**
 * write a file with the given mode and read it back; the mode is set with
 * fchmod because open() keeps the old mode of an existing file and applies
 * the umask (exec of an eboot.bin without x bits fails with EACCES)
 **/
static int
write_file(const char *path, const unsigned char *data, size_t len, mode_t mode) {
  static unsigned char check[1 << 16];
  size_t off;
  ssize_t n;
  int fd;

  if((fd=open(path, O_WRONLY | O_CREAT | O_TRUNC, mode)) < 0) {
    say("open %s: errno %d", path, errno);
    return -1;
  }
  n = write(fd, data, len);
  if(fchmod(fd, mode)) {
    say("chmod %s: errno %d", path, errno);
    close(fd);
    return -1;
  }
  close(fd);
  if(n != (ssize_t)len) {
    say("write %s: %zd of %zu bytes, errno %d", path, n, len, errno);
    return -1;
  }

  if((fd=open(path, O_RDONLY)) < 0) {
    say("cannot read back %s", path);
    return -1;
  }
  for(off=0; off < len; off += n) {
    n = read(fd, check, sizeof check);
    if(n <= 0 || (size_t)n > len - off || memcmp(check, data + off, n)) {
      break;
    }
  }
  if(off != len || read(fd, check, 1) != 0) {
    close(fd);
    say("%s differs after writing", path);
    return -1;
  }
  close(fd);
  return 0;
}


/**
 * write the icon and the background again where the home screen keeps its
 * copies of them, in the folders of those copies that are there (none are
 * made: they are there once ShadowMountPlus has added XPS5). Returns how
 * many were written, or -1 when one could not be
 **/
static int
refresh_art(void) {
  static const struct {
    const char          *dir;
    const char          *path;
    const unsigned char *data;
    size_t               len;
  } art[] = {
    {APPMETA_DIR, APPMETA_DIR "/icon0.png", icon0_png, sizeof icon0_png},
    {APPMETA_DIR, APPMETA_DIR "/pic0.png", pic0_png, sizeof pic0_png},
    {APPMETA_DIR, APPMETA_DIR "/pic0.dds", pic_dds, sizeof pic_dds},
    {APPMETA_DIR, APPMETA_DIR "/pic1.dds", pic_dds, sizeof pic_dds},
    {APPMETA_DIR, APPMETA_DIR "/pic2.dds", pic_dds, sizeof pic_dds},
    {APP_DIR, APP_DIR "/icon0.png", icon0_png, sizeof icon0_png},
  };
  struct stat st;
  int written = 0;

  for(size_t i=0; i < sizeof art / sizeof art[0]; i++) {
    if(stat(art[i].dir, &st) || !S_ISDIR(st.st_mode)) {
      continue;
    }
    if(write_file(art[i].path, art[i].data, art[i].len, 0644)) {
      return -1;
    }
    written++;
  }
  return written;
}


#ifdef XPS5_ENGINE
/**
 * the login engine into ENGINE_DIR, unless the same one is there: 1 when
 * written, 0 when it was there, -1 when it could not be (said)
 **/
static int
install_engine(void) {
  static unsigned char out[1 << 20];
  static tarout_t tar;
  lzma_stream s = LZMA_STREAM_INIT;
  lzma_ret ret;
  char stamp[80] = "";
  struct stat st;
  ssize_t n;
  int fd;

  if((fd=open(ENGINE_STAMP, O_RDONLY)) >= 0) {
    n = read(fd, stamp, sizeof stamp - 1);
    close(fd);
    stamp[n > 0 ? n : 0] = 0;
    stamp[strcspn(stamp, "\r\n")] = 0;
  }
  if(!strcmp(stamp, ENGINE_PACK_SHA256) && !stat(ENGINE_LIB, &st) && st.st_size > 0) {
    return 0;
  }
  if(make_dir("/data/TwitterPS5") || make_dir(ENGINE_DIR)) {
    return -1;
  }
  unlink(ENGINE_STAMP); // until the whole engine is written
  say("XPS5: writing its login engine (142 MiB)");
  if(lzma_stream_decoder(&s, 128 << 20, 0) != LZMA_OK) {
    say("login engine not installed: no unpacker");
    return -1;
  }
  s.next_in = engine_pack;
  s.avail_in = (size_t)(engine_pack_end - engine_pack);
  tarout_init(&tar, ENGINE_DIR, 0755);
  do {
    s.next_out = out;
    s.avail_out = sizeof out;
    ret = lzma_code(&s, LZMA_FINISH);
    if((ret == LZMA_OK || ret == LZMA_STREAM_END) && tarout_feed(&tar, out, sizeof out - s.avail_out)) {
      break;
    }
  } while(ret == LZMA_OK);
  lzma_end(&s);
  if(ret != LZMA_STREAM_END) {
    say("login engine not installed: unpacking (%d), file error %d, errno %d", (int)ret, tar.error, tar.err_no);
    tarout_finish(&tar);
    return -1;
  }
  if(tarout_finish(&tar)) {
    say("login engine not installed: file error %d, errno %d", tar.error, tar.err_no);
    return -1;
  }
  if(write_file(ENGINE_STAMP, (const unsigned char *)ENGINE_PACK_SHA256, strlen(ENGINE_PACK_SHA256), 0644)) {
    return -1;
  }
  klog_printf("[TwitterPS5 install] login engine: %d files, %lld MiB\n", tar.files, tar.bytes >> 20);
  return 1;
}
#endif


/**
 * XPS5 closed when it is open: 1 when it was, 0 when it was not, -1 when it
 * stays open
 **/
static int
close_title(void) {
  int app = sceLncUtilGetAppId(TITLE_ID), ret;

  if(app <= 0) {
    return 0;
  }
  ret = sceLncUtilKillApp(app);
  for(int i=0; i < 50 && sceLncUtilGetAppId(TITLE_ID) > 0; i++) {
    usleep(100 * 1000);
  }
  if(sceLncUtilGetAppId(TITLE_ID) > 0) {
    say("XPS5 did not close (0x%08x): close it and send the installer again", (unsigned)ret);
    return -1;
  }
  klog_printf("[TwitterPS5 install] XPS5 closed: 0x%08x\n", (unsigned)ret);
  usleep(500 * 1000); // and its files let go
  return 1;
}


int
main(void) {
  const char *engine = "";
  int closed, art;

  if((closed=close_title()) < 0) {
    return EXIT_FAILURE;
  }
  if(make_dir(TITLE_DIR) || make_dir(TITLE_DIR "/sce_sys") || make_dir(TITLE_DIR "/sce_module")) {
    return EXIT_FAILURE;
  }

  if(write_file(TITLE_DIR "/sce_module/libc.prx", libc_prx, sizeof libc_prx, 0777) ||
     write_file(TITLE_DIR "/eboot.bin", eboot_bin, sizeof eboot_bin, 0777) ||
     write_file(TITLE_DIR "/sce_sys/icon0.png", icon0_png, sizeof icon0_png, 0644) ||
     write_file(TITLE_DIR "/sce_sys/pic0.png", pic0_png, sizeof pic0_png, 0644) ||
     write_file(TITLE_DIR "/sce_sys/pic0.dds", pic_dds, sizeof pic_dds, 0644) ||
     write_file(TITLE_DIR "/sce_sys/pic1.dds", pic_dds, sizeof pic_dds, 0644) ||
     write_file(TITLE_DIR "/sce_sys/pic2.dds", pic_dds, sizeof pic_dds, 0644) ||
     write_file(TITLE_DIR "/notify.elf", notify_elf, sizeof notify_elf, 0644) ||
     write_file(TITLE_DIR "/sce_sys/param.json", param_json, sizeof param_json, 0644)) {
    return EXIT_FAILURE;
  }

#ifdef XPS5_ENGINE
  // XPS5 is in place: the login engine, which it works without
  switch(install_engine()) {
  case 1:
    engine = ", with its login engine";
    break;
  case 0:
    engine = ", its login engine as it was";
    break;
  default:
    engine = ", without its login engine";
    break;
  }
#endif

  // the home screen's copies of its pictures, when it is on it already
  if((art=refresh_art()) < 0) {
    return EXIT_FAILURE;
  }
  klog_printf("[TwitterPS5 install] home screen pictures: %d\n", art);
  if(art) {
    say("XPS5 %swritten to " TITLE_DIR "%s. Open it from Media on the home screen.",
        closed ? "closed and " : "", engine);
  } else {
    say("XPS5 %swritten to " TITLE_DIR "%s. ShadowMountPlus adds it to Media on the home screen in "
        "about 15 s.", closed ? "closed and " : "", engine);
  }
  return EXIT_SUCCESS;
}
