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
  "session" answers, once, for the session of play that ended last (at
            least ten minutes of a game, src/sessions.h): "ok\n", its line,
            "best\t<the name of its best trophy>\n", a "pic\t<path>\n" for
            each of its last screenshots and "end\n"; or "none\n";
  "week"    answers "ok\n", the lines of the sessions of the last seven days
            and "end\n";
  "trophies <title id>"  answers "ok <earned> <all>\n", a line for each
            trophy of the game's set ("id\tgrade\thidden\tearned\tname\t
            description\n", the last two empty for a hidden one not earned)
            and "end\n"; or "none\n";
  "badges <seen> <back> [<id> [<id>]]"  answers "badges <notices>
            <messages> <open> <news>\n": how many notices and messages came
            since the user last looked at each, for the counts of the
            title's bar; then the lists the title shows count as looked at
            (seen: 1 the notices, 2 the messages). The ids are of the
            conversation the title has open (its own, and its X Chat one):
            it counts as read, and news is 1 when it had new messages. With
            back (the title came to the front), open is "chat" or
            "notifications" when a toast told of one in the last minute and
            a half, for its Open, else "-";
  "type <screen> <seq> <max> <what> <lang> <draft>"  serves the writing
            on the phone for the title's window (src/phone.h), and answers
            its state line with what the phone wrote after it;
  "untype"  stops it, the window closed, and answers "ok\n";
  "space play <n> <live> <id> <host> <url>", "space status",
  "space hush <n> <0|1>", "space stop <n>"  a Space played by the
            payload's radio (src/radio.h), which goes on with a game in
            front, where the title is suspended: the title hands it over
            with the playlist's address (no X session goes with it), and
            each answers how it stands (src/spacecmd.h);
anything else gets "on 27" (27: the inboxes read every 6 s, slower after a 429;
26: "badges" counts messages, not conversations, and
takes the conversation the title has open as read, its fourth answer 1 when
that one had new ones; 25: "space", a Space's sound over the game; 24: quiet in competitive games, its toasts summed up after; 23: X Chat's toasts say the words, with the
title's key; 22: "type" and "untype"; 21: a toast for each new message again, the
title's Chat open or not; 20 left them out while it was; 19: a toast's Open goes to the messages when
a message's toast and a notice's came together, and the notices may be read
every 15 s; 18: the messages read every 20 s, on a clock
of their own, and "badges"; 17: a toast for each new message of X Chat
too, who wrote and when, as its words are encrypted; 16: a toast for each new direct message,
from the inbox read at most once a minute, and the people the user
follows read every two minutes instead of ten; 15: the sessions of play, "session", "week"
and "trophies", a toast when someone the user follows starts a game or
opens XPS5, read from their profiles every ten minutes (and who of them
was seen around, kept in the account's file for the title's friends,
seen.h), and "Online on XPS5" in the profile's location while XPS5 is
open with no game; 14: the PS5's HDR screenshots, .jxr, are
offered too; 13: a capture is new by its path, not its
date, as screenshots never came out newer; 12: the captures' folders are read with
readdir, as getdents answered EINVAL in them; 11: of the two settings files, the one
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
#include "phoneserve.h"
#include "play.h"
#include "radio.h"
#include "seen.h"
#include "sessions.h"
#include "settings.h"
#include "trophy.h"
#include "xapi.h"
#include "xchat.h"
#include "xchatopen.h"
#include "xcrypt.h"
#include "xnotify.h"
#include "xsign.h"

#define XCHAT_KEY_DIR "/data/TwitterPS5" // where the title keeps the X Chat key

#define CONTROL_PORT 9047        // on 127.0.0.1, while a copy runs
#define WAIT_LIMITED (10 * 60)   // after X asked to wait (HTTP 429)
#define SHOW_MAX     3           // toasts a reading, then one that counts the rest
#define READ_COUNT   20
#define READ_MAX     (4 << 20)   // bytes of a capture a "read" gives
#define OPEN_LINK    "psgm:play?id=TWPS00001"
#define RISING_EVERY (15 * 60)   // seconds between two looks at the user's own posts
#define RISING_COUNT 20
#define FRIENDS_EVERY  (2 * 60)  // seconds between two looks at who the user follows, for a game started
#define FRIENDS_COUNT  100       // of the people they follow, the newest
#define FRIENDS_TOASTS 2         // toasts a look at most
#define DM_EVERY     6           // seconds between two looks at each inbox, X Chat's too (20 until
                                 // 2026-11-21: a message took too long to reach the console)
#define DM_SLOWEST   30          // each 429 doubles that, up to this, for as long as the payload runs
#define OPEN_WITHIN  90          // seconds a toast's Open may take to bring the title to the front
#define TOAST_BURST  30          // seconds apart a message's toast and a notice's count as together
#define DM_MAX       60          // conversations of the inbox looked at
#define XCHAT_RETRY  (10 * 60)   // after X Chat's inbox did not come
#define SHOT_EVERY   10          // seconds between two looks at the captures
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
  T_OFF_SETTINGS, T_RISING, T_SHOT_PHOTO, T_SHOT_VIDEO, T_BRONZE, T_SILVER, T_GOLD, T_PLATINUM,
  T_SESSION, T_FRIEND, T_ONLINE, T_FRIEND_ONLINE, T_QUIET, T_QUIET_BODY, TEXTS
};

static const char *const g_texts[LANGS][TEXTS] = {
  {"Open", "XPS5: the X session is missing", "XPS5: X did not accept the session",
   "Link it again from a PC: XPS5, Settings, X account.", "%d more notifications on X", "X notifications are off",
   "Send the payload again to turn them on.", "X notifications are on",
   "Likes, replies, mentions and messages show up here, during games too.",
   "Turned off in XPS5's Settings.", "Your post is rising \xc2\xb7 %lld likes",
   "Post this screenshot on X", "Post this clip on X",
   "Bronze trophy \302\267 post it on X", "Silver trophy \302\267 post it on X", "Gold trophy \302\267 post it on X",
   "Platinum trophy \302\267 post it on X",
   "Your session card is ready \302\267 post it from XPS5", "started playing %s",
   "Online on XPS5", "opened XPS5", "%d X notifications while you played",
   "Kept quiet during a competitive game. Change it in XPS5's Settings."},
  {"Abrir", "XPS5: falta a sess\xc3\xa3o do X", "XPS5: o X n\xc3\xa3o aceitou a sess\xc3\xa3o",
   "Liga-a outra vez pelo PC: XPS5, Defini\303\247\303\265es, Conta do X.", "Mais %d notifica\xc3\xa7\xc3\xb5" "es no X",
   "Notifica\xc3\xa7\xc3\xb5" "es do X desligadas", "Envia o payload outra vez para as ligar.",
   "Notifica\xc3\xa7\xc3\xb5" "es do X ligadas",
   "Os gostos, respostas, men\xc3\xa7\xc3\xb5" "es e mensagens aparecem aqui, tamb\xc3\xa9m durante o jogo.",
   "Desligadas nas Defini\xc3\xa7\xc3\xb5" "es do XPS5.", "O teu post est\xc3\xa1 a subir \xc2\xb7 %lld gostos",
   "Publica esta captura no X", "Publica este v\303\255deo no X",
   "Trof\303\251u de bronze \302\267 publica-o no X", "Trof\303\251u de prata \302\267 publica-o no X",
   "Trof\303\251u de ouro \302\267 publica-o no X", "Trof\303\251u de platina \302\267 publica-o no X",
   "O cart\303\243o da tua sess\303\243o est\303\241 pronto \302\267 publica-o no XPS5", "come\303\247ou a jogar %s",
   "Online no XPS5", "abriu o XPS5", "%d notifica\303\247\303\265" "es do X enquanto jogavas",
   "Ficaram em sil\303\252ncio durante um jogo competitivo. Muda isto nas Defini\303\247\303\265" "es do XPS5."},
  {"Abrir", "XPS5: falta la sesi\xc3\xb3n de X", "XPS5: X no acept\xc3\xb3 la sesi\xc3\xb3n",
   "Vinc\303\272lala otra vez desde un PC: XPS5, Configuraci\303\263n, Cuenta de X.", "%d notificaciones m\xc3\xa1s en X",
   "Notificaciones de X desactivadas", "Env\xc3\xad" "a el payload otra vez para activarlas.",
   "Notificaciones de X activadas",
   "Los Me gusta, respuestas, menciones y mensajes aparecen aqu\xc3\xad, tambi\xc3\xa9n durante el juego.",
   "Desactivadas en la Configuraci\xc3\xb3n de XPS5.", "Tu post est\xc3\xa1 en auge \xc2\xb7 %lld Me gusta",
   "Publica esta captura en X", "Publica este clip en X",
   "Trofeo de bronce \302\267 publ\303\255calo en X", "Trofeo de plata \302\267 publ\303\255calo en X",
   "Trofeo de oro \302\267 publ\303\255calo en X", "Trofeo de platino \302\267 publ\303\255calo en X",
   "La tarjeta de tu sesi\303\263n est\303\241 lista \302\267 publ\303\255cala en XPS5", "empez\303\263 a jugar a %s",
   "En l\303\255nea en XPS5", "abri\303\263 XPS5", "%d notificaciones de X mientras jugabas",
   "Se silenciaron durante un juego competitivo. C\303\241mbialo en la Configuraci\303\263n de XPS5."},
  {"Ouvrir", "XPS5 : session X absente", "XPS5 : X n'a pas accept\xc3\xa9 la session",
   "Lie-la de nouveau depuis un PC : XPS5, Param\303\250tres, Compte X.", "%d notifications de plus sur X",
   "Notifications X d\xc3\xa9sactiv\xc3\xa9" "es", "Renvoie le payload pour les activer.",
   "Notifications X activ\xc3\xa9" "es",
   "Les J'aime, r\xc3\xa9ponses, mentions et messages s'affichent ici, m\xc3\xaame en jeu.",
   "D\xc3\xa9sactiv\xc3\xa9" "es dans les Param\xc3\xa8tres de XPS5.", "Ton post est en hausse \xc2\xb7 %lld J'aime",
   "Publie cette capture sur X", "Publie ce clip sur X",
   "Troph\303\251e de bronze \302\267 publie-le sur X", "Troph\303\251e d'argent \302\267 publie-le sur X",
   "Troph\303\251e d'or \302\267 publie-le sur X", "Troph\303\251e de platine \302\267 publie-le sur X",
   "La carte de ta session est pr\303\252te \302\267 publie-la depuis XPS5", "a commenc\303\251 \303\240 jouer \303\240 %s",
   "En ligne sur XPS5", "a ouvert XPS5", "%d notifications X pendant que tu jouais",
   "Mises en sourdine pendant un jeu comp\303\251titif. Change-le dans les Param\303\250tres de XPS5."},
  {"\xc3\x96" "ffnen", "XPS5: X-Sitzung fehlt", "XPS5: X hat die Sitzung nicht akzeptiert",
   "Verkn\303\274pfe sie erneut vom PC aus: XPS5, Einstellungen, X-Konto.", "%d weitere Mitteilungen auf X",
   "X-Mitteilungen aus", "Sende den Payload erneut, um sie einzuschalten.", "X-Mitteilungen an",
   "Likes, Antworten, Erw\xc3\xa4hnungen und Nachrichten erscheinen hier, auch im Spiel.",
   "In den Einstellungen von XPS5 ausgeschaltet.", "Dein Post ist im Aufwind \xc2\xb7 %lld Likes",
   "Poste diesen Screenshot auf X", "Poste diesen Clip auf X",
   "Bronze-Troph\303\244e \302\267 poste sie auf X", "Silber-Troph\303\244e \302\267 poste sie auf X",
   "Gold-Troph\303\244e \302\267 poste sie auf X", "Platin-Troph\303\244e \302\267 poste sie auf X",
   "Deine Sitzungskarte ist fertig \302\267 poste sie in XPS5", "spielt jetzt %s",
   "Online auf XPS5", "hat XPS5 ge\303\266ffnet", "%d X-Mitteilungen, w\303\244hrend du gespielt hast",
   "W\303\244hrend eines kompetitiven Spiels stummgeschaltet. \303\204ndere das in den Einstellungen von XPS5."},
  {"Apri", "XPS5: manca la sessione di X", "XPS5: X non ha accettato la sessione",
   "Collegala di nuovo da un PC: XPS5, Impostazioni, Account X.", "Altre %d notifiche su X", "Notifiche di X disattivate",
   "Invia di nuovo il payload per attivarle.", "Notifiche di X attivate",
   "Mi piace, risposte, menzioni e messaggi compaiono qui, anche durante il gioco.",
   "Disattivate nelle Impostazioni di XPS5.", "Il tuo post \xc3\xa8 in ascesa \xc2\xb7 %lld Mi piace",
   "Pubblica questo screenshot su X", "Pubblica questa clip su X",
   "Trofeo di bronzo \302\267 pubblicalo su X", "Trofeo d'argento \302\267 pubblicalo su X",
   "Trofeo d'oro \302\267 pubblicalo su X", "Trofeo di platino \302\267 pubblicalo su X",
   "La scheda della tua sessione \303\250 pronta \302\267 pubblicala da XPS5", "ha iniziato a giocare a %s",
   "Online su XPS5", "ha aperto XPS5", "%d notifiche di X mentre giocavi",
   "Silenziate durante un gioco competitivo. Cambialo nelle Impostazioni di XPS5."},
  {"Openen", "XPS5: X-sessie ontbreekt", "XPS5: X accepteerde de sessie niet",
   "Koppel de sessie opnieuw vanaf een pc: XPS5, Instellingen, X-account.", "Nog %d meldingen op X", "X-meldingen uit",
   "Stuur de payload opnieuw om ze aan te zetten.", "X-meldingen aan",
   "Likes, antwoorden, vermeldingen en berichten verschijnen hier, ook tijdens het spel.",
   "Uitgezet in de Instellingen van XPS5.", "Je post stijgt \xc2\xb7 %lld likes",
   "Post deze screenshot op X", "Post deze clip op X",
   "Bronzen trofee \302\267 post hem op X", "Zilveren trofee \302\267 post hem op X",
   "Gouden trofee \302\267 post hem op X", "Platina trofee \302\267 post hem op X",
   "Je sessiekaart is klaar \302\267 post hem vanuit XPS5", "is %s gaan spelen",
   "Online op XPS5", "heeft XPS5 geopend", "%d X-meldingen terwijl je speelde",
   "Stil gehouden tijdens een competitieve game. Wijzig dit in de Instellingen van XPS5."},
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


// for the counts of the title's bar ("badges"), and what a toast's Open is for
enum { OPEN_NONE, OPEN_NOTICES, OPEN_CHAT };

static struct {
  int notices;                  // notices since the user last looked at them
  long long notices_seen;       // when that was, X's time; 0 before the first reading
  long long chats_seen;
  long long newest_notice;      // the newest time read of each
  long long newest_chat;
  long long notice_toast_at;    // when the last toast with Open told of a notice; 0 none
  long long chat_toast_at;      // of a message
} g_badges;


/*
 * The messages for the bar's count: for each conversation with unread
 * messages from someone else since the user last looked at the list, how
 * many came. A reading sees each conversation's last message only, so the
 * messages of one conversation that came between two readings count as one.
 * The conversation the title has open ("badges ... <id>") counts as read and
 * keeps its last time, so what was read there does not come back.
 */
#define COUNTED_MAX (2 * DM_MAX)

static struct {
  char      id[X_ID_MAX];
  long long time;  // its last message counted, X's time
  int       n;     // its messages since the user last looked
  int       inbox; // 0 the direct messages', 1 X Chat's
} g_counted[COUNTED_MAX];
static int g_ncounted;


static int
counted_find(const char *id) {
  for(int i=0; i < g_ncounted; i++) {
    if(!strcmp(g_counted[i].id, id)) {
      return i;
    }
  }
  return -1;
}


/**
 * an inbox's reading counted: chats are its count unread conversations
 * (xnotify_dm_fresh kept only those), the newer than the last look counted
 **/
static void
count_messages(const x_post_t *chats, int count, int inbox) {
  int i, k, at;

  // this inbox's no longer unread: read somewhere else
  for(i=0; i < g_ncounted;) {
    for(k=0; g_counted[i].inbox == inbox && k < count && strcmp(chats[k].id, g_counted[i].id); k++) {
    }
    if(g_counted[i].inbox == inbox && k == count) {
      g_counted[i] = g_counted[--g_ncounted];
    } else {
      i++;
    }
  }
  for(k=0; k < count; k++) {
    if(!chats[k].id[0] || chats[k].time <= g_badges.chats_seen) {
      continue;
    }
    if((at=counted_find(chats[k].id)) < 0) {
      if(g_ncounted < COUNTED_MAX) {
        at = g_ncounted++;
        snprintf(g_counted[at].id, sizeof g_counted[at].id, "%s", chats[k].id);
        g_counted[at].time = chats[k].time;
        g_counted[at].n = 1;
        g_counted[at].inbox = inbox;
      }
    } else if(chats[k].time > g_counted[at].time) {
      g_counted[at].time = chats[k].time;
      g_counted[at].n++;
    }
  }
}


static int
counted_sum(void) {
  int n = 0;

  for(int i=0; i < g_ncounted; i++) {
    n += g_counted[i].n;
  }
  return n;
}


/**
 * the conversation the title shows read: whether it had messages counted
 **/
static int
counted_read(const char *id) {
  const int at = id[0] && strcmp(id, "-") ? counted_find(id) : -1;
  int had;

  if(at < 0) {
    return 0;
  }
  had = g_counted[at].n > 0;
  g_counted[at].n = 0;
  return had;
}


/**
 * the seconds until an inbox's next look, after one that answered status:
 * X asking to wait (429) waits WAIT_LIMITED, and from then on the looks
 * come half as often (up to DM_SLOWEST apart)
 **/
static int g_dm_every = DM_EVERY;

static int
chat_wait(int status) {
  if(status != 429) {
    return g_dm_every;
  }
  if(g_dm_every < DM_SLOWEST) {
    g_dm_every = g_dm_every * 2 > DM_SLOWEST ? DM_SLOWEST : g_dm_every * 2;
    log_line("messages: X asked to wait; a look every %d s from now on", g_dm_every);
  }
  return WAIT_LIMITED;
}


/**
 * a toast with Open told of what (OPEN_*)
 **/
static void
toasted(int what) {
  if(what == OPEN_CHAT) {
    g_badges.chat_toast_at = (long long)time(NULL);
  } else {
    g_badges.notice_toast_at = (long long)time(NULL);
  }
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


/*
 * Quiet in competitive games (the settings' quiet): while one runs, the
 * toasts of X's notifications and messages (the ones with Open) are kept
 * back and counted; once it closes, one toast says how many came. The bar's
 * counts go on as ever.
 */

static void game_name_of(const char *id, char *name, size_t size);

static struct {
  char      game[32];     // the title id of the game seen last, or empty
  int       competitive;  // it is played against others (game_competitive)
  int       kept;         // toasts kept back while it ran
  long long looked;       // when it was looked at
} g_quiet;


/**
 * whether a competitive game runs and the settings want quiet then; the
 * game looked at once every few seconds
 **/
static int
quiet_now(void) {
  const long long now = (long long)time(NULL);
  char id[32] = "", name[GAME_NAME];
  int app;

  if(!g_settings.quiet) {
    return 0;
  }
  if(now - g_quiet.looked < 5 && now >= g_quiet.looked) {
    return g_quiet.competitive;
  }
  g_quiet.looked = now;
  app = sceSystemServiceGetAppIdOfRunningBigApp();
  if(app <= 0 || sceSystemServiceGetAppTitleId(app, id) || (id[sizeof id - 1] = 0, !game_id_ok(id))) {
    id[0] = 0;
  }
  if(strcmp(id, g_quiet.game)) {
    snprintf(g_quiet.game, sizeof g_quiet.game, "%s", id);
    name[0] = 0;
    if(id[0]) {
      game_name_of(id, name, sizeof name);
    }
    g_quiet.competitive = id[0] && game_competitive(name);
    log_line("quiet: %s", !id[0] ? "no game" : g_quiet.competitive ? "a competitive game runs" : "a game runs");
  }
  return g_quiet.competitive;
}


/**
 * the toast, and again without its third line when the PS5 did not take it
 **/
static void
toast(const xnotify_toast_t *t, int open) {
  static xnotify_toast_t plain;

  if(open && quiet_now()) {
    g_quiet.kept++; // what it said is not kept: only that one came
    return;
  }
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
 * once the competitive game closed (or quiet was turned off): how many
 * toasts were kept back, in one with Open
 **/
static void
quiet_over(void) {
  xnotify_toast_t t;

  if(!g_quiet.kept || quiet_now()) {
    return;
  }
  memset(&t, 0, sizeof t);
  snprintf(t.title, sizeof t.title, text(T_QUIET), g_quiet.kept);
  snprintf(t.body, sizeof t.body, "%s", text(T_QUIET_BODY));
  t.icon = "messages";
  log_line("quiet: %d toasts kept back, said in one", g_quiet.kept);
  g_quiet.kept = 0;
  toast(&t, 1);
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
    if(!g_badges.notices_seen) {
      // the first reading: what is there already is not new
      g_badges.notices_seen = (long long)time(NULL);
      xnotify_newer(posts, count, 0, &g_badges.notices_seen);
    }
    g_badges.notices = xnotify_newer(posts, count, g_badges.notices_seen, &g_badges.newest_notice);
    if(found) {
      toasted(OPEN_NOTICES);
    }
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
 * Friends who start a game or open XPS5: the people the user follows whose
 * profile says they play or are on XPS5 (their XPS5's "Playing on your
 * profile" and "Online on your profile", play.h), looked at every
 * FRIENDS_EVERY; a game that was not there the look before is said once,
 * and so is XPS5 opened by someone who was neither on it nor playing.
 */

static void seen_keep(const x_post_t *posts, int count);

typedef struct friends {
  int  read;                            // a look was made: the first only notes
  int  count;
  char id[FRIENDS_COUNT][24];           // their account numbers
  char game[FRIENDS_COUNT][PLAY_GAME];  // what each played at the last look, or empty
  char online[FRIENDS_COUNT];           // or whether they were on XPS5
} friends_t;


/**
 * where a friend was at the last look, or -1 when they were not among the
 * ones looked at
 **/
static int
friend_index(const friends_t *f, const char *id) {
  for(int i=0; i < f->count; i++) {
    if(!strcmp(f->id[i], id)) {
      return i;
    }
  }
  return -1;
}


/**
 * a look at the people the user follows, for one who started a game since
 * the last look (its toast shown); the HTTP status, or -1
 **/
static int
read_friends(friends_t *f) {
  static char url[8192];
  static friends_t now;
  const char *uid = g_settings.ids[g_settings.account - 1];
  char path[64], *at;
  int ret, count, total, playing = 0, online = 0, told = 0, was;
  xnotify_toast_t t;
  https_response_t res;
  x_session_t session;
  x_request_t request;
  x_post_t *posts;
  json_doc_t *doc;
  size_t off;

  if(!uid[0] || x_feed_url(url, sizeof url, X_FEED_FRIENDS, uid, FRIENDS_COUNT, NULL)) {
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
    log_line("friends: network 0x%08x", (unsigned)ret);
    return -1;
  }
  if(res.status != 200 || !(doc=json_parse(res.body, res.len, &off))) {
    log_line("friends: HTTP %d", res.status);
    mem_free(res.body);
    return res.status;
  }
  if((posts=calloc(FRIENDS_COUNT, sizeof *posts))) {
    count = x_timeline_posts(json_root(doc), posts, FRIENDS_COUNT, &total);
    memset(&now, 0, sizeof now);
    for(int i=0; i < count; i++) {
      const x_post_t *p = &posts[i];
      if(p->kind != X_KIND_USER || !p->user_id[0] || now.count >= FRIENDS_COUNT) {
        continue;
      }
      snprintf(now.id[now.count], sizeof now.id[0], "%s", p->user_id);
      snprintf(now.game[now.count], sizeof now.game[0], "%s", p->playing);
      now.online[now.count] = (char)(!p->playing[0] && p->online);
      now.count++;
      if(!p->playing[0] && !p->online) {
        continue;
      }
      playing += p->playing[0] != 0;
      online += !p->playing[0];
      was = friend_index(f, p->user_id);
      if(!f->read || told >= FRIENDS_TOASTS) {
        continue; // the first look only notes
      }
      // a game said before is not said again; XPS5, only to someone who was
      // neither on it nor playing (a game closed is not news)
      if(p->playing[0] ? was >= 0 && !strcmp(f->game[was], p->playing) :
         was >= 0 && (f->online[was] || f->game[was][0])) {
        continue;
      }
      memset(&t, 0, sizeof t);
      snprintf(t.title, sizeof t.title, "%s", p->name[0] ? p->name : p->user);
      if(p->playing[0]) {
        snprintf(t.body, sizeof t.body, text(T_FRIEND), p->playing);
      } else {
        snprintf(t.body, sizeof t.body, "%s", text(T_FRIEND_ONLINE));
      }
      snprintf(t.avatar, sizeof t.avatar, "%s", p->avatar);
      if((at=strstr(t.avatar, "_normal."))) {
        memcpy(at, "_bigger", 7); // the bigger picture of the same person, as long a name
      }
      t.icon = "messages";
      toast(&t, 1);
      memset(&t, 0, sizeof t);
      told++;
    }
    now.read = 1;
    *f = now;
    log_line("friends: %d people, %d playing, %d on XPS5, %d said", now.count, playing, online, told);
    if(count > 0) {
      seen_keep(posts, count);
    }
    memset(posts, 0, FRIENDS_COUNT * sizeof *posts);
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
 * a look at the people the user follows, into the account's file of who
 * was seen around (seen.h) that the title shows its friends from; the
 * title notes its own looks into it too, so it is read first. Only counts
 * go to the klog
 **/
static void
seen_keep(const x_post_t *posts, int count) {
  static seen_t seen;
  static char out[SEEN_MOST];
  char path[64], fresh[72], *old;
  size_t len = 0;
  long long n;
  int fd, ok = 0, added;

  if(seen_path(g_settings.account, path, sizeof path) || snprintf(fresh, sizeof fresh, "%s.new", path) < 0) {
    return;
  }
  old = read_small(path, SEEN_MOST, &len);
  seen_parse(&seen, old, old ? len : 0);
  free(old);
  added = seen_note(&seen, posts, count, (long long)time(NULL));
  if((n=seen_format(&seen, out, sizeof out)) > 0 && (fd=open(fresh, O_WRONLY | O_CREAT | O_TRUNC, 0666)) >= 0) {
    ok = !write_all(fd, out, (size_t)n);
    close(fd);
    ok = ok && !rename(fresh, path);
    if(!ok) {
      unlink(fresh);
    }
  }
  log_line("seen: %d people, %d changes, kept %d", seen.count, added, ok);
  memset(&seen, 0, sizeof seen);
  memset(out, 0, sizeof out);
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
    if(!strncmp(line, "game=", 5) && (game_id_ok(line + 5) || !strcmp(line + 5, PLAY_XPS5_ID))) {
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
 * the x-client-transaction-id of a request (method, "GET" or "POST") to
 * url, as the title signs its own; returns whether tid holds it
 **/
static int
sign_request(const x_session_t *s, const char *method, const char *url, char *tid, size_t size) {
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
  return g_tid_at && !xtid_make(&g_tid, method, buf, now, random_byte(), tid, size);
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
  signed_tid = sign_request(s, "POST", url, tid, sizeof tid);
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
 * a look at the inbox, as x.com's Messages reads it, for messages that came
 * since the last look (their toasts shown, the oldest first); the HTTP
 * status, or -1
 **/
static int
read_dms(xnotify_t *n) {
  static char url[4096];
  const char *me = g_settings.ids[g_settings.account - 1];
  char path[64], tid[XTID_HEADER_MAX];
  https_header_t headers[X_HEADER_COUNT + 1];
  int fresh[SHOW_MAX], ret, total, count, found, heads = X_HEADER_COUNT, signed_tid;
  xnotify_toast_t t;
  https_response_t res;
  x_session_t session;
  x_request_t request;
  x_post_t *chats;
  json_doc_t *doc;
  size_t off;

  if(!me[0] || x_dm_inbox_url(url, sizeof url)) {
    return -1;
  }
  if(settings_session_path(g_settings.account, path, sizeof path)) {
    snprintf(path, sizeof path, "%s", X_SESSION_PATH);
  }
  if(x_session_read(&session, path)) {
    return -1;
  }
  signed_tid = sign_request(&session, "GET", url, tid, sizeof tid);
  x_request_init(&request, &session);
  memcpy(headers, request.headers, sizeof request.headers);
  if(signed_tid) {
    headers[heads++] = (https_header_t){"x-client-transaction-id", tid};
  }
  ret = https_get(url, headers, heads, &res);
  memset(&session, 0, sizeof session);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  if(ret) {
    log_line("dms: network 0x%08x", (unsigned)ret);
    return -1;
  }
  if(signed_tid && res.status == 404) {
    g_tid_at = 0; // a new page's key next time
  }
  if(res.status != 200 || !(doc=json_parse(res.body, res.len, &off))) {
    log_line("dms: HTTP %d%s", res.status, signed_tid ? " (tid)" : "");
    mem_free(res.body);
    return res.status == 200 ? -1 : res.status;
  }
  total = x_dm_inbox(json_root(doc), me, NULL, 0);
  total = total < 1 ? 1 : total > DM_MAX ? DM_MAX : total;
  if((chats=calloc(total, sizeof *chats))) {
    count = x_dm_inbox(json_root(doc), me, chats, total);
    ret = count;
    found = xnotify_dm_fresh(n, chats, &count, fresh, SHOW_MAX);
    log_line("dms: %d conversations, %d unread, %d new", ret, count, found);
    xnotify_newer(chats, count, g_badges.chats_seen, &g_badges.newest_chat);
    count_messages(chats, count, 0);
    if(found) {
      toasted(OPEN_CHAT);
    }
    // the oldest first, so the newest is the last on the screen
    for(int i=(found < SHOW_MAX ? found : SHOW_MAX) - 1; i >= 0; i--) {
      xnotify_dm_toast(&chats[fresh[i]], g_lang, &t);
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
    memset(chats, 0, total * sizeof *chats);
    free(chats);
  }
  json_free(doc);
  mem_free(res.body);
  return 200;
}


/**
 * the X Chat key the title kept for the account whose number is me, so the
 * inbox's last messages open and their toasts say what was written; read
 * again at each look, so a key the title forgot is forgotten here too. The
 * key and the words never go to the log.
 **/
static void
xchat_key(const char *me) {
  char path[XCHAT_KEY_FILE_MAX];
  uint8_t identity[32];

  if(me && me[0] && !xchat_open_path(XCHAT_KEY_DIR, me, path, sizeof path) &&
     !xchat_open_load(path, identity)) {
    if(!xchat_open_ready(me)) {
      log_line("xchat: the account's key is in hand");
    }
    xchat_open_set(me, identity);
  } else if(xchat_open_ready(NULL)) {
    xchat_open_forget();
    log_line("xchat: no key for the account");
  }
  xcrypt_wipe(identity, sizeof identity);
}


/**
 * a look at X Chat's inbox, as x.com's Chat reads it, for messages that came
 * since the last look: their toasts say who wrote and, with the key the
 * title kept for the account (xchat_key), what; without it the words stay
 * encrypted; the HTTP status, or -1
 **/
static int
read_xchat(xnotify_t *n) {
  static char url[1024], uuid[37];
  const char *me = g_settings.ids[g_settings.account - 1];
  char path[64], tid[XTID_HEADER_MAX];
  unsigned char bytes[16];
  https_header_t headers[XCHAT_HEADER_COUNT];
  int fresh[SHOW_MAX], ret, total, count, found, heads, signed_tid;
  xnotify_toast_t t;
  https_response_t res;
  x_session_t session;
  x_request_t request;
  x_post_t *chats;
  json_doc_t *doc;
  size_t off;

  if(!me[0] || xchat_inbox_url(url, sizeof url)) {
    xchat_key(NULL);
    return -1;
  }
  xchat_key(me);
  if(!uuid[0]) {
    // this copy's own, as a browser keeps one
    for(size_t i=0; i < sizeof bytes; i++) {
      bytes[i] = random_byte();
    }
    xchat_uuid(bytes, uuid);
  }
  if(settings_session_path(g_settings.account, path, sizeof path)) {
    snprintf(path, sizeof path, "%s", X_SESSION_PATH);
  }
  if(x_session_read(&session, path)) {
    return -1;
  }
  signed_tid = sign_request(&session, "GET", url, tid, sizeof tid);
  x_request_init(&request, &session);
  heads = xchat_headers(&request, uuid, signed_tid ? tid : NULL, headers);
  ret = https_get(url, headers, heads, &res);
  memset(&session, 0, sizeof session);
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  if(ret) {
    log_line("xchat: network 0x%08x", (unsigned)ret);
    return -1;
  }
  if(res.status != 200 || !(doc=json_parse(res.body, res.len, &off))) {
    log_line("xchat: HTTP %d%s, %zu bytes", res.status, signed_tid ? " (tid)" : "", res.len);
    mem_free(res.body);
    return res.status == 200 ? -1 : res.status;
  }
  if((total=xchat_inbox(json_root(doc), me, NULL, 0)) < 0) {
    log_line("xchat: HTTP 200, %zu bytes, not an inbox", res.len);
    json_free(doc);
    mem_free(res.body);
    return -1;
  }
  total = total < 1 ? 1 : total > DM_MAX ? DM_MAX : total;
  if((chats=calloc(total, sizeof *chats))) {
    count = xchat_inbox(json_root(doc), me, chats, total);
    count = count < 0 ? 0 : count;
    ret = count;
    for(int i=0; i < count; i++) {
      // the words X sent unencrypted are in the inbox too, and read_dms
      // tells of them: here only the encrypted ones, opened or not
      chats[i].unread &= chats[i].encrypted;
    }
    found = xnotify_dm_fresh(n, chats, &count, fresh, SHOW_MAX);
    log_line("xchat: %zu bytes, %d conversations, %d unread, %d new", res.len, ret, count, found);
    xnotify_newer(chats, count, g_badges.chats_seen, &g_badges.newest_chat);
    count_messages(chats, count, 1);
    if(found) {
      toasted(OPEN_CHAT);
    }
    for(int i=(found < SHOW_MAX ? found : SHOW_MAX) - 1; i >= 0; i--) {
      xnotify_dm_toast(&chats[fresh[i]], g_lang, &t);
      toast(&t, 1);
      memset(&t, 0, sizeof t);
    }
    if(found > SHOW_MAX) {
      memset(&t, 0, sizeof t);
      snprintf(t.title, sizeof t.title, text(T_MORE), found - SHOW_MAX);
      snprintf(t.body, sizeof t.body, "X Chat");
      t.icon = "messages";
      toast(&t, 1);
    }
    memset(chats, 0, total * sizeof *chats);
    free(chats);
  }
  json_free(doc);
  mem_free(res.body);
  return 200;
}


/**
 * whether a location is one of XPS5's: a game, or "on XPS5"
 **/
static int
play_ours(const char *location) {
  char game[PLAY_GAME];

  return play_game_of(location, game, sizeof game) || play_online_of(location);
}


/**
 * a look at the game that runs, and at XPS5: once it has stayed for two
 * looks, the profile says the game (when the settings want it), else "on
 * XPS5" while XPS5 is open (when they want that), else what it said before;
 * returns the seconds until the next look
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
  // what the profile is to say, by a title id: the game's, or XPS5's own
  if(!(g_settings.playing && id[0])) {
    snprintf(id, sizeof id, "%s", g_settings.online && sceLncUtilGetAppId(PLAY_XPS5_ID) > 0 ? PLAY_XPS5_ID : "");
  }
  if(strcmp(id, g_play.seen)) {
    // a game or XPS5 opened or closed: one more look first, so a moment does not count
    snprintf(g_play.seen, sizeof g_play.seen, "%s", id);
    g_play.steady = 1;
    return PLAY_EVERY;
  }
  g_play.steady += g_play.steady < 2;
  want = id[0] != 0;
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
    if(play_ours(now)) {
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
  log_line("playing: the profile %s", !want ? "gets back what it said" :
           strcmp(id, PLAY_XPS5_ID) ? "gets the game" : "says XPS5");
  if(want) {
    if(!strcmp(id, PLAY_XPS5_ID)) {
      ret = play_online_status(text(T_ONLINE), status, sizeof status);
    } else {
      game_name_of(id, name, sizeof name);
      ret = play_status(name[0] ? name : id, status, sizeof status);
    }
    if(ret) {
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
      snprintf(g_play.before, sizeof g_play.before, "%s", play_ours(now) ? "" : now);
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
  return g_settings.playing || g_settings.online || g_play.shown[0] || !g_play.checked ||
         g_play.account != g_settings.account;
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
 * the set's trophies, with their names in the language of the settings
 * (else English), from its TROPHY.UCP, into list; how many, or -1
 **/
static int
set_list(const char *set, trophy_info_t *list, int max) {
  char path[96], file[48];
  unsigned char *head = NULL, *table = NULL, *conf = NULL, *meta = NULL;
  unsigned long long off, len, meta_len = 0;
  size_t table_len;
  int count, n = -1;

  snprintf(path, sizeof path, "/user/trophy2/nobackup/conf/%s/TROPHY.UCP", set);
  if(!(head=read_part(path, 0, TROPHY_UCP_HEAD)) || (count=trophy_ucp_files(head, TROPHY_UCP_HEAD)) < 0) {
    goto out;
  }
  table_len = (size_t)count * TROPHY_UCP_ENTRY;
  if(!(table=read_part(path, TROPHY_UCP_HEAD, table_len)) ||
     trophy_ucp_find(table, table_len, count, "tropconf.json", &off, &len) || len > TROPHY_FILE_MOST ||
     !(conf=read_part(path, off, len))) {
    goto out;
  }
  if(!trophy_ucp_meta(table, table_len, count, game_locale(g_lang), file, sizeof file) &&
     !trophy_ucp_find(table, table_len, count, file, &off, &meta_len) && meta_len <= TROPHY_FILE_MOST) {
    meta = read_part(path, off, meta_len);
  }
  n = trophy_list((char *)conf, (size_t)len, (char *)meta, meta ? (size_t)meta_len : 0, list, max);
out:
  free(head);
  free(table);
  free(conf);
  free(meta);
  return n;
}


/*
 * The sessions of play (sessions.h): when a game opens and closes, and the
 * trophies and captures between, for the title's cards.
 */

#define SESSION_ANSWER (12 * 3600) // how long one that ended waits for the title
#define SESSIONS_MOST  (256 << 10) // bytes of the file read
#define TROPHY_LIST    512         // trophies of a set listed at most

static struct {
  session_t now;                   // id empty: none under way
  int       best_grade;            // of its best trophy, -1 for none
  session_t last;                  // id empty: none waiting for the title
  long long ended;                 // when last ended
} g_sess = {.best_grade = -1};


/**
 * a trophy of game earned: the session's, when it is of its game
 **/
static void
session_trophy(const char *game, int grade, const char *name) {
  if(!g_sess.now.id[0] || strcmp(game, g_sess.now.id) || grade < 0 || grade > 3) {
    return;
  }
  g_sess.now.trophies[grade]++;
  if(grade > g_sess.best_grade && name && name[0]) {
    g_sess.best_grade = grade;
    session_text(name, g_sess.now.best, sizeof g_sess.now.best);
  }
}


/**
 * a new capture of game: the session's, and its screenshots for its card
 **/
static void
session_shot(const char *game, const capture_t *c) {
  session_t *s = &g_sess.now;

  if(!s->id[0] || strcmp(game, s->id)) {
    return;
  }
  s->shots++;
  if(c->kind != CAPTURE_PNG && c->kind != CAPTURE_JPEG && c->kind != CAPTURE_JXR) {
    return;
  }
  if(s->pic_count == SESSION_PICS) {
    memmove(s->pics[0], s->pics[1], (SESSION_PICS - 1) * sizeof s->pics[0]);
    s->pic_count--;
  }
  snprintf(s->pics[s->pic_count++], sizeof s->pics[0], "%s", c->path);
}


/**
 * the session into the file, which keeps the last weeks
 **/
static void
sessions_append(const session_t *s) {
  const size_t room = SESSIONS_MOST + 2 * SESSION_LINE;
  char *old, *out;
  size_t len = 0;
  long long n;
  int fd, ok = 0;

  old = read_small(SESSIONS_PATH, SESSIONS_MOST, &len);
  if(!(out=malloc(room))) {
    free(old);
    return;
  }
  n = sessions_kept(old, old ? len : 0, s->end - SESSIONS_KEEP, s, out, room);
  free(old);
  if(n > 0 && (fd=open(SESSIONS_PATH ".new", O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0) {
    ok = !write_all(fd, out, (size_t)n);
    close(fd);
    ok = ok && !rename(SESSIONS_PATH ".new", SESSIONS_PATH);
    if(!ok) {
      unlink(SESSIONS_PATH ".new");
    }
  }
  log_line("sessions: one of %lld min kept %d, the file %lld bytes", (s->end - s->start) / 60, ok, n);
  free(out);
}


/**
 * the session under way ends: kept and offered when it was long enough
 **/
static void
end_session(void) {
  session_t *s = &g_sess.now;
  char took[32];
  xnotify_toast_t t;

  if(s->end - s->start >= SESSION_LEAST) {
    g_sess.last = *s;
    g_sess.ended = (long long)time(NULL);
    sessions_append(s);
    session_duration(s->end - s->start, took, sizeof took);
    memset(&t, 0, sizeof t);
    snprintf(t.title, sizeof t.title, "%s", text(T_SESSION));
    snprintf(t.body, sizeof t.body, "%s \xc2\xb7 %s", s->name[0] ? s->name : s->id, took);
    t.icon = "messages";
    toast(&t, 1);
    memset(&t, 0, sizeof t);
  } else {
    log_line("sessions: one of %lld s, too short", s->end - s->start);
  }
  memset(s, 0, sizeof *s);
  g_sess.best_grade = -1;
}


/**
 * a look at the game that is open: a session starts with it, and ends when
 * it closes, another opens, or it went unseen a while (the PS5 at rest)
 **/
static void
watch_session(long long now) {
  char id[32], name[GAME_NAME];
  session_t *s = &g_sess.now;
  int app, open = open_game(id, sizeof id, &app);

  if(s->id[0] && (now - s->end > SESSION_GAP || now < s->end || !open || strcmp(id, s->id))) {
    end_session();
  }
  if(open && !s->id[0]) {
    memset(s, 0, sizeof *s);
    snprintf(s->id, sizeof s->id, "%s", id);
    name[0] = 0;
    game_name_of(id, name, sizeof name);
    session_text(name, s->name, sizeof s->name);
    s->start = now;
    g_sess.best_grade = -1;
    log_line("sessions: one starts");
  }
  if(s->id[0]) {
    s->end = now;
  }
}


/**
 * "session": the session that ended last, once: "ok", its line, its best
 * trophy, its screenshots and "end"; or "none"
 **/
static void
serve_session(int c) {
  char line[SESSION_LINE + SESSION_PATH + 16];
  session_t *s = &g_sess.last;
  int bad;

  if(!s->id[0] || (long long)time(NULL) - g_sess.ended > SESSION_ANSWER || session_line(s, line, sizeof line)) {
    (void)!write_all(c, "none\n", 5);
    return;
  }
  bad = write_all(c, "ok\n", 3) || write_all(c, line, strlen(line));
  snprintf(line, sizeof line, "best\t%s\n", s->best);
  bad = bad || write_all(c, line, strlen(line));
  for(int i=0; i < s->pic_count && !bad; i++) {
    snprintf(line, sizeof line, "pic\t%s\n", s->pics[i]);
    bad = write_all(c, line, strlen(line));
  }
  bad = bad || write_all(c, "end\n", 4);
  log_line("sessions: one given to the title %d", !bad);
  memset(s, 0, sizeof *s);
}


/**
 * "week": "ok", the lines of the sessions of the last seven days, "end"
 **/
static void
serve_week(int c) {
  long long from = (long long)time(NULL) - WEEK_SECONDS;
  char *text, line[SESSION_LINE + 64];
  size_t len = 0, i = 0, n;
  session_t s;
  int bad, sent = 0;

  text = read_small(SESSIONS_PATH, SESSIONS_MOST, &len);
  bad = write_all(c, "ok\n", 3);
  while(text && i < len && !bad) {
    n = strcspn(text + i, "\n");
    if(n + 2 < sizeof line) {
      memcpy(line, text + i, n);
      line[n] = 0;
      if(!session_parse(line, &s) && s.end >= from) {
        line[n] = '\n';
        bad = write_all(c, line, n + 1);
        sent++;
      }
    }
    i += n + 1;
  }
  bad = bad || write_all(c, "end\n", 4);
  log_line("sessions: %d of the week given %d", sent, !bad);
  free(text);
}


/**
 * "trophies <title id>": "ok <earned> <all>", a line for each trophy (id,
 * grade, hidden, earned, name and description between tabs; a hidden one
 * not earned yet says nothing of itself), then "end"; or "none"
 **/
static void
serve_trophies(int c, const char *id) {
  char set[TROPHY_SET_LEN], path[160], line[TROPHY_NAME + TROPHY_DETAIL + 48];
  trophy_info_t *list = NULL;
  trophy_earned_t *have = NULL;
  unsigned char *record;
  long long mtime;
  size_t len;
  int n = -1, earned = 0, bad, mine;

  set[0] = 0;
  if(game_id_ok(id)) {
    set_of_game(id, set);
  }
  if(set[0] && (list=malloc(TROPHY_LIST * sizeof *list)) && (n=set_list(set, list, TROPHY_LIST)) > 0 &&
     (have=calloc(1, sizeof *have)) && !record_of_set(set, path, sizeof path, &mtime) &&
     (record=(unsigned char *)read_small(path, TROPHY_RECORD_MOST, &len))) {
    if(trophy_earned(record, len, have) < 0) {
      memset(have, 0, sizeof *have);
    }
    free(record);
  }
  if(n <= 0) {
    (void)!write_all(c, "none\n", 5);
    log_line("trophies: no list (set %d)", !!set[0]);
    free(list);
    free(have);
    return;
  }
  for(int i=0; i<n; i++) {
    earned += have && trophy_has(have, list[i].id);
  }
  snprintf(line, sizeof line, "ok %d %d\n", earned, n);
  bad = write_all(c, line, strlen(line));
  for(int i=0; i < n && !bad; i++) {
    mine = have && trophy_has(have, list[i].id);
    snprintf(line, sizeof line, "%d\t%d\t%d\t%d\t%s\t%s\n", list[i].id, list[i].grade, list[i].hidden, mine,
             list[i].hidden && !mine ? "" : list[i].name, list[i].hidden && !mine ? "" : list[i].detail);
    bad = write_all(c, line, strlen(line));
  }
  bad = bad || write_all(c, "end\n", 4);
  log_line("trophies: %d of %d listed %d", earned, n, !bad);
  free(list);
  free(have);
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
      if(trophy_about(g_troph.set, i, &grade, name, sizeof name)) {
        continue;
      }
      session_trophy(g_troph.game, grade, name);
      if(grade > best_grade) {
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
 * The new captures, offered for a post. A capture is new when its path was
 * not there at the look before: on the console a clip's date told it, but
 * screenshots never came out newer than the newest capture seen before
 * (2026-11-14), so the dates are only logged. A new file is offered once
 * its size holds from one look to the next (a clip is written for a while).
 */

#define SHOT_AGAIN   2           // seconds to the next look while a new one is still written
#define SHOT_GAP     60          // a look this long after the one before only notes what is there
#define SHOT_PATHS   (1 << 18)   // paths remembered at most

static struct {
  char      game[32];            // the title id of the game that runs (or ran last)
  int       known;               // the paths below are of a look before
  uint64_t *paths;               // their hashes, sorted
  int       count;
  long long looked;              // when the last look was
  capture_t wait;                // a new one still written, path empty for none
  int       still;               // looks it has kept its size
  capture_t pending;             // a new one for the title, path empty for none
  long long found;               // when it was found
  long long toasted;             // when the last toast about one went
  int       error;               // the errno the last look ended on, for the log
  int       full;                // more files than SHOT_PATHS, so nothing is new
} g_shots;


typedef struct shot_look {
  uint64_t *paths;               // this look's hashes
  int       count, room;
  int       fresh;               // new pictures and clips
  capture_t newest;              // the newest of them
  int       other;               // new files that are neither (thumbnails, other kinds)
  char      other_ext[32];       // their extensions, for the log
  int       kinds[5], others;    // all the files, by kind
  int       wait_seen;           // g_shots.wait is still there
  capture_t wait_now;            // as it is now
  int       full;
} shot_look_t;


static uint64_t
path_hash(const char *s) {
  uint64_t h = 0xcbf29ce484222325ULL;

  while(*s) {
    h = (h ^ (unsigned char)*s++) * 0x100000001b3ULL;
  }
  return h;
}


static int
hash_order(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

  return x < y ? -1 : x > y;
}


static int
path_known(uint64_t h) {
  int lo = 0, hi = g_shots.count - 1;

  while(lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    if(g_shots.paths[mid] == h) {
      return 1;
    }
    if(g_shots.paths[mid] < h) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return 0;
}


/**
 * a picture in a clip's folder, or under a folder of small copies: not one
 * the user took
 **/
static int
shot_is_copy(const capture_t *c) {
  return strstr(c->path, "/thumb") ||
         ((c->kind == CAPTURE_JPEG || c->kind == CAPTURE_PNG || c->kind == CAPTURE_JXR) &&
          strstr(c->path, "/video/"));
}


/**
 * the extension of a path, letters and digits only, for the log
 **/
static void
shot_ext(const char *path, char *ext, size_t size) {
  const char *dot = strrchr(path, '.'), *slash = strrchr(path, '/');
  size_t n = 0;

  if(dot && (!slash || dot > slash)) {
    for(dot++; *dot && n + 1 < size; dot++) {
      if((*dot >= 'a' && *dot <= 'z') || (*dot >= 'A' && *dot <= 'Z') || (*dot >= '0' && *dot <= '9')) {
        ext[n++] = *dot;
      }
    }
  }
  ext[n] = 0;
  if(!n) {
    snprintf(ext, size, "none");
  }
}


static void
shot_one(void *ctx, const capture_t *c) {
  shot_look_t *l = ctx;
  uint64_t h = path_hash(c->path), *grown;

  if(c->kind >= 0) {
    l->kinds[c->kind]++;
  } else {
    l->others++;
  }
  if(l->count == l->room) {
    if(l->room >= SHOT_PATHS || !(grown=realloc(l->paths, (size_t)(l->room ? l->room * 2 : 256) * sizeof *grown))) {
      l->full = 1;
      return;
    }
    l->paths = grown;
    l->room = l->room ? l->room * 2 : 256;
  }
  l->paths[l->count++] = h;
  if(g_shots.wait.path[0] && !strcmp(c->path, g_shots.wait.path)) {
    l->wait_seen = 1;
    l->wait_now = *c;
  }
  if(!g_shots.known || path_known(h)) {
    return;
  }
  if(c->kind < 0 || shot_is_copy(c)) {
    char ext[8];
    l->other++;
    shot_ext(c->path, ext, sizeof ext);
    if(!strstr(l->other_ext, ext) && strlen(l->other_ext) + strlen(ext) + 3 < sizeof l->other_ext) {
      snprintf(l->other_ext + strlen(l->other_ext), sizeof l->other_ext - strlen(l->other_ext), "%s.%s",
               l->other_ext[0] ? " " : "", ext);
    }
    return;
  }
  // the newest; of files written together (a screenshot and what goes with
  // it), the biggest
  if(!l->fresh++ || c->time > l->newest.time + 2 ||
     (c->time >= l->newest.time - 2 && c->size > l->newest.size)) {
    l->newest = *c;
  }
}


/**
 * the title id in a capture's path (photo/NPXS40087/<title id>/...), else
 * the game that runs
 **/
static void
shot_game(const capture_t *c, char *id, size_t size) {
  const char *at = strstr(c->path, "/" CAPTURES_APP "/");
  size_t n = 0;

  if(at) {
    at += strlen(CAPTURES_APP) + 2;
    while(at[n] && at[n] != '/' && n + 1 < size) {
      n++;
    }
    memcpy(id, at, n);
    id[n] = 0;
    if(game_id_ok(id)) {
      return;
    }
  }
  snprintf(id, size, "%s", g_shots.game);
}


/**
 * a new capture for the title, and a toast about it, one a minute at most
 **/
static void
shot_offer(const capture_t *c, long long now) {
  char id[32], name[GAME_NAME];
  xnotify_toast_t t;
  int video = c->kind == CAPTURE_MP4 || c->kind == CAPTURE_WEBM;

  g_shots.pending = *c;
  g_shots.found = now;
  log_line("capture: a new %s, offered", video ? "clip" : c->kind == CAPTURE_JXR ? "HDR picture" : "picture");
  shot_game(c, id, sizeof id);
  session_shot(id, c);
  if(now - g_shots.toasted < SHOT_TOASTS || now - g_troph.toasted < SHOT_TOASTS) {
    return;
  }
  g_shots.toasted = now;
  name[0] = 0;
  if(id[0]) {
    game_name_of(id, name, sizeof name);
  }
  memset(&t, 0, sizeof t);
  snprintf(t.title, sizeof t.title, "%s", text(video ? T_SHOT_VIDEO : T_SHOT_PHOTO));
  snprintf(t.body, sizeof t.body, "%s", name[0] ? name : "XPS5");
  t.icon = "messages";
  toast(&t, 1);
  memset(&t, 0, sizeof t);
}


/**
 * a look at all the captures: a path not there before is new, and is
 * offered once it is written; returns the seconds to the next look
 **/
static int
watch_shots(void) {
  char id[32] = "";
  long long now = (long long)time(NULL);
  int app = sceSystemServiceGetAppIdOfRunningBigApp(), files;
  shot_look_t l;

  if(app > 0 && !sceSystemServiceGetAppTitleId(app, id) && (id[sizeof id - 1]=0, game_id_ok(id))) {
    snprintf(g_shots.game, sizeof g_shots.game, "%s", id);
  }
  if(g_shots.pending.path[0] && now - g_shots.found > SHOT_FRESH) {
    memset(&g_shots.pending, 0, sizeof g_shots.pending);
  }
  if(g_shots.known && (now - g_shots.looked > SHOT_GAP || now < g_shots.looked)) {
    // after a rest, or with the sharing turned off for a while: what came
    // meanwhile is not offered
    g_shots.known = 0;
    memset(&g_shots.wait, 0, sizeof g_shots.wait);
  }
  g_shots.looked = now;
  memset(&l, 0, sizeof l);
  if((files=captures_each(CAPTURES_ROOT, shot_one, &l)) < 0 || l.full) {
    if(captures_stats.error != g_shots.error || l.full != g_shots.full) {
      log_line("capture: the look ends on errno %d, %d files, %s", captures_stats.error, files,
               l.full ? "too many to tell a new one" : "none read");
      g_shots.error = captures_stats.error;
      g_shots.full = l.full;
    }
    free(l.paths);
    g_shots.known = 0; // the next look that reads them all only notes them
    return SHOT_EVERY;
  }
  g_shots.full = 0;
  qsort(l.paths, (size_t)l.count, sizeof *l.paths, hash_order);
  if(!g_shots.known || captures_stats.error != g_shots.error) {
    // counts and codes only
    log_line("capture: a look at all of them, %d files (jpg %d, png %d, jxr %d, mp4 %d, webm %d, other %d), "
             "%d folders, %d unreadable, errno %d", files, l.kinds[CAPTURE_JPEG], l.kinds[CAPTURE_PNG],
             l.kinds[CAPTURE_JXR], l.kinds[CAPTURE_MP4], l.kinds[CAPTURE_WEBM], l.others, captures_stats.folders,
             captures_stats.unreadable, captures_stats.error);
    g_shots.error = captures_stats.error;
  }
  free(g_shots.paths);
  g_shots.paths = l.paths;
  g_shots.count = l.count;
  if(!g_shots.known) {
    g_shots.known = 1;
    return SHOT_EVERY;
  }
  if(l.other) {
    log_line("capture: %d new file(s) left out (%s)", l.other, l.other_ext);
  }
  // the one still written: offered when it kept its size
  if(g_shots.wait.path[0]) {
    if(!l.wait_seen) {
      memset(&g_shots.wait, 0, sizeof g_shots.wait);
    } else if(l.wait_now.size == g_shots.wait.size && l.wait_now.time == g_shots.wait.time &&
              l.wait_now.size > 0) {
      int video = l.wait_now.kind == CAPTURE_MP4 || l.wait_now.kind == CAPTURE_WEBM;
      if(++g_shots.still >= (video ? 2 : 1)) {
        shot_offer(&l.wait_now, now);
        memset(&g_shots.wait, 0, sizeof g_shots.wait);
      }
    } else {
      g_shots.wait = l.wait_now;
      g_shots.still = 0;
    }
  }
  if(l.fresh) {
    log_line("capture: %d new, the newest a %s written %lld s before the look", l.fresh,
             l.newest.kind == CAPTURE_MP4 || l.newest.kind == CAPTURE_WEBM ? "clip" :
             l.newest.kind == CAPTURE_JXR ? "HDR picture" : "picture", now - l.newest.time);
    g_shots.wait = l.newest;
    g_shots.still = 0;
  }
  return g_shots.wait.path[0] ? SHOT_AGAIN : SHOT_EVERY;
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
 * "type <screen> <seq> <max> <what> <lang> <draft>": the state line of the
 * writing on the phone, and the text the phone wrote when it is new to the
 * title (src/phone.h)
 **/
static void
serve_type(int c, char *args) {
  static char draft[PHONE_TEXT_MAX + 1], text[PHONE_TEXT_MAX + 1];
  char what[8] = "", lang[4] = "", line[160], *hex;
  phone_state_t st;
  unsigned screen, seq;
  size_t len = 0;
  int max, n, at = 0;

  if(sscanf(args, "%u %u %d %7s %3s %n", &screen, &seq, &max, what, lang, &at) < 5 || !at) {
    (void)!write(c, "err 22\n", 7);
    return;
  }
  hex = args + at;
  hex[strcspn(hex, " \r\n")] = 0;
  if(phone_unhex(hex, draft, sizeof draft) < 0) {
    draft[0] = 0;
  }
  phoneserve_ask(screen, seq, max, what, lang, draft, &st, text, sizeof text, &len);
  if((n=phone_state_line(line, sizeof line, &st, len)) > 0) {
    write_all(c, line, (size_t)n);
    if(len) {
      write_all(c, text, len);
    }
  }
  memset(draft, 0, sizeof draft);
  memset(text, 0, sizeof text);
  memset(args, 0, strlen(args));
}


#define HUSH_FOR    4000 // ms a title's hush lasts unless it says it again (title/space.c)
#define RADIO_WAIT  50   // tenths of a second the radio gets to stop, at the end

// the Space the radio plays for the title (src/spacecmd.h)
static struct {
  unsigned n;                // the title's number for it, 0 for none
  int      live;
  char     id[SPACE_NAME];
  char     host[SPACE_NAME];
} g_space;


/**
 * "space play|status|hush|stop ...": a Space played here, where a game in
 * front does not suspend it, and how it stands. The playlist's address
 * goes to the radio only: never to the klog, nor back to the title
 **/
static void
serve_space(int c, char *args) {
  static char url[SPACE_URL];
  char line[SPACE_LINE], id[SPACE_NAME], host[SPACE_NAME];
  space_status_t s;
  unsigned n;
  int live, on, len;

  if(!strncmp(args, "play ", 5)) {
    if(!space_cmd_play_args(args + 5, &n, &live, id, host, url)) {
      radio_play(url, live); // one playing stops
      g_space.n = n;
      g_space.live = live;
      memcpy(g_space.id, id, sizeof id);
      memcpy(g_space.host, host, sizeof host);
      log_line("space: played for the title, %s", live ? "live" : "a recording");
    } else {
      log_line("space: a play not understood (%zu bytes)", strlen(args));
    }
    memset(url, 0, sizeof url);
  } else if(sscanf(args, "stop %u", &n) == 1) {
    if(!n || n == g_space.n) {
      radio_stop();
      memset(&g_space, 0, sizeof g_space);
      log_line("space: stopped by the title");
    }
  } else if(sscanf(args, "hush %u %d", &n, &on) == 2) {
    if(n && n == g_space.n) {
      if(on) {
        radio_hush_for(HUSH_FOR); // a title suspended mid-video never says it is over
      } else {
        radio_hush(0);
      }
    }
  }
  radio_status(&s);
  if((len=space_line_put(line, sizeof line, g_space.n, &s, g_space.live, g_space.id, g_space.host)) > 0) {
    write_all(c, line, (size_t)len);
  }
  memset(args, 0, strlen(args));
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
 * "badges <seen> <back> [<id> [<id>]]": the bar's counts, and what a
 * toast's Open is for (see the commands at the top)
 **/
static void
serve_badges(int c, const char *args) {
  const long long now = (long long)time(NULL);
  const char *open = "-";
  char line[80], *end, ids[2][X_ID_MAX] = {"", ""};
  long seen, back;
  int chat, notice, news;

  seen = strtol(args, &end, 10);
  back = strtol(end, &end, 10);
  // the conversation open in the title (its id, and its X Chat id): read
  if(sscanf(end, " %63s %63s", ids[0], ids[1]) < 1) {
    ids[0][0] = 0;
  }
  _Static_assert(X_ID_MAX == 64, "the widths above");
  news = counted_read(ids[0]);
  news |= counted_read(ids[1]);
  if(back) {
    chat = g_badges.chat_toast_at && now - g_badges.chat_toast_at <= OPEN_WITHIN;
    notice = g_badges.notice_toast_at && now - g_badges.notice_toast_at <= OPEN_WITHIN;
    if(chat && notice) {
      // both: the PS5 shows them in turn, and which one's Open it was is
      // not told. Together, the message's (what the user answers); apart,
      // the newer one's
      chat = g_badges.notice_toast_at - g_badges.chat_toast_at <= TOAST_BURST;
      notice = !chat;
    }
    open = chat ? "chat" : notice ? "notifications" : "-";
    // its Open, or the title opened some other way
    g_badges.chat_toast_at = g_badges.notice_toast_at = 0;
  }
  snprintf(line, sizeof line, "badges %d %d %s %d\n", g_badges.notices, counted_sum(), open, news);
  (void)!write(c, line, strlen(line));
  if(seen & 1) {
    g_badges.notices_seen = g_badges.newest_notice > now ? g_badges.newest_notice : now;
    g_badges.notices = 0;
  }
  if(seen & 2) {
    g_badges.chats_seen = g_badges.newest_chat > now ? g_badges.newest_chat : now;
    g_ncounted = 0;
  }
}


/**
 * wait until the time end, answering whoever asks; returns WAIT_TIME, or
 * WAIT_STOP when a new copy asked this one to stop, or WAIT_RELOAD when the
 * title changed the settings
 **/
static int
wait_until(int control, time_t end) {
  static char cmd[CAPTURE_PATH + 64 + 2 * PHONE_TEXT_MAX]; // a "type" carries its draft in hex
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
    } else if(!strncmp(cmd, "type ", 5)) {
      serve_type(c, cmd + 5);
    } else if(!strcmp(cmd, "untype")) {
      phoneserve_close();
      (void)!write(c, "ok\n", 3);
    } else if(!strncmp(cmd, "space ", 6)) {
      serve_space(c, cmd + 6);
    } else if(!strcmp(cmd, "shot")) {
      serve_shot(c);
    } else if(!strcmp(cmd, "trophy")) {
      serve_trophy(c);
    } else if(!strncmp(cmd, "launch ", 7)) {
      serve_launch(c, cmd + 7);
    } else if(!strcmp(cmd, "session")) {
      serve_session(c);
    } else if(!strcmp(cmd, "week")) {
      serve_week(c);
    } else if(!strncmp(cmd, "badges ", 7)) {
      serve_badges(c, cmd + 7);
    } else if(!strncmp(cmd, "trophies ", 9)) {
      serve_trophies(c, cmd + 9);
    } else {
      (void)!write(c, "on 27\n", 6);
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
  static friends_t friends;
  static xnotify_t dms, xchats;
  long long rising_at = 0, friends_at = 0, dm_at = 0, xchat_at = 0, notice_at = 0, shot_at = 0, play_at = 0, next;
  int shot_wait;
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
  xchat_open_install(); // X Chat's words open once the account's key is in hand (xchat_key)
  if((ret=https_init(X_USER_AGENT, log_text))) {
    printf("XPS5 notify: HTTPS does not start (0x%08x)\n", (unsigned)ret);
    fflush(stdout);
    _exit(EXIT_FAILURE);
  }
  radio_init(log_line); // a Space the title hands over, see serve_space
  if(g_settings.notify) {
    printf("XPS5 notify: on, a reading every %d s; send it again to turn it off\n",
           g_settings.every);
  } else {
    printf("XPS5 notify: the notifications are off in XPS5's Settings; "
           "it only reads the captures for the app\n");
  }
  log_line("on, notifications %d, playing %d, online %d", g_settings.notify, g_settings.playing, g_settings.online);
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
      // who the user follows, as seldom, for one who started a game
      if(g_settings.notify && g_settings.friends && wait != WAIT_LIMITED && (long long)time(NULL) >= friends_at) {
        friends_at = (long long)time(NULL) + (read_friends(&friends) == 429 ? WAIT_LIMITED : FRIENDS_EVERY);
      }
      notice_at = (long long)time(NULL) + wait;
    }
    // the messages, on a clock of their own, more often than the notices: the
    // inbox, and X Chat's, where the new conversations are (seldom while it
    // does not come)
    if(g_settings.notify && (long long)time(NULL) >= dm_at) {
      dm_at = (long long)time(NULL) + chat_wait(read_dms(&dms));
    }
    if(g_settings.notify && (long long)time(NULL) >= xchat_at) {
      ret = read_xchat(&xchats);
      xchat_at = (long long)time(NULL) + (ret == 429 || ret == 200 ? chat_wait(ret) : XCHAT_RETRY);
    }
    // the toasts kept back during a competitive game, in one once it closed
    quiet_over();
    // the game's trophies and captures, often (no network), for a new one to post
    if(g_settings.share && (long long)time(NULL) >= shot_at) {
      watch_session((long long)time(NULL)); // first: the trophies and captures count in it
      watch_trophies(); // its toast stands for the PS5's screenshot of it
      shot_wait = watch_shots();
      shot_at = (long long)time(NULL) + shot_wait;
    }
    // the game in the profile, while it is wanted or still shown
    if(play_wanted() && (long long)time(NULL) >= play_at) {
      play_at = (long long)time(NULL) + watch_playing();
    }
    next = g_settings.share && shot_at < notice_at ? shot_at : notice_at;
    if(play_wanted() && play_at < next) {
      next = play_at;
    }
    if(g_settings.notify) {
      next = dm_at < next ? dm_at : next;
      next = xchat_at < next ? xchat_at : next;
    }
    while((ret=wait_until(control, (time_t)next)) == WAIT_RELOAD) {
      was = g_settings.notify;
      account = g_settings.account;
      load_settings();
      if(g_settings.account != account) {
        // another account: its notices are new to it, so the first reading only notes them
        memset(&notices, 0, sizeof notices);
        memset(&rising, 0, sizeof rising);
        memset(&friends, 0, sizeof friends);
        memset(&dms, 0, sizeof dms);
        memset(&xchats, 0, sizeof xchats);
        memset(&g_badges, 0, sizeof g_badges);
        g_ncounted = 0;
        rising_at = friends_at = dm_at = xchat_at = 0;
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
      if(g_settings.notify) {
        next = dm_at < next ? dm_at : next;
        next = xchat_at < next ? xchat_at : next;
      }
    }
    if(ret == WAIT_STOP) {
      break;
    }
  }
  // the copy that knocked says it
  log_line("stopped by a new copy");
  // a Space playing stops first: its fetcher may be in a request
  radio_stop();
  for(int i=0; i < RADIO_WAIT && radio_alive(); i++) {
    usleep(100 * 1000);
  }
  https_fini();
  close(control);
  _exit(EXIT_SUCCESS);
}
