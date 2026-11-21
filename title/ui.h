/* The title's screen, apart from the PS5: the lists of posts (the two
timelines, a search, notifications, bookmarks, profiles and a post with
its replies)
laid out the way x.com shows them, the selection, the scrolling and the
pointer the controller moves, and each frame drawn into a UI_WIDTH x
UI_HEIGHT buffer. The title's main.c gives it posts, buttons and sticks;
tests/preview.c draws it on a computer. */

#pragma once

#include "captures.h"
#include "settings.h"
#include "text.h"
#include "xapi.h"

#define UI_WIDTH  1920
#define UI_HEIGHT 1080

// the lists: the sections of the bar, then the ones opened from a post.
// Explore (UI_SEARCH) shows X's trends and news until something is searched
// for; Chat lists the user's conversations (direct messages); Calls, Grok and
// Subscription are pages that say where X has them; History is kept by the
// screen; Articles are the user's own (UserArticlesTweets); Lists, from More,
// the lists the user made or follows; Game, Home's third tab while a game
// runs, the latest posts about it (X's search, game_query); Friends, Chat's
// second tab, the people the user follows as a PS5's friends are listed:
// who plays and who is on XPS5 first, then the ones seen last (Following)
enum {
  UI_FOR_YOU, UI_FOLLOWING, UI_SEARCH, UI_NOTIFICATIONS, UI_BOOKMARKS, UI_ME, // sections
  UI_CHAT, UI_CALLS, UI_GROK, UI_PREMIUM, UI_HISTORY, UI_ARTICLES, UI_LISTS, UI_GAME, UI_FRIENDS,
  UI_USER,  // a profile
  UI_POST,  // a post and its replies
  UI_DM,    // a conversation of Chat
  UI_LIST,  // the posts of a list
  UI_FEEDS
};

// the posts History keeps, newest first, and Read later
#define UI_HISTORY_MAX 40
#define UI_LATER_MAX   60

// the controller's buttons, as the pad reports them
#define UI_R3       0x0004 // the right stick pressed: the small video closes
#define UI_OPTIONS  0x0008
#define UI_UP       0x0010
#define UI_RIGHT    0x0020
#define UI_DOWN     0x0040
#define UI_LEFT     0x0080
#define UI_L2       0x0100
#define UI_R2       0x0200
#define UI_L1       0x0400
#define UI_R1       0x0800
#define UI_TRIANGLE 0x1000
#define UI_CIRCLE   0x2000
#define UI_CROSS    0x4000
#define UI_SQUARE   0x8000
#define UI_TOUCHPAD 0x100000 // the press of the touch pad: the bar's menu

// why a timeline could not be read
enum {
  ERR_NONE,
  ERR_NO_SESSION, // no session file
  ERR_NO_COOKIE,  // code -1: no auth_token, -2: no ct0
  ERR_HTTPS,      // code: SCE error of https_init
  ERR_NETWORK,    // code: SCE error of https_get
  ERR_HTTP,       // code: HTTP status, x_code: X's error or -1
  ERR_ANSWER,     // HTTP 200 that is not JSON
  ERR_EMPTY,      // a timeline without posts
  ERR_MEMORY,
  ERR_CANCELLED,  // the keyboard was closed without a reply
  ERR_KEYBOARD,   // code: why the keyboard did not open
  ERR_CAPTURE,    // a capture could not be read; code: errno or the helper's
  ERR_UPLOAD,     // upload.x.com did not take a picture or video; code: HTTP status, x_code
  ERR_PROCESSING, // X could not process the video (its format, its length)
  ERR_TOO_BIG,    // a capture over what X takes
  ERR_NO_GAME,    // the game of an invite is not installed
  ERR_GAME_OPEN,  // another game is open, which the invite's waits for
  ERR_LAUNCH,     // code: why the PS5 did not open the game
  ERR_XCHAT_PIN,      // the X Chat PIN was wrong; x_code: the guesses left
  ERR_XCHAT_LOCKED,   // no guesses left: only a reset in X's own apps opens it
  ERR_XCHAT_NONE,     // this account has no X Chat keys to recover
  ERR_XCHAT_IDENTITY, // a key came back, but not this account's
  ERR_XCHAT_NET,      // a realm did not answer or refused; code: a JB_* code
  ERR_CLIP,           // the part of a video asked for could not be cut out of it; code: a CLIP_* or MP4_* code
  ERR_SPACE_OFF,      // the Space is not on the air and has no recording; code: X_SPACE_* of xapi.h
};

// what the buttons ask main.c to do: to a post, a post of the user's own
// or a reply (both from the composer, with its text and captures), the
// keyboard for a search or for the composer's text, or the PS5's captures
// for the composer; the keyboard for a message, and sending it
enum {
  UI_ACT_NONE, UI_ACT_LIKE, UI_ACT_UNLIKE, UI_ACT_REPLY, UI_ACT_REPOST, UI_ACT_UNREPOST, UI_ACT_POST,
  UI_ACT_BOOKMARK, UI_ACT_UNBOOKMARK, UI_ACT_SEARCH, UI_ACT_COMPOSE_TEXT, UI_ACT_CAPTURES,
  UI_ACT_DELETE, // the user's own post, after the menu and its question
  UI_ACT_DM_TEXT, // the keyboard for a message to the conversation in id, to user
  UI_ACT_DM,      // the message, to the conversation in id
  UI_ACT_QUOTE,   // the composer's post, quoting the post id of user
  UI_ACT_EDIT,    // the composer's text, as the new one of the user's post id
  UI_ACT_PIN, UI_ACT_UNPIN, // the user's post id on top of their profile, or not
  // to the user with the number id (screen name user)
  UI_ACT_FOLLOW, UI_ACT_UNFOLLOW, UI_ACT_MUTE, UI_ACT_UNMUTE, UI_ACT_BLOCK, UI_ACT_UNBLOCK,
  UI_ACT_SCHEDULE, // the composer's post, to go out ui_schedule_delay seconds from now
  UI_ACT_UNSCHEDULE, // the scheduled post id taken back
  UI_ACT_VOTE,      // a choice in the poll of the post id (ui_vote_get)
  UI_ACT_POLL_TEXT, // the keyboard for a choice of the composer's poll (ui_poll_choice)
  UI_ACT_GROK_TEXT, // the keyboard for a question to Grok (ui_grok_text)
  UI_ACT_GROK,      // the question to Grok (ui_grok_get); its answer comes with ui_grok_answer
  UI_ACT_INVITE,    // an invite to play (ui_invite_get) to the conversation id, with user
  UI_ACT_LAUNCH,    // the game of an invite opened (ui_launch_get)
  UI_ACT_SHARE_DM,  // the composer's text and first capture, a message to the conversation id
  UI_ACT_VOICE_DM,  // a voice note (ui_voice_dm_get), a message to the conversation id, with user
  UI_ACT_XCHAT_PIN, // the keyboard for the X Chat PIN (masked), to unlock reading
  UI_ACT_XCHAT_UNLOCK, // the PIN (ui_xchat_pin_get) recovers the X Chat keys and keeps them
  UI_ACT_SPACE      // the Space id (of user's post) asked about; its sound plays (ui_space) when it can
};

// the people of a conversation ui_party hands over, at most (the user among them)
#define UI_PARTY_MAX 9

// the media of a post: up to four pictures, or one video
#define UI_MEDIA_MAX 4

// a list to read from X
typedef struct ui_request {
  unsigned view;                 // which list asked, for ui_result
  int      feed;                 // UI_*
  char     id[X_ID_MAX];         // the user of UI_ME, UI_USER and UI_ARTICLES, the post of
                                 // UI_POST, or empty: UI_ME (and UI_ARTICLES) before the
                                 // signed-in user's profile came; the signed-in user of
                                 // UI_CHAT; the conversation of UI_DM; the list of UI_LIST
  int      profile;              // read the profile first (UI_ME, UI_USER; UI_ARTICLES
                                 // without id, for its number only)
  char     cursor[X_CURSOR_MAX]; // of the posts after the ones shown, or empty for the top
  char     query[X_QUERY_MAX + 1]; // what UI_SEARCH looks for (UTF-8), or empty for
                                   // Explore's trends; UI_GAME's search
  int      latest;               // its latest posts, not the top ones; Explore's news,
                                 // not its trends
  int      side;                 // UI_SIDE_*: for the side column, not a list
  int      xchat;                // UI_DM of a conversation on X Chat: its page is read there,
                                 // encrypted, and opened when the account is unlocked
  char     xchat_id[X_ID_MAX];   // UI_DM of the inbox's that goes on on X Chat too: its
                                 // number there, whose messages join the inbox's
} ui_request_t;

// what the side column reads: x.com's side column of trends (ExploreSidebar),
// Explore's trends when that did not come, and who to follow (ConnectTabTimeline)
// and the user's scheduled posts, for their sheet (FetchScheduledTweets)
// and the people the user (id) follows, for who of them plays now (Following)
enum { UI_SIDE_NONE, UI_SIDE_TRENDS, UI_SIDE_EXPLORE, UI_SIDE_WHO, UI_SIDE_SCHEDULED, UI_SIDE_PLAYING };


/**
 * the screen's pixels a point, before ui_init: 1 (1080p), 4 / 3 (1440p) or
 * 2 (4K). The layout stays 1920 x 1080 points; ui_draw's surface has this
 * scale
 **/
void ui_scale(float scale);

/**
 * open the fonts, with the settings the app keeps and the PS5's language
 * (LANG_* of settings.h) for LANG_SYSTEM; returns 0 or -1
 **/
int ui_init(const settings_t *s, int system_lang);

/**
 * the language the screen shows, LANG_* (never LANG_SYSTEM)
 **/
int ui_lang(void);

/**
 * History's posts, newest first, to be kept between runs: their count,
 * and in *changed whether they changed since the last call
 **/
int ui_history_get(const x_post_t **posts, int *changed);

/**
 * History as it was kept: count posts (X_KIND_POST), newest first
 **/
void ui_history_set(const x_post_t *posts, int count);

/**
 * Read later (History's second tab), like ui_history_get and _set: the
 * posts kept to read after playing, newest first
 **/
int ui_later_get(const x_post_t **posts, int *changed);
void ui_later_set(const x_post_t *posts, int count);

/**
 * a text of the screen in its language, from its Portuguese and English
 **/
const char *ui_text(const char *pt, const char *en);

/**
 * the settings, once the Settings screen changed them; returns whether
 * they changed since the last call
 **/
int ui_settings_changed(settings_t *s);

/**
 * where the session file is and who it signs in, for the Settings screen
 **/
void ui_session_info(int found, const char *path);

/**
 * which accounts have a session file (bit 0 the first), for the Settings
 **/
void ui_accounts(unsigned present);

/**
 * the account whose session the lists are read with (1 to SETTINGS_ACCOUNTS)
 **/
int ui_account(void);

/**
 * the account the bar's account menu logged out of, once (its session file
 * is to be left empty), or 0
 **/
int ui_logout_take(void);

/*
 * Linking an account from a PC (src/link.h): the screen for it, opened from
 * the Settings' X account or from a missing session, and main.c's thread,
 * which listens while it is open.
 */
enum { UI_LINK_STARTING, UI_LINK_WAITING, UI_LINK_CHECKING, UI_LINK_DONE, UI_LINK_NO_NETWORK,
       UI_LINK_NO_HELPER }; // the payload, which serves it, does not run

// what went wrong with the last session the PC sent
enum { UI_LINK_FINE, UI_LINK_WRONG_CODE, UI_LINK_REFUSED, UI_LINK_UNWRITTEN };

typedef struct ui_link {
  int  state;    // UI_LINK_*
  char ip[16];   // the console's address in the home network, once known
  char code[8];  // the digits the extension must send
  int  last;     // UI_LINK_FINE or what went wrong last
  int  wrong;    // codes sent wrong since this one came
  int  renewed;  // the code is a new one, after too many wrong
  int  refused;  // what X answered the session it refused (HTTP status), -1 none
  int  error;    // why UI_LINK_NO_NETWORK
  int  account;  // where the session went, once done
  char user[64]; // and whose it is
  int  seen;     // the QR's page opened, on a phone or a PC
} ui_link_t;

/**
 * the account the screen links (1 to SETTINGS_ACCOUNTS) while it is open,
 * else 0
 **/
int ui_link_wanted(void);

/**
 * how the linking goes; once it is done, the lists of the account it went
 * to are read again with it
 **/
void ui_link_status(const ui_link_t *st);

/*
 * Logging in on the console (src/loginproto.h): X's own login page in a
 * window of XPS5's, opened where the linking was while the login engine is
 * on the console (ui_login_ready). main.c starts the engine while the window
 * is open, hands its page over as pixels and passes on what the controller
 * does to it.
 */
enum { UI_LOGIN_STARTING, UI_LOGIN_SHOWN, UI_LOGIN_FAILED };

// why it failed, or (UI_LOGIN_PAGE) why the page shown did not load
enum { UI_LOGIN_FINE, UI_LOGIN_NO_ENGINE, UI_LOGIN_NO_LOADER, UI_LOGIN_NO_START, UI_LOGIN_LOST,
       UI_LOGIN_PAGE, UI_LOGIN_CRASHED, UI_LOGIN_NO_READ };

typedef struct ui_login {
  int state;    // UI_LOGIN_*: STARTING until the page's first pixels
  int why;      // UI_LOGIN_FINE or what went wrong
  int code;     // with it: the system's error, the engine's LOGIN_END_* or WebKit's
  int loading;  // the page loads, progress of 100 so far
  int progress;
  int on_x;     // the page is X's, over HTTPS
  int blocked;  // links out of X the window did not follow, so far
  int no_field; // △ found no field to type in, so far
  int keyboard; // the PS5's keyboard is up for a field of the page
} ui_login_t;

// what the controller did to the page, at x, y of its pixels
enum { UI_LOGIN_MOVE = 1, UI_LOGIN_CLICK, UI_LOGIN_WHEEL, UI_LOGIN_KEY, UI_LOGIN_FIELD, UI_LOGIN_RETRY };

typedef struct ui_login_input {
  int kind; // UI_LOGIN_MOVE ...
  int x, y;
  int d;    // UI_LOGIN_WHEEL: pixels down the page (up below 0); UI_LOGIN_KEY: a LOGIN_KEY_*
} ui_login_input_t;

/**
 * whether the login engine is on the console: then the Settings' X account
 * and a missing session open the login window, else the linking
 **/
void ui_login_ready(int ready);

/**
 * while the login window is open, the account it signs in (1 to
 * SETTINGS_ACCOUNTS), its page's size in the screen's pixels and its zoom
 * in percent; else 0
 **/
int ui_login_wanted(int *width, int *height, int *zoom);

/**
 * how the window goes
 **/
void ui_login_status(const ui_login_t *st);

/**
 * the page's pixels, width x height as ui_login_wanted gave, or NULL; drawn
 * until the next call
 **/
void ui_login_pixels(const uint32_t *pixels, int width, int height);

/**
 * the next thing the controller did to the page, in the order done;
 * 0 when there is none
 **/
int ui_login_input(ui_login_input_t *in);

// X's session from the window, as main.c tries it: UI_SESSION_CHECKING while
// X is asked, then 0 (kept), X's HTTP status, or one of these
enum { UI_SESSION_CHECKING = -100, UI_SESSION_NO_NETWORK = -1, UI_SESSION_UNWRITTEN = -2 };

/**
 * how X's session from the login window went for account (1 to
 * SETTINGS_ACCOUNTS): with status 0 it is kept, user's (a screen name), and
 * the screen goes to that account and closes the window
 **/
void ui_login_session(int account, int status, const char *user);

/**
 * the list to read now, which is then shown as loading: the one shown,
 * when it has no posts yet or □ asked for it, from the top; or, near the
 * end of its posts, the ones after them; returns whether there is one
 **/
int ui_wanted(ui_request_t *req);

/**
 * what reading req's list gave: posts (malloc'd, the screen keeps and frees
 * them), their count and the cursor of the ones after them (or empty), and
 * the profile when req asked for it, or NULL; or no posts and why, which
 * keeps the posts shown
 **/
void ui_result(const ui_request_t *req, x_post_t *posts, int count, const char *cursor,
               const x_user_t *user, int error, int code, int x_code, long long when);

/**
 * what R2 (like, or take it back), L2 (reply), the pointer (repost,
 * bookmark) or the bar (a post, a search) asked to do: UI_ACT_*, with the
 * post's number in id and its author's screen name in user (both empty for
 * UI_ACT_POST and UI_ACT_SEARCH); UI_ACT_NONE when nothing is asked or the
 * last is still going
 **/
int ui_action(char *id, size_t isize, char *user, size_t usize);

/**
 * what the composer holds, for UI_ACT_POST and UI_ACT_REPLY and as the
 * keyboard's start for UI_ACT_COMPOSE_TEXT: its text, and the captures it
 * carries (at most max); returns how many
 **/
int ui_compose_get(char *text, size_t size, capture_t *media, int max);

/**
 * the most characters the composer's keyboard takes: more for X Premium,
 * whose posts can be long
 **/
int ui_compose_max(void);

/**
 * the poll of the composer's post (UI_ACT_POST, UI_ACT_REPLY): its choices
 * that have words, and how many minutes it stays open; returns how many,
 * or 0 for none
 **/
int ui_poll_get(char labels[][X_POLL_LABEL], int *minutes);

/**
 * which choice of the composer's poll the keyboard of UI_ACT_POLL_TEXT
 * writes (1 to 4), and its words now, to start with
 **/
int ui_poll_choice(char *text, size_t size);

/**
 * what that keyboard gave (UTF-8): the choice's words; ends the action
 **/
void ui_poll_text(const char *text);

/**
 * the vote UI_ACT_VOTE sends: the poll's card and its name, and the
 * choice, 1 to 4, which it returns
 **/
int ui_vote_get(char *card, size_t csize, char *name, size_t nsize);

/**
 * how many seconds from now the composer's post is scheduled for (X
 * Premium), or 0 for now
 **/
int ui_schedule_delay(void);

/**
 * what the keyboard of UI_ACT_COMPOSE_TEXT gave (UTF-8), which becomes the
 * composer's text; ends the action like ui_action_done
 **/
void ui_compose_text(const char *text);

/**
 * what the keyboard of UI_ACT_DM_TEXT gave (UTF-8), which is then sent to
 * the conversation (UI_ACT_DM); empty, nothing is
 **/
void ui_dm_text(const char *text);

/**
 * the message UI_ACT_DM sends; returns its length
 **/
int ui_dm_get(char *text, size_t size);

/**
 * what the keyboard of UI_ACT_GROK_TEXT gave (UTF-8): the question that
 * UI_ACT_GROK then sends; empty, nothing is
 **/
void ui_grok_text(const char *text);

// what UI_ACT_GROK asks of Grok
enum {
  UI_GROK_HINT,      // the question on Grok's page, about the game
  UI_GROK_TRANSLATE, // a post into the app's language: question its words, author who wrote it
  UI_GROK_CATCHUP,   // what the user missed: question the posts' lines ("@author: words" each)
};

/**
 * the question UI_ACT_GROK sends, about the game named in game (or empty),
 * *fresh 1 when it starts a new conversation, *what UI_GROK_* and the
 * screen name of a post to translate in author; returns its length
 **/
int ui_grok_get(char *question, size_t qsize, char *game, size_t gsize, int *fresh, int *what,
                char *author, size_t asize);

/**
 * the words of the invite UI_ACT_INVITE sends; returns their length
 **/
int ui_invite_get(char *text, size_t size);

/**
 * the voice note UI_ACT_VOICE_DM sends into c; 1, or 0 when there is none
 **/
int ui_voice_dm_get(capture_t *c);

/**
 * the people of the conversation (its number) just read, as its answer
 * lists them, the user possibly among them (count up to UI_PARTY_MAX):
 * a group's, for its side card; before ui_result hands its messages over
 **/
void ui_party(const char *conversation, const x_post_t *people, int count);

/**
 * the game UI_ACT_LAUNCH opens: its title id (empty when the invite had
 * none) and its name; returns 0, or -1 when there is none
 **/
int ui_launch_get(char *id, size_t isize, char *name, size_t nsize);

/**
 * Grok's answer to it, before ui_action_done says how it went
 **/
void ui_grok_answer(const char *answer);

/**
 * the Space UI_ACT_SPACE asked about, when its sound starts playing
 * (space.h), before ui_action_done says how it went
 **/
void ui_space(const x_space_t *space);

// the controller's microphone, as the Calls page tests it (mic.h)
enum { UI_MIC_OFF, UI_MIC_OPENING, UI_MIC_ON, UI_MIC_FAILED };
#define UI_MIC_NO_DEVICE    0x01 // the flags of silent
#define UI_MIC_PRIORITY_LOW 0x02

/**
 * whether the Calls page tests the microphone, shown and asked with ✕;
 * *retry is 1 once when ✕ asks again after it failed
 **/
int ui_mic_wanted(int *retry);

/**
 * how the microphone is: a UI_MIC_* state, the SCE error when it failed,
 * the silent flags, and the meter's level and peak, 0..1
 **/
void ui_mic(int state, int code, int silent, float level, float peak);

// XPS5's updates, as Settings' row shows them (src/update.h)
enum {
  UI_UPDATE_OFF,        // this build does not look for them
  UI_UPDATE_NONE,       // not looked for yet
  UI_UPDATE_CHECKING,
  UI_UPDATE_LATEST,     // this is the newest
  UI_UPDATE_FOUND,      // a newer one, version
  UI_UPDATE_GETTING,    // fetching it, percent
  UI_UPDATE_INSTALLING, // the installer runs: XPS5 closes
  UI_UPDATE_FAILED,     // error (ERR_*), code; version when one was found
};

/**
 * how the updates go: UI_UPDATE_*, the newer version (or empty), how much
 * of it came, and why it failed
 **/
void ui_update(int state, const char *version, int percent, int error, int code);

/**
 * what Settings asked of the updates since the last call: 1 look for one,
 * 2 install the one found, or 0
 **/
int ui_update_wanted(void);

/**
 * the PS5's captures UI_ACT_CAPTURES asked for, newest first, or count -1
 * and why (ERR_*, code); ends the action
 **/
void ui_captures(const capture_t *list, int count, int error, int code);

/**
 * the game that runs beside the app: its title id ("PPSA01325") and name,
 * or both empty; the composer offers its hashtag and the captures of it
 * come first
 **/
void ui_game(const char *id, const char *name);

/**
 * the counts of the bar: notices and messages that came since the user
 * last looked at them, as the payload says; while the list of one shows,
 * its new ones are read again, and so is the conversation open when news
 * (it had new messages)
 **/
void ui_badges(int notices, int chats, int news);

/**
 * the lists of the counts the user looks at now: 1 the notifications, 2
 * Chat's list of conversations (not a conversation: see ui_badges_open)
 **/
int ui_badges_seen(void);

/**
 * the conversation open, which counts as read: its id and its X Chat id
 * into a and b (size each), "" when none
 **/
void ui_badges_open(char *a, char *b, size_t size);

/**
 * a toast's Open brought the app to the front: what it told of (1 the
 * notifications, 2 Chat) is shown, unless a sheet is open
 **/
void ui_open_told(int what);

/**
 * a new capture of the game that runs, which the payload offered in a
 * toast: the composer of a post of the user's own carries it, with the
 * game's hashtag, and opens unless another screen is up
 **/
void ui_shot(const capture_t *c);

/**
 * for the pictures' loader (any thread): the .jxr capture at path turned
 * out HDR when its picture was made, so the screen labels it so (it goes to
 * X as Ultra HDR)
 **/
void ui_capture_hdr(const char *path);

/**
 * a trophy of the game the user just earned, which the payload offered in
 * a toast (grade 0 bronze to 3 platinum, and its name): the composer of a
 * post of the user's own says it, with the game's hashtag, and opens
 * unless another screen is up; the PS5's screenshot of it comes as a
 * ui_shot
 **/
void ui_trophy(int grade, const char *name);

/*
 * The game's own corner (Home's tab of the game): its trophies, a card of a
 * session of play or of the week (title/cards.h) for a post, and posts
 * looking for a group.
 */

#define UI_TROPHY_NAME   128
#define UI_TROPHY_DETAIL 256
#define UI_TROPHIES_MAX  512

// a trophy of the game, as the payload lists them ("trophies")
typedef struct ui_trophy_row {
  int  id, grade, hidden, earned; // grade 0 bronze to 3 platinum
  char name[UI_TROPHY_NAME];      // empty for a hidden one not earned
  char detail[UI_TROPHY_DETAIL];
} ui_trophy_row_t;

/**
 * the trophies of the game with the title id, count -1 when it has none
 * the PS5 knows of
 **/
void ui_trophies(const char *id, const ui_trophy_row_t *rows, int count);

/**
 * whether the trophies of the game are wanted again (their sheet opened),
 * once
 **/
int ui_trophies_wanted(void);

/**
 * a session of play that just ended (the payload's "session"), with its
 * best trophy and screenshots: its card is made for the composer
 **/
struct session;
void ui_session(const struct session *s);

/**
 * whether the week's card was asked for (once), and then the week's
 * sessions (NULL when the payload could not say)
 **/
struct week;
int  ui_week_wanted(void);
void ui_week(const struct week *w);

/**
 * whether the week's sessions are wanted for the card on the user's own
 * profile (once), and then the week (NULL when the payload could not say);
 * it comes this way too whenever it was asked for the week's card
 **/
int  ui_week_info_wanted(void);
void ui_week_info(const struct week *w);

/*
 * The people the user follows as XPS5 saw them around (src/seen.h), for
 * Friends, their activity and the player cards: main.c reads the account's
 * file, and keeps what the screen notes into it from its looks.
 */
struct seen;

/**
 * the account's file as main.c read it, or NULL for none
 **/
void ui_seen_set(const struct seen *s);

/**
 * 1 once after the screen noted a look into it (*s is then to be kept), or 0
 **/
int ui_seen_get(const struct seen **s);

/**
 * a card drawn and waiting to be a JPEG: its pixels (mem_alloc'd, now the
 * caller's) and size; returns 0 when there is none, UI_CARD_PICTURE, or
 * UI_CARD_VOICE for a voice note's picture, which becomes its MP4
 **/
enum { UI_CARD_NONE, UI_CARD_PICTURE, UI_CARD_VOICE };
int ui_card_take(uint32_t **pixels, int *w, int *h);

/**
 * the card as a JPEG the composer can carry (the path main.c reads it by),
 * or the voice note's MP4; c NULL when it could not be made
 **/
void ui_card_done(const capture_t *c);

/*
 * A voice note for a post (src/voice.h): the composer's sheet tapes the
 * controller's microphone, at VOICE_RATE, until ✕ stops it.
 */

/**
 * whether the voice note's sheet tapes the microphone; *retry is 1 once
 * when ✕ asks for it again after it failed
 **/
int ui_voice_wanted(int *retry);

/**
 * how the microphone is while it tapes: a UI_MIC_* state, the SCE error
 * when it failed (0 for no memory), the silent flags, the meter's level,
 * 0..1, and the samples taped
 **/
void ui_voice_heard(int state, int code, int silent, float level, size_t samples);

/**
 * 1 once the note was stopped to be made: the tape ends, and its loudness
 * goes to ui_voice_card
 **/
int ui_voice_take(void);

/**
 * the note's card, from count bars of its loudness (0..1) over seconds of
 * sound: its picture comes by ui_card_take as UI_CARD_VOICE. Returns 0 when
 * it is not made (let go of, or bars NULL when the tape could not be read)
 **/
int ui_voice_card(const float *bars, int count, int seconds);

/*
 * Dictation: the same sheet, opened by the composer's dictation button,
 * tapes at most DICT_MAX_S seconds (src/dictproto.h); once ui_voice_take,
 * main.c has the words made on the console and hands them to ui_dictated.
 */

/**
 * whether the sheet tapes (or was stopped) for a dictation, not a note
 **/
int ui_voice_dictating(void);

/**
 * the dictation's words (a DICT_* status, and the text when DICT_OK) added
 * to the end of the post's text; the sheet goes back to the composer. Let
 * go of when the sheet was cancelled meanwhile
 **/
void ui_dictated(int status, const char *text);

/**
 * how far a post's upload got, in bytes of all its media, while it goes
 **/
void ui_progress(long long done, long long total);

/**
 * what the keyboard of UI_ACT_SEARCH gave (UTF-8, at most X_QUERY_MAX
 * bytes): the search shows its posts; ends the action like ui_action_done
 **/
void ui_search(const char *text);

/**
 * how the action from ui_action went: ERR_NONE, or why not, with code and
 * x_code as for ui_result
 **/
void ui_action_done(int error, int code, int x_code);

/**
 * the buttons pressed this frame; returns whether the screen changed
 **/
int ui_keys(unsigned keys);

/**
 * the sticks, -128 to 127 from the middle, every frame: the left one moves
 * the pointer, the right one scrolls; returns whether the screen changed
 **/
int ui_sticks(int lx, int ly, int rx, int ry);

/**
 * where the pointer is, or -1, -1 while it is hidden
 **/
void ui_pointer(int *x, int *y);

/**
 * the colour of the controller's light bar (rgb): the banner's, as the
 * backdrop has it, as bright as the bar goes; 1, or 0 when Settings leaves
 * the bar to the PS5
 **/
int ui_light(unsigned char rgb[3]);

/**
 * what the controller should feel now, once: a tick for a like or a repost,
 * two knocks for new notifications or messages, a rising run for a post
 * that went out, a bump with the triggers at a list's end, a buzz on an
 * error; UI_RUMBLE_NONE when nothing, or when Settings turned it off
 **/
enum { UI_RUMBLE_NONE, UI_RUMBLE_TICK, UI_RUMBLE_NEWS, UI_RUMBLE_SENT, UI_RUMBLE_EDGE, UI_RUMBLE_ERROR };
int ui_rumble(void);

/**
 * 1 when voice notes play on the controller's speaker (Settings)
 **/
int ui_pad_speaker(void);

/*
 * Writing on the phone (src/phone.h): the window with its QR, which main.c
 * serves through the payload.
 */
enum { UI_PHONE_STARTING, UI_PHONE_WAITING, UI_PHONE_NO_NETWORK, UI_PHONE_NO_HELPER };

typedef struct ui_phone {
  int  state;     // UI_PHONE_*
  int  seen;      // the page was opened
  char ip[16];    // the console's address, once waiting
  char token[17]; // what the QR carries after '#', once waiting
} ui_phone_t;

/**
 * while the window is open: its number (one more for each window), and the
 * draft it opened with, how many characters the text takes and what it is
 * for ("post", "reply" or "dm"); 0 while it is closed
 **/
unsigned ui_phone_wanted(char *draft, size_t size, int *max, char *what, size_t wsize);

/**
 * where the payload's server is, for the window
 **/
void ui_phone_status(const ui_phone_t *st);

/**
 * all of what the phone wrote, each time it changed; done when its Send was
 * pressed, which uses it at once
 **/
void ui_phone_text(const char *text, int done);

/**
 * how long the last frame took, in microseconds: the pointer and the right
 * stick go as far in a long frame as in the frames it took the time of
 **/
void ui_elapsed(unsigned us);

/**
 * pictures came for what was drawn; what is under a sheet is drawn again
 * with them
 **/
void ui_pictures_arrived(void);

/**
 * a step of the scrolling and the spinners; returns whether the next frame
 * looks different
 **/
int ui_animate(void);

/**
 * the way in (XPS5's letters, growing until they go through the screen)
 * and the pages' transitions, which the title turns on; the tests draw
 * without them
 **/
void ui_motion(void);

/**
 * draw a frame; now is the Unix time, frame counts up once a frame
 **/
void ui_draw(const text_surface_t *dst, long long now, unsigned frame);

/**
 * after ui_draw: whether the pointer moved onto something that lit up and
 * the pointer sound is on (once), with pan from -0.35 (left) to 0.35
 **/
int ui_hover_take(float *pan);
