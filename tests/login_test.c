/* Host tests of title/loginlink.c, the title's side of the login window:
a made-up ELF loader and login engine on this computer's 127.0.0.1, met
through the same login_net_t main.c gives the PS5's sockets. They check the
launcher and its key on the loader's connection, the HELLO, the page's
frames as they merge, the state and the field, the input sent back, the
BYE, and each way it can fail. Nothing of X here: the page is two colours. */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "loginlink.h"
#include "loginproto.h"

static int g_failed;

#define CHECK(cond) do { \
  if(!(cond)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_failed++; \
  } \
} while(0)

#define PAGE_W 64
#define PAGE_H 80
#define COLOUR_A 0xff112233u
#define COLOUR_B 0xff445566u
#define FAKE_AUTH "0123456789abcdef0123456789abcdef01234567"
#define FAKE_CT0  "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"
#define FAKE_SESSION "auth_token=" FAKE_AUTH "\nct0=" FAKE_CT0 "\n"

// how the made-up loader and engine behave this time
enum { ENGINE_FINE, ENGINE_ENDS, ENGINE_WRONG_SIZE, ENGINE_BIG_SESSION, LOADER_ERROR, LOADER_SILENT };

static struct {
  int  loader_port, engine_port; // where they listen here
  int  loader_fd, engine_fd;
  int  mode;
  char launcher[256];             // the made-up launcher's file
  char key_hex[2 * LOGIN_KEY_BYTES + 1];
  // what the engine saw
  int  hello_ok, moved, clicked, typed, returned, wheeled, asked, bye;
  int  frames_sent;
  pthread_mutex_t lock;
  char log[8192];
} t = {.lock = PTHREAD_MUTEX_INITIALIZER};


/*
 * the computer's sockets as login_net_t: 9021 is the loader here, and the
 * engine's ports its own
 */

static int
net_connect(int port, int seconds) {
  struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  struct timeval tv = {seconds, 0};
  int s;

  if(port == 9021) {
    if(!t.loader_port) {
      return -111; // nothing there
    }
    port = t.loader_port;
  } else if(port >= LOGIN_PORT_FIRST && port <= LOGIN_PORT_LAST) {
    port = t.engine_port;
  } else {
    return -1;
  }
  a.sin_port = htons((uint16_t)port);
  if((s=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }
  if(seconds) {
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  }
  if(connect(s, (struct sockaddr *)&a, sizeof a)) {
    close(s);
    return -111;
  }
  return s;
}


static int
net_send(int s, const void *buf, size_t len) {
  return (int)send(s, buf, len, MSG_NOSIGNAL);
}


static int
net_recv(int s, void *buf, size_t len) {
  return (int)recv(s, buf, len, 0);
}


static void
net_shut(int s, int both) {
  shutdown(s, both ? SHUT_RDWR : SHUT_WR);
}


static void
net_close(int s) {
  close(s);
}


static void
net_random(void *buf, size_t len) {
  static unsigned char n = 0x5a;
  for(size_t i=0; i < len; i++) {
    ((unsigned char *)buf)[i] = n += 37;
  }
}


static void
net_log(const char *fmt, ...) {
  char line[256];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  pthread_mutex_lock(&t.lock);
  if(strlen(t.log) + strlen(line) + 2 < sizeof t.log) {
    strcat(t.log, line);
    strcat(t.log, "\n");
  }
  pthread_mutex_unlock(&t.lock);
}

static const login_net_t g_net = {net_connect, net_send, net_recv, net_shut, net_close, net_random, net_log};


static int
listen_here(int *port) {
  struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  socklen_t len = sizeof a;
  int s = socket(AF_INET, SOCK_STREAM, 0);

  if(s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 2) ||
     getsockname(s, (struct sockaddr *)&a, &len)) {
    fprintf(stderr, "login_test: no socket here\n");
    exit(2);
  }
  *port = ntohs(a.sin_port);
  return s;
}


static int
read_all(int s, void *buf, size_t len) {
  char *p = buf;
  ssize_t n;

  while(len) {
    if((n=recv(s, p, len, 0)) <= 0) {
      return 0;
    }
    p += n;
    len -= (size_t)n;
  }
  return 1;
}


static int
write_all(int s, const void *buf, size_t len) {
  return send(s, buf, len, MSG_NOSIGNAL) == (ssize_t)len;
}


static void
send_msg(int s, uint16_t type, uint16_t arg, const void *body, size_t size) {
  login_msg_t m = {type, arg, (uint32_t)size};
  write_all(s, &m, sizeof m);
  if(size) {
    write_all(s, body, size);
  }
}


static void
send_frame(int s, int x, int y, int w, int h, uint32_t colour) {
  size_t n = (size_t)w * h;
  uint32_t *px = malloc(n * 4);
  login_rect_t r = {(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h};
  login_msg_t m = {LOGIN_FRAME, 0, (uint32_t)(sizeof r + n * 4)};

  for(size_t i=0; i < n; i++) {
    px[i] = colour;
  }
  write_all(s, &m, sizeof m);
  write_all(s, &r, sizeof r);
  write_all(s, px, n * 4);
  free(px);
  t.frames_sent++;
}


/**
 * the loader: the launcher, then the key on what would be its stdin, until
 * the title closes its side; then the engine's line
 **/
static void *
loader_main(void *arg) {
  static unsigned char got[8192];
  size_t len = 0;
  ssize_t n;
  int s;
  FILE *f;
  unsigned char elf[4096];
  size_t elf_len;

  (void)arg;
  if((s=accept(t.loader_fd, NULL, NULL)) < 0) {
    return NULL;
  }
  while(len < sizeof got && (n=recv(s, got + len, sizeof got - len, 0)) > 0) {
    len += (size_t)n;
  }
  f = fopen(t.launcher, "rb");
  elf_len = f ? fread(elf, 1, sizeof elf, f) : 0;
  if(f) {
    fclose(f);
  }
  // the launcher as it is, then "key <hex>\n" and nothing more
  CHECK(len == elf_len + 5 + 2 * LOGIN_KEY_BYTES);
  CHECK(len > elf_len && !memcmp(got, elf, elf_len));
  if(len == elf_len + 5 + 2 * LOGIN_KEY_BYTES) {
    CHECK(!memcmp(got + elf_len, "key ", 4));
    CHECK(got[len - 1] == '\n');
    memcpy(t.key_hex, got + elf_len + 4, 2 * LOGIN_KEY_BYTES);
    t.key_hex[2 * LOGIN_KEY_BYTES] = 0;
  }
  if(t.mode == LOADER_ERROR) {
    write_all(s, "[elfldr] something\nxps5-login error 3\n", 38);
  } else if(t.mode != LOADER_SILENT) {
    // a library's line first, longer than a short buffer (as libvulkan's on
    // the PS5), and an empty one
    char noise[400];
    memset(noise, 'v', sizeof noise);
    memcpy(noise, "[libvulkan] pid 315: ", 21);
    noise[sizeof noise - 1] = '\n';
    write_all(s, noise, sizeof noise);
    write_all(s, "\n[elfldr] spawned\nxps5-login port 9074\n", 39);
  }
  close(s);
  return NULL;
}


static int
hex_value(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}


/**
 * the engine: HELLO with the key, the page in two frames, its state and a
 * field, then what the title sends until BYE
 **/
static void *
engine_main(void *arg) {
  login_msg_t m;
  login_hello_t hello;
  login_ready_t ready = {PAGE_W, PAGE_H};
  login_state_t st;
  char body[LOGIN_TEXT_MAX + 64];
  uint8_t key[LOGIN_KEY_BYTES];
  int s;

  (void)arg;
  if((s=accept(t.engine_fd, NULL, NULL)) < 0) {
    return NULL;
  }
  if(!read_all(s, &m, sizeof m) || m.type != LOGIN_HELLO || m.size != sizeof hello ||
     !read_all(s, &hello, sizeof hello)) {
    close(s);
    return NULL;
  }
  for(int i=0; i < LOGIN_KEY_BYTES; i++) {
    key[i] = (uint8_t)(hex_value(t.key_hex[2 * i]) << 4 | hex_value(t.key_hex[2 * i + 1]));
  }
  t.hello_ok = hello.magic == LOGIN_MAGIC && hello.version == LOGIN_VERSION && hello.zoom == 140 &&
               hello.width == PAGE_W && hello.height == PAGE_H && !strcmp(hello.lang, "pt") &&
               !memcmp(hello.key, key, sizeof key);
  if(t.mode == ENGINE_WRONG_SIZE) {
    login_ready_t wrong = {PAGE_W + 1, PAGE_H};
    send_msg(s, LOGIN_READY, 0, &wrong, sizeof wrong);
    send_frame(s, 0, 0, PAGE_W, PAGE_H, COLOUR_A); // never read
    while(read_all(s, &m, sizeof m) && (!m.size || read_all(s, body, m.size))) {
    }
    close(s);
    return NULL;
  }
  send_msg(s, LOGIN_READY, 0, &ready, sizeof ready);
  send_frame(s, 0, 0, PAGE_W, PAGE_H, COLOUR_A);
  send_frame(s, 3, 2, 5, 4, COLOUR_B);
  send_frame(s, PAGE_W - 2, 0, 5, 4, COLOUR_B); // out of the page: left out
  memset(&st, 0, sizeof st);
  st.loading = 1;
  st.progress = 40;
  st.on_x = 1;
  st.blocked = 2;
  send_msg(s, LOGIN_STATE, 0, &st, sizeof st);
  send_msg(s, 200, 7, "abcdefg", 7); // one this side does not know: skipped
  send_msg(s, LOGIN_FIELD, LOGIN_FIELD_EMAIL, "me@example.com", 14);
  if(t.mode == ENGINE_BIG_SESSION) {
    static char big[LOGIN_SESSION_MAX + 1];
    memset(big, 'a', sizeof big);
    send_msg(s, LOGIN_SESSION, 0, big, sizeof big);
    while(read_all(s, &m, sizeof m) && (!m.size || read_all(s, body, m.size))) {
    }
    close(s);
    return NULL;
  }
  send_msg(s, LOGIN_SESSION, 0, FAKE_SESSION, strlen(FAKE_SESSION)); // the page signed in
  if(t.mode == ENGINE_ENDS) {
    send_msg(s, LOGIN_END, LOGIN_END_TIME, NULL, 0);
    close(s);
    return NULL;
  }
  while(read_all(s, &m, sizeof m)) {
    if(m.size > sizeof body || (m.size && !read_all(s, body, m.size))) {
      break;
    }
    if(m.type == LOGIN_POINTER && m.size == sizeof(login_point_t)) {
      login_point_t p;
      memcpy(&p, body, sizeof p);
      t.moved += m.arg == LOGIN_MOVE && p.x == 10 && p.y == 20;
      t.clicked += (m.arg == LOGIN_DOWN || m.arg == LOGIN_UP) && p.x == 11 && p.y == 21;
    } else if(m.type == LOGIN_WHEEL && m.size == sizeof(login_wheel_t)) {
      login_wheel_t w;
      memcpy(&w, body, sizeof w);
      t.wheeled += w.dy == 120;
    } else if(m.type == LOGIN_TEXT) {
      t.typed += m.arg == 1 && m.size == 5 && !memcmp(body, "hello", 5);
    } else if(m.type == LOGIN_KEY) {
      t.returned += m.arg == LOGIN_KEY_RETURN;
    } else if(m.type == LOGIN_ASK_FIELD) {
      t.asked++;
      send_msg(s, LOGIN_FIELD, LOGIN_FIELD_PASSWORD | LOGIN_FIELD_ASKED, NULL, 0);
    } else if(m.type == LOGIN_BYE) {
      t.bye++;
    }
  }
  close(s);
  return NULL;
}


static void
wait_ms(int ms) {
  struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}


/**
 * until the link says so (or two seconds)
 **/
static int
wait_for(int (*done)(const login_link_state_t *)) {
  login_link_state_t st;

  for(int i=0; i < 400; i++) {
    loginlink_state(&st);
    if(done(&st)) {
      return 1;
    }
    wait_ms(5);
  }
  return 0;
}


static int
is_shown(const login_link_state_t *st) {
  return st->state == LOGIN_LINK_SHOWN && st->frames >= 2 && st->fields >= 1 && st->loading;
}


static int
has_session(const login_link_state_t *st) {
  return st->sessions >= 1;
}


static int
is_failed(const login_link_state_t *st) {
  return st->state == LOGIN_LINK_FAILED;
}


static int
asked_back(const login_link_state_t *st) {
  return st->fields >= 2;
}


static void
write_launcher(void) {
  FILE *f = fopen(t.launcher, "wb");
  for(int i=0; i < 3000; i++) {
    fputc((i * 7 + 1) & 0xff, f);
  }
  fclose(f);
}


/**
 * one window: the loader and the engine started here for it
 **/
static void
run(int mode, pthread_t *loader, pthread_t *engine) {
  t.mode = mode;
  t.hello_ok = t.moved = t.clicked = t.typed = t.returned = t.wheeled = t.asked = t.bye = 0;
  t.frames_sent = 0;
  memset(t.key_hex, 0, sizeof t.key_hex);
  pthread_create(loader, NULL, loader_main, NULL);
  pthread_create(engine, NULL, engine_main, NULL);
}


static void
stop_engine_thread(pthread_t engine) {
  // an engine the title never met: met here, so its thread ends
  int s = net_connect(LOGIN_PORT_FIRST, 1);
  if(s >= 0) {
    close(s);
  }
  pthread_join(engine, NULL);
}


static void
wait_ended(void) {
  for(int i=0; i < 400 && loginlink_running(); i++) {
    wait_ms(5);
  }
  CHECK(!loginlink_running());
}


static void
test_window(void) {
  pthread_t loader, engine;
  login_link_state_t st;
  const uint32_t *px;
  login_point_t move = {10, 20}, click = {11, 21};
  login_wheel_t wheel = {30, 40, 0, 120};
  int w, h, changed, kind;
  char text[64], session[LOGIN_SESSION_MAX + 1];

  run(ENGINE_FINE, &loader, &engine);
  CHECK(!loginlink_start(&g_net, t.launcher, PAGE_W, PAGE_H, 140, "pt"));
  CHECK(loginlink_start(&g_net, t.launcher, PAGE_W, PAGE_H, 140, "pt") == -1); // one at a time
  CHECK(loginlink_running());
  CHECK(wait_for(is_shown));
  pthread_join(loader, NULL);
  CHECK(t.hello_ok);

  // the page: A with B where the second frame was; the third left out
  px = loginlink_pixels(&w, &h, &changed);
  CHECK(px && w == PAGE_W && h == PAGE_H && changed);
  if(px) {
    CHECK(px[0] == COLOUR_A && px[PAGE_W * PAGE_H - 1] == COLOUR_A);
    CHECK(px[2 * PAGE_W + 3] == COLOUR_B && px[5 * PAGE_W + 7] == COLOUR_B);
    CHECK(px[2 * PAGE_W + 8] == COLOUR_A && px[6 * PAGE_W + 3] == COLOUR_A);
    CHECK(px[PAGE_W - 1] == COLOUR_A);
  }
  px = loginlink_pixels(&w, &h, &changed);
  CHECK(px && !changed); // nothing new since

  loginlink_state(&st);
  CHECK(st.progress == 40 && st.on_x == 1 && st.blocked == 2 && !st.crashed && !st.error);
  CHECK(st.width == PAGE_W && st.height == PAGE_H);
  CHECK(loginlink_field(&kind, text, sizeof text));
  CHECK(kind == LOGIN_FIELD_EMAIL && !strcmp(text, "me@example.com"));
  CHECK(!loginlink_field(&kind, text, sizeof text)); // once

  // X's session, once, as it came
  CHECK(wait_for(has_session));
  loginlink_state(&st);
  CHECK(st.sessions == 1);
  CHECK(loginlink_session(session, sizeof session) == (int)strlen(FAKE_SESSION));
  CHECK(!strcmp(session, FAKE_SESSION));
  CHECK(loginlink_session(session, sizeof session) == 0);

  // what the controller did, to the engine
  CHECK(!loginlink_send(LOGIN_POINTER, LOGIN_MOVE, &move, sizeof move));
  CHECK(!loginlink_send(LOGIN_POINTER, LOGIN_DOWN, &click, sizeof click));
  CHECK(!loginlink_send(LOGIN_POINTER, LOGIN_UP, &click, sizeof click));
  CHECK(!loginlink_send(LOGIN_WHEEL, 0, &wheel, sizeof wheel));
  CHECK(!loginlink_send(LOGIN_TEXT, 1, "hello", 5));
  CHECK(!loginlink_send(LOGIN_KEY, LOGIN_KEY_RETURN, NULL, 0));
  CHECK(!loginlink_send(LOGIN_ASK_FIELD, 0, NULL, 0));
  CHECK(wait_for(asked_back));
  CHECK(loginlink_field(&kind, text, sizeof text));
  CHECK(kind == (LOGIN_FIELD_PASSWORD | LOGIN_FIELD_ASKED) && !text[0]);

  // the window closes: BYE, and the thread ends without a failure
  loginlink_stop();
  pthread_join(engine, NULL);
  wait_ended();
  CHECK(t.moved == 1 && t.clicked == 2 && t.wheeled == 1 && t.typed == 1 && t.returned == 1);
  CHECK(t.asked == 1 && t.bye == 1);
  loginlink_state(&st);
  CHECK(st.state == LOGIN_LINK_SHOWN);
  CHECK(loginlink_send(LOGIN_KEY, LOGIN_KEY_TAB, NULL, 0) == -1); // nobody there
  loginlink_free();
  px = loginlink_pixels(&w, &h, &changed);
  CHECK(!px && !changed);

  // the key went to the loader and the engine, never to the log
  pthread_mutex_lock(&t.lock);
  CHECK(t.key_hex[0] && !strstr(t.log, t.key_hex) && !strstr(t.log, t.key_hex + 32));
  CHECK(!strstr(t.log, "example.com") && !strstr(t.log, "hello"));
  CHECK(!strstr(t.log, FAKE_AUTH) && !strstr(t.log, FAKE_CT0) && strstr(t.log, "session came"));
  pthread_mutex_unlock(&t.lock);
}


static void
test_failures(void) {
  pthread_t loader, engine;
  login_link_state_t st;
  const uint32_t *px;
  int w, h, changed;
  char missing[300];

  // the engine ends on its own: lost, with its code
  run(ENGINE_ENDS, &loader, &engine);
  CHECK(!loginlink_start(&g_net, t.launcher, PAGE_W, PAGE_H, 140, "pt"));
  CHECK(wait_for(is_failed));
  pthread_join(loader, NULL);
  pthread_join(engine, NULL);
  wait_ended();
  loginlink_state(&st);
  CHECK(st.why == LOGIN_LINK_LOST && st.code == LOGIN_END_TIME && st.frames == 2);
  px = loginlink_pixels(&w, &h, &changed);
  CHECK(px && changed); // the page it showed stays until the window closes
  loginlink_free();

  // a page of another size than asked: left
  run(ENGINE_WRONG_SIZE, &loader, &engine);
  CHECK(!loginlink_start(&g_net, t.launcher, PAGE_W, PAGE_H, 140, "en"));
  CHECK(wait_for(is_failed));
  pthread_join(loader, NULL);
  pthread_join(engine, NULL);
  wait_ended();
  loginlink_state(&st);
  CHECK(st.why == LOGIN_LINK_LOST && st.frames == 0);
  loginlink_free();

  // a session longer than any: left, and none kept
  run(ENGINE_BIG_SESSION, &loader, &engine);
  CHECK(!loginlink_start(&g_net, t.launcher, PAGE_W, PAGE_H, 140, "pt"));
  CHECK(wait_for(is_failed));
  pthread_join(loader, NULL);
  pthread_join(engine, NULL);
  wait_ended();
  loginlink_state(&st);
  CHECK(st.why == LOGIN_LINK_LOST && st.sessions == 0);
  CHECK(loginlink_session(missing, sizeof missing) == 0);
  loginlink_free();

  // the launcher could not start the engine: its error
  run(LOADER_ERROR, &loader, &engine);
  CHECK(!loginlink_start(&g_net, t.launcher, PAGE_W, PAGE_H, 140, "pt"));
  CHECK(wait_for(is_failed));
  pthread_join(loader, NULL);
  stop_engine_thread(engine);
  wait_ended();
  loginlink_state(&st);
  CHECK(st.why == LOGIN_LINK_NO_START && st.code == 3);
  loginlink_free();

  // no line from it at all
  run(LOADER_SILENT, &loader, &engine);
  CHECK(!loginlink_start(&g_net, t.launcher, PAGE_W, PAGE_H, 140, "pt"));
  CHECK(wait_for(is_failed));
  pthread_join(loader, NULL);
  stop_engine_thread(engine);
  wait_ended();
  loginlink_state(&st);
  CHECK(st.why == LOGIN_LINK_NO_START && st.code == -1);
  loginlink_free();

  // no engine on the console
  snprintf(missing, sizeof missing, "%s.missing", t.launcher);
  CHECK(!loginlink_start(&g_net, missing, PAGE_W, PAGE_H, 140, "pt"));
  CHECK(wait_for(is_failed));
  wait_ended();
  loginlink_state(&st);
  CHECK(st.why == LOGIN_LINK_NO_ENGINE && st.code == ENOENT);
  loginlink_free();

  // an empty one, as a write cut short leaves it
  snprintf(missing, sizeof missing, "%s.empty", t.launcher);
  fclose(fopen(missing, "wb"));
  CHECK(!loginlink_start(&g_net, missing, PAGE_W, PAGE_H, 140, "pt"));
  CHECK(wait_for(is_failed));
  wait_ended();
  loginlink_state(&st);
  CHECK(st.why == LOGIN_LINK_NO_ENGINE && st.code == -2);
  loginlink_free();
  unlink(missing);

  // no loader
  w = t.loader_port;
  t.loader_port = 0;
  CHECK(!loginlink_start(&g_net, t.launcher, PAGE_W, PAGE_H, 140, "pt"));
  CHECK(wait_for(is_failed));
  wait_ended();
  loginlink_state(&st);
  CHECK(st.why == LOGIN_LINK_NO_LOADER && st.code == -111);
  loginlink_free();
  t.loader_port = w;

  // sizes out of range
  CHECK(loginlink_start(&g_net, t.launcher, 32, PAGE_H, 140, "pt") == -1);
  CHECK(loginlink_start(&g_net, t.launcher, PAGE_W, LOGIN_SIDE_MAX + 1, 140, "pt") == -1);
  CHECK(!loginlink_running());
}


int
main(void) {
  const char *tmp = getenv("TMPDIR");

  alarm(30); // a test that hangs fails
  snprintf(t.launcher, sizeof t.launcher, "%s/login_test_%d.elf", tmp && tmp[0] ? tmp : "/tmp", (int)getpid());
  write_launcher();
  t.loader_fd = listen_here(&t.loader_port);
  t.engine_fd = listen_here(&t.engine_port);
  test_window();
  test_failures();
  close(t.loader_fd);
  close(t.engine_fd);
  unlink(t.launcher);
  if(g_failed) {
    fprintf(stderr, "login_test: %d failed\n", g_failed);
    return 1;
  }
  printf("login_test: ok\n");
  return 0;
}
