/* TwitterPS5: the user's X as a native title (TWPS00001) that opens over
the game: the timelines, notifications, bookmarks, profiles and posts with
their replies. They come from the requests x.com itself makes, with the
user's own session (see xapi.h), read on a thread of their own; the pictures
come from pbs.twimg.com on a few more, so the screen keeps answering the
controller. ui.c lays out and draws it, player.c plays the videos. New posts
and replies carry the PS5's own screenshots and clips, uploaded to
upload.x.com the way x.com's composer does. When it opens, it also starts
the payload that shows new X notifications over the game (src/notify.c),
which also reads the captures for it when its sandbox does not let it.

Only codes and counts go to the klog; the session, the posts and the
pictures' addresses never do. Returning from main crashes this launch
context, so main never returns. */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "captures.h"
#include "cards.h"
#include "clip.h"
#include "dictproto.h"
#include "game.h"
#include "https.h"
#include "ime.h"
#include "images.h"
#include "jpeg.h"
#include "jxr.h"
#include "json.h"
#include "link.h"
#include "loginlink.h"
#include "loginproto.h"
#include "mem.h"
#include "mic.h"
#include "phone.h"
#include "player.h"
#include "seen.h"
#include "sessions.h"
#include "settings.h"
#include "sound.h"
#include "space.h"
#include "text.h"
#include "ui.h"
#include "update.h"
#include "video.h"
#include "voice.h"
#include "ultrahdr.h"
#include "workers.h"
#include "xapi.h"
#include "xchat.h"
#include "xchatopen.h"
#include "xcrypt.h"
#include "juicebox.h"
#include "xsign.h"
#include "xtid.h"

// the installer copies the session file here too, for when the title
// cannot read /data; it is read only while /data has no session file of
// the first account at all (an empty one is an account logged out)
#define APP_SESSION_PATH "/app0/session.txt"
#define APP_SESSION_COPY "/data/homebrew/TWPS00001/session.txt" // the same, where the installer puts it

#define TIMELINE_COUNT 20
#define SEARCH_CHARS   100        // the keyboard of a search takes this many
#define MAX_POSTS      200
#define LOADERS        3          // pictures fetched at once
#define IMAGE_BUDGET   64         // MiB of pictures kept at 1080p, at most
#define WORKERS        3          // threads that share the screen's heaviest passes

#define PROT_CPU_RW 0x03

#define PAD_KEYS    (UI_UP | UI_DOWN | UI_LEFT | UI_RIGHT | UI_L1 | UI_R1 | UI_L2 | UI_R2 | \
                     UI_TRIANGLE | UI_CIRCLE | UI_CROSS | UI_SQUARE | UI_OPTIONS | UI_TOUCHPAD | UI_R3)
#define PAD_REPEATS (UI_UP | UI_DOWN | UI_L1 | UI_R1)

#define REPEAT_DELAY 24 // frames a button is held before it repeats
#define REPEAT_EVERY 5

#define PHOTO_SIDE    2560 // a screenshot over X's 5 MB is made this big at most, as a JPEG
#define PHOTO_QUALITY 88
#define HDR_QUALITY   92   // a .jxr screenshot made a JPEG: as sharp as fits in X's 5 MB
#define GAIN_QUALITY  90   // its gain map's JPEG, for Ultra HDR
#define ULTRAHDR_ROOM 4096 // what Ultra HDR's XMP and MPF take, at most
#define PROCESS_LIMIT (10 * 60) // seconds X may take to process a video


int sceKernelDebugOutText(int channel, const char *text);
int sceKernelUsleep(unsigned microseconds);
uint64_t sceKernelGetProcessTime(void);
int sceKernelAvailableFlexibleMemorySize(size_t *size);
int sceKernelMapFlexibleMemory(void **addr, size_t len, int prot, int flags);
int sceKernelMunmap(void *addr, size_t len);
int sceSystemServiceParamGetInt(int id, int *value);
int sceUserServiceInitialize(const void *params);
int sceUserServiceGetInitialUser(int *user);
int scePadInit(void);
int scePadOpen(int user, int type, int index, const void *param);
int scePadReadState(int handle, void *data);
int scePadSetLightBar(int handle, const void *param);
int scePadResetLightBar(int handle);
int scePadSetVibration(int handle, const void *param);    // {large motor, small motor}, 0 to 255
int scePadSetTriggerEffect(int handle, const void *param); // pad_triggers_t


enum { FETCH_IDLE, FETCH_RUNNING, FETCH_DONE };

typedef struct fetch {
  int          state; // FETCH_*, handed over with acquire and release
  ui_request_t req;   // what to read
  int          error; // ERR_* of ui.h
  int          code;
  int          x_code;
  x_post_t    *posts;
  int          count;
  char         next[X_CURSOR_MAX]; // of the posts after these
  int          has_user;           // the profile req asked for came
  x_user_t     user;
  x_post_t     party[UI_PARTY_MAX]; // of a conversation, its people (ui_party)
  int          party_count;
  seen_t       seen;               // with a look at the people the user follows, the
  int          seen_ok;            // account's file of who was seen around (ui_seen_set)
} fetch_t;

static int seen_read(int account, seen_t *s);

// a like, or a post or reply with its captures, sent on a thread of its own
typedef struct action {
  int       state; // FETCH_*
  int       kind;  // UI_ACT_*
  int       error; // ERR_* of ui.h
  int       code;
  int       x_code;
  char      id[sizeof ((x_post_t *)0)->id]; // empty for a post of the user's own
  char      user[sizeof ((x_post_t *)0)->user]; // of the post quoted
  char      text[X_REPLY_MAX + 1];
  capture_t media[UI_MEDIA_MAX];
  int       count;
  long long sent;  // bytes of the media uploaded, atomic
  long long total; // of all of them
  long long at;    // when a scheduled post goes out, Unix time
  size_t    ultra_hdr;  // the bytes of an Ultra HDR picture it uploaded, or 0
  int       poll_count; // the choices of a post's poll, or 0
  char      choices[X_POLL_MAX][X_POLL_LABEL];
  int       minutes;    // how long the poll stays open
  char      card[48];   // a vote's poll card and its name, and the choice
  char      card_name[32];
  int       choice;
  char      game[GAME_NAME];         // Grok's question is about it, in text
  int       fresh;                   // in a new conversation
  int       lang;                    // LANG_* it is answered in
  char      answer[X_GROK_ANSWER];   // and its answer; of a Space, where its sound is
  int       grok;                    // UI_GROK_*: a hint, a translation (of user's text), a catch-up
  x_space_t space;                   // the Space UI_ACT_SPACE asked about
} action_t;

// the PS5's captures, listed on a thread of their own for the composer
typedef struct listing {
  int       state; // FETCH_*
  int       count; // or -1
  int       error;
  int       code;
  capture_t list[CAPTURES_MAX];
  char      game_id[GAME_ID], game_name[GAME_NAME]; // the game that runs, for their order
} listing_t;

// the game that runs beside the title, asked of the payload now and then
#define GAME_EVERY 45 // seconds
#define BADGE_EVERY 2 // seconds between the payload's counts for the bar (asked on 127.0.0.1: cheap)
typedef struct game {
  int       state; // FETCH_*
  long long asked;
  long long seen;  // the last frame's time: a gap says the app was behind the game
  int       share; // the Settings let the game's new captures be asked for
  char      id[GAME_ID], name[GAME_NAME];
  capture_t shot;  // a new capture of the game the payload offered, path empty for none
  int       trophy; // a new trophy's grade (0 bronze to 3 platinum), -1 for none
  char      trophy_name[128];
  // asked of it by the screen, before it starts: the week's sessions, and
  // the game's trophies again
  int       want_week, want_trophies;
  // what came: a session that ended (session_ok), the week (week_ok 1, or
  // -1 when it could not be read), and the game's trophies (troph_count:
  // TROPHIES_NOT_READ, -1 for none, else how many troph_rows has)
  int       session_ok;
  session_t session;
  int       week_ok;
  week_t    week;
  char      troph_id[GAME_ID];
  long long troph_at;
  int       troph_count;
  ui_trophy_row_t *troph_rows;
} game_t;
#define TROPHIES_NOT_READ -2
#define TROPHIES_EVERY    600 // seconds a game's trophies are read again, at most


static text_surface_t g_screen;
static fetch_t        g_fetch;
static action_t       g_action;
static listing_t      g_listing;
static game_t         g_game;
static settings_t     g_settings;
static int            g_https = -1; // 0 once https_init worked, else its error
static int            g_pad = -1;
static unsigned       g_frame;
static int            g_mic_on, g_mic_shown; // the microphone's test: started, and its state on the screen

_Static_assert(MIC_OFF == UI_MIC_OFF && MIC_OPENING == UI_MIC_OPENING && MIC_ON == UI_MIC_ON &&
               MIC_FAILED == UI_MIC_FAILED && MIC_NO_DEVICE == UI_MIC_NO_DEVICE &&
               MIC_PRIORITY_LOW == UI_MIC_PRIORITY_LOW, "the microphone's states as ui.h has them");


static void
log_text(const char *line) {
  char buf[300];

  snprintf(buf, sizeof buf, "[TwitterPS5] %s\n", line);
  sceKernelDebugOutText(0, buf);
}


/**
 * status for the klog; never pass the session, a post or a URL here
 **/
static void
log_line(const char *fmt, ...) {
  char buf[256];
  va_list args;

  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  log_text(buf);
}


/**
 * start fn(arg) on a thread with a stack of that size; returns whether it
 * started
 **/
static int
start_thread(void *(*fn)(void *), void *arg, size_t stack) {
  pthread_attr_t attr;
  pthread_t thread;
  int ok;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, stack);
  ok = !pthread_create(&thread, &attr, fn, arg);
  if(ok) {
    pthread_detach(thread);
  }
  pthread_attr_destroy(&attr);
  return ok;
}


/*
 * Memory: malloc has a heap of a few MiB, so the big blocks (see mem.h)
 * come from the title's flexible memory.
 */

static void *
map_flexible(size_t len) {
  void *p = NULL;

  return sceKernelMapFlexibleMemory(&p, len, PROT_CPU_RW, 0) ? NULL : p;
}


static void
unmap_flexible(void *p, size_t len) {
  sceKernelMunmap(p, len);
}


/**
 * how many MiB malloc gives, up to max, freed again; for the klog
 **/
static int
heap_free(int max) {
  void *last = NULL, *b;
  int n = 0;

  while(n < max && (b=malloc(1 << 20))) {
    *(void **)b = last;
    last = b;
    n++;
  }
  while(last) {
    b = *(void **)last;
    free(last);
    last = b;
  }
  return n;
}


/*
 * The timelines, read on a thread of their own.
 */

static volatile int g_account = 1; // whose session, as the Settings chose it

/*
 * Each account's session as last read, for when its file stops opening: back
 * from a game (the title suspended, saved to the SSD and restored) the
 * title has found /data/TwitterPS5/session.txt unreadable while the payload
 * still read it, and every refresh failed until XPS5 was opened again. A
 * logged-out account (its file empty) or a file gone from a folder that is
 * there is not read from here. Only errno goes to the klog.
 */
static pthread_mutex_t g_kept_lock = PTHREAD_MUTEX_INITIALIZER;
static x_session_t g_kept[SETTINGS_ACCOUNTS + 1];
static int g_kept_said[SETTINGS_ACCOUNTS + 1]; // the errno last logged + 1, 0 for none

/**
 * whether the account's file failed to read for a reason other than being
 * logged out or removed
 **/
static int
kept_usable(const char *path) {
  struct stat st;

  if(!stat(path, &st)) {
    return st.st_size > 0; // empty: logged out
  }
  if(errno == ENOENT && !stat(SETTINGS_SESSION_DIR, &st)) {
    return 0; // the folder is there, the file is not
  }
  return 1;
}

static void
kept_set(int account, const x_session_t *s) {
  if(account < 1 || account > SETTINGS_ACCOUNTS) {
    return;
  }
  pthread_mutex_lock(&g_kept_lock);
  if(s) {
    g_kept[account] = *s;
  } else {
    memset(&g_kept[account], 0, sizeof g_kept[account]);
  }
  g_kept_said[account] = 0;
  pthread_mutex_unlock(&g_kept_lock);
}

static int
read_session_of(int account, x_session_t *s) {
  char path[64];
  int ret, err, said;
  struct stat st;

  if(settings_session_path(account, path, sizeof path)) {
    return -3;
  }
  errno = 0;
  ret = x_session_read(s, path);
  err = errno;
  pthread_mutex_lock(&g_kept_lock);
  if(ret == -3 && account == 1 && stat(path, &st) &&
     !(g_kept[account].auth_token[0] && kept_usable(path))) {
    ret = x_session_read(s, APP_SESSION_PATH); // the installer's copy, when /data has none
  }
  if(!ret) {
    g_kept[account] = *s;
    if(g_kept_said[account]) {
      log_line("session: the file reads again");
      g_kept_said[account] = 0;
    }
  } else if(ret == -3 && g_kept[account].auth_token[0] && kept_usable(path)) {
    *s = g_kept[account];
    ret = 0;
    if(g_kept_said[account] != err + 1) {
      said = !stat(SETTINGS_SESSION_DIR, &st);
      log_line("session: the file does not open (errno %d, its folder %s), the one read before is used",
               err, said ? "there" : "not reached");
      g_kept_said[account] = err + 1;
    }
  } else if(ret == -3 && g_kept_said[account] != -(err + 1)) {
    said = !stat(SETTINGS_SESSION_DIR, &st);
    log_line("session: none read (errno %d, its folder %s, one read before %d)", err,
             said ? "there" : "not reached", !!g_kept[account].auth_token[0]);
    g_kept_said[account] = -(err + 1);
  }
  pthread_mutex_unlock(&g_kept_lock);
  return ret;
}

/**
 * the session of the account the Settings chose
 **/
static int
read_session(x_session_t *s) {
  return read_session_of(g_account, s);
}


/**
 * what the lists are called in the klog
 **/
static const char *
feed_name(int feed) {
  static const char *const names[UI_FEEDS] = {
    [UI_FOR_YOU] = "for you", [UI_FOLLOWING] = "following", [UI_SEARCH] = "search",
    [UI_NOTIFICATIONS] = "notifications", [UI_BOOKMARKS] = "bookmarks", [UI_ME] = "own profile",
    [UI_CHAT] = "chat", [UI_CALLS] = "calls", [UI_GROK] = "grok", [UI_PREMIUM] = "subscription",
    [UI_HISTORY] = "history", [UI_ARTICLES] = "articles", [UI_USER] = "profile", [UI_POST] = "post",
    [UI_DM] = "conversation", [UI_LISTS] = "lists", [UI_LIST] = "list", [UI_GAME] = "game",
    [UI_FRIENDS] = "friends",
  };
  return feed >= 0 && feed < UI_FEEDS ? names[feed] : "?";
}


static int  sign(const x_session_t *session, const char *method, const char *url, char *tid,
                 size_t size);
static void forget_tid(void);


/**
 * GET url with the session, and the x-client-transaction-id tid unless it
 * is NULL, and parse the answer; returns the document, or NULL with f's
 * error set, and then res's body freed
 **/
static json_doc_t *
get_json(fetch_t *f, const char *url, const x_session_t *session, https_response_t *res,
         const char *what, const char *tid) {
  https_header_t headers[X_HEADER_COUNT + 1];
  int count = X_HEADER_COUNT;
  x_request_t request;
  const char *message;
  json_doc_t *doc;
  size_t err_off, left = 0;
  int ret, why;

  x_request_init(&request, session);
  memcpy(headers, request.headers, sizeof request.headers);
  if(tid) {
    headers[count++] = (https_header_t){"x-client-transaction-id", tid};
  }
  ret = https_get(url, headers, count, res);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  if(ret) {
    f->error = ERR_NETWORK;
    f->code = ret;
    return NULL;
  }
  log_line("%s%s: HTTP %d, %zu bytes", what, tid ? " (tid)" : "", res->status, res->len);

  f->code = res->status;
  f->x_code = -1;
  if(!(doc=json_parse_why(res->body, res->len, &err_off, &why))) {
    f->error = res->status == 200 ? ERR_ANSWER : ERR_HTTP;
    if(why == JSON_ERR_MEMORY) {
      f->error = ERR_MEMORY;
      sceKernelAvailableFlexibleMemorySize(&left);
      log_line("answer is not JSON: no memory at byte %zu, %zu MiB flexible left", err_off,
               left >> 20);
    } else {
      log_line("answer is not JSON: %s at byte %zu (0x%02x)", why == JSON_ERR_DEPTH ? "too deep" :
               "syntax", err_off, err_off < res->len ? (unsigned char)res->body[err_off] : 0);
    }
    mem_free(res->body);
    res->body = NULL;
    return NULL;
  }
  if((f->x_code=x_first_error(json_root(doc), &message)) >= 0) {
    log_line("X error %d", f->x_code);
  }
  if(res->status != 200) {
    f->error = ERR_HTTP;
    json_free(doc);
    mem_free(res->body);
    res->body = NULL;
    return NULL;
  }
  return doc;
}


/**
 * the profile f asks for: the signed-in user's, or the one with f's number;
 * returns 0, or -1 with f's error set
 **/
static int
fetch_profile(fetch_t *f, const x_session_t *session, char *url, size_t size) {
  https_response_t res;
  json_doc_t *doc;
  int ret;

  if(x_user_url(url, size, f->req.id[0] ? f->req.id : NULL)) {
    f->error = ERR_MEMORY;
    return -1;
  }
  if(!(doc=get_json(f, url, session, &res, "profile", NULL))) {
    return -1;
  }
  if((ret=x_user_parse(json_root(doc), &f->user))) {
    f->error = ERR_ANSWER;
  } else {
    f->has_user = 1;
  }
  log_line("profile: %s", ret ? "none in the answer" : "read");
  json_free(doc);
  mem_free(res.body);
  return ret;
}


/**
 * a conversation just read is read up to its newest message (last), as
 * x.com marks it when it is opened; once for each newest message, as the
 * conversation shown is read again and again; only how it went goes to the
 * klog
 **/
static void
mark_read(const x_session_t *session, const char *conversation, const char *last, char *url, size_t usize,
          char *tid, size_t tsize) {
  static char marked[2 * X_ID_MAX + 2]; // "<conversation> <message>" of the last one marked
  https_header_t headers[X_HEADER_COUNT + 2];
  https_response_t res = {0};
  x_request_t request;
  char body[192], now[sizeof marked];
  int count = X_HEADER_COUNT, ret, signed_tid;

  snprintf(now, sizeof now, "%s %s", conversation ? conversation : "", last ? last : "");
  if(!strcmp(now, marked) || x_dm_read_request(url, usize, body, sizeof body, conversation, last)) {
    memset(now, 0, sizeof now);
    return;
  }
  signed_tid = sign(session, "POST", url, tid, tsize);
  x_request_init(&request, session);
  memcpy(headers, request.headers, sizeof request.headers);
  for(int i=0; i < X_HEADER_COUNT; i++) {
    if(!strcmp(headers[i].name, "content-type")) {
      headers[i].value = "application/x-www-form-urlencoded";
    }
  }
  if(signed_tid) {
    headers[count++] = (https_header_t){"origin", "https://x.com"};
    headers[count++] = (https_header_t){"x-client-transaction-id", tid};
  }
  ret = https_post(url, headers, count, body, &res);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  memset(url, 0, usize);
  memset(body, 0, sizeof body);
  log_line("conversation read: %d, HTTP %d", ret, ret ? 0 : res.status);
  if(!ret) {
    if(res.status == 200) {
      memcpy(marked, now, sizeof marked);
    }
    mem_free(res.body);
  }
  memset(now, 0, sizeof now);
}


static unsigned random_byte(void);


static int
newer_chat(const void *a, const void *b) {
  const x_post_t *x = a, *y = b;
  return x->time != y->time ? (x->time < y->time ? 1 : -1) : 0;
}


/**
 * X Chat's conversations, put among the inbox's in f, newest first: who
 * wrote and when, as their words are encrypted; nothing changes when they
 * do not come
 **/
static void
add_xchat(fetch_t *f, const x_session_t *session, const char *me, char *url, size_t usize, char *tid,
          size_t tsize) {
  static char uuid[37];
  https_header_t headers[XCHAT_HEADER_COUNT];
  unsigned char bytes[16];
  https_response_t res;
  x_request_t request;
  x_post_t *all;
  json_doc_t *doc;
  size_t off;
  int heads, count, signed_tid, ret, total;

  if(!me || !me[0] || xchat_inbox_url(url, usize)) {
    return;
  }
  if(!uuid[0]) {
    // the title's own, as a browser keeps one
    for(size_t i=0; i < sizeof bytes; i++) {
      bytes[i] = random_byte();
    }
    xchat_uuid(bytes, uuid);
  }
  signed_tid = sign(session, "GET", url, tid, tsize);
  x_request_init(&request, session);
  heads = xchat_headers(&request, uuid, signed_tid ? tid : NULL, headers);
  ret = https_get(url, headers, heads, &res);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  if(ret) {
    log_line("xchat: network 0x%08x", (unsigned)ret);
    return;
  }
  log_line("xchat%s: HTTP %d, %zu bytes", signed_tid ? " (tid)" : "", res.status, res.len);
  if(res.status != 200 || !(doc=json_parse(res.body, res.len, &off))) {
    mem_free(res.body);
    return;
  }
  count = xchat_inbox(json_root(doc), me, NULL, 0);
  count = count > MAX_POSTS - f->count ? MAX_POSTS - f->count : count;
  if(count > 0 && (all=mem_calloc(f->count + count, sizeof *all))) {
    if(f->posts && f->count) {
      memcpy(all, f->posts, f->count * sizeof *all);
    }
    count = xchat_inbox(json_root(doc), me, all + f->count, count);
    count = count < 0 ? 0 : count;
    mem_free(f->posts);
    f->posts = all;
    total = f->count + count;
    f->count = xchat_merge(all, f->count, count);
    log_line("xchat: %d in the inbox already", total - f->count);
    qsort(all, f->count, sizeof *all, newer_chat);
    if(f->count && f->error == ERR_EMPTY) {
      f->error = ERR_NONE;
    }
  }
  log_line("xchat: %d conversations", count < 0 ? 0 : count);
  json_free(doc);
  mem_free(res.body);
}


/*
 * Reading X Chat's encrypted messages: the user's identity
 * key, recovered from Juicebox with their PIN (juicebox.h), opens the
 * conversations' keys and their words (xchatopen.h, src/xchat.c's hooks).
 * The key is kept in /data, so the PIN is asked only once; the PIN itself is
 * never written anywhere, only typed and wiped. The payload has none of this.
 */
#define XCHAT_KEY_DIR "/data/TwitterPS5"
#define XCHAT_MIN_GUESSES 10 // below this, the recovery warns, so a typo cannot lock the account out
#define XCHAT_PUBLIC_KEYS 16  // the user's public keys read from X's list
#define XCHAT_PUBLIC_MAX  200 // the writing of one (the hex of an SPKI is 182)

static const char *json_find(const json_node_t *node, const char *name, int depth, char *buf, size_t size);

typedef struct jb_ctx {
  const x_session_t *session;
} jb_ctx_t;


/**
 * a Juicebox request: POST the CBOR body to a realm, with the version
 * header and, when it has one, the realm's bearer token
 **/
static int
jb_post(void *vctx, const char *url, const char *bearer, const uint8_t *body, size_t len,
        int *status, uint8_t **answer, size_t *answer_len) {
  https_header_t h[4];
  https_response_t res;
  char auth[768];
  int n = 0, ret;

  (void)vctx;
  h[n++] = (https_header_t){"content-type", "application/octet-stream"};
  h[n++] = (https_header_t){"x-juicebox-version", JB_VERSION};
  if(bearer) {
    snprintf(auth, sizeof auth, "Bearer %s", bearer);
    h[n++] = (https_header_t){"authorization", auth};
  }
  ret = https_post_data(url, h, n, body, len, &res);
  memset(auth, 0, sizeof auth);
  if(ret) {
    return -1;
  }
  *status = res.status;
  *answer = (uint8_t *)res.body;
  *answer_len = res.len;
  return 0;
}


static void
jb_release(void *vctx, uint8_t *answer) {
  (void)vctx;
  mem_free(answer);
}


/**
 * the public keys in one of X's key entries, wherever in it X puts them:
 * every string named public_key, identity_public_key or signing_public_key,
 * signing[] saying which are signing keys ("public_key" is the identity's,
 * X's SDK says, unless the entry names an identity_public_key too)
 **/
static void
collect_public_keys(const json_node_t *node, int depth, int named_identity, char keys[][XCHAT_PUBLIC_MAX],
                    int *signing, int *nkeys, int max) {
  for(const json_node_t *m=node && depth > 0 ? node->child : NULL; m && *nkeys < max; m=m->next) {
    if(m->type == JSON_OBJECT || m->type == JSON_ARRAY) {
      collect_public_keys(m, depth - 1, named_identity, keys, signing, nkeys, max);
    } else if(m->key && m->type == JSON_STRING &&
              (!strcmp(m->key, "public_key") || !strcmp(m->key, "identity_public_key") ||
               !strcmp(m->key, "signing_public_key"))) {
      const char *s = json_string(m);
      if(s && strlen(s) < XCHAT_PUBLIC_MAX) {
        snprintf(keys[*nkeys], XCHAT_PUBLIC_MAX, "%s", s);
        signing[*nkeys] = !strcmp(m->key, "signing_public_key") || (!strcmp(m->key, "public_key") && named_identity);
        (*nkeys)++;
      }
    }
  }
}


/**
 * the user's Juicebox config and the public keys X lists for them
 * (GetPublicKeysQuery, with the juicebox tokens): cfg is filled, the public
 * keys go into keys (at most max) with signing[] saying which sign, *nkeys
 * set; 0, or -1
 **/
static int
fetch_public_keys(const x_session_t *session, const char *me, jb_config_t *cfg, char keys[][XCHAT_PUBLIC_MAX],
                  int *signing, int *nkeys, int max) {
  static char url[1024], tid[XTID_HEADER_MAX];
  https_header_t headers[XCHAT_HEADER_COUNT];
  https_response_t res;
  x_request_t request;
  json_doc_t *doc;
  const json_node_t *users, *list, *e;
  size_t off;
  int heads, signed_tid, ret, have_cfg = 0;

  *nkeys = 0;
  snprintf(url, sizeof url, "https://api.x.com/graphql/C-YphxeIcPLOGtFNGhkuOw/GetPublicKeysQuery"
    "?variables=%%7B%%22ids%%22%%3A%%5B%%22%s%%22%%5D%%2C%%22include_juicebox_tokens%%22%%3Atrue%%7D", me);
  signed_tid = sign(session, "GET", url, tid, sizeof tid);
  x_request_init(&request, session);
  heads = xchat_headers(&request, NULL, signed_tid ? tid : NULL, headers);
  ret = https_get(url, headers, heads, &res);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  memset(tid, 0, sizeof tid);
  if(ret) {
    log_line("xchat keys: network 0x%08x", (unsigned)ret);
    return -1;
  }
  log_line("xchat keys: HTTP %d, %zu bytes", res.status, res.len);
  if(res.status != 200 || !(doc=json_parse(res.body, res.len, &off))) {
    mem_free(res.body);
    return -1;
  }
  users = json_path(json_root(doc), "data", "user_results_by_rest_ids", NULL);
  for(const json_node_t *u=users ? users->child : NULL; u; u=u->next) {
    list = json_path(u, "result", "get_public_keys", NULL);
    list = json_get(list, "public_keys_with_token_map");
    for(e=list ? list->child : NULL; e; e=e->next) {
      const json_node_t *map = json_get(e, "token_map");
      collect_public_keys(e, 5, json_find(e, "identity_public_key", 5, NULL, 0) != NULL, keys, signing, nkeys, max);
      if(!have_cfg && map && !jb_config_read(map, cfg)) {
        have_cfg = 1;
      }
    }
  }
  json_free(doc);
  mem_free(res.body);
  if(!have_cfg) {
    log_line("xchat keys: no Juicebox config read (%d keys)", *nkeys);
    return -1;
  }
  return 0;
}


/**
 * the unlock worker: the PIN (a->text, wiped here) recovers the identity key
 * and keeps it, so the messages open from now on. a->error says how it went,
 * a->x_code carries the guesses left after a wrong PIN
 **/
static void
send_unlock(action_t *a) {
  x_session_t session;
  jb_config_t cfg;
  jb_net_t net;
  jb_ctx_t ctx;
  jb_result_t r;
  uint8_t secret[JB_SECRET], identity[32];
  char keys[XCHAT_PUBLIC_KEYS][XCHAT_PUBLIC_MAX];
  const char *keyp[XCHAT_PUBLIC_KEYS];
  int signing[XCHAT_PUBLIC_KEYS];
  char path[XCHAT_KEY_FILE_MAX];
  xchat_identity_t seen;
  int ret, nkeys = 0, slen = 0;

  memset(&cfg, 0, sizeof cfg);
  memset(secret, 0, sizeof secret);
  memset(identity, 0, sizeof identity);
  if((ret=read_session(&session))) {
    a->error = ret == -3 ? ERR_NO_SESSION : ERR_NO_COOKIE;
    xcrypt_wipe(a->text, sizeof a->text);
    return;
  }
  if(g_https) {
    a->error = ERR_HTTPS;
    a->code = g_https;
    xcrypt_wipe(a->text, sizeof a->text);
    memset(&session, 0, sizeof session);
    return;
  }
  ctx.session = &session;
  net = (jb_net_t){jb_post, jb_release, &ctx};

  if(fetch_public_keys(&session, a->id, &cfg, keys, signing, &nkeys, XCHAT_PUBLIC_KEYS)) {
    a->error = ERR_XCHAT_NET;
    goto done;
  }
  for(int i=0; i < nkeys; i++) {
    keyp[i] = keys[i];
  }
  log_line("xchat unlock: %d realms, threshold %d, max %d guesses, %d keys", cfg.count,
           cfg.recover_threshold, cfg.max_guesses, nkeys);

  // Recover1 first: whether the keys are there at all, with no guess counted
  jb_check(&cfg, &net, &r);
  log_line("xchat unlock: check %s, %d realms", jb_code_name(r.code), r.realms_ok);
  if(r.code == JB_NOT_REGISTERED) {
    a->error = ERR_XCHAT_NONE;
    goto done;
  }
  if(r.code != JB_OK) {
    a->error = ERR_XCHAT_NET;
    a->code = r.code;
    goto done;
  }

  // the PIN (one guess at the realms): the identity private key out of it
  jb_recover(&cfg, &net, a->text, strlen(a->text), secret, &slen, &r);
  xcrypt_wipe(a->text, sizeof a->text);
  log_line("xchat unlock: recover %s, %d left, %d bytes", jb_code_name(r.code), r.guesses_left, slen);
  a->code = r.code;
  a->x_code = r.guesses_left; // the guesses left, for the wrong-PIN message
  if(r.code == JB_WRONG_PIN) {
    a->error = ERR_XCHAT_PIN;
    goto done;
  }
  if(r.code == JB_NO_GUESSES) {
    a->error = ERR_XCHAT_LOCKED;
    goto done;
  }
  if(r.code != JB_OK) {
    a->error = ERR_XCHAT_NET;
    goto done;
  }

  // the secret is the identity (and signing) private key: it must be one X
  // lists for this account, or it is not the key that opens their messages
  ret = xchat_open_identity(secret, slen, keyp, signing, nkeys, identity, &seen);
  xcrypt_wipe(secret, sizeof secret);
  log_line("xchat unlock: identity %d; a secret of %d bytes, %d tried, %d keys listed, %d read, signing only %d", ret,
           seen.secret, seen.tried, seen.keys, seen.points, seen.signing);
  if(ret != 1) {
    // sizes and counts only, for the message: what to look at next
    a->error = ERR_XCHAT_IDENTITY;
    a->code = seen.secret;
    a->x_code = seen.keys * 100 + seen.points;
    goto done;
  }
  xchat_open_set(a->id, identity);
  if(!xchat_open_path(XCHAT_KEY_DIR, a->id, path, sizeof path)) {
    if(xchat_open_save(path, identity)) {
      log_line("xchat unlock: the key did not save to %s", path);
    }
  }
  a->error = ERR_NONE;
  log_line("xchat unlock: done, %d conversation keys", xchat_open_keys());

done:
  xcrypt_wipe(a->text, sizeof a->text);
  xcrypt_wipe(secret, sizeof secret);
  xcrypt_wipe(identity, sizeof identity);
  jb_config_free(&cfg);
  memset(&session, 0, sizeof session);
}


/**
 * the identity key kept in /data for the account whose number is me, loaded
 * so the messages open at once; forgotten when me has none (or is empty)
 **/
static void
xchat_load_key(const char *me) {
  char path[XCHAT_KEY_FILE_MAX];
  uint8_t identity[32];

  if(!me || !me[0]) {
    xchat_open_forget();
    return;
  }
  if(xchat_open_ready(me)) {
    return; // already in hand
  }
  if(!xchat_open_path(XCHAT_KEY_DIR, me, path, sizeof path) && !xchat_open_load(path, identity)) {
    xchat_open_set(me, identity);
    log_line("xchat: identity for %s loaded", me);
  } else {
    xchat_open_forget();
  }
  xcrypt_wipe(identity, sizeof identity);
}


/*
 * The senders' signing keys, which check their messages' signatures:
 * asked of X with GetPublicKeysQuery, as the
 * unlock asks for the user's own, never taken from a message. Public keys,
 * but still only their count goes to the klog.
 */
#define SIGNERS_FETCH 8 // senders whose keys one conversation's page asks X for, at most

/**
 * the first member named name in node or below it (depth levels at most):
 * a string's text, or a number's digits copied into buf; NULL when there is
 * none
 **/
static const char *
json_find(const json_node_t *node, const char *name, int depth, char *buf, size_t size) {
  const char *found = NULL;

  for(const json_node_t *m=node && depth > 0 ? node->child : NULL; m && !found; m=m->next) {
    if(m->key && !strcmp(m->key, name) && m->type == JSON_STRING) {
      found = json_string(m);
    } else if(m->key && !strcmp(m->key, name) && m->type == JSON_NUMBER && buf && m->len < size) {
      memcpy(buf, m->str, m->len);
      buf[m->len] = 0;
      found = buf;
    } else if(m->type == JSON_OBJECT || m->type == JSON_ARRAY) {
      found = json_find(m, name, depth - 1, buf, size);
    }
  }
  return found;
}


/**
 * the signing keys X lists for the sender user (their number), given to
 * xchatopen.c: none when X does not answer, so they are asked for again
 * later. url and tid are the caller's buffers
 **/
static void
fetch_signer(const x_session_t *session, const char *user, char *url, size_t usize, char *tid, size_t tsize) {
  https_header_t headers[XCHAT_HEADER_COUNT];
  https_response_t res;
  x_request_t request;
  json_doc_t *doc = NULL;
  const json_node_t *users, *list;
  char versions[XCHAT_SIGNER_KEYS][32], keys[XCHAT_SIGNER_KEYS][128];
  const char *vp[XCHAT_SIGNER_KEYS], *kp[XCHAT_SIGNER_KEYS];
  size_t off, n = strlen(user);
  int heads, signed_tid, ret, count = 0;

  if(!n || n > 20 || strspn(user, "0123456789") != n) {
    return; // only a number goes into the url
  }
  n = (size_t)snprintf(url, usize, "https://api.x.com/graphql/C-YphxeIcPLOGtFNGhkuOw/GetPublicKeysQuery"
    "?variables=%%7B%%22ids%%22%%3A%%5B%%22%s%%22%%5D%%2C%%22include_juicebox_tokens%%22%%3Afalse%%7D", user);
  if(n >= usize) {
    return;
  }
  signed_tid = sign(session, "GET", url, tid, tsize);
  x_request_init(&request, session);
  heads = xchat_headers(&request, NULL, signed_tid ? tid : NULL, headers);
  ret = https_get(url, headers, heads, &res);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  memset(tid, 0, tsize);
  memset(url, 0, usize);
  if(ret) {
    log_line("xchat signing keys: network 0x%08x", (unsigned)ret);
    xchat_open_signer(user, NULL, NULL, 0, (long long)time(NULL));
    return;
  }
  if(res.status == 200) {
    doc = json_parse(res.body, res.len, &off);
  }
  users = doc ? json_path(json_root(doc), "data", "user_results_by_rest_ids", NULL) : NULL;
  for(const json_node_t *u=users ? users->child : NULL; u; u=u->next) {
    list = json_get(json_path(u, "result", "get_public_keys", NULL), "public_keys_with_token_map");
    for(const json_node_t *e=list ? list->child : NULL; e && count < XCHAT_SIGNER_KEYS; e=e->next) {
      // each of the user's keys: its signing half and its version, wherever
      // in the entry X puts them
      const char *key = json_find(e, "signing_public_key", 4, NULL, 0);
      const char *version = json_find(e, "public_key_version", 4, versions[count], sizeof versions[0]);
      if(key && strlen(key) < sizeof keys[0]) {
        snprintf(keys[count], sizeof keys[0], "%s", key);
        if(version != versions[count]) {
          snprintf(versions[count], sizeof versions[0], "%s", version ? version : "");
        }
        vp[count] = versions[count];
        kp[count] = keys[count];
        count++;
      }
    }
  }
  log_line("xchat signing keys: HTTP %d, %d", res.status, count);
  xchat_open_signer(user, vp, kp, count, (long long)time(NULL));
  if(doc) {
    json_free(doc);
  }
  mem_free(res.body);
}


/**
 * the messages of a conversation on X Chat (UI_DM with req.xchat), read from
 * its page and opened with the account's key when it is in hand (else they
 * stay sealed); oldest first, X_KIND_DM. Each one's signature is checked with
 * its sender's keys as X lists them: those not in hand yet are asked for, and
 * the page read again
 **/
static void
fetch_xchat(fetch_t *f, const x_session_t *session, char *url, size_t usize, char *tid, size_t tsize) {
  static char uuid[37];
  https_header_t headers[XCHAT_HEADER_COUNT];
  https_response_t res;
  x_request_t request;
  json_doc_t *doc;
  const char *me = g_settings.ids[g_account - 1];
  size_t off;
  int heads, signed_tid, ret, total, n;

  if(xchat_conversation_url(url, usize, f->req.id, NULL)) {
    f->error = ERR_MEMORY;
    return;
  }
  if(!uuid[0]) {
    unsigned char bytes[16];
    for(size_t i=0; i < sizeof bytes; i++) {
      bytes[i] = (unsigned char)random_byte();
    }
    xchat_uuid(bytes, uuid);
  }
  signed_tid = sign(session, "GET", url, tid, tsize);
  x_request_init(&request, session);
  heads = xchat_headers(&request, uuid, signed_tid ? tid : NULL, headers);
  ret = https_get(url, headers, heads, &res);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  if(ret) {
    f->error = ERR_NETWORK;
    f->code = ret;
    log_line("xchat page: network 0x%08x", (unsigned)ret);
    return;
  }
  log_line("xchat page%s: HTTP %d, %zu bytes", signed_tid ? " (tid)" : "", res.status, res.len);
  if(res.status != 200 || !(doc=json_parse(res.body, res.len, &off))) {
    f->error = res.status == 200 ? ERR_ANSWER : ERR_HTTP;
    f->code = res.status;
    mem_free(res.body);
    return;
  }
  total = xchat_messages(json_root(doc), f->req.id, me, NULL, 0);
  total = total < 1 ? 1 : total > MAX_POSTS ? MAX_POSTS : total;
  if(!(f->posts=mem_calloc(total, sizeof *f->posts))) {
    f->error = ERR_MEMORY;
  } else if((n=xchat_messages(json_root(doc), f->req.id, me, f->posts, total)) <= 0) {
    f->error = ERR_EMPTY;
    mem_free(f->posts);
    f->posts = NULL;
  } else {
    char users[SIGNERS_FETCH][24];
    const int want = xchat_open_wanted((long long)time(NULL), users, SIGNERS_FETCH);
    f->count = n;
    // the senders whose signing keys are not in hand yet: asked of X, and
    // the page read again so their messages' signatures are checked
    for(int i=0; i < want; i++) {
      fetch_signer(session, users[i], url, usize, tid, tsize);
    }
    if(want > 0 && (n=xchat_messages(json_root(doc), f->req.id, me, f->posts, total)) > 0) {
      f->count = n;
    }
  }
  log_line("xchat page: %d messages, %d keys in hand", f->count, xchat_open_keys());
  json_free(doc);
  mem_free(res.body);
}


static int
older_first(const void *a, const void *b) {
  const x_post_t *x = a, *y = b;
  return x->time != y->time ? (x->time < y->time ? -1 : 1) : x->xchat - y->xchat;
}


/**
 * a conversation of the inbox that goes on on X Chat (req.xchat_id, its
 * number there): the messages of its X Chat page join the ones read, oldest
 * first, the newest MAX_POSTS of them; opened when the keys are in hand,
 * else sealed (✕ on one asks for the PIN)
 **/
static void
join_xchat(fetch_t *f, const x_session_t *session, char *url, size_t usize, char *tid, size_t tsize) {
  fetch_t *x = mem_calloc(1, sizeof *x);
  x_post_t *all;
  int n;

  if(!x) {
    return;
  }
  x->req = f->req;
  snprintf(x->req.id, sizeof x->req.id, "%s", f->req.xchat_id);
  x->req.xchat = 1;
  fetch_xchat(x, session, url, usize, tid, tsize);
  memset(url, 0, usize);
  if(x->count > 0 && (all=mem_alloc((size_t)(f->count + x->count) * sizeof *all))) {
    if(f->count) {
      memcpy(all, f->posts, (size_t)f->count * sizeof *all);
    }
    memcpy(all + f->count, x->posts, (size_t)x->count * sizeof *all);
    n = f->count + x->count;
    qsort(all, (size_t)n, sizeof *all, older_first);
    if(n > MAX_POSTS) {
      memmove(all, all + n - MAX_POSTS, MAX_POSTS * sizeof *all);
      n = MAX_POSTS;
    }
    mem_free(f->posts);
    f->posts = all;
    f->count = n;
    f->error = ERR_NONE; // X Chat answered, whatever the inbox's page did
    f->code = 0;
  }
  log_line("dm: %d of X Chat's joined", x->count);
  mem_free(x->posts);
  mem_free(x);
}


/**
 * the user's conversations (UI_CHAT, the user's number in f's id) or the
 * messages of one (UI_DM, its number in f's id), as x.com's Messages reads
 * them; only their count goes to the klog
 **/
static void
fetch_dm(fetch_t *f, const x_session_t *session, char *url, size_t usize, char *tid, size_t tsize) {
  int inbox = f->req.feed == UI_CHAT, total, signed_tid;
  const char *me;
  https_response_t res;
  json_doc_t *doc;

  if(!inbox && f->req.xchat) {
    // a conversation on X Chat: its own page, encrypted
    fetch_xchat(f, session, url, usize, tid, tsize);
    memset(url, 0, usize);
    return;
  }
  if(inbox ? x_dm_inbox_url(url, usize) : x_dm_conversation_url(url, usize, f->req.id)) {
    f->error = ERR_MEMORY;
    return;
  }
  signed_tid = sign(session, "GET", url, tid, tsize);
  doc = get_json(f, url, session, &res, feed_name(f->req.feed), signed_tid ? tid : NULL);
  memset(url, 0, usize); // it has the conversation's number
  if(signed_tid && f->code == 404) {
    forget_tid(); // a new page's key next time
  }
  if(!doc) {
    if(!inbox && f->req.xchat_id[0]) {
      join_xchat(f, session, url, usize, tid, tsize); // it may go on on X Chat only now
    }
    return;
  }
  me = f->req.id[0] ? f->req.id : NULL;
  total = inbox ? x_dm_inbox(json_root(doc), me, NULL, 0) : x_dm_messages(json_root(doc), f->req.id, NULL, 0);
  total = total < 1 ? 1 : total > MAX_POSTS ? MAX_POSTS : total;
  if(!(f->posts=mem_calloc(total, sizeof *f->posts))) {
    f->error = ERR_MEMORY;
    log_line("no memory for %d messages", total);
  } else if(!(f->count=inbox ? x_dm_inbox(json_root(doc), me, f->posts, total) :
              x_dm_messages(json_root(doc), f->req.id, f->posts, total))) {
    f->error = ERR_EMPTY;
    mem_free(f->posts);
    f->posts = NULL;
  }
  if(!inbox) {
    // who is in it, with what their profiles say they play: a group's side card
    f->party_count = x_dm_members(json_root(doc), f->req.id, NULL, f->party, UI_PARTY_MAX);
  }
  log_line("%s: %d, %d people", feed_name(f->req.feed), f->count, f->party_count);
  json_free(doc);
  mem_free(res.body);
  if(inbox) {
    // the conversations started on X Chat are not among them
    add_xchat(f, session, me, url, usize, tid, tsize);
  }
  if(!inbox && f->count) {
    mark_read(session, f->req.id, f->posts[f->count - 1].id, url, usize, tid, tsize);
  }
  if(!inbox && f->req.xchat_id[0]) {
    join_xchat(f, session, url, usize, tid, tsize);
  }
}


/**
 * the user's scheduled posts, as x.com's unsent posts read them; none is an
 * answer too (ERR_EMPTY)
 **/
static void
fetch_scheduled(fetch_t *f, x_session_t *session, char *url, size_t usize) {
  static char tid[XTID_HEADER_MAX];
  https_response_t res;
  json_doc_t *doc;
  int total = 0, signed_tid;

  if(x_scheduled_url(url, usize)) {
    f->error = ERR_MEMORY;
    return;
  }
  // with the header, as x.com's unsent posts read them
  signed_tid = sign(session, "GET", url, tid, sizeof tid);
  doc = get_json(f, url, session, &res, "scheduled posts", signed_tid ? tid : NULL);
  if(signed_tid && f->code == 404) {
    forget_tid();
  }
  memset(tid, 0, sizeof tid);
  if(!doc) {
    return;
  }
  x_scheduled_posts(json_root(doc), NULL, 0, &total);
  total = total < 1 ? 1 : total > 100 ? 100 : total;
  if(!(f->posts=mem_calloc(total, sizeof *f->posts))) {
    f->error = ERR_MEMORY;
  } else if(!(f->count=x_scheduled_posts(json_root(doc), f->posts, total, &total))) {
    f->error = ERR_EMPTY;
    mem_free(f->posts);
    f->posts = NULL;
  }
  log_line("scheduled posts: %d", f->count);
  json_free(doc);
  mem_free(res.body);
}


/**
 * the notifications of every account with a session, in one list newest
 * first, each marked with its account (Notifications' second tab): the
 * accounts' own lists merged, so each keeps its order. An account whose
 * reading fails is left out; only when none reads is its failure the list's.
 * The klog has the counts, not whose accounts
 **/
static void
fetch_all_notices(fetch_t *f, char *url, size_t size) {
  enum { EACH = 40 };
  x_post_t *all = NULL, *one, *merged;
  https_response_t res;
  x_session_t session;
  json_doc_t *doc;
  int count = 0, got, total, accounts = 0, error = ERR_NO_SESSION, code = -3, x_code = -1, i, j, k;

  for(int a=1; a <= SETTINGS_ACCOUNTS && count < MAX_POSTS; a++) {
    if(read_session_of(a, &session)) {
      continue; // none linked there
    }
    if(x_feed_url(url, size, X_FEED_NOTIFICATIONS, NULL, EACH, NULL)) {
      memset(&session, 0, sizeof session);
      error = ERR_MEMORY;
      break;
    }
    f->error = ERR_NONE;
    f->code = 0;
    doc = get_json(f, url, &session, &res, "notifications of an account", NULL);
    memset(&session, 0, sizeof session);
    if(!doc) {
      error = f->error;
      code = f->code;
      x_code = f->x_code;
      continue;
    }
    total = 0;
    x_timeline_posts(json_root(doc), NULL, 0, &total);
    total = total < 1 ? 1 : total > EACH * 2 ? EACH * 2 : total;
    one = mem_calloc(total, sizeof *one);
    got = one ? x_timeline_posts(json_root(doc), one, total, &total) : 0;
    json_free(doc);
    mem_free(res.body);
    if(!one) {
      error = ERR_MEMORY;
      break;
    }
    accounts++;
    // into what the accounts before had, by time, the earlier ones first on a tie
    if(got && !(merged=mem_calloc((size_t)(count + got), sizeof *merged))) {
      mem_free(one);
      error = ERR_MEMORY;
      break;
    }
    for(i=j=k=0; got && (i < count || j < got); k++) {
      if(j >= got || (i < count && all[i].time >= one[j].time)) {
        merged[k] = all[i++];
      } else {
        merged[k] = one[j++];
        merged[k].account = a;
      }
    }
    if(got) {
      mem_free(all);
      all = merged;
      count += got;
    }
    mem_free(one);
  }
  memset(url, 0, size);
  if(count > MAX_POSTS) {
    count = MAX_POSTS; // the oldest go
  }
  if(!count) {
    mem_free(all);
    f->error = accounts ? ERR_EMPTY : error;
    f->code = accounts ? 200 : code;
    f->x_code = accounts ? -1 : x_code;
    log_line("notifications of every account: none (%d read)", accounts);
    return;
  }
  f->error = ERR_NONE;
  f->code = 200;
  f->x_code = -1;
  f->posts = all;
  f->count = count;
  log_line("notifications of every account: %d accounts, %d posts", accounts, count);
}


static void
fetch_timeline(fetch_t *f) {
  static char url[8192], tid[XTID_HEADER_MAX];
  // the pages kept by the screen read nothing
  static const int feeds[UI_FEEDS] = {
    [UI_FOR_YOU] = X_FEED_FOR_YOU, [UI_FOLLOWING] = X_FEED_FOLLOWING, [UI_SEARCH] = X_FEED_SEARCH,
    [UI_NOTIFICATIONS] = X_FEED_NOTIFICATIONS, [UI_BOOKMARKS] = X_FEED_BOOKMARKS, [UI_ME] = X_FEED_USER,
    [UI_CHAT] = -1, [UI_CALLS] = -1, [UI_GROK] = -1, [UI_PREMIUM] = -1, [UI_HISTORY] = -1,
    [UI_ARTICLES] = X_FEED_ARTICLES, [UI_USER] = X_FEED_USER, [UI_POST] = X_FEED_POST, [UI_DM] = -1,
    [UI_LISTS] = X_FEED_LISTS, [UI_LIST] = X_FEED_LIST, [UI_GAME] = -1, [UI_FRIENDS] = X_FEED_FRIENDS,
  };
  const char *id = f->req.id;
  https_response_t res;
  x_session_t session;
  json_doc_t *doc;
  int ret, total = 0, feed, signed_tid = 0;
  int search = (f->req.feed == UI_SEARCH || f->req.feed == UI_GAME) && f->req.query[0];
  int explore = f->req.feed == UI_SEARCH && !f->req.query[0];

  if((ret=read_session(&session))) {
    f->error = ret == -3 ? ERR_NO_SESSION : ERR_NO_COOKIE;
    f->code = ret;
    log_line("session: %d", ret);
    return;
  }
  if(g_https) {
    memset(&session, 0, sizeof session);
    f->error = ERR_HTTPS;
    f->code = g_https;
    return;
  }
  if(f->req.feed == UI_NOTIFICATIONS && f->req.latest && !f->req.side && !f->req.cursor[0]) {
    memset(&session, 0, sizeof session);
    fetch_all_notices(f, url, sizeof url); // every account's, in one
    return;
  }
  if(f->req.feed == UI_CHAT || f->req.feed == UI_DM) {
    fetch_dm(f, &session, url, sizeof url, tid, sizeof tid);
    memset(&session, 0, sizeof session);
    return;
  }
  // a profile on top of its posts; the signed-in user's gives its number
  if(f->req.profile && fetch_profile(f, &session, url, sizeof url)) {
    memset(&session, 0, sizeof session);
    return;
  }
  if(f->req.side == UI_SIDE_SCHEDULED) {
    fetch_scheduled(f, &session, url, sizeof url);
    memset(&session, 0, sizeof session);
    return;
  }
  if(f->has_user) {
    id = f->user.id;
  }
  feed = f->req.feed >= 0 && f->req.feed < UI_FEEDS ? feeds[f->req.feed] : -1;
  if(search) {
    // the game's tab, its latest posts
    feed = f->req.latest || f->req.feed == UI_GAME ? X_FEED_SEARCH_LATEST : X_FEED_SEARCH;
    id = f->req.query;
  } else if(explore) {
    feed = f->req.latest ? X_FEED_NEWS : X_FEED_TRENDING; // Explore's tabs
  }
  // the side column's: x.com's own side column of trends, or who to follow
  if(f->req.side == UI_SIDE_TRENDS) {
    feed = X_FEED_SIDEBAR;
  } else if(f->req.side == UI_SIDE_WHO) {
    feed = X_FEED_WHO;
  } else if(f->req.side == UI_SIDE_PLAYING) {
    feed = X_FEED_FRIENDS; // the newest of the people the user follows
  }
  if(feed < 0 || x_feed_url(url, sizeof url, feed, id[0] ? id : NULL,
                           f->req.side == UI_SIDE_WHO ? 6 :
                           f->req.side == UI_SIDE_PLAYING || f->req.feed == UI_FRIENDS ? 100 : TIMELINE_COUNT,
                           f->req.cursor)) {
    memset(&session, 0, sizeof session);
    f->error = ERR_MEMORY;
    return;
  }

  // a search and Explore carry the header, as x.com's do; the timelines go without
  signed_tid = (search || explore || f->req.side || f->req.feed == UI_FRIENDS) &&
               sign(&session, "GET", url, tid, sizeof tid);
  doc = get_json(f, url, &session, &res, f->req.side == UI_SIDE_WHO ? "who to follow" :
                 f->req.side == UI_SIDE_PLAYING ? "playing now" :
                 f->req.side ? "side trends" : feed_name(f->req.feed), signed_tid ? tid : NULL);
  memset(&session, 0, sizeof session);
  memset(url, 0, sizeof url); // it has the words searched for
  if(signed_tid && f->code == 404) {
    forget_tid(); // a new page's key next time
  }
  if(!doc) {
    return;
  }

  // as many posts as the answer has, counted first
  x_timeline_posts(json_root(doc), NULL, 0, &total);
  total = total < 1 ? 1 : total > MAX_POSTS ? MAX_POSTS : total;
  if(!(f->posts=mem_calloc(total, sizeof *f->posts))) {
    f->error = ERR_MEMORY;
    log_line("no memory for %d posts", total);
  } else if(!(f->count=x_timeline_posts(json_root(doc), f->posts, total, &total))) {
    f->error = ERR_EMPTY;
    mem_free(f->posts);
    f->posts = NULL;
  } else {
    x_timeline_cursor(json_root(doc), f->next, sizeof f->next);
    // a look at the people the user follows: the file it is noted into, as
    // it is now (the payload notes its looks into it too)
    if(f->req.side == UI_SIDE_PLAYING || (f->req.feed == UI_FRIENDS && !f->req.cursor[0])) {
      f->seen_ok = !seen_read(g_account, &f->seen);
    }
  }
  log_line("%s%s: %d posts, kept %d%s", f->req.side == UI_SIDE_WHO ? "who to follow" :
           f->req.side == UI_SIDE_PLAYING ? "playing now" :
           f->req.side ? "side trends" : feed_name(f->req.feed), f->req.cursor[0] ? " (more)" : "",
           total, f->count, f->next[0] ? ", more after" : "");
  json_free(doc);
  mem_free(res.body);
}


static void *
fetch_main(void *arg) {
  fetch_t *f = arg;

  fetch_timeline(f);
  __atomic_store_n(&f->state, FETCH_DONE, __ATOMIC_RELEASE);
  return NULL;
}


static void
start_fetch(const ui_request_t *req) {
  memset(&g_fetch, 0, sizeof g_fetch);
  g_fetch.state = FETCH_RUNNING;
  g_fetch.req = *req;
  // the TLS handshake and the JSON walk want more than a small default stack
  if(!start_thread(fetch_main, &g_fetch, 1 << 20)) {
    g_fetch.error = ERR_MEMORY;
    g_fetch.state = FETCH_DONE;
  }
}


/*
 * The x-client-transaction-id header (see xtid.h), worked out from a page
 * of x.com and the ondemand.s chunk it names. X refuses a post without it.
 * The action thread and the fetch thread (for a search) both sign, one at
 * a time under g_tid_lock.
 */

#define TID_MAX_AGE (60 * 60) // seconds a page's key is used for
#define TID_RETRY   60        // seconds after a page that gave none before the next

static pthread_mutex_t g_tid_lock;
static xtid_t          g_tid;
static long long       g_tid_at;     // when g_tid was read; 0 for never, or after X refused it
static long long       g_tid_failed; // when reading a page last gave no key


/**
 * read a page of x.com with the user's session (the signed-in home page
 * names the chunk; the bare one is the fallback), then the chunk; returns
 * 0 or -1
 **/
static int
load_tid(const x_session_t *session) {
  return xsign_load(&g_tid, session, log_line);
}


/**
 * the byte the header is masked with
 **/
static unsigned
random_byte(void) {
  static unsigned state;

  if(!state) {
    state = (unsigned)time(NULL) * 2654435761u | 1;
  }
  // xorshift32
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state >> 24;
}


/**
 * the header of a request of method to url, from the path without its
 * query; a page of x.com is read first when there is no key yet, or an
 * old one; returns whether tid holds the header
 **/
static int
sign(const x_session_t *session, const char *method, const char *url, char *tid, size_t size) {
  const char *path = strchr(url + strlen("https://"), '/');
  long long now = (long long)time(NULL);
  char buf[256];
  size_t n;
  int ok;

  if(!path || (n=strcspn(path, "?")) >= sizeof buf) {
    return 0;
  }
  memcpy(buf, path, n);
  buf[n] = 0;
  pthread_mutex_lock(&g_tid_lock);
  if((!g_tid_at || now - g_tid_at > TID_MAX_AGE) && now - g_tid_failed >= TID_RETRY) {
    if(load_tid(session)) {
      g_tid_at = 0;
      g_tid_failed = now;
    } else {
      g_tid_at = now;
    }
  }
  ok = g_tid_at && !xtid_make(&g_tid, method, buf, now, random_byte(), tid, size);
  pthread_mutex_unlock(&g_tid_lock);
  return ok;
}


/**
 * after X refused a header: the next one comes from a new page
 **/
static void
forget_tid(void) {
  pthread_mutex_lock(&g_tid_lock);
  g_tid_at = 0;
  pthread_mutex_unlock(&g_tid_lock);
}



/*
 * The captures a post carries, uploaded to upload.x.com in the steps x.com's
 * composer takes: INIT with the size and the type, APPEND in parts of up to
 * 4 MB, FINALIZE, then STATUS while X processes a video. Neither a media's
 * number nor a capture's path goes to the klog.
 */

static int       kept_kind(const char *path);
static long long capture_read(const char *path, long long off, void *buf, size_t len,
                              long long *total);
static long long capture_load(const char *path, void *buf, long long len);


/**
 * the headers of an upload: the session's, the body's content-type when it
 * has one, and the origin x.com's composer sends; returns how many
 **/
static int
upload_headers(https_header_t *out, const x_request_t *r, const char *type) {
  int n = 0;

  for(int i=0; i < X_HEADER_COUNT; i++) {
    if(strcmp(r->headers[i].name, "content-type")) {
      out[n++] = r->headers[i];
    }
  }
  if(type) {
    out[n++] = (https_header_t){"content-type", type};
  }
  out[n++] = (https_header_t){"origin", "https://x.com"};
  return n;
}


/**
 * a step without a body (INIT and FINALIZE are POSTs, STATUS a GET) and its
 * JSON answer; returns the document, then res's body to free after it, or
 * NULL with a's error set
 **/
static json_doc_t *
upload_step(action_t *a, const x_session_t *session, const char *url, int get, const char *what,
            https_response_t *res) {
  https_header_t headers[X_HEADER_COUNT + 2];
  x_request_t request;
  const char *message;
  json_doc_t *doc;
  size_t err_off;
  int ret, why, count;

  x_request_init(&request, session);
  count = upload_headers(headers, &request, NULL);
  ret = get ? https_get(url, headers, count, res) : https_post_data(url, headers, count, "", 0, res);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  if(ret) {
    a->error = ERR_NETWORK;
    a->code = ret;
    return NULL;
  }
  doc = json_parse_why(res->body, res->len, &err_off, &why);
  a->x_code = doc ? x_first_error(json_root(doc), &message) : -1;
  log_line("upload %s: HTTP %d, %zu bytes, X says %d", what, res->status, res->len, a->x_code);
  if(res->status / 100 != 2 || !doc) {
    a->error = res->status / 100 == 2 ? ERR_ANSWER : ERR_UPLOAD;
    a->code = res->status;
    if(doc) {
      json_free(doc);
    }
    mem_free(res->body);
    res->body = NULL;
    return NULL;
  }
  return doc;
}


/**
 * a screenshot the PS5 saved as JPEG XR (file, which it frees) as a JPEG X
 * takes: at its whole size and HDR_QUALITY first, less of either until it
 * fits in 5 MB. An HDR one with light over SDR's white goes as Ultra HDR,
 * its gain map behind the SDR picture (a->ultra_hdr set)
 **/
static uint8_t *
hdr_photo(action_t *a, uint8_t *file, size_t flen, size_t *len, const char **type) {
  unsigned long start = (unsigned long)sceKernelGetProcessTime();
  int w = 0, h = 0, format = 0, error = 0, quality = HDR_QUALITY, sw, sh, gw = 0, gh = 0;
  float gain_max = 0;
  uint8_t *gain = NULL, *gain_jpeg = NULL, *jpeg = NULL, *joined;
  uint32_t *pixels = jxr_decode_hdr(file, flen, &w, &h, &format, &gain, &gw, &gh, &gain_max, &error), *smaller;
  size_t jlen = 0, glen = 0, room = X_UPLOAD_PHOTO, total;

  mem_free(file);
  if(!pixels) {
    log_line("upload: a .jxr screenshot not read, error %d, pixel format 0x%x", error, format);
    a->error = error == -2 ? ERR_MEMORY : ERR_CAPTURE;
    a->code = error;
    return NULL;
  }
  if(gain) {
    if(jpeg_encode_gray(gain, gw, gh, GAIN_QUALITY, &gain_jpeg, &glen) ||
       glen + ULTRAHDR_ROOM > X_UPLOAD_PHOTO / 4) {
      mem_free(gain_jpeg);
      gain_jpeg = NULL;
    } else {
      room = X_UPLOAD_PHOTO - glen - ULTRAHDR_ROOM; // the SDR picture gets the rest
    }
    mem_free(gain);
  }
  for(int tries=0; tries < 6 && !jpeg; tries++) {
    if(jpeg_encode(pixels, w, h, quality, &jpeg, &jlen)) {
      jpeg = NULL;
      break;
    }
    if(jlen <= room) {
      break;
    }
    mem_free(jpeg);
    jpeg = NULL;
    if(quality > 80) {
      quality -= 6;
      continue;
    }
    sw = w * 3 / 4;
    sh = h * 3 / 4;
    if(!(smaller=image_fit_pixels(pixels, w, h, sw, sh))) {
      break;
    }
    mem_free(pixels);
    pixels = smaller;
    w = sw;
    h = sh;
  }
  mem_free(pixels);
  log_line("upload: a .jxr screenshot (pixel format 0x%x, %s) made a %dx%d JPEG at %d, %zu bytes, in %lu ms",
           format, jxr_format_hdr(format) ? "HDR" : "SDR", w, h, quality, jpeg ? jlen : 0,
           ((unsigned long)sceKernelGetProcessTime() - start) / 1000);
  if(jpeg && gain_jpeg) {
    if(!ultrahdr_join(jpeg, jlen, gain_jpeg, glen, gain_max, &joined, &total)) {
      mem_free(jpeg);
      jpeg = joined;
      jlen = total;
      a->ultra_hdr = total;
      log_line("upload: Ultra HDR, a gain map of %dx%d (%zu bytes) up to %.2f stops, %zu bytes in all", gw, gh,
               glen, (double)gain_max, total);
    } else {
      log_line("upload: Ultra HDR not made, the SDR picture goes alone");
    }
  } else if(jpeg && jxr_format_hdr(format)) {
    log_line("upload: no gain map (%s)", gw ? "it did not fit" : "no light over SDR's white");
  }
  mem_free(gain_jpeg);
  if(!jpeg) {
    a->error = ERR_TOO_BIG;
    return NULL;
  }
  *len = jlen;
  *type = "image/jpeg";
  return jpeg;
}


/**
 * a picture as X takes it: the capture as it is, when it is up to 5 MB,
 * else made smaller, as a JPEG; *type is its content-type. Returns the bytes
 * (mem_free them), or NULL with a's error set
 **/
static uint8_t *
photo_bytes(action_t *a, const capture_t *c, size_t *len, const char **type) {
  int w, h, side = PHOTO_SIDE, quality = PHOTO_QUALITY;
  long long total = 0, n;
  uint8_t *file, *jpeg = NULL;
  uint32_t *pixels;
  size_t jlen = 0;

  if((n=capture_read(c->path, 0, NULL, 0, &total)) < 0 || total <= 0) {
    a->error = ERR_CAPTURE;
    a->code = (int)n;
    return NULL;
  }
  if(total > CAPTURE_PHOTO_MAX) {
    a->error = ERR_TOO_BIG;
    return NULL;
  }
  if(!(file=mem_alloc((size_t)total))) {
    a->error = ERR_MEMORY;
    return NULL;
  }
  if((n=capture_load(c->path, file, total)) != total) {
    mem_free(file);
    a->error = ERR_CAPTURE;
    a->code = (int)(n < 0 ? n : -1);
    return NULL;
  }
  if(c->kind == CAPTURE_JXR) {
    return hdr_photo(a, file, (size_t)total, len, type);
  }
  if(total <= X_UPLOAD_PHOTO) {
    *len = (size_t)total;
    *type = c->kind == CAPTURE_PNG ? "image/png" : "image/jpeg";
    return file;
  }

  // smaller and smaller, until it fits
  for(int tries=0; tries < 3 && !jpeg; tries++) {
    if(!(pixels=image_scaled(file, (size_t)total, side, &w, &h))) {
      break;
    }
    if(jpeg_encode(pixels, w, h, quality, &jpeg, &jlen)) {
      jpeg = NULL;
    } else if(jlen > X_UPLOAD_PHOTO) {
      mem_free(jpeg);
      jpeg = NULL;
    }
    mem_free(pixels);
    log_line("upload: picture made %dx%d, %s", w, h, jpeg ? "fits" : "too big");
    side = side * 3 / 4;
    quality -= 10;
  }
  mem_free(file);
  if(!jpeg) {
    a->error = ERR_TOO_BIG;
    return NULL;
  }
  *len = jlen;
  *type = "image/jpeg";
  return jpeg;
}


/**
 * the part c->clip of the video c, total bytes long, cut into clip
 * (clip.h): its moov found by the top-level boxes' headers alone, then read
 * whole; 0, or -1 with a's error set
 **/
static int
clip_capture(action_t *a, const capture_t *c, long long total, clip_t *clip) {
  uint8_t box[16], *moov;
  uint64_t at = 0, size = 0, moov_at = 0, moov_size = 0;
  unsigned head_len;
  uint32_t type;
  long long n;
  int64_t from, to;
  mp4_t m;
  int ret;

  memset(clip, 0, sizeof *clip);
  while(at + 8 <= (uint64_t)total) {
    size_t want = (uint64_t)total - at < sizeof box ? (size_t)((uint64_t)total - at) : sizeof box;
    if((n=capture_read(c->path, (long long)at, box, want, NULL)) < 8 ||
       clip_box(box, (size_t)n, at, (uint64_t)total, &type, &size, &head_len)) {
      break;
    }
    if(type == CLIP_FOURCC('m', 'o', 'o', 'v')) {
      moov_at = at;
      moov_size = size;
      break;
    }
    at += size;
  }
  if(!moov_size || moov_size > CLIP_MOOV_MOST) {
    log_line("upload: the video has no moov to cut it by (%llu bytes)", (unsigned long long)moov_size);
    a->error = ERR_CLIP;
    a->code = MP4_NO_MOOV;
    return -1;
  }
  if(!(moov=mem_alloc((size_t)moov_size))) {
    a->error = ERR_MEMORY;
    return -1;
  }
  for(uint64_t got=0; got < moov_size; got += (uint64_t)n) {
    size_t want = moov_size - got < X_UPLOAD_PART ? (size_t)(moov_size - got) : X_UPLOAD_PART;
    if((n=capture_read(c->path, (long long)(moov_at + got), moov + got, want, NULL)) <= 0) {
      mem_free(moov);
      a->error = ERR_CAPTURE;
      a->code = n < 0 ? (int)n : -1;
      return -1;
    }
  }
  if(!(ret=mp4_parse_moov(&m, moov, (size_t)moov_size, (uint64_t)total))) {
    clip_span(c->clip, m.duration_ms, &from, &to);
    // the codes of clip.h past mp4.h's
    ret = clip_make(&m, from, to, clip) - 10 * (clip->head == NULL);
    log_line("upload: the video's part %d: %lld ms from %lld ms of %lld, %s", c->clip, (long long)clip->length_ms,
             (long long)clip->start_ms, (long long)m.duration_ms, ret ? "not cut" : "cut");
    mp4_free(&m);
  } else {
    log_line("upload: the video's moov not read, %d", ret);
  }
  mem_free(moov);
  if(ret) {
    clip_free(clip);
    a->error = ret == MP4_MEMORY || ret == CLIP_MEMORY - 10 ? ERR_MEMORY : ERR_CLIP;
    a->code = ret;
    return -1;
  }
  return 0;
}


/**
 * len bytes at off of the video that goes: the capture's, or its clip's
 * (the head, then the capture's range); how many, or a negative error
 **/
static long long
video_read(const capture_t *c, const clip_t *clip, long long off, uint8_t *buf, size_t len) {
  size_t done = 0, k;
  long long n;

  if(!clip) {
    return capture_read(c->path, off, buf, len, NULL);
  }
  if(off < 0 || (uint64_t)off + len > clip_size(clip)) {
    return -1;
  }
  if((uint64_t)off < clip->head_len) {
    k = clip->head_len - (size_t)off < len ? clip->head_len - (size_t)off : len;
    memcpy(buf, clip->head + off, k);
    done = k;
  }
  while(done < len) {
    n = capture_read(c->path, (long long)clip->from + off + (long long)done - (long long)clip->head_len, buf + done,
                     len - done, NULL);
    if(n <= 0) {
      return n < 0 ? n : -1;
    }
    done += (size_t)n;
  }
  return (long long)done;
}


/**
 * upload one capture, whose size counts from base in a's progress; on 0,
 * media_id holds the number X gave it, else a's error is set
 **/
static int
upload_media(action_t *a, const x_session_t *session, const capture_t *c, long long base,
             char *media_id, size_t size) {
  static char url[512];
  char boundary[40], head[256], tail[96], form[128];
  long long total = 0, off, n, weight = c->size > 0 ? c->size : 1;
  int ret, wait = 1, state = -1, last = -1, segment = 0, count, waited = 0, cut = 0;
  https_header_t headers[X_HEADER_COUNT + 2];
  const char *type = "video/mp4";
  uint8_t *photo = NULL, *part = NULL;
  size_t plen = 0, hlen, tlen;
  https_response_t res;
  x_request_t request;
  json_doc_t *doc;
  clip_t clip;

  memset(media_id, 0, size);
  memset(&clip, 0, sizeof clip);
  if(c->kind == CAPTURE_WEBM) {
    a->error = ERR_PROCESSING; // X takes no WebM, which the composer tells first
    return -1;
  }
  if(c->kind == CAPTURE_MP4) {
    if((n=capture_read(c->path, 0, NULL, 0, &total)) < 0 || total <= 0) {
      a->error = ERR_CAPTURE;
      a->code = (int)n;
      return -1;
    }
    // a part of it: a head made here, then a range of its bytes
    if(c->clip > CLIP_WHOLE && c->clip < CLIP_KINDS) {
      if(clip_capture(a, c, total, &clip)) {
        return -1;
      }
      cut = 1;
      total = (long long)clip_size(&clip);
    }
    if(total > X_UPLOAD_VIDEO) {
      clip_free(&clip);
      a->error = ERR_TOO_BIG;
      return -1;
    }
  } else if(!(photo=photo_bytes(a, c, &plen, &type))) {
    return -1;
  } else {
    total = (long long)plen;
  }

  // INIT
  if(x_upload_url(url, sizeof url, X_UPLOAD_INIT, NULL, total, type,
                  a->kind == UI_ACT_SHARE_DM || a->kind == UI_ACT_VOICE_DM ? X_UPLOAD_FOR_DM : 0)) {
    a->error = ERR_MEMORY;
    goto fail;
  }
  if(!(doc=upload_step(a, session, url, 0, "init", &res))) {
    goto fail;
  }
  ret = x_upload_id(json_root(doc), media_id, size);
  json_free(doc);
  mem_free(res.body);
  if(ret) {
    a->error = ERR_ANSWER;
    goto fail;
  }

  // APPEND, in parts
  snprintf(boundary, sizeof boundary, "----TwitterPS5%02x%02x%02x%02x%02x%02x%02x%02x",
           random_byte(), random_byte(), random_byte(), random_byte(), random_byte(),
           random_byte(), random_byte(), random_byte());
  if(x_upload_form(boundary, head, sizeof head, tail, sizeof tail, form, sizeof form) ||
     !(part=mem_alloc(sizeof head + X_UPLOAD_PART + sizeof tail))) {
    a->error = ERR_MEMORY;
    goto fail;
  }
  hlen = strlen(head);
  tlen = strlen(tail);
  for(off=0; off < total; off += n, segment++) {
    n = total - off < X_UPLOAD_PART ? total - off : X_UPLOAD_PART;
    memcpy(part, head, hlen);
    if(photo) {
      memcpy(part + hlen, photo + off, (size_t)n);
    } else if((ret=(int)video_read(c, cut ? &clip : NULL, off, part + hlen, (size_t)n)) != n) {
      a->error = ERR_CAPTURE;
      a->code = ret < 0 ? ret : -1;
      goto fail;
    }
    memcpy(part + hlen + n, tail, tlen);
    if(x_upload_url(url, sizeof url, X_UPLOAD_APPEND, media_id, 0, NULL, segment)) {
      a->error = ERR_MEMORY;
      goto fail;
    }
    x_request_init(&request, session);
    count = upload_headers(headers, &request, form);
    ret = https_post_data(url, headers, count, part, hlen + (size_t)n + tlen, &res);
    memset(&request, 0, sizeof request);
    memset(headers, 0, sizeof headers);
    if(ret) {
      a->error = ERR_NETWORK;
      a->code = ret;
      goto fail;
    }
    log_line("upload part %d: HTTP %d", segment, res.status);
    mem_free(res.body);
    if(res.status / 100 != 2) {
      a->error = ERR_UPLOAD;
      a->code = res.status;
      goto fail;
    }
    __atomic_store_n(&a->sent, base + (off + n) * weight / total, __ATOMIC_RELAXED);
  }
  mem_free(part);
  mem_free(photo);
  part = photo = NULL;
  clip_free(&clip);

  // FINALIZE, then STATUS while X processes a video
  if(x_upload_url(url, sizeof url, X_UPLOAD_FINALIZE, media_id, 0, NULL, 0)) {
    a->error = ERR_MEMORY;
    goto fail;
  }
  if(!(doc=upload_step(a, session, url, 0, "finalize", &res))) {
    goto fail;
  }
  for(;;) {
    state = x_upload_state(json_root(doc), &wait);
    json_free(doc);
    mem_free(res.body);
    if(state != last) {
      log_line("upload: %s after %d s", state == X_UPLOAD_READY ? "ready" :
               state == X_UPLOAD_WAIT ? "processing" : "failed", waited);
      last = state;
    }
    if(state == X_UPLOAD_READY) {
      return 0;
    }
    if(state != X_UPLOAD_WAIT || waited >= PROCESS_LIMIT) {
      a->error = ERR_PROCESSING;
      goto fail;
    }
    sceKernelUsleep((unsigned)wait * 1000000u);
    waited += wait;
    if(x_upload_url(url, sizeof url, X_UPLOAD_STATUS, media_id, 0, NULL, 0)) {
      a->error = ERR_MEMORY;
      goto fail;
    }
    if(!(doc=upload_step(a, session, url, 1, "status", &res))) {
      goto fail;
    }
  }

 fail:
  mem_free(part);
  mem_free(photo);
  clip_free(&clip);
  memset(media_id, 0, size);
  return -1;
}


/**
 * every capture of a post or reply; on 0, ids holds their numbers
 **/
static int
upload_all(action_t *a, const x_session_t *session, char ids[][32]) {
  long long base = 0, total = 0;

  for(int i=0; i < a->count; i++) {
    total += a->media[i].size > 0 ? a->media[i].size : 1;
  }
  __atomic_store_n(&a->total, total, __ATOMIC_RELAXED);
  for(int i=0; i < a->count; i++) {
    if(upload_media(a, session, &a->media[i], base, ids[i], 32)) {
      log_line("upload: media %d of %d failed, error %d", i + 1, a->count, a->error);
      return -1;
    }
    base += a->media[i].size > 0 ? a->media[i].size : 1;
    __atomic_store_n(&a->sent, base, __ATOMIC_RELAXED);
  }
  return 0;
}


/**
 * the card of the composer's poll, made on caps.x.com the way x.com's
 * composer makes it (a form, with the session, without the header): its
 * card:// into card; 0, or -1 with a->error. Neither the choices nor the
 * card go to the klog.
 **/
static int
create_poll(action_t *a, x_session_t *session, char *card, size_t size) {
  static char url[256], body[2048];
  const char *labels[X_POLL_MAX], *message;
  https_header_t headers[X_HEADER_COUNT + 1];
  https_response_t res;
  x_request_t request;
  json_doc_t *doc;
  size_t err_off;
  int ret, why, done = -1, count = X_HEADER_COUNT;

  card[0] = 0;
  for(int i=0; i < a->poll_count && i < X_POLL_MAX; i++) {
    labels[i] = a->choices[i];
  }
  if(x_poll_card_request(url, sizeof url, body, sizeof body, labels, a->poll_count, a->minutes)) {
    a->error = ERR_MEMORY;
    return -1;
  }
  x_request_init(&request, session);
  memcpy(headers, request.headers, sizeof request.headers);
  for(int i=0; i < X_HEADER_COUNT; i++) {
    if(!strcmp(headers[i].name, "content-type")) {
      headers[i].value = "application/x-www-form-urlencoded";
    }
  }
  headers[count++] = (https_header_t){"origin", "https://x.com"};
  ret = https_post(url, headers, count, body, &res);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  memset(body, 0, sizeof body);
  if(ret) {
    a->error = ERR_NETWORK;
    a->code = ret;
    return -1;
  }
  a->code = res.status;
  if((doc=json_parse_why(res.body, res.len, &err_off, &why))) {
    if(x_card_uri(json_root(doc), card, size)) {
      done = x_action_done(json_root(doc), &message);
    }
    json_free(doc);
  }
  log_line("poll card: HTTP %d, %zu bytes, %s, X says %d", res.status, res.len, card[0] ? "made" : "none",
           card[0] ? 0 : done);
  mem_free(res.body);
  if(res.status == 200 && card[0]) {
    return 0;
  }
  card[0] = 0;
  a->x_code = done;
  a->error = done > 0 || res.status != 200 ? ERR_HTTP : ERR_ANSWER;
  return -1;
}


/*
 * Likes, replies, reposts and bookmarks, POSTed the way x.com does with the
 * user's session. Neither the reply nor the post's number goes to the klog.
 */

/*
 * Grok's hints: the conversation the questions go to (a new one when the
 * screen asks, or after a failure), the host that answered last (0
 * X_GROK_HOST, 1 X_GROK_HOST_OLD), and whether X refused the model named
 */
static char g_grok_conv[X_GROK_ID_MAX];
static int  g_grok_host;
static int  g_grok_no_model;


/**
 * a language as Grok is told to answer in it
 **/
static const char *
lang_name(int lang) {
  static const char *const names[LANGS] = {
    [LANG_EN] = "English", [LANG_PT] = "European Portuguese", [LANG_ES] = "Spanish", [LANG_FR] = "French",
    [LANG_DE] = "German", [LANG_IT] = "Italian", [LANG_NL] = "Dutch",
  };
  return lang >= 0 && lang < LANGS && names[lang] ? names[lang] : "English";
}


/**
 * POST body to url with the session, with the header and the origin of
 * x.com's Grok page; *signed_tid says whether it had the header. The
 * answer in res, or the error of https_post
 **/
static int
grok_post(const x_session_t *session, const char *url, const char *body, https_response_t *res,
          int *signed_tid) {
  static char tid[XTID_HEADER_MAX];
  https_header_t headers[X_HEADER_COUNT + 2];
  x_request_t request;
  int count = X_HEADER_COUNT, ret;

  *signed_tid = sign(session, "POST", url, tid, sizeof tid);
  x_request_init(&request, session);
  memcpy(headers, request.headers, sizeof request.headers);
  for(int i=0; i < X_HEADER_COUNT; i++) {
    if(!strcmp(headers[i].name, "referer")) {
      headers[i].value = "https://x.com/i/grok";
    }
  }
  headers[count++] = (https_header_t){"origin", "https://x.com"};
  if(*signed_tid) {
    headers[count++] = (https_header_t){"x-client-transaction-id", tid};
  }
  ret = https_post(url, headers, count, body, res);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  memset(tid, 0, sizeof tid);
  return ret;
}


/**
 * a's question to Grok, in a new conversation when it asks or none is
 * open (a translation or a catch-up always in one of its own, so the
 * hints' conversation goes on without them); its answer into a->answer.
 * The klog has codes and sizes only, never the words
 **/
static void
send_grok(action_t *a) {
  static char url[512], body[2 * X_GROK_PROMPT + 1024], prompt[X_GROK_PROMPT], own[X_GROK_ID_MAX];
  static const char *const hosts[2] = {X_GROK_HOST, X_GROK_HOST_OLD};
  const char *message;
  https_response_t res;
  x_session_t session;
  json_doc_t *doc;
  size_t err_off;
  int ret, why, signed_tid = 0, parts = 0, h = g_grok_host, host_tried = 0, model_tried = 0;
  char *conv = a->grok == UI_GROK_HINT ? g_grok_conv : own;

  if((ret=read_session(&session))) {
    a->error = ret == -3 ? ERR_NO_SESSION : ERR_NO_COOKIE;
    return;
  }
  if(g_https) {
    memset(&session, 0, sizeof session);
    a->error = ERR_HTTPS;
    a->code = g_https;
    return;
  }
  if(a->fresh || a->grok != UI_GROK_HINT || !conv[0]) {
    conv[0] = 0;
    if(x_grok_create_request(url, sizeof url, body, sizeof body)) {
      memset(&session, 0, sizeof session);
      a->error = ERR_MEMORY;
      return;
    }
    if((ret=grok_post(&session, url, body, &res, &signed_tid))) {
      memset(&session, 0, sizeof session);
      log_line("grok: new conversation: 0x%08x", (unsigned)ret);
      a->error = ERR_NETWORK;
      a->code = ret;
      return;
    }
    a->code = res.status;
    if((doc=json_parse_why(res.body, res.len, &err_off, &why))) {
      if(x_grok_conversation(json_root(doc), conv, X_GROK_ID_MAX)) {
        a->x_code = x_first_error(json_root(doc), &message);
      }
      json_free(doc);
    }
    log_line("grok: new conversation%s: HTTP %d, %s, X says %d", signed_tid ? " (tid)" : "", res.status,
             conv[0] ? "opened" : "no id", a->x_code);
    mem_free(res.body);
    if(!conv[0]) {
      memset(&session, 0, sizeof session);
      if(signed_tid && (res.status == 404 || a->x_code == 226)) {
        forget_tid();
      }
      a->error = res.status == 200 && a->x_code <= 0 ? ERR_ANSWER : ERR_HTTP;
      return;
    }
  }
  ret = a->grok == UI_GROK_TRANSLATE ? x_grok_translate_prompt(prompt, sizeof prompt, a->user, a->text,
                                                               lang_name(a->lang)) :
        a->grok == UI_GROK_CATCHUP ? x_grok_catchup_prompt(prompt, sizeof prompt, a->text, lang_name(a->lang)) :
        x_grok_prompt(prompt, sizeof prompt, a->game, a->text, lang_name(a->lang));
  memset(a->text, 0, sizeof a->text);
  if(ret) {
    memset(&session, 0, sizeof session);
    a->error = ERR_MEMORY;
    return;
  }
  // grok.x.com, or api.x.com when it cannot be reached; the model named, or
  // X's own when it is refused
  for(;;) {
    if(x_grok_request(url, sizeof url, body, sizeof body, hosts[h], conv,
                      g_grok_no_model ? NULL : X_GROK_MODEL, prompt)) {
      ret = -1;
      break;
    }
    ret = grok_post(&session, url, body, &res, &signed_tid);
    if(ret && !host_tried) {
      log_line("grok: %s: 0x%08x, the other host", hosts[h], (unsigned)ret);
      host_tried = 1;
      h = !h;
      continue;
    }
    if(!ret && (res.status == 400 || res.status == 422) && !g_grok_no_model && !model_tried) {
      log_line("grok: HTTP %d with the model, again without it", res.status);
      mem_free(res.body);
      g_grok_no_model = model_tried = 1;
      continue;
    }
    break;
  }
  memset(&session, 0, sizeof session);
  memset(prompt, 0, sizeof prompt);
  memset(body, 0, sizeof body);
  if(ret) {
    a->error = ret < 0 ? ERR_MEMORY : ERR_NETWORK;
    a->code = ret;
    conv[0] = 0;
    return;
  }
  g_grok_host = h;
  a->code = res.status;
  if(res.status == 200) {
    parts = x_grok_answer(res.body, res.len, a->answer, sizeof a->answer, &a->x_code);
  } else if((doc=json_parse_why(res.body, res.len, &err_off, &why))) {
    a->x_code = x_first_error(json_root(doc), &message);
    json_free(doc);
  }
  if(model_tried && res.status != 200) {
    g_grok_no_model = 0; // it was not the model
  }
  log_line("grok: %s%s%s: HTTP %d, %zu bytes, %d parts, X says %d", hosts[h], g_grok_no_model ? " (X's model)" : "",
           signed_tid ? " (tid)" : "", res.status, res.len, parts, a->x_code);
  mem_free(res.body);
  if(signed_tid && (res.status == 404 || a->x_code == 226)) {
    forget_tid();
  }
  if(res.status == 200 && parts > 0) {
    return;
  }
  memset(a->answer, 0, sizeof a->answer);
  conv[0] = 0; // a new conversation next time
  a->error = res.status == 200 && a->x_code <= 0 ? ERR_ANSWER : ERR_HTTP;
}


/**
 * the game of an invite, opened by the payload (outside the title's
 * sandbox): the one with its title id, or the installed one of its name
 **/
static int ask_payload(const char *cmd, char *line, size_t size);
int sceNetSocketClose(int s);

/**
 * a Space to hear (UI_ACT_SPACE): how it stands (AudioSpaceById), then
 * where its sound is (live_video_stream/status), both asked of X with the
 * session as x.com asks them. The playlist's address goes back in
 * a->answer for space.c, which fetches it with no session; neither it nor
 * the Space's media key goes to the klog
 **/
static void
send_space(action_t *a) {
  static char url[1024];
  const char *message;
  https_response_t res;
  x_session_t session;
  x_request_t request;
  json_doc_t *doc;
  size_t err_off;
  int ret, why;

  if((ret=read_session(&session))) {
    a->error = ret == -3 ? ERR_NO_SESSION : ERR_NO_COOKIE;
    return;
  }
  if(g_https) {
    memset(&session, 0, sizeof session);
    a->error = ERR_HTTPS;
    a->code = g_https;
    return;
  }
  if(x_space_url(url, sizeof url, a->id)) {
    memset(&session, 0, sizeof session);
    a->error = ERR_MEMORY;
    return;
  }
  for(int step=0; step < 2 && !a->error; step++) {
    x_request_init(&request, &session);
    ret = https_get(url, request.headers, X_HEADER_COUNT, &res);
    memset(&request, 0, sizeof request);
    memset(url, 0, sizeof url);
    if(ret) {
      a->error = ERR_NETWORK;
      a->code = ret;
      break;
    }
    log_line("space: %s, HTTP %d, %zu bytes", step ? "where its sound is" : "how it stands", res.status, res.len);
    a->code = res.status;
    if(!(doc=json_parse_why(res.body, res.len, &err_off, &why))) {
      a->error = res.status == 200 ? ERR_ANSWER : ERR_HTTP;
    } else if(res.status != 200) {
      a->x_code = x_first_error(json_root(doc), &message);
      a->error = ERR_HTTP;
    } else if(!step) {
      if(x_space_parse(json_root(doc), &a->space)) {
        a->error = ERR_ANSWER;
      } else if(a->space.state != X_SPACE_LIVE && !(a->space.state == X_SPACE_ENDED && a->space.replay)) {
        a->error = ERR_SPACE_OFF; // not started yet, or over with no recording
        a->code = a->space.state;
      } else if(x_space_stream_url(url, sizeof url, a->space.media_key)) {
        a->error = ERR_MEMORY;
      }
      log_line("space: %s, %d speaker(s)", a->space.state == X_SPACE_LIVE ? "on the air" :
               a->space.state == X_SPACE_SOON ? "not started" : a->space.state == X_SPACE_ENDED ?
               (a->space.replay ? "over, recorded" : "over") : "unknown", a->space.speakers);
      memset(a->space.media_key, 0, sizeof a->space.media_key);
    } else if(x_space_stream_parse(json_root(doc), a->answer, sizeof a->answer)) {
      a->error = ERR_ANSWER;
    }
    json_free(doc);
    mem_free(res.body);
  }
  memset(&session, 0, sizeof session);
  memset(url, 0, sizeof url);
}


static void
send_launch(action_t *a) {
  char cmd[GAME_NAME + 48], line[64];
  int s;

  for(char *c=a->game; *c; c++) {
    *c = *c == '\n' || *c == '\r' ? ' ' : *c;
  }
  snprintf(cmd, sizeof cmd, "launch %s %s\n", a->id[0] ? a->id : "-", a->game);
  if((s=ask_payload(cmd, line, sizeof line)) < 0) {
    a->error = ERR_NETWORK;
    a->code = s;
    log_line("launch: the payload did not answer (%d)", s);
    return;
  }
  sceNetSocketClose(s);
  if(!strncmp(line, "ok", 2)) {
    a->error = ERR_NONE;
  } else if(!strncmp(line, "none", 4)) {
    a->error = ERR_NO_GAME;
  } else if(!strncmp(line, "busy", 4)) {
    a->error = ERR_GAME_OPEN;
  } else {
    a->error = ERR_LAUNCH;
    a->code = !strncmp(line, "err ", 4) ? (int)strtoul(line + 4, NULL, 16) : -1;
  }
  log_line("launch: error %d, code 0x%08x", a->error, (unsigned)a->code);
}


/**
 * what X kept of an Ultra HDR picture just posted (ours bytes): its copy at
 * the whole size, fetched once, logged as counts only
 **/
static void
look_at_copy(const char *url, size_t ours) {
  https_response_t res;
  ultrahdr_info_t info;
  int ret;

  memset(&res, 0, sizeof res);
  if((ret=https_get(url, NULL, 0, &res))) {
    log_line("ultra hdr: X's copy not fetched (%d)", ret);
    return;
  }
  ultrahdr_inspect(res.status == 200 ? (const uint8_t *)res.body : NULL, res.len, &info);
  log_line("ultra hdr: X's copy, HTTP %d, %zu bytes (ours %zu): %d picture(s), XMP %d, MPF %d, gain map %d, "
           "ISO %d", res.status, res.len, ours, info.pictures, info.xmp, info.mpf, info.gain, info.iso);
  mem_free(res.body);
}


static void
send_action(action_t *a) {
  static const char *const names[] = {
    [UI_ACT_LIKE] = "like", [UI_ACT_UNLIKE] = "unlike", [UI_ACT_REPLY] = "reply",
    [UI_ACT_REPOST] = "repost", [UI_ACT_UNREPOST] = "unrepost", [UI_ACT_POST] = "post",
    [UI_ACT_BOOKMARK] = "bookmark", [UI_ACT_UNBOOKMARK] = "unbookmark", [UI_ACT_DELETE] = "delete",
    [UI_ACT_DM] = "message", [UI_ACT_QUOTE] = "quote", [UI_ACT_EDIT] = "edit", [UI_ACT_PIN] = "pin",
    [UI_ACT_UNPIN] = "unpin", [UI_ACT_FOLLOW] = "follow", [UI_ACT_UNFOLLOW] = "unfollow", [UI_ACT_MUTE] = "mute",
    [UI_ACT_UNMUTE] = "unmute", [UI_ACT_BLOCK] = "block", [UI_ACT_UNBLOCK] = "unblock",
    [UI_ACT_SCHEDULE] = "schedule", [UI_ACT_UNSCHEDULE] = "unschedule", [UI_ACT_VOTE] = "vote",
    [UI_ACT_INVITE] = "invite", [UI_ACT_SHARE_DM] = "message with media", [UI_ACT_VOICE_DM] = "voice message",
  };
  static char url[1024], body[X_BODY_MAX], tid[XTID_HEADER_MAX], ids[UI_MEDIA_MAX][32], card[64];
  static char copy_url[512];
  char request_id[40];
  int composed = a->kind == UI_ACT_REPLY || a->kind == UI_ACT_POST || a->kind == UI_ACT_QUOTE ||
                 a->kind == UI_ACT_EDIT;
  // a vote goes to caps.x.com, as a form and without the header, as x.com sends it
  int caps = a->kind == UI_ACT_VOTE;
  int form = (a->kind >= UI_ACT_FOLLOW && a->kind <= UI_ACT_UNBLOCK) || caps;
  const char *what = a->kind > UI_ACT_NONE && a->kind < (int)(sizeof names / sizeof *names) && names[a->kind] ?
                     names[a->kind] : "?";
  const char *message, *media[UI_MEDIA_MAX];
  https_header_t headers[X_HEADER_COUNT + 2];
  https_response_t res;
  x_session_t session;
  x_request_t request;
  json_doc_t *doc;
  size_t err_off;
  int ret, why, done = -1, count = X_HEADER_COUNT, signed_tid = 0, copy = 0;

  if(a->kind == UI_ACT_GROK) {
    send_grok(a);
    return;
  }
  if(a->kind == UI_ACT_LAUNCH) {
    send_launch(a);
    return;
  }
  if(a->kind == UI_ACT_XCHAT_UNLOCK) {
    send_unlock(a);
    return;
  }
  if(a->kind == UI_ACT_SPACE) {
    send_space(a);
    return;
  }
  if((ret=read_session(&session))) {
    a->error = ret == -3 ? ERR_NO_SESSION : ERR_NO_COOKIE;
    return;
  }
  if((a->kind == UI_ACT_REPLY || a->kind == UI_ACT_POST || a->kind == UI_ACT_QUOTE ||
      a->kind == UI_ACT_SCHEDULE || a->kind == UI_ACT_SHARE_DM || a->kind == UI_ACT_VOICE_DM) && a->count) {
    // the captures first: the post names them
    if(g_https) {
      memset(&session, 0, sizeof session);
      a->error = ERR_HTTPS;
      a->code = g_https;
      return;
    }
    if(upload_all(a, &session, ids)) {
      memset(&session, 0, sizeof session);
      return;
    }
    for(int i=0; i < a->count; i++) {
      media[i] = ids[i];
    }
  }
  // a poll's card first: the post carries it
  card[0] = 0;
  if(composed && a->poll_count && (a->kind == UI_ACT_POST || a->kind == UI_ACT_REPLY)) {
    if(g_https) {
      memset(&session, 0, sizeof session);
      a->error = ERR_HTTPS;
      a->code = g_https;
      return;
    }
    if(create_poll(a, &session, card, sizeof card)) {
      memset(&session, 0, sizeof session);
      return;
    }
  }
  if(composed) {
    x_compose_t how = {
      a->kind == UI_ACT_REPLY ? a->id : NULL, a->kind == UI_ACT_QUOTE ? a->user : NULL,
      a->kind == UI_ACT_QUOTE ? a->id : NULL, a->kind == UI_ACT_EDIT ? a->id : NULL,
      card[0] ? card : NULL,
    };
    ret = x_compose_request(url, sizeof url, body, sizeof body, &how, a->text, media,
                            a->kind == UI_ACT_EDIT ? 0 : a->count);
  } else if(a->kind == UI_ACT_SCHEDULE) {
    ret = x_schedule_request(url, sizeof url, body, sizeof body, a->text, media, a->count, a->at);
  } else if(a->kind == UI_ACT_PIN || a->kind == UI_ACT_UNPIN) {
    ret = x_pin_request(url, sizeof url, body, sizeof body, a->id, a->kind == UI_ACT_PIN);
  } else if(form) {
    static const int whats[] = {
      [UI_ACT_FOLLOW] = X_FOLLOW, [UI_ACT_UNFOLLOW] = X_UNFOLLOW, [UI_ACT_MUTE] = X_MUTE,
      [UI_ACT_UNMUTE] = X_UNMUTE, [UI_ACT_BLOCK] = X_BLOCK, [UI_ACT_UNBLOCK] = X_UNBLOCK,
    };
    ret = x_user_request(url, sizeof url, body, sizeof body, whats[a->kind], a->id);
  } else if(a->kind == UI_ACT_REPOST || a->kind == UI_ACT_UNREPOST) {
    ret = x_repost_request(url, sizeof url, body, sizeof body, a->id, a->kind == UI_ACT_REPOST);
  } else if(a->kind == UI_ACT_BOOKMARK || a->kind == UI_ACT_UNBOOKMARK) {
    ret = x_bookmark_request(url, sizeof url, body, sizeof body, a->id, a->kind == UI_ACT_BOOKMARK);
  } else if(a->kind == UI_ACT_DELETE) {
    ret = x_delete_request(url, sizeof url, body, sizeof body, a->id);
  } else if(a->kind == UI_ACT_UNSCHEDULE) {
    ret = x_unschedule_request(url, sizeof url, body, sizeof body, a->id);
  } else if(a->kind == UI_ACT_VOTE) {
    ret = x_vote_request(url, sizeof url, body, sizeof body, a->card, a->id, a->card_name, a->choice);
  } else if(a->kind == UI_ACT_DM || a->kind == UI_ACT_INVITE || a->kind == UI_ACT_SHARE_DM ||
            a->kind == UI_ACT_VOICE_DM) {
    // a new UUID (version 4) names the message, as x.com's does
    unsigned char b[16];
    for(int i=0; i < 16; i++) {
      b[i] = (unsigned char)random_byte();
    }
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    snprintf(request_id, sizeof request_id,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2],
             b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    ret = a->kind == UI_ACT_SHARE_DM || a->kind == UI_ACT_VOICE_DM ?
          x_dm_media_request(url, sizeof url, body, sizeof body, a->id, a->text, a->count ? ids[0] : NULL,
                             request_id) :
          x_dm_send_request(url, sizeof url, body, sizeof body, a->id, a->text, request_id);
  } else {
    ret = x_like_request(url, sizeof url, body, sizeof body, a->id, a->kind == UI_ACT_LIKE);
  }
  memset(a->text, 0, sizeof a->text);
  memset(ids, 0, sizeof ids);
  if(ret || g_https) {
    memset(&session, 0, sizeof session);
    a->error = ret ? ERR_MEMORY : ERR_HTTPS;
    a->code = g_https;
    return;
  }

  // likes go out as they always have; the rest carry the header, and the
  // origin a page of x.com sends
  if(a->kind != UI_ACT_LIKE && a->kind != UI_ACT_UNLIKE && !caps) {
    signed_tid = sign(&session, "POST", url, tid, sizeof tid);
  }

  x_request_init(&request, &session);
  memcpy(headers, request.headers, sizeof request.headers);
  // following, muting and blocking go as a form, as x.com sends them
  for(int i=0; form && i < X_HEADER_COUNT; i++) {
    if(!strcmp(headers[i].name, "content-type")) {
      headers[i].value = "application/x-www-form-urlencoded";
    }
  }
  if(signed_tid || caps) {
    headers[count++] = (https_header_t){"origin", "https://x.com"};
  }
  if(signed_tid) {
    headers[count++] = (https_header_t){"x-client-transaction-id", tid};
  }
  ret = https_post(url, headers, count, body, &res);
  memset(&session, 0, sizeof session);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  memset(body, 0, sizeof body);
  if(ret) {
    a->error = ERR_NETWORK;
    a->code = ret;
    return;
  }

  a->code = res.status;
  if((doc=json_parse_why(res.body, res.len, &err_off, &why))) {
    done = x_action_done(json_root(doc), &message);
    // one Ultra HDR picture posted: X's copy of it looked at, below
    copy = composed && a->ultra_hdr && a->count == 1 && res.status == 200 && !done &&
           !x_created_media_url(json_root(doc), copy_url, sizeof copy_url);
    json_free(doc);
  }
  log_line("%s%s%s: HTTP %d, %zu bytes, X says %d", what, a->count ? " with media" : "",
           signed_tid ? " (tid)" : "", res.status, res.len, done);
  mem_free(res.body);
  if(signed_tid && (done == 226 || res.status == 404)) {
    forget_tid(); // a new page's key next time
  }

  // 139: the post was liked already, 327: reposted already, which is what was asked;
  // a vote answers with the poll's card, a scheduled post taken back with no error
  if(res.status == 200 && (!done || (a->kind == UI_ACT_LIKE && done == 139) ||
                           (a->kind == UI_ACT_REPOST && done == 327) ||
                           ((caps || a->kind == UI_ACT_UNSCHEDULE) && done == -1))) {
    if(copy) {
      look_at_copy(copy_url, a->ultra_hdr);
    }
    return;
  }
  a->x_code = done;
  a->error = done > 0 || res.status != 200 ? ERR_HTTP : ERR_ANSWER;
}


static void *
action_main(void *arg) {
  action_t *a = arg;

  send_action(a);
  __atomic_store_n(&a->state, FETCH_DONE, __ATOMIC_RELEASE);
  return NULL;
}


/**
 * send kind about the post id: for a post or a reply, with the composer's
 * text and captures
 **/
static void
start_action(int kind, const char *id, const char *user) {
  memset(&g_action, 0, sizeof g_action);
  g_action.state = FETCH_RUNNING;
  g_action.kind = kind;
  snprintf(g_action.id, sizeof g_action.id, "%s", id);
  snprintf(g_action.user, sizeof g_action.user, "%s", user);
  if(kind == UI_ACT_POST || kind == UI_ACT_REPLY || kind == UI_ACT_QUOTE || kind == UI_ACT_EDIT ||
     kind == UI_ACT_SCHEDULE) {
    g_action.count = ui_compose_get(g_action.text, sizeof g_action.text, g_action.media,
                                    UI_MEDIA_MAX);
    g_action.at = (long long)time(NULL) + ui_schedule_delay();
    if(kind == UI_ACT_POST || kind == UI_ACT_REPLY) {
      g_action.poll_count = ui_poll_get(g_action.choices, &g_action.minutes);
    }
  } else if(kind == UI_ACT_VOTE) {
    g_action.choice = ui_vote_get(g_action.card, sizeof g_action.card, g_action.card_name,
                                  sizeof g_action.card_name);
  } else if(kind == UI_ACT_DM) {
    ui_dm_get(g_action.text, sizeof g_action.text);
  } else if(kind == UI_ACT_INVITE) {
    ui_invite_get(g_action.text, sizeof g_action.text);
  } else if(kind == UI_ACT_SHARE_DM) {
    // a message carries one picture or one video: the first
    g_action.count = ui_compose_get(g_action.text, sizeof g_action.text, g_action.media, UI_MEDIA_MAX);
    g_action.count = g_action.count > 1 ? 1 : g_action.count;
  } else if(kind == UI_ACT_VOICE_DM) {
    g_action.text[0] = 0; // the note alone
    g_action.count = ui_voice_dm_get(&g_action.media[0]);
  } else if(kind == UI_ACT_LAUNCH) {
    ui_launch_get(g_action.id, sizeof g_action.id, g_action.game, sizeof g_action.game);
  } else if(kind == UI_ACT_GROK) {
    ui_grok_get(g_action.text, sizeof g_action.text, g_action.game, sizeof g_action.game, &g_action.fresh,
                &g_action.grok, g_action.user, sizeof g_action.user);
    g_action.lang = ui_lang();
  }
  if(!start_thread(action_main, &g_action, 1 << 20)) {
    g_action.error = ERR_MEMORY;
    g_action.state = FETCH_DONE;
  }
}


/**
 * the X Chat unlock on its own thread: the PIN recovers the identity key
 * (send_unlock); the PIN is held only here and wiped once it is used
 **/
static void
start_unlock(const char *me, const char *pin) {
  memset(&g_action, 0, sizeof g_action);
  g_action.state = FETCH_RUNNING;
  g_action.kind = UI_ACT_XCHAT_UNLOCK;
  snprintf(g_action.id, sizeof g_action.id, "%s", me);
  snprintf(g_action.text, sizeof g_action.text, "%s", pin);
  if(!start_thread(action_main, &g_action, 1 << 20)) {
    xcrypt_wipe(g_action.text, sizeof g_action.text);
    g_action.error = ERR_MEMORY;
    g_action.state = FETCH_DONE;
  }
}


/*
 * The pictures, fetched by LOADERS threads from pbs.twimg.com, with no X
 * session: they are public.
 */

static void *
loader_main(void *arg) {
  static int reported;
  char url[IMAGES_URL_MAX];
  https_response_t res;
  int w, h, ret;

  (void)arg;
  for(;;) {
    if(!images_next(url, sizeof url, &w, &h)) {
      sceKernelUsleep(30 * 1000);
      continue;
    }
    // a capture's picture, for the composer
    if(!strncmp(url, "file://", 7)) {
      long long total = 0;
      void *data = NULL;
      int kind = kept_kind(url + 7);

      if((kind == CAPTURE_JPEG || kind == CAPTURE_PNG || kind == CAPTURE_JXR) &&
         capture_read(url + 7, 0, NULL, 0, &total) >= 0 && total > 0 && total <= CAPTURE_PHOTO_MAX &&
         (data=mem_alloc((size_t)total)) && capture_load(url + 7, data, total) != total) {
        mem_free(data);
        data = NULL;
      }
      if(kind == CAPTURE_JXR) {
        // a .jxr screenshot: from the smaller size its bands keep, in SDR;
        // the screen labels the HDR ones
        static int told;
        uint32_t *pixels = NULL, *fit = NULL;
        int jw = 0, jh = 0, format = 0, error = -1;
        unsigned long start = (unsigned long)sceKernelGetProcessTime();
        if(data && (pixels=jxr_decode(data, (size_t)total, w, h, &jw, &jh, &format, &error))) {
          fit = image_fit_pixels(pixels, jw, jh, w, h);
          mem_free(pixels);
        }
        if(fit && jxr_format_hdr(format)) {
          ui_capture_hdr(url + 7);
        }
        if(__atomic_fetch_add(&told, 1, __ATOMIC_RELAXED) < 3) {
          log_line("picture: a .jxr screenshot, pixel format 0x%x (%s), %dx%d for %dx%d, %s in %lu ms", format,
                   jxr_format_hdr(format) ? "HDR" : "SDR", jw, jh, w, h, fit ? "drawn" : "not read",
                   ((unsigned long)sceKernelGetProcessTime() - start) / 1000);
          if(!fit) {
            log_line("picture: the .jxr screenshot's error %d", error);
          }
        }
        mem_free(data);
        images_done_pixels(url, w, h, fit);
        continue;
      }
      images_done(url, w, h, data, data ? (size_t)total : 0);
      mem_free(data);
      continue;
    }
    // a picture of X Chat is encrypted: fetched with the session (it may be
    // private) and opened before it is decoded
    {
      uint8_t key[32];
      x_session_t session;
      x_request_t req;
      int enc = images_key(url, w, h, key);
      const https_header_t *hdr = NULL;
      int nhdr = 0;

      memset(&session, 0, sizeof session);
      memset(&req, 0, sizeof req);
      if(enc && !read_session(&session)) {
        x_request_init(&req, &session);
        hdr = req.headers;
        nhdr = X_HEADER_COUNT;
      }
      ret = https_get(url, hdr, nhdr, &res);
      memset(&session, 0, sizeof session);
      memset(&req, 0, sizeof req);
      if(!ret && res.status == 200 && enc) {
        unsigned char *plain = res.len ? mem_alloc(res.len) : NULL;
        size_t plen = 0;
        // opened, and its bytes the ones its message says (their SHA-256)
        if(plain && !xcrypt_media_open(key, (const uint8_t *)res.body, res.len, plain, res.len, &plen) &&
           !xchat_open_check(url, plain, plen)) {
          images_done(url, w, h, plain, plen);
        } else {
          if(__atomic_fetch_add(&reported, 1, __ATOMIC_RELAXED) < 5) {
            log_line("picture: X Chat media (%zu bytes) did not open, or not as its message says", res.len);
          }
          images_done(url, w, h, NULL, 0);
        }
        mem_free(plain);
      } else if(!ret && res.status == 200) {
        images_done(url, w, h, res.body, res.len);
      } else {
        if(!ret && __atomic_fetch_add(&reported, 1, __ATOMIC_RELAXED) < 5) {
          log_line("picture:%s HTTP %d", enc ? " X Chat" : "", res.status);
        }
        images_done(url, w, h, NULL, 0);
      }
      xcrypt_wipe(key, sizeof key);
      mem_free(res.body);
    }
  }
  return NULL;
}


/*
 * The controller.
 */

// the PS5's own screens have the controller (its control center, over the
// app that still runs): the bit it sets among the buttons
#define PAD_INTERCEPTED 0x80000000u

static int g_pad_taken;  // the PS5's screens have the controller
static int g_pad_back;   // it came back from them, to be seen once
static int g_pad_keyboard; // it was the PS5's keyboard that had it, not its control center

/**
 * the keys pressed this frame, with held ones repeating, and where the
 * sticks are: left x and y, right x and y, -128 to 127 from the middle
 **/
static unsigned
read_pad(int sticks[4]) {
  static unsigned held[21];
  static unsigned retry;
  unsigned char state[512] __attribute__((aligned(16)));
  unsigned buttons, pressed = 0, bit;
  int user;

  memset(sticks, 0, 4 * sizeof *sticks);
  if(g_pad < 0) {
    if(retry++ % 60) {
      return 0;
    }
    if(sceUserServiceGetInitialUser(&user) ||
       (g_pad=scePadOpen(user, 0, 0, NULL)) < 0) {
      if(retry == 1) {
        log_line("pad: open 0x%08x", (unsigned)g_pad);
      }
      g_pad = -1;
      return 0;
    }
    log_line("pad: open");
  }

  memset(state, 0, sizeof state);
  if(scePadReadState(g_pad, state)) {
    return 0;
  }
  memcpy(&buttons, state, sizeof buttons);
  if(!!(buttons & PAD_INTERCEPTED) != g_pad_taken) {
    g_pad_taken = !g_pad_taken;
    g_pad_back |= !g_pad_taken;
    log_line("pad: %s", g_pad_taken ? "the PS5's screens have it" : "back");
  }
  // the sticks, bytes 4 to 7, 128 in the middle
  for(int i=0; i<4; i++) {
    sticks[i] = state[4 + i] - 128;
  }

  for(int i=0; i<21; i++) {
    bit = 1u << i;
    if(!(PAD_KEYS & bit)) {
      continue;
    }
    if(!(buttons & bit)) {
      held[i] = 0;
    } else if(++held[i] == 1 || ((bit & PAD_REPEATS) && held[i] > REPEAT_DELAY &&
                                 (held[i] - REPEAT_DELAY) % REPEAT_EVERY == 0)) {
      pressed |= bit;
    }
  }
  return pressed;
}


/**
 * the microphone's test on Calls: started and stopped as the screen asks,
 * for the signed-in user, and its meter on the screen while it runs
 **/
static void
test_mic(int *dirty) {
  mic_status_t status;
  int retry, want = ui_mic_wanted(&retry), user, ret;

  if(g_mic_on && (!want || retry)) {
    mic_stop();
    g_mic_on = 0;
  }
  if(want && !g_mic_on) {
    if((ret=sceUserServiceGetInitialUser(&user))) {
      log_line("mic: no user 0x%08x, the system's then", (unsigned)ret);
      user = 0xff;
    }
    g_mic_on = !mic_start(user, log_line); // else the last test still ends: the next frame
  }
  if(!want) {
    // the microphone may go on for a voice note: the test shows it off
    if(g_mic_shown != MIC_OFF) {
      ui_mic(MIC_OFF, 0, 0, 0, 0);
      g_mic_shown = MIC_OFF;
      *dirty = 1;
    }
    return;
  }
  mic_get(&status);
  ui_mic(status.state, status.code, status.silent, status.level, status.peak);
  *dirty |= want || status.state != g_mic_shown;
  g_mic_shown = status.state;
}


/**
 * a voice note's tape: the microphone on for it while its sheet wants it,
 * the samples in the title's memory, and once ✕ stops it their loudness to
 * the screen for the note's card, and the sound kept for its MP4
 **/
static int16_t *g_voice_pcm; // the note taped last, until the card's job takes it
static size_t   g_voice_n;

static int dictate_begin(int16_t *pcm, size_t n);


/**
 * a tape of room samples wiped (it may hold a dictation) and freed
 **/
static void
tape_free(int16_t *tape, size_t room) {
  if(tape) {
    memset(tape, 0, room * sizeof *tape);
    mem_free(tape);
  }
}

static void
tape_voice(int *dirty) {
  static int16_t *tape;
  static int on, shown = MIC_OFF;
  const size_t room = (size_t)VOICE_RATE * VOICE_MAX_SECONDS;
  float bars[CARD_VOICE_BARS];
  mic_status_t status;
  int retry, want, user, ret;
  size_t n;

  if(ui_voice_take()) {
    n = on ? mic_tape_end() : 0;
    if(on) {
      mic_stop();
      on = 0;
    }
    shown = MIC_OFF;
    log_line("voice: %zu samples taped", n);
    if(ui_voice_dictating()) {
      // the words, made by the dictation payload: the job takes the tape
      n = n < DICT_MAX_SAMPLES ? n : DICT_MAX_SAMPLES;
      if(tape && n >= (size_t)VOICE_RATE * VOICE_MIN_SECONDS && !dictate_begin(tape, n)) {
        tape = NULL;
      } else {
        ui_dictated(DICT_FAILED, NULL);
      }
      tape_free(tape, room);
      tape = NULL;
      *dirty = 1;
      return;
    }
    mem_free(g_voice_pcm);
    g_voice_pcm = NULL;
    if(tape && n >= (size_t)VOICE_RATE * VOICE_MIN_SECONDS) {
      voice_bars(tape, n, bars, CARD_VOICE_BARS);
      if(ui_voice_card(bars, CARD_VOICE_BARS, (int)(n / VOICE_RATE))) {
        g_voice_pcm = tape;
        g_voice_n = n;
        tape = NULL;
      }
    } else {
      ui_voice_card(NULL, 0, 0);
    }
    tape_free(tape, room);
    tape = NULL;
    *dirty = 1;
    return;
  }
  want = ui_voice_wanted(&retry);
  if(on && (!want || retry)) {
    mic_tape_end();
    mic_stop();
    on = 0;
  }
  if(!want && tape) {
    tape_free(tape, room);
    tape = NULL;
  }
  if(want && !on && !g_mic_on) {
    if(!tape && !(tape=mem_alloc(room * sizeof *tape))) {
      log_line("voice: no memory for the tape");
      ui_voice_heard(MIC_FAILED, 0, 0, 0, 0);
      *dirty = 1;
      return;
    }
    if((ret=sceUserServiceGetInitialUser(&user))) {
      log_line("voice: no user 0x%08x, the system's then", (unsigned)ret);
      user = 0xff;
    }
    if(!mic_start(user, log_line)) { // else the last one still ends: the next frame
      mic_tape(tape, room);
      on = 1;
    }
  }
  if(!want && shown == MIC_OFF) {
    return;
  }
  mic_get(&status);
  ui_voice_heard(status.state, status.code, status.silent, status.level, on ? mic_taped() : 0);
  *dirty |= want || status.state != shown;
  shown = want ? status.state : MIC_OFF;
}


/*
 * The DualSense's motors and adaptive triggers, for what the screen says
 * (ui_rumble): a pattern of steps, each the strength of the two motors for
 * some milliseconds; the triggers push back for a moment at a list's end.
 */

// the SDK's ScePadTriggerEffectParam: which triggers (L2 1, R2 2), then a
// command for each, its mode and 48 bytes of what the mode takes
enum { PAD_TRIGGER_OFF, PAD_TRIGGER_FEEDBACK };
typedef struct {
  uint8_t mask;
  uint8_t pad[7];
  struct {
    int32_t mode;
    uint8_t pad[4];
    uint8_t data[48]; // FEEDBACK: the place it starts (0 to 9) and its strength (1 to 8)
  } command[2];
} pad_triggers_t;
_Static_assert(sizeof(pad_triggers_t) == 120, "ScePadTriggerEffectParam is 120 bytes");

typedef struct {
  uint8_t large, small;
  uint16_t ms;
} rumble_step_t;

static const rumble_step_t g_tick[] = {{0, 90, 35}};
static const rumble_step_t g_news[] = {{140, 40, 70}, {0, 0, 90}, {140, 40, 70}};
static const rumble_step_t g_sent[] = {{0, 60, 40}, {0, 0, 40}, {40, 110, 50}, {0, 0, 40}, {120, 160, 70}};
static const rumble_step_t g_edge[] = {{60, 0, 60}};
static const rumble_step_t g_error[] = {{200, 60, 160}};

static void
set_triggers(int strength) {
  static int logged;
  pad_triggers_t t;
  int ret;

  memset(&t, 0, sizeof t);
  t.mask = 1 | 2;
  for(int i=0; i<2; i++) {
    t.command[i].mode = strength ? PAD_TRIGGER_FEEDBACK : PAD_TRIGGER_OFF;
    t.command[i].data[0] = 0;
    t.command[i].data[1] = (uint8_t)strength;
  }
  if((ret=scePadSetTriggerEffect(g_pad, &t)) && !logged++) {
    log_line("pad: triggers 0x%08x", (unsigned)ret);
  }
}

/**
 * every frame: the next step of the pattern under way, or a new one
 **/
static void
rumble_pad(void) {
  static const rumble_step_t *steps;
  static int count, at, logged, triggers;
  static unsigned long until, triggers_until;
  unsigned long now = (unsigned long)sceKernelGetProcessTime();
  uint8_t motors[2];
  int r, ret;

  r = ui_rumble();
  if(g_pad < 0 || g_pad_taken) {
    steps = NULL;
    return;
  }
  if(r != UI_RUMBLE_NONE) {
    steps = r == UI_RUMBLE_TICK ? g_tick : r == UI_RUMBLE_NEWS ? g_news : r == UI_RUMBLE_SENT ? g_sent :
            r == UI_RUMBLE_EDGE ? g_edge : g_error;
    count = r == UI_RUMBLE_TICK ? 1 : r == UI_RUMBLE_NEWS ? 3 : r == UI_RUMBLE_SENT ? 5 : 1;
    at = -1;
    until = 0;
    if(r == UI_RUMBLE_EDGE) {
      set_triggers(6);
      triggers = 1;
      triggers_until = now + 350000;
    }
  }
  if(triggers && now >= triggers_until) {
    set_triggers(0);
    triggers = 0;
  }
  if(!steps || now < until) {
    return;
  }
  if(++at >= count) {
    motors[0] = motors[1] = 0;
    steps = NULL;
  } else {
    motors[0] = steps[at].large;
    motors[1] = steps[at].small;
    until = now + steps[at].ms * 1000ul;
  }
  if((ret=scePadSetVibration(g_pad, motors)) && !logged++) {
    log_line("pad: vibration 0x%08x", (unsigned)ret);
  }
}


/**
 * the controller's light bar in the colour the screen gives it (ui_light),
 * when that changed, or the PS5's own again
 **/
static void
light_pad(void) {
  static int lit = -1, logged;
  static unsigned char was[3];
  unsigned char rgb[3], param[4] = {0, 0, 0, 0};
  int on, d = 0, ret;

  if(g_pad < 0) {
    lit = -1;
    return;
  }
  on = ui_light(rgb);
  for(int c=0; c<3; c++) {
    d += rgb[c] > was[c] ? rgb[c] - was[c] : was[c] - rgb[c];
  }
  if(on == lit && (!on || d < 6)) {
    return;
  }
  if(on) {
    memcpy(param, rgb, 3);
    ret = scePadSetLightBar(g_pad, param);
    memcpy(was, rgb, 3);
  } else {
    ret = scePadResetLightBar(g_pad);
  }
  if(ret && !logged++) {
    log_line("pad: light 0x%08x", (unsigned)ret);
  }
  lit = on;
}


/**
 * the keyboard for kind: a search, or the composer's text, which it starts
 * with, for a reply to user's post or (user empty) a post of the user's
 * own; returns 0 or why it did not open
 **/
static int
open_keyboard(int kind, const char *user) {
  static char text[X_REPLY_MAX + 1];
  capture_t media[UI_MEDIA_MAX];
  char title[128];
  int id, ret;

  if((ret=sceUserServiceGetInitialUser(&id))) {
    log_line("keyboard: no user 0x%08x", (unsigned)ret);
    return ret;
  }
  if(kind == UI_ACT_SEARCH) {
    return ime_open(id, ui_text("Pesquisar no X", "Search X"),
                    ui_text("Palavras, #temas ou @pessoas", "Words, #topics or @people"), NULL,
                    SEARCH_CHARS);
  }
  if(kind == UI_ACT_XCHAT_PIN) {
    // masked, like a password: the PIN is never shown or kept
    return ime_open_field(id, ui_text("PIN do X Chat", "X Chat PIN"), "", NULL, 64, IME_FIELD_PASSWORD);
  }
  if(kind == UI_ACT_DM_TEXT) {
    ui_dm_get(text, sizeof text);
    if(user[0]) {
      snprintf(title, sizeof title, ui_text("Mensagem para @%s", "Message to @%s"), user);
    } else {
      snprintf(title, sizeof title, "%s", ui_text("Nova mensagem", "New message"));
    }
    ret = ime_open(id, title, ui_text("Escreve uma mensagem", "Start a message"), text, 1000);
    memset(text, 0, sizeof text);
    return ret;
  }
  if(kind == UI_ACT_GROK_TEXT) {
    return ime_open(id, ui_text("Pedir uma dica ao Grok", "Ask Grok for a hint"),
                    ui_text("Onde est\xc3\xa1s encravado?", "Where are you stuck?"), NULL, 140);
  }
  if(kind == UI_ACT_POLL_TEXT) {
    int which = ui_poll_choice(text, sizeof text);
    snprintf(title, sizeof title, ui_text("Op\xc3\xa7\xc3\xa3o %d da sondagem", "Poll choice %d"), which);
    ret = ime_open(id, title, ui_text("At\xc3\xa9 25 caracteres", "Up to 25 characters"), text, 25);
    memset(text, 0, sizeof text);
    return ret;
  }
  ui_compose_get(text, sizeof text, media, 0);
  if(user[0]) {
    snprintf(title, sizeof title, ui_text("Responder a @%s", "Reply to @%s"), user);
  } else {
    snprintf(title, sizeof title, "%s", ui_text("Novo post", "New post"));
  }
  ret = ime_open(id, title, user[0] ? ui_text("Publica a tua resposta", "Post your reply") :
                 ui_text("O que est\xc3\xa1 a acontecer?!", "What is happening?!"), text, ui_compose_max());
  memset(text, 0, sizeof text);
  return ret;
}


/*
 * The X notifications payload (src/notify.c), which the installer puts beside
 * the eboot. When it does not run yet, the title hands it to the ELF loader on
 * 127.0.0.1, which runs it as a process of its own: it keeps reading the
 * notifications after the title closes, and shows them over the game. A copy
 * that runs reads the settings again when told to, lists and reads the
 * PS5's captures for the title, whose sandbox may not have them, and serves
 * the linking from a PC: the title may not listen on a port (its bind gives
 * 0x8041010d, EACCES, on the network and on 127.0.0.1).
 */

#define NOTIFY_ELF  "/app0/notify.elf"
#define NOTIFY_PORT 9047        // where a copy that runs answers (src/notify.c)
#define LOADER_PORT 9021        // the ELF loader of etaHEN
#define NOTIFY_MAX  (8 << 20)  // the most a payload beside the eboot may be

#define NET_AF_INET     2
#define NET_SOCK_STREAM 1
#define NET_SOL_SOCKET  0xffff
#define NET_SO_SNDTIMEO 0x1005
#define NET_SO_RCVTIMEO 0x1006
#define NET_SHUT_WR     1
#define NET_SHUT_RDWR   2

typedef struct net_addr {
  uint8_t  len;
  uint8_t  family;
  uint16_t port; // network order
  uint32_t addr; // network order
  uint16_t vport;
  char     zero[6];
} net_addr_t;

int sceNetSocket(const char *name, int family, int type, int protocol);
int sceNetConnect(int s, const void *addr, unsigned len);
int sceNetSend(int s, const void *buf, size_t len, int flags);
int sceNetRecv(int s, void *buf, size_t len, int flags);
int sceNetSetsockopt(int s, int level, int name, const void *value, unsigned len);
int sceNetShutdown(int s, int how);
int sceNetSocketClose(int s);


/**
 * a connection to that port of 127.0.0.1 that waits that many seconds at
 * most, or a negative error
 **/
static int
local_connect(const char *name, int port, int seconds) {
  int s, ret, usec = seconds * 1000000;
  net_addr_t a;

  if((s=sceNetSocket(name, NET_AF_INET, NET_SOCK_STREAM, 0)) < 0) {
    return s;
  }
  static int told; // said once: the PS5 refused the waits (a wait of 10 s for the loader went on for good)
  const int snd = sceNetSetsockopt(s, NET_SOL_SOCKET, NET_SO_SNDTIMEO, &usec, sizeof usec),
            rcv = sceNetSetsockopt(s, NET_SOL_SOCKET, NET_SO_RCVTIMEO, &usec, sizeof usec);
  if((snd < 0 || rcv < 0) && !__atomic_exchange_n(&told, 1, __ATOMIC_ACQ_REL)) {
    log_line("net: the wait of %d s not set (0x%08x, 0x%08x)", seconds, (unsigned)snd, (unsigned)rcv);
  }
  memset(&a, 0, sizeof a);
  a.len = sizeof a;
  a.family = NET_AF_INET;
  a.port = (uint16_t)(((port & 0xff) << 8) | ((port >> 8) & 0xff));
  a.addr = 0x0100007f; // 127.0.0.1
  if((ret=sceNetConnect(s, &a, sizeof a)) < 0) {
    sceNetSocketClose(s);
    return ret;
  }
  return s;
}


/**
 * a file read whole, or NULL; with open and read, like the session file
 **/
static unsigned char *
read_whole(const char *path, size_t *len) {
  unsigned char *buf;
  off_t size;
  size_t off = 0;
  ssize_t n = 1;
  int fd;

  if((fd=open(path, O_RDONLY)) < 0) {
    return NULL;
  }
  size = lseek(fd, 0, SEEK_END);
  if(size <= 0 || size > NOTIFY_MAX || lseek(fd, 0, SEEK_SET) || !(buf=malloc(size))) {
    close(fd);
    return NULL;
  }
  while(off < (size_t)size && (n=read(fd, buf + off, size - off)) > 0) {
    off += n;
  }
  close(fd);
  if(off != (size_t)size) {
    free(buf);
    return NULL;
  }
  *len = off;
  return buf;
}


static pthread_mutex_t g_notify_lock;
static long long       g_notify_failed; // when loading the payload last did not work
static int             g_notify_on;     // the settings want the notifications, atomic
static int             g_notify_busy;   // notify_main runs, atomic
static int             g_notify_again;  // the settings changed while it ran, atomic
static int             g_payload_ready; // a copy with the captures' commands answered, atomic


/**
 * a line the payload says, up to its '\n'; returns 0 or -1
 **/
static int
recv_line(int s, char *line, size_t size) {
  size_t n = 0;
  char c = 0;

  while(n + 1 < size && sceNetRecv(s, &c, 1, 0) == 1 && c != '\n') {
    line[n++] = c;
  }
  line[n] = 0;
  return c == '\n' ? 0 : -1;
}


/**
 * all of len bytes into buf; returns how many came
 **/
static size_t
recv_all(int s, void *buf, size_t len) {
  size_t got = 0;
  int n;

  while(got < len && (n=sceNetRecv(s, (char *)buf + got, len - got, 0)) > 0) {
    got += (size_t)n;
  }
  return got;
}


/**
 * hand the payload beside the eboot to the ELF loader; returns 0 or -1
 **/
/**
 * len bytes of an ELF to the ELF loader, which runs it; what names it in
 * the klog. 0, or -1
 **/
static int
send_elf(const unsigned char *elf, size_t len, const char *what) {
  char answer[16];
  size_t off;
  int s, ret = 0;

  if((s=local_connect("TwitterPS5 load", LOADER_PORT, 10)) < 0) {
    log_line("%s: loader 0x%08x", what, (unsigned)s);
    return -1;
  }
  for(off=0; off < len; off += (size_t)ret) {
    if((ret=sceNetSend(s, elf + off, len - off, 0)) <= 0) {
      break;
    }
  }
  // the loader runs it once the sending side closes; it says a line then
  sceNetShutdown(s, NET_SHUT_WR);
  ret = off == len ? sceNetRecv(s, answer, sizeof answer, 0) : -1;
  sceNetSocketClose(s);
  log_line("%s: sent %zu of %zu bytes, %s", what, off, len, ret > 0 ? "it started" : "no answer");
  return off == len ? 0 : -1;
}


static int
load_payload(void) {
  unsigned char *elf;
  size_t len;
  int ret;

  if(!(elf=read_whole(NOTIFY_ELF, &len))) {
    log_line("notify: no payload beside the eboot");
    return -1;
  }
  ret = send_elf(elf, len, "notify");
  free(elf);
  return ret;
}


/*
 * Dictation (src/dictproto.h): the voice sheet's tape, once ✕ stops it for a
 * dictation, goes to the dictation payload beside the eboot (src/dictated.c),
 * handed to the ELF loader the first time; its words go into the post
 * (ui_dictated). The payload runs whisper.cpp, which the title cannot (C++
 * with exceptions), and keeps the model's memory out of the title's. The
 * sound is wiped once sent and the words once shown; the log gets counts
 * and codes only. ○ on the sheet closes the connection, which stops the
 * payload's work.
 */

#define DICTATE_ELF  "/app0/dictate.elf"
#define DICTATE_WAIT 90 // seconds for the words, the model's first reading included

static struct {
  int      state;  // FETCH_*, atomic
  int      cancel; // atomic: the sheet let go of it
  int16_t *pcm;    // the tape, n samples
  size_t   n;
  char     lang[DICT_LANG];
  int      status; // DICT_*
  char     text[DICT_TEXT_MAX + 1];
} g_dictate;


static int
dictate_cancelled(void) {
  return __atomic_load_n(&g_dictate.cancel, __ATOMIC_ACQUIRE);
}


/**
 * the dictation's socket made to wait a second at a time, which
 * dictate_send and dictate_recv count (up to DICTATE_WAIT, or until ○): the
 * PS5 refuses local_connect's microseconds (0x80410116), so FreeBSD's
 * struct timeval; whether it took is said once in the klog
 **/
static int
dictate_waits(int s) {
  static int told;
  const struct { int64_t sec, usec; } second = {1, 0};
  const int snd = sceNetSetsockopt(s, NET_SOL_SOCKET, NET_SO_SNDTIMEO, &second, sizeof second),
            rcv = sceNetSetsockopt(s, NET_SOL_SOCKET, NET_SO_RCVTIMEO, &second, sizeof second);

  if(!__atomic_exchange_n(&told, 1, __ATOMIC_ACQ_REL)) {
    log_line("dictate: waits of a second %s (0x%08x, 0x%08x)", snd < 0 || rcv < 0 ? "not set" : "set",
             (unsigned)snd, (unsigned)rcv);
  }
  return s;
}


/**
 * a connection to the payload, handed to the ELF loader first when none
 * answers; a socket, or -1
 **/
static int
dictate_connect(void) {
  unsigned char *elf;
  size_t len;
  int s;

  if((s=local_connect("XPS5 dictate", DICT_PORT, 1)) >= 0) {
    return dictate_waits(s);
  }
  if(!(elf=read_whole(DICTATE_ELF, &len))) {
    log_line("dictate: no payload beside the eboot");
    return -1;
  }
  s = send_elf(elf, len, "dictate");
  free(elf);
  if(s) {
    return -1;
  }
  for(int i=0; i < 50 && !dictate_cancelled(); i++) {
    usleep(100 * 1000);
    if((s=local_connect("XPS5 dictate", DICT_PORT, 1)) >= 0) {
      return dictate_waits(s);
    }
  }
  log_line("dictate: the payload does not answer (0x%08x)", (unsigned)s);
  return -1;
}


/**
 * len bytes to the payload, waiting a second at a time, up to DICTATE_WAIT
 * with the answer's, for as long as the sheet wants them; 0 or -1
 **/
static int
dictate_send(int s, const void *buf, size_t len, int *waited) {
  size_t off = 0;
  int n;

  while(off < len && !dictate_cancelled()) {
    if((n=sceNetSend(s, (const char *)buf + off, len - off, 0)) > 0) {
      off += (size_t)n;
    } else if((unsigned)n != 0x80410123u || ++*waited >= DICTATE_WAIT) { // EAGAIN: the second's wait
      return -1;
    }
  }
  return off == len ? 0 : -1;
}


/**
 * len bytes of the answer, waiting a second at a time, up to DICTATE_WAIT,
 * for as long as the sheet wants them; 0 or -1
 **/
static int
dictate_recv(int s, void *buf, size_t len, int *waited) {
  size_t got = 0;
  int n;

  while(got < len && !dictate_cancelled()) {
    if((n=sceNetRecv(s, (char *)buf + got, len - got, 0)) > 0) {
      got += (size_t)n;
    } else if(((unsigned)n != 0x80410123u && (unsigned)n != 0x8041013cu) || ++*waited >= DICTATE_WAIT) {
      return -1; // closed, broken, or too long
    }
  }
  return got == len ? 0 : -1;
}


/**
 * one request: the tape, then the words; a DICT_* status, or DICT_OLD when
 * the payload that answered is of another version (it goes)
 **/
static int
dictate_ask(int s) {
  unsigned char head[DICT_HEAD], answer[DICT_ANSWER];
  const uint32_t version = DICT_VERSION, n = (uint32_t)g_dictate.n;
  uint32_t theirs, len;
  int32_t status;
  int waited = 0;

  memcpy(head, DICT_MAGIC, 8);
  memcpy(head + 8, &version, 4);
  memcpy(head + 12, g_dictate.lang, DICT_LANG);
  memcpy(head + 12 + DICT_LANG, &n, 4);
  if(dictate_send(s, head, sizeof head, &waited) ||
     dictate_send(s, g_dictate.pcm, g_dictate.n * sizeof *g_dictate.pcm, &waited) ||
     dictate_recv(s, answer, sizeof answer, &waited)) {
    return DICT_FAILED;
  }
  memcpy(&theirs, answer, 4);
  memcpy(&status, answer + 4, 4);
  memcpy(&len, answer + 8, 4);
  if(status == DICT_OLD || theirs != DICT_VERSION) {
    log_line("dictate: the payload is of version %u, the title %u", (unsigned)theirs, DICT_VERSION);
    return DICT_OLD;
  }
  if(len > DICT_TEXT_MAX || (len && dictate_recv(s, g_dictate.text, len, &waited))) {
    return DICT_FAILED;
  }
  g_dictate.text[len] = 0;
  return status;
}


static void *
dictate_main(void *arg) {
  long long began = (long long)time(NULL);
  int s, status = DICT_FAILED;

  (void)arg;
  // twice: a payload of another version goes when asked, and the new one
  // is handed to the loader
  for(int i=0; i < 2 && !dictate_cancelled(); i++) {
    if((s=dictate_connect()) < 0) {
      status = DICT_FAILED;
      break;
    }
    status = dictate_ask(s);
    sceNetSocketClose(s);
    if(status != DICT_OLD) {
      break;
    }
    usleep(500 * 1000); // its port let go of
  }
  memset(g_dictate.pcm, 0, g_dictate.n * sizeof *g_dictate.pcm);
  mem_free(g_dictate.pcm);
  g_dictate.pcm = NULL;
  g_dictate.status = status == DICT_OLD ? DICT_FAILED : status;
  log_line("dictate: %zu samples, status %d, %zu bytes, %lld s%s", g_dictate.n, g_dictate.status,
           g_dictate.status == DICT_OK ? strlen(g_dictate.text) : (size_t)0, (long long)time(NULL) - began,
           dictate_cancelled() ? ", let go of" : "");
  __atomic_store_n(&g_dictate.state, FETCH_DONE, __ATOMIC_RELEASE);
  return NULL;
}


/**
 * the tape of n samples handed to the dictation's thread, which wipes and
 * frees it; 0, or -1 when it did not start (the tape stays the caller's)
 **/
static int
dictate_begin(int16_t *pcm, size_t n) {
  if(__atomic_load_n(&g_dictate.state, __ATOMIC_ACQUIRE) != FETCH_IDLE) {
    log_line("dictate: one is under way");
    return -1;
  }
  g_dictate.pcm = pcm;
  g_dictate.n = n;
  // English, always (the user's choice, 2026-11-21): the tiny model guessed
  // European Portuguese badly and wrote it badly, and the screen's
  // language is no hint of what is spoken. "" would have the payload guess
  memset(g_dictate.lang, 0, sizeof g_dictate.lang);
  memcpy(g_dictate.lang, "en", 2);
  g_dictate.text[0] = 0;
  __atomic_store_n(&g_dictate.cancel, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_dictate.state, FETCH_RUNNING, __ATOMIC_RELEASE);
  if(!start_thread(dictate_main, NULL, 128 << 10)) {
    g_dictate.pcm = NULL;
    __atomic_store_n(&g_dictate.state, FETCH_IDLE, __ATOMIC_RELEASE);
    log_line("dictate: no thread");
    return -1;
  }
  return 0;
}


/**
 * the dictation's words to the sheet once they come, wiped after; the
 * thread told to let go when the sheet did
 **/
static void
dictate_step(int *dirty) {
  const int state = __atomic_load_n(&g_dictate.state, __ATOMIC_ACQUIRE);

  if(state == FETCH_RUNNING && !ui_voice_dictating()) {
    __atomic_store_n(&g_dictate.cancel, 1, __ATOMIC_RELEASE);
  }
  if(state != FETCH_DONE) {
    return;
  }
  if(!dictate_cancelled()) {
    ui_dictated(g_dictate.status, g_dictate.text);
    *dirty = 1;
  }
  memset(g_dictate.text, 0, sizeof g_dictate.text);
  __atomic_store_n(&g_dictate.state, FETCH_IDLE, __ATOMIC_RELEASE);
}


/*
 * XPS5's own updates (src/update.h): update.json read at start and when
 * Settings asks, and the install ELF it names fetched, checked and handed
 * to the ELF loader when Settings asks for it. No X session goes there.
 */
typedef struct updater {
  int            state;   // FETCH_*
  int            install; // fetch and install the one found, not only look
  int            result;  // UI_UPDATE_* once done
  int            error, code;
  update_t       found;
  https_stream_t st;      // the install ELF as it comes, for its percent
} updater_t;

static updater_t    g_updater;
static volatile int g_update_stop;


/**
 * look at update.json, and with u->install fetch and send the ELF
 **/
static void
update_run(updater_t *u) {
  static char url[512];
  https_response_t res;
  unsigned char sha[32];
  int ret;

  u->result = UI_UPDATE_FAILED;
  if(g_https) {
    u->error = ERR_HTTPS;
    u->code = g_https;
    return;
  }
  if((ret=https_get(XPS5_UPDATE_URL, NULL, 0, &res))) {
    log_line("update: 0x%08x", (unsigned)ret);
    u->error = ERR_NETWORK;
    u->code = ret;
    return;
  }
  ret = res.status == 200 ? update_parse(res.body, res.len, &u->found) : -1;
  log_line("update: HTTP %d, %zu bytes, %s", res.status, res.len, ret ? "not read" : "read");
  mem_free(res.body);
  if(ret) {
    u->error = res.status == 200 ? ERR_ANSWER : ERR_HTTP;
    u->code = res.status;
    return;
  }
  if(!update_newer(XPS5_VERSION, u->found.version)) {
    u->result = UI_UPDATE_LATEST;
    return;
  }
  if(!u->install) {
    u->result = UI_UPDATE_FOUND;
    return;
  }
  if(update_file_url(XPS5_UPDATE_URL, u->found.file, url, sizeof url)) {
    u->error = ERR_MEMORY;
    return;
  }
  // the ELF, over verified TLS, then its size and its SHA-256
  u->st.stop = &g_update_stop;
  ret = https_get_stream(url, UPDATE_MAX, &u->st);
  if(ret || u->st.state != HTTPS_STREAM_DONE || u->st.status != 200) {
    log_line("update: the file: 0x%08x, HTTP %d", (unsigned)ret, u->st.status);
    u->error = u->st.status && u->st.status != 200 ? ERR_HTTP : ERR_NETWORK;
    u->code = u->st.status && u->st.status != 200 ? u->st.status : ret;
  } else if(u->st.len != u->found.size) {
    log_line("update: %zu bytes, not %zu", u->st.len, u->found.size);
    u->error = ERR_ANSWER;
  } else {
    xtid_sha256(u->st.body, u->st.len, sha);
    if(memcmp(sha, u->found.sha256, sizeof sha)) {
      log_line("update: the SHA-256 is not the one published");
      u->error = ERR_ANSWER;
    } else if(send_elf((const unsigned char *)u->st.body, u->st.len, "update")) {
      u->error = ERR_NETWORK;
    } else {
      u->result = UI_UPDATE_INSTALLING; // the installer closes XPS5
    }
  }
  mem_free(u->st.body);
  u->st.body = NULL;
}


static void *
update_main(void *arg) {
  updater_t *u = arg;

  update_run(u);
  __atomic_store_n(&u->state, FETCH_DONE, __ATOMIC_RELEASE);
  return NULL;
}


/**
 * look for an update, or with install fetch and install the one found
 **/
static void
start_update(int install) {
  if(!XPS5_UPDATE_URL[0] || __atomic_load_n(&g_updater.state, __ATOMIC_ACQUIRE) != FETCH_IDLE) {
    return;
  }
  memset(&g_updater, 0, sizeof g_updater);
  g_updater.state = FETCH_RUNNING;
  g_updater.install = install;
  ui_update(install ? UI_UPDATE_GETTING : UI_UPDATE_CHECKING, NULL, 0, ERR_NONE, 0);
  if(!start_thread(update_main, &g_updater, 1 << 20)) {
    g_updater.result = UI_UPDATE_FAILED;
    g_updater.error = ERR_MEMORY;
    g_updater.state = FETCH_DONE;
  }
}


/**
 * a command to a copy that runs, with the line it answers into answer, or
 * an empty one; returns whether one answered
 **/
static int
tell_payload(const char *cmd, char *answer, size_t size) {
  int s;

  answer[0] = 0;
  if((s=local_connect("TwitterPS5 ask", NOTIFY_PORT, 2)) < 0) {
    return 0;
  }
  sceNetSend(s, cmd, strlen(cmd), 0);
  recv_line(s, answer, size);
  sceNetSocketClose(s);
  return 1;
}


/**
 * which copy runs, told to read the settings again: 0 none, 1 one from
 * before the captures' commands, 2 one from before "game", 3 one from
 * before "link", and so on: the number of its "on N" answer, which a new
 * install's PAYLOAD_VERSION has to reach (a plain "on" is the first)
 **/
#define PAYLOAD_VERSION 27
static int
payload_version(void) {
  char answer[16];
  long n;

  if(!tell_payload("reload\n", answer, sizeof answer)) {
    return 0;
  }
  if(strncmp(answer, "on ", 3) || answer[3] < '1' || answer[3] > '9') {
    return 1;
  }
  n = strtol(answer + 3, NULL, 10);
  return n > 0 && n < 1000 ? (int)n : 1;
}


/**
 * whether the payload runs, which then reads the settings again; when it
 * does not and start is set, it is loaded and waited for, and a copy from
 * an older install is stopped first. One at a time: a second copy loaded
 * while one runs would turn the first off
 **/
static int
reach_payload(int start) {
  long long now = (long long)time(NULL);
  char answer[16];
  int ver, ok;

  pthread_mutex_lock(&g_notify_lock);
  if((ver=payload_version()) && ver < PAYLOAD_VERSION) {
    tell_payload("stop\n", answer, sizeof answer);
    for(int i=0; i < 40 && (ver=payload_version()); i++) {
      sceKernelUsleep(250 * 1000); // it stops after the reading it may be in
    }
    log_line("notify: the old copy %s", ver ? "does not stop" : "stopped");
  }
  if(!ver && start && now - g_notify_failed >= 30 && !load_payload()) {
    // it listens before anything else it does
    for(int i=0; i < 20 && (ver=payload_version()) != PAYLOAD_VERSION; i++) {
      sceKernelUsleep(250 * 1000);
    }
  }
  ok = ver == PAYLOAD_VERSION;
  if(!ok && start) {
    g_notify_failed = now;
  }
  __atomic_store_n(&g_payload_ready, ok, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_notify_lock);
  return ok;
}


static void *
notify_main(void *arg) {
  static x_session_t session;
  int ret, on;

  (void)arg;
  do {
    // without a session it would only say so, and the title says it already
    ret = read_session(&session);
    memset(&session, 0, sizeof session);
    on = __atomic_load_n(&g_notify_on, __ATOMIC_RELAXED);
    log_line("notify: %s", reach_payload(on && !ret) ? "runs" : on ? "does not run" : "off");
  } while(__atomic_exchange_n(&g_notify_again, 0, __ATOMIC_ACQ_REL));
  __atomic_store_n(&g_notify_busy, 0, __ATOMIC_RELEASE);
  return NULL;
}


/**
 * after the settings changed, or at the start: the payload reads them, or
 * starts when they want the notifications
 **/
static void
notify_settings(int on) {
  __atomic_store_n(&g_notify_on, on, __ATOMIC_RELAXED);
  if(__atomic_exchange_n(&g_notify_busy, 1, __ATOMIC_ACQ_REL)) {
    __atomic_store_n(&g_notify_again, 1, __ATOMIC_RELEASE);
  } else if(!start_thread(notify_main, NULL, 64 << 10)) {
    __atomic_store_n(&g_notify_busy, 0, __ATOMIC_RELEASE);
    log_line("notify: no thread");
  }
}


/*
 * The PS5's captures: read by the title when its sandbox has them, else by
 * the payload, which is started for it when it does not run.
 */

/**
 * a command to the payload, started first when it does not run, and the
 * first line of its answer; returns the socket for the rest, or a negative
 * error
 **/
static int
ask_payload(const char *cmd, char *line, size_t size) {
  int s = -1;

  if((!__atomic_load_n(&g_payload_ready, __ATOMIC_ACQUIRE) ||
      (s=local_connect("TwitterPS5 captures", NOTIFY_PORT, 20)) < 0) &&
     (!reach_payload(1) || (s=local_connect("TwitterPS5 captures", NOTIFY_PORT, 20)) < 0)) {
    return s < 0 ? s : -1;
  }
  if(sceNetSend(s, cmd, strlen(cmd), 0) != (int)strlen(cmd) || recv_line(s, line, size)) {
    sceNetSocketClose(s);
    return -1;
  }
  return s;
}


static long long
direct_read(const char *path, long long off, void *buf, size_t len, long long *total) {
  long long size, got = 0;
  ssize_t n = 1;
  int fd;

  if((fd=open(path, O_RDONLY)) < 0) {
    return errno > 0 ? -errno : -1;
  }
  size = (long long)lseek(fd, 0, SEEK_END);
  if(size < 0 || lseek(fd, (off_t)off, SEEK_SET) != (off_t)off) {
    close(fd);
    return -1;
  }
  while((size_t)got < len && (n=read(fd, (char *)buf + got, len - (size_t)got)) > 0) {
    got += n;
  }
  close(fd);
  if(total) {
    *total = size;
  }
  return n < 0 ? -1 : got;
}


static long long
payload_read(const char *path, long long off, void *buf, size_t len, long long *total) {
  char cmd[CAPTURE_PATH + 64], line[64];
  long long n = 0, size = 0;
  int s, e = 0;

  if(len > X_UPLOAD_PART || snprintf(cmd, sizeof cmd, "read %lld %zu %s\n", off, len, path) >=
     (int)sizeof cmd) {
    return -1;
  }
  if((s=ask_payload(cmd, line, sizeof line)) < 0) {
    return -1;
  }
  if(sscanf(line, "ok %lld %lld", &n, &size) != 2 || n < 0 || (size_t)n > len) {
    sceNetSocketClose(s);
    return sscanf(line, "err %d", &e) == 1 && e > 0 ? -e : -1;
  }
  if(recv_all(s, buf, (size_t)n) != (size_t)n) {
    n = -1;
  }
  sceNetSocketClose(s);
  if(total) {
    *total = size;
  }
  return n;
}


/*
 * The cards made for a post (title/cards.h), JPEGs kept in memory under
 * paths of their own, which the composer carries as captures: with a game
 * suspended behind it the title may not reach /data (errno 2). A voice
 * note's MP4 is kept the same way.
 */

#define CARD_KEPT   4
#define CARD_PREFIX "/xps5-cards/"
static struct {
  char     path[48];
  uint8_t *data;
  size_t   len;
} g_cards[CARD_KEPT];
static pthread_mutex_t g_cards_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned        g_card_serial;

/**
 * a JPEG, or a voice note's MP4 (kind CAPTURE_MP4), kept under a new path
 * (the oldest kept goes); its path into c
 **/
static void
card_keep(uint8_t *data, size_t len, int kind, capture_t *c) {
  pthread_mutex_lock(&g_cards_lock);
  const unsigned n = g_card_serial++;
  mem_free(g_cards[n % CARD_KEPT].data);
  snprintf(g_cards[n % CARD_KEPT].path, sizeof g_cards[0].path, CARD_PREFIX "%s-%u.%s",
           kind == CAPTURE_MP4 ? "voice" : "card", n + 1, kind == CAPTURE_MP4 ? "mp4" : "jpg");
  g_cards[n % CARD_KEPT].data = data;
  g_cards[n % CARD_KEPT].len = len;
  memset(c, 0, sizeof *c);
  snprintf(c->path, sizeof c->path, "%s", g_cards[n % CARD_KEPT].path);
  pthread_mutex_unlock(&g_cards_lock);
  c->kind = kind;
  c->size = (long long)len;
  c->time = (long long)time(NULL);
}


/**
 * CAPTURE_* of a path a capture is read by: one of the PS5's (captures.h),
 * or a card kept, by the name card_keep gives it; -1 for any other
 **/
static int
kept_kind(const char *path) {
  const char *name;
  size_t n;

  if(!path || strncmp(path, CARD_PREFIX, sizeof CARD_PREFIX - 1)) {
    return captures_kind(path);
  }
  name = path + sizeof CARD_PREFIX - 1;
  n = strlen(name);
  if(n < 5 || n >= sizeof g_cards[0].path - (sizeof CARD_PREFIX - 1) || strchr(name, '/')) {
    return -1;
  }
  return !strcmp(name + n - 4, ".jpg") ? CAPTURE_JPEG : !strcmp(name + n - 4, ".mp4") ? CAPTURE_MP4 : -1;
}


/**
 * capture_read of a card or voice note kept: returns how many bytes, or -2 (ENOENT) when
 * it is not kept any more
 **/
static long long
card_read(const char *path, long long off, void *buf, size_t len, long long *total) {
  long long n = -2;

  pthread_mutex_lock(&g_cards_lock);
  for(int i=0; i < CARD_KEPT; i++) {
    if(g_cards[i].data && !strcmp(g_cards[i].path, path)) {
      n = off >= (long long)g_cards[i].len ? 0 : (long long)g_cards[i].len - off;
      n = n > (long long)len ? (long long)len : n;
      if(n > 0 && buf) {
        memcpy(buf, g_cards[i].data + off, (size_t)n);
      }
      if(total) {
        *total = (long long)g_cards[i].len;
      }
      break;
    }
  }
  pthread_mutex_unlock(&g_cards_lock);
  return n;
}


/**
 * up to len bytes at off of a capture into buf, and its size into *total
 * unless it is NULL; returns how many, or a negative error
 **/
static long long
capture_read(const char *path, long long off, void *buf, size_t len, long long *total) {
  long long n;

  if(kept_kind(path) < 0 || off < 0) {
    return -1;
  }
  if(!strncmp(path, CARD_PREFIX, sizeof CARD_PREFIX - 1)) {
    return card_read(path, off, buf, len, total);
  }
  if((n=direct_read(path, off, buf, len, total)) >= 0) {
    return n;
  }
  return payload_read(path, off, buf, len, total);
}


/*
 * X Chat's encrypted videos, GIFs and voice notes for the player: a post's
 * video_url "file://xchat/<16 hex>" (xchat_media_token) is read here. At its
 * first read its bytes are fetched with the session (they may be private)
 * and opened (xcrypt_media_open); then they go out a part at a time, and are
 * let go of once the last part was read (the player keeps its own copy).
 */
#define XCHAT_MEDIA_PREFIX "xchat/"
#define XCHAT_MEDIA_MAX    (128 << 20) // bytes of an encrypted video, at most

static pthread_mutex_t g_xmedia_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
  char           token[XCHAT_TOKEN];
  unsigned char *data;
  size_t         len;
} g_xmedia;


/**
 * the attachment named token fetched and opened into g_xmedia; 0, or a
 * negative error. Under g_xmedia_lock
 **/
static int
xchat_media_fetch(const char *token) {
  https_response_t res;
  x_session_t session;
  x_request_t req;
  uint8_t key[32];
  char url[256];
  unsigned char *plain = NULL;
  size_t plen = 0;
  int ret;

  mem_free(g_xmedia.data);
  memset(&g_xmedia, 0, sizeof g_xmedia);
  if(xchat_open_media(token, url, sizeof url, key)) {
    log_line("player: X Chat media unknown (not opened in this run)");
    return -2;
  }
  memset(&res, 0, sizeof res);
  memset(&req, 0, sizeof req);
  if((ret=read_session(&session))) {
    xcrypt_wipe(key, sizeof key);
    return ret < 0 ? ret : -1;
  }
  x_request_init(&req, &session);
  ret = https_get_big(url, req.headers, X_HEADER_COUNT, XCHAT_MEDIA_MAX, &res);
  memset(&session, 0, sizeof session);
  memset(&req, 0, sizeof req);
  if(ret || res.status != 200) {
    log_line("player: X Chat media %s %d", ret ? "network" : "HTTP", ret ? ret : res.status);
    mem_free(res.body);
    xcrypt_wipe(key, sizeof key);
    return ret < 0 ? ret : -1;
  }
  if(!res.len || !(plain=mem_alloc(res.len)) ||
     xcrypt_media_open(key, (const uint8_t *)res.body, res.len, plain, res.len, &plen) || !plen ||
     xchat_open_check(url, plain, plen)) {
    log_line("player: X Chat media (%zu bytes) did not open, or not as its message says", res.len);
    mem_free(plain);
    mem_free(res.body);
    xcrypt_wipe(key, sizeof key);
    return -1;
  }
  log_line("player: X Chat media opened, %zu KiB", plen >> 10);
  mem_free(res.body);
  xcrypt_wipe(key, sizeof key);
  snprintf(g_xmedia.token, sizeof g_xmedia.token, "%s", token);
  g_xmedia.data = plain;
  g_xmedia.len = plen;
  return 0;
}


/**
 * capture_read of X Chat's media (path its token): fetched and opened at
 * its first read (off 0)
 **/
static long long
xchat_media_read(const char *token, long long off, void *buf, size_t len, long long *total) {
  long long n;
  int ret;

  if(off < 0) {
    return -1;
  }
  pthread_mutex_lock(&g_xmedia_lock);
  if(!g_xmedia.data || strcmp(g_xmedia.token, token)) {
    if(off || (ret=xchat_media_fetch(token))) {
      pthread_mutex_unlock(&g_xmedia_lock);
      return off ? -2 : ret;
    }
  }
  n = off >= (long long)g_xmedia.len ? 0 : (long long)g_xmedia.len - off;
  n = n > (long long)len ? (long long)len : n;
  if(n > 0 && buf) {
    memcpy(buf, g_xmedia.data + off, (size_t)n);
  }
  if(total) {
    *total = (long long)g_xmedia.len;
  }
  if(buf && off + n >= (long long)g_xmedia.len) {
    mem_free(g_xmedia.data); // all of it read
    memset(&g_xmedia, 0, sizeof g_xmedia);
  }
  pthread_mutex_unlock(&g_xmedia_lock);
  return n;
}


/**
 * what the player reads by "file://": X Chat's media, or a capture
 **/
static long long
media_read(const char *path, long long off, void *buf, size_t len, long long *total) {
  if(!strncmp(path, XCHAT_MEDIA_PREFIX, sizeof XCHAT_MEDIA_PREFIX - 1)) {
    return xchat_media_read(path, off, buf, len, total);
  }
  return capture_read(path, off, buf, len, total);
}


/**
 * the first len bytes of a capture into buf, a part at a time; returns how
 * many came, or a negative error
 **/
static long long
capture_load(const char *path, void *buf, long long len) {
  long long off = 0, n = 0, part;

  while(off < len) {
    part = len - off < X_UPLOAD_PART ? len - off : X_UPLOAD_PART;
    if((n=capture_read(path, off, (char *)buf + off, (size_t)part, NULL)) <= 0) {
      break;
    }
    off += n;
  }
  return n < 0 ? n : off;
}


/**
 * the captures as the payload lists them; returns how many, or -1 with
 * *code set
 **/
static int
payload_list(capture_t *list, int max, int *code) {
  char line[32], *text;
  long long len = 0;
  int s, n;

  if((s=ask_payload("list\n", line, sizeof line)) < 0) {
    *code = s;
    return -1;
  }
  if(sscanf(line, "ok %lld", &len) != 1 || len < 0 || len > (1 << 20) ||
     !(text=mem_alloc((size_t)len + 1))) {
    sceNetSocketClose(s);
    *code = sscanf(line, "err %d", code) == 1 ? *code : -1;
    return -1;
  }
  n = recv_all(s, text, (size_t)len) == (size_t)len ? captures_parse(text, (size_t)len, list, max) : -1;
  sceNetSocketClose(s);
  mem_free(text);
  *code = n < 0 ? -1 : 0;
  return n;
}


/**
 * the game that runs, as the payload says, into id and name (GAME_ID and
 * GAME_NAME bytes); both empty when none does or the payload cannot say
 **/
static void
ask_game(char *id, char *name) {
  char line[GAME_NAME + 48], *space;
  int s;

  id[0] = name[0] = 0;
  if((s=ask_payload("game\n", line, sizeof line)) < 0) {
    return;
  }
  sceNetSocketClose(s);
  if(strncmp(line, "ok ", 3)) {
    return;
  }
  if((space=strchr(line + 3, ' '))) {
    *space = 0;
  }
  if(game_id_ok(line + 3)) {
    snprintf(id, GAME_ID, "%s", line + 3);
    snprintf(name, GAME_NAME, "%s", space ? space + 1 : "");
  }
}


/**
 * the game's new capture the payload offered in a toast, once, into c
 * (path empty for none)
 **/
static void
ask_shot(capture_t *c) {
  char line[CAPTURE_PATH + 64];
  long long when = 0, size = 0;
  int s, at = 0, kind;

  memset(c, 0, sizeof *c);
  if((s=ask_payload("shot\n", line, sizeof line)) < 0) {
    return;
  }
  sceNetSocketClose(s);
  line[strcspn(line, "\r\n")] = 0;
  if(sscanf(line, "ok %lld %lld %n", &when, &size, &at) == 2 && at > 0 &&
     (kind=captures_kind(line + at)) >= 0) {
    snprintf(c->path, sizeof c->path, "%s", line + at);
    c->time = when;
    c->size = size;
    c->kind = kind;
  }
  memset(line, 0, sizeof line);
}


/**
 * the game's new trophy the payload offered in a toast, once: its grade
 * (0 to 3) and name into g, or grade -1
 **/
static void
ask_trophy(game_t *g) {
  char line[192];
  int s, grade = -1, at = 0;

  g->trophy = -1;
  g->trophy_name[0] = 0;
  if((s=ask_payload("trophy\n", line, sizeof line)) < 0) {
    return;
  }
  sceNetSocketClose(s);
  line[strcspn(line, "\r\n")] = 0;
  if(sscanf(line, "ok %d %n", &grade, &at) == 1 && grade >= 0 && grade <= 3) {
    g->trophy = grade;
    snprintf(g->trophy_name, sizeof g->trophy_name, "%s", at > 0 ? line + at : "");
  }
}


/**
 * the rest of the payload's answer on s, to its end (the payload closes
 * it), at most most bytes; mem_alloc'd with a NUL after it, or NULL
 **/
static char *
recv_rest(int s, size_t most, size_t *len) {
  size_t size = 16 << 10, got = 0;
  char *buf = mem_alloc(size + 1), *bigger;
  int n;

  while(buf && (n=sceNetRecv(s, buf + got, size - got, 0)) > 0) {
    got += (size_t)n;
    if(got == size) {
      if(size >= most || !(bigger=mem_alloc(size * 2 + 1))) {
        mem_free(buf);
        return NULL;
      }
      memcpy(bigger, buf, got);
      mem_free(buf);
      buf = bigger;
      size *= 2;
    }
  }
  if(buf) {
    buf[got] = 0;
    *len = got;
  }
  return buf;
}


/**
 * the line of text that starts at *at (cut at its '\n', which goes), and
 * *at moved past it; NULL at the end
 **/
static char *
next_line(char **at) {
  char *line = *at, *end;

  if(!line || !*line) {
    return NULL;
  }
  if((end=strchr(line, '\n'))) {
    *end = 0;
    *at = end + 1;
  } else {
    *at = line + strlen(line);
  }
  line[strcspn(line, "\r")] = 0;
  return line;
}


/**
 * the session of play that ended, once, into g->session (session_ok)
 **/
static void
ask_session(game_t *g) {
  char line[16], *text, *at, *l;
  size_t len = 0;
  int s;

  g->session_ok = 0;
  if((s=ask_payload("session\n", line, sizeof line)) < 0) {
    return;
  }
  text = strcmp(line, "ok") ? NULL : recv_rest(s, 64 << 10, &len);
  sceNetSocketClose(s);
  at = text;
  if((l=next_line(&at)) && !session_parse(l, &g->session)) {
    while((l=next_line(&at)) && strcmp(l, "end")) {
      if(!strncmp(l, "best\t", 5)) {
        session_text(l + 5, g->session.best, sizeof g->session.best);
      } else if(!strncmp(l, "pic\t", 4) && g->session.pic_count < SESSION_PICS && captures_kind(l + 4) >= 0) {
        snprintf(g->session.pics[g->session.pic_count++], sizeof g->session.pics[0], "%s", l + 4);
      }
    }
    g->session_ok = 1;
    log_line("sessions: one came, %d screenshots", g->session.pic_count);
  }
  mem_free(text);
}


/**
 * the sessions of the last seven days, into g->week (week_ok)
 **/
static void
ask_week(game_t *g) {
  const long long now = (long long)time(NULL);
  char line[16], *text = NULL;
  size_t len = 0;
  int s, n = -1;

  memset(&g->week, 0, sizeof g->week);
  g->week_ok = -1;
  if((s=ask_payload("week\n", line, sizeof line)) >= 0) {
    text = strcmp(line, "ok") ? NULL : recv_rest(s, 1 << 20, &len);
    sceNetSocketClose(s);
  }
  if(text) {
    n = week_add_file(&g->week, text, len, now - WEEK_SECONDS, now + 60);
    g->week_ok = 1;
  }
  log_line("sessions: the week's came %d, %d of them", g->week_ok, n);
  mem_free(text);
}


/**
 * the trophies of the game g->id, into g->troph_rows (troph_count)
 **/
static void
ask_trophies(game_t *g) {
  char cmd[GAME_ID + 16], line[48], *text = NULL, *at, *l, *f[6];
  int s, earned = 0, all = 0, n = 0;
  size_t len = 0;

  g->troph_count = TROPHIES_NOT_READ;
  snprintf(cmd, sizeof cmd, "trophies %s\n", g->id);
  if((s=ask_payload(cmd, line, sizeof line)) < 0) {
    return;
  }
  snprintf(g->troph_id, sizeof g->troph_id, "%s", g->id);
  g->troph_at = (long long)time(NULL);
  if(!strcmp(line, "none")) {
    sceNetSocketClose(s);
    g->troph_count = -1;
    log_line("trophies: the game has none listed");
    return;
  }
  if(sscanf(line, "ok %d %d", &earned, &all) == 2 && all > 0 && all <= UI_TROPHIES_MAX &&
     (g->troph_rows=mem_alloc((size_t)all * sizeof *g->troph_rows))) {
    text = recv_rest(s, 1 << 20, &len);
  }
  sceNetSocketClose(s);
  at = text;
  while(text && n < all && (l=next_line(&at)) && strcmp(l, "end")) {
    // id, grade, hidden, earned, name and description, between tabs
    ui_trophy_row_t *r = &g->troph_rows[n];
    int k = 0;
    for(f[k++]=l; k < 6 && (l=strchr(l, '\t')); f[k++]=++l) {
      *l = 0;
    }
    if(k == 6) {
      memset(r, 0, sizeof *r);
      r->id = atoi(f[0]);
      r->grade = atoi(f[1]) & 3;
      r->hidden = atoi(f[2]) != 0;
      r->earned = atoi(f[3]) != 0;
      session_text(f[4], r->name, sizeof r->name);
      session_text(f[5], r->detail, sizeof r->detail);
      n++;
    }
  }
  mem_free(text);
  if(n > 0) {
    g->troph_count = n;
  } else {
    mem_free(g->troph_rows);
    g->troph_rows = NULL;
  }
  log_line("trophies: %d listed, %d earned", n, earned);
}


// the counts of the bar, and what a toast's Open was for, as the payload says
static struct {
  int  state;                    // FETCH_*
  int  seen, back;               // asked with: the lists the user looks at, and back in front,
  char conv[2][X_ID_MAX];        // and the conversation open (its id, its X Chat id), read
  int  ok, notices, chats, open; // the answer; chats: messages; open: 0, 1 the notifications, 2 Chat
  int  news;                     // the open conversation had new messages
  int  want_back;                // came to the front (or started): asked so at once
  long long asked;
} g_badge;


static void *
badge_main(void *arg) {
  char cmd[32 + 2 * X_ID_MAX], line[80], open[16];
  int s, notices, chats, news = 0, got;

  (void)arg;
  g_badge.ok = g_badge.open = g_badge.news = 0;
  snprintf(cmd, sizeof cmd, "badges %d %d %s %s\n", g_badge.seen, g_badge.back,
           g_badge.conv[0][0] ? g_badge.conv[0] : "-", g_badge.conv[1][0] ? g_badge.conv[1] : "-");
  if((s=ask_payload(cmd, line, sizeof line)) >= 0) {
    sceNetSocketClose(s);
    if((got=sscanf(line, "badges %d %d %15s %d", &notices, &chats, open, &news)) >= 3) {
      g_badge.notices = notices;
      g_badge.chats = chats;
      g_badge.open = !strcmp(open, "notifications") ? 1 : !strcmp(open, "chat") ? 2 : 0;
      g_badge.news = got == 4 && news;
      g_badge.ok = 1;
    }
  }
  __atomic_store_n(&g_badge.state, FETCH_DONE, __ATOMIC_RELEASE);
  return NULL;
}


static void *
game_main(void *arg) {
  game_t *g = arg;
  const long long now = (long long)time(NULL);

  ask_game(g->id, g->name);
  g->trophy = -1;
  if(g->share) {
    ask_trophy(g);
    ask_shot(&g->shot);
  }
  ask_session(g);
  g->week_ok = 0;
  if(g->want_week) {
    ask_week(g);
  }
  g->troph_count = TROPHIES_NOT_READ;
  if(game_id_ok(g->id) && (g->want_trophies || g->trophy >= 0 || strcmp(g->id, g->troph_id) ||
                           now - g->troph_at >= TROPHIES_EVERY)) {
    ask_trophies(g);
  }
  __atomic_store_n(&g->state, FETCH_DONE, __ATOMIC_RELEASE);
  return NULL;
}


/*
 * A card the screen drew, turned into a JPEG by a thread of its own; or a
 * voice note's, the picture of its MP4 with the sound taped.
 */

static struct {
  int       state; // FETCH_*
  int       kind;  // UI_CARD_*
  uint32_t *pixels;
  int       w, h, ok;
  int16_t  *pcm;   // a voice note's sound, n samples at VOICE_RATE
  size_t    n;
  capture_t c;
} g_card_job;


/**
 * w x h pixels at the voice note's VOICE_W x VOICE_H, each the average of
 * the area it covers (mem_alloc'd), or NULL
 **/
static uint32_t *
voice_picture(const uint32_t *src, int w, int h) {
  const float sx = (float)w / VOICE_W, sy = (float)h / VOICE_H, area = sx * sy;
  uint32_t *out = mem_alloc((size_t)VOICE_W * VOICE_H * sizeof *out);
  float *acc = mem_alloc((size_t)VOICE_W * 3 * sizeof *acc);

  if(!out || !acc || w < VOICE_W || h < VOICE_H) {
    mem_free(out);
    mem_free(acc);
    return NULL;
  }
  for(int y=0; y < VOICE_H; y++) {
    const float top = y * sy, bottom = top + sy;
    memset(acc, 0, (size_t)VOICE_W * 3 * sizeof *acc);
    for(int r=(int)top; r < h && r < bottom; r++) {
      const float wy = (r + 1 < bottom ? r + 1 : bottom) - (r > top ? r : top);
      const uint32_t *row = src + (size_t)r * (size_t)w;
      for(int x=0; x < VOICE_W; x++) {
        const float left = x * sx, right = left + sx;
        float c[3] = {0, 0, 0};
        for(int i=(int)left; i < w && i < right; i++) {
          const float wx = (i + 1 < right ? i + 1 : right) - (i > left ? i : left);
          c[0] += wx * (float)(row[i] & 0xff);
          c[1] += wx * (float)(row[i] >> 8 & 0xff);
          c[2] += wx * (float)(row[i] >> 16 & 0xff);
        }
        for(int k=0; k<3; k++) {
          acc[x * 3 + k] += wy * c[k];
        }
      }
    }
    for(int x=0; x < VOICE_W; x++) {
      uint32_t p = 0xff000000u;
      for(int k=0; k<3; k++) {
        const float v = acc[x * 3 + k] / area + 0.5f;
        p |= (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v) << (8 * k);
      }
      out[(size_t)y * VOICE_W + (size_t)x] = p;
    }
  }
  mem_free(acc);
  return out;
}


static void *
card_main(void *arg) {
  const int voice = g_card_job.kind == UI_CARD_VOICE;
  uint8_t *data = NULL;
  size_t len = 0;

  (void)arg;
  if(voice) {
    uint32_t *picture = voice_picture(g_card_job.pixels, g_card_job.w, g_card_job.h);
    g_card_job.ok = picture && !voice_mp4(g_card_job.pcm, g_card_job.n, picture, &data, &len);
    mem_free(picture);
    mem_free(g_card_job.pcm);
    g_card_job.pcm = NULL;
  } else {
    g_card_job.ok = !jpeg_encode(g_card_job.pixels, g_card_job.w, g_card_job.h, 92, &data, &len);
  }
  mem_free(g_card_job.pixels);
  g_card_job.pixels = NULL;
  if(g_card_job.ok) {
    card_keep(data, len, voice ? CAPTURE_MP4 : CAPTURE_JPEG, &g_card_job.c);
  }
  log_line("card: %dx%d, %s of %zu bytes %d", g_card_job.w, g_card_job.h, voice ? "a voice note's MP4" : "a JPEG",
           len, g_card_job.ok);
  __atomic_store_n(&g_card_job.state, FETCH_DONE, __ATOMIC_RELEASE);
  return NULL;
}


/**
 * the frame's part of the cards: one made handed to the screen, or one
 * drawn handed to the thread (a voice note's with its sound)
 **/
static void
card_job_step(int *dirty) {
  int kind;

  if(__atomic_load_n(&g_card_job.state, __ATOMIC_ACQUIRE) == FETCH_DONE) {
    ui_card_done(g_card_job.ok ? &g_card_job.c : NULL);
    __atomic_store_n(&g_card_job.state, FETCH_IDLE, __ATOMIC_RELEASE);
    *dirty = 1;
    return;
  }
  if(__atomic_load_n(&g_card_job.state, __ATOMIC_ACQUIRE) != FETCH_IDLE ||
     !(kind=ui_card_take(&g_card_job.pixels, &g_card_job.w, &g_card_job.h))) {
    return;
  }
  g_card_job.kind = kind;
  g_card_job.pcm = NULL;
  if(kind == UI_CARD_VOICE) {
    g_card_job.pcm = g_voice_pcm;
    g_card_job.n = g_voice_n;
    g_voice_pcm = NULL;
  }
  g_card_job.state = FETCH_RUNNING;
  if((kind == UI_CARD_VOICE && !g_card_job.pcm) || !start_thread(card_main, NULL, 256 << 10)) {
    mem_free(g_card_job.pixels);
    g_card_job.pixels = NULL;
    mem_free(g_card_job.pcm);
    g_card_job.pcm = NULL;
    g_card_job.state = FETCH_IDLE;
    ui_card_done(NULL);
    *dirty = 1;
  }
}


static void *
listing_main(void *arg) {
  listing_t *l = arg;
  int n, m, code = 0;

  // the title's own reading first; an empty folder may be its sandbox's
  n = captures_list(l->list, CAPTURES_MAX);
  log_line("captures: %d here (%d folders, %d files, errno %d)", n, captures_stats.folders,
           captures_stats.files, captures_stats.error);
  if(n <= 0) {
    m = payload_list(l->list, CAPTURES_MAX, &code);
    log_line("captures: %d from the payload", m);
    n = m >= 0 ? m : n;
  }
  l->count = n;
  l->error = n < 0 ? ERR_CAPTURE : ERR_NONE;
  l->code = code;
  if(n > 0) {
    ask_game(l->game_id, l->game_name); // its captures come first
  }
  __atomic_store_n(&l->state, FETCH_DONE, __ATOMIC_RELEASE);
  return NULL;
}


static void
start_listing(void) {
  memset(&g_listing, 0, sizeof g_listing);
  g_listing.state = FETCH_RUNNING;
  if(!start_thread(listing_main, &g_listing, 256 << 10)) {
    g_listing.count = -1;
    g_listing.error = ERR_MEMORY;
    g_listing.state = FETCH_DONE;
  }
}


static void
halt(const char *what, int code) {
  log_line("cannot start: %s 0x%08x", what, (unsigned)code);
  for(;;) {
    sceKernelUsleep(1000000);
  }
}


/**
 * where the session file is read from, and whether it reads
 **/
static void
session_info(void) {
  x_session_t session;
  char path[64];
  unsigned present = 0;
  int ret = -3, at_app = 0;
  struct stat st;

  int first_file = 0; // the first account has a session file in /data, empty or not

  for(int i=1; i <= SETTINGS_ACCOUNTS; i++) {
    if(!settings_session_path(i, path, sizeof path) && !stat(path, &st)) {
      first_file |= i == 1;
      if(st.st_size > 0) {
        present |= 1u << (i - 1);
      }
    }
  }
  if(!first_file && !stat(APP_SESSION_PATH, &st) && st.st_size > 0) {
    present |= 1;
  }
  if(!settings_session_path(g_account, path, sizeof path)) {
    ret = x_session_read(&session, path);
  }
  if(ret == -3 && g_account == 1 && !first_file && !(ret=x_session_read(&session, APP_SESSION_PATH))) {
    at_app = 1;
  }
  memset(&session, 0, sizeof session);
  ui_accounts(present);
  ui_session_info(!ret, at_app ? APP_SESSION_PATH : path);
}


/*
 * Linking an account from a PC (src/link.h). The title is a media app,
 * whose sandbox may not listen on a port, so the notify payload serves the
 * XPS5 extension (src/linkserve.c): while the screen is open a thread asks
 * it where the linking is, a few times a second, and the screen shows it.
 * The payload is started for it when it does not run, with the
 * notifications on or off. Neither the cookies, nor the code, nor the PC's
 * address, nor whose account it is go to the klog.
 */

#define LINK_ASK_MS  400 // between the questions to the payload
#define LINK_PATIENT 25  // questions without an answer before the screen says so

_Static_assert(UI_LINK_STARTING == LINK_STARTING && UI_LINK_WAITING == LINK_WAITING &&
               UI_LINK_CHECKING == LINK_CHECKING && UI_LINK_DONE == LINK_DONE &&
               UI_LINK_NO_NETWORK == LINK_NO_NETWORK && UI_LINK_FINE == LINK_FINE &&
               UI_LINK_WRONG_CODE == LINK_WRONG_CODE && UI_LINK_REFUSED == LINK_REFUSED &&
               UI_LINK_UNWRITTEN == LINK_UNWRITTEN, "the screen's states are the payload's");

static struct {
  pthread_mutex_t lock;
  int             running; // the thread, atomic
  int             stop;    // the screen closed, atomic
  int             started; // for the screen open now, so once (main loop's)
  unsigned        screen;  // which screen it is, for the payload, under lock
  int             account; // the account the screen links, under lock
  char            lang[4]; // the language XPS5 shows, for the extension, under lock
  ui_link_t       st;      // what it shows, under lock
} g_link = {.lock = PTHREAD_MUTEX_INITIALIZER};


/**
 * the payload's state line as the screen shows it
 **/
static void
link_shown(const link_state_t *ls, ui_link_t *st) {
  memset(st, 0, sizeof *st);
  st->state = ls->state;
  st->last = ls->last;
  st->wrong = ls->wrong;
  st->renewed = ls->renewed;
  st->refused = ls->refused;
  st->error = ls->error;
  st->account = ls->account;
  snprintf(st->ip, sizeof st->ip, "%s", ls->ip);
  snprintf(st->code, sizeof st->code, "%s", ls->code);
  snprintf(st->user, sizeof st->user, "%s", ls->user);
  st->seen = ls->seen;
}


static void *
link_main(void *arg) {
  char cmd[48], line[160];
  link_state_t ls;
  ui_link_t st;
  int missed = 0, done = 0;

  (void)arg;
  if(!reach_payload(1)) {
    log_line("link: the payload does not run");
    pthread_mutex_lock(&g_link.lock);
    g_link.st.state = UI_LINK_NO_HELPER;
    pthread_mutex_unlock(&g_link.lock);
    __atomic_store_n(&g_link.running, 0, __ATOMIC_RELEASE);
    return NULL;
  }
  log_line("link: asking the payload");
  while(!done && !__atomic_load_n(&g_link.stop, __ATOMIC_ACQUIRE)) {
    pthread_mutex_lock(&g_link.lock);
    snprintf(cmd, sizeof cmd, "link %u %d %s\n", g_link.screen, g_link.account, g_link.lang);
    pthread_mutex_unlock(&g_link.lock);
    if(tell_payload(cmd, line, sizeof line) && !link_state_parse(line, &ls)) {
      missed = 0;
      link_shown(&ls, &st);
      pthread_mutex_lock(&g_link.lock);
      g_link.st = st;
      pthread_mutex_unlock(&g_link.lock);
      // over: an account linked, or no network to wait on
      done = ls.state == LINK_DONE || ls.state == LINK_NO_NETWORK;
    } else if(++missed == LINK_PATIENT) {
      // busy reading the notifications, or gone: the screen says so, and it is asked on
      log_line("link: the payload does not answer");
      pthread_mutex_lock(&g_link.lock);
      g_link.st.state = UI_LINK_NO_HELPER;
      pthread_mutex_unlock(&g_link.lock);
    }
    memset(line, 0, sizeof line);
    memset(&ls, 0, sizeof ls);
    if(!done) {
      sceKernelUsleep(LINK_ASK_MS * 1000);
    }
  }
  tell_payload("unlink\n", line, sizeof line);
  log_line("link: %s", done ? "over" : "closed");
  __atomic_store_n(&g_link.running, 0, __ATOMIC_RELEASE);
  return NULL;
}


/**
 * the linking, each frame: started when its screen opens, told to stop
 * when it closes, and how it goes handed to the screen; returns whether
 * that changed
 **/
static int
link_frame(void) {
  static ui_link_t shown;
  static unsigned screens;
  int want = ui_link_wanted(), running = __atomic_load_n(&g_link.running, __ATOMIC_ACQUIRE);
  ui_link_t st;

  if(!want) {
    if(running) {
      __atomic_store_n(&g_link.stop, 1, __ATOMIC_RELEASE);
    }
    g_link.started = 0;
    return 0;
  }
  if(!g_link.started && !running) {
    g_link.started = 1;
    if(!screens) {
      // a run of its own too, for a payload that outlived the last one
      screens = (unsigned)sceKernelGetProcessTime() ^ (unsigned)time(NULL) << 8;
    }
    pthread_mutex_lock(&g_link.lock);
    memset(&g_link.st, 0, sizeof g_link.st);
    g_link.st.state = UI_LINK_STARTING;
    g_link.screen = ++screens;
    g_link.account = want;
    // the extension's page then speaks it too
    snprintf(g_link.lang, sizeof g_link.lang, "%s", settings_lang_code(ui_lang()));
    pthread_mutex_unlock(&g_link.lock);
    __atomic_store_n(&g_link.stop, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_link.running, 1, __ATOMIC_RELEASE);
    if(!start_thread(link_main, NULL, 128 << 10)) {
      __atomic_store_n(&g_link.running, 0, __ATOMIC_RELEASE);
      pthread_mutex_lock(&g_link.lock);
      g_link.st.state = UI_LINK_NO_HELPER;
      pthread_mutex_unlock(&g_link.lock);
    }
  }
  pthread_mutex_lock(&g_link.lock);
  g_link.account = want;
  st = g_link.st;
  pthread_mutex_unlock(&g_link.lock);
  if(!memcmp(&st, &shown, sizeof st)) {
    return 0;
  }
  if(st.state == UI_LINK_DONE && shown.state != UI_LINK_DONE) {
    session_info(); // the account has a session file now
  }
  shown = st;
  ui_link_status(&st);
  return 1;
}


/*
 * Writing on the phone (src/phone.h): while its window is open, the payload
 * is asked every PHONE_ASK_MS where its server is and what the phone wrote,
 * the draft going with the question until the phone writes.
 */

#define PHONE_ASK_MS   300
#define PHONE_PATIENT  10 // questions without an answer before the window says so

static struct {
  pthread_mutex_t lock;
  int             running, stop; // the thread's, atomic
  unsigned        window;        // the window it serves (ui_phone_wanted)
  unsigned        screen;        // its number for the payload
  int             max;
  char            what[8];
  char            lang[4];
  char            draft[PHONE_TEXT_MAX + 1];
  ui_phone_t      st;
  char            text[PHONE_TEXT_MAX + 1];
  int             fresh;         // text came, and its Send
  int             done;
} g_phonew = {.lock = PTHREAD_MUTEX_INITIALIZER};


/**
 * one question: its state line into st, and the text that follows it into
 * text (size bytes, ended) with its length into len; 0, or -1
 **/
static int
phone_ask(const char *cmd, phone_state_t *st, char *text, size_t size, size_t *len) {
  char line[160];
  int s, ret = 0;

  *len = 0;
  text[0] = 0;
  if((s=local_connect("TwitterPS5 phone", NOTIFY_PORT, 2)) < 0) {
    return -1;
  }
  sceNetSend(s, cmd, strlen(cmd), 0);
  if(recv_line(s, line, sizeof line) || phone_state_parse(line, st, len) || *len >= size ||
     (*len && recv_all(s, text, *len) != *len)) {
    ret = -1;
    *len = 0;
  }
  text[*len] = 0;
  sceNetSocketClose(s);
  return ret;
}


static void *
phone_main(void *arg) {
  static char cmd[96 + 2 * PHONE_TEXT_MAX], hex[2 * PHONE_TEXT_MAX + 2], text[PHONE_TEXT_MAX + 1];
  phone_state_t ps;
  ui_phone_t st;
  unsigned seq = 0;
  size_t len;
  int missed = 0;

  (void)arg;
  if(!reach_payload(1)) {
    log_line("phone: the payload does not run");
    pthread_mutex_lock(&g_phonew.lock);
    g_phonew.st.state = UI_PHONE_NO_HELPER;
    pthread_mutex_unlock(&g_phonew.lock);
    __atomic_store_n(&g_phonew.running, 0, __ATOMIC_RELEASE);
    return NULL;
  }
  log_line("phone: asking the payload");
  while(!__atomic_load_n(&g_phonew.stop, __ATOMIC_ACQUIRE)) {
    pthread_mutex_lock(&g_phonew.lock);
    if(seq || phone_hex(g_phonew.draft, hex, sizeof hex) < 0) {
      snprintf(hex, sizeof hex, "-"); // the phone writes now: no draft
    }
    snprintf(cmd, sizeof cmd, "type %u %u %d %s %s %s\n", g_phonew.screen, seq, g_phonew.max, g_phonew.what,
             g_phonew.lang, hex);
    pthread_mutex_unlock(&g_phonew.lock);
    if(!phone_ask(cmd, &ps, text, sizeof text, &len)) {
      missed = 0;
      memset(&st, 0, sizeof st);
      st.state = ps.state == PHONE_WAITING ? UI_PHONE_WAITING : ps.state == PHONE_NO_NETWORK ?
                 UI_PHONE_NO_NETWORK : UI_PHONE_STARTING;
      st.seen = ps.seen;
      snprintf(st.ip, sizeof st.ip, "%s", ps.ip);
      snprintf(st.token, sizeof st.token, "%s", ps.token);
      pthread_mutex_lock(&g_phonew.lock);
      g_phonew.st = st;
      if(ps.seq && ps.seq != seq) {
        seq = ps.seq;
        memcpy(g_phonew.text, text, len + 1);
        g_phonew.fresh = 1;
        g_phonew.done = ps.done;
      }
      pthread_mutex_unlock(&g_phonew.lock);
    } else if(++missed == PHONE_PATIENT) {
      log_line("phone: the payload does not answer");
      pthread_mutex_lock(&g_phonew.lock);
      g_phonew.st.state = UI_PHONE_NO_HELPER;
      pthread_mutex_unlock(&g_phonew.lock);
    }
    memset(text, 0, sizeof text);
    memset(cmd, 0, sizeof cmd);
    sceKernelUsleep(PHONE_ASK_MS * 1000);
  }
  tell_payload("untype\n", hex, 16);
  memset(hex, 0, sizeof hex);
  log_line("phone: closed");
  __atomic_store_n(&g_phonew.running, 0, __ATOMIC_RELEASE);
  return NULL;
}


/**
 * each frame: the thread started for a new window and stopped when it
 * closes, its state and what the phone wrote handed to the window; returns
 * whether something changed on screen
 **/
static int
phone_frame(void) {
  static char draft[PHONE_TEXT_MAX + 1], text[PHONE_TEXT_MAX + 1];
  static unsigned screens;
  static ui_phone_t shown;
  char what[8];
  unsigned window;
  int max, running = __atomic_load_n(&g_phonew.running, __ATOMIC_ACQUIRE), fresh, done, changed = 0;
  ui_phone_t st;

  window = ui_phone_wanted(draft, sizeof draft, &max, what, sizeof what);
  if(!window || window != g_phonew.window) {
    if(running) {
      __atomic_store_n(&g_phonew.stop, 1, __ATOMIC_RELEASE);
      return 0; // a new window once that one stopped
    }
    if(!window) {
      g_phonew.window = 0;
      return 0;
    }
    if(!screens) {
      screens = (unsigned)sceKernelGetProcessTime() ^ (unsigned)time(NULL) << 8;
    }
    pthread_mutex_lock(&g_phonew.lock);
    g_phonew.window = window;
    g_phonew.screen = ++screens;
    g_phonew.max = max;
    snprintf(g_phonew.what, sizeof g_phonew.what, "%s", what);
    snprintf(g_phonew.lang, sizeof g_phonew.lang, "%s", settings_lang_code(ui_lang()));
    snprintf(g_phonew.draft, sizeof g_phonew.draft, "%s", draft);
    memset(&g_phonew.st, 0, sizeof g_phonew.st);
    g_phonew.fresh = 0;
    pthread_mutex_unlock(&g_phonew.lock);
    memset(&shown, 0, sizeof shown);
    __atomic_store_n(&g_phonew.stop, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_phonew.running, 1, __ATOMIC_RELEASE);
    if(!start_thread(phone_main, NULL, 128 << 10)) {
      __atomic_store_n(&g_phonew.running, 0, __ATOMIC_RELEASE);
      pthread_mutex_lock(&g_phonew.lock);
      g_phonew.st.state = UI_PHONE_NO_HELPER;
      pthread_mutex_unlock(&g_phonew.lock);
    }
  }
  pthread_mutex_lock(&g_phonew.lock);
  st = g_phonew.st;
  fresh = g_phonew.fresh;
  done = g_phonew.done;
  if(fresh) {
    memcpy(text, g_phonew.text, sizeof text);
    g_phonew.fresh = 0;
  }
  pthread_mutex_unlock(&g_phonew.lock);
  if(memcmp(&st, &shown, sizeof st)) {
    shown = st;
    ui_phone_status(&st);
    changed = 1;
  }
  if(fresh) {
    ui_phone_text(text, done);
    memset(text, 0, sizeof text);
    changed = 1;
  }
  return changed;
}


/*
 * The login window (title/loginlink.h): X's own login page, from the login
 * engine started while the screen has the window open. Its sockets are the
 * PS5's; the key the engine is met with comes from the system's random
 * bytes where the sandbox gives them, mixed with the clocks, through
 * SHA-256. The PS5's keyboard opens for the page's fields; what is typed
 * goes to the page and is wiped here, never logged.
 */

static int
login_connect(int port, int seconds) {
  return local_connect("XPS5 login", port, seconds);
}


static int
login_send(int s, const void *buf, size_t len) {
  return sceNetSend(s, buf, len, 0);
}


static int
login_recv(int s, void *buf, size_t len) {
  return sceNetRecv(s, buf, len, 0);
}


static void
login_shut(int s, int both) {
  sceNetShutdown(s, both ? NET_SHUT_RDWR : NET_SHUT_WR);
}


static void
login_close(int s) {
  sceNetSocketClose(s);
}


static void
login_random(void *buf, size_t len) {
  static unsigned calls;
  struct {
    unsigned char system[32];
    uint64_t      clocks[3];
    const void   *where;
    unsigned      calls, part;
  } seed;
  unsigned char out[32];
  int fd, got = 0;

  memset(&seed, 0, sizeof seed);
  seed.clocks[0] = sceKernelGetProcessTime();
  if((fd=open("/dev/urandom", O_RDONLY)) >= 0) {
    got = read(fd, seed.system, sizeof seed.system) == (ssize_t)sizeof seed.system;
    close(fd);
  }
  seed.clocks[1] = (uint64_t)time(NULL);
  seed.where = &seed;
  seed.calls = ++calls;
  for(size_t off=0; off < len; off += sizeof out) {
    seed.clocks[2] = sceKernelGetProcessTime();
    seed.part++;
    xtid_sha256(&seed, sizeof seed, out);
    memcpy((unsigned char *)buf + off, out, len - off < sizeof out ? len - off : sizeof out);
  }
  memset(&seed, 0, sizeof seed);
  memset(out, 0, sizeof out);
  if(calls == 1) {
    log_line("login: system random bytes %s", got ? "read" : "not there, the clocks alone");
  }
}


static const login_net_t g_login_net = {
  login_connect, login_send, login_recv, login_shut, login_close, login_random, log_line,
};

static struct {
  int        started; // for the window open now
  int        typing;  // the PS5's keyboard is up for a field: its LOGIN_FIELD_*
  int        no_field;
  ui_login_t shown;
} g_login;


/*
 * X's session from the login window (LOGIN_SESSION): tried on X on a thread
 * of its own (whose profile it opens), then kept in the account the window
 * signs in, as the linking's payload keeps one (src/linkserve.c
 * write_session: a new file put in place of the old). Neither the cookies
 * nor whose account it is go to the klog.
 */

enum { LOGIN_CHECK_IDLE, LOGIN_CHECK_RUNNING, LOGIN_CHECK_DONE };

#define SCREEN_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_"

static struct {
  int         state;    // LOGIN_CHECK_*, atomic
  int         account;  // the account it goes into
  int         status;   // once done: 0, X's HTTP status, UI_SESSION_NO_NETWORK or UI_SESSION_UNWRITTEN
  char        user[16]; // once done and taken: whose it is
  x_session_t session;  // for the thread, which wipes it
} g_login_check;


/**
 * the session tried on X: 0 and whose it is (a screen name) into user, or
 * X's HTTP status, or UI_SESSION_NO_NETWORK (notify.c's linkserve_check)
 **/
static int
login_try(const x_session_t *session, char *user, size_t size) {
  static char url[8192];
  static x_user_t profile;
  x_request_t request;
  https_response_t res;
  json_doc_t *doc;
  size_t off;
  int ret;

  user[0] = 0;
  if(x_user_url(url, sizeof url, NULL)) {
    return UI_SESSION_NO_NETWORK;
  }
  x_request_init(&request, session);
  ret = https_get(url, request.headers, X_HEADER_COUNT, &res);
  memset(&request, 0, sizeof request);
  if(ret) {
    log_line("login: X not reached, 0x%08x", (unsigned)ret);
    return UI_SESSION_NO_NETWORK;
  }
  if(res.status != 200) {
    mem_free(res.body);
    return res.status;
  }
  ret = 200; // an answer without the profile: X did not take it either
  if((doc=json_parse(res.body, res.len, &off))) {
    memset(&profile, 0, sizeof profile);
    if(!x_user_parse(json_root(doc), &profile) && profile.user[0]) {
      snprintf(user, size, "%s", profile.user);
      ret = 0;
    }
    memset(&profile, 0, sizeof profile);
    json_free(doc);
  }
  mem_free(res.body);
  return ret;
}


/**
 * the session into the account's file, through a new file put in its
 * place; 0, or -1
 **/
static int
login_write(int account, const x_session_t *session) {
  char path[64], tmp[80], text[512];
  int fd, n, ret = 0;

  if(settings_session_path(account, path, sizeof path) ||
     (n=link_session_text(text, sizeof text, session->auth_token, session->ct0)) < 0) {
    return -1;
  }
  snprintf(tmp, sizeof tmp, "%s.new", path);
  mkdir("/data/TwitterPS5", 0777);
  if((fd=open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
    memset(text, 0, sizeof text);
    return -1;
  }
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
  } else {
    kept_set(account, session);
  }
  return ret;
}


/**
 * an account logged out from the bar's menu (ui_logout_take): its session
 * file left empty, so nothing reads it (the notify payload neither), and
 * for the first account the installer's copy removed too, when the title
 * may (an empty file in /data already keeps the title from reading it).
 * Only the account's number and errno go to the klog
 **/
static void
login_forget(int account) {
  char path[64];
  int fd, err = 0;

  if(settings_session_path(account, path, sizeof path)) {
    return;
  }
  kept_set(account, NULL); // logged out here even when the file cannot be emptied
  if((fd=open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
    err = errno;
  } else {
    close(fd);
  }
  if(account == 1 && unlink(APP_SESSION_COPY) && errno != ENOENT) {
    log_line("login: the installer's copy of account 1 stays (errno %d)", errno);
  }
  log_line("login: account %d logged out%s (errno %d)", account, err ? ", its file stays" : "", err);
  session_info();
}


static void *
login_check_main(void *arg) {
  char user[64];
  int status;

  (void)arg;
  status = login_try(&g_login_check.session, user, sizeof user);
  if(!status && login_write(g_login_check.account, &g_login_check.session)) {
    status = UI_SESSION_UNWRITTEN;
  }
  memset(&g_login_check.session, 0, sizeof g_login_check.session);
  g_login_check.status = status;
  snprintf(g_login_check.user, sizeof g_login_check.user, "%.*s", (int)strspn(user, SCREEN_CHARS), user);
  memset(user, 0, sizeof user);
  __atomic_store_n(&g_login_check.state, LOGIN_CHECK_DONE, __ATOMIC_RELEASE);
  return NULL;
}


/**
 * a session the window sent, len bytes of text: tried on X, unless one is
 * being tried already
 **/
static void
login_take(int account, const char *text, size_t len) {
  int ret;

  if(__atomic_load_n(&g_login_check.state, __ATOMIC_ACQUIRE) != LOGIN_CHECK_IDLE) {
    log_line("login: a session while one is tried, left");
    return;
  }
  if((ret=x_session_parse(&g_login_check.session, text, len))) {
    memset(&g_login_check.session, 0, sizeof g_login_check.session);
    log_line("login: the session misses a cookie (%d)", ret);
    return;
  }
  g_login_check.account = account;
  __atomic_store_n(&g_login_check.state, LOGIN_CHECK_RUNNING, __ATOMIC_RELEASE);
  if(!start_thread(login_check_main, NULL, 1 << 20)) {
    memset(&g_login_check.session, 0, sizeof g_login_check.session);
    __atomic_store_n(&g_login_check.state, LOGIN_CHECK_IDLE, __ATOMIC_RELEASE);
    log_line("login: no thread to try the session");
    return;
  }
  log_line("login: account %d tried on X", account);
  ui_login_session(account, UI_SESSION_CHECKING, NULL);
}


/**
 * the PS5's keyboard for a field of the page, LOGIN_FIELD_* kind, starting
 * with its text (none for a password); 0 or why not
 **/
static int
login_keyboard(int kind, const char *text) {
  int id, ret;

  if((ret=sceUserServiceGetInitialUser(&id))) {
    return ret;
  }
  switch(kind) {
  case LOGIN_FIELD_PASSWORD:
    return ime_open_field(id, ui_text("Palavra-passe do X", "X password"), "", NULL, 128, IME_FIELD_PASSWORD);
  case LOGIN_FIELD_EMAIL:
    return ime_open_field(id, ui_text("E-mail para o X", "E-mail for X"), "", text, 254, IME_FIELD_EMAIL);
  case LOGIN_FIELD_NUMBER:
    return ime_open_field(id, ui_text("C\xc3\xb3" "digo do X", "Code from X"), "", text, 32, IME_FIELD_NUMBER);
  default:
    return ime_open_field(id, ui_text("Escrever na p\xc3\xa1gina do X", "Type into X's page"), "", text, 254,
                          IME_FIELD_TEXT);
  }
}


/**
 * the login window, each frame: its engine started when it opens and told
 * to stop when it closes, the controller's input sent on, the keyboard for
 * a field the page has in focus, and the page and how it goes handed to the
 * screen; returns whether that changed
 **/
static int
login_frame(void) {
  static char text[LOGIN_TEXT_MAX + 1];
  int w, h, zoom, want = ui_login_wanted(&w, &h, &zoom), changed = 0, kind, ret, pw, ph, fresh;
  const uint32_t *pixels;
  ui_login_input_t in;
  login_link_state_t ls;
  ui_login_t st;

  // a session tried on X: kept or not, the screen told, even with the window closed
  if(__atomic_load_n(&g_login_check.state, __ATOMIC_ACQUIRE) == LOGIN_CHECK_DONE) {
    ret = g_login_check.status;
    log_line("login: account %d %s (%d)", g_login_check.account, !ret ? "signed in" :
             ret == UI_SESSION_UNWRITTEN ? "not written" : ret == UI_SESSION_NO_NETWORK ? "not tried" :
             "refused by X", ret);
    if(!ret) {
      session_info(); // the account has a session file now
    }
    ui_login_session(g_login_check.account, ret, g_login_check.user);
    memset(g_login_check.user, 0, sizeof g_login_check.user);
    __atomic_store_n(&g_login_check.state, LOGIN_CHECK_IDLE, __ATOMIC_RELEASE);
    changed = 1;
  }

  // what was typed for a field goes to the page, then is wiped
  if(g_login.typing && (ret=ime_poll(text, sizeof text)) != IME_RUNNING) {
    log_line("login: keyboard %s", ret == IME_DONE ? "done" : ret == IME_CANCELLED ? "closed" : "failed");
    if(ret == IME_DONE && want) {
      loginlink_send(LOGIN_TEXT, g_login.typing == LOGIN_FIELD_EDIT ? 0 : 1, text, strlen(text));
      if(g_login.typing != LOGIN_FIELD_EDIT) {
        loginlink_send(LOGIN_KEY, LOGIN_KEY_RETURN, NULL, 0); // the keyboard's Go
      }
    }
    memset(text, 0, sizeof text);
    g_login.typing = 0;
    changed = 1;
  }
  if(!want) {
    if(g_login.started) {
      loginlink_stop();
      g_login.started = 0;
      log_line("login: window closed");
    }
    loginlink_free(); // once its thread is done
    return changed;
  }
  if(!g_login.started && !loginlink_running()) {
    g_login.started = 1;
    g_login.no_field = 0;
    memset(&g_login.shown, 0xff, sizeof g_login.shown); // told again below
    log_line("login: window open, %dx%d at %d%%", w, h, zoom);
    if(loginlink_start(&g_login_net, LOGIN_LAUNCHER, w, h, zoom, settings_lang_code(ui_lang()))) {
      log_line("login: not started");
    }
  }

  // the controller's input, in the order done
  while(ui_login_input(&in)) {
    login_point_t p = {in.x, in.y};
    login_wheel_t wh = {in.x, in.y, 0, in.d};
    switch(in.kind) {
    case UI_LOGIN_MOVE:
      loginlink_send(LOGIN_POINTER, LOGIN_MOVE, &p, sizeof p);
      break;
    case UI_LOGIN_CLICK:
      loginlink_send(LOGIN_POINTER, LOGIN_DOWN, &p, sizeof p);
      loginlink_send(LOGIN_POINTER, LOGIN_UP, &p, sizeof p);
      break;
    case UI_LOGIN_WHEEL:
      loginlink_send(LOGIN_WHEEL, 0, &wh, sizeof wh);
      break;
    case UI_LOGIN_KEY:
      loginlink_send(LOGIN_KEY, (uint16_t)in.d, NULL, 0);
      break;
    case UI_LOGIN_FIELD:
      loginlink_send(LOGIN_ASK_FIELD, 0, NULL, 0);
      break;
    case UI_LOGIN_RETRY:
      // the page again while the engine runs, else the engine again
      if(loginlink_running()) {
        loginlink_send(LOGIN_RELOAD, 0, NULL, 0);
      } else {
        ui_login_pixels(NULL, 0, 0); // the screen lets go of the page first
        loginlink_free();
        g_login.started = 0;
      }
      break;
    }
  }

  // a field the page has in focus: the keyboard for it
  if(!g_login.typing && loginlink_field(&kind, text, sizeof text)) {
    if((kind & 0xff) == LOGIN_FIELD_NONE) {
      g_login.no_field += (kind & LOGIN_FIELD_ASKED) != 0;
    } else if((ret=login_keyboard(kind & 0xff, text))) {
      log_line("login: keyboard 0x%08x", (unsigned)ret);
    } else {
      g_login.typing = kind & 0xff;
    }
    memset(text, 0, sizeof text);
  }

  // X's session once the page signed in: tried on X, then kept (login_check_main)
  if((ret=loginlink_session(text, sizeof text)) > 0) {
    login_take(want, text, (size_t)ret);
    memset(text, 0, sizeof text);
  }

  // the page, and how it goes
  if((pixels=loginlink_pixels(&pw, &ph, &fresh)) && fresh) {
    ui_login_pixels(pixels, pw, ph);
    changed = 1;
  }
  loginlink_state(&ls);
  memset(&st, 0, sizeof st);
  if(ls.crashed) {
    st.state = UI_LOGIN_FAILED;
    st.why = UI_LOGIN_CRASHED;
  } else if(ls.state == LOGIN_LINK_FAILED) {
    st.state = UI_LOGIN_FAILED;
    st.why = ls.why == LOGIN_LINK_NO_ENGINE ? UI_LOGIN_NO_ENGINE : ls.why == LOGIN_LINK_NO_READ ?
             UI_LOGIN_NO_READ : ls.why == LOGIN_LINK_NO_LOADER ? UI_LOGIN_NO_LOADER :
             ls.why == LOGIN_LINK_NO_START ? UI_LOGIN_NO_START : UI_LOGIN_LOST;
    st.code = ls.code;
  } else if(ls.state == LOGIN_LINK_SHOWN) {
    st.state = UI_LOGIN_SHOWN;
    if(ls.error && !ls.loading) {
      st.why = UI_LOGIN_PAGE;
      st.code = ls.error;
    }
  } else {
    st.state = UI_LOGIN_STARTING;
  }
  st.loading = ls.loading;
  st.progress = ls.progress;
  st.on_x = ls.on_x;
  st.blocked = ls.blocked;
  st.no_field = g_login.no_field;
  st.keyboard = g_login.typing != 0;
  if(memcmp(&st, &g_login.shown, sizeof st)) {
    g_login.shown = st;
    ui_login_status(&st);
    changed = 1;
  }
  return changed;
}


/*
 * History between runs: the posts opened, as the screen keeps them, in a
 * file of the app's folder on the console (a header, then the posts). A
 * file of another version, or one cut short, is left out.
 */

#define HISTORY_PATH  "/data/TwitterPS5/history.bin"
#define HISTORY_APP_PATH "/app0/history.bin" // when /data is lost, as back from a game
#define HISTORY_MAGIC 0x31485754u // "TWH1"

typedef struct history_head {
  uint32_t magic;
  uint32_t size;  // of an x_post_t
  uint32_t count;
  uint32_t zero;
} history_head_t;


static int
read_all(int fd, void *buf, size_t size) {
  char *p = buf;
  ssize_t n;

  while(size) {
    if((n=read(fd, p, size)) <= 0) {
      return -1;
    }
    p += n;
    size -= (size_t)n;
  }
  return 0;
}


static int
write_all(int fd, const void *buf, size_t size) {
  const char *p = buf;
  ssize_t n;

  while(size) {
    if((n=write(fd, p, size)) <= 0) {
      return -1;
    }
    p += n;
    size -= (size_t)n;
  }
  return 0;
}


/*
 * The people the user follows as XPS5 saw them around (src/seen.h): the
 * account's file, which the payload notes its looks into too, read before
 * the screen notes one of its own and kept after.
 */

/**
 * the account's file into s: 0, also when there is none yet; -1 when it
 * could not be read (/data out of reach, with a game behind)
 **/
static int
seen_read(int account, seen_t *s) {
  char path[64], *text;
  struct stat st;
  ssize_t n;
  size_t len = 0;
  int fd;

  memset(s, 0, sizeof *s);
  if(seen_path(account, path, sizeof path)) {
    return -1;
  }
  if((fd=open(path, O_RDONLY)) < 0) {
    return errno == ENOENT && !stat(SETTINGS_SESSION_DIR, &st) ? 0 : -1;
  }
  if(!(text=mem_alloc(SEEN_MOST))) {
    close(fd);
    return -1;
  }
  while(len < SEEN_MOST && (n=read(fd, text + len, SEEN_MOST - len)) > 0) {
    len += (size_t)n;
  }
  close(fd);
  seen_parse(s, text, len);
  memset(text, 0, SEEN_MOST);
  mem_free(text);
  return 0;
}


/**
 * what the screen noted, kept in the account's file (written beside it,
 * then put in its place); only errno goes to the klog
 **/
static void
save_seen(void) {
  static char out[SEEN_MOST];
  const seen_t *s;
  char path[64], fresh[72];
  long long n;
  int fd, ok = 0;

  if(!ui_seen_get(&s) || seen_path(g_account, path, sizeof path) ||
     snprintf(fresh, sizeof fresh, "%s.new", path) >= (int)sizeof fresh) {
    return;
  }
  if((n=seen_format(s, out, sizeof out)) > 0 && (fd=open(fresh, O_WRONLY | O_CREAT | O_TRUNC, 0666)) >= 0) {
    ok = !write_all(fd, out, (size_t)n);
    close(fd);
    ok = ok && !rename(fresh, path);
    if(!ok) {
      unlink(fresh);
    }
  }
  if(!ok) {
    log_line("seen: not kept (errno %d)", errno);
  }
  memset(out, 0, sizeof out);
}


/**
 * the account's file for the screen, as it starts and when the account
 * changes
 **/
static void
load_seen(void) {
  static seen_t s;

  ui_seen_set(seen_read(g_account, &s) ? NULL : &s);
  log_line("seen: %d people, %d changes", s.count, s.events);
  memset(&s, 0, sizeof s);
}


/**
 * an account logged out: who was seen around goes with its session
 **/
static void
forget_seen(int account) {
  char path[64];

  if(!seen_path(account, path, sizeof path) && unlink(path) && errno != ENOENT) {
    log_line("seen: the file of account %d stays (errno %d)", account, errno);
  }
  if(account == g_account) {
    ui_seen_set(NULL);
  }
}


#define ENDS(f) (memchr((f), 0, sizeof (f)) != NULL)

/**
 * a post of the file is whole: its texts end inside their fields
 **/
static int
history_post_ok(const x_post_t *p) {
  if(p->kind != X_KIND_POST || !p->id[0] || p->media_count < 0 || p->media_count > X_MAX_MEDIA ||
     p->face_count < 0 || p->face_count > X_MAX_FACES || !ENDS(p->id) || !ENDS(p->user) || !ENDS(p->user_id) ||
     !ENDS(p->name) || !ENDS(p->avatar) || !ENDS(p->reposter) || !ENDS(p->reposter_name) ||
     !ENDS(p->quote_user) || !ENDS(p->quote_name) || !ENDS(p->quote_avatar) || !ENDS(p->quote_text) ||
     !ENDS(p->text) || p->poll_count < 0 || p->poll_count > X_POLL_MAX || p->poll_voted < 0 ||
     p->poll_voted > p->poll_count || !ENDS(p->poll_card) || !ENDS(p->poll_name)) {
    return 0;
  }
  for(int i=0; i < X_POLL_MAX; i++) {
    if(!ENDS(p->poll_label[i])) {
      return 0;
    }
  }
  for(int i=0; i < X_MAX_MEDIA; i++) {
    if(!ENDS(p->media[i].url) || !ENDS(p->media[i].video_url) || !ENDS(p->media[i].video_small)) {
      return 0;
    }
  }
  for(int i=0; i < X_MAX_FACES; i++) {
    if(!ENDS(p->faces[i])) {
      return 0;
    }
  }
  return 1;
}


/**
 * History, and Read later (its second tab), as files of whole posts: the
 * one in /data, or in /app0 when /data is lost (as back from a game)
 **/
typedef struct kept_list {
  const char *what;
  const char *paths[2];
  uint32_t    magic;
  int         max;
  int       (*get)(const x_post_t **posts, int *changed);
  void      (*set)(const x_post_t *posts, int count);
} kept_list_t;

static const kept_list_t g_lists[2] = {
  {"history", {HISTORY_PATH, HISTORY_APP_PATH}, HISTORY_MAGIC, UI_HISTORY_MAX, ui_history_get, ui_history_set},
  {"read later", {"/data/TwitterPS5/later.bin", "/app0/later.bin"}, 0x314c5754u /* "TWL1" */, UI_LATER_MAX,
   ui_later_get, ui_later_set},
};


static void
load_kept(const kept_list_t *k) {
  history_head_t head;
  x_post_t *posts = NULL;
  int fd, n = 0, at = settings_newest(k->paths, 2); // the one written last

  if(at < 0 || (fd=open(k->paths[at], O_RDONLY)) < 0) {
    return;
  }
  if(!read_all(fd, &head, sizeof head) && head.magic == k->magic && head.size == sizeof *posts &&
     head.count > 0 && head.count <= (uint32_t)k->max && (posts=mem_calloc(head.count, sizeof *posts)) &&
     !read_all(fd, posts, head.count * sizeof *posts)) {
    for(uint32_t i=0; i < head.count; i++) {
      if(history_post_ok(&posts[i])) {
        posts[n++] = posts[i];
      }
    }
    k->set(posts, n);
  }
  mem_free(posts);
  close(fd);
  log_line("%s: %d posts kept", k->what, n);
}


static void
save_kept(const kept_list_t *k) {
  const x_post_t *posts;
  history_head_t head = {k->magic, sizeof *posts, 0, 0};
  int changed, fd, ok;

  head.count = (uint32_t)k->get(&posts, &changed);
  if(!changed) {
    return;
  }
  mkdir("/data/TwitterPS5", 0777);
  if((fd=open(k->paths[0], O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0 &&
     (fd=open(k->paths[1], O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
    log_line("%s: not kept (%d)", k->what, errno);
    return;
  }
  ok = !write_all(fd, &head, sizeof head) && (!head.count || !write_all(fd, posts, head.count * sizeof *posts));
  close(fd);
  if(!ok) {
    log_line("%s: not kept (%d)", k->what, errno);
  }
}


static void
load_history(void) {
  load_kept(&g_lists[0]);
  load_kept(&g_lists[1]);
}


static void
save_history(void) {
  save_kept(&g_lists[0]);
  save_kept(&g_lists[1]);
}


int
main(void) {
  static const char *const settings_paths[] = {SETTINGS_PATH, SETTINGS_APP_PATH};
  static ui_request_t req;
  static char reply[X_REPLY_MAX + 1];
  char id[sizeof ((x_post_t *)0)->id], user[sizeof ((x_post_t *)0)->user];
  const char *step = "";
  long long sent = -1, now_sent;
  size_t flexible = 0;
  settings_t changed;
  unsigned keys;
  int lang = 0, ret, kind, loaders = 0, dirty = 1, budget, replying = 0, sticks[4];
  int tv_w = 0, tv_h = 0, workers;
  int want_week = 0, want_trophies = 0; // for the game's thread, when it starts next
  int week_card = 0; // the week asked for is for its card
  float scale;
  uint64_t tick, ticked;

  log_line("started");
  pthread_mutex_init(&g_tid_lock, NULL);
  pthread_mutex_init(&g_notify_lock, NULL);
  mem_use(map_flexible, unmap_flexible);
  xcrypt_init(login_random); // X Chat's cryptography (nonces, blinding) from the system random
  xchat_open_install();      // so xchat.c opens the messages once the account's key is in hand
  sceSystemServiceParamGetInt(1, &lang);
  ret = settings_load(&g_settings, settings_paths, 2);
  log_line("settings: file %d, language %d of the PS5's %d", ret, g_settings.lang, lang);
  workers = workers_init(WORKERS);
  if((ret=video_open(g_settings.screen, &step))) {
    halt(step, ret);
  }
  // the layout stays 1920 x 1080 points, drawn at the picture's pixels
  scale = video_height() / 1080.0f;
  ret = video_tv(&tv_w, &tv_h);
  log_line("screen: %dx%d for %s, the TV %dx%d (0x%08x), %d workers", video_width(),
           video_height(), g_settings.screen ? "the setting" : "the TV", tv_w, tv_h,
           (unsigned)ret, workers);
  if(video_refused(&tv_h, &ret, &step)) {
    log_line("screen: %dp refused at %s 0x%08x", tv_h, step, (unsigned)ret);
  }
  g_screen = (text_surface_t){video_pixels(), UI_WIDTH, UI_HEIGHT, scale, 0};
  ui_scale(scale);
  if(ui_init(&g_settings, settings_lang_of_system(lang))) {
    halt("fonts", -1);
  }
  ui_motion(); // XPS5's letters on the way in, and the pages' transitions
  x_set_language(settings_lang_code(ui_lang()));
  {
    // the login window, while its engine is on the console
    struct stat st;
    int there = !stat(LOGIN_LAUNCHER, &st);
    ui_login_ready(there);
    log_line("login: engine %s", there ? "there" : "not there");
  }
  sceUserServiceInitialize(NULL);
  scePadInit();
  ime_set_log(log_line);
  player_init(log_line);
  player_files(media_read); // the composer plays a clip of the PS5's; X Chat's videos and voice notes
  sound_init(log_line);
  space_init(log_line, tell_payload); // the payload plays it when it can: over a game too
  g_account = g_settings.account;
  session_info();
  load_history();
  load_seen();
  xchat_load_key(g_settings.ids[g_account - 1]); // the account's X Chat key, when it was kept

  // the pictures get an eighth of the flexible memory left, up to
  // IMAGE_BUDGET for each 1080p's worth of pixels
  sceKernelAvailableFlexibleMemorySize(&flexible);
  budget = (int)(flexible >> 23);
  ret = (int)(IMAGE_BUDGET * scale * scale);
  budget = budget < 8 ? 8 : budget > ret ? ret : budget;
  log_line("memory: %zu MiB flexible, malloc gives %d MiB, pictures %d MiB", flexible >> 20,
           heap_free(64), budget);
  images_init((size_t)budget << 20);

  if(!(g_https=https_init(X_USER_AGENT, log_text))) {
    for(int i=0; i<LOADERS; i++) {
      loaders += start_thread(loader_main, NULL, 1 << 20);
    }
    log_line("picture loaders: %d", loaders);
    if(g_settings.notify) {
      notify_settings(1);
    }
  }
  // XPS5's updates: looked for once at start, when this build has where
  if(XPS5_UPDATE_URL[0] && !g_https) {
    start_update(0);
  } else {
    ui_update(XPS5_UPDATE_URL[0] ? UI_UPDATE_NONE : UI_UPDATE_OFF, NULL, 0, ERR_NONE, 0);
  }

  g_badge.want_back = 1; // opened by a toast's Open, maybe
  ticked = sceKernelGetProcessTime();
  for(;;) {
    g_frame++;
    // how long the last frame took, for the pointer to keep its speed
    tick = sceKernelGetProcessTime();
    ui_elapsed((unsigned)(tick - ticked < 1000000 ? tick - ticked : 1000000));
    ticked = tick;
    // the keyboard has the controller while it is up
    keys = read_pad(sticks);
    // the PS5's keyboard has the controller too: coming back from it is no toast's Open
    g_pad_keyboard |= g_pad_taken && replying;
    if(!replying && ((keys && ui_keys(keys)) | ui_sticks(sticks[0], sticks[1], sticks[2], sticks[3]))) {
      dirty = 1;
    }
    // a Space in the background goes quiet while a video plays its sound
    {
      player_status_t ps;
      player_status(&ps);
      space_hush(ps.sound && ps.has_audio && (ps.state == PLAYER_PLAYING || ps.state == PLAYER_LOADING));
    }
    // a Space the payload kept playing while XPS5 was closed: the one playing
    {
      x_space_t found;
      if(space_found(&found)) {
        ui_space(&found);
        dirty = 1;
      }
    }
    // the game that runs, now and then, once the payload answers
    // back in front after the game (the PS5 stops an app behind another):
    // the game and a capture it offered, asked at once
    if((long long)time(NULL) - g_game.seen >= 3) {
      g_game.asked = 0;
      g_badge.want_back = 1; // maybe by a toast's Open
    }
    if(g_pad_back) {
      // back from the PS5's control center, which a toast's Open goes through
      g_badge.want_back |= !g_pad_keyboard;
      g_pad_back = g_pad_keyboard = 0;
    }
    // the week's card and the trophies' sheet: asked at once; the week for
    // the card of the user's own profile too
    if(ui_week_wanted()) {
      want_week = week_card = 1;
      g_game.asked = 0;
    }
    if(ui_week_info_wanted()) {
      want_week = 1;
      g_game.asked = 0;
    }
    if(ui_trophies_wanted()) {
      want_trophies = 1;
      g_game.asked = 0;
    }
    // a card drawn: a JPEG (or a voice note's MP4), kept for the composer
    card_job_step(&dirty);
    g_game.seen = (long long)time(NULL);
    if(__atomic_load_n(&g_game.state, __ATOMIC_ACQUIRE) == FETCH_DONE) {
      ui_game(g_game.id, g_game.name);
      if(g_game.trophy >= 0) {
        log_line("trophy offered: grade %d", g_game.trophy);
        ui_trophy(g_game.trophy, g_game.trophy_name);
        g_game.trophy = -1;
      }
      if(g_game.shot.path[0]) {
        log_line("capture offered: kind %d", g_game.shot.kind);
        ui_shot(&g_game.shot);
        memset(&g_game.shot, 0, sizeof g_game.shot);
      }
      if(g_game.troph_count != TROPHIES_NOT_READ) {
        ui_trophies(g_game.troph_id, g_game.troph_rows, g_game.troph_count);
        mem_free(g_game.troph_rows);
        g_game.troph_rows = NULL;
      }
      if(g_game.session_ok) {
        ui_session(&g_game.session);
        g_game.session_ok = 0;
      }
      if(g_game.week_ok) {
        ui_week_info(g_game.week_ok > 0 ? &g_game.week : NULL);
        if(week_card) {
          ui_week(g_game.week_ok > 0 ? &g_game.week : NULL);
          week_card = 0;
        }
        g_game.week_ok = 0;
      }
      __atomic_store_n(&g_game.state, FETCH_IDLE, __ATOMIC_RELEASE);
      dirty = 1;
    } else if(__atomic_load_n(&g_game.state, __ATOMIC_ACQUIRE) == FETCH_IDLE &&
              __atomic_load_n(&g_payload_ready, __ATOMIC_ACQUIRE) &&
              (long long)time(NULL) - g_game.asked >= GAME_EVERY) {
      g_game.asked = (long long)time(NULL);
      g_game.share = g_settings.share;
      g_game.want_week = want_week;
      g_game.want_trophies = want_trophies;
      want_week = want_trophies = 0;
      g_game.state = FETCH_RUNNING;
      if(!start_thread(game_main, &g_game, 64 << 10)) {
        g_game.state = FETCH_IDLE;
      }
    }
    // the bar's counts, often (the payload is on this PS5). Coming back, the
    // app stays where it was left: the PS5 does not say whether a toast's
    // Open brought it, and a toast in the last minute and a half sent the
    // user to Chat each time they came back (2026-11-21), so the payload's
    // guess is only logged
    if(__atomic_load_n(&g_badge.state, __ATOMIC_ACQUIRE) == FETCH_DONE) {
      if(g_badge.ok) {
        ui_badges(g_badge.notices, g_badge.chats, g_badge.news);
        if(g_badge.open) {
          log_line("back after a toast of %s: staying", g_badge.open == 1 ? "notifications" : "chat");
        }
        dirty = 1;
      }
      __atomic_store_n(&g_badge.state, FETCH_IDLE, __ATOMIC_RELEASE);
    } else if(__atomic_load_n(&g_badge.state, __ATOMIC_ACQUIRE) == FETCH_IDLE &&
              __atomic_load_n(&g_payload_ready, __ATOMIC_ACQUIRE) &&
              (g_badge.want_back || (long long)time(NULL) - g_badge.asked >= BADGE_EVERY)) {
      g_badge.asked = (long long)time(NULL);
      g_badge.seen = ui_badges_seen();
      ui_badges_open(g_badge.conv[0], g_badge.conv[1], sizeof g_badge.conv[0]);
      g_badge.back = g_badge.want_back;
      g_badge.state = FETCH_RUNNING;
      if(start_thread(badge_main, &g_badge, 32 << 10)) {
        g_badge.want_back = 0;
      } else {
        g_badge.state = FETCH_IDLE;
      }
    }
    if((ret=ui_logout_take())) {
      // logged out from the bar's menu, before anything is read with that session again
      login_forget(ret);
      forget_seen(ret);
      dirty = 1;
    }
    if(ui_account() != g_account) {
      // another account from the Settings: what is read next is read with its session
      g_account = ui_account();
      log_line("account %d", g_account);
      session_info();
      load_seen();
      xchat_load_key(g_settings.ids[g_account - 1]); // its own X Chat key, or none
    }
    if(__atomic_load_n(&g_fetch.state, __ATOMIC_ACQUIRE) == FETCH_DONE) {
      if(g_fetch.req.feed == UI_DM && g_fetch.party_count) {
        ui_party(g_fetch.req.id, g_fetch.party, g_fetch.party_count);
      }
      if(g_fetch.seen_ok) {
        ui_seen_set(&g_fetch.seen); // as the payload left it, for the look to be noted into
      }
      ui_result(&g_fetch.req, g_fetch.posts, g_fetch.count, g_fetch.next,
                g_fetch.has_user ? &g_fetch.user : NULL, g_fetch.error, g_fetch.code, g_fetch.x_code,
                time(NULL));
      g_fetch.posts = NULL;
      save_seen();
      __atomic_store_n(&g_fetch.state, FETCH_IDLE, __ATOMIC_RELEASE);
      dirty = 1;
    }
    if(__atomic_load_n(&g_fetch.state, __ATOMIC_ACQUIRE) == FETCH_IDLE && ui_wanted(&req)) {
      start_fetch(&req);
      dirty = 1;
    }

    // a like, a repost, a bookmark or a post goes out at once; a search or
    // the composer's text once the keyboard closes; the captures come from
    // a thread
    if(!replying && __atomic_load_n(&g_action.state, __ATOMIC_ACQUIRE) == FETCH_IDLE &&
       __atomic_load_n(&g_listing.state, __ATOMIC_ACQUIRE) == FETCH_IDLE &&
       (kind=ui_action(id, sizeof id, user, sizeof user)) != UI_ACT_NONE) {
      if(kind == UI_ACT_CAPTURES) {
        start_listing();
      } else if(kind != UI_ACT_SEARCH && kind != UI_ACT_COMPOSE_TEXT && kind != UI_ACT_DM_TEXT &&
                kind != UI_ACT_POLL_TEXT && kind != UI_ACT_GROK_TEXT && kind != UI_ACT_XCHAT_PIN) {
        start_action(kind, id, user);
        sent = -1;
      } else if((ret=open_keyboard(kind, user))) {
        log_line("keyboard: 0x%08x", (unsigned)ret);
        ui_action_done(ERR_KEYBOARD, ret, -1);
      } else {
        replying = kind;
      }
      dirty = 1;
    }
    if(replying && (ret=ime_poll(reply, replying == UI_ACT_SEARCH ? X_QUERY_MAX + 1 : sizeof reply)) !=
       IME_RUNNING) {
      log_line("keyboard: %s", ret == IME_DONE ? "done" : ret == IME_CANCELLED ? "closed" : "failed");
      if(ret == IME_DONE && reply[0] && replying == UI_ACT_SEARCH) {
        ui_search(reply); // the fetch thread reads it
      } else if(ret == IME_DONE && replying == UI_ACT_COMPOSE_TEXT) {
        ui_compose_text(reply);
      } else if(ret == IME_DONE && replying == UI_ACT_POLL_TEXT) {
        ui_poll_text(reply);
      } else if(ret == IME_DONE && replying == UI_ACT_DM_TEXT) {
        ui_dm_text(reply); // sent next, when it has words
      } else if(ret == IME_DONE && replying == UI_ACT_GROK_TEXT) {
        ui_grok_text(reply); // asked next, when it has words
      } else if(ret == IME_DONE && replying == UI_ACT_XCHAT_PIN && reply[0]) {
        start_unlock(id, reply); // recovers the X Chat keys on its thread
        sent = -1;
      } else {
        ui_action_done(ret == IME_FAILED ? ERR_KEYBOARD : ERR_CANCELLED, 0, -1);
      }
      replying = 0;
      xcrypt_wipe(reply, sizeof reply); // the PIN is not left in the buffer
      memset(reply, 0, sizeof reply);
      dirty = 1;
    }
    if(__atomic_load_n(&g_listing.state, __ATOMIC_ACQUIRE) == FETCH_DONE) {
      if(g_listing.count > 0) {
        ui_game(g_listing.game_id, g_listing.game_name);
      }
      ui_captures(g_listing.list, g_listing.count, g_listing.error, g_listing.code);
      __atomic_store_n(&g_listing.state, FETCH_IDLE, __ATOMIC_RELEASE);
      dirty = 1;
    }
    if(__atomic_load_n(&g_action.state, __ATOMIC_ACQUIRE) == FETCH_RUNNING && g_action.count &&
       (now_sent=__atomic_load_n(&g_action.sent, __ATOMIC_RELAXED)) != sent) {
      sent = now_sent;
      ui_progress(sent, __atomic_load_n(&g_action.total, __ATOMIC_RELAXED));
      dirty = 1;
    }
    // XPS5's updates: what Settings asks, how far the file came, how it went
    if((ret=ui_update_wanted())) {
      start_update(ret == 2);
      dirty = 1;
    }
    if(__atomic_load_n(&g_updater.state, __ATOMIC_ACQUIRE) == FETCH_RUNNING && g_updater.install &&
       __atomic_load_n(&g_updater.st.state, __ATOMIC_ACQUIRE) == HTTPS_STREAM_READING && g_updater.st.len) {
      static int shown = -1;
      int percent = (int)(__atomic_load_n(&g_updater.st.got, __ATOMIC_ACQUIRE) * 100 / g_updater.st.len);
      if(percent != shown) {
        shown = percent;
        ui_update(UI_UPDATE_GETTING, g_updater.found.version, percent, ERR_NONE, 0);
        dirty = 1;
      }
    }
    if(__atomic_load_n(&g_updater.state, __ATOMIC_ACQUIRE) == FETCH_DONE) {
      ui_update(g_updater.result, g_updater.found.version, 100, g_updater.error, g_updater.code);
      __atomic_store_n(&g_updater.state, FETCH_IDLE, __ATOMIC_RELEASE);
      dirty = 1;
    }
    if(__atomic_load_n(&g_action.state, __ATOMIC_ACQUIRE) == FETCH_DONE) {
      if(g_action.kind == UI_ACT_GROK) {
        ui_grok_answer(g_action.answer);
        memset(g_action.answer, 0, sizeof g_action.answer);
      }
      if(g_action.kind == UI_ACT_SPACE) {
        if(!g_action.error) {
          // its sound from X's video CDN, with no X session (space.h)
          space_play(g_action.answer, g_action.space.state == X_SPACE_LIVE,
                     g_action.space.id[0] ? g_action.space.id : g_action.id, g_action.space.host);
          ui_space(&g_action.space);
        }
        memset(g_action.answer, 0, sizeof g_action.answer);
        memset(&g_action.space, 0, sizeof g_action.space);
      }
      ui_action_done(g_action.error, g_action.code, g_action.x_code);
      memset(g_action.text, 0, sizeof g_action.text);
      __atomic_store_n(&g_action.state, FETCH_IDLE, __ATOMIC_RELEASE);
      dirty = 1;
    }

    // what the Settings screen changed: kept, and told to the payload
    if(ui_settings_changed(&changed)) {
      ret = settings_save(&changed, settings_paths, 2);
      log_line("settings: saved to file %d, language %d, notifications %d every %d s", ret,
               changed.lang, changed.notify, changed.every);
      x_set_language(settings_lang_code(ui_lang()));
      if(!g_https && (changed.notify != g_settings.notify || changed.every != g_settings.every ||
                      changed.lang != g_settings.lang || changed.account != g_settings.account ||
                      changed.rising != g_settings.rising || changed.share != g_settings.share ||
                      memcmp(changed.ids, g_settings.ids, sizeof changed.ids))) {
        notify_settings(changed.notify);
      }
      g_settings = changed;
      dirty = 1;
    }
    // an account linked from a PC, while its screen is open
    if(link_frame()) {
      dirty = 1;
    }
    // what the phone writes, while its window is open
    if(phone_frame()) {
      dirty = 1;
    }
    // the login window, while it is open
    if(login_frame()) {
      dirty = 1;
    }
    if(images_arrived()) {
      ui_pictures_arrived(); // what is under a sheet is drawn again with them
      dirty = 1;
    }
    if(ui_animate() || g_frame % 1800 == 0) { // the ages, every half a minute
      dirty = 1;
    }
    if(g_frame % 180 == 0) { // History, every three seconds when it changed
      save_history();
    }
    if(g_frame % 4 == 0) { // the light bar after the banner
      light_pad();
    }
    rumble_pad(); // its motors, to the millisecond
    if(g_frame % 600 == 300) { // a session file put there by FTP, every ten seconds
      session_info();
    }
    test_mic(&dirty);
    tape_voice(&dirty);
    dictate_step(&dirty);

    if(dirty) {
      float pan;
      ui_draw(&g_screen, time(NULL), g_frame);
      if(ui_hover_take(&pan)) {
        sound_tick(pan); // the pointer lit something up
      }
      video_present();
      dirty = 0;
    } else {
      video_wait();
    }
  }
}
