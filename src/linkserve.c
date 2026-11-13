/* Linking an X account from a PC, in the notify payload; see linkserve.h. */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <ps5/klog.h>

#include "linkpage.h" // the QR's page, src/linkpage/ by tools/embed.py
#include "linkserve.h"
#include "settings.h"

#define SCREEN_NAME "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"

static struct {
  pthread_mutex_t    lock;
  int                running; // the thread
  int                stop;    // asked to stop
  int                open;    // a screen asked since the last "unlink"
  unsigned           screen;  // which one: the title counts them
  int                account; // the one it links, 1 to 5
  char               lang[4]; // what the extension is told
  time_t             asked;   // when the title asked last
  link_state_t       st;
  unsigned long long seed;
} g_ls = {.lock = PTHREAD_MUTEX_INITIALIZER};

#define VISITS_LOGGED 24 // connections said in the klog, each time it listens

static int g_visits; // the connections this listening had (its thread's)


static void
log_line(const char *fmt, ...) {
  char buf[160];
  va_list args;

  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  klog_printf("[TwitterPS5 notify] link: %s\n", buf);
}


static unsigned long long
clock_bits(void) {
  struct timespec a, b;

  clock_gettime(CLOCK_MONOTONIC, &a);
  clock_gettime(CLOCK_REALTIME, &b);
  return (unsigned long long)a.tv_nsec << 32 ^ (unsigned long long)a.tv_sec << 12 ^
         (unsigned long long)b.tv_nsec ^ (unsigned long long)b.tv_sec << 40;
}


/**
 * a new code, under the lock: the kernel's random bits mixed with the clock
 **/
static void
new_code(void) {
  unsigned long long bits = 0;

  arc4random_buf(&bits, sizeof bits);
  g_ls.seed = link_mix(g_ls.seed ^ bits ^ clock_bits());
  link_code(g_ls.seed, g_ls.st.code);
  g_ls.seed = link_mix(g_ls.seed);
}


/**
 * the console's address in the home network: the one a socket sends from
 * towards the internet (a UDP socket that is connected sends nothing);
 * 0, or an errno
 **/
static int
console_address(char ip[16]) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  const unsigned char *b = (const unsigned char *)&a.sin_addr.s_addr;
  int s, ret;

  if((s=socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    return errno ? errno : -1;
  }
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(53);
  a.sin_addr.s_addr = htonl(0x01010101); // 1.1.1.1
  ret = connect(s, (struct sockaddr *)&a, sizeof a);
  if(!ret) {
    memset(&a, 0, sizeof a);
    ret = getsockname(s, (struct sockaddr *)&a, &len);
  }
  if(ret) {
    ret = errno ? errno : -1;
  }
  close(s);
  if(!ret && !a.sin_addr.s_addr) {
    ret = -1;
  }
  if(!ret) {
    snprintf(ip, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  }
  return ret;
}


/**
 * the socket that waits for the extension on every address of the console,
 * or -1 with errno
 **/
static int
listen_link(void) {
  struct sockaddr_in a;
  int s, on = 1, e;

  if((s=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(LINK_PORT);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if(bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 4)) {
    e = errno;
    close(s);
    errno = e;
    return -1;
  }
  return s;
}


/**
 * len bytes to the connection; 0, or -1 when it broke
 **/
static int
send_all(int c, const void *data, size_t len) {
  const char *at = data;
  ssize_t sent;

  for(size_t done = 0; done < len; done += (size_t)sent) {
    if((sent=send(c, at + done, len - done, 0)) <= 0) {
      return -1;
    }
  }
  return 0;
}


static void
send_answer(int c, const link_request_t *r, int status, const char *json) {
  char out[1024];
  int n = link_response(out, sizeof out, status, r ? r->origin : NULL, json);

  if(n > 0) {
    send_all(c, out, (size_t)n);
  }
}


/**
 * a file of the QR's page, its headers and its len bytes
 **/
static void
send_file(int c, const char *type, const unsigned char *data, size_t len) {
  char head[512];
  int n = link_file_head(head, sizeof head, type, len);

  if(n > 0 && !send_all(c, head, (size_t)n)) {
    send_all(c, data, len);
  }
}


/**
 * the session into the account's file, through a new file put in its
 * place; 0, or -1. Readable by the title, which runs as another user.
 **/
static int
write_session(int account, const char *auth_token, const char *ct0) {
  char path[64], tmp[80], text[512];
  int fd, n, ret = 0;

  if(settings_session_path(account, path, sizeof path) ||
     (n=link_session_text(text, sizeof text, auth_token, ct0)) < 0) {
    return -1;
  }
  snprintf(tmp, sizeof tmp, "%s.new", path);
  mkdir("/data/TwitterPS5", 0777);
  if((fd=open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
    memset(text, 0, sizeof text);
    return -1;
  }
  fchmod(fd, 0644);
  if(write(fd, text, (size_t)n) != n) {
    ret = -1;
  }
  close(fd);
  memset(text, 0, sizeof text);
  if(!ret && rename(tmp, path)) {
    ret = -1;
  }
  if(ret) {
    unlink(tmp);
  }
  return ret;
}


/**
 * a POST /session: the code, then the session tried on X, then kept;
 * returns 1 once an account is linked
 **/
static int
serve_session(int c, const link_request_t *r) {
  static char auth[128], ct0[256];
  char code[LINK_CODE_LEN + 1], json[160], user[64];
  int ret, wrong, renewed = 0, account, status;

  ret = link_parse_session(r->body, r->body_len, code, auth, sizeof auth, ct0, sizeof ct0);
  if(ret == -1 || ret == -2) {
    log_line("a request without a session (%d)", ret);
    send_answer(c, r, 400, "{\"error\":\"cookies\"}");
    ret = 0;
    goto out;
  }
  pthread_mutex_lock(&g_ls.lock);
  if(ret || !link_same(code, g_ls.st.code)) {
    wrong = ++g_ls.st.wrong;
    g_ls.st.last = LINK_WRONG_CODE;
    g_ls.st.renewed = 0;
    if(wrong >= LINK_TRIES) {
      new_code();
      g_ls.st.wrong = 0;
      g_ls.st.renewed = renewed = 1;
    }
    pthread_mutex_unlock(&g_ls.lock);
    log_line("a wrong code (%d of %d)", wrong, LINK_TRIES);
    snprintf(json, sizeof json, "{\"error\":\"code\",\"left\":%d,\"renewed\":%s}",
             renewed ? LINK_TRIES : LINK_TRIES - wrong, renewed ? "true" : "false");
    send_answer(c, r, 403, json);
    ret = 0;
    goto out;
  }
  g_ls.st.state = LINK_CHECKING;
  account = g_ls.account;
  pthread_mutex_unlock(&g_ls.lock);

  // tried on X: whose profile it opens
  if((status=linkserve_check(auth, ct0, user, sizeof user))) {
    log_line("X did not take the session (%d)", status);
    pthread_mutex_lock(&g_ls.lock);
    g_ls.st.state = LINK_WAITING;
    g_ls.st.last = LINK_REFUSED;
    g_ls.st.refused = status;
    pthread_mutex_unlock(&g_ls.lock);
    if(status < 0) {
      send_answer(c, r, 502, "{\"error\":\"network\"}");
    } else {
      snprintf(json, sizeof json, "{\"error\":\"x\",\"status\":%d}", status);
      send_answer(c, r, 401, json);
    }
    ret = 0;
    goto out;
  }

  ret = write_session(account, auth, ct0);
  pthread_mutex_lock(&g_ls.lock);
  if(ret) {
    g_ls.st.state = LINK_WAITING;
    g_ls.st.last = LINK_UNWRITTEN;
  } else {
    g_ls.st.state = LINK_DONE;
    g_ls.st.last = LINK_FINE;
    g_ls.st.account = account;
    snprintf(g_ls.st.user, sizeof g_ls.st.user, "%.*s", (int)strspn(user, SCREEN_NAME), user);
    snprintf(json, sizeof json, "{\"ok\":true,\"user\":\"%s\",\"account\":%d}", g_ls.st.user, account);
  }
  pthread_mutex_unlock(&g_ls.lock);
  log_line("account %d %s", account, ret ? "not written" : "linked");
  if(ret) {
    send_answer(c, r, 500, "{\"error\":\"write\"}");
    ret = 0;
  } else {
    send_answer(c, r, 200, json);
    ret = 1;
  }
  memset(user, 0, sizeof user);
out:
  memset(auth, 0, sizeof auth);
  memset(ct0, 0, sizeof ct0);
  memset(code, 0, sizeof code);
  return ret;
}


/**
 * what a connection's first bytes were, for the klog: nothing in time,
 * an HTTPS greeting on this HTTP port (a browser that upgrades), HTTP,
 * or something else
 **/
static const char *
visit_kind(const char *buf, size_t len, long need) {
  if(!len) {
    return "nothing came";
  }
  if((unsigned char)buf[0] == 0x16) {
    return "https on the http port";
  }
  if(need > 0) {
    return "http";
  }
  return need < 0 ? "http, too big" : "http, cut short";
}


/**
 * one connection of the extension: its request read and answered;
 * returns 1 once an account is linked. The klog says each one's kind,
 * its route's class and the answer's status (codes only)
 **/
static int
serve(int c) {
  static char buf[LINK_REQUEST_MAX];
  struct timeval tv = {.tv_sec = 5};
  const char *route = "-";
  char hello[64];
  size_t len = 0;
  link_request_t r;
  long need = 0;
  ssize_t n;
  int done = 0, status, origin = 0;

  setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  while(!(need=link_request_size(buf, len)) && len < sizeof buf &&
        (n=recv(c, buf + len, sizeof buf - len, 0)) > 0) {
    len += (size_t)n;
  }
  if(need <= 0 || link_parse_request(buf, (size_t)need, &r)) {
    status = 400;
    send_answer(c, NULL, 400, "{\"error\":\"request\"}");
  } else if(!r.origin_ok) {
    status = 403;
    origin = 1;
    send_answer(c, &r, 403, "{\"error\":\"origin\"}");
  } else if(r.method == LINK_OPTIONS) {
    status = 204;
    route = "preflight";
    send_answer(c, &r, 204, NULL);
  } else if(r.method == LINK_GET && (!strcmp(r.path, "/") || !strcmp(r.path, "/index.html"))) {
    status = 200;
    route = "page";
    pthread_mutex_lock(&g_ls.lock);
    if(!g_ls.st.seen) {
      g_ls.st.seen = 1; // the screen says so
      log_line("page opened");
    }
    pthread_mutex_unlock(&g_ls.lock);
    send_file(c, "text/html; charset=utf-8", link_page_html, sizeof link_page_html);
  } else if(r.method == LINK_GET && !strcmp(r.path, "/link.js")) {
    status = 200;
    route = "script";
    send_file(c, "text/javascript; charset=utf-8", link_page_js, sizeof link_page_js);
  } else if(r.method == LINK_GET && !strcmp(r.path, "/link.css")) {
    status = 200;
    route = "style";
    send_file(c, "text/css; charset=utf-8", link_page_css, sizeof link_page_css);
  } else if(r.method == LINK_GET && !strcmp(r.path, "/hello")) {
    status = 200;
    route = "hello";
    pthread_mutex_lock(&g_ls.lock);
    snprintf(hello, sizeof hello, "{\"app\":\"XPS5\",\"v\":1,\"lang\":\"%s\"}", g_ls.lang);
    pthread_mutex_unlock(&g_ls.lock);
    send_answer(c, &r, 200, hello);
  } else if(r.method == LINK_POST && !strcmp(r.path, "/session")) {
    status = 0; // serve_session says how it went
    route = "session";
    done = serve_session(c, &r);
  } else if(r.method != LINK_GET && r.method != LINK_POST) {
    status = 405;
    send_answer(c, &r, 405, "{\"error\":\"method\"}");
  } else {
    status = 404;
    route = "unknown";
    send_answer(c, &r, 404, "{\"error\":\"path\"}");
  }
  if(++g_visits <= VISITS_LOGGED) {
    log_line("a connection: %s, %s%s, answer %d", visit_kind(buf, len, need), route,
             origin ? ", another site's" : "", status);
  }
  memset(buf, 0, len);
  return done;
}


static void *
serve_main(void *arg) {
  struct timeval tv;
  char ip[16] = "";
  fd_set fds;
  int s = -1, c, ret, done = 0, quit;

  (void)arg;
  g_visits = 0;
  if((ret=console_address(ip)) || (s=listen_link()) < 0) {
    ret = ret ? ret : errno ? errno : -1;
    log_line("no network (errno %d)", ret);
    pthread_mutex_lock(&g_ls.lock);
    g_ls.st.state = LINK_NO_NETWORK;
    g_ls.st.error = ret;
    g_ls.running = 0;
    pthread_mutex_unlock(&g_ls.lock);
    return NULL;
  }
  pthread_mutex_lock(&g_ls.lock);
  snprintf(g_ls.st.ip, sizeof g_ls.st.ip, "%s", ip);
  new_code();
  g_ls.st.state = LINK_WAITING;
  pthread_mutex_unlock(&g_ls.lock);
  log_line("waiting on port %d", LINK_PORT);

  while(!done) {
    pthread_mutex_lock(&g_ls.lock);
    quit = g_ls.stop || time(NULL) - g_ls.asked > LINKSERVE_IDLE;
    pthread_mutex_unlock(&g_ls.lock);
    if(quit) {
      break;
    }
    tv.tv_sec = 0;
    tv.tv_usec = 250 * 1000;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    if(select(s + 1, &fds, NULL, NULL, &tv) <= 0 || (c=accept(s, NULL, NULL)) < 0) {
      continue;
    }
    pthread_mutex_lock(&g_ls.lock);
    g_ls.seed = link_mix(g_ls.seed ^ clock_bits()); // when it came, too
    pthread_mutex_unlock(&g_ls.lock);
    done = serve(c);
    close(c);
  }
  close(s);
  log_line("%s after %d connections", done ? "done" : "closed", g_visits);
  pthread_mutex_lock(&g_ls.lock);
  g_ls.running = 0;
  pthread_mutex_unlock(&g_ls.lock);
  return NULL;
}


void
linkserve_ask(unsigned screen, int account, const char *lang, link_state_t *st) {
  time_t now = time(NULL);
  pthread_attr_t attr;
  pthread_t thread;
  int running;

  pthread_mutex_lock(&g_ls.lock);
  // a screen of its own, or this one's server stopped before it linked
  // (the title did not ask for a while): one that runs stops first
  if(!g_ls.open || screen != g_ls.screen ||
     (!g_ls.running && g_ls.st.state != LINK_DONE && g_ls.st.state != LINK_NO_NETWORK)) {
    g_ls.stop = 1;
    for(int i=0; i < 40 && g_ls.running; i++) {
      pthread_mutex_unlock(&g_ls.lock);
      usleep(50 * 1000);
      pthread_mutex_lock(&g_ls.lock);
    }
    if(g_ls.running) {
      memset(st, 0, sizeof *st); // still going: asked again in a moment
      pthread_mutex_unlock(&g_ls.lock);
      return;
    }
    memset(&g_ls.st, 0, sizeof g_ls.st);
    g_ls.st.state = LINK_STARTING;
    g_ls.open = 1;
    g_ls.screen = screen;
    g_ls.stop = 0;
    g_ls.asked = now;
    g_ls.running = 1;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if((running=pthread_create(&thread, &attr, serve_main, NULL))) {
      g_ls.running = 0;
      g_ls.st.state = LINK_NO_NETWORK;
      g_ls.st.error = running;
      log_line("no thread (%d)", running);
    }
    pthread_attr_destroy(&attr);
  }
  g_ls.asked = now;
  if(g_ls.st.state == LINK_STARTING || g_ls.st.state == LINK_WAITING) {
    g_ls.account = account >= 1 && account <= SETTINGS_ACCOUNTS ? account : 1;
  }
  if(lang && strlen(lang) == 2 && strspn(lang, "abcdefghijklmnopqrstuvwxyz") == 2) {
    snprintf(g_ls.lang, sizeof g_ls.lang, "%s", lang);
  } else if(!g_ls.lang[0]) {
    snprintf(g_ls.lang, sizeof g_ls.lang, "en");
  }
  *st = g_ls.st;
  pthread_mutex_unlock(&g_ls.lock);
}


void
linkserve_close(void) {
  pthread_mutex_lock(&g_ls.lock);
  g_ls.open = 0;
  g_ls.stop = 1;
  memset(g_ls.st.code, 0, sizeof g_ls.st.code);
  pthread_mutex_unlock(&g_ls.lock);
}
