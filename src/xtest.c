/* TwitterPS5 X test: reads the Following timeline with the user's own X
session, the same request x.com makes in a browser, and shows the latest
posts as notifications on the PS5.

The session is the auth_token and ct0 cookies, which the user puts in
/data/TwitterPS5/session.txt (see xapi.h). Only the HTTP status and counts
go to stdout and klog; the session and the posts never do.

HTTPS goes through https.c. The first runs of this test found what it
needs on FW 13.60: payloads get no root certificates from the system, and
the PS5 verifies x.com only with the Let's Encrypt chain loaded as PEM. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ps5/klog.h>

#include "https.h"
#include "json.h"
#include "mem.h"
#include "xapi.h"

#define SHOW_POSTS 10


typedef struct notify_request {
  char unused[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);


/**
 * status for stdout and klog; never pass the session or a post here
 **/
static void
log_text(const char *line) {
  printf("%s\n", line);
  fflush(stdout);
  klog_printf("[TwitterPS5 xtest] %s\n", line);
}


static void
log_line(const char *fmt, ...) {
  char buf[512];
  va_list args;

  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  log_text(buf);
}


/**
 * a notification on the PS5's screen, which is where the posts go
 **/
static void
notify(const char *fmt, ...) {
  notify_request_t req;
  va_list args;

  memset(&req, 0, sizeof req);
  va_start(args, fmt);
  vsnprintf(req.message, sizeof req.message, fmt, args);
  va_end(args);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}


static int
read_session(x_session_t *s) {
  int ret = x_session_read(s, X_SESSION_PATH);

  if(ret == -3) {
    log_line("no " X_SESSION_PATH);
    notify("X test: put your auth_token and ct0 cookies in " X_SESSION_PATH);
    return -1;
  }
  if(ret) {
    log_line("session file has no %s", ret == -1 ? "auth_token" : "ct0");
    notify("X test: %s is missing from " X_SESSION_PATH,
           ret == -1 ? "auth_token" : "ct0");
    return -1;
  }
  // lengths only: 40 and 160 characters is what x.com sets
  log_line("session: auth_token %zu chars, ct0 %zu chars",
           strlen(s->auth_token), strlen(s->ct0));
  return 0;
}


static void
show_answer(https_response_t *res) {
  x_post_t *posts;
  json_doc_t *doc;
  const char *message;
  char line[300];
  size_t err_off;
  int code, shown, total;

  if(res->len >= 2 && (unsigned char)res->body[0] == 0x1f &&
     (unsigned char)res->body[1] == 0x8b) {
    log_line("the answer is still gzip-compressed");
    notify("X test: HTTP %d, but the answer came compressed", res->status);
    return;
  }
  if(!(doc=json_parse(res->body, res->len, &err_off))) {
    log_line("answer is not JSON (stops at byte %zu of %zu)", err_off, res->len);
    notify("X test: HTTP %d, %zu bytes that are not JSON", res->status, res->len);
    return;
  }

  // X's error texts describe the request, not anyone's posts
  if((code=x_first_error(json_root(doc), &message)) >= 0) {
    log_line("X error %d: %.200s", code, message ? message : "");
  }
  if(res->status != 200) {
    if(code >= 0) {
      notify("X test: HTTP %d, X error %d", res->status, code);
    } else {
      notify("X test: HTTP %d", res->status);
    }
    json_free(doc);
    return;
  }

  if(!(posts=calloc(SHOW_POSTS, sizeof *posts))) {
    json_free(doc);
    return;
  }
  shown = x_timeline_posts(json_root(doc), posts, SHOW_POSTS, &total);
  log_line("timeline: %d posts in the answer, showing %d", total, shown);
  if(!shown) {
    notify("X test: the timeline answer has no posts");
  }
  for(int i=0; i<shown; i++) {
    x_post_line(&posts[i], line, sizeof line);
    notify("%d/%d %s", i + 1, shown, line);
    usleep(1500 * 1000);
  }

  memset(posts, 0, SHOW_POSTS * sizeof *posts);
  free(posts);
  json_free(doc);
}


int
main(void) {
  static char url[8192];
  https_response_t res;
  x_session_t session;
  x_request_t request;
  int ret;

  log_line("started");
  if(read_session(&session)) {
    return EXIT_FAILURE;
  }
  if(x_timeline_url(url, sizeof url, 20, 1, NULL)) {
    log_line("timeline URL does not fit");
    return EXIT_FAILURE;
  }
  if(https_init(X_USER_AGENT, log_text)) {
    notify("X test: HTTPS does not start");
    return EXIT_FAILURE;
  }

  notify("X test: reading your Following timeline...");
  x_request_init(&request, &session);
  ret = https_get(url, request.headers, X_HEADER_COUNT, &res);
  memset(&session, 0, sizeof session);
  memset(&request, 0, sizeof request);
  https_fini();

  if(ret) {
    notify("X test: could not reach x.com (0x%08x)", (unsigned)ret);
    return EXIT_FAILURE;
  }
  log_line("HTTP %d, %zu bytes", res.status, res.len);
  show_answer(&res);
  mem_free(res.body);
  return EXIT_SUCCESS;
}
