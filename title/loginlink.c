/* The title's side of the login window, see loginlink.h and
src/loginproto.h. One thread starts the engine and then reads what it sends;
main.c's thread sends the controller's input (under a lock of its own) and
takes the page, which comes into one buffer while the screen draws another:
what changed goes over once a frame. The key the engine is met with is made
here for each window and wiped once sent. */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "loginlink.h"
#include "loginproto.h"
#include "mem.h"

#define LOADER_PORT   9021            // the ELF loader of etaHEN
#define LAUNCHER_MAX  (4 << 20)
#define LOADER_SECS   10              // to reach the loader, and for the engine's line with its port
#define LOADER_LINES  256             // lines before the port's, at most
#define LOADER_BYTES  (64 << 10)      // and bytes
#define WHITE         0xffffffffu     // the page before its first frame

static struct {
  pthread_mutex_t    lock;      // st, the part of back not in front, the field
  pthread_mutex_t    send_lock; // sock, and what is sent on it
  int                running;   // the thread, atomic
  int                stop;      // the window closed, atomic
  int                sock;      // the engine's connection, or -1
  const login_net_t *net;
  char               path[160];
  int                width, height, zoom;
  char               lang[8];
  login_link_state_t st;
  uint32_t          *back, *front; // the page as it comes, and as the screen draws it
  int                bw, bh;       // their size
  int                x0, y0, x1, y1; // back's part not in front yet (x1 <= x0: none)
  int                have;          // front holds the page
  int                field_new, field_kind;
  char               field[LOGIN_TEXT_MAX + 1];
  int                session_new;   // a LOGIN_SESSION not taken yet
  char               session[LOGIN_SESSION_MAX + 1];
} g = {.lock = PTHREAD_MUTEX_INITIALIZER, .send_lock = PTHREAD_MUTEX_INITIALIZER, .sock = -1};


static int
send_all(int s, const void *buf, size_t len) {
  const char *p = buf;
  int n;

  while(len) {
    if((n=g.net->send(s, p, len)) <= 0) {
      return 0;
    }
    p += n;
    len -= (size_t)n;
  }
  return 1;
}


static int
recv_all(int s, void *buf, size_t len) {
  char *p = buf;
  int n;

  while(len) {
    if((n=g.net->recv(s, p, len)) <= 0) {
      return 0;
    }
    p += n;
    len -= (size_t)n;
  }
  return 1;
}


/**
 * len bytes read and left
 **/
static int
skip(int s, size_t len) {
  char buf[512];

  while(len) {
    size_t n = len < sizeof buf ? len : sizeof buf;
    if(!recv_all(s, buf, n)) {
      return 0;
    }
    len -= n;
  }
  return 1;
}


/**
 * the launcher opened to be sent, its size in *len: the descriptor, or -1
 * with why (LOGIN_LINK_NO_ENGINE when it is not there, NO_READ when it is
 * but cannot be read) and its code (errno, or -2 for an empty or too big
 * file). Nothing big is allocated for it: it goes in pieces (send_file).
 * 2026-11-13, after a game: reading it whole into memory failed every time
 * until XPS5 was opened again, the file being there; what failed was not
 * logged then, as it is now
 **/
static int
open_launcher(const char *path, size_t *len, int *why, int *code) {
  off_t size;
  int fd;

  if((fd=open(path, O_RDONLY)) < 0) {
    *code = errno;
    *why = errno == ENOENT || errno == ENOTDIR ? LOGIN_LINK_NO_ENGINE : LOGIN_LINK_NO_READ;
    g.net->log("login: launcher not opened, errno %d", *code);
    return -1;
  }
  if((size=lseek(fd, 0, SEEK_END)) < 0 || lseek(fd, 0, SEEK_SET)) {
    *code = errno;
    *why = LOGIN_LINK_NO_READ;
    g.net->log("login: launcher not read, errno %d", *code);
    close(fd);
    return -1;
  }
  if(!size || size > LAUNCHER_MAX) {
    *code = -2;
    *why = LOGIN_LINK_NO_ENGINE;
    g.net->log("login: a launcher of %lld bytes", (long long)size);
    close(fd);
    return -1;
  }
  *len = (size_t)size;
  return fd;
}


/**
 * len bytes of fd sent on s, a piece at a time: 1, or 0 when the
 * connection broke, or when the file could not be read (*err: errno, or -3
 * when it ended first; 0 otherwise)
 **/
static int
send_file(int s, int fd, size_t len, int *err) {
  static unsigned char buf[16 << 10]; // link_main's, one window at a time
  ssize_t n;

  *err = 0;
  while(len) {
    if((n=read(fd, buf, len < sizeof buf ? len : sizeof buf)) < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      *err = n < 0 ? errno : -3;
      return 0;
    }
    if(!send_all(s, buf, (size_t)n)) {
      return 0;
    }
    len -= (size_t)n;
  }
  return 1;
}


static void
fail(int why, int code) {
  pthread_mutex_lock(&g.lock);
  g.st.state = LOGIN_LINK_FAILED;
  g.st.why = why;
  g.st.code = code;
  pthread_mutex_unlock(&g.lock);
  g.net->log("login: failed %d, code %d", why, code);
}


/**
 * the engine's port from its line on the loader's connection, or 0 with
 * its error (a LOGIN_END_*) or -1 in *code. The libraries the engine loads
 * may write lines of their own there first, of any length (libvulkan's is
 * about a hundred characters): each is read to its end, and only its first
 * part is looked at. The klog gets counts only, never the lines.
 **/
static int
read_port(int s, int *code) {
  char line[256], c;
  const char *at;
  size_t len = 0, bytes = 0;
  int n, value, lines = 0;

  *code = -1;
  while(lines < LOADER_LINES && bytes < LOADER_BYTES) {
    if((n=g.net->recv(s, &c, 1)) != 1) {
      g.net->log("login: %d lines from the loader, then %s %d", lines, n ? "an error" : "the end", n);
      return 0; // the end, or nothing for LOADER_SECS
    }
    bytes++;
    if(c != '\n') {
      if(len + 1 < sizeof line) {
        line[len++] = c;
      }
      continue;
    }
    line[len] = 0;
    len = 0;
    lines++;
    if(!(at=strstr(line, "xps5-login "))) {
      continue;
    }
    if(sscanf(at, "xps5-login port %d", &value) == 1 && value >= LOGIN_PORT_FIRST && value <= LOGIN_PORT_LAST) {
      return value;
    }
    if(sscanf(at, "xps5-login error %d", &value) == 1) {
      *code = value;
      return 0;
    }
  }
  g.net->log("login: %d lines from the loader, none with the port", lines);
  return 0;
}


/**
 * a frame: its pixels into back, where the rectangle says
 **/
static int
take_frame(int s, const login_msg_t *m) {
  login_rect_t r;

  if(m->size < sizeof r || !recv_all(s, &r, sizeof r)) {
    return 0;
  }
  if(!r.w || !r.h || r.x + r.w > g.bw || r.y + r.h > g.bh ||
     m->size != sizeof r + (uint64_t)r.w * r.h * 4) {
    g.net->log("login: a frame out of the page");
    return skip(s, m->size - sizeof r);
  }
  if(r.x == 0 && r.w == g.bw) {
    if(!recv_all(s, g.back + (size_t)r.y * g.bw, (size_t)r.w * r.h * 4)) {
      return 0;
    }
  } else {
    for(int y=0; y < r.h; y++) {
      if(!recv_all(s, g.back + (size_t)(r.y + y) * g.bw + r.x, (size_t)r.w * 4)) {
        return 0;
      }
    }
  }
  pthread_mutex_lock(&g.lock);
  if(g.x1 <= g.x0) {
    g.x0 = r.x, g.y0 = r.y, g.x1 = r.x + r.w, g.y1 = r.y + r.h;
  } else {
    g.x0 = r.x < g.x0 ? r.x : g.x0;
    g.y0 = r.y < g.y0 ? r.y : g.y0;
    g.x1 = r.x + r.w > g.x1 ? r.x + r.w : g.x1;
    g.y1 = r.y + r.h > g.y1 ? r.y + r.h : g.y1;
  }
  g.st.frames++;
  if(g.st.state == LOGIN_LINK_STARTING) {
    g.st.state = LOGIN_LINK_SHOWN;
  }
  pthread_mutex_unlock(&g.lock);
  return 1;
}


/**
 * what the engine sends, until it ends or the window closes; its end code
 * (LOGIN_END_*) when it told one, else -1
 **/
static int
take(int s) {
  char text[LOGIN_TEXT_MAX + 1];
  login_ready_t ready;
  login_state_t ls;
  login_msg_t m;
  int ok = 1, end = -1;

  while(ok && recv_all(s, &m, sizeof m)) {
    switch(m.type) {
    case LOGIN_READY:
      if(m.size != sizeof ready || !recv_all(s, &ready, sizeof ready)) {
        ok = 0;
      } else if(ready.width != g.bw || ready.height != g.bh) {
        g.net->log("login: a page of another size, %dx%d", ready.width, ready.height);
        ok = 0;
      } else {
        pthread_mutex_lock(&g.lock);
        g.st.width = ready.width;
        g.st.height = ready.height;
        pthread_mutex_unlock(&g.lock);
      }
      break;
    case LOGIN_FRAME:
      ok = take_frame(s, &m);
      break;
    case LOGIN_STATE:
      if(m.size != sizeof ls || !recv_all(s, &ls, sizeof ls)) {
        ok = 0;
        break;
      }
      pthread_mutex_lock(&g.lock);
      g.st.loading = ls.loading;
      g.st.progress = ls.progress;
      g.st.on_x = ls.on_x;
      g.st.crashed = ls.crashed;
      g.st.error = ls.error;
      g.st.blocked = ls.blocked;
      pthread_mutex_unlock(&g.lock);
      break;
    case LOGIN_FIELD:
      if(m.size > LOGIN_TEXT_MAX || !recv_all(s, text, m.size)) {
        ok = 0;
        break;
      }
      text[m.size] = 0;
      pthread_mutex_lock(&g.lock);
      g.field_kind = m.arg;
      memcpy(g.field, text, m.size + 1);
      g.field_new = 1;
      g.st.fields++;
      pthread_mutex_unlock(&g.lock);
      memset(text, 0, sizeof text);
      break;
    case LOGIN_SESSION:
      if(!m.size || m.size > LOGIN_SESSION_MAX || !recv_all(s, text, m.size)) {
        ok = 0;
        break;
      }
      text[m.size] = 0;
      pthread_mutex_lock(&g.lock);
      memcpy(g.session, text, m.size + 1);
      g.session_new = 1;
      g.st.sessions++;
      pthread_mutex_unlock(&g.lock);
      memset(text, 0, sizeof text);
      g.net->log("login: X's session came");
      break;
    case LOGIN_END:
      g.net->log("login: the engine ends, %d", m.arg);
      skip(s, m.size);
      end = m.arg;
      ok = 0;
      break;
    default:
      ok = skip(s, m.size);
      break;
    }
  }
  return end;
}


static void *
link_main(void *arg) {
  const login_net_t *net = g.net;
  uint8_t key[LOGIN_KEY_BYTES];
  char line[8 + 2 * LOGIN_KEY_BYTES];
  login_hello_t hello;
  login_msg_t m;
  size_t len = 0;
  int s, fd, port, code = -1, why = LOGIN_LINK_NO_ENGINE, sent, err, end = -1;

  (void)arg;
  if((fd=open_launcher(g.path, &len, &why, &code)) < 0) {
    fail(why, code);
    goto out;
  }
  if((s=net->connect(LOADER_PORT, LOADER_SECS)) < 0) {
    close(fd);
    fail(LOGIN_LINK_NO_LOADER, s);
    goto out;
  }
  // the launcher, then the key on its stdin; the loader runs it once
  // the sending side is closed
  net->random(key, sizeof key);
  memcpy(line, "key ", 4);
  for(int i=0; i < LOGIN_KEY_BYTES; i++) {
    snprintf(line + 4 + 2 * i, 3, "%02x", key[i]);
  }
  line[4 + 2 * LOGIN_KEY_BYTES] = '\n';
  sent = send_file(s, fd, len, &err) && send_all(s, line, 5 + 2 * LOGIN_KEY_BYTES);
  memset(line, 0, sizeof line);
  close(fd);
  if(err) {
    // the loader has part of it and no key: it runs nothing
    net->close(s);
    memset(key, 0, sizeof key);
    net->log("login: launcher not read whole, %d", err);
    fail(LOGIN_LINK_NO_READ, err);
    goto out;
  }
  net->shut(s, 0);
  port = sent ? read_port(s, &code) : 0;
  net->close(s);
  net->log("login: launcher %zu bytes %s, port %d", len, sent ? "sent" : "not sent", port);
  if(!port) {
    memset(key, 0, sizeof key);
    fail(LOGIN_LINK_NO_START, sent ? code : -1);
    goto out;
  }
  if(__atomic_load_n(&g.stop, __ATOMIC_ACQUIRE)) {
    memset(key, 0, sizeof key);
    goto out; // the engine gives up on its own, without the title
  }
  if((s=net->connect(port, 0)) < 0) {
    memset(key, 0, sizeof key);
    fail(LOGIN_LINK_NO_START, s);
    goto out;
  }
  memset(&hello, 0, sizeof hello);
  hello.magic = LOGIN_MAGIC;
  hello.version = LOGIN_VERSION;
  hello.zoom = (uint16_t)g.zoom;
  hello.width = (uint16_t)g.width;
  hello.height = (uint16_t)g.height;
  snprintf(hello.lang, sizeof hello.lang, "%s", g.lang);
  memcpy(hello.key, key, sizeof key);
  memset(key, 0, sizeof key);
  m.type = LOGIN_HELLO;
  m.arg = 0;
  m.size = sizeof hello;
  pthread_mutex_lock(&g.send_lock);
  sent = send_all(s, &m, sizeof m) && send_all(s, &hello, sizeof hello);
  memset(&hello, 0, sizeof hello);
  g.sock = sent ? s : -1;
  pthread_mutex_unlock(&g.send_lock);
  if(!sent) {
    net->close(s);
    fail(LOGIN_LINK_NO_START, -2);
    goto out;
  }
  net->log("login: the engine met on its port");
  if(!__atomic_load_n(&g.stop, __ATOMIC_ACQUIRE)) {
    end = take(s);
  }

  pthread_mutex_lock(&g.send_lock);
  g.sock = -1;
  pthread_mutex_unlock(&g.send_lock);
  net->close(s);
  if(!__atomic_load_n(&g.stop, __ATOMIC_ACQUIRE)) {
    fail(LOGIN_LINK_LOST, end);
  } else {
    pthread_mutex_lock(&g.lock);
    net->log("login: closed after %u frames, %u fields", g.st.frames, g.st.fields);
    pthread_mutex_unlock(&g.lock);
  }
out:
  __atomic_store_n(&g.running, 0, __ATOMIC_RELEASE);
  return NULL;
}


int
loginlink_start(const login_net_t *net, const char *path, int width, int height, int zoom, const char *lang) {
  const size_t n = (size_t)width * height;
  pthread_attr_t attr;
  pthread_t thread;
  int ok;

  if(__atomic_load_n(&g.running, __ATOMIC_ACQUIRE) || width < 64 || height < 64 || width > LOGIN_SIDE_MAX ||
     height > LOGIN_SIDE_MAX) {
    return -1;
  }
  if(g.bw != width || g.bh != height || !g.back || !g.front) {
    mem_free(g.back);
    mem_free(g.front);
    g.back = mem_alloc(n * 4);
    g.front = mem_alloc(n * 4);
    g.bw = g.back && g.front ? width : 0;
    g.bh = g.back && g.front ? height : 0;
    if(!g.bw) {
      return -1;
    }
  }
  for(size_t i=0; i < n; i++) {
    g.back[i] = g.front[i] = WHITE;
  }
  g.net = net;
  snprintf(g.path, sizeof g.path, "%s", path);
  g.width = width;
  g.height = height;
  g.zoom = zoom;
  snprintf(g.lang, sizeof g.lang, "%s", lang && lang[0] ? lang : "en");
  memset(&g.st, 0, sizeof g.st);
  g.st.state = LOGIN_LINK_STARTING;
  g.x0 = g.y0 = g.x1 = g.y1 = 0;
  g.have = 0;
  g.field_new = 0;
  memset(g.field, 0, sizeof g.field);
  g.session_new = 0;
  memset(g.session, 0, sizeof g.session);
  __atomic_store_n(&g.stop, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g.running, 1, __ATOMIC_RELEASE);
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 256 << 10);
  ok = !pthread_create(&thread, &attr, link_main, NULL);
  pthread_attr_destroy(&attr);
  if(!ok) {
    __atomic_store_n(&g.running, 0, __ATOMIC_RELEASE);
    return -1;
  }
  pthread_detach(thread);
  return 0;
}


void
loginlink_stop(void) {
  login_msg_t m = {LOGIN_BYE, 0, 0};

  __atomic_store_n(&g.stop, 1, __ATOMIC_RELEASE);
  pthread_mutex_lock(&g.send_lock);
  if(g.sock >= 0) {
    send_all(g.sock, &m, sizeof m);
    g.net->shut(g.sock, 1); // the thread's read ends
  }
  pthread_mutex_unlock(&g.send_lock);
  pthread_mutex_lock(&g.lock);
  memset(g.field, 0, sizeof g.field);
  g.field_new = 0;
  memset(g.session, 0, sizeof g.session);
  g.session_new = 0;
  pthread_mutex_unlock(&g.lock);
}


void
loginlink_free(void) {
  if(__atomic_load_n(&g.running, __ATOMIC_ACQUIRE)) {
    return;
  }
  mem_free(g.back);
  mem_free(g.front);
  g.back = g.front = NULL;
  g.bw = g.bh = 0;
  g.have = 0;
}


int
loginlink_running(void) {
  return __atomic_load_n(&g.running, __ATOMIC_ACQUIRE);
}


void
loginlink_state(login_link_state_t *st) {
  pthread_mutex_lock(&g.lock);
  *st = g.st;
  pthread_mutex_unlock(&g.lock);
}


const uint32_t *
loginlink_pixels(int *width, int *height, int *changed) {
  *changed = 0;
  pthread_mutex_lock(&g.lock);
  if(g.x1 > g.x0 && g.front) {
    for(int y=g.y0; y < g.y1; y++) {
      memcpy(g.front + (size_t)y * g.bw + g.x0, g.back + (size_t)y * g.bw + g.x0, (size_t)(g.x1 - g.x0) * 4);
    }
    g.x0 = g.y0 = g.x1 = g.y1 = 0;
    g.have = 1;
    *changed = 1;
  }
  pthread_mutex_unlock(&g.lock);
  *width = g.bw;
  *height = g.bh;
  return g.have ? g.front : NULL;
}


int
loginlink_field(int *kind, char *text, size_t size) {
  int got;

  pthread_mutex_lock(&g.lock);
  if((got=g.field_new)) {
    *kind = g.field_kind;
    snprintf(text, size, "%s", g.field);
    memset(g.field, 0, sizeof g.field);
    g.field_new = 0;
  }
  pthread_mutex_unlock(&g.lock);
  return got;
}


int
loginlink_session(char *text, size_t size) {
  size_t len = 0;

  pthread_mutex_lock(&g.lock);
  if(g.session_new) {
    if(size) {
      len = strlen(g.session);
      len = len < size ? len : size - 1;
      memcpy(text, g.session, len);
      text[len] = 0;
    }
    memset(g.session, 0, sizeof g.session);
    g.session_new = 0;
  }
  pthread_mutex_unlock(&g.lock);
  return (int)len;
}


int
loginlink_send(uint16_t type, uint16_t arg, const void *body, size_t size) {
  login_msg_t m = {type, arg, (uint32_t)size};
  int ok;

  pthread_mutex_lock(&g.send_lock);
  ok = g.sock >= 0 && send_all(g.sock, &m, sizeof m) && (!size || send_all(g.sock, body, size));
  pthread_mutex_unlock(&g.send_lock);
  return ok ? 0 : -1;
}
