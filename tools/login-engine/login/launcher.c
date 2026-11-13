/* XPS5's login engine's launcher: the payload the title hands to the ELF
loader when its login window opens (src/loginproto.h says how they meet).

A title cannot start processes, a payload can (BrowserPS5-WebKit v1's design,
read from its poc/launcher). This one reads the title's key from its stdin,
starts the window process (login-ui.elf, WebKit's UIProcess) with the
control socket at BROWSERPS5_LAUNCHER_FD and the loader's connection as its
stdio, so that it can say its port there, then starts the WebProcess and the
NetworkProcess libWebKit asks for over that socket, each with its IPC socket
at BROWSERPS5_CHILD_IPC_FD and no stdio of the title's. Every process gets
the environment below: the SDK's loader finds the engine's libraries through
LD_LIBRARY_PATH, and nothing is read from or written to another homebrew's
folders. Only the window process gets the key.

When the window process ends (the title closed the window, or it failed) or
LOGIN_SECONDS have gone, every process is killed and reaped and the page's
data (X's cookies, its storage, temporary files) is deleted. The klog gets
codes, counts and times only. */

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
#include "loginproto.h"

#define ENGINE_DIR    LOGIN_ENGINE_DIR
#define LOGIN_SECONDS 1800 // a window open longer than this is closed
#define KEY_MS        10000
#define MAX_CHILDREN  8

extern char **elfldr_child_envp; // build-login.sh's elfldr.c: given to execve

#define ENV_COMMON                                    \
  "LD_LIBRARY_PATH=" ENGINE_DIR,                      \
  "HOME=" ENGINE_DIR "/home",                         \
  "TMPDIR=" ENGINE_DIR "/tmp",                        \
  "FONTCONFIG_FILE=" ENGINE_DIR "/fonts.conf",        \
  "BROWSERPS5_PROCESS_DIR=" ENGINE_DIR,               \
  "BROWSERPS5_WEB_IN_TITLE=0"

static char g_key_env[16 + 2 * LOGIN_KEY_BYTES + 1];
static char *g_env[] = {ENV_COMMON, 0};
static char *g_env_ui[] = {ENV_COMMON, g_key_env, 0};

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
  klog_printf("[XPS5 login] launcher: %s\n", line);
}


/**
 * a line to the title, on the loader's connection (its stdout); nothing
 * happens when the title is no longer there
 **/
static void
tell(const char *what, int n) {
  char line[48];
  int len = snprintf(line, sizeof line, "xps5-login %s %d\n", what, n);

  if(write(STDOUT_FILENO, line, (size_t)len) != len) {
    say("the title did not get \"%s\"", what);
  }
}


/**
 * the title's key, "key <hex>\n" on stdin, into g_key_env; 0 or -1
 **/
static int
read_key(void) {
  char line[8 + 2 * LOGIN_KEY_BYTES];
  size_t n = 0;
  char c = 0;

  while(n + 1 < sizeof line) {
    struct pollfd p = {STDIN_FILENO, POLLIN, 0};
    if(poll(&p, 1, KEY_MS) <= 0 || read(STDIN_FILENO, &c, 1) != 1 || c == '\n') {
      break;
    }
    line[n++] = c;
  }
  line[n] = 0;
  if(c != '\n' || n != 4 + 2 * LOGIN_KEY_BYTES || memcmp(line, "key ", 4) ||
     strspn(line + 4, "0123456789abcdef") != 2 * LOGIN_KEY_BYTES) {
    memset(line, 0, sizeof line);
    return -1;
  }
  snprintf(g_key_env, sizeof g_key_env, "XPS5_LOGIN_KEY=%s", line + 4);
  memset(line, 0, sizeof line);
  return 0;
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


/**
 * path as a process of its own; stdio 0 leaves it none of the title's
 **/
static pid_t
spawn(const char *path, const char *arg, int stdio, int ipc_fd, int child_fd, const char *kind) {
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
  pid = elfldr_spawn_ipc(stdio, ipc_fd, child_fd, argv, elf, size);
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
 * one process ended, killed first unless it ends by itself within wait_ms;
 * how it ended (as wait4 says, or -1) and its peak memory to the klog
 **/
static int
end_child(int i, int wait_ms) {
  struct rusage ru;
  int status = -1;
  pid_t pid = g_children[i].pid, got = 0;

  if(!pid) {
    return -1;
  }
  memset(&ru, 0, sizeof ru);
  for(int t=0; t < wait_ms / 50 && !(got=wait4(pid, &status, WNOHANG, &ru)); t++) {
    usleep(50 * 1000);
  }
  if(got != pid) {
    kill(pid, SIGKILL);
    got = wait4(pid, &status, 0, &ru);
  }
  if(got != pid) {
    say("%s not reaped (errno %d)", g_children[i].kind, errno);
    status = -1;
  } else if(WIFSIGNALED(status)) {
    say("%s ended by signal %d, peak %ld MiB", g_children[i].kind, WTERMSIG(status), ru.ru_maxrss >> 10);
  } else {
    say("%s ended with code %d, peak %ld MiB", g_children[i].kind, WEXITSTATUS(status), ru.ru_maxrss >> 10);
  }
  g_children[i].pid = 0;
  return status;
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


static int
remove_page_data(void) {
  return remove_tree(ENGINE_DIR "/web") + remove_tree(ENGINE_DIR "/tmp") + remove_tree(ENGINE_DIR "/home");
}


int
main(void) {
  launcher_req_t req;
  char path[300];
  int ctl[2], have = 0, served = 0, fd, end = LOGIN_END_CLOSED, status, n_left;
  pid_t ui;
  ssize_t n;

  signal(SIGPIPE, SIG_IGN);
  g_start = time(NULL);
  if(read_key()) {
    say("no key from the title");
    tell("error", LOGIN_END_NO_TITLE);
    return 1;
  }
  for(size_t i=0; i < sizeof g_files / sizeof g_files[0]; i++) {
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", ENGINE_DIR, g_files[i]);
    have += !stat(path, &st);
  }
  say("start: %d of %d engine files there", have, (int)(sizeof g_files / sizeof g_files[0]));
  if(have != (int)(sizeof g_files / sizeof g_files[0]) || chdir(ENGINE_DIR)) {
    tell("error", LOGIN_END_NO_FILES);
    return 1;
  }
  // what a run that ended badly left, then a clean start
  if((n_left=remove_page_data())) {
    say("%d files of an earlier window removed", n_left);
  }
  mkdir(ENGINE_DIR "/home", 0755);
  mkdir(ENGINE_DIR "/tmp", 0755);
  mkdir(ENGINE_DIR "/web", 0755);

  if(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ctl)) {
    say("no socketpair (errno %d)", errno);
    tell("error", LOGIN_END_NO_START);
    return 1;
  }
  elfldr_child_envp = g_env_ui;
  ui = spawn(ENGINE_DIR "/login-ui.elf", "XPS5Login", STDOUT_FILENO, ctl[1], BROWSERPS5_LAUNCHER_FD, "ui");
  memset(g_key_env, 0, sizeof g_key_env);
  elfldr_child_envp = g_env;
  close(ctl[1]);
  if(ui < 0) {
    tell("error", LOGIN_END_NO_START);
    close(ctl[0]);
    remove_page_data();
    return 1;
  }

  // its requests, until it closes the socket or the time is up
  for(;;) {
    struct pollfd p = {ctl[0], POLLIN, 0};
    int r = poll(&p, 1, 1000);

    if(time(NULL) - g_start >= LOGIN_SECONDS) {
      say("open %d s: closed", LOGIN_SECONDS);
      end = LOGIN_END_TIME;
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
      reply(ctl[0], fd < 0 ? -EBADF : spawn(req.path, req.arg, 0, fd, BROWSERPS5_CHILD_IPC_FD, kind));
    } else if(req.type == LAUNCHER_KILL) {
      int done = -ESRCH;
      for(int i=0; i < MAX_CHILDREN; i++) {
        if(g_children[i].pid && g_children[i].pid == req.pid) {
          end_child(i, 0);
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
  // the window process first, given a moment to end by itself: its exit
  // code is a LOGIN_END_*
  for(int i=0; i < MAX_CHILDREN; i++) {
    if(g_children[i].pid == ui) {
      status = end_child(i, end == LOGIN_END_TIME ? 0 : 2000);
      if(end != LOGIN_END_TIME) {
        end = status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) <= LOGIN_END_TIME ?
              WEXITSTATUS(status) : LOGIN_END_CRASHED;
      }
    }
  }
  for(int i=0; i < MAX_CHILDREN; i++) {
    end_child(i, 0);
  }
  tell("end", end); // the title is usually gone by now
  say("page data removed: %d files", remove_page_data());
  say("done, %ld s, end %d", (long)(time(NULL) - g_start), end);
  return 0;
}
