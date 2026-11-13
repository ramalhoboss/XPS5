/* The login engine's first test on the PS5 (phase 2): does XPS5's own
WebKit run there from XPS5's folder alone, and how far does X's login page
get in it? Nothing is shown and nothing is typed.

This payload is the launcher (BrowserPS5-WebKit v1's design, read from its
poc/launcher): a title cannot start processes, a payload can. It starts
login-ui.elf (WebKit's UIProcess, login-ui.cpp) with the control socket at
BROWSERPS5_LAUNCHER_FD, then starts the WebProcess and the NetworkProcess
libWebKit asks for over it, each with its IPC socket at
BROWSERPS5_CHILD_IPC_FD. Every process gets the environment below: the
SDK's loader finds the engine's libraries through LD_LIBRARY_PATH (before
/user/homebrew/lib, where another homebrew's could be), and nothing is read
from or written to BrowserPS5's folders.

At the end (the UIProcess done, or PROBE_SECONDS) every process is killed and
reaped, its peak memory logged, and the page's data (X's cookies for a
visitor, its storage) deleted. The klog gets counts, codes and sizes only. */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <ps5/klog.h>

#include "elfldr.h"
#include "launcher_proto.h"

#define ENGINE_DIR    "/data/TwitterPS5/engine"
#define PROBE_SECONDS 240
#define MAX_CHILDREN  8

extern char **elfldr_child_envp; // build-probe.sh's elfldr.c: given to execve

static char *g_env[] = {
  "LD_LIBRARY_PATH=" ENGINE_DIR,
  "HOME=" ENGINE_DIR "/home",
  "TMPDIR=" ENGINE_DIR "/tmp",
  "FONTCONFIG_FILE=" ENGINE_DIR "/fonts.conf",
  "BROWSERPS5_PROCESS_DIR=" ENGINE_DIR,
  "BROWSERPS5_WEB_IN_TITLE=0",
  0,
};

static const char *const g_files[] = {
  "libWebKitXps5.so.SOVERSION", "libEGL.so.1", "libvulkan.so.1", "WebProcess.elf",
  "NetworkProcess.elf", "login-ui.elf", "cert.pem", "fonts.conf",
};

static struct {
  pid_t pid;
  const char *kind;
} g_children[MAX_CHILDREN];

static time_t g_start;


static void
say(const char *fmt, ...) {
  char line[200];
  va_list args;

  va_start(args, fmt);
  vsnprintf(line, sizeof line, fmt, args);
  va_end(args);
  klog_printf("[XPS5 login] probe: %s\n", line);
  printf("probe: %s\n", line);
  fflush(stdout);
}


static int
read_file(const char *path, uint8_t **data, size_t *size) {
  struct stat st;
  uint8_t *buf;
  int fd;

  if((fd=open(path, O_RDONLY)) < 0) {
    return -errno;
  }
  if(fstat(fd, &st) || !(buf=malloc(st.st_size))) {
    close(fd);
    return -ENOMEM;
  }
  if(read(fd, buf, st.st_size) != st.st_size) {
    free(buf);
    close(fd);
    return -EIO;
  }
  close(fd);
  *data = buf;
  *size = st.st_size;
  return 0;
}


static void
track(pid_t pid, const char *kind) {
  for(int i=0; i < MAX_CHILDREN; i++) {
    if(!g_children[i].pid) {
      g_children[i].pid = pid;
      g_children[i].kind = kind;
      return;
    }
  }
}


static pid_t
spawn(const char *path, const char *arg, int ipc_fd, int child_fd, const char *kind) {
  char path_copy[LAUNCHER_PATH_MAX], arg_copy[LAUNCHER_ARG_MAX];
  char *argv[] = {path_copy, arg_copy, 0};
  uint8_t *elf;
  size_t size;
  pid_t pid;
  int err;

  snprintf(path_copy, sizeof path_copy, "%s", path);
  snprintf(arg_copy, sizeof arg_copy, "%s", arg);
  if((err=read_file(path, &elf, &size))) {
    say("%s: its file not read (%d)", kind, err);
    return err;
  }
  pid = elfldr_spawn_ipc(STDOUT_FILENO, ipc_fd, child_fd, argv, elf, size);
  free(elf);
  if(pid < 0) {
    say("%s: not started", kind);
    return -EIO;
  }
  track(pid, kind);
  say("%s started, %ld s in", kind, (long)(time(NULL) - g_start));
  return pid;
}


static ssize_t
recv_req(int sock, launcher_req_t *req, int *fd) {
  char control[CMSG_SPACE(sizeof(int))];
  struct msghdr msg = {0};
  struct cmsghdr *cmsg;
  struct iovec iov;
  ssize_t n;

  iov.iov_base = req;
  iov.iov_len = sizeof *req;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof control;
  *fd = -1;
  if((n=recvmsg(sock, &msg, 0)) <= 0) {
    return n;
  }
  for(cmsg=CMSG_FIRSTHDR(&msg); cmsg; cmsg=CMSG_NXTHDR(&msg, cmsg)) {
    if(cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
      memcpy(fd, CMSG_DATA(cmsg), sizeof(int));
    }
  }
  return n;
}


static void
reply(int sock, int32_t result) {
  launcher_rep_t rep = {result};

  send(sock, &rep, sizeof rep, 0);
}


/**
 * one process killed and reaped: how it ended and its peak memory
 **/
static void
end_child(int i, int killed) {
  struct rusage ru;
  int status = 0;
  pid_t pid = g_children[i].pid;

  if(!pid) {
    return;
  }
  if(killed) {
    kill(pid, SIGKILL);
  }
  memset(&ru, 0, sizeof ru);
  if(wait4(pid, &status, 0, &ru) == pid) {
    if(WIFSIGNALED(status)) {
      say("%s ended by signal %d, peak %ld MiB", g_children[i].kind, WTERMSIG(status), ru.ru_maxrss >> 10);
    } else {
      say("%s ended with code %d, peak %ld MiB", g_children[i].kind, WEXITSTATUS(status), ru.ru_maxrss >> 10);
    }
  } else {
    say("%s not reaped (errno %d)", g_children[i].kind, errno);
  }
  g_children[i].pid = 0;
}


/**
 * the processes that ended by themselves, as they end
 **/
static void
reap(void) {
  struct rusage ru;
  int status;
  pid_t pid;

  while((pid=wait4(-1, &status, WNOHANG, &ru)) > 0) {
    for(int i=0; i < MAX_CHILDREN; i++) {
      if(g_children[i].pid == pid) {
        say("%s ended early (%s %d), %ld s in, peak %ld MiB", g_children[i].kind,
            WIFSIGNALED(status) ? "signal" : "code", WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status),
            (long)(time(NULL) - g_start), ru.ru_maxrss >> 10);
        g_children[i].pid = 0;
      }
    }
  }
}


/**
 * a folder of the engine's and all in it (the page's data)
 **/
static int
remove_tree(const char *path) {
  char sub[512];
  struct dirent *e;
  struct stat st;
  DIR *dir;
  int n = 0;

  if(lstat(path, &st)) {
    return 0;
  }
  if(!S_ISDIR(st.st_mode)) {
    return !unlink(path);
  }
  if((dir=opendir(path))) {
    while((e=readdir(dir))) {
      if(strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
        snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
        n += remove_tree(sub);
      }
    }
    closedir(dir);
  }
  rmdir(path);
  return n;
}


int
main(void) {
  launcher_req_t req;
  char path[300];
  int ctl[2], have = 0, served = 0, fd;
  pid_t ui;
  ssize_t n;

  signal(SIGPIPE, SIG_IGN);
  g_start = time(NULL);
  for(size_t i=0; i < sizeof g_files / sizeof g_files[0]; i++) {
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", ENGINE_DIR, g_files[i]);
    have += !stat(path, &st);
  }
  say("start: %d of %d engine files there", have, (int)(sizeof g_files / sizeof g_files[0]));
  if(have != (int)(sizeof g_files / sizeof g_files[0])) {
    return 1;
  }
  if(chdir(ENGINE_DIR)) {
    say("no chdir (errno %d)", errno);
    return 1;
  }
  mkdir(ENGINE_DIR "/home", 0755);
  mkdir(ENGINE_DIR "/tmp", 0755);
  mkdir(ENGINE_DIR "/web", 0755);
  elfldr_child_envp = g_env;

  if(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ctl)) {
    say("no socketpair (errno %d)", errno);
    return 1;
  }
  ui = spawn(ENGINE_DIR "/login-ui.elf", "XPS5Login", ctl[1], BROWSERPS5_LAUNCHER_FD, "ui");
  close(ctl[1]);
  if(ui < 0) {
    return 1;
  }

  // its requests, until it closes the socket or the time is up
  for(;;) {
    struct pollfd p = {ctl[0], POLLIN, 0};
    int r = poll(&p, 1, 1000);

    reap();
    if(time(NULL) - g_start >= PROBE_SECONDS) {
      say("the time is up (%d s)", PROBE_SECONDS);
      break;
    }
    if(r <= 0) {
      continue;
    }
    if((n=recv_req(ctl[0], &req, &fd)) <= 0) {
      if(n < 0 && errno == EINTR) {
        continue;
      }
      break;
    }
    req.path[sizeof req.path - 1] = 0;
    req.arg[sizeof req.arg - 1] = 0;
    if(n != sizeof req) {
      reply(ctl[0], -EPROTO);
    } else if(req.type == LAUNCHER_SPAWN) {
      const char *kind = strstr(req.path, "NetworkProcess") ? "network" :
                         strstr(req.path, "WebProcess") ? "web" : "other";
      reply(ctl[0], fd < 0 ? -EBADF : spawn(req.path, req.arg, fd, BROWSERPS5_CHILD_IPC_FD, kind));
    } else if(req.type == LAUNCHER_KILL) {
      int done = -ESRCH;
      for(int i=0; i < MAX_CHILDREN; i++) {
        if(g_children[i].pid && g_children[i].pid == req.pid) {
          end_child(i, 1);
          done = 0;
        }
      }
      reply(ctl[0], done);
    } else {
      reply(ctl[0], -EINVAL);
    }
    if(fd >= 0) {
      close(fd);
    }
    served++;
  }
  close(ctl[0]);
  say("%d requests served", served);
  for(int i=0; i < MAX_CHILDREN; i++) {
    end_child(i, 1);
  }
  // fontconfig's cache where fonts.conf puts it: the WebProcess read
  // XPS5's fonts.conf (FONTCONFIG_FILE), not another one
  {
    DIR *dir = opendir(ENGINE_DIR "/fontcache");
    int files = 0;
    struct dirent *e;
    while(dir && (e=readdir(dir))) {
      files += e->d_name[0] != '.';
    }
    if(dir) {
      closedir(dir);
    }
    say("font cache: %d files", files);
  }
  // what the page left: X's cookies for a visitor, its storage, temporary files
  say("page data removed: %d files", remove_tree(ENGINE_DIR "/web") + remove_tree(ENGINE_DIR "/tmp") +
                                     remove_tree(ENGINE_DIR "/home"));
  say("done, %ld s", (long)(time(NULL) - g_start));
  return 0;
}
