/* XPS5's login window, the engine's side: WebKit's UIProcess, started by
launcher.c with the launcher's socket at BROWSERPS5_LAUNCHER_FD, the loader's
connection to the title as its stdio and the title's key in XPS5_LOGIN_KEY.

It listens on 127.0.0.1 (src/loginproto.h), says the port on its stdout, then
lets nothing more go there (WebKit's messages would land in the title's
connection), and takes the one connection that brings the key. Then it opens
X's login page at the size, zoom and language the title asked for, paints it
into memory as the page changes and sends what changed to the title, which
draws it in its window; the title's pointer, wheel, keys and text come back
as the page's own events, as if a mouse and a keyboard were there. When the
title asks, or after a click, it says which kind of field has the focus, and
its text unless it is a password, so the title can open the PS5's keyboard
for it.

Only X's pages open in the window: a link elsewhere is not followed (frames
inside X's pages, such as a check that the user is a person, still load).
The klog gets codes, counts and times only, never the page's text, its
fields or what is typed.

Once X's page has signed in, X's session (its auth_token and ct0 cookies)
goes to the title (session_check), which keeps it in the account the window
signs in; neither cookie goes to the klog.

The engine's user agent says what it runs on, a PlayStation 5 and its system
software: no other device's or browser's is claimed. The WebKit calls are
those of BrowserPS5-WebKit v1's hello_page and browser (read only). */

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <WebKit/WKArray.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationPlayStation.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKErrorRef.h>
#include <WebKit/WKEvent.h>
#include <WebKit/WKEventPlayStation.h>
#include <WebKit/WKFrameInfoRef.h>
#include <WebKit/WKFramePolicyListener.h>
#include <WebKit/WKNavigationActionRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPageResourceLoadClient.h>
#include <WebKit/WKPageUIClient.h>
#include <WebKit/WKPagePrivatePlayStation.h>
#include <WebKit/WKRunLoop.h>
#include <WebKit/WKString.h>
#include <WebKit/WKType.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKURLRequest.h>
#include <WebKit/WKURLResponse.h>
#include <WebKit/WKView.h>
#include <WebKit/WKViewClient.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>
#include <WebKit/WKWebsiteDataStoreRef.h>

#include <ps5/kernel.h>
#include <ps5/klog.h>
#include <sqlite3.h>

#include "loginproto.h"

#define ENGINE_DIR LOGIN_ENGINE_DIR
#define DATA_DIR   ENGINE_DIR "/web"
#define LOGIN_URL  "https://x.com/i/flow/login"
#define ACCEPT_MS  30000 // for the title to connect after the port is said
#define HELLO_MS   5000  // for its LOGIN_HELLO once connected
#define TRIES      3     // connections without the key before it gives up
#define TICK_MS    250   // how often the page's progress is looked at
#define LOADS_MS   2000  // how often the klog gets what the page loaded, at most
#define COOKIES_MS 1000  // how often the cookie jar is looked at for X's session, while on X
#define SIGNED_MS  3000  // from auth_token's coming to its sending, unless a page finishes first

// libwpe's key codes are X11 keysyms: ASCII and Latin-1 are themselves, the
// rest of Unicode is 0x01000000 + the code point (v1's hello_page)
#define KEY_BACKSPACE 0xff08
#define KEY_TAB       0xff09
#define KEY_RETURN    0xff0d
#define KEY_ESCAPE    0xff1b

// a message for the title, after the frames' header (see queue)
typedef struct out {
  struct out *next;
  size_t      len;
} out_t;

// one from the title, for the main thread
typedef struct in {
  login_msg_t m;
  char        data[]; // m.size bytes and a 0
} in_t;

static struct {
  int             sock; // the title's connection
  int             width, height, zoom;
  char            lang[8];
  WKViewRef       view;
  uint32_t       *pixels; // what WKPagePaint paints: cairo's ARGB32
  WKRect          damage; // to paint, main thread
  bool            paint_pending;
  pthread_mutex_t lock; // pixels, the damage painted and the queue
  pthread_cond_t  wake;
  int             x0, y0, x1, y1; // painted, not sent yet (x1 <= x0: none)
  out_t          *head, *tail;
  bool            closing;
  login_state_t   state, sent; // main thread
  double          start_ms;
  unsigned        frames, clicks, fields;
  bool            stopped;
} g = {.sock = -1, .lock = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER};


static double
now_ms(void) {
  struct timespec t;

  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}


static long
since(void) {
  return (long)(now_ms() - g.start_ms);
}


static void
say(const char *fmt, ...) {
  char line[200];
  va_list args;

  va_start(args, fmt);
  vsnprintf(line, sizeof line, fmt, args);
  va_end(args);
  klog_printf("[XPS5 login] ui: %s\n", line);
}


static WKStringRef
string(const char *s) {
  return WKStringCreateWithUTF8CString(s);
}


static bool
send_all(int s, const void *buf, size_t len) {
  const char *p = (const char *)buf;
  ssize_t n;

  while(len) {
    if((n=send(s, p, len, 0)) < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      return false;
    }
    p += n;
    len -= (size_t)n;
  }
  return true;
}


/**
 * len bytes, waiting ms at most for each part (-1: as long as it takes)
 **/
static bool
recv_all(int s, void *buf, size_t len, int ms) {
  char *p = (char *)buf;
  ssize_t n;

  while(len) {
    if(ms >= 0) {
      struct pollfd w = {s, POLLIN, 0};
      if(poll(&w, 1, ms) <= 0) {
        return false;
      }
    }
    if((n=recv(s, p, len, 0)) < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      return false;
    }
    p += n;
    len -= (size_t)n;
  }
  return true;
}


/*
 * To the title: frames from the sender thread, other messages in the order
 * they were queued, before the next frame.
 */

static void
queue(uint16_t type, uint16_t arg, const void *body, size_t size) {
  out_t *o = (out_t *)malloc(sizeof(out_t) + sizeof(login_msg_t) + size);
  login_msg_t m = {type, arg, (uint32_t)size};

  if(!o) {
    return;
  }
  o->next = 0;
  o->len = sizeof m + size;
  memcpy(o + 1, &m, sizeof m);
  if(size) {
    memcpy((char *)(o + 1) + sizeof m, body, size);
  }
  pthread_mutex_lock(&g.lock);
  if(g.tail) {
    g.tail->next = o;
  } else {
    g.head = o;
  }
  g.tail = o;
  pthread_cond_signal(&g.wake);
  pthread_mutex_unlock(&g.lock);
}


static void *
sender(void *) {
  size_t cap = sizeof(login_msg_t) + sizeof(login_rect_t) + (size_t)g.width * g.height * 4;
  uint8_t *buf = (uint8_t *)malloc(cap);
  bool ok = buf != 0;

  pthread_mutex_lock(&g.lock);
  while(ok) {
    while(!g.closing && !g.head && g.x1 <= g.x0) {
      pthread_cond_wait(&g.wake, &g.lock);
    }
    if(g.head) {
      out_t *o = g.head;
      g.head = o->next;
      if(!g.head) {
        g.tail = 0;
      }
      pthread_mutex_unlock(&g.lock);
      ok = send_all(g.sock, o + 1, o->len);
      memset(o + 1, 0, o->len); // a LOGIN_SESSION's cookies among them
      free(o);
      pthread_mutex_lock(&g.lock);
      continue;
    }
    if(g.closing) {
      break;
    }
    // what was painted since the last frame, as the title's pixels
    login_rect_t r = {(uint16_t)g.x0, (uint16_t)g.y0, (uint16_t)(g.x1 - g.x0), (uint16_t)(g.y1 - g.y0)};
    login_msg_t m = {LOGIN_FRAME, 0, (uint32_t)(sizeof r + (size_t)r.w * r.h * 4)};
    uint32_t *dst = (uint32_t *)(buf + sizeof m + sizeof r);
    g.x0 = g.y0 = g.x1 = g.y1 = 0;
    for(int y=0; y < r.h; y++) {
      const uint32_t *src = g.pixels + (size_t)(r.y + y) * g.width + r.x;
      for(int x=0; x < r.w; x++) {
        uint32_t p = src[x];
        *dst++ = 0xff000000u | (p & 0x0000ff00u) | ((p >> 16) & 0xffu) | ((p & 0xffu) << 16);
      }
    }
    pthread_mutex_unlock(&g.lock);
    memcpy(buf, &m, sizeof m);
    memcpy(buf + sizeof m, &r, sizeof r);
    ok = send_all(g.sock, buf, sizeof m + m.size);
    g.frames++;
    pthread_mutex_lock(&g.lock);
  }
  pthread_mutex_unlock(&g.lock);
  free(buf);
  return 0;
}


/*
 * The page: painted into memory as WebKit says what changed.
 */

static void
paint(void *) {
  WKRect rect = g.damage;
  int x0 = (int)rect.origin.x, y0 = (int)rect.origin.y;
  int x1 = (int)(rect.origin.x + rect.size.width + 0.999), y1 = (int)(rect.origin.y + rect.size.height + 0.999);

  g.damage = WKRectMake(0, 0, 0, 0);
  g.paint_pending = false;
  x0 = x0 < 0 ? 0 : x0;
  y0 = y0 < 0 ? 0 : y0;
  x1 = x1 > g.width ? g.width : x1;
  y1 = y1 > g.height ? g.height : y1;
  if(x1 <= x0 || y1 <= y0) {
    return;
  }
  pthread_mutex_lock(&g.lock);
  WKPagePaint(WKViewGetPage(g.view), (unsigned char *)g.pixels, WKSizeMake(g.width, g.height),
              WKRectMake(x0, y0, x1 - x0, y1 - y0));
  if(g.x1 <= g.x0) {
    g.x0 = x0, g.y0 = y0, g.x1 = x1, g.y1 = y1;
  } else {
    g.x0 = x0 < g.x0 ? x0 : g.x0;
    g.y0 = y0 < g.y0 ? y0 : g.y0;
    g.x1 = x1 > g.x1 ? x1 : g.x1;
    g.y1 = y1 > g.y1 ? y1 : g.y1;
  }
  pthread_cond_signal(&g.wake);
  pthread_mutex_unlock(&g.lock);
}


static WKRect
rect_union(WKRect a, WKRect b) {
  if(a.size.width <= 0 || a.size.height <= 0) {
    return b;
  }
  double x0 = a.origin.x < b.origin.x ? a.origin.x : b.origin.x;
  double y0 = a.origin.y < b.origin.y ? a.origin.y : b.origin.y;
  double x1 = a.origin.x + a.size.width, y1 = a.origin.y + a.size.height;
  if(b.origin.x + b.size.width > x1) {
    x1 = b.origin.x + b.size.width;
  }
  if(b.origin.y + b.size.height > y1) {
    y1 = b.origin.y + b.size.height;
  }
  return WKRectMake(x0, y0, x1 - x0, y1 - y0);
}


static void
setViewNeedsDisplay(WKViewRef, WKRect rect, const void *) {
  g.damage = rect_union(g.damage, rect);
  if(!g.paint_pending) {
    g.paint_pending = true;
    WKRunLoopCallOnMainThread(paint, 0);
  }
}


/*
 * Where the page may go: X's pages, over HTTPS for the title's "on X".
 */

static bool
host_is_x(const char *h) {
  static const char *const hosts[] = {"x.com", "twitter.com"};
  size_t n = strlen(h);

  for(const char *x : hosts) {
    size_t k = strlen(x);
    if(!strcmp(h, x) || (n > k + 1 && h[n - k - 1] == '.' && !strcmp(h + n - k, x))) {
      return true;
    }
  }
  return false;
}


/**
 * 2: an X page over HTTPS, 1: X over HTTP or about:blank, 0: anything else
 **/
static int
url_kind(WKURLRef url) {
  char scheme[16] = "", host[256] = "";
  WKStringRef s;

  if(!url) {
    return 0;
  }
  if((s=WKURLCopyScheme(url))) {
    WKStringGetUTF8CString(s, scheme, sizeof scheme);
    WKRelease(s);
  }
  if(!strcmp(scheme, "about")) {
    return 1;
  }
  if((s=WKURLCopyHostName(url))) {
    WKStringGetUTF8CString(s, host, sizeof host);
    WKRelease(s);
  }
  for(char *c=host; *c; c++) {
    *c = *c >= 'A' && *c <= 'Z' ? *c + 32 : *c;
  }
  if(!host_is_x(host)) {
    return 0;
  }
  return !strcmp(scheme, "https") ? 2 : !strcmp(scheme, "http") ? 1 : 0;
}


static void
send_state(void) {
  if(memcmp(&g.state, &g.sent, sizeof g.state)) {
    g.sent = g.state;
    queue(LOGIN_STATE, 0, &g.state, sizeof g.state);
  }
}


static void
decidePolicyForNavigationAction(WKPageRef, WKNavigationActionRef action, WKFramePolicyListenerRef listener,
                                WKTypeRef, const void *) {
  WKFrameInfoRef frame = WKNavigationActionCopyTargetFrameInfo(action);
  bool main = !frame || WKFrameInfoGetIsMainFrame(frame);
  bool ok = !WKNavigationActionShouldPerformDownload(action);

  if(frame) {
    WKRelease(frame);
  }
  if(ok && main) {
    WKURLRequestRef req = WKNavigationActionCopyRequest(action);
    WKURLRef url = req ? WKURLRequestCopyURL(req) : 0;
    ok = url_kind(url) > 0;
    if(url) {
      WKRelease(url);
    }
    if(req) {
      WKRelease(req);
    }
  }
  if(ok) {
    WKFramePolicyListenerUse(listener);
    return;
  }
  WKFramePolicyListenerIgnore(listener);
  g.state.blocked++;
  say("a page outside X not opened (%d so far)", g.state.blocked);
  send_state();
}


static void
didStartProvisionalNavigation(WKPageRef, WKNavigationRef, WKTypeRef, const void *) {
  g.state.loading = 1;
  g.state.error = 0;
  send_state();
}


static void
didCommitNavigation(WKPageRef page, WKNavigationRef, WKTypeRef, const void *) {
  WKURLRef url = WKPageCopyCommittedURL(page);

  g.state.on_x = url_kind(url) == 2;
  if(url) {
    WKRelease(url);
  }
  say("committed at %ld ms, on X %d", since(), g.state.on_x);
  send_state();
}


static void probe_features(void);


/*
 * X's session for the title (src/loginproto.h LOGIN_SESSION). auth_token is
 * an HttpOnly cookie, which no page script reads, so it comes from the
 * engine's own cookie jar, WebCore's CookieJarDB (SQLite, in WAL mode, which
 * lets this process read it while the NetworkProcess writes it). Once
 * auth_token is there, the session goes when the page X opens after the
 * sign-in has finished, or SIGNED_MS later, with the ct0 then; once for each
 * auth_token. The klog gets sqlite's codes and the times, never the values.
 */

static struct {
  uint64_t sent;     // FNV-1a of the auth_token last sent, or 0
  double   seen_ms;  // when an auth_token not sent yet was first seen, or 0
  bool     finished; // a page finished since then
  double   last_ms;  // the jar's last look
  int      error;    // sqlite's last error, said once
  bool     odd;      // cookies not as X's look, said once
} g_session;


static uint64_t
fnv(const char *s) {
  uint64_t h = 0xcbf29ce484222325ull;

  while(*s) {
    h = (h ^ (unsigned char)*s++) * 0x100000001b3ull;
  }
  return h ? h : 1;
}


static bool
hex(const char *s, size_t min, size_t max) {
  size_t n = strspn(s, "0123456789abcdefABCDEF");

  return !s[n] && n >= min && n <= max;
}


/**
 * X's two cookies from the jar, the newest of each for x.com; 0 with both,
 * 1 without auth_token, 2 without ct0, or sqlite's error below 0
 **/
static int
read_cookies(char *auth, size_t auth_size, char *ct0, size_t ct0_size) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  int rc;

  auth[0] = ct0[0] = 0;
  rc = sqlite3_open_v2(DATA_DIR "/cookies.db", &db, SQLITE_OPEN_READWRITE, 0);
  if(rc == SQLITE_OK) {
    sqlite3_busy_timeout(db, 500);
    rc = sqlite3_prepare_v2(db, "SELECT name, value FROM Cookie WHERE name IN ('auth_token', 'ct0') AND "
                                "domain IN ('.x.com', 'x.com') ORDER BY lastupdated", -1, &st, 0);
  }
  while(rc == SQLITE_OK && (rc=sqlite3_step(st)) == SQLITE_ROW) {
    const char *name = (const char *)sqlite3_column_text(st, 0);
    const char *value = (const char *)sqlite3_column_text(st, 1);
    if(name && value) {
      if(!strcmp(name, "auth_token")) {
        snprintf(auth, auth_size, "%s", value);
      } else {
        snprintf(ct0, ct0_size, "%s", value);
      }
    }
    rc = SQLITE_OK;
  }
  if(st) {
    sqlite3_finalize(st);
  }
  if(db) {
    sqlite3_close(db);
  }
  if(rc != SQLITE_DONE) {
    auth[0] = ct0[0] = 0;
    return -rc;
  }
  return !auth[0] ? 1 : !ct0[0] ? 2 : 0;
}


// main thread, from tick
static void
session_check(void) {
  char auth[128], ct0[256], text[LOGIN_SESSION_MAX];
  uint64_t h;
  int ret, n;

  if(!g.state.on_x || now_ms() - g_session.last_ms < COOKIES_MS) {
    return;
  }
  g_session.last_ms = now_ms();
  if((ret=read_cookies(auth, sizeof auth, ct0, sizeof ct0)) < 0) {
    if(ret != g_session.error) {
      say("the cookie jar did not read: sqlite %d", -ret);
    }
    g_session.error = ret;
    return;
  }
  if(ret == 1) {
    g_session.seen_ms = 0; // signed out, or not yet in
  } else if((h=fnv(auth)) != g_session.sent) {
    if(!g_session.seen_ms) {
      g_session.seen_ms = now_ms();
      g_session.finished = false;
      say("signed in at %ld ms", since());
    } else if(ret == 0 && (g_session.finished || now_ms() - g_session.seen_ms >= SIGNED_MS)) {
      if(!hex(auth, 20, 127) || !hex(ct0, 20, 255)) {
        if(!g_session.odd) {
          say("X's cookies are not as they look: none sent");
        }
        g_session.odd = true;
      } else if((n=snprintf(text, sizeof text, "auth_token=%s\nct0=%s\n", auth, ct0)) > 0 &&
                (size_t)n < sizeof text) {
        queue(LOGIN_SESSION, 0, text, (size_t)n);
        g_session.sent = h;
        g_session.seen_ms = 0;
        say("X's session handed to the title at %ld ms", since());
      }
    }
  }
  memset(auth, 0, sizeof auth);
  memset(ct0, 0, sizeof ct0);
  memset(text, 0, sizeof text);
}


static void
didFinishNavigation(WKPageRef, WKNavigationRef, WKTypeRef, const void *) {
  g.state.loading = 0;
  g.state.progress = 100;
  say("finished at %ld ms", since());
  send_state();
  probe_features();
  if(g_session.seen_ms) {
    g_session.finished = true; // the page after the sign-in: the session goes at the next look
  }
}


static void
didFailNavigation(WKPageRef, WKNavigationRef, WKErrorRef error, WKTypeRef, const void *) {
  char domain[64] = "";
  WKStringRef d = WKErrorCopyDomain(error);

  if(d) {
    WKStringGetUTF8CString(d, domain, sizeof domain);
    WKRelease(d);
  }
  int code = WKErrorGetErrorCode(error);

  g.state.loading = 0;
  // a load stopped by the window itself (a link out of X: 102) or by the
  // next one (-999) leaves the page as it was
  if(code != 102 && code != -999) {
    g.state.error = code;
  }
  // the error's domain and code; not its text, which can hold the address
  say("failed at %ld ms: %s %d", since(), domain, code);
  send_state();
}


static void
webProcessDidCrash(WKPageRef, const void *) {
  g.state.crashed = 1;
  g.state.loading = 0;
  say("the WebProcess ended at %ld ms", since());
  send_state();
}


/*
 * What the page loads, for the klog in counts only (never an address, a
 * header or a page's text): by the kind of host, the requests, their HTTP
 * classes, the last refusal's status and the last error's code; and what
 * the page asks of the window that it does not do (new windows, dialogs),
 * and how many console messages it writes. All on the main thread.
 */

enum { H_X, H_TWIMG, H_ARKOSE, H_GOOGLE, H_APPLE, H_OTHER, H_KINDS };
static const char *const g_host_names[H_KINDS] = {"x", "twimg", "arkose", "google", "apple", "other"};

static struct {
  unsigned sent[H_KINDS], ok[H_KINDS], moved[H_KINDS], refused[H_KINDS], broken[H_KINDS], failed[H_KINDS];
  int      last_status[H_KINDS]; // of the last 4xx or 5xx
  int      last_error[H_KINDS];  // of the last load that failed
  bool     changed[H_KINDS];
  unsigned console, windows, dialogs;
  bool     asked_changed;
  double   said_ms;
} g_loads;


static bool
host_ends(const char *h, const char *d) {
  size_t n = strlen(h), k = strlen(d);
  return !strcmp(h, d) || (n > k + 1 && h[n - k - 1] == '.' && !strcmp(h + n - k, d));
}


static int
host_kind(WKURLRef url) {
  char host[256] = "";
  WKStringRef s;

  if(url && (s=WKURLCopyHostName(url))) {
    WKStringGetUTF8CString(s, host, sizeof host);
    WKRelease(s);
  }
  for(char *c=host; *c; c++) {
    *c = *c >= 'A' && *c <= 'Z' ? *c + 32 : *c;
  }
  return host_is_x(host) ? H_X : host_ends(host, "twimg.com") ? H_TWIMG :
         host_ends(host, "arkoselabs.com") || host_ends(host, "arkoselabs.cn") ? H_ARKOSE :
         host_ends(host, "google.com") || host_ends(host, "gstatic.com") ? H_GOOGLE :
         host_ends(host, "apple.com") ? H_APPLE : H_OTHER;
}


static void
didSendRequest(const void *, WKPageRef, WKURLRequestRef request) {
  WKURLRef url = request ? WKURLRequestCopyURL(request) : 0;
  int k = host_kind(url);

  if(url) {
    WKRelease(url);
  }
  g_loads.sent[k]++;
  g_loads.changed[k] = true;
}


static void
didReceiveResponse(const void *, WKPageRef, WKURLRef url, WKURLResponseRef response) {
  int k = host_kind(url), status = response ? WKURLResponseHTTPStatusCode(response) : 0;

  if(status >= 500) {
    g_loads.broken[k]++;
    g_loads.last_status[k] = status;
  } else if(status >= 400) {
    g_loads.refused[k]++;
    g_loads.last_status[k] = status;
  } else if(status >= 300) {
    g_loads.moved[k]++;
  } else {
    g_loads.ok[k]++;
  }
  g_loads.changed[k] = true;
}


static void
didCompleteWithError(const void *, WKPageRef, WKURLRef url, WKURLResponseRef, WKErrorRef error) {
  int k, code = error ? WKErrorGetErrorCode(error) : 0;

  if(!code || code == -999) { // none, or cancelled by the page
    return;
  }
  k = host_kind(url);
  g_loads.failed[k]++;
  g_loads.last_error[k] = code;
  g_loads.changed[k] = true;
}


static WKPageRef
createNewPage(WKPageRef, WKPageConfigurationRef, WKNavigationActionRef, WKWindowFeaturesRef, const void *) {
  g_loads.windows++; // the window opens no others
  g_loads.asked_changed = true;
  return 0;
}


// Where the window is, in the screen's points (the page sees a 1920x1080
// screen): the page's size without its zoom (140% times the title's scale),
// in the middle of the screen. Without this, pages saw window.outerWidth and
// outerHeight 0, which no browser window has (2026-11-13 probe on the PS5).
static WKRect
getWindowFrame(WKPageRef, const void *) {
  double scale = g.zoom > 0 ? g.zoom / 140.0 : 1.0;
  double w = g.width / scale, h = g.height / scale;
  return WKRectMake((1920 - w) / 2, (1080 - h) / 2, w, h);
}


static void
runJavaScriptAlert(WKPageRef, WKStringRef, WKFrameRef, WKSecurityOriginRef,
                   WKPageRunJavaScriptAlertResultListenerRef listener, const void *) {
  g_loads.dialogs++;
  g_loads.asked_changed = true;
  WKPageRunJavaScriptAlertResultListenerCall(listener);
}


static void
runJavaScriptConfirm(WKPageRef, WKStringRef, WKFrameRef, WKSecurityOriginRef,
                     WKPageRunJavaScriptConfirmResultListenerRef listener, const void *) {
  g_loads.dialogs++;
  g_loads.asked_changed = true;
  WKPageRunJavaScriptConfirmResultListenerCall(listener, false);
}


static void
runJavaScriptPrompt(WKPageRef, WKStringRef, WKStringRef, WKFrameRef, WKSecurityOriginRef,
                    WKPageRunJavaScriptPromptResultListenerRef listener, const void *) {
  g_loads.dialogs++;
  g_loads.asked_changed = true;
  WKPageRunJavaScriptPromptResultListenerCall(listener, 0);
}


static void
addMessageToConsole(WKPageRef, WKStringRef, const void *) {
  g_loads.console++; // how many; never what
  g_loads.asked_changed = true;
}


/**
 * what changed since the last time, at most every LOADS_MS
 **/
static void
loads_report(void) {
  if(now_ms() - g_loads.said_ms < LOADS_MS) {
    return;
  }
  for(int k=0; k < H_KINDS; k++) {
    if(!g_loads.changed[k]) {
      continue;
    }
    g_loads.changed[k] = false;
    g_loads.said_ms = now_ms();
    say("loads %s: %u sent, %u ok, %u moved, %u refused (last %d), %u broken, %u failed (last %d)",
        g_host_names[k], g_loads.sent[k], g_loads.ok[k], g_loads.moved[k], g_loads.refused[k], g_loads.last_status[k],
        g_loads.broken[k], g_loads.failed[k], g_loads.last_error[k]);
  }
  if(g_loads.asked_changed) {
    g_loads.asked_changed = false;
    g_loads.said_ms = now_ms();
    say("the page asked: %u console messages, %u new windows, %u dialogs", g_loads.console, g_loads.windows,
        g_loads.dialogs);
  }
}


// the progress of a load, and "on X" as the page goes, on the main thread
static void
tick(void *) {
  WKPageRef page = WKViewGetPage(g.view);

  if(g.state.loading) {
    double p = WKPageGetEstimatedProgress(page);
    g.state.progress = (uint8_t)(p < 0 ? 0 : p > 1 ? 100 : p * 100 + 0.5);
  }
  send_state();
  loads_report();
  session_check();
}


static void *
ticker(void *) {
  while(!g.stopped) {
    usleep(TICK_MS * 1000);
    WKRunLoopCallOnMainThread(tick, 0);
  }
  return 0;
}


/*
 * What the title does to the page.
 */

static void
send_key(WKPageRef page, int32_t code, const char *text, uint32_t length, uint32_t modifiers) {
  WKPageHandleKeyboardEvent(page, WKKeyboardEventMake(kWKEventKeyDown, kWKInputTypeNormal, text, length, "", code,
                                                      0, 0, modifiers));
  WKPageHandleKeyboardEvent(page, WKKeyboardEventMake(kWKEventKeyUp, kWKInputTypeNormal, text, length, "", code,
                                                      0, 0, modifiers));
}


/**
 * a key per character, as if typed (v1's type_text)
 **/
static void
type_text(WKPageRef page, const char *text) {
  const unsigned char *s = (const unsigned char *)text;

  while(*s) {
    uint32_t length = *s < 0x80 ? 1 : (*s & 0xe0) == 0xc0 ? 2 : (*s & 0xf0) == 0xe0 ? 3 : 4;
    uint32_t cp = length == 1 ? *s : *s & (0x7f >> length);
    for(uint32_t i=1; i < length; i++) {
      if((s[i] & 0xc0) != 0x80) {
        return; // cut short: the title sends valid UTF-8
      }
      cp = cp << 6 | (s[i] & 0x3f);
    }
    int32_t code = cp == '\n' ? KEY_RETURN :
                   (cp >= 0x20 && cp < 0x7f) || (cp >= 0xa0 && cp <= 0xff) ? (int32_t)cp :
                   (int32_t)(0x01000000 | cp);
    send_key(page, code, (const char *)s, length, 0);
    s += length;
  }
}


// the element in focus, inside shadow roots too
#define FOCUSED_ELEMENT                                         \
  "var e = document.activeElement;"                             \
  "while (e && e.shadowRoot && e.shadowRoot.activeElement)"     \
  "  e = e.shadowRoot.activeElement;"

// "<kind>\n<text>" for a field that takes text, never a password's text;
// null for anything else
static const char field_script[] = "(function () {" FOCUSED_ELEMENT R"(
  if (!e) return null;
  if (e.tagName == 'TEXTAREA') return 'text\n' + e.value;
  if (e.tagName == 'INPUT') {
    var t = (e.type || 'text').toLowerCase();
    if (t == 'password') return 'password\n';
    if (t == 'number' || t == 'tel' || e.inputMode == 'numeric') return 'number\n' + e.value;
    if (/^(text|search|url|email)$/.test(t)) return (t == 'email' ? 'email' : 'text') + '\n' + e.value;
    return null;
  }
  if (e.isContentEditable) return 'edit\n';
  return null;
})())";

static const char select_script[] = "(function () {" FOCUSED_ELEMENT R"(
  if (e && e.select) e.select();
})())";


static void
ignore_result(WKTypeRef, WKErrorRef, void *) {
}


static void
run_script(const char *script, void *ctx, WKPageEvaluateJavaScriptFunction done) {
  WKStringRef s = string(script);

  WKPageEvaluateJavaScriptInMainFrame(WKViewGetPage(g.view), s, ctx, done);
  WKRelease(s);
}


/*
 * What this engine offers a page, next to a Safari's, for the klog: which
 * web features are there and how the window looks to the page. It only
 * reads (a canvas made and dropped), leaves nothing in the page and
 * reports nothing of the page itself. The bits, in order:
 *  0 WebAssembly         7 serviceWorker      14 PublicKeyCredential  21 navigator.webdriver (1: automation shows)
 *  1 WebGL               8 Worker             15 localStorage works   22 document visible
 *  2 WebGL2              9 SharedWorker       16 cookies enabled      23 document has focus
 *  3 AudioContext       10 BroadcastChannel   17 Notification         24 PointerEvent
 *  4 mediaDevices       11 RTCPeerConnection  18 canvas 2D readable   25 TouchEvent
 *  5 indexedDB          12 video plays MP4    19 OffscreenCanvas
 *  6 crypto.subtle      13 MediaSource        20 Intl time zone set
 * then the screen and its available part, the window inside and outside,
 * the pixel ratio, the cores, the memory, touch points, languages and
 * navigator.platform.
 */
static const char features_script[] = R"((function () {
  function t(f) { try { return !!f(); } catch (e) { return false; } }
  function gl(k) { return t(function () { return document.createElement('canvas').getContext(k); }); }
  var n = navigator, s = screen, w = window, b = [
    t(function () { return typeof WebAssembly == 'object'; }), gl('webgl'), gl('webgl2'),
    t(function () { return w.AudioContext || w.webkitAudioContext; }),
    t(function () { return n.mediaDevices && n.mediaDevices.enumerateDevices; }),
    t(function () { return w.indexedDB; }), t(function () { return w.crypto && crypto.subtle; }),
    t(function () { return n.serviceWorker; }), t(function () { return w.Worker; }),
    t(function () { return w.SharedWorker; }), t(function () { return w.BroadcastChannel; }),
    t(function () { return w.RTCPeerConnection; }),
    t(function () { return document.createElement('video').canPlayType('video/mp4'); }),
    t(function () { return w.MediaSource; }), t(function () { return w.PublicKeyCredential; }),
    t(function () { return w.localStorage && typeof localStorage.length == 'number'; }),
    t(function () { return n.cookieEnabled; }), t(function () { return w.Notification; }),
    t(function () { var c = document.createElement('canvas').getContext('2d'); c.fillRect(0, 0, 1, 1);
                    return c.getImageData(0, 0, 1, 1).data[3] == 255; }),
    t(function () { return w.OffscreenCanvas; }),
    t(function () { return Intl.DateTimeFormat().resolvedOptions().timeZone; }),
    t(function () { return n.webdriver; }), t(function () { return document.visibilityState == 'visible'; }),
    t(function () { return document.hasFocus(); }), t(function () { return w.PointerEvent; }),
    t(function () { return w.TouchEvent; })];
  var o = '';
  for (var i = 0; i < b.length; i++) o += b[i] ? '1' : '0';
  return o + ' screen ' + s.width + 'x' + s.height + ' avail ' + s.availWidth + 'x' + s.availHeight +
         ' inner ' + w.innerWidth + 'x' + w.innerHeight + ' outer ' + w.outerWidth + 'x' + w.outerHeight +
         ' dpr ' + w.devicePixelRatio + ' cores ' + n.hardwareConcurrency + ' mem ' + n.deviceMemory +
         ' touch ' + n.maxTouchPoints + ' langs ' + (n.languages || []).length +
         ' platform ' + String(n.platform).slice(0, 24);
})())";


static void
features_found(WKTypeRef result, WKErrorRef, void *) {
  char answer[200] = "";

  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    WKStringGetUTF8CString((WKStringRef)result, answer, sizeof answer);
  }
  say("features %s", answer[0] ? answer : "not read");
}


static void
probe_features(void) {
  run_script(features_script, 0, features_found);
}


// field_script's answer; ctx 1 when the title asked, 0 after a click (then
// only a field that takes text is said)
static void
field_found(WKTypeRef result, WKErrorRef, void *ctx) {
  static char answer[16 + LOGIN_TEXT_MAX];
  const bool asked = ctx != 0;
  int kind = LOGIN_FIELD_NONE;
  char *text = 0;

  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    WKStringGetUTF8CString((WKStringRef)result, answer, sizeof answer);
    if((text=strchr(answer, '\n'))) {
      *text++ = 0;
      kind = !strcmp(answer, "password") ? LOGIN_FIELD_PASSWORD : !strcmp(answer, "email") ? LOGIN_FIELD_EMAIL :
             !strcmp(answer, "number") ? LOGIN_FIELD_NUMBER : !strcmp(answer, "edit") ? LOGIN_FIELD_EDIT :
             LOGIN_FIELD_TEXT;
    }
  }
  if(kind == LOGIN_FIELD_PASSWORD || !text) {
    text = (char *)"";
  }
  if(asked || kind != LOGIN_FIELD_NONE) {
    g.fields++;
    queue(LOGIN_FIELD, (uint16_t)(kind | (asked ? LOGIN_FIELD_ASKED : 0)), text, strlen(text));
  }
  memset(answer, 0, sizeof answer);
}


static void stop(void *);

// on the main thread: WebKit's API is not thread safe
static void
dispatch(void *arg) {
  in_t *in = (in_t *)arg;
  WKPageRef page = WKViewGetPage(g.view);
  const login_point_t *at = (const login_point_t *)in->data;
  const login_wheel_t *w = (const login_wheel_t *)in->data;

  switch(in->m.type) {
  case LOGIN_POINTER:
    if(in->m.size < sizeof *at) {
      break;
    }
    if(in->m.arg == LOGIN_MOVE) {
      WKPageHandleMouseEvent(page, WKMouseEventMake(kWKEventMouseMove, kWKEventMouseButtonNoButton,
                                                    WKPointMake(at->x, at->y), 0, 0));
    } else {
      WKPageHandleMouseEvent(page, WKMouseEventMake(in->m.arg == LOGIN_DOWN ? kWKEventMouseDown : kWKEventMouseUp,
                                                    kWKEventMouseButtonLeftButton, WKPointMake(at->x, at->y), 1, 0));
      if(in->m.arg == LOGIN_UP) {
        g.clicks++;
        run_script(field_script, 0, field_found); // after the click: a field to type in?
      }
    }
    break;
  case LOGIN_WHEEL:
    if(in->m.size >= sizeof *w) {
      // a wheel's delta is positive upwards; a tick is 40 pixels
      WKPageHandleWheelEvent(page, WKWheelEventMake(kWKEventWheel, WKPointMake(w->x, w->y),
                                                    WKSizeMake(-w->dx, -w->dy),
                                                    WKSizeMake(-w->dx / 40.0, -w->dy / 40.0), 0));
    }
    break;
  case LOGIN_KEY:
    switch(in->m.arg) {
    case LOGIN_KEY_RETURN:
      send_key(page, KEY_RETURN, 0, 0, 0);
      break;
    case LOGIN_KEY_BACKSPACE:
      send_key(page, KEY_BACKSPACE, 0, 0, 0);
      break;
    case LOGIN_KEY_TAB:
    case LOGIN_KEY_BACKTAB:
      send_key(page, KEY_TAB, "\t", 1, in->m.arg == LOGIN_KEY_BACKTAB ? kWKEventModifiersShiftKey : 0);
      break;
    case LOGIN_KEY_ESCAPE:
      send_key(page, KEY_ESCAPE, 0, 0, 0);
      break;
    }
    break;
  case LOGIN_TEXT:
    // the select runs in the WebProcess before the keys that follow
    if(in->m.arg == 1) {
      run_script(select_script, 0, ignore_result);
      if(!in->data[0]) {
        send_key(page, KEY_BACKSPACE, 0, 0, 0);
      }
    }
    type_text(page, in->data);
    break;
  case LOGIN_ASK_FIELD:
    run_script(field_script, (void *)1, field_found);
    break;
  case LOGIN_RELOAD: {
    WKURLRef url = WKURLCreateWithUTF8CString(LOGIN_URL);
    g.state.crashed = 0;
    g.state.error = 0;
    WKPageLoadURL(page, url);
    WKRelease(url);
    say("the login page again at %ld ms", since());
    break;
  }
  }
  memset(in, 0, sizeof *in + in->m.size);
  free(in);
}


static void *
reader(void *) {
  login_msg_t m;

  while(recv_all(g.sock, &m, sizeof m, -1)) {
    in_t *in;
    if(m.type == LOGIN_BYE || m.size > LOGIN_TEXT_MAX + 64 || !(in=(in_t *)malloc(sizeof *in + m.size + 1))) {
      break;
    }
    in->m = m;
    if(!recv_all(g.sock, in->data, m.size, -1)) {
      free(in);
      break;
    }
    in->data[m.size] = 0;
    WKRunLoopCallOnMainThread(dispatch, in);
  }
  WKRunLoopCallOnMainThread(stop, 0);
  return 0;
}


static void
stop(void *) {
  if(!g.stopped) {
    g.stopped = true;
    WKRunLoopStopMain();
  }
}


/*
 * Meeting the title.
 */

/**
 * XPS5_LOGIN_KEY's hex into key; 0 or -1
 **/
static int
take_key(uint8_t key[LOGIN_KEY_BYTES]) {
  const char *hex = getenv("XPS5_LOGIN_KEY");
  int ok = hex && strlen(hex) == 2 * LOGIN_KEY_BYTES;

  for(int i=0; ok && i < 2 * LOGIN_KEY_BYTES; i++) {
    char c = hex[i];
    int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    ok = v >= 0;
    key[i / 2] = (uint8_t)(i % 2 ? key[i / 2] | v : v << 4);
  }
  unsetenv("XPS5_LOGIN_KEY");
  return ok ? 0 : -1;
}


/**
 * a socket listening on 127.0.0.1 at the first free port of the range, or -1
 **/
static int
listen_local(int *port) {
  int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;

  if(s < 0) {
    return -1;
  }
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  for(int p=LOGIN_PORT_FIRST; p <= LOGIN_PORT_LAST; p++) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)p);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(!bind(s, (struct sockaddr *)&a, sizeof a) && !listen(s, 2)) {
      *port = p;
      return s;
    }
  }
  close(s);
  return -1;
}


/**
 * from now on what the process writes goes nowhere: WebKit's messages
 * would land in the title's connection
 **/
static void
quiet_stdio(void) {
  int fd = open("/dev/null", O_RDWR);
  int pair[2];

  if(fd < 0 && !socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) {
    close(pair[1]); // writes then fail, reads end at once
    fd = pair[0];
  }
  for(int i=0; i < 3; i++) {
    if(fd >= 0) {
      dup2(fd, i);
    } else {
      close(i);
    }
  }
  if(fd > 2) {
    close(fd);
  }
}


/**
 * the title's connection with its key and a hello that makes sense, or -1
 **/
static int
meet_title(int ls, const uint8_t key[LOGIN_KEY_BYTES]) {
  double until = now_ms() + ACCEPT_MS;

  for(int tries=0; tries < TRIES && now_ms() < until; ) {
    struct pollfd p = {ls, POLLIN, 0};
    login_msg_t m;
    login_hello_t h;
    uint8_t diff = 0;
    int s, one = 1;

    if(poll(&p, 1, (int)(until - now_ms())) <= 0) {
      break;
    }
    if((s=accept(ls, 0, 0)) < 0) {
      continue;
    }
    tries++;
    if(!recv_all(s, &m, sizeof m, HELLO_MS) || m.type != LOGIN_HELLO || m.size != sizeof h ||
       !recv_all(s, &h, sizeof h, HELLO_MS)) {
      say("a connection without a hello");
      close(s);
      continue;
    }
    for(int i=0; i < LOGIN_KEY_BYTES; i++) {
      diff |= h.key[i] ^ key[i];
    }
    memset(h.key, 0, sizeof h.key);
    if(diff || h.magic != LOGIN_MAGIC || h.version != LOGIN_VERSION) {
      say("a connection without the key (version %d)", h.version);
      close(s);
      continue;
    }
    if(h.width < 64 || h.height < 64 || h.width > LOGIN_SIDE_MAX || h.height > LOGIN_SIDE_MAX ||
       h.zoom < 50 || h.zoom > 400) {
      say("a hello out of range: %dx%d, zoom %d", h.width, h.height, h.zoom);
      close(s);
      continue;
    }
    g.width = h.width;
    g.height = h.height;
    g.zoom = h.zoom;
    h.lang[sizeof h.lang - 1] = 0;
    for(char *c=h.lang; *c; c++) {
      if(!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '-')) {
        *c = 0;
        break;
      }
    }
    snprintf(g.lang, sizeof g.lang, "%s", h.lang[0] ? h.lang : "en");
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return s;
  }
  return -1;
}


int
main(void) {
  uint8_t key[LOGIN_KEY_BYTES];
  pthread_t thread;
  int ls, port = 0;

  signal(SIGPIPE, SIG_IGN);
  g.start_ms = now_ms();
  if(take_key(key)) {
    say("no key");
    printf("xps5-login error %d\n", LOGIN_END_NO_TITLE);
    fflush(stdout);
    _exit(LOGIN_END_NO_TITLE);
  }
  if((ls=listen_local(&port)) < 0) {
    say("no port free (errno %d)", errno);
    printf("xps5-login error %d\n", LOGIN_END_NO_PORT);
    fflush(stdout);
    _exit(LOGIN_END_NO_PORT);
  }
  printf("xps5-login port %d\n", port);
  fflush(stdout);
  quiet_stdio();
  say("start, pid %d, port %d", getpid(), port);
  g.sock = meet_title(ls, key);
  close(ls);
  memset(key, 0, sizeof key);
  if(g.sock < 0) {
    say("the title did not come");
    _exit(LOGIN_END_NO_TITLE);
  }
  say("the title came at %ld ms: %dx%d, zoom %d%%", since(), g.width, g.height, g.zoom);
  if(!(g.pixels=(uint32_t *)calloc((size_t)g.width * g.height, 4))) {
    say("no memory for the page");
    _exit(LOGIN_END_NO_START);
  }

  WKRunLoopInitializeMain();
  WKContextConfigurationRef contextConfig = WKContextConfigurationCreate();
  {
    // the page in XPS5's language, English after it
    WKTypeRef languages[2] = {string(strcmp(g.lang, "en") ? g.lang : "en-US"), string("en")};
    WKArrayRef list = WKArrayCreate(languages, 2);
    WKContextConfigurationSetOverrideLanguages(contextConfig, list);
    WKRelease(list);
  }
  // the port takes the processes' folder from BROWSERPS5_PROCESS_DIR (launcher.c)
  WKContextConfigurationSetWebProcessPath(contextConfig, string(ENGINE_DIR "/WebProcess.elf"));
  WKContextConfigurationSetNetworkProcessPath(contextConfig, string(ENGINE_DIR "/NetworkProcess.elf"));
  WKContextConfigurationSetProcessSwapsOnNavigation(contextConfig, false);
  WKContextConfigurationSetPrewarmsProcessesAutomatically(contextConfig, false);
  WKContextRef context = WKContextCreateWithConfiguration(contextConfig);
  WKContextSetUsesSingleWebProcess(context, true);

  WKWebsiteDataStoreConfigurationRef dataConfig = WKWebsiteDataStoreConfigurationCreate();
  WKWebsiteDataStoreConfigurationSetCookieStorageFile(dataConfig, string(DATA_DIR "/cookies.db"));
  WKWebsiteDataStoreConfigurationSetLocalStorageDirectory(dataConfig, string(DATA_DIR "/localstorage"));
  WKWebsiteDataStoreConfigurationSetIndexedDBDatabaseDirectory(dataConfig, string(DATA_DIR "/indexeddb"));
  WKWebsiteDataStoreConfigurationSetGeneralStorageDirectory(dataConfig, string(DATA_DIR "/storage"));
  WKWebsiteDataStoreConfigurationSetServiceWorkerRegistrationDirectory(dataConfig, string(DATA_DIR "/serviceworkers"));
  WKWebsiteDataStoreConfigurationSetCacheStorageDirectory(dataConfig, string(DATA_DIR "/cachestorage"));
  WKWebsiteDataStoreConfigurationSetNetworkCacheDirectory(dataConfig, string(DATA_DIR "/cache"));
  WKWebsiteDataStoreRef data = WKWebsiteDataStoreCreateWithConfiguration(dataConfig);

  WKPageConfigurationRef pageConfig = WKPageConfigurationCreate();
  WKPageConfigurationSetContext(pageConfig, context);
  WKPageConfigurationSetWebsiteDataStore(pageConfig, data);
  g.view = WKViewCreate(pageConfig);
  WKPageRef page = WKViewGetPage(g.view);

  WKViewClientV0 viewClient;
  memset(&viewClient, 0, sizeof viewClient);
  viewClient.base.version = 0;
  viewClient.setViewNeedsDisplay = setViewNeedsDisplay;
  WKViewSetViewClient(g.view, &viewClient.base);

  WKPageNavigationClientV0 navigationClient;
  memset(&navigationClient, 0, sizeof navigationClient);
  navigationClient.base.version = 0;
  navigationClient.decidePolicyForNavigationAction = decidePolicyForNavigationAction;
  navigationClient.didStartProvisionalNavigation = didStartProvisionalNavigation;
  navigationClient.didCommitNavigation = didCommitNavigation;
  navigationClient.didFinishNavigation = didFinishNavigation;
  navigationClient.didFailProvisionalNavigation = didFailNavigation;
  navigationClient.didFailNavigation = didFailNavigation;
  navigationClient.webProcessDidCrash = webProcessDidCrash;
  WKPageSetPageNavigationClient(page, &navigationClient.base);

  // what it loads, and what it asks of the window, for the klog (counts)
  WKPageResourceLoadClientV0 loadClient;
  memset(&loadClient, 0, sizeof loadClient);
  loadClient.base.version = 0;
  loadClient.didSendRequest = didSendRequest;
  loadClient.didReceiveResponse = didReceiveResponse;
  loadClient.didCompleteWithError = didCompleteWithError;
  WKPageSetResourceLoadClient(page, &loadClient.base);

  WKPageUIClientV19 uiClient;
  memset(&uiClient, 0, sizeof uiClient);
  uiClient.base.version = 19;
  uiClient.createNewPage = createNewPage;
  uiClient.runJavaScriptAlert = runJavaScriptAlert;
  uiClient.runJavaScriptConfirm = runJavaScriptConfirm;
  uiClient.runJavaScriptPrompt = runJavaScriptPrompt;
  uiClient.addMessageToConsole = addMessageToConsole;
  uiClient.getWindowFrame = getWindowFrame;
  WKPageSetPageUIClient(page, &uiClient.base);

  // The engine's name says what it runs on: a PlayStation 5 and its system
  // software (0x13600000 is 13.60), as BrowserPS5 RN names itself to YouTube
  // (docs/28, 2026-11-11: 14 runs in 14 accepted). WebKit's own name said
  // "PlayStation/0.00": the port reads the model and version with Sony's
  // getenv_np, which this build has not.
  {
    uint32_t fw = kernel_get_fw_version();
    if(fw) {
      char ua[192];
      snprintf(ua, sizeof ua, "Mozilla/5.0 (PlayStation; PlayStation 5/%x.%02x) AppleWebKit/605.1.15 "
               "(KHTML, like Gecko) Version/18.0 Safari/605.1.15", fw >> 24, (fw >> 16) & 0xff);
      WKStringRef name = WKStringCreateWithUTF8CString(ua);
      WKPageSetCustomUserAgent(page, name);
      WKRelease(name);
      say("named PlayStation 5/%x.%02x", fw >> 24, (fw >> 16) & 0xff);
    } else {
      say("no system software version: WebKit's own name");
    }
  }

  WKViewSetSize(g.view, WKSizeMake(g.width, g.height));
  WKViewSetVisible(g.view, true);
  WKViewSetActive(g.view, true);
  WKViewSetFocus(g.view, true);
  WKPageSetPageZoomFactor(page, g.zoom / 100.0);

  {
    login_ready_t ready = {(uint16_t)g.width, (uint16_t)g.height};
    queue(LOGIN_READY, 0, &ready, sizeof ready);
  }
  pthread_create(&thread, 0, sender, 0);
  pthread_detach(thread);
  pthread_create(&thread, 0, reader, 0);
  pthread_detach(thread);
  pthread_create(&thread, 0, ticker, 0);
  pthread_detach(thread);

  WKURLRef url = WKURLCreateWithUTF8CString(LOGIN_URL);
  WKPageLoadURL(page, url);
  WKRelease(url);
  say("loading X's login page at %ld ms", since());

  WKRunLoopRunMain();

  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  say("closed at %ld ms: %u frames, %u clicks, %u fields, %d pages outside X, ui peak %ld MiB", since(),
      g.frames, g.clicks, g.fields, g.state.blocked, ru.ru_maxrss >> 10);
  pthread_mutex_lock(&g.lock);
  g.closing = true;
  pthread_cond_signal(&g.wake);
  pthread_mutex_unlock(&g.lock);
  // not return: WebKit's threads still run, and the static destructors under
  // them crashed v1's app on its way out (hello_page, 2026-11-03)
  _exit(LOGIN_END_CLOSED);
}
