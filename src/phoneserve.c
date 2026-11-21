/* Writing on the phone, in the notify payload; see phoneserve.h. */

#include <errno.h>
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

#include <ps5/klog.h>

#include "link.h"      // the HTTP of the linking page, which this page speaks too
#include "phonepage.h" // the page, src/phonepage/ by tools/embed.py
#include "phoneserve.h"

static struct {
  pthread_mutex_t    lock;
  int                running; // the thread
  int                stop;    // asked to stop
  int                open;    // a window asked since the last "untype"
  unsigned           screen;  // which one: the title counts them
  time_t             asked;   // when the title asked last
  phone_state_t      st;
  int                max;     // characters the text takes
  char               what[8]; // post, reply, dm or search
  char               lang[4];
  char               text[PHONE_TEXT_MAX + 1];
  unsigned long long seed;
} g_ps = {.lock = PTHREAD_MUTEX_INITIALIZER};

static int g_visits;


static void
log_line(const char *fmt, ...) {
  char buf[160];
  va_list args;

  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  klog_printf("[TwitterPS5 notify] phone: %s\n", buf);
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
 * the console's address in the home network (see linkserve.c); 0, or an errno
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
  a.sin_addr.s_addr = htonl(0x01010101);
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


static int
listen_phone(void) {
  struct sockaddr_in a;
  int s, on = 1, e;

  if((s=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(PHONE_PORT);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if(bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 4)) {
    e = errno;
    close(s);
    errno = e;
    return -1;
  }
  return s;
}


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
send_answer(int c, int status, const char *json) {
  static char out[PHONE_TEXT_MAX * 2 + 1024];
  int n = link_response(out, sizeof out, status, NULL, json);

  if(n > 0) {
    send_all(c, out, (size_t)n);
  }
  memset(out, 0, sizeof out);
}


static void
send_file(int c, const char *type, const unsigned char *data, size_t len) {
  char head[512];
  int n = link_file_head(head, sizeof head, type, len);

  if(n > 0 && !send_all(c, head, (size_t)n)) {
    send_all(c, data, len);
  }
}


/**
 * a POST of the page: its token first, then the draft given, or the text kept
 **/
static int
serve_post(int c, const link_request_t *r, int text) {
  static char body[PHONE_TEXT_MAX + 1], json[PHONE_TEXT_MAX * 2 + 160];
  char token[PHONE_TOKEN_LEN + 1];
  int has_text, done, n;
  unsigned seq;

  if(phone_parse_body(r->body, r->body_len, token, body, sizeof body, &has_text, &done)) {
    send_answer(c, 400, "{\"error\":\"body\"}");
    return 400;
  }
  pthread_mutex_lock(&g_ps.lock);
  if(!g_ps.st.token[0] || !link_same(token, g_ps.st.token)) {
    pthread_mutex_unlock(&g_ps.lock);
    memset(body, 0, sizeof body);
    send_answer(c, 403, "{\"error\":\"token\"}");
    return 403;
  }
  if(!text) {
    n = phone_draft_json(json, sizeof json, g_ps.text, g_ps.max, g_ps.what, g_ps.lang, g_ps.st.seq);
  } else {
    if(has_text && (strcmp(body, g_ps.text) || done != g_ps.st.done)) {
      snprintf(g_ps.text, sizeof g_ps.text, "%s", body);
      g_ps.st.seq++;
      g_ps.st.done = done;
    }
    seq = g_ps.st.seq;
    n = snprintf(json, sizeof json, "{\"ok\":true,\"seq\":%u}", seq);
  }
  pthread_mutex_unlock(&g_ps.lock);
  memset(body, 0, sizeof body);
  if(n < 0) {
    send_answer(c, 500, "{\"error\":\"size\"}");
    return 500;
  }
  send_answer(c, 200, json);
  memset(json, 0, sizeof json);
  return 200;
}


static void
serve(int c) {
  static char buf[LINK_REQUEST_MAX];
  struct timeval tv = {.tv_sec = 5};
  const char *route = "-";
  char ip[16];
  size_t len = 0;
  link_request_t r;
  long need = 0;
  ssize_t n;
  int status;

  setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  while(!(need=link_request_size(buf, len)) && len < sizeof buf &&
        (n=recv(c, buf + len, sizeof buf - len, 0)) > 0) {
    len += (size_t)n;
  }
  pthread_mutex_lock(&g_ps.lock);
  snprintf(ip, sizeof ip, "%s", g_ps.st.ip);
  pthread_mutex_unlock(&g_ps.lock);
  if(need <= 0 || link_parse_request(buf, (size_t)need, &r)) {
    status = 400;
    send_answer(c, 400, "{\"error\":\"request\"}");
  } else if(!phone_origin_ok(r.origin, ip)) {
    status = 403;
    route = "another site";
    send_answer(c, 403, "{\"error\":\"origin\"}");
  } else if(r.method == LINK_GET && (!strcmp(r.path, "/") || !strcmp(r.path, "/index.html"))) {
    status = 200;
    route = "page";
    pthread_mutex_lock(&g_ps.lock);
    if(!g_ps.st.seen) {
      g_ps.st.seen = 1;
      log_line("page opened");
    }
    pthread_mutex_unlock(&g_ps.lock);
    send_file(c, "text/html; charset=utf-8", phone_page_html, sizeof phone_page_html);
  } else if(r.method == LINK_GET && !strcmp(r.path, "/phone.js")) {
    status = 200;
    route = "script";
    send_file(c, "text/javascript; charset=utf-8", phone_page_js, sizeof phone_page_js);
  } else if(r.method == LINK_GET && !strcmp(r.path, "/phone.css")) {
    status = 200;
    route = "style";
    send_file(c, "text/css; charset=utf-8", phone_page_css, sizeof phone_page_css);
  } else if(r.method == LINK_POST && !strcmp(r.path, "/draft")) {
    route = "draft";
    status = serve_post(c, &r, 0);
  } else if(r.method == LINK_POST && !strcmp(r.path, "/text")) {
    route = "text";
    status = serve_post(c, &r, 1);
  } else {
    status = 404;
    send_answer(c, 404, "{\"error\":\"path\"}");
  }
  if(++g_visits <= 24 || status != 200) {
    log_line("a connection: %s, answer %d", route, status);
  }
  memset(buf, 0, len);
}


static void *
serve_main(void *arg) {
  struct timeval tv;
  unsigned long long bits = 0;
  char ip[16] = "";
  fd_set fds;
  int s = -1, c, ret, quit;

  (void)arg;
  g_visits = 0;
  if((ret=console_address(ip)) || (s=listen_phone()) < 0) {
    ret = ret ? ret : errno ? errno : -1;
    log_line("no network (errno %d)", ret);
    pthread_mutex_lock(&g_ps.lock);
    g_ps.st.state = PHONE_NO_NETWORK;
    g_ps.st.error = ret;
    g_ps.running = 0;
    pthread_mutex_unlock(&g_ps.lock);
    return NULL;
  }
  arc4random_buf(&bits, sizeof bits);
  pthread_mutex_lock(&g_ps.lock);
  snprintf(g_ps.st.ip, sizeof g_ps.st.ip, "%s", ip);
  g_ps.seed = link_mix(g_ps.seed ^ bits ^ clock_bits());
  phone_token(g_ps.seed, g_ps.st.token);
  g_ps.seed = link_mix(g_ps.seed);
  g_ps.st.state = PHONE_WAITING;
  pthread_mutex_unlock(&g_ps.lock);
  log_line("waiting on port %d", PHONE_PORT);

  for(;;) {
    pthread_mutex_lock(&g_ps.lock);
    quit = g_ps.stop || time(NULL) - g_ps.asked > PHONE_IDLE;
    pthread_mutex_unlock(&g_ps.lock);
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
    serve(c);
    close(c);
  }
  close(s);
  log_line("closed after %d connections", g_visits);
  pthread_mutex_lock(&g_ps.lock);
  g_ps.running = 0;
  memset(g_ps.text, 0, sizeof g_ps.text);
  pthread_mutex_unlock(&g_ps.lock);
  return NULL;
}


void
phoneserve_ask(unsigned screen, unsigned seq, int max, const char *what, const char *lang, const char *draft,
               phone_state_t *st, char *text, size_t size, size_t *len) {
  time_t now = time(NULL);
  pthread_attr_t attr;
  pthread_t thread;
  int ret;

  *len = 0;
  pthread_mutex_lock(&g_ps.lock);
  if(!g_ps.open || screen != g_ps.screen ||
     (!g_ps.running && g_ps.st.state != PHONE_NO_NETWORK)) {
    g_ps.stop = 1;
    for(int i=0; i < 40 && g_ps.running; i++) {
      pthread_mutex_unlock(&g_ps.lock);
      usleep(50 * 1000);
      pthread_mutex_lock(&g_ps.lock);
    }
    if(g_ps.running) {
      memset(st, 0, sizeof *st);
      pthread_mutex_unlock(&g_ps.lock);
      return;
    }
    memset(&g_ps.st, 0, sizeof g_ps.st);
    memset(g_ps.text, 0, sizeof g_ps.text);
    g_ps.st.state = PHONE_STARTING;
    g_ps.open = 1;
    g_ps.screen = screen;
    g_ps.stop = 0;
    g_ps.asked = now;
    g_ps.running = 1;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if((ret=pthread_create(&thread, &attr, serve_main, NULL))) {
      g_ps.running = 0;
      g_ps.st.state = PHONE_NO_NETWORK;
      g_ps.st.error = ret;
      log_line("no thread (%d)", ret);
    }
    pthread_attr_destroy(&attr);
  }
  g_ps.asked = now;
  g_ps.max = max > 0 && max < 100000 ? max : 280;
  snprintf(g_ps.what, sizeof g_ps.what, "%s", what && strspn(what, "abcdefghijklmnopqrstuvwxyz") == strlen(what) ?
           what : "post");
  if(lang && strlen(lang) == 2 && strspn(lang, "abcdefghijklmnopqrstuvwxyz") == 2) {
    snprintf(g_ps.lang, sizeof g_ps.lang, "%s", lang);
  } else if(!g_ps.lang[0]) {
    snprintf(g_ps.lang, sizeof g_ps.lang, "en");
  }
  // the title's draft, until the phone writes
  if(!g_ps.st.seq && draft) {
    snprintf(g_ps.text, sizeof g_ps.text, "%s", draft);
  }
  *st = g_ps.st;
  if(g_ps.st.seq != seq && g_ps.st.seq) {
    *len = strlen(g_ps.text) < size ? strlen(g_ps.text) : 0;
    memcpy(text, g_ps.text, *len);
  }
  pthread_mutex_unlock(&g_ps.lock);
}


void
phoneserve_close(void) {
  pthread_mutex_lock(&g_ps.lock);
  g_ps.open = 0;
  g_ps.stop = 1;
  memset(g_ps.st.token, 0, sizeof g_ps.st.token);
  memset(g_ps.text, 0, sizeof g_ps.text);
  pthread_mutex_unlock(&g_ps.lock);
}
