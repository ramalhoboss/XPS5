/* TwitterPS5 diag: measures how fast the ShellUI browser card is. It serves
a local test page and posts a notification that opens it in the card next to
the game (the same card TwitterPS5 uses for X). The page times plain
JavaScript (an interpreter without JIT is 10-50x slower), JSON, DOM layout,
canvas and animation frame rates, checks WebGL, and sends the results back;
they are printed to stdout (the send.ps1 terminal) and the klog.

Listens on 127.0.0.1 only. Every "Run again" on the page sends another
report; it exits after DIAG_REPORTS reports or DIAG_SECONDS. */

#include <errno.h>
#include <signal.h>
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
#include <sys/time.h>

#include <ps5/klog.h>

#include "toast.h"

#define DIAG_PORT    9081
#define DIAG_SECONDS 600
#define DIAG_REPORTS 4


static const char page[] =
  "<!doctype html><html><head><meta charset=\"utf-8\">"
  "<meta name=\"viewport\" content=\"width=device-width\">"
  "<title>XPS5 diag</title><style>"
  "body{font:22px sans-serif;background:#111;color:#eee;margin:24px}"
  "td{padding:4px 14px;vertical-align:top;word-break:break-all}"
  "button{margin:20px 0;padding:16px 36px;font-size:22px;background:#1d9bf0;"
  "color:#fff;border:0;border-radius:36px}"
  "#b{position:relative;width:60px;height:60px;background:#1d9bf0;border-radius:30px}"
  ".a{animation:m 1s linear infinite alternate}"
  "@keyframes m{from{transform:translateX(0)}to{transform:translateX(300px)}}"
  "#s{height:160px;overflow:auto;border:1px solid #333;margin-top:12px}"
  "</style></head><body><h1>XPS5: card speed</h1>"
  "<div id=\"st\">running...</div><div id=\"b\"></div><div id=\"s\"></div>"
  "<button id=\"go\">Run again</button><table id=\"t\"></table><script>"
  "var run=0;"
  "function ms(f){var t=performance.now();f();return Math.round(performance.now()-t);}"
  // frames per second over len ms; slow = frames that took over 25 ms
  "function frames(len,step,done){var n=0,slow=0,start=performance.now(),last=start;"
  "function f(t){n++;if(t-last>25)slow++;last=t;if(step)step(n);"
  "if(t-start<len)requestAnimationFrame(f);"
  "else done(Math.round(n*1000/(t-start)),slow);}requestAnimationFrame(f);}"
  "function show(r){var t=document.getElementById('t');t.innerHTML='';"
  "for(var k in r){var tr=t.insertRow();tr.insertCell().textContent=k;"
  "tr.insertCell().textContent=String(r[k]);}}"
  "function test(){"
  "var r={run:++run},b=document.getElementById('b'),s=document.getElementById('s');"
  "document.getElementById('st').textContent='running...';"
  "r.viewport=innerWidth+'x'+innerHeight;r.dpr=devicePixelRatio;"
  "r.cores=navigator.hardwareConcurrency;r.wasm=typeof WebAssembly;"
  "r.ua=navigator.userAgent;"
  "r.jsLoopMs=ms(function(){var x=0;for(var i=0;i<1e7;i++){x=(x+i*7)|0;}window.x=x;});"
  "r.jsObjectsMs=ms(function(){var a=[];for(var i=0;i<200000;i++)a.push({id:i,v:(i*7919)%1000});"
  "a.sort(function(p,q){return p.v-q.v;});window.a=a.length;});"
  "r.jsonMs=ms(function(){var o=[];for(var i=0;i<20000;i++)"
  "o.push({id:'t'+i,text:'hello world '+i,likes:i,user:{name:'u'+i}});"
  "window.j=JSON.parse(JSON.stringify(o)).length;});"
  "r.domMs=ms(function(){var d=document.createElement('div');"
  "for(var i=0;i<2000;i++){var p=document.createElement('p');"
  "p.textContent='post '+i+' lorem ipsum dolor sit amet';d.appendChild(p);}"
  "document.body.appendChild(d);window.h=d.offsetHeight;d.remove();});"
  "r.canvasMs=ms(function(){var c=document.createElement('canvas');c.width=c.height=1024;"
  "var g=c.getContext('2d');for(var i=0;i<5000;i++){g.fillStyle='hsl('+i%360+',50%,50%)';"
  "g.fillRect(i%1000,(i*7)%1000,20,20);}g.getImageData(0,0,1,1);});"
  "var c=document.createElement('canvas'),gl=null;r.webgl='none';"
  "try{gl=c.getContext('webgl2');if(gl)r.webgl='webgl2';"
  "else{gl=c.getContext('webgl');if(gl)r.webgl='webgl';}}catch(e){r.webgl='error '+e;}"
  "if(gl){var e=gl.getExtension('WEBGL_debug_renderer_info');"
  "r.glRenderer=e?gl.getParameter(e.UNMASKED_RENDERER_WEBGL):gl.getParameter(gl.RENDERER);}"
  "if(!s.firstChild){var l='';for(var i=0;i<3000;i++)l+='<div>row '+i+'</div>';s.innerHTML=l;}"
  "show(r);b.className='a';"
  "frames(3000,null,function(fps,slow){r.fpsCss=fps;r.slowCss=slow;b.className='';"
  "frames(3000,function(n){b.style.left=(n*8%300)+'px';},function(fps,slow){"
  "r.fpsJs=fps;r.slowJs=slow;b.style.left='0';"
  "frames(3000,function(n){s.scrollTop=n*12;},function(fps,slow){"
  "r.fpsScroll=fps;r.slowScroll=slow;show(r);"
  "document.getElementById('st').textContent='done, run '+r.run;"
  "new Image().src='/report?'+encodeURIComponent(JSON.stringify(r));});});});}"
  "document.getElementById('go').onclick=test;setTimeout(test,500);"
  "</script></body></html>";


static volatile sig_atomic_t stopping;


static void
on_signal(int sig) {
  (void)sig;
  stopping = 1;
}


/**
 * print to stdout and klog
 **/
static void
say(const char *fmt, ...) {
  char buf[4096];
  va_list args;

  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);

  printf("%s\n", buf);
  fflush(stdout);
  klog_printf("[TwitterPS5 diag] %s\n", buf);
}


static void
send_all(int fd, const char *data, size_t len) {
  while(len) {
    ssize_t n = send(fd, data, len, 0);
    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      return;
    }
    data += n;
    len -= (size_t)n;
  }
}


static void
respond(int fd, const char *status, const char *type, const char *body, size_t len) {
  char head[512];
  int n = snprintf(head, sizeof head,
                   "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                   "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                   status, type, len);
  if(n > 0 && (size_t)n < sizeof head) {
    send_all(fd, head, (size_t)n);
    send_all(fd, body, len);
  }
}


/**
 * decode %XX escapes in place
 **/
static void
url_decode(char *s) {
  char *out = s;

  for(; *s; s++) {
    if(s[0] == '%' && s[1] && s[2]) {
      char hex[3] = {s[1], s[2], 0};
      *out++ = (char)strtol(hex, NULL, 16);
      s += 2;
    } else {
      *out++ = *s;
    }
  }
  *out = 0;
}


/**
 * serve one request; returns 1 when it was a report
 **/
static int
serve(int fd) {
  char req[8192];
  size_t used = 0;
  struct timeval tv = {3, 0};

  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  while(used < sizeof req - 1) {
    ssize_t n = recv(fd, req + used, sizeof req - used - 1, 0);
    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      break;
    }
    used += (size_t)n;
    req[used] = 0;
    if(strstr(req, "\r\n\r\n")) {
      break;
    }
  }
  req[used] = 0;
  if(strncmp(req, "GET ", 4)) {
    return 0;
  }

  char *path = req + 4;
  char *end = strchr(path, ' ');
  if(!end) {
    return 0;
  }
  *end = 0;

  if(!strcmp(path, "/")) {
    say("page requested");
    respond(fd, "200 OK", "text/html; charset=utf-8", page, sizeof page - 1);
    return 0;
  }

  if(!strncmp(path, "/report?", 8)) {
    char *data = path + 8;
    url_decode(data);
    say("report: %s", data);
    respond(fd, "204 No Content", "text/plain", "", 0);
    return 1;
  }

  respond(fd, "404 Not Found", "text/plain", "", 0);
  return 0;
}


int
main(void) {
  struct sockaddr_in addr;
  unsigned id;
  int reports = 0;
  int srv;
  int err;
  time_t start = time(NULL);

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  if((srv=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    say("socket: errno %d", errno);
    return EXIT_FAILURE;
  }
  int one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

  memset(&addr, 0, sizeof addr);
  addr.sin_len = sizeof addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(DIAG_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(bind(srv, (struct sockaddr*)&addr, sizeof addr) || listen(srv, 8)) {
    say("bind/listen on port %d: errno %d", DIAG_PORT, errno);
    close(srv);
    return EXIT_FAILURE;
  }

  if((err=toast_post("http://127.0.0.1:9081/", "Card speed test", "Run test", &id))) {
    say("sceNotificationSend: 0x%08x", (unsigned)err);
    close(srv);
    fflush(stdout);
    _exit(EXIT_FAILURE);
  }
  say("notification %u posted; waiting for reports (max %d s)", id, DIAG_SECONDS);

  while(!stopping && reports < DIAG_REPORTS &&
        difftime(time(NULL), start) < DIAG_SECONDS) {
    fd_set rd;
    struct timeval wait = {1, 0};

    FD_ZERO(&rd);
    FD_SET(srv, &rd);
    if(select(srv + 1, &rd, NULL, NULL, &wait) <= 0) {
      continue;
    }
    int fd = accept(srv, NULL, NULL);
    if(fd < 0) {
      continue;
    }
    reports += serve(fd);
    close(fd);
  }

  say("done: %d report(s)", reports);
  close(srv);

  // libSceNotification throws "resource deadlock would occur" from its exit
  // handlers, so skip them
  fflush(stdout);
  _exit(reports ? EXIT_SUCCESS : EXIT_FAILURE);
}
