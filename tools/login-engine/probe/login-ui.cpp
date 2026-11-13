/* The login engine's UIProcess for its first test on the PS5 (phase 2),
started by probe.c with the launcher's socket at BROWSERPS5_LAUNCHER_FD.

It opens X's login page in a view nobody sees (1280x720), paints it into
memory as XPS5's window would, and says how far the page got: when it
committed and finished, then, once its scripts have run, how many fields
and buttons it has. Nothing is typed and nothing of the page is kept or
logged: counts, codes and times only. The page's data (cookies, storage)
goes to ENGINE_DIR/web, which probe.c deletes after.

The engine's user agent is its own (the PlayStation port's): no other
device's or browser's is claimed.

The WebKit calls are those of BrowserPS5-WebKit v1's hello_page and browser
(read only): one WebProcess, no process swaps, no prewarming, the data in a
folder of ours. */

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <WebKit/WKArray.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationPlayStation.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKErrorRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPagePrivatePlayStation.h>
#include <WebKit/WKRunLoop.h>
#include <WebKit/WKString.h>
#include <WebKit/WKType.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKView.h>
#include <WebKit/WKViewClient.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>
#include <WebKit/WKWebsiteDataStoreRef.h>

#include <ps5/klog.h>

#define ENGINE_DIR "/data/TwitterPS5/engine"
#define DATA_DIR   ENGINE_DIR "/web"
#define LOGIN_URL  "https://x.com/i/flow/login"
#define WIDTH      1280
#define HEIGHT     720
#define LOOKS      4     // how often the page is looked at after it finished
#define LOOK_EVERY 15    // seconds between the looks
#define NO_FINISH  150   // seconds: the page never finished

static struct {
  WKViewRef view;
  uint32_t *pixels;
  WKRect damage;
  bool paint_pending, finished, done;
  unsigned paints, looks;
  double start_ms, finish_ms;
} g;


static double
now_ms(void) {
  struct timespec t;

  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}


static void
say(const char *fmt, ...) {
  char line[200];
  va_list args;

  va_start(args, fmt);
  vsnprintf(line, sizeof line, fmt, args);
  va_end(args);
  klog_printf("[XPS5 login] ui: %s\n", line);
  printf("ui: %s\n", line);
  fflush(stdout);
}


static WKStringRef
string(const char *s) {
  return WKStringCreateWithUTF8CString(s);
}


static long
since(void) {
  return (long)(now_ms() - g.start_ms);
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
paint(void *) {
  WKRect rect = g.damage;
  double t0 = now_ms();

  g.damage = WKRectMake(0, 0, 0, 0);
  g.paint_pending = false;
  WKPagePaint(WKViewGetPage(g.view), (unsigned char *)g.pixels, WKSizeMake(WIDTH, HEIGHT), rect);
  if(!g.paints++) {
    say("first paint at %ld ms (%.1f ms)", (long)(t0 - g.start_ms), now_ms() - t0);
  }
}


static void
setViewNeedsDisplay(WKViewRef, WKRect rect, const void *) {
  g.damage = rect_union(g.damage, rect);
  if(!g.paint_pending) {
    g.paint_pending = true;
    WKRunLoopCallOnMainThread(paint, 0);
  }
}


static void
didCommitNavigation(WKPageRef, WKNavigationRef, WKTypeRef, const void *) {
  say("committed at %ld ms", since());
}


static void
didFinishNavigation(WKPageRef, WKNavigationRef, WKTypeRef, const void *) {
  if(!g.finished) {
    g.finished = true;
    g.finish_ms = now_ms();
    say("finished at %ld ms", since());
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
  // the error's domain (WebKit's names, such as "WebKitNetworkError") and
  // code; not its text, which can hold the address
  say("failed at %ld ms: %s %d", since(), domain, WKErrorGetErrorCode(error));
}


static void
webProcessDidCrash(WKPageRef, const void *) {
  say("the WebProcess crashed at %ld ms", since());
}


/**
 * how much of the painted page is not its background (the top left
 * pixel's colour), in tenths of a percent: an empty page is 0
 **/
static int
painted_share(void) {
  uint32_t bg = g.pixels[0];
  long other = 0;

  for(long i=0; i < (long)WIDTH * HEIGHT; i++) {
    other += g.pixels[i] != bg;
  }
  return (int)(other * 1000 / ((long)WIDTH * HEIGHT));
}


// counts of the page's parts only: its fields, by kind, its buttons and
// frames, and whether it is still on an x.com address
static const char look_script[] =
  "(function () {"
  "  var f = document.querySelectorAll('input'), text = 0, pass = 0;"
  "  for (var i = 0; i < f.length; i++) {"
  "    var t = (f[i].type || 'text').toLowerCase();"
  "    if (t == 'password') pass++; else if (t == 'text' || t == 'email' || t == 'tel') text++;"
  "  }"
  "  var h = location.hostname, x = h == 'x.com' || h.slice(-6) == '.x.com';"
  "  return f.length + ' ' + text + ' ' + pass + ' ' +"
  "    document.querySelectorAll('button,[role=button]').length + ' ' +"
  "    document.querySelectorAll('iframe').length + ' ' + (x ? 1 : 0) + ' ' +"
  "    (document.readyState == 'complete' ? 1 : 0);"
  "})()";


static void stop(void *);

static void
looked(WKTypeRef result, WKErrorRef error, void *) {
  char counts[64] = "";
  int inputs = 0, text = 0, pass = 0, buttons = 0, frames = 0, x = 0, complete = 0;
  struct rusage ru;

  g.looks++;
  if(result && WKGetTypeID(result) == WKStringGetTypeID()) {
    WKStringGetUTF8CString((WKStringRef)result, counts, sizeof counts);
    sscanf(counts, "%d %d %d %d %d %d %d", &inputs, &text, &pass, &buttons, &frames, &x, &complete);
    say("look %u at %ld ms: %d fields (%d text, %d password), %d buttons, %d frames, x.com %d, complete %d",
        g.looks, since(), inputs, text, pass, buttons, frames, x, complete);
  } else {
    say("look %u at %ld ms: no answer (error %d)", g.looks, since(), error ? WKErrorGetErrorCode(error) : 0);
  }
  getrusage(RUSAGE_SELF, &ru);
  say("look %u: %u paints, %d.%d%% of the page painted, ui peak %ld MiB", g.looks, g.paints,
      painted_share() / 10, painted_share() % 10, ru.ru_maxrss >> 10);
  if(g.looks >= LOOKS || (text && g.looks >= 2)) {
    WKRunLoopCallOnMainThread(stop, 0);
  }
}


static void
look(void *) {
  WKStringRef s = string(look_script);

  WKPageEvaluateJavaScriptInMainFrame(WKViewGetPage(g.view), s, 0, looked);
  WKRelease(s);
}


static void
stop(void *) {
  if(!g.done) {
    g.done = true;
    WKRunLoopStopMain();
  }
}


// the looks, LOOK_EVERY seconds apart once the page finished
static void *
timer(void *) {
  unsigned asked = 0;

  for(;;) {
    sleep(1);
    if(g.done) {
      return 0;
    }
    if(!g.finished) {
      if(since() > NO_FINISH * 1000L) {
        say("not finished after %d s", NO_FINISH);
        WKRunLoopCallOnMainThread(look, 0);
        sleep(5);
        WKRunLoopCallOnMainThread(stop, 0);
        return 0;
      }
      continue;
    }
    if(asked < LOOKS && now_ms() - g.finish_ms >= (asked + 1) * LOOK_EVERY * 1000.0) {
      asked++;
      WKRunLoopCallOnMainThread(look, 0);
    }
    if(asked >= LOOKS && now_ms() - g.finish_ms >= (LOOKS + 2) * LOOK_EVERY * 1000.0) {
      WKRunLoopCallOnMainThread(stop, 0);
      return 0;
    }
  }
}


int
main(void) {
  pthread_t thread;

  g.start_ms = now_ms();
  if(!(g.pixels=(uint32_t *)calloc((size_t)WIDTH * HEIGHT, 4))) {
    say("no memory for the page");
    return 1;
  }
  say("start, pid %d", getpid());
  {
    // probe.c's environment: with it the SDK's loader takes the engine's
    // libraries from ENGINE_DIR before /user/homebrew/lib
    const char *lib = getenv("LD_LIBRARY_PATH"), *fonts = getenv("FONTCONFIG_FILE"), *tmp = getenv("TMPDIR");
    say("environment: libraries %d, fonts %d, temporary files %d", lib && !strcmp(lib, ENGINE_DIR),
        fonts && !strcmp(fonts, ENGINE_DIR "/fonts.conf"), tmp && !strcmp(tmp, ENGINE_DIR "/tmp"));
  }

  WKRunLoopInitializeMain();
  WKContextConfigurationRef contextConfig = WKContextConfigurationCreate();
  {
    WKTypeRef languages[2] = {string("en-US"), string("en")};
    WKArrayRef list = WKArrayCreate(languages, 2);
    WKContextConfigurationSetOverrideLanguages(contextConfig, list);
    WKRelease(list);
  }
  // the port takes the processes' folder from BROWSERPS5_PROCESS_DIR (probe.c)
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
  navigationClient.didCommitNavigation = didCommitNavigation;
  navigationClient.didFinishNavigation = didFinishNavigation;
  navigationClient.didFailProvisionalNavigation = didFailNavigation;
  navigationClient.didFailNavigation = didFailNavigation;
  navigationClient.webProcessDidCrash = webProcessDidCrash;
  WKPageSetPageNavigationClient(page, &navigationClient.base);

  WKViewSetSize(g.view, WKSizeMake(WIDTH, HEIGHT));
  WKViewSetVisible(g.view, true);
  WKViewSetActive(g.view, true);
  WKViewSetFocus(g.view, true);

  WKURLRef url = WKURLCreateWithUTF8CString(LOGIN_URL);
  WKPageLoadURL(page, url);
  WKRelease(url);
  say("loading X's login page at %ld ms", since());

  pthread_create(&thread, 0, timer, 0);
  WKRunLoopRunMain();

  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  say("done at %ld ms: %u paints, ui peak %ld MiB", since(), g.paints, ru.ru_maxrss >> 10);
  // not return: WebKit's threads still run, and the static destructors under
  // them crashed v1's app on its way out (hello_page, 2026-11-03)
  _exit(0);
}
