/* XPS5's dictation, a payload of its own: the title (title/main.c) hands it
to etaHEN's ELF loader the first time ✕ stops a dictation, as it does the
notifications' payload, and sends it the sound on 127.0.0.1:DICT_PORT
(src/dictproto.h); it answers with the words, made by whisper.cpp with the
tiny model the installer put in DICT_MODEL.

It runs apart from the title because whisper.cpp wants C++ with its
exceptions and threads, which the payload SDK has and the title does not,
and because the model's memory (about 185 MiB at its peak with the 8-bit
model, measured on a PC with the same code) stays out of the title's. On
the PS5 (2026-11-16, the 8-bit model), 11 s of speech took 0.93 s the
first time, the model read from disk, and 0.70 s after (1.26 s with the
language guessed); the 16-bit one took 2.4 s, 1.2 s and 1.9 s. It listens
on 127.0.0.1 only, keeps the model for the next dictation, and goes after
IDLE_S without one, its memory freed. A second copy finds the port taken
and goes at once.

Neither the sound nor the words go anywhere but back to the title: not to
a file, not to the klog (whisper.cpp's own messages are silenced, see
src/dictate.c), not to the network. */

#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <ps5/klog.h>

#include "dictproto.h"
#include "dictserve.h"

#define IDLE_S   300 // the model freed and the payload gone after this long without a dictation
#define WAIT_S   20  // for a request's bytes, and for its answer to go

// One thread. A dictation is always asked from XPS5, so a title is in front,
// and the PS5 then leaves the system's processes (payloads included) only a
// core or two. ggml's threads wait for one another spinning: on 2026-11-21
// four of them on those cores never finished a dictation, and the
// notifications' payload, which shares the cores, stopped. The 4 threads of
// the 2026-11-16 timings above ran with no title in front.
#define THREADS  1
#define NICE     20 // the lowest priority: the console's menus and the notifications first


static void
log_line(const char *fmt, ...) {
  char line[256];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  klog_printf("[XPS5 dictate] %s\n", line);
}


/**
 * how many cores this process may run on, for the klog; 0 when it is not said
 **/
static int
cores(void) {
  cpuset_t set;

  CPU_ZERO(&set);
  if(cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof set, &set)) {
    return 0;
  }
  return CPU_COUNT(&set);
}


/**
 * a fault (or whisper.cpp's abort) said in the klog before it goes, so one
 * that never answers is told from one that died
 **/
static void
fault(int sig) {
  klog_printf("[XPS5 dictate] signal %d: going\n", sig);
  _exit(EXIT_FAILURE);
}


static int
listen_local(void) {
  struct sockaddr_in a;
  int s, on = 1;

  if((s=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(DICT_PORT);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 2)) {
    close(s);
    return -1;
  }
  return s;
}


int
main(void) {
  const struct timeval wait = {.tv_sec = WAIT_S};
  dictate_t *d = NULL;
  struct pollfd p;
  int s, c, ret;

  signal(SIGPIPE, SIG_IGN);
  signal(SIGSEGV, fault);
  signal(SIGBUS, fault);
  signal(SIGILL, fault);
  signal(SIGFPE, fault);
  signal(SIGABRT, fault);
  // a line on stdout, which is the ELF loader's connection: the title waits
  // for it before it connects (send_elf in title/main.c), as for the
  // notifications' payload. Without it the title waited there for good and
  // the dictation never left "loading"
  if((s=listen_local()) < 0) {
    printf("XPS5 dictate: a copy runs already\n");
    fflush(stdout);
    log_line("port %d taken (errno %d): a copy runs already", DICT_PORT, errno);
    return EXIT_SUCCESS;
  }
  printf("XPS5 dictate: listening on 127.0.0.1:%d, version %d\n", DICT_PORT, DICT_VERSION);
  fflush(stdout);
  // behind everything else on those cores: the PS5's own menus, the
  // notifications' payload; the words come a little later instead
  ret = setpriority(PRIO_PROCESS, 0, NICE);
  log_line("listening on 127.0.0.1:%d, version %d, %d cores, %d thread, nice %s", DICT_PORT, DICT_VERSION, cores(),
           THREADS, ret ? "refused" : "set");
  for(;;) {
    p.fd = s;
    p.events = POLLIN;
    p.revents = 0;
    if((ret=poll(&p, 1, IDLE_S * 1000)) == 0) {
      log_line("no dictation for %d s: going", IDLE_S);
      break;
    }
    if(ret < 0 || (c=accept(s, NULL, NULL)) < 0) {
      if(ret < 0 ? errno == EINTR : errno == EINTR || errno == ECONNABORTED) {
        continue;
      }
      log_line("accept: errno %d", errno);
      break;
    }
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof wait);
    ret = dictserve_answer(c, &d, DICT_MODEL, THREADS, log_line);
    close(c);
    if(ret == DICT_OLD) {
      log_line("going, for the title's version");
      break;
    }
  }
  close(s);
  dictate_close(d);
  return EXIT_SUCCESS;
}
