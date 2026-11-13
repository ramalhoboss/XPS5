/* TwitterPS5 notify: a payload that stays running and shows the user's new
X notifications (likes, reposts, follows, replies, mentions) as PS5 toasts,
over the game too. It reads NotificationsTimeline with the session in
/data/TwitterPS5/session.txt, the same request the title makes, as often as
the title's Settings say (settings.h); see xnotify.h for what counts as new
and how a toast looks. X words the notices in the language of the Settings.

The title (TWPS00001) starts it when it opens, from the copy the installer
puts beside the eboot, so it is on again after a restart of the PS5 once the
app has been opened. A running copy listens on a port of its own on
127.0.0.1, with a line for each command:
  "stop"    turns it off (a copy sent by hand while one runs says it, so
            sending it again turns it off);
  "reload"  reads the settings again (notifications on or off, how often,
            the language);
  "list"    answers "ok <length>\n" and the PS5's captures in the lines of
            captures_format, for the title, whose sandbox may not have them;
  "read <offset> <length> <path>"  answers "ok <length> <size>\n" and up to
            4 MB of a capture (only a path captures_kind allows), or
            "err <errno>\n";
  "game"    answers "ok <title id> <name>\n" for the game that runs (the
            name from its param.json or param.sfo, maybe empty), or
            "none\n";
  "link <screen> <account> <lang>"  serves the linking from a PC for the
            title's screen (src/linkserve.h), which may not listen itself,
            and answers its state line (link_state_line of src/link.h);
  "unlink"  stops it, the screen closed, and answers "ok\n";
  "shot"    answers "ok <time> <size> <path>\n" for a new capture of the
            game that runs, found in the last minutes and offered in a
            toast, once, or "none\n";
  "trophy"  answers "ok <grade> <name>\n" (grade 0 bronze to 3 platinum)
            for a trophy of the game the user just earned, offered in a
            toast, once, or "none\n";
  "uninstall"  gives each profile that shows a game what it said before,
            answers "ok\n" and turns it off (src/uninstall.c);
  "launch <title id or -> <name>"  opens the game of an invite: the one
            with that title id, else the installed one of that name (another
            region's); answers "ok\n", "none\n" when it is not installed,
            "busy\n" while another game is open, or "err <code>\n";
anything else gets "on 11" (11: of the two settings files, the one
written last is read; 10: "uninstall", only games count as the game
open, and a game status left in the profile with no record of it comes
off; 9: the QR's server says each connection in
the klog; 8: the game open also behind XPS5; 7: "launch",
and the game in the profile's location while it runs, see play.h; 6: the
QR's page and its state, "shot" and "trophy"; 5
the user's posts rising; 4 the one before those, 3 the one before "link", 2 the one before
"game", and a copy from before the captures' says only "on"), which is how
the title knows not to start a second one, or to stop an old one first. With the notifications off in the Settings it only
answers these.

Only codes and counts go to the klog; the session, the notifications and
the captures' paths never do. */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
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

#include "captures.h"
#include "game.h"
#include "https.h"
#include "json.h"
#include "linkserve.h"
#include "mem.h"
#include "play.h"
#include "settings.h"
#include "trophy.h"
#include "xapi.h"
#include "xnotify.h"
#include "xsign.h"

#define CONTROL_PORT 9047        // on 127.0.0.1, while a copy runs
#define WAIT_LIMITED (10 * 60)   // after X asked to wait (HTTP 429)
#define SHOW_MAX     3           // toasts a reading, then one that counts the rest
#define READ_COUNT   20
#define READ_MAX     (4 << 20)   // bytes of a capture a "read" gives
#define OPEN_LINK    "psgm:play?id=TWPS00001"
#define RISING_EVERY (15 * 60)   // seconds between two looks at the user's own posts
#define RISING_COUNT 20
#define SHOT_EVERY   10          // seconds between two looks at the game's captures
#define SHOT_FRESH   (5 * 60)    // how long a new capture waits for the title
#define SHOT_TOASTS  60          // seconds at least between two toasts about them
#define PLAY_EVERY   15          // seconds between two looks at the game, for the profile
#define PLAY_RETRY   (2 * 60)    // after X did not take a change of it
#define TID_MAX_AGE  (60 * 60)   // seconds a page's key signs for
#define TID_RETRY    60          // after a page that gave none

#define SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM 0xFE

int sceNotificationSend(int user, bool logged, const char *payload);
int sceSystemServiceParamGetInt(int id, int *value);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int app_id, char *title_id);

typedef struct lnc_app_param {
  uint32_t size;
  int32_t  user_id;
  uint32_t app_opt;
  uint64_t crash_report;
  uint32_t check_flag;
} lnc_app_param_t;

int sceLncUtilResumeApp(int app_id, int cause);
int sceLncUtilLaunchApp(const char *title_id, const char **argv, lnc_app_param_t *param);
int sceLncUtilGetAppId(const char *title_id);
int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(int *user_id);

// what the payload says itself, in the order of LANG_* (settings.h)
enum {
  T_OPEN, T_NO_SESSION, T_REFUSED, T_COPY_AGAIN, T_MORE, T_OFF, T_SEND_AGAIN, T_ON, T_ON_BODY,
  T_OFF_SETTINGS, T_RISING, T_SHOT_PHOTO, T_SHOT_VIDEO, T_BRONZE, T_SILVER, T_GOLD, T_PLATINUM, TEXTS
};

static const char *const g_texts[LANGS][TEXTS] = {
  {"Open", "XPS5: the X session is missing", "XPS5: X did not accept the session",
   "Link it again from a PC: XPS5, Settings, X account.", "%d more notifications on X", "X notifications are off",
   "Send the payload again to turn them on.", "X notifications are on",
   "Likes, replies and mentions show up here, during games too.",
   "Turned off in XPS5's Settings.", "Your post is rising \xc2\xb7 %lld likes",
   "Post this screenshot on X", "Post this clip on X",
   "Bronze trophy \302\267 post it on X", "Silver trophy \302\267 post it on X", "Gold trophy \302\267 post it on X",
   "Platinum trophy \302\267 post it on X"},
  {"Abrir", "XPS5: falta a sess\xc3\xa3o do X", "XPS5: o X n\xc3\xa3o aceitou a sess\xc3\xa3o",
   "Liga-a outra vez pelo PC: XPS5, Defini\303\247\303\265es, Conta do X.", "Mais %d notifica\xc3\xa7\xc3\xb5" "es no X",
   "Notifica\xc3\xa7\xc3\xb5" "es do X desligadas", "Envia o payload outra vez para as ligar.",
   "Notifica\xc3\xa7\xc3\xb5" "es do X ligadas",
   "Os gostos, respostas e men\xc3\xa7\xc3\xb5" "es aparecem aqui, tamb\xc3\xa9m durante o jogo.",
   "Desligadas nas Defini\xc3\xa7\xc3\xb5" "es do XPS5.", "O teu post est\xc3\xa1 a subir \xc2\xb7 %lld gostos",
   "Publica esta captura no X", "Publica este v\303\255deo no X",
   "Trof\303\251u de bronze \302\267 publica-o no X", "Trof\303\251u de prata \302\267 publica-o no X",
   "Trof\303\251u de ouro \302\267 publica-o no X", "Trof\303\251u de platina \302\267 publica-o no X"},
  {"Abrir", "XPS5: falta la sesi\xc3\xb3n de X", "XPS5: X no acept\xc3\xb3 la sesi\xc3\xb3n",
   "Vinc\303\272lala otra vez desde un PC: XPS5, Configuraci\303\263n, Cuenta de X.", "%d notificaciones m\xc3\xa1s en X",
   "Notificaciones de X desactivadas", "Env\xc3\xad" "a el payload otra vez para activarlas.",
   "Notificaciones de X activadas",
   "Los Me gusta, respuestas y menciones aparecen aqu\xc3\xad, tambi\xc3\xa9n durante el juego.",
   "Desactivadas en la Configuraci\xc3\xb3n de XPS5.", "Tu post est\xc3\xa1 en auge \xc2\xb7 %lld Me gusta",
   "Publica esta captura en X", "Publica este clip en X",
   "Trofeo de bronce \302\267 publ\303\255calo en X", "Trofeo de plata \302\267 publ\303\255calo en X",
   "Trofeo de oro \302\267 publ\303\255calo en X", "Trofeo de platino \302\267 publ\303\255calo en X"},
  {"Ouvrir", "XPS5 : session X absente", "XPS5 : X n'a pas accept\xc3\xa9 la session",
   "Lie-la de nouveau depuis un PC : XPS5, Param\303\250tres, Compte X.", "%d notifications de plus sur X",
   "Notifications X d\xc3\xa9sactiv\xc3\xa9" "es", "Renvoie le payload pour les activer.",
   "Notifications X activ\xc3\xa9" "es",
   "Les J'aime, r\xc3\xa9ponses et mentions s'affichent ici, m\xc3\xaame en jeu.",
   "D\xc3\xa9sactiv\xc3\xa9" "es dans les Param\xc3\xa8tres de XPS5.", "Ton post est en hausse \xc2\xb7 %lld J'aime",
   "Publie cette capture sur X", "Publie ce clip sur X",
   "Troph\303\251e de bronze \302\267 publie-le sur X", "Troph\303\251e d'argent \302\267 publie-le sur X",
   "Troph\303\251e d'or \302\267 publie-le sur X", "Troph\303\251e de platine \302\267 publie-le sur X"},
  {"\xc3\x96" "ffnen", "XPS5: X-Sitzung fehlt", "XPS5: X hat die Sitzung nicht akzeptiert",
   "Verkn\303\274pfe sie erneut vom PC aus: XPS5, Einstellungen, X-Konto.", "%d weitere Mitteilungen auf X",
   "X-Mitteilungen aus", "Sende den Payload erneut, um sie einzuschalten.", "X-Mitteilungen an",
   "Likes, Antworten und Erw\xc3\xa4hnungen erscheinen hier, auch im Spiel.",
   "In den Einstellungen von XPS5 ausgeschaltet.", "Dein Post ist im Aufwind \xc2\xb7 %lld Likes",
   "Poste diesen Screenshot auf X", "Poste diesen Clip auf X",
   "Bronze-Troph\303\244e \302\267 poste sie auf X", "Silber-Troph\303\244e \302\267 poste sie auf X",
   "Gold-Troph\303\244e \302\267 poste sie auf X", "Platin-Troph\303\244e \302\267 poste sie auf X"},
  {"Apri", "XPS5: manca la sessione di X", "XPS5: X non ha accettato la sessione",
   "Collegala di nuovo da un PC: XPS5, Impostazioni, Account X.", "Altre %d notifiche su X", "Notifiche di X disattivate",
   "Invia di nuovo il payload per attivarle.", "Notifiche di X attivate",
   "Mi piace, risposte e menzioni compaiono qui, anche durante il gioco.",
   "Disattivate nelle Impostazioni di XPS5.", "Il tuo post \xc3\xa8 in ascesa \xc2\xb7 %lld Mi piace",
   "Pubblica questo screenshot su X", "Pubblica questa clip su X",
   "Trofeo di bronzo \302\267 pubblicalo su X", "Trofeo d'argento \302\267 pubblicalo su X",
   "Trofeo d'oro \302\267 pubblicalo su X", "Trofeo di platino \302\267 pubblicalo su X"},
  {"Openen", "XPS5: X-sessie ontbreekt", "XPS5: X accepteerde de sessie niet",
   "Koppel de sessie opnieuw vanaf een pc: XPS5, Instellingen, X-account.", "Nog %d meldingen op X", "X-meldingen uit",
   "Stuur de payload opnieuw om ze aan te zetten.", "X-meldingen aan",
   "Likes, antwoorden en vermeldingen verschijnen hier, ook tijdens het spel.",
   "Uitgezet in de Instellingen van XPS5.", "Je post stijgt \xc2\xb7 %lld likes",
   "Post deze screenshot op X", "Post deze clip op X",
   "Bronzen trofee \302\267 post hem op X", "Zilveren trofee \302\267 post hem op X",
   "Gouden trofee \302\267 post hem op X", "Platina trofee \302\267 post hem op X"},
};

static const char *const g_settings_paths[] = {SETTINGS_PATH, SETTINGS_DIR_PATH};

static settings_t g_settings;
static int        g_lang;        // LANG_* the toasts are in
static int        g_system_lang; // the PS5's, sceSystemServiceParamGetInt 1
static unsigned   g_toasts;


/**
 * status for the klog; never pass the session, a notification or a path here
 **/
static void
log_text(const char *line) {
  klog_printf("[TwitterPS5 notify] %s\n", line);
}


static void
log_line(const char *fmt, ...) {
  char buf[256];
  va_list args;

  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  log_text(buf);
}


static const char *
text(int which) {
  return g_texts[g_lang >= 0 && g_lang < LANGS ? g_lang : LANG_EN][which];
}


/**
 * the settings the title keeps, and the language they make
 **/
static void
load_settings(void) {
  int at = settings_load(&g_settings, g_settings_paths, 2);

  g_lang = g_settings.lang == LANG_SYSTEM ? settings_lang_of_system(g_system_lang) : g_settings.lang;
  x_set_language(settings_lang_code(g_lang));
  log_line("settings: file %d, notifications %d, every %d s, language %d", at, g_settings.notify,
           g_settings.every, g_lang);
}


/**
 * a toast; with open, a button that opens the title
 **/
static int
send_toast(const xnotify_toast_t *t, int open) {
  static char json[XNOTIFY_JSON];
  long long now = (long long)time(NULL);
  int ret;

  // a new id every time, so the PS5 does not take it for one it has
  if(xnotify_json(json, sizeof json, t, text(T_OPEN), open ? OPEN_LINK : NULL, now,
                  (unsigned)now * 16 + (g_toasts++ & 15))) {
    log_line("toast does not fit");
    return -1;
  }
  if((ret=sceNotificationSend(SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM, true, json))) {
    log_line("toast 0x%08x%s", (unsigned)ret, t->extra[0] ? " with its third line" : "");
  }
  memset(json, 0, sizeof json);
  return ret;
}


/**
 * the toast, and again without its third line when the PS5 did not take it
 **/
static void
toast(const xnotify_toast_t *t, int open) {
  static xnotify_toast_t plain;

  if(send_toast(t, open) && t->extra[0]) {
    plain = *t;
    plain.extra[0] = 0;
    send_toast(&plain, open);
    memset(&plain, 0, sizeof plain);
  }
}


/**
 * a toast of the app's own, about itself
 **/
static void
say(const char *title, const char *body) {
  xnotify_toast_t t;

  memset(&t, 0, sizeof t);
  snprintf(t.title, sizeof t.title, "%s", title);
  snprintf(t.body, sizeof t.body, "%s", body);
  t.icon = "messages";
  toast(&t, 0);
}


/**
 * one reading: the toasts of what is new; returns the seconds until the next
 **/
static int
read_notices(xnotify_t *n) {
  static char url[8192];
  static int warned;
  int fresh[SHOW_MAX], ret, count, total, found, every = g_settings.every;
  char path[64];
  xnotify_toast_t t;
  https_response_t res;
  x_session_t session;
  x_request_t request;
  x_post_t *posts;
  json_doc_t *doc;
  size_t off;

  if(settings_session_path(g_settings.account, path, sizeof path)) {
    snprintf(path, sizeof path, "%s", X_SESSION_PATH);
  }
  if((ret=x_session_read(&session, path))) {
    log_line("session: %d", ret);
    if(!warned) {
      say(text(T_NO_SESSION), path);
      warned = 1;
    }
    return every;
  }
  if(x_feed_url(url, sizeof url, X_FEED_NOTIFICATIONS, NULL, READ_COUNT, NULL)) {
    memset(&session, 0, sizeof session);
    return every;
  }
  x_request_init(&request, &session);
  ret = https_get(url, request.headers, X_HEADER_COUNT, &res);
  memset(&session, 0, sizeof session);
  memset(&request, 0, sizeof request);
  if(ret) {
    log_line("network 0x%08x", (unsigned)ret);
    return every;
  }
  if(res.status != 200) {
    log_line("HTTP %d", res.status);
    mem_free(res.body);
    if((res.status == 401 || res.status == 403) && !warned) {
      say(text(T_REFUSED), text(T_COPY_AGAIN));
      warned = 1;
    }
    return res.status == 429 ? WAIT_LIMITED : every;
  }
  warned = 0;

  if(!(doc=json_parse(res.body, res.len, &off))) {
    log_line("answer is not JSON (byte %zu of %zu)", off, res.len);
    mem_free(res.body);
    return every;
  }
  count = 0;
  if((posts=calloc(READ_COUNT * 2, sizeof *posts))) {
    count = x_timeline_posts(json_root(doc), posts, READ_COUNT * 2, &total);
    found = xnotify_fresh(n, posts, count, fresh, SHOW_MAX);
    log_line("%d notices, %d new", count, found);
    // the oldest first, so the newest is the last on the screen
    for(int i=(found < SHOW_MAX ? found : SHOW_MAX) - 1; i >= 0; i--) {
      xnotify_toast(&posts[fresh[i]], g_lang, &t);
      toast(&t, 1);
      memset(&t, 0, sizeof t);
    }
    if(found > SHOW_MAX) {
      memset(&t, 0, sizeof t);
      snprintf(t.title, sizeof t.title, text(T_MORE), found - SHOW_MAX);
      snprintf(t.body, sizeof t.body, "XPS5");
      t.icon = "messages";
      toast(&t, 1);
    }
    memset(posts, 0, READ_COUNT * 2 * sizeof *posts);
    free(posts);
  }
  json_free(doc);
  mem_free(res.body);
  return every;
}


/**
 * a look at the user's own latest posts, for one rising fast (xnotify_rising),
 * whose toast it shows; the HTTP status, or -1
 **/
static int
read_rising(xnotify_rising_t *r) {
  static char url[8192];
  const char *uid = g_settings.ids[g_settings.account - 1];
  char path[64], title[XNOTIFY_TEXT];
  int ret, count, total, at;
  xnotify_toast_t t;
  https_response_t res;
  x_session_t session;
  x_request_t request;
  x_post_t *posts;
  json_doc_t *doc;
  size_t off;

  if(!uid[0] || x_feed_url(url, sizeof url, X_FEED_USER, uid, RISING_COUNT, NULL)) {
    return -1;
  }
  if(settings_session_path(g_settings.account, path, sizeof path)) {
    snprintf(path, sizeof path, "%s", X_SESSION_PATH);
  }
  if(x_session_read(&session, path)) {
    return -1;
  }
  x_request_init(&request, &session);
  ret = https_get(url, request.headers, X_HEADER_COUNT, &res);
  memset(&session, 0, sizeof session);
  memset(&request, 0, sizeof request);
  if(ret) {
    log_line("rising: network 0x%08x", (unsigned)ret);
    return -1;
  }
  if(res.status != 200 || !(doc=json_parse(res.body, res.len, &off))) {
    log_line("rising: HTTP %d", res.status);
    mem_free(res.body);
    return res.status;
  }
  if((posts=calloc(RISING_COUNT * 2, sizeof *posts))) {
    count = x_timeline_posts(json_root(doc), posts, RISING_COUNT * 2, &total);
    at = xnotify_rising(r, posts, count, uid, (long long)time(NULL));
    log_line("rising: %d posts, %d rising", count, at >= 0);
    if(at >= 0) {
      snprintf(title, sizeof title, text(T_RISING), posts[at].likes);
      xnotify_rising_toast(&posts[at], title, &t);
      toast(&t, 1);
      memset(&t, 0, sizeof t);
    }
    memset(posts, 0, RISING_COUNT * 2 * sizeof *posts);
    free(posts);
  }
  json_free(doc);
  mem_free(res.body);
  return 200;
}


/*
 * The captures, for the title.
 */

static int
write_all(int fd, const void *buf, size_t len) {
  size_t off = 0;
  ssize_t n;

  while(off < len && (n=write(fd, (const char *)buf + off, len - off)) > 0) {
    off += (size_t)n;
  }
  return off == len ? 0 : -1;
}


/**
 * "list": the captures, newest first
 **/
static void
serve_list(int c) {
  size_t size = (size_t)CAPTURES_MAX * (CAPTURE_PATH + 48);
  capture_t *list = calloc(CAPTURES_MAX, sizeof *list);
  char *buf = malloc(size), head[32];
  int n = list ? captures_list(list, CAPTURES_MAX) : -1, len = -1, e = errno;

  if(n >= 0 && buf) {
    len = captures_format(list, n, buf, size);
  }
  if(len < 0) {
    snprintf(head, sizeof head, "err %d\n", e > 0 ? e : ENOMEM);
    (void)!write_all(c, head, strlen(head));
  } else {
    snprintf(head, sizeof head, "ok %d\n", len);
    if(!write_all(c, head, strlen(head))) {
      (void)!write_all(c, buf, (size_t)len);
    }
  }
  log_line("captures listed: %d (%d folders, %d unreadable, %d files, %d kept, %d too long, errno %d)", n,
           captures_stats.folders, captures_stats.unreadable, captures_stats.files, captures_stats.kept,
           captures_stats.long_paths, captures_stats.error);
  free(list);
  free(buf);
}


/**
 * "read <offset> <length> <path>"
 **/
static void
serve_read(int c, const char *args) {
  long long off = -1, size = 0;
  unsigned long long len = 0;
  char head[64], *buf = NULL;
  ssize_t got = -1;
  int at = 0, fd = -1, e = EINVAL;

  if(sscanf(args, "%lld %llu %n", &off, &len, &at) == 2 && at > 0 && off >= 0 && len <= READ_MAX &&
     captures_kind(args + at) >= 0) {
    if((fd=open(args + at, O_RDONLY)) < 0) {
      e = errno;
    } else if((size=lseek(fd, 0, SEEK_END)) < 0 || !(buf=malloc(len ? len : 1))) {
      e = ENOMEM;
    } else if((got=pread(fd, buf, len, off)) < 0) {
      e = errno;
    }
  }
  if(got < 0) {
    snprintf(head, sizeof head, "err %d\n", e > 0 ? e : EIO);
    (void)!write_all(c, head, strlen(head));
  } else {
    snprintf(head, sizeof head, "ok %zd %lld\n", got, size);
    if(!write_all(c, head, strlen(head))) {
      (void)!write_all(c, buf, (size_t)got);
    }
  }
  if(fd >= 0) {
    close(fd);
  }
  free(buf);
}


/**
 * a small file read whole into a buffer of its own, NUL after it; NULL
 * when it is not there or bigger than max
 **/
static char *
read_small(const char *path, size_t max, size_t *len) {
  char *buf = NULL;
  off_t size;
  size_t off = 0;
  ssize_t n = 1;
  int fd;

  if((fd=open(path, O_RDONLY)) < 0) {
    return NULL;
  }
  size = lseek(fd, 0, SEEK_END);
  if(size > 0 && (size_t)size <= max && !lseek(fd, 0, SEEK_SET) && (buf=malloc((size_t)size + 1))) {
    while(off < (size_t)size && (n=read(fd, buf + off, (size_t)size - off)) > 0) {
      off += (size_t)n;
    }
    buf[off] = 0;
  }
  close(fd);
  if(buf && off != (size_t)size) {
    free(buf);
    buf = NULL;
  }
  *len = off;
  return buf;
}


/**
 * the name of the game with the title id, in the language of the
 * settings, from the PS5's data about it; empty when it has none
 **/
static void
game_name_of(const char *id, char *name, size_t size) {
  static const char *const dirs[] = {"/user/appmeta/", "/system_data/priv/appmeta/"};
  char path[96], *buf;
  size_t len;

  name[0] = 0;
  for(size_t i=0; i < sizeof dirs / sizeof dirs[0] && !name[0]; i++) {
    snprintf(path, sizeof path, "%s%s/param.json", dirs[i], id);
    if((buf=read_small(path, 1 << 20, &len))) {
      game_name_json(buf, len, game_locale(g_lang), name, size);
      free(buf);
    }
    snprintf(path, sizeof path, "%s%s/param.sfo", dirs[i], id);
    if(!name[0] && (buf=read_small(path, 1 << 16, &len))) {
      game_name_sfo((const unsigned char *)buf, len, name, size);
      free(buf);
    }
  }
}


/*
 * The game that is open.
 */

/**
 * whether the app with the title id is a game: its param.json's category
 * says so (0; a media app such as YouTube, or XPS5 itself, is 65536); a PS4
 * one, with a param.sfo and no param.json, counts as a game. The answer for
 * the last id asked is kept, since open_game asks often
 **/
static int
is_game(const char *id) {
  static const char *const dirs[] = {"/user/appmeta/", "/system_data/priv/appmeta/"};
  static char last[32];
  static int last_game;
  char path[96], *buf;
  size_t len;
  int category = -2;

  if(!game_id_ok(id)) {
    return 0;
  }
  if(!strcmp(id, last)) {
    return last_game;
  }
  for(size_t i=0; i < sizeof dirs / sizeof dirs[0] && category == -2; i++) {
    snprintf(path, sizeof path, "%s%s/param.json", dirs[i], id);
    if((buf=read_small(path, 1 << 20, &len))) {
      category = game_category_json(buf, len);
      free(buf);
    }
  }
  last_game = category <= 0; // none said (-1) or no param.json (-2): a game
  snprintf(last, sizeof last, "%s", id);
  if(!last_game) {
    log_line("game: an app of category %d is not a game", category);
  }
  return last_game;
}


static char g_open[16];   // the game seen last as the PS5's big app, while it stays open
static int  g_open_asked; // the installed games were asked for one, once


/**
 * the game that is open: the PS5's big app when it is a game, else the one
 * seen last while it is still open behind (with XPS5 or the home screen in
 * front, the big app is theirs); once, the installed games are asked, for
 * one opened before this copy started. Its title id into id and its app
 * into app; 1, or 0 for none
 **/
static int
open_game(char *id, size_t size, int *app) {
  char now[32] = "";
  struct dirent *e;
  DIR *d;
  int a = sceSystemServiceGetAppIdOfRunningBigApp();

  *app = -1;
  id[0] = 0;
  if(a > 0 && !sceSystemServiceGetAppTitleId(a, now)) {
    now[sizeof now - 1] = 0;
  }
  if(is_game(now)) {
    snprintf(g_open, sizeof g_open, "%s", now);
    *app = a;
  } else if(g_open[0] && (a=sceLncUtilGetAppId(g_open)) > 0) {
    *app = a;
  } else {
    g_open[0] = 0;
    if(!g_open_asked && (d=opendir("/user/appmeta"))) {
      g_open_asked = 1;
      while(!g_open[0] && (e=readdir(d))) {
        if(game_id_ok(e->d_name) && (a=sceLncUtilGetAppId(e->d_name)) > 0 && is_game(e->d_name)) {
          snprintf(g_open, sizeof g_open, "%s", e->d_name);
          *app = a;
        }
      }
      closedir(d);
      log_line("game: the installed ones asked, one open %d", !!g_open[0]);
    }
  }
  if(!g_open[0]) {
    return 0;
  }
  snprintf(id, size, "%s", g_open);
  return 1;
}


/**
 * "game": the game that is open, its title id and its name in the
 * language of the settings, from the PS5's data about it
 **/
static void
serve_game(int c) {
  static int last_app = 1, last_named = -1;
  char id[32] = "", name[GAME_NAME] = "", line[GAME_NAME + 48];
  int app;

  if(open_game(id, sizeof id, &app)) {
    game_name_of(id, name, sizeof name);
    snprintf(line, sizeof line, "ok %s %s\n", id, name);
  } else {
    snprintf(line, sizeof line, "none\n");
  }
  (void)!write_all(c, line, strlen(line));
  if(app != last_app || !!name[0] != last_named) {
    log_line("game: app 0x%08x, open %d, a name %d", (unsigned)app, !!id[0], !!name[0]);
    last_app = app;
    last_named = !!name[0];
  }
}


/*
 * Invites: the game of one, opened.
 */

/**
 * whether the PS5 has the game with the title id
 **/
static int
installed(const char *id) {
  static const char *const dirs[] = {"/user/appmeta/", "/system_data/priv/appmeta/"};
  char path[96];
  struct stat st;

  for(size_t i=0; game_id_ok(id) && i < sizeof dirs / sizeof dirs[0]; i++) {
    snprintf(path, sizeof path, "%s%s", dirs[i], id);
    if(!stat(path, &st) && S_ISDIR(st.st_mode)) {
      return 1;
    }
  }
  return 0;
}


/**
 * whether two names are of the same game: the same words without the
 * edition, the accents and the case (game_keys' first)
 **/
static int
same_game(const char *a, const char *b) {
  static game_keys_t ka, kb;

  return game_keys(a, &ka) > 0 && game_keys(b, &kb) > 0 && !strcmp(ka.key[0], kb.key[0]);
}


/**
 * the installed game named name, as its title id into id (another region's
 * has another id); 0, or -1
 **/
static int
find_game(const char *name, char *id, size_t size) {
  char found[GAME_NAME];
  struct dirent *e;
  DIR *d;

  id[0] = 0;
  if(!(d=opendir("/user/appmeta"))) {
    return -1;
  }
  while(!id[0] && (e=readdir(d))) {
    if(game_id_ok(e->d_name)) {
      game_name_of(e->d_name, found, sizeof found);
      if(found[0] && same_game(found, name)) {
        snprintf(id, size, "%s", e->d_name);
      }
    }
  }
  closedir(d);
  return id[0] ? 0 : -1;
}


/**
 * "launch <title id or -> <name>": the game of an invite, opened, or
 * brought back when it is the one open
 **/
static void
serve_launch(int c, const char *args) {
  static int user_service;
  lnc_app_param_t param = {.size = sizeof param};
  char id[16] = "", running[32] = "", line[48];
  const char *name = strchr(args, ' ');
  int app, ret;

  name = name ? name + 1 : "";
  if((size_t)(strcspn(args, " ")) < sizeof id) {
    memcpy(id, args, strcspn(args, " "));
    id[strcspn(args, " ")] = 0;
  }
  if(!installed(id) && (!name[0] || find_game(name, id, sizeof id))) {
    log_line("launch: not installed");
    (void)!write_all(c, "none\n", 5);
    return;
  }
  open_game(running, sizeof running, &app);
  if(running[0] && strcmp(running, id)) {
    log_line("launch: another game is open");
    (void)!write_all(c, "busy\n", 5);
    return;
  }
  if(running[0]) {
    ret = sceLncUtilResumeApp(app, 0);
  } else {
    if(!user_service) {
      sceUserServiceInitialize(NULL);
      user_service = 1;
    }
    if(sceUserServiceGetForegroundUser(&param.user_id)) {
      param.user_id = -1;
    }
    ret = sceLncUtilLaunchApp(id, NULL, &param);
  }
  log_line("launch: %s 0x%08x", running[0] ? "resume" : "launch", (unsigned)ret);
  if(ret < 0) {
    snprintf(line, sizeof line, "err %08x\n", (unsigned)ret);
  } else {
    snprintf(line, sizeof line, "ok\n");
  }
  (void)!write_all(c, line, strlen(line));
}


/*
 * Playing now: the game that runs in the profile's location (play.h), and
 * what it said before back once the game closes.
 */

static struct {
  int       account;   // whose state is loaded, 0 for none
  char      seen[32];  // the game seen at the last look, or empty
  int       steady;    // how many looks in a row saw it
  char      shown[32]; // the game the profile says now, or empty
  char      before[PLAY_STATUS * 2]; // the location the profile had before
  int       checked;   // the profile looked at once, for a status no record names
} g_play;

static xtid_t    g_tid;
static long long g_tid_at, g_tid_failed;


/**
 * the file of the account's state; 0 or -1
 **/
static int
play_path(int account, char *path, size_t size) {
  return snprintf(path, size, "%splaying%d.txt", SETTINGS_SESSION_DIR, account) < (int)size ? 0 : -1;
}


/**
 * the state of the account in the settings: the game its profile shows and
 * what it said before, as the last copy left them
 **/
static void
play_load(void) {
  char path[96], *buf, *line, *next;
  size_t len;

  memset(&g_play, 0, sizeof g_play);
  g_play.account = g_settings.account;
  if(play_path(g_play.account, path, sizeof path) || !(buf=read_small(path, 4096, &len))) {
    return;
  }
  for(line=buf; line && *line; line=next) {
    if((next=strchr(line, '\n'))) {
      *next++ = 0;
    }
    if(!strncmp(line, "game=", 5) && game_id_ok(line + 5)) {
      snprintf(g_play.shown, sizeof g_play.shown, "%s", line + 5);
    } else if(!strncmp(line, "before=", 7)) {
      snprintf(g_play.before, sizeof g_play.before, "%s", line + 7);
    }
  }
  free(buf);
}


static void
play_save(void) {
  char path[96], buf[sizeof g_play.before + 64];
  int fd, n;

  if(play_path(g_play.account, path, sizeof path)) {
    return;
  }
  n = snprintf(buf, sizeof buf, "game=%s\nbefore=%s\n", g_play.shown, g_play.before);
  if(n < 0 || (size_t)n >= sizeof buf || (fd=open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
    log_line("playing: state not kept (errno %d)", errno);
    return;
  }
  if(write_all(fd, buf, (size_t)n)) {
    log_line("playing: state not kept (errno %d)", errno);
  }
  close(fd);
}


static unsigned
random_byte(void) {
  static unsigned state;

  if(!state) {
    state = (unsigned)time(NULL) * 2654435761u | 1;
  }
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state >> 24;
}


/**
 * the x-client-transaction-id of a POST to url, as the title signs its
 * own; returns whether tid holds it
 **/
static int
sign_post(const x_session_t *s, const char *url, char *tid, size_t size) {
  const char *path = strchr(url + strlen("https://"), '/');
  long long now = (long long)time(NULL);
  char buf[256];
  size_t n;

  if(!path || (n=strcspn(path, "?")) >= sizeof buf) {
    return 0;
  }
  memcpy(buf, path, n);
  buf[n] = 0;
  if((!g_tid_at || now - g_tid_at > TID_MAX_AGE) && now - g_tid_failed >= TID_RETRY) {
    if(xsign_load(&g_tid, s, log_line)) {
      g_tid_at = 0;
      g_tid_failed = now;
    } else {
      g_tid_at = now;
    }
  }
  return g_tid_at && !xtid_make(&g_tid, "POST", buf, now, random_byte(), tid, size);
}


/**
 * the profile's location now, from Viewer; 0, or the HTTP status, or -1
 **/
static int
read_location(const x_session_t *s, char *location, size_t size) {
  static char url[4096];
  https_response_t res;
  x_request_t request;
  json_doc_t *doc;
  x_user_t *user;
  size_t off;
  int ret = -1;

  location[0] = 0;
  if(x_user_url(url, sizeof url, NULL)) {
    return -1;
  }
  x_request_init(&request, s);
  ret = https_get(url, request.headers, X_HEADER_COUNT, &res);
  memset(&request, 0, sizeof request);
  if(ret) {
    log_line("playing: profile 0x%08x", (unsigned)ret);
    return -1;
  }
  ret = res.status == 200 ? -1 : res.status;
  if(res.status == 200 && (doc=json_parse(res.body, res.len, &off))) {
    if((user=calloc(1, sizeof *user))) {
      if(!x_user_parse(json_root(doc), user)) {
        snprintf(location, size, "%s", user->location);
        ret = 0;
      }
      memset(user, 0, sizeof *user);
      free(user);
    }
    json_free(doc);
  }
  log_line("playing: profile HTTP %d, %s", res.status, ret ? "unread" : "read");
  mem_free(res.body);
  return ret;
}


/**
 * the profile's location set to location; 0, or the HTTP status, or -1
 **/
static int
write_location(const x_session_t *s, const char *location) {
  static char url[256], body[512];
  char tid[XTID_HEADER_MAX];
  https_header_t headers[X_HEADER_COUNT + 2];
  int count = X_HEADER_COUNT, ret, done = -1, signed_tid;
  const char *message;
  https_response_t res;
  x_request_t request;
  json_doc_t *doc;
  size_t off;

  if(x_location_request(url, sizeof url, body, sizeof body, location)) {
    return -1;
  }
  signed_tid = sign_post(s, url, tid, sizeof tid);
  x_request_init(&request, s);
  memcpy(headers, request.headers, sizeof request.headers);
  for(int i=0; i < X_HEADER_COUNT; i++) {
    if(!strcmp(headers[i].name, "content-type")) {
      headers[i].value = "application/x-www-form-urlencoded";
    }
  }
  headers[count++] = (https_header_t){"origin", "https://x.com"};
  if(signed_tid) {
    headers[count++] = (https_header_t){"x-client-transaction-id", tid};
  }
  ret = https_post(url, headers, count, body, &res);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  memset(body, 0, sizeof body);
  if(ret) {
    log_line("playing: profile change 0x%08x", (unsigned)ret);
    return -1;
  }
  if((doc=json_parse(res.body, res.len, &off))) {
    done = x_first_error(json_root(doc), &message);
    json_free(doc);
  }
  log_line("playing: profile change%s HTTP %d, X says %d", signed_tid ? " (tid)" : "", res.status, done);
  mem_free(res.body);
  if(signed_tid && (res.status == 404 || done == 226)) {
    g_tid_at = 0; // a new page's key next time
  }
  return res.status == 200 && done <= 0 ? 0 : res.status != 200 ? res.status : -1;
}


/**
 * a look at the game that runs: once it has stayed for two looks, the
 * profile says it (when the settings want it), or what it said before once
 * the game closed; returns the seconds until the next look
 **/
static int
watch_playing(void) {
  char id[32] = "", name[GAME_NAME], path[64], now[PLAY_STATUS * 2], status[PLAY_STATUS];
  int app, want, ret, stale;
  x_session_t session;

  if(g_play.account != g_settings.account) {
    play_load();
  }
  open_game(id, sizeof id, &app);
  if(strcmp(id, g_play.seen)) {
    // a game opened or closed: one more look first, so a moment does not count
    snprintf(g_play.seen, sizeof g_play.seen, "%s", id);
    g_play.steady = 1;
    return PLAY_EVERY;
  }
  g_play.steady += g_play.steady < 2;
  want = g_settings.playing && id[0];
  // once for each account: a status that no record here names (the copy
  // that put it there was stopped, and its record removed, as an uninstall
  // does) comes off, whatever the settings
  stale = !want && !g_play.shown[0] && !g_play.checked;
  if(g_play.steady < 2 || (want && !strcmp(g_play.shown, id)) || (!want && !g_play.shown[0] && !stale)) {
    return PLAY_EVERY;
  }
  if(settings_session_path(g_settings.account, path, sizeof path) || x_session_read(&session, path)) {
    return PLAY_RETRY;
  }
  if(stale) {
    if((ret=read_location(&session, now, sizeof now))) {
      memset(&session, 0, sizeof session);
      return ret == 429 ? WAIT_LIMITED : PLAY_RETRY;
    }
    g_play.checked = 1;
    if(play_game_of(now, name, sizeof name)) {
      log_line("playing: a status left behind comes off");
      if((ret=write_location(&session, ""))) {
        g_play.checked = 0; // again later
      }
    }
    memset(&session, 0, sizeof session);
    memset(now, 0, sizeof now);
    return !ret ? PLAY_EVERY : ret == 429 ? WAIT_LIMITED : PLAY_RETRY;
  }
  ret = -1;
  log_line("playing: the profile %s", want ? "gets the game" : "gets back what it said");
  if(want) {
    game_name_of(id, name, sizeof name);
    if(play_status(name[0] ? name : id, status, sizeof status)) {
      memset(&session, 0, sizeof session);
      return PLAY_RETRY;
    }
    // what it said before, once: a game after another keeps the first's
    if(!g_play.shown[0]) {
      if((ret=read_location(&session, now, sizeof now))) {
        memset(&session, 0, sizeof session);
        return ret == 429 ? WAIT_LIMITED : PLAY_RETRY;
      }
      g_play.checked = 1;
      // a status left by a copy that stopped is not what it said before
      snprintf(g_play.before, sizeof g_play.before, "%s", play_game_of(now, name, sizeof name) ? "" : now);
    }
    if(!(ret=write_location(&session, status))) {
      snprintf(g_play.shown, sizeof g_play.shown, "%s", id);
      play_save();
    }
  } else if(!(ret=write_location(&session, g_play.before))) {
    g_play.shown[0] = 0;
    g_play.before[0] = 0;
    play_save();
  }
  memset(&session, 0, sizeof session);
  memset(now, 0, sizeof now);
  return !ret ? PLAY_EVERY : ret == 429 ? WAIT_LIMITED : PLAY_RETRY;
}


/**
 * XPS5 being uninstalled: each account whose profile shows a game gets back
 * what it said before, at once, while the records of it are still there
 **/
static void
play_restore_all(void) {
  int account = g_settings.account, restored = 0, failed = 0;
  char path[64];
  x_session_t session;

  for(int a=1; a <= SETTINGS_ACCOUNTS; a++) {
    g_settings.account = a;
    play_load();
    if(!g_play.shown[0]) {
      continue;
    }
    if(settings_session_path(a, path, sizeof path) || x_session_read(&session, path) ||
       write_location(&session, g_play.before)) {
      failed++;
    } else {
      g_play.shown[0] = 0;
      g_play.before[0] = 0;
      play_save();
      restored++;
    }
    memset(&session, 0, sizeof session);
  }
  g_settings.account = account;
  play_load();
  log_line("playing: uninstalled, %d profiles got back what they said, %d did not", restored, failed);
}


/**
 * whether the game in the profile needs looks: wanted, still shown, or the
 * profile not looked at yet for a status left behind
 **/
static int
play_wanted(void) {
  return g_settings.playing || g_play.shown[0] || !g_play.checked || g_play.account != g_settings.account;
}


/*
 * The game's new trophies, offered for a post.
 */

static struct {
  char            game[32];            // the title id whose trophies are watched
  char            set[TROPHY_SET_LEN]; // its trophy set, empty when it has none
  int             known;               // what the user had of it was read
  long long       mtime;               // the time of the record read last
  trophy_earned_t have;
  int             id;                  // a new trophy for the title, -1 for none
  int             grade;               // TROPHY_*
  char            name[TROPHY_NAME];
  long long       found;               // when it was found
  long long       toasted;             // when its toast went
} g_troph = {.id = -1};


/**
 * len bytes of a file from off, NUL after them, in a buffer of their own;
 * NULL when they are not all there or more than TROPHY_FILE_MOST
 **/
static unsigned char *
read_part(const char *path, unsigned long long off, unsigned long long len) {
  unsigned char *buf = NULL;
  int fd;

  if(!len || len > TROPHY_FILE_MOST || (fd=open(path, O_RDONLY)) < 0) {
    return NULL;
  }
  if((buf=malloc(len + 1)) && pread(fd, buf, len, (off_t)off) == (ssize_t)len) {
    buf[len] = 0;
  } else {
    free(buf);
    buf = NULL;
  }
  close(fd);
  return buf;
}


/**
 * the trophy set the game id uses, from its npbind.dat, into set (empty
 * when it has none)
 **/
static void
set_of_game(const char *id, char set[TROPHY_SET_LEN]) {
  static const char *const dirs[] = {"/user/appmeta/", "/user/appmeta/external/", "/user/app/"};
  char path[112], *buf;
  size_t len;

  set[0] = 0;
  for(size_t i=0; i < sizeof dirs / sizeof dirs[0] && !set[0]; i++) {
    snprintf(path, sizeof path, "%s%s/%strophy2/npbind.dat", dirs[i], id, i == 2 ? "sce_sys/" : "");
    if((buf=read_small(path, 1 << 16, &len))) {
      trophy_set_of((const unsigned char *)buf, len, set);
      free(buf);
    }
  }
}


/**
 * the user's record of the set: the TRPTITLE.DAT of it written last under
 * /user/home; 0 with its path and time, or -1
 **/
static int
record_of_set(const char *set, char *path, size_t size, long long *mtime) {
  char names[1024], at_path[160];
  const char *at = names;
  struct stat st;
  int n;

  *mtime = -1;
  if((n=captures_folder_names("/user/home", names, sizeof names)) <= 0) {
    return -1;
  }
  for(int i=0; i < n; i++, at += strlen(at) + 1) {
    if(strlen(at) != 8 || strspn(at, "0123456789abcdef") != 8) {
      continue;
    }
    snprintf(at_path, sizeof at_path, "/user/home/%s/trophy2/nobackup/data/%s/TRPTITLE.DAT", at, set);
    if(!stat(at_path, &st) && (long long)st.st_mtime > *mtime) {
      *mtime = (long long)st.st_mtime;
      snprintf(path, size, "%s", at_path);
    }
  }
  return *mtime >= 0 ? 0 : -1;
}


/**
 * the grade and the name (in the language of the settings, else English)
 * of the trophy id of the set, from its TROPHY.UCP; 0, or -1
 **/
static int
trophy_about(const char *set, int id, int *grade, char *name, size_t size) {
  char path[96], file[48], game[TROPHY_NAME];
  unsigned char *head = NULL, *table = NULL, *text = NULL;
  unsigned long long off, len;
  size_t table_len;
  int count, ret = -1;

  name[0] = 0;
  snprintf(path, sizeof path, "/user/trophy2/nobackup/conf/%s/TROPHY.UCP", set);
  if(!(head=read_part(path, 0, TROPHY_UCP_HEAD)) || (count=trophy_ucp_files(head, TROPHY_UCP_HEAD)) < 0) {
    goto out;
  }
  table_len = (size_t)count * TROPHY_UCP_ENTRY;
  if(!(table=read_part(path, TROPHY_UCP_HEAD, table_len)) ||
     trophy_ucp_find(table, table_len, count, "tropconf.json", &off, &len) || !(text=read_part(path, off, len)) ||
     (*grade=trophy_grade((char *)text, (size_t)len, id)) < 0) {
    goto out;
  }
  free(text);
  text = NULL;
  if(!trophy_ucp_meta(table, table_len, count, game_locale(g_lang), file, sizeof file) &&
     !trophy_ucp_find(table, table_len, count, file, &off, &len) && (text=read_part(path, off, len))) {
    trophy_names((char *)text, (size_t)len, id, name, size, game, sizeof game);
  }
  ret = 0;
out:
  free(head);
  free(table);
  free(text);
  return ret;
}


/**
 * a look at the user's trophies of the game that runs (or ran last): a
 * new one, the best when there are more, waits for the title, and a toast
 * offers it
 **/
static void
watch_trophies(void) {
  char id[32] = "", path[160], name[TROPHY_NAME];
  long long now = (long long)time(NULL), mtime;
  int app = sceSystemServiceGetAppIdOfRunningBigApp(), n, fresh = 0, best = -1, grade, best_grade = -1;
  trophy_earned_t *have;
  xnotify_toast_t t;
  unsigned char *buf;
  size_t len;

  if(app > 0 && !sceSystemServiceGetAppTitleId(app, id) && (id[sizeof id - 1]=0, game_id_ok(id)) &&
     strcmp(id, g_troph.game)) {
    // another game: what the user has of it is read first
    snprintf(g_troph.game, sizeof g_troph.game, "%s", id);
    set_of_game(id, g_troph.set);
    g_troph.known = 0;
    g_troph.mtime = -1;
    log_line("trophies: the game has a set %d", !!g_troph.set[0]);
  }
  if(g_troph.id >= 0 && now - g_troph.found > SHOT_FRESH) {
    g_troph.id = -1;
  }
  if(!g_troph.set[0] || record_of_set(g_troph.set, path, sizeof path, &mtime) || mtime == g_troph.mtime ||
     !(buf=(unsigned char *)read_small(path, TROPHY_RECORD_MOST, &len))) {
    return;
  }
  if(!(have=malloc(sizeof *have))) {
    free(buf);
    return;
  }
  n = trophy_earned(buf, len, have);
  free(buf);
  if(n < 0) {
    free(have); // being written, maybe: read again the next time
    return;
  }
  g_troph.mtime = mtime;
  if(!g_troph.known) {
    g_troph.known = 1;
    g_troph.have = *have;
    free(have);
    return;
  }
  for(int i=0; i < TROPHY_IDS && fresh < 8; i++) {
    if(trophy_has(have, i) && !trophy_has(&g_troph.have, i)) {
      fresh++;
      if(!trophy_about(g_troph.set, i, &grade, name, sizeof name) && grade > best_grade) {
        best = i;
        best_grade = grade;
        snprintf(g_troph.name, sizeof g_troph.name, "%s", name);
      }
    }
  }
  g_troph.have = *have;
  free(have);
  log_line("trophies: %d earned, %d new, grade %d", n, fresh, best_grade);
  if(best < 0) {
    return;
  }
  g_troph.id = best;
  g_troph.grade = best_grade;
  g_troph.found = now;
  g_troph.toasted = now;
  memset(&t, 0, sizeof t);
  snprintf(t.title, sizeof t.title, "%s", g_troph.name[0] ? g_troph.name : text(T_BRONZE + best_grade));
  snprintf(t.body, sizeof t.body, "%s", text(T_BRONZE + best_grade));
  t.icon = "messages";
  toast(&t, 1);
  memset(&t, 0, sizeof t);
}


/**
 * "trophy": the game's new trophy for the title, once
 **/
static void
serve_trophy(int c) {
  char line[TROPHY_NAME + 32];

  if(g_troph.id >= 0 && (long long)time(NULL) - g_troph.found <= SHOT_FRESH) {
    snprintf(line, sizeof line, "ok %d %s\n", g_troph.grade, g_troph.name);
  } else {
    snprintf(line, sizeof line, "none\n");
  }
  g_troph.id = -1;
  (void)!write_all(c, line, strlen(line));
}


/*
 * The game's new captures, offered for a post.
 */

static struct {
  char      game[32];            // the title id whose captures are watched
  int       known;               // its newest capture before was seen
  long long newest;              // the time of its newest capture seen
  char      last[CAPTURE_PATH];  // the newest one's path, so a clip still written is not new again
  capture_t pending;             // a new one for the title, path empty for none
  long long found;               // when it was found
  long long toasted;             // when the last toast about one went
} g_shots;


/**
 * a look at the captures of the game that runs (or ran last, while this
 * app is in front): a new one waits for the title, and a toast offers it,
 * one a minute at most
 **/
static void
watch_shots(void) {
  char id[32] = "", name[GAME_NAME];
  long long now = (long long)time(NULL);
  int app = sceSystemServiceGetAppIdOfRunningBigApp(), found, video;
  xnotify_toast_t t;
  capture_t c;

  if(app > 0 && !sceSystemServiceGetAppTitleId(app, id) && (id[sizeof id - 1]=0, game_id_ok(id)) &&
     strcmp(id, g_shots.game)) {
    // another game: the captures it has from before are not new
    snprintf(g_shots.game, sizeof g_shots.game, "%s", id);
    g_shots.known = 0;
  }
  if(g_shots.pending.path[0] && now - g_shots.found > SHOT_FRESH) {
    memset(&g_shots.pending, 0, sizeof g_shots.pending);
  }
  if(!g_shots.game[0] || (found=captures_newest_of(CAPTURES_ROOT, g_shots.game, &c)) < 0) {
    return;
  }
  if(!g_shots.known) {
    g_shots.known = 1;
    g_shots.newest = found ? c.time : 0;
    snprintf(g_shots.last, sizeof g_shots.last, "%s", found ? c.path : "");
    return;
  }
  if(!found || c.time <= g_shots.newest) {
    return;
  }
  g_shots.newest = c.time;
  if(!strcmp(c.path, g_shots.last)) {
    if(!strcmp(c.path, g_shots.pending.path)) {
      g_shots.pending = c; // a clip still being written
    }
    return;
  }
  snprintf(g_shots.last, sizeof g_shots.last, "%s", c.path);
  g_shots.pending = c;
  g_shots.found = now;
  video = c.kind == CAPTURE_MP4 || c.kind == CAPTURE_WEBM;
  log_line("capture: a new %s", video ? "clip" : "picture");
  if(now - g_shots.toasted < SHOT_TOASTS || now - g_troph.toasted < SHOT_TOASTS) {
    return;
  }
  g_shots.toasted = now;
  game_name_of(g_shots.game, name, sizeof name);
  memset(&t, 0, sizeof t);
  snprintf(t.title, sizeof t.title, "%s", text(video ? T_SHOT_VIDEO : T_SHOT_PHOTO));
  snprintf(t.body, sizeof t.body, "%s", name[0] ? name : "XPS5");
  t.icon = "messages";
  toast(&t, 1);
  memset(&t, 0, sizeof t);
}


/**
 * "shot": the game's new capture for the title, once
 **/
static void
serve_shot(int c) {
  char line[CAPTURE_PATH + 64];

  if(g_shots.pending.path[0] && (long long)time(NULL) - g_shots.found <= SHOT_FRESH) {
    snprintf(line, sizeof line, "ok %lld %lld %s\n", g_shots.pending.time, g_shots.pending.size,
             g_shots.pending.path);
  } else {
    snprintf(line, sizeof line, "none\n");
  }
  memset(&g_shots.pending, 0, sizeof g_shots.pending);
  (void)!write_all(c, line, strlen(line));
  memset(line, 0, sizeof line);
}


/*
 * The control port.
 */

/**
 * whether a copy already runs; if so, it is told to stop
 **/
static int
stop_running(void) {
  struct sockaddr_in a;
  int s, ok;

  if((s=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return 0;
  }
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(CONTROL_PORT);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if((ok=!connect(s, (struct sockaddr *)&a, sizeof a))) {
    (void)!write(s, "stop\n", 5);
  }
  close(s);
  return ok;
}


/*
 * The linking from a PC, for the title (linkserve.h).
 */

int
linkserve_check(const char *auth_token, const char *ct0, char *user, size_t size) {
  static char url[8192];
  static x_user_t profile;
  x_session_t session;
  x_request_t request;
  https_response_t res;
  json_doc_t *doc;
  size_t off;
  int ret;

  user[0] = 0;
  if(x_user_url(url, sizeof url, NULL)) {
    return -1;
  }
  memset(&session, 0, sizeof session);
  snprintf(session.auth_token, sizeof session.auth_token, "%s", auth_token);
  snprintf(session.ct0, sizeof session.ct0, "%s", ct0);
  x_request_init(&request, &session);
  ret = https_get(url, request.headers, X_HEADER_COUNT, &res);
  memset(&session, 0, sizeof session);
  memset(&request, 0, sizeof request);
  if(ret) {
    log_line("link: network 0x%08x", (unsigned)ret);
    return -1;
  }
  if(res.status != 200) {
    mem_free(res.body);
    return res.status;
  }
  // an answer without the profile: X did not take it either
  ret = 200;
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
 * "link <screen> <account> <lang>": the state line of the linking
 **/
static void
serve_link(int c, const char *args) {
  link_state_t st;
  char lang[4] = "", line[160];
  unsigned screen;
  int account, n;

  if(sscanf(args, "%u %d %3s", &screen, &account, lang) < 2) {
    (void)!write(c, "err 22\n", 7);
    return;
  }
  linkserve_ask(screen, account, lang, &st);
  if((n=link_state_line(line, sizeof line, &st)) > 0) {
    write_all(c, line, (size_t)n);
  }
  memset(line, 0, sizeof line);
  memset(&st, 0, sizeof st);
}


/**
 * the port the next copy and the title knock on, or -1
 **/
static int
listen_control(void) {
  struct sockaddr_in a;
  int s, on = 1;

  if((s=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(CONTROL_PORT);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 4)) {
    log_line("control port: errno %d", errno);
    close(s);
    return -1;
  }
  return s;
}


/**
 * a command, up to its '\n', the end of what was sent or 2 s of quiet
 **/
static void
read_command(int c, char *cmd, size_t size) {
  size_t n = 0;
  ssize_t got;

  while(n + 1 < size && (got=read(c, cmd + n, size - 1 - n)) > 0) {
    n += (size_t)got;
    if(memchr(cmd, '\n', n)) {
      break;
    }
  }
  cmd[n] = 0;
  cmd[strcspn(cmd, "\r\n")] = 0;
}


enum { WAIT_TIME, WAIT_STOP, WAIT_RELOAD };

/**
 * wait until the time end, answering whoever asks; returns WAIT_TIME, or
 * WAIT_STOP when a new copy asked this one to stop, or WAIT_RELOAD when the
 * title changed the settings
 **/
static int
wait_until(int control, time_t end) {
  char cmd[CAPTURE_PATH + 64];
  struct timeval tv;
  time_t now;
  fd_set fds;
  int c;

  if(control < 0) {
    if((now=time(NULL)) < end) {
      sleep((unsigned)(end - now));
    }
    return WAIT_TIME;
  }
  while((now=time(NULL)) < end) {
    tv.tv_sec = end - now;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(control, &fds);
    if(select(control + 1, &fds, NULL, NULL, &tv) <= 0) {
      return WAIT_TIME;
    }
    if((c=accept(control, NULL, NULL)) < 0) {
      sleep(1);
      continue;
    }
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    tv.tv_sec = 20;
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    read_command(c, cmd, sizeof cmd);
    if(!strcmp(cmd, "stop")) {
      close(c);
      return WAIT_STOP;
    }
    if(!strcmp(cmd, "uninstall")) {
      // the uninstaller: the profiles first, then off
      play_restore_all();
      (void)!write(c, "ok\n", 3);
      close(c);
      return WAIT_STOP;
    }
    if(!strcmp(cmd, "list")) {
      serve_list(c);
    } else if(!strncmp(cmd, "read ", 5)) {
      serve_read(c, cmd + 5);
    } else if(!strcmp(cmd, "game")) {
      serve_game(c);
    } else if(!strncmp(cmd, "link ", 5)) {
      serve_link(c, cmd + 5);
    } else if(!strcmp(cmd, "unlink")) {
      linkserve_close();
      (void)!write(c, "ok\n", 3);
    } else if(!strcmp(cmd, "shot")) {
      serve_shot(c);
    } else if(!strcmp(cmd, "trophy")) {
      serve_trophy(c);
    } else if(!strncmp(cmd, "launch ", 7)) {
      serve_launch(c, cmd + 7);
    } else {
      (void)!write(c, "on 11\n", 6);
      if(!strcmp(cmd, "reload")) {
        close(c);
        return WAIT_RELOAD;
      }
    }
    close(c);
  }
  return WAIT_TIME;
}


/**
 * stdin, stdout and stderr are the sender's connection; letting go of them
 * ends it, so the sender returns while this keeps running
 **/
static void
detach(void) {
  int fd = open("/dev/null", O_RDWR);

  fflush(stdout);
  fflush(stderr);
  for(int i=0; i<3; i++) {
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


int
main(void) {
  static xnotify_t notices;
  static xnotify_rising_t rising;
  long long rising_at = 0, notice_at = 0, shot_at = 0, play_at = 0, next;
  int control, ret, wait, was, account;

  signal(SIGPIPE, SIG_IGN);
  sceSystemServiceParamGetInt(1, &g_system_lang);
  load_settings();

  if(stop_running()) {
    printf("XPS5 notify: the running copy stops; X notifications are off\n");
    log_line("off");
    say(text(T_OFF), text(T_SEND_AGAIN));
    fflush(stdout);
    _exit(EXIT_SUCCESS); // libSceNotification throws from its exit handlers
  }
  control = listen_control();
  if((ret=https_init(X_USER_AGENT, log_text))) {
    printf("XPS5 notify: HTTPS does not start (0x%08x)\n", (unsigned)ret);
    fflush(stdout);
    _exit(EXIT_FAILURE);
  }
  if(g_settings.notify) {
    printf("XPS5 notify: on, a reading every %d s; send it again to turn it off\n",
           g_settings.every);
  } else {
    printf("XPS5 notify: the notifications are off in XPS5's Settings; "
           "it only reads the captures for the app\n");
  }
  log_line("on, notifications %d, playing %d", g_settings.notify, g_settings.playing);
  play_load(); // a game the profile still shows from before, put back once it is closed
  detach();
  if(g_settings.notify) {
    say(text(T_ON), text(T_ON_BODY));
  }

  for(;;) {
    if((long long)time(NULL) >= notice_at) {
      wait = g_settings.notify ? read_notices(&notices) : g_settings.every;
      // the user's own posts, more seldom, for one rising
      if(g_settings.notify && g_settings.rising && wait != WAIT_LIMITED && (long long)time(NULL) >= rising_at) {
        rising_at = (long long)time(NULL) + (read_rising(&rising) == 429 ? WAIT_LIMITED : RISING_EVERY);
      }
      notice_at = (long long)time(NULL) + wait;
    }
    // the game's trophies and captures, often (no network), for a new one to post
    if(g_settings.share && (long long)time(NULL) >= shot_at) {
      watch_trophies(); // first: its toast stands for the PS5's screenshot of it
      watch_shots();
      shot_at = (long long)time(NULL) + SHOT_EVERY;
    }
    // the game in the profile, while it is wanted or still shown
    if(play_wanted() && (long long)time(NULL) >= play_at) {
      play_at = (long long)time(NULL) + watch_playing();
    }
    next = g_settings.share && shot_at < notice_at ? shot_at : notice_at;
    if(play_wanted() && play_at < next) {
      next = play_at;
    }
    while((ret=wait_until(control, (time_t)next)) == WAIT_RELOAD) {
      was = g_settings.notify;
      account = g_settings.account;
      load_settings();
      if(g_settings.account != account) {
        // another account: its notices are new to it, so the first reading only notes them
        memset(&notices, 0, sizeof notices);
        memset(&rising, 0, sizeof rising);
        rising_at = 0;
        log_line("account changed");
        if(g_settings.notify) {
          notice_at = 0;
          break;
        }
      }
      if(g_settings.notify != was) {
        say(g_settings.notify ? text(T_ON) : text(T_OFF),
            g_settings.notify ? text(T_ON_BODY) : text(T_OFF_SETTINGS));
        if(g_settings.notify) {
          notice_at = 0;
          break; // a reading now
        }
      }
      notice_at = (long long)time(NULL) + g_settings.every;
      if(play_wanted()) {
        play_at = 0; // a look at once: the setting changed
        break;
      }
      next = g_settings.share && shot_at < notice_at ? shot_at : notice_at;
    }
    if(ret == WAIT_STOP) {
      break;
    }
  }
  // the copy that knocked says it
  log_line("stopped by a new copy");
  https_fini();
  close(control);
  _exit(EXIT_SUCCESS);
}
