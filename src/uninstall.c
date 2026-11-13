/* TwitterPS5 uninstall: XPS5 removed from the console, as if it had never
been installed:

  1. XPS5 closed when it is open (sceLncUtilKillApp), never the game;
  2. its notifications payload stopped ("stop" on 127.0.0.1:9047, the
     command a new copy of it sends an old one);
  3. its tile taken off the home screen by ShadowMountPlus, which added it
     from /data/homebrew, through its API on 127.0.0.1:10101
     (/api/v1/games/uninstall); when that does not answer, by the system
     (sceAppInstUtilAppUnInstall);
  4. its files removed: the title (/data/homebrew/TWPS00001), the home
     screen's copies of it (/user/app/TWPS00001 and
     /user/appmeta/TWPS00001), and /data/TwitterPS5 with the X sessions of
     every account, the settings, the history and the login engine.

Nothing else is touched: the PS5's captures and other titles stay. Only
codes and counts go to the klog. */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <ps5/klog.h>

#include "rmtree.h"

#define TITLE_ID    "TWPS00001"
#define NOTIFY_PORT 9047
#define SMP_PORT    10101

typedef struct notify_request {
  char unused[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);
int sceLncUtilGetAppId(const char *title_id);
int sceLncUtilKillApp(int app_id);
int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppUnInstall(const char *title_id);


/**
 * print to stdout and klog; shown as a notification too when shown is set
 **/
static void
say(int shown, const char *fmt, ...) {
  notify_request_t req;
  va_list args;

  memset(&req, 0, sizeof req);
  va_start(args, fmt);
  vsnprintf(req.message, sizeof req.message, fmt, args);
  va_end(args);

  printf("%s\n", req.message);
  fflush(stdout);
  klog_printf("[TwitterPS5 uninstall] %s\n", req.message);
  if(shown) {
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
  }
}


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
    say(1, "XPS5 did not close (0x%08x): close it and send the uninstaller again", (unsigned)ret);
    return -1;
  }
  usleep(500 * 1000); // and its files let go
  return 1;
}


/**
 * a connection to 127.0.0.1:port, with a timeout of seconds both ways; -1
 * when nothing listens there
 **/
static int
local_connect(int port, int seconds) {
  struct sockaddr_in a;
  struct timeval tv = {seconds, 0};
  int s;

  if((s=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(s, (struct sockaddr *)&a, sizeof a)) {
    close(s);
    return -1;
  }
  return s;
}


/**
 * the notifications payload told to stop: 1 when it ran and stopped, 0
 * when it did not run, -1 when it still answers. A copy from "on 10" on is
 * told "uninstall", which first gives the X profiles that show a game what
 * they said before (its records of them are about to go); an older one
 * answers that with its version, and is told "stop"
 **/
static int
stop_notify(void) {
  char answer[16] = "";
  ssize_t n;
  int s;

  if((s=local_connect(NOTIFY_PORT, 30)) < 0) {
    return 0;
  }
  (void)!write(s, "uninstall\n", 10);
  n = read(s, answer, sizeof answer - 1);
  close(s);
  answer[n > 0 ? n : 0] = 0;
  klog_printf("[TwitterPS5 uninstall] the notifications payload answered %s\n",
              !strncmp(answer, "ok", 2) ? "ok" : n > 0 ? "with its version" : "nothing");
  if(strncmp(answer, "ok", 2) && (s=local_connect(NOTIFY_PORT, 2)) >= 0) {
    (void)!write(s, "stop\n", 5);
    close(s);
  }
  for(int i=0; i < 30; i++) {
    usleep(100 * 1000);
    if((s=local_connect(NOTIFY_PORT, 1)) < 0) {
      return 1;
    }
    close(s);
  }
  return -1;
}


/**
 * ShadowMountPlus asked to take XPS5 off the home screen: its HTTP status,
 * or -1 when its API does not answer
 **/
static int
smp_uninstall(void) {
  static const char body[] = "{\"title_id\":\"" TITLE_ID "\"}";
  char req[320], answer[512];
  int s, status = -1;
  ssize_t n, got = 0;

  if((s=local_connect(SMP_PORT, 30)) < 0) {
    return -1;
  }
  n = snprintf(req, sizeof req,
               "POST /api/v1/games/uninstall HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
               "Content-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
               SMP_PORT, sizeof body - 1, body);
  if(write(s, req, (size_t)n) != n) {
    close(s);
    return -1;
  }
  while(got < (ssize_t)sizeof answer - 1 && (n=read(s, answer + got, sizeof answer - 1 - got)) > 0) {
    got += n;
  }
  close(s);
  answer[got] = 0;
  if(sscanf(answer, "HTTP/1.%*d %d", &status) != 1) {
    return -1;
  }
  return status;
}


int
main(void) {
  static const char *const dirs[] = {
    "/data/homebrew/" TITLE_ID, "/user/appmeta/" TITLE_ID, "/user/app/" TITLE_ID, "/data/TwitterPS5",
  };
  rmtree_count_t c;
  struct stat st;
  int closed, stopped, smp = -1, sys = 1, left = 0;

  if((closed=close_title()) < 0) {
    return EXIT_FAILURE;
  }
  stopped = stop_notify();

  // off the home screen first, while its folder is still there: by
  // ShadowMountPlus when it added it, else (or when it does not answer)
  // by the system
  if(!stat("/data/homebrew/" TITLE_ID, &st)) {
    smp = smp_uninstall();
  }
  if(smp < 0 && !(sys=sceAppInstUtilInitialize())) {
    sys = sceAppInstUtilAppUnInstall(TITLE_ID);
  }

  memset(&c, 0, sizeof c);
  for(size_t i=0; i < sizeof dirs / sizeof *dirs; i++) {
    if(rmtree(dirs[i], &c)) {
      left++;
    }
  }
  klog_printf("[TwitterPS5 uninstall] closed %d, notifications %d, ShadowMountPlus %d, system 0x%08x, "
              "%d files and %d folders removed, %d failed (errno %d)\n",
              closed, stopped, smp, (unsigned)sys, c.files, c.dirs, c.failed, c.err_no);

  if(left) {
    say(1, "XPS5: %d of its folders could not be removed (errno %d). Send the uninstaller again.", left, c.err_no);
    return EXIT_FAILURE;
  }
  if(stopped < 0) {
    say(1, "XPS5 removed, but its notifications still run until the PS5 restarts.");
  } else if(smp >= 0 && smp != 200) {
    say(1, "XPS5's files are removed, but ShadowMountPlus answered %d: remove its tile there.", smp);
  } else if(smp < 0 && sys) {
    say(1, "XPS5's files are removed, but the PS5 answered 0x%08x when taking its tile off the home "
        "screen.", (unsigned)sys);
  } else {
    say(1, "XPS5 removed: the app, its notifications, its tile, your X sessions and its settings.");
  }
  return EXIT_SUCCESS;
}
