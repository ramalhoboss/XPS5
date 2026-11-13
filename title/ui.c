/* The title's screen, see ui.h. A panel of frosted glass floats over a warm,
softly lit backdrop, with a bar of round buttons beside it, one for each
section: the list on the left of the panel, the controls and the next post on
its right. The posts keep x.com's parts (names, badges, pictures, quotes,
counts), sized for a television across the room. A pointer the left stick
moves presses what lies under it: each frame notes where its buttons, posts
and pictures are. The backdrop, the glass and their shadows are painted
once; a frame copies them and draws the rest on top. */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "draw.h"
#include "fonts.h"
#include "game.h"
#include "play.h"
#include "images.h"
#include "lang.h"
#include "link.h"
#include "mem.h"
#include "player.h"
#include "qr.h"
#include "loginproto.h"
#include "ui.h"
#include "update.h"
#include "workers.h"

#define MAX_LINES   60 // of a post's text, when ✕ opens it
#define SHORT_LINES 10 // of a long post until then
#define QUOTE_LINES 5

// the bar of X's sections down the left, as x.com has it (the app's logo on
// top, an icon and the name of each, the Post button under them, the
// account at the bottom), and the glass panel right of it
#define NAV_X      30
#define NAV_W      316
#define NAV_ITEM   64
#define NAV_ITEMS  11
#define NAV_LOGO_Y 43
#define NAV_LOGO_W 247 // title/art/logo.png, drawn as it is
#define NAV_LOGO_H 50
#define NAV_Y      (NAV_LOGO_Y + NAV_LOGO_H + 23)
#define NAV_ICON   36
#define NAV_POST_Y (NAV_Y + NAV_ITEMS * NAV_ITEM + 18)
#define NAV_POST_H 72
#define NAV_ME_H   84
#define NAV_ME_Y   (UI_HEIGHT - 30 - NAV_ME_H)
#define PANEL_X   372
#define PANEL_Y   30
#define PANEL_W   1518
#define PANEL_H   990
#define PANEL_R   48

// in the panel: the timeline, a line, and the side column
#define DIV_X     (PANEL_X + 1062)
#define MAIN_X    (PANEL_X + 44)
#define MAIN_W    (DIV_X - 44 - MAIN_X)
#define SIDE_X    (DIV_X + 40)
#define SIDE_W    (PANEL_X + PANEL_W - 40 - SIDE_X)
#define SIDE_Y    (PANEL_Y + 34)
#define SIDE_ROWS 3               // of the buttons, two to a row, at the side column's bottom
#define SIDE_ROW  44
#define SIDE_KEYS_Y (PANEL_Y + PANEL_H - 34 - SIDE_ROWS * SIDE_ROW)

// over the timeline: the app and the tabs, then where a reply is written
#define TOP_Y     (PANEL_Y + 26)
#define TOP_H     68
#define INPUT_Y   (TOP_Y + TOP_H + 16)
#define INPUT_H   58
#define LIST_TOP  (INPUT_Y + INPUT_H + 20)
#define LIST_H    (PANEL_Y + PANEL_H - 2 - LIST_TOP)
#define LIST_PAD  12 // above the first post
#define FADE      36 // the posts fade out over this much of the bottom
#define FADE_TOP  14 // and of the top
// what scrolling shows whole: from LIST_PAD down to the fade
#define VIEW_H    (LIST_H - LIST_PAD - FADE + CARD_GAP + 8)

// a post, inside the box that marks the selected one
#define CARD_PAD      22
#define CARD_GAP      10  // under the box
#define CARD_R        30
#define PAD_TOP       24
#define PAD_BOTTOM    16
#define AVATAR        68
#define AVATAR_X      (MAIN_X + CARD_PAD)
#define TEXT_X        (AVATAR_X + AVATAR + 22)
#define TEXT_W        (MAIN_X + MAIN_W - CARD_PAD - TEXT_X)
#define REPOST_H      38
#define NAME_H        46
#define LINE_H        44
#define GAP           18  // above the media and the quote
#define RADIUS        28  // of a picture alone and of the quote
#define POLL_ROW      52  // a choice of a post's poll
#define POLL_GAP      10
#define POLL_FOOT     46  // its votes and time, under the choices
#define TILE_R        20  // of the pictures of a grid
#define MEDIA_GAP     8
#define MEDIA_H       400 // of two pictures or more
#define MEDIA_MAX     430 // of one, so a post with three lines fits whole
#define QUOTE_PAD     20
#define QUOTE_AVATAR  34
#define QUOTE_NAME_H  40
#define QUOTE_LINE_H  38
#define ACTIONS_H     52
#define ICON          32
#define ACTION_STEP   72
#define META_H        36
#define SEEN_H        76  // "You left off here", over the first post of Following seen before
#define COVER_H       118 // the cover over a post that may give the game away, under its name
#define WEEK_H        268 // "Your week on X", under the user's own profile
#define FOOTER_H      150 // under the last post: more loading, or why not

// the next post in the side column
#define SIDE_LINE_H   36  // a line of the side column's words
#define SIDE_HEAD_H   56  // a box's title
#define SIDE_TREND_H  80  // a trend in the side column
#define SIDE_MORE_H   44  // "Show more" under them
#define SIDE_USER_H   72  // someone to follow
#define SIDE_TRENDS   4
#define SIDE_PEOPLE   3
#define SIDE_ITEMS    6   // kept of either
#define SIDE_EVERY    (10 * 60) // seconds the side column's boxes are kept
#define SIDE_RETRY    (2 * 60)  // and after they did not come
#define SIDE_VIEW     0xfffffff0u // the requests of the side column

#define MORE_AHEAD 4   // posts left below the selection when more are read
#define MAX_POSTS  600 // kept in a tab; no more are read past them

#define TOAST_FRAMES 240 // four seconds

// the pointer the left stick moves, and the scrolling of the right one
#define STICK_DEAD 26 // of 128: where a stick at rest may lean
#define POINTER_R  14 // the pointer's bead; over something to press it grows by 7
#define SCROLL_MAX 40 // pixels a frame, the right stick all the way

#define SECTIONS  UI_USER // the lists of the bar; the ones after open from a post
#define STACK_MAX 8       // posts and profiles opened one from another
#define ZONES     200     // places the pointer can press in a frame

// a profile, on top of its posts
#define BANNER_H   250
#define BIG_AVATAR 136
#define BIO_LINES  4

// a notification
#define FACE         44
#define NOTICE_ICON  38
#define NOTICE_LINES 3

// 0xAABBGGRR; an alpha below FF lets what is under it show through
#define RGBA(r, g, b, a) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(g) << 8 | (r))
#define FG          RGBA(248, 245, 242, 255)
#define GREY        RGBA(200, 190, 182, 255) // the names' details, the counts
#define DIM         RGBA(160, 150, 142, 255)
#define ACCENT      RGBA(242, 122, 26, 255)  // orange
#define BLUE        RGBA(29, 155, 240, 255)  // X's badge
#define PINK        RGBA(249, 24, 128, 255)
#define GREEN       RGBA(0, 186, 124, 255)
#define GOLD        RGBA(226, 183, 25, 255)
#define STEEL       RGBA(130, 154, 171, 255)
#define RED         RGBA(244, 33, 46, 255)
#define WHITE       RGBA(255, 255, 255, 255)
#define INK         RGBA(28, 22, 18, 255)    // the check on a gold badge
#define CHIP        RGBA(255, 255, 255, 30)  // a pill on the glass
#define CHIP_ON     RGBA(255, 255, 255, 52)
#define SELECTED    RGBA(255, 255, 255, 22)
#define EDGE        RGBA(255, 255, 255, 34)
// what the pointer is on: lighter, with a bright rim, to be seen across a room
#define HOVER       RGBA(255, 255, 255, 58)
#define HOVER_RIM   RGBA(255, 255, 255, 175)
#define HOVER_CARD  RGBA(255, 255, 255, 100) // the rim of the post under it, under its buttons' own
#define WELL        RGBA(0, 0, 0, 52)        // a dip in the glass: the reply box, the tabs
#define PLACEHOLDER RGBA(255, 255, 255, 20)  // a picture still loading
#define THREAD      RGBA(255, 255, 255, 36)
// what changes with the theme: the sheets' glass, a dark tile, the video's bar
#define NIGHT       (g_settings.theme == SETTINGS_NIGHT)
#define PANE        (NIGHT ? RGBA(25, 25, 29, 255) : RGBA(56, 47, 41, 255))
#define SCRIM       (NIGHT ? RGBA(0, 0, 0, 172) : RGBA(12, 9, 8, 160)) // over the screen under a sheet
#define TILE        (NIGHT ? RGBA(10, 10, 12, 255) : RGBA(30, 24, 21, 255))
#define BAR         (NIGHT ? RGBA(16, 16, 19, 230) : RGBA(36, 29, 25, 230))
#define FLAT        (NIGHT ? RGBA(8, 8, 10, 255) : RGBA(44, 37, 33, 255))

// a text in the screen's language, from its Portuguese and English
#define L(pt, en) lang_text(g_lang, (pt), (en))

enum {
  F_BODY, F_NAME, F_SMALL, F_SMALL_BOLD, F_QUOTE, F_QUOTE_BOLD, F_PILL, F_PILL_OFF,
  F_BRAND, F_HEADING, F_LEGEND, F_TITLE, F_SIDE, F_SIDE_BOLD, F_KEY, F_COMPOSE, F_NAV, F_NAV_BOLD,
  F_CODE, // the linking's address and code
  FONTS
};


typedef struct rect {
  int x, y, w, h;
} rect_t;

typedef struct card {
  const x_post_t *post;
  int             y;      // in the list
  int             height;
  int             open;   // ✕ showed the whole text
  int             lines;
  int             more;   // the text has lines past the ones shown
  uint16_t        start[MAX_LINES];
  uint16_t        len[MAX_LINES];
  // from the card's top, and the media from (TEXT_X, media_y)
  int             avatar_y;
  int             body_y;
  int             media_y, media_w, media_h;
  rect_t          media[X_MAX_MEDIA];
  int             quote_y, quote_h, quote_lines, quote_more;
  int             poll_y, poll_h;
  uint16_t        quote_start[QUOTE_LINES];
  uint16_t        quote_len[QUOTE_LINES];
  int             actions_y;
  int             meta;   // the line of counts under the buttons
  int             rising; // climbing fast among the list's posts (see mark_rising)
  int             spoiler; // covered: it may give the game that runs away (see covered)
  int             bubble_x, bubble_y, bubble_w, bubble_h; // of a message, from the card's top
} card_t;

enum { LOAD_NONE, LOAD_TOP, LOAD_MORE };

typedef struct tab {
  int       feed;                   // UI_*
  unsigned  serial;                 // which list this is, for ui_result
  char      arg[X_ID_MAX];          // the user of UI_USER, the post of UI_POST, the
                                    // conversation of UI_DM
  x_post_t *posts;
  card_t   *cards;
  int       count;
  int       header_h;               // of the profile over the posts, or 0
  int       content_h;              // with the profile, and FOOTER_H under the last post
  int       sel, scroll, target;
  long long fetched_at;
  int       error, code, x_code;    // of the last reading from the top
  int       more_error, more_code;  // of the last reading of more
  char      cursor[X_CURSOR_MAX];   // of the posts after these, or empty
  int       loading;                // LOAD_*
  int       wanted;                 // □ asked for the top
  int       has_user;               // the profile, of UI_ME and UI_USER
  x_user_t  user;
  int       bio_lines;
  uint16_t  bio_start[BIO_LINES], bio_len[BIO_LINES];
  int       game_alone;        // its game on a line of its own (game_line_alone)
  char      query[X_QUERY_MAX + 1]; // what UI_SEARCH looks for, or empty before the keyboard
  int       latest;                 // its latest posts, not the top ones
  int       seen_at;                // of Following: the first post that was on top the
                                    // last time, under "You left off here", or 0
} tab_t;

// what the pointer can press
enum {
  Z_NAV, Z_TAB, Z_PRODUCT, Z_REFRESH, Z_BACK, Z_WELL, Z_VIEWER, // Z_PRODUCT: the search's tabs
  Z_SIDE_TREND, Z_SIDE_MORE, // a trend of the side column, and Explore over them
  Z_SIDE_USER, Z_SIDE_FOLLOW, // someone to follow, and their Follow button
  Z_SIDE_PLAYER, Z_SIDE_INVITE, // someone who plays now, and inviting them
  Z_SCHED_ROW, Z_SCHED_CLOSE, // a scheduled post, and closing their sheet
  Z_NAV_POST, Z_NAV_ME, // the bar's Post button and account
  Z_FOLLOW, Z_UMENU,    // a profile's Follow button and its menu
  Z_LINK,               // linking the account from a PC, where its session is missing
  Z_GROK,               // asking Grok for a hint
  Z_MINI, Z_MINI_CLOSE, // the small video in the corner: back to its post, and closing it
  // the sheets (Z_SHEET: around one, Z_PANE: the pane itself) and the
  // viewer's video
  Z_SHEET, Z_PANE, Z_C_CLOSE, Z_C_TEXT, Z_C_MEDIA, Z_C_REMOVE, Z_C_ADD, Z_C_SEND, Z_C_SCHEDULE, Z_C_GAME,
  Z_C_POLL, Z_C_CHOICE, Z_C_CHOICE_ADD, Z_C_LENGTH, // the composer's poll
  Z_C_DM, // the composer's capture, sent in a message
  Z_G_BACK, Z_G_ADD,
  Z_G_TILE, Z_S_ROW, Z_S_CLOSE, Z_S_PAGE, Z_V_PLAY, Z_V_SOUND, Z_V_BAR,
  Z_M_ROW, Z_Q_YES, Z_Q_NO, // a menu's rows, the question's buttons
  Z_L_PREV, Z_L_NEXT, Z_L_CLOSE, // the linking's account, and closing it
  Z_R_CLOSE, // closing the thread's reader
  Z_LG_PAGE, Z_LG_CLOSE, Z_LG_PREV, Z_LG_NEXT, // the login window's page, closing it, and its account
  Z_CARD, Z_AUTHOR, Z_LIKE, Z_REPLY, Z_REPOST, Z_BOOKMARK, Z_MORE, Z_MEDIA, // of the post arg
  Z_DOTS, // its menu
  Z_POLL, // a choice of its poll (sub)
  Z_T_SOUND, // the sound of the video playing in the timeline
};

typedef struct zone {
  int    kind; // Z_*
  int    arg;  // the button of the bar, the tab or the post
  int    sub;  // the picture
  rect_t r;    // on the screen
} zone_t;


static int            g_lang = LANG_EN;     // LANG_* the screen shows
static int            g_system_lang = LANG_EN;
static settings_t     g_settings;
static int            g_settings_dirty;     // changed since ui_settings_changed
static int            g_session_found;
static char           g_session_path[96];
static unsigned       g_accounts;           // which have a session file, bit 0 the first
// linking an account from a PC: the account it goes to, the sheet it was
// opened from (SHEET_SETTINGS or SHEET_NONE), and how it goes (main.c's)
static struct {
  int       account;
  int       from;
  ui_link_t st;
} g_linking;
// the login window (ui_login_*): whether the engine is there, the account
// it signs in, the sheet it was opened from, how it goes (main.c's), the
// page main.c hands over, and what the controller did to it, for main.c
#define LOGIN_INPUTS 64
static struct {
  int              ready;
  int              account;
  int              from;
  ui_login_t       st;
  const uint32_t  *pixels;
  int              pw, ph;
  ui_login_input_t in[LOGIN_INPUTS];
  int              first, count;
  int              px, py;  // where the pointer was last said to be, in the page's pixels
  float            wheel;   // the right stick's pixels not sent yet
  int              note;    // 1: a link out of X was not followed, 2: △ found no field
  uint64_t         note_us; // g_ui_us when it came
} g_login;
static int            g_section = UI_FOLLOWING; // under the lists opened from it
static int            g_home = UI_FOLLOWING;    // the timeline the house goes back to
static tab_t          g_tabs[SECTIONS];
static tab_t          g_stack[STACK_MAX];       // opened from a post, the last on top
static int            g_depth;
static unsigned       g_serial = SECTIONS;
static text_font_t   *g_fonts[FONTS], *g_fallbacks[FONTS];
static text_surface_t g_dst;      // the frame being drawn, or the timeline's part of it
static float          g_scale = 1;  // the screen's pixels a point (ui_scale)
static uint32_t      *g_backdrop; // the screen without what changes, UI_WIDTH x UI_HEIGHT

// the pointer, shown once the left stick moves it and until the buttons move
static struct {
  float x, y;
  int   shown;
  float live; // 0 to 1: how far it has turned into the ring over something to press
} g_ptr = {UI_WIDTH / 2.0f, UI_HEIGHT / 2.0f, 0, 0};
// the pointer's colour (red, green, blue) and how much of it a banner gave
// (0 to 1): the accent's, or a banner's, as the backdrop has it, and where
// it is going with the backdrop (backdrop_aim, backdrop_move)
static float g_ptr_col[4] = {242, 122, 26, 0}, g_ptr_to[4] = {242, 122, 26, 0};
static float g_step = 1; // how long the last frame took, in sixtieths of a second
static uint64_t g_ui_us; // how long the screen has been up, by ui_elapsed
static uint64_t g_input_us; // g_ui_us when a button or a stick was last used
static uint64_t g_active_us; // with XPS5 in use since the last break (see break_check)
static int g_player_wanted; // something drawn this frame shows the player

// what is under a sheet (the screen, dimmed for a pane, and the pane's
// shadow), drawn once and kept while only the pointer or the sheet itself
// changes: moving the pointer over a sheet then costs the sheet, not the
// whole screen. g_under_used: this frame began from it; g_under_fresh: it
// was drawn again this frame, and takes the pane's shadow when it comes
static uint32_t *g_under;
static int       g_under_sheet, g_under_stale = 1, g_under_scroll, g_under_theme;
static int       g_under_used, g_under_fresh;
static long long g_under_now;
static struct {
  int x, y, w, h;
} g_under_pane; // whose shadow it has

// where the pointer could press in the last frame; what is drawn in the
// timeline lands g_off_y lower on the screen, cut to g_clip_*
static zone_t g_zones[ZONES];
static int    g_zone_count;
static int    g_off_y, g_clip_top, g_clip_bottom = UI_HEIGHT;

// what the pointer lit up in the last frame, for the tick main.c plays
// (ui_hover_take) when it moves onto something else: kind -1 for nothing,
// card the post whose card it is in, or -1
static struct {
  int   kind, arg, sub, card;
  float x, y;
  int   pending;
  float pan;
} g_lit = {-1, 0, 0, -1, 0, 0, 0, 0};

// a post's pictures over the whole screen
static struct {
  int       open;
  int       index, count;
  int       waiting; // for the picture
  uint64_t  input_us; // g_ui_us when a button or the stick was last used, for a video's controls
  x_media_t media[X_MAX_MEDIA];
} g_viewer;

// the video of a post the selection left while it played on: in a corner
// of the timeline (draw_mini) until another one plays, the list changes, or
// R3 or its ✕ closes it
#define MINI_W     480
#define MINI_H     270
#define MINI_AFTER (4ull * 1000000) // playing that long in its post, it follows into the corner
static struct {
  x_media_t media;
  char      post[X_ID_MAX];
  unsigned  serial;   // of the list it is in
  uint64_t  since_us; // g_ui_us when it began to play in its post
  int       on;       // it plays, in its post or in the corner
  int       here;     // its post showed it in this frame
  int       shown;    // the corner did, in the last frame
} g_mini;

// what R2 or L2 asked for, until main.c says how it went
static struct {
  int  kind;  // UI_ACT_*
  int  taken; // by ui_action
  char id[sizeof ((x_post_t *)0)->id];
  char user[sizeof ((x_post_t *)0)->user];
  char card[sizeof ((x_post_t *)0)->poll_card]; // a vote's poll, and the choice
  char card_name[sizeof ((x_post_t *)0)->poll_name];
  int  choice;
} g_act;

// the composer's capture goes in a message: Chat picks the conversation
static int g_pick;

// an invite to play being sent, and the game of one being opened
static struct {
  char text[PLAY_INVITE];
  char game[PLAY_GAME];
  char id[16];
} g_play_act;

// the message being written to a conversation, kept when it did not go
static struct {
  char conv[X_ID_MAX];
  char text[X_REPLY_MAX + 1];
} g_dm;

// the game that runs beside the app, as the payload says (ui_game), and
// the one whose posts Home's tab of the game has (it stays while it is shown)
static struct {
  char id[GAME_ID], name[GAME_NAME];
  char tab[GAME_NAME];
} g_game;
#define GAME_TAB_W    200 // the most of the game's name its tab on Home shows
#define GAME_TAB_ICON 30

// Grok's hints (UI_GROK): the question asked about the game, and its
// answer or why it did not come; fresh: the next one starts a new
// conversation
static struct {
  char question[X_GROK_QUESTION];
  char game[GAME_NAME];
  char answer[X_GROK_ANSWER];
  int  asking;
  int  error, code, x_code;
  int  fresh;
} g_grok = {.fresh = 1};

// XPS5's updates (Settings' last row): how they go, as main.c says, and
// what the row asked of main.c
static struct {
  int  state; // UI_UPDATE_*
  char version[24];
  int  percent;
  int  error, code;
  int  wanted;
} g_update;


// the posts about the game that runs stay covered (Settings, spoilers): its
// name, how posts name it, and the posts ✕ uncovered since
#define REVEALED 32
static char        g_spoil_name[GAME_NAME];
static game_keys_t g_spoil;
static char        g_revealed[REVEALED][X_ID_MAX];
static int         g_revealed_n;

// a line at the bottom for a few seconds, the way x.com says a post was sent
static struct {
  char text[200];
  int  error;
  int  frames;
} g_toast;


static void paint_backdrop(void);
static int  backdrop_move(void);
static int  compose_tag_game(void);
static int  open_scheduled(void);
static int  poll_open(const x_post_t *p, long long now);
static int  open_link(int from);
static int  open_signin(int from);
static int  close_login(void);
static int  login_keys(unsigned keys);
static int  login_click(void);
static int  login_sticks(int moved, int ry);
static int  login_account_step(int d);
static void draw_login(unsigned frame);
static void read_account(int at);
static int  own_post(const x_post_t *p);
static int  pointer_turn(void);
static int  ask_unschedule(int i);
struct side_box;
static void side_keep(struct side_box *b, const x_post_t *posts, int count, int kind);

// the sheets over the panel
enum { SHEET_NONE, SHEET_COMPOSE, SHEET_GALLERY, SHEET_SETTINGS, SHEET_MENU, SHEET_CONFIRM, SHEET_SCHEDULED,
       SHEET_LINK, SHEET_READER, SHEET_LOGIN };
static void mini_keep(const x_post_t *p, const x_media_t *m);
static int  mini_back(tab_t *t);
static int  mini_close(void);
static void draw_pane(int x, int y, int w, int h);
static int  round_button(int kind, int cx, int cy, int icon);
static void draw_hints(int cx, int y, const char *line);
static int  open_more(void);
static int  open_accounts(void);
static int  open_post_menu(const x_post_t *p, int right, int top, int bottom);
static int  open_user_menu(const struct tab *t, int right, int top, int bottom);
static void history_add(const x_post_t *p);
static void show_selected(tab_t *t);
static void fill_history(void);
static int  is_mine(const x_post_t *p);
static int  pick_chat(void);
static int  share_to(const x_post_t *p);
static int  g_me_asked; // the signed-in user's profile was asked for, once
static long long g_now; // the time of the last frame drawn

// the side column's boxes, kept SIDE_EVERY seconds: "What's happening",
// X's first trends (x.com's side column of them, or Explore's when that
// did not come: source), and "Who to follow"
typedef struct side_box {
  x_post_t  items[SIDE_ITEMS];
  int       count, loading, error, source;
  long long at; // when they were last asked for or came, or 0
} side_box_t;
static side_box_t g_trends, g_who;

// "Playing now": the people the user follows whose profile says a game,
// read every SIDE_PLAYING_EVERY seconds for the user whose number is in who
#define SIDE_PLAYING_EVERY (5 * 60)
static side_box_t g_playing;
static char       g_playing_who[24];

// the user's scheduled posts, in their sheet (from More), soonest first
static struct {
  x_post_t *items; // mem_alloc'd
  int       count, sel;
  int       wanted, loading, error, code;
} g_sched;
static void remove_post(const char *id);
static const char *list_title(const struct tab *t);
static int g_sheet;

static int  open_composer(const x_post_t *to);
static int  open_settings(void);
static int  sheet_keys(unsigned keys);
static int  sheet_click(const zone_t *z);
static int  sheet_animate(void);
static int  sheet_scroll(int ry);
static void compose_done(int error);
static void compose_when(char *buf, size_t size);
static int  viewer_keys(unsigned keys);
static int  lang_shown(void);
static int  inline_video(const x_media_t *m, int index, int x, int y, int w, int h, float r);
enum { INLINE_NONE = -1, INLINE_WAIT, INLINE_PLAYING, INLINE_PAUSED };
static unsigned g_frame_no; // ui_draw's frame, for the spinners drawn deep down
static int      g_inline_sound; // the timeline's videos play with their sound, once it was turned on
static int      g_spinning; // a video's spinner was drawn: the next frame too
static void spinner(float cx, float cy, float r, float t, unsigned frame, uint32_t color);
static const char *video_of(const x_media_t *m);
static void draw_viewer_video(const x_media_t *m, const char *still, unsigned frame);
static void draw_sheet(long long now, unsigned frame);
static void draw_snackbar(void);


static text_font_t *
font(int id) {
  return g_fonts[id];
}


/**
 * the emoji x.com shows, Twemoji's pictures on abs-0.twimg.com
 **/
typedef struct copy_job {
  void       *to;
  const void *from;
  size_t      bytes;
} copy_job_t;


static void
copy_part(void *arg, int part, int parts) {
  const copy_job_t *j = arg;
  const size_t from = j->bytes * part / parts & ~(size_t)63;
  const size_t to = part + 1 == parts ? j->bytes : j->bytes * (part + 1) / parts & ~(size_t)63;

  memcpy((char *)j->to + from, (const char *)j->from + from, to - from);
}


/**
 * memcpy of a whole screen, shared by the workers
 **/
static void
screen_copy(void *to, const void *from, size_t bytes) {
  copy_job_t j = {to, from, bytes};

  workers_run(copy_part, &j);
}


/**
 * w points in the screen's pixels, as text_size gives them
 **/
static int
pxs(float w) {
  return (int)floorf(w * g_scale + 0.5f);
}


/**
 * the picture for url, made for a box of w x h points at the screen's
 * pixels (see images_get)
 **/
static const uint32_t *
picture(const char *url, int w, int h) {
  return images_get(url, pxs(w), pxs(h));
}


/**
 * the size a video's pictures are made at for a box of w x h points: the
 * box's pixels, or, past the player's 1920 x 1080, the box's points (4K
 * draws each pixel of a 1080p video as 2 x 2)
 **/
static void
video_px(int w, int h, int *pw, int *ph) {
  *pw = pxs(w) & ~1;
  *ph = pxs(h) & ~1;
  if(*pw > 1920 || *ph > 1080) {
    *pw = w;
    *ph = h;
  }
}


static const uint32_t *
emoji_picture(const char *name, int size) {
  char url[IMAGES_URL_MAX];

  snprintf(url, sizeof url, "https://abs-0.twimg.com/emoji/v2/72x72/%s.png", name);
  return images_get(url, size, size);
}


int
ui_init(const settings_t *s, int system_lang) {
  static const struct { int bold; float px; } specs[FONTS] = {
    [F_BODY] = {0, 34},       [F_NAME] = {1, 34},       [F_SMALL] = {0, 27},
    [F_SMALL_BOLD] = {1, 27}, [F_QUOTE] = {0, 30},      [F_QUOTE_BOLD] = {1, 30},
    [F_PILL] = {1, 28},       [F_PILL_OFF] = {0, 28},   [F_BRAND] = {1, 33},
    [F_HEADING] = {1, 31},    [F_LEGEND] = {0, 28},     [F_TITLE] = {1, 46},
    [F_SIDE] = {0, 27},       [F_SIDE_BOLD] = {1, 28},  [F_KEY] = {1, 22},
    [F_COMPOSE] = {0, 40},      [F_NAV] = {0, 31},        [F_NAV_BOLD] = {1, 31},
    [F_CODE] = {1, 64},
  };

  g_settings = *s;
  g_system_lang = system_lang;
  g_lang = lang_shown();
  for(int i=0; i<FONTS; i++) {
    // Inter, with DejaVu for the arrows and symbols it lacks
    if(!(g_fonts[i]=text_font_open_scaled(specs[i].bold ? inter_bold : inter_regular, specs[i].px, g_scale)) ||
       !(g_fallbacks[i]=text_font_open_scaled(specs[i].bold ? dejavu_sans_bold : dejavu_sans, specs[i].px,
                                              g_scale))) {
      return -1;
    }
    text_font_set_fallback(g_fonts[i], g_fallbacks[i]);
  }
  text_set_emoji(emoji_picture);
  paint_backdrop();
  for(int i=0; i < SECTIONS; i++) {
    g_tabs[i].feed = i;
    g_tabs[i].serial = i + 1;
  }
  g_tabs[UI_GAME].latest = 1; // the latest posts about the game
  g_tabs[g_section].wanted = 1;
  return 0;
}


void
ui_scale(float scale) {
  g_scale = scale > 1 ? scale : 1;
}


int
ui_lang(void) {
  return g_lang;
}


const char *
ui_text(const char *pt, const char *en) {
  return L(pt, en);
}


int
ui_settings_changed(settings_t *s) {
  if(!g_settings_dirty) {
    return 0;
  }
  g_settings_dirty = 0;
  *s = g_settings;
  return 1;
}


void
ui_session_info(int found, const char *path) {
  g_session_found = found;
  snprintf(g_session_path, sizeof g_session_path, "%s", path ? path : "");
}


void
ui_accounts(unsigned present) {
  g_accounts = present;
}


int
ui_account(void) {
  return g_settings.account;
}


/**
 * the signed-in user's screen name and number, kept in the settings for the
 * account (the payload reads their posts with the number)
 **/
static void
remember_user(const x_user_t *u) {
  char *kept = g_settings.users[g_settings.account - 1], *id = g_settings.ids[g_settings.account - 1];
  size_t len = strlen(u->user), digits = strspn(u->id, "0123456789");

  if(len && len < sizeof g_settings.users[0] && strcmp(kept, u->user)) {
    memcpy(kept, u->user, len + 1);
    g_settings_dirty = 1;
  }
  if(digits && !u->id[digits] && digits < sizeof g_settings.ids[0] && strcmp(id, u->id)) {
    memcpy(id, u->id, digits + 1);
    g_settings_dirty = 1;
  }
}


/*
 * Text.
 */

/**
 * the baseline that centres a line of font in a row h high at top
 **/
static int
baseline(text_font_t *f, int top, int h) {
  return top + (h - text_line_height(f)) / 2 + text_ascent(f);
}


static int
width_of(text_font_t *f, const char *s) {
  return text_width(f, s, strlen(s));
}


static int
text(text_font_t *f, int x, int base, const char *s, uint32_t color) {
  return text_draw(f, &g_dst, x, base, s, strlen(s), color);
}


/**
 * s cut with an ellipsis to fit width; returns the x after it
 **/
static int
text_fitted(text_font_t *f, int x, int base, const char *s, int width, uint32_t color) {
  static const char ellipsis[] = "\xe2\x80\xa6";
  size_t len = strlen(s), fit;

  if(text_width(f, s, len) <= width) {
    return text_draw(f, &g_dst, x, base, s, len, color);
  }
  fit = text_fit(f, s, len, width - text_width(f, ellipsis, 3));
  x = text_draw(f, &g_dst, x, base, s, fit, color);
  return text_draw(f, &g_dst, x, base, ellipsis, 3, color);
}


/**
 * a paragraph wrapped to width, a line every line_h; returns the y after it
 **/
static int
text_wrapped(text_font_t *f, int x, int y, int line_h, const char *s, int width, uint32_t color) {
  const char *next;
  size_t len;

  while(*s) {
    len = text_break(f, s, width, &next);
    text_draw(f, &g_dst, x, baseline(f, y, line_h), s, len, color);
    y += line_h;
    s = next;
  }
  return y;
}


/**
 * how many lines text_wrapped() breaks s into at width
 **/
static int
wrapped_lines(text_font_t *f, const char *s, int width) {
  const char *next;
  int n = 0;

  while(*s) {
    text_break(f, s, width, &next);
    n++;
    s = next;
  }
  return n;
}


/**
 * the lines text breaks into at width, at most max; *more says whether
 * some did not fit
 **/
static int
break_lines(text_font_t *f, const char *text, int width, int max, uint16_t *start,
            uint16_t *len, int *more) {
  const char *s = text, *next;
  int n = 0;

  while(*s && n < max) {
    start[n] = s - text;
    len[n] = text_break(f, s, width, &next);
    n++;
    s = next;
  }
  *more = *s != 0;
  return n;
}


/**
 * how long ago t was, the way x.com says it: "45 s", "5 min", "3 h", "3 de nov"
 **/
static void
age(long long t, long long now, char *buf, size_t size) {
  long long diff = now - t;
  time_t when = (time_t)t, today = (time_t)now;
  struct tm *tm;
  int this_year;

  if(diff < 1) {
    snprintf(buf, size, "%s", L("agora", "now"));
  } else if(diff < 60) {
    snprintf(buf, size, L("%lld s", "%llds"), diff);
  } else if(diff < 3600) {
    snprintf(buf, size, L("%lld min", "%lldm"), diff / 60);
  } else if(diff < 86400) {
    snprintf(buf, size, L("%lld h", "%lldh"), diff / 3600);
  } else if((tm=gmtime(&today))) {
    this_year = tm->tm_year;
    if(!(tm=gmtime(&when))) {
      buf[0] = 0;
      return;
    }
    lang_date(g_lang, tm->tm_mday, tm->tm_mon, tm->tm_year == this_year ? 0 : tm->tm_year + 1900, buf,
              size);
  } else {
    buf[0] = 0;
  }
}


/**
 * a count the way x.com shortens it: 999, 1,2 mil, 15 mil, 1,2 M (1.2K,
 * 15K, 1.2M in English), cut rather than rounded
 **/
static void
count_text(long long n, char *buf, size_t size) {
  long long whole, tenth;
  int unit;

  if(n < 1000) {
    snprintf(buf, size, "%lld", n);
    return;
  }
  unit = n >= 1000000;
  whole = unit ? n / 1000000 : n / 1000;
  tenth = unit ? n / 100000 % 10 : n / 100 % 10;
  if(whole < 10 && tenth) {
    snprintf(buf, size, "%lld%c%lld%s", whole, lang_decimal(g_lang), tenth, lang_unit(g_lang, unit));
  } else {
    snprintf(buf, size, "%lld%s", whole, lang_unit(g_lang, unit));
  }
}


/*
 * Layout of the posts.
 */

static int
layout_media(card_t *c) {
  const x_post_t *p = c->post;
  int half = (TEXT_W - MEDIA_GAP) / 2, right = half + MEDIA_GAP, top = (MEDIA_H - MEDIA_GAP) / 2;
  int w = TEXT_W, h;
  rect_t *m = c->media;

  c->media_w = TEXT_W;
  switch(p->media_count) {
  case 1:
    // one picture shows whole: a tall one narrower, not cut
    h = p->media[0].width && p->media[0].height ?
      (int)((long long)TEXT_W * p->media[0].height / p->media[0].width) : MEDIA_H;
    if(h > MEDIA_MAX) {
      w = (int)((long long)TEXT_W * MEDIA_MAX / h);
      h = MEDIA_MAX;
    }
    w = w < TEXT_W / 2 ? TEXT_W / 2 : w;
    h = h < 240 ? 240 : h;
    m[0] = (rect_t){0, 0, w, h};
    c->media_w = w;
    return h;
  case 2:
    m[0] = (rect_t){0, 0, half, MEDIA_H};
    m[1] = (rect_t){right, 0, TEXT_W - right, MEDIA_H};
    return MEDIA_H;
  case 3:
    m[0] = (rect_t){0, 0, half, MEDIA_H};
    m[1] = (rect_t){right, 0, TEXT_W - right, top};
    m[2] = (rect_t){right, top + MEDIA_GAP, TEXT_W - right, MEDIA_H - top - MEDIA_GAP};
    return MEDIA_H;
  default:
    m[0] = (rect_t){0, 0, half, top};
    m[1] = (rect_t){right, 0, TEXT_W - right, top};
    m[2] = (rect_t){0, top + MEDIA_GAP, half, MEDIA_H - top - MEDIA_GAP};
    m[3] = (rect_t){right, top + MEDIA_GAP, TEXT_W - right, MEDIA_H - top - MEDIA_GAP};
    return MEDIA_H;
  }
}


/**
 * a notification: the people in it beside its icon, what it says, and the
 * post it is about in grey
 **/
static void
layout_notice(card_t *c) {
  const x_post_t *p = c->post;
  int y = PAD_TOP + FACE + 12;

  c->avatar_y = PAD_TOP;
  c->body_y = y;
  c->lines = break_lines(font(F_BODY), p->text, TEXT_W, NOTICE_LINES, c->start, c->len, &c->more);
  y += c->lines * LINE_H;
  c->quote_lines = 0;
  if(p->quote_text[0]) {
    c->quote_y = y + 4;
    c->quote_lines = break_lines(font(F_QUOTE), p->quote_text, TEXT_W, NOTICE_LINES, c->quote_start,
                                 c->quote_len, &c->quote_more);
    y = c->quote_y + c->quote_lines * QUOTE_LINE_H;
  }
  c->media_h = c->quote_h = c->meta = 0;
  c->actions_y = y;
  c->height = y + PAD_BOTTOM + 6 + CARD_GAP;
}


#define BUBBLE_MAX (MAIN_W * 3 / 5) // the widest a message is
#define BUBBLE_PAD 20
#define BUBBLE_R   26
#define GROUP_FACE 46 // the two people of a group without a picture
#define DM_TIME_H  32


/**
 * what a message carries besides its text, in a few words, and its icon
 **/
static const char *
dm_carries(int what, int *icon) {
  switch(what) {
  case X_DM_PHOTO:
    *icon = ICON_PHOTO;
    return L("Foto", "Photo");
  case X_DM_VIDEO:
    *icon = ICON_VIDEO;
    return L("V\xc3\xad" "deo", "Video");
  case X_DM_GIF:
    *icon = ICON_VIDEO;
    return "GIF";
  case X_DM_POST:
    *icon = ICON_SHARE;
    return L("Post partilhado", "Shared post");
  case X_DM_INVITE:
    *icon = ICON_GAME;
    return L("\xe2\x9c\x95 Abrir o jogo", "\xe2\x9c\x95 Open the game");
  default:
    *icon = -1;
    return "";
  }
}


/**
 * how much of a message's text shows: an invite's tag, which names its game
 * for XPS5, stays out
 **/
static int
dm_shown(const x_post_t *p) {
  const char *tag = NULL;

  for(const char *s=p->icon == X_DM_INVITE ? strstr(p->text, " (XPS5") : NULL; s; s=strstr(s + 1, " (XPS5")) {
    tag = s;
  }
  return tag ? (int)(tag - p->text) : (int)strlen(p->text);
}


/**
 * a conversation of Chat: its picture, who it is with, and its last message
 * on one line
 **/
static void
layout_chat(card_t *c) {
  c->avatar_y = PAD_TOP;
  c->body_y = PAD_TOP + NAME_H;
  c->lines = c->more = 0;
  c->media_h = c->quote_h = c->quote_lines = c->meta = 0;
  c->actions_y = c->body_y + LINE_H;
  c->height = c->actions_y + PAD_BOTTOM + CARD_GAP;
}


/**
 * a message: a bubble as wide as its longest line, the user's own on the
 * right, the others' on the left beside their picture (in a group, under
 * their name), and when it was sent under it
 **/
static void
layout_dm(card_t *c) {
  const x_post_t *p = c->post;
  text_font_t *f = font(F_BODY);
  int w = 0, y = 8, lw, icon;
  const char *carries = dm_carries(p->icon, &icon);
  char shown[sizeof p->text];

  if(p->group && !p->mine) {
    y += 36;
  }
  // the lines of what shows, at the same places as in the text
  snprintf(shown, sizeof shown, "%.*s", dm_shown(p), p->text);
  c->lines = shown[0] ? break_lines(f, shown, BUBBLE_MAX - 2 * BUBBLE_PAD, MAX_LINES, c->start, c->len,
                                    &c->more) : 0;
  for(int i=0; i < c->lines; i++) {
    lw = text_width(f, p->text + c->start[i], c->len[i]);
    w = lw > w ? lw : w;
  }
  c->quote_lines = carries[0] ? 1 : 0;
  if(c->quote_lines) {
    lw = 40 + width_of(f, carries);
    w = lw > w ? lw : w;
  }
  c->bubble_w = w + 2 * BUBBLE_PAD;
  c->bubble_w = c->bubble_w < 2 * BUBBLE_R + 20 ? 2 * BUBBLE_R + 20 : c->bubble_w;
  c->bubble_h = (c->lines + c->quote_lines) * LINE_H + 2 * BUBBLE_PAD - 16;
  c->bubble_h = c->bubble_h < 2 * BUBBLE_R ? 2 * BUBBLE_R : c->bubble_h;
  c->bubble_x = p->mine ? MAIN_X + MAIN_W - CARD_PAD - c->bubble_w : MAIN_X + CARD_PAD + FACE + 14;
  c->bubble_y = y;
  c->avatar_y = y + c->bubble_h - FACE;
  c->body_y = y + BUBBLE_PAD - 8;
  c->media_h = c->quote_h = c->meta = 0;
  c->actions_y = y + c->bubble_h + 4;
  c->height = c->actions_y + DM_TIME_H + 4 + CARD_GAP;
}


#define TREND_PIC  104 // a story's picture, on the right of a trend
#define TREND_INFO 36  // the grey lines over and under its name
#define LIST_PIC   96  // a list's banner, on its left
#define LIST_FACE  30  // its owner's picture


/**
 * a trend of Explore, as x.com lists them: where it trends over its name
 * (two lines at most, for a story of the news), how much it is posted under
 * it, and a story's picture on the right
 **/
static void
layout_trend(card_t *c) {
  const x_post_t *p = c->post;
  int w = MAIN_W - 2 * CARD_PAD - 16 - (p->avatar[0] ? TREND_PIC + 20 : 0), least;

  c->avatar_y = PAD_TOP;
  c->body_y = PAD_TOP + TREND_INFO;
  c->lines = break_lines(font(F_NAME), p->name, w, 2, c->start, c->len, &c->more);
  c->media_h = c->quote_h = c->quote_lines = c->meta = 0;
  c->actions_y = c->body_y + c->lines * LINE_H;
  c->height = c->actions_y + (p->quote_text[0] ? TREND_INFO : 0) + PAD_BOTTOM + CARD_GAP;
  least = PAD_TOP + (p->avatar[0] ? TREND_PIC : 0) + PAD_BOTTOM + CARD_GAP;
  c->height = c->height < least ? least : c->height;
}


/**
 * a list: its banner on the left, its name, whose it is, two lines of its
 * description and how many are in it
 **/
static void
layout_list(card_t *c) {
  const x_post_t *p = c->post;
  int w = MAIN_W - 2 * CARD_PAD - LIST_PIC - 22, least;

  c->avatar_y = PAD_TOP;
  c->body_y = PAD_TOP + NAME_H + (p->user[0] ? TREND_INFO + 4 : 0);
  c->lines = p->quote_text[0] ? break_lines(font(F_SMALL), p->quote_text, w, 2, c->start, c->len, &c->more) : 0;
  c->media_h = c->quote_h = c->quote_lines = c->meta = 0;
  c->actions_y = c->body_y + c->lines * TREND_INFO;
  c->height = c->actions_y + TREND_INFO + PAD_BOTTOM + CARD_GAP;
  least = PAD_TOP + LIST_PIC + PAD_BOTTOM + CARD_GAP;
  c->height = c->height < least ? least : c->height;
}


static int own_post(const x_post_t *p);


/**
 * whether p is one of the posts of Home's tab of the game, or of a list
 * opened from it
 **/
static int
of_game_tab(const x_post_t *p) {
  const tab_t *t = &g_tabs[UI_GAME];

  if(t->posts && p >= t->posts && p < t->posts + t->count) {
    return 1;
  }
  for(int i=0; g_section == UI_GAME && i < g_depth; i++) {
    t = &g_stack[i];
    if(t->posts && p >= t->posts && p < t->posts + t->count) {
      return 1;
    }
  }
  return 0;
}


/**
 * whether the post p stays covered, as it may give the game that runs away:
 * it names it, it is not the user's own, and ✕ did not uncover it. The
 * game's own tab is all about it: there, the posts with words that tend to
 * give it away ("ending", "final boss")
 **/
static int
covered(const x_post_t *p) {
  if(!g_settings.spoilers || !g_spoil.count || p->kind != X_KIND_POST || !p->id[0] || own_post(p)) {
    return 0;
  }
  for(int i=0; i < REVEALED; i++) {
    if(!strcmp(g_revealed[i], p->id)) {
      return 0;
    }
  }
  if(of_game_tab(p)) {
    return game_spoils(p->text, &g_spoil) || game_spoils(p->quote_text, &g_spoil);
  }
  return game_named(p->text, &g_spoil) || game_named(p->quote_text, &g_spoil);
}


static void
layout_card(card_t *c) {
  const x_post_t *p = c->post;
  int y = PAD_TOP, least;

  if(p->kind == X_KIND_NOTICE) {
    layout_notice(c);
    return;
  }
  if(p->kind == X_KIND_TREND) {
    layout_trend(c);
    return;
  }
  if(p->kind == X_KIND_LIST) {
    layout_list(c);
    return;
  }
  if(p->kind == X_KIND_CHAT) {
    layout_chat(c);
    return;
  }
  if(p->kind == X_KIND_DM) {
    layout_dm(c);
    return;
  }
  if(p->reposter[0] || p->pinned) {
    y += REPOST_H;
  }
  c->avatar_y = y;
  least = y + AVATAR + PAD_TOP;
  y += NAME_H;

  c->body_y = y;
  c->spoiler = covered(p);
  c->lines = break_lines(font(F_BODY), p->text, TEXT_W, c->open ? MAX_LINES : SHORT_LINES,
                         c->start, c->len, &c->more);
  if(c->spoiler) {
    c->lines = c->more = 0; // its cover instead of all it shows (draw_cover)
    y += COVER_H;
  }
  // a shortened text does not end on an empty line
  while(c->more && !c->open && c->lines > 1 && !c->len[c->lines - 1]) {
    c->lines--;
  }
  y += c->lines * LINE_H;
  if(c->more && !c->open) {
    y += LINE_H; // "Mostrar mais"
  }

  c->media_h = 0;
  if(p->media_count && !c->spoiler) {
    y += c->lines ? GAP - 6 : 6;
    c->media_y = y;
    c->media_h = layout_media(c);
    y += c->media_h;
  }
  c->poll_h = 0;
  if(p->poll_count && !c->spoiler) {
    y += c->lines || c->media_h ? GAP - 6 : 6;
    c->poll_y = y;
    c->poll_h = p->poll_count * (POLL_ROW + POLL_GAP) - POLL_GAP + POLL_FOOT;
    y += c->poll_h;
  }
  c->quote_h = 0;
  if(p->quote_user[0] && !c->spoiler) {
    y += c->lines || c->media_h ? GAP - 6 : 6;
    c->quote_y = y;
    c->quote_lines = break_lines(font(F_QUOTE), p->quote_text, TEXT_W - 2 * QUOTE_PAD,
                                 QUOTE_LINES, c->quote_start, c->quote_len, &c->quote_more);
    c->quote_h = QUOTE_PAD + QUOTE_NAME_H + c->quote_lines * QUOTE_LINE_H + QUOTE_PAD - 4;
    y += c->quote_h;
  }

  c->actions_y = y + 4;
  y = c->actions_y + ACTIONS_H;
  c->meta = c->open && p->views > 0; // the other counts are on the buttons
  y += c->meta ? META_H : 0;
  y += PAD_BOTTOM;
  c->height = (y > least ? y : least) + CARD_GAP;
}


/**
 * put the cards one under the other
 **/
static void
place(tab_t *t) {
  t->content_h = t->header_h;
  for(int i=0; i<t->count; i++) {
    t->content_h += i && i == t->seen_at ? SEEN_H : 0; // "You left off here" over it
    t->cards[i].y = t->content_h;
    t->content_h += t->cards[i].height;
  }
  t->content_h += t->count ? FOOTER_H : 0;
}


static int game_line_alone(const x_user_t *u);

/**
 * the lines of a profile's bio, and how high the profile is on top of its
 * posts (see draw_profile)
 **/
static void
layout_profile(tab_t *t) {
  int more;

  t->header_h = 0;
  t->game_alone = 0;
  if(!t->has_user) {
    return;
  }
  t->game_alone = game_line_alone(&t->user);
  t->bio_lines = break_lines(font(F_BODY), t->user.bio, MAIN_W - 2 * CARD_PAD, BIO_LINES,
                             t->bio_start, t->bio_len, &more);
  t->header_h = BANNER_H + 78 + 56 + 40 + t->bio_lines * LINE_H + (t->bio_lines ? 12 : 0) +
                44 + (t->game_alone ? 44 : 0) + 48 + 26 + (t->feed == UI_ME ? WEEK_H : 0);
}


static void
set_cursor(tab_t *t, const char *cursor) {
  snprintf(t->cursor, sizeof t->cursor, "%s", cursor && strlen(cursor) < sizeof t->cursor ?
           cursor : "");
}


/**
 * whether posts has a post with that number
 **/
static int
has_post(const x_post_t *posts, int count, const char *id) {
  for(int i=0; i<count; i++) {
    if(!strcmp(posts[i].id, id)) {
      return 1;
    }
  }
  return 0;
}


/**
 * put the posts that came after t's under them, leaving out the ones it
 * already shows; returns how many it put, or -1 without the memory
 **/
static int
append(tab_t *t, const x_post_t *posts, int count) {
  x_post_t *all;
  card_t *cards;
  int n = t->count, before = t->count;

  if(count <= 0) {
    return 0;
  }
  if(!(all=mem_realloc(t->posts, (n + count) * sizeof *all))) {
    return -1;
  }
  // the posts may have moved
  t->posts = all;
  for(int i=0; i<n; i++) {
    t->cards[i].post = &all[i];
  }
  if(!(cards=mem_realloc(t->cards, (n + count) * sizeof *cards))) {
    return -1;
  }
  t->cards = cards;
  for(int i=0; i<count && n < MAX_POSTS; i++) {
    if(!posts[i].id[0] || !has_post(all, n, posts[i].id)) {
      all[n] = posts[i];
      memset(&cards[n], 0, sizeof *cards);
      cards[n].post = &all[n];
      t->count = n + 1; // its card knows its list
      layout_card(&cards[n]);
      n++;
    }
  }
  count = n - before;
  t->count = n;
  place(t);
  return count;
}


static void mark_rising(tab_t *t, long long now);
static void toast(int error, const char *fmt, ...) __attribute__((format(printf, 2, 3)));


static void
more_result(tab_t *t, x_post_t *posts, int count, const char *cursor, int error, int code) {
  int added;

  t->more_error = error;
  t->more_code = code;
  if(error == ERR_EMPTY) {
    // nothing after these
    t->more_error = ERR_NONE;
    t->cursor[0] = 0;
  }
  if(!posts) {
    return;
  }
  added = append(t, posts, count);
  mem_free(posts);
  if(added > 0) {
    mark_rising(t, g_now);
  }
  if(added < 0) {
    t->more_error = ERR_MEMORY;
  } else if(!added || t->count >= MAX_POSTS) {
    // a page of posts already shown would come again and again
    t->cursor[0] = 0;
  } else {
    set_cursor(t, cursor);
  }
}


/**
 * the list a request came from, while it is there
 **/
static tab_t *
find_view(unsigned serial) {
  for(int i=0; i < SECTIONS; i++) {
    if(g_tabs[i].serial == serial) {
      return &g_tabs[i];
    }
  }
  for(int i=0; i < g_depth; i++) {
    if(g_stack[i].serial == serial) {
      return &g_stack[i];
    }
  }
  return NULL;
}


/**
 * the list shown: the last one opened, else the section's
 **/
static tab_t *
view(void) {
  return g_depth ? &g_stack[g_depth - 1] : &g_tabs[g_section];
}


static void clamp_target(tab_t *t);


/**
 * a post with its replies opens on the post, whole
 **/
static void
focus(tab_t *t) {
  card_t *c;

  for(int i=0; i < t->count; i++) {
    if(!strcmp(t->posts[i].id, t->arg)) {
      c = &t->cards[i];
      t->sel = i;
      c->open = 1;
      layout_card(c);
      place(t);
      t->target = c->y;
      clamp_target(t);
      t->scroll = t->target;
      return;
    }
  }
}


/**
 * the messages of a conversation: which the user sent, and whether it is a
 * group's, whose messages say who sent them; in Chat it is read now
 **/
static void
dm_mark(const tab_t *t, x_post_t *posts, int count) {
  const tab_t *me = &g_tabs[UI_ME];
  tab_t *chat = &g_tabs[UI_CHAT];

  for(int i=0; i < count; i++) {
    posts[i].mine = me->has_user && !strcmp(posts[i].user_id, me->user.id);
    posts[i].group = !t->user.user[0];
  }
  for(int i=0; i < chat->count; i++) {
    if(!strcmp(chat->posts[i].id, t->arg)) {
      chat->posts[i].unread = 0;
    }
  }
}


#define SEEN_IDS     3            // posts kept of Following's top, in case one goes
#define RISING_LIKES 25           // with fewer, it is a chat among friends
#define RISING_AGE   (24 * 3600)  // older posts do not rise any more


/**
 * whether the numbers between commas have id
 **/
static int
seen_has(const char *list, const char *id) {
  size_t n = strlen(id), len;
  const char *end;

  for(const char *s=list; n && *s; s += len + (end ? 1 : 0)) {
    end = strchr(s, ',');
    len = end ? (size_t)(end - s) : strlen(s);
    if(len == n && !memcmp(s, id, n)) {
      return 1;
    }
  }
  return 0;
}


/**
 * Following, newest first: the first of the posts that were on top the last
 * time it was read (the account's, kept in the settings) gets "You left off
 * here" over it, and a toast says how many came since; the posts on top now
 * are the next time's
 **/
static void
mark_seen(tab_t *t) {
  char *seen, top[SETTINGS_SEEN] = "";
  int at = -1, kept = 0;
  size_t n;

  t->seen_at = 0;
  if(g_settings.account < 1 || g_settings.account > SETTINGS_ACCOUNTS) {
    return;
  }
  seen = g_settings.seen[g_settings.account - 1];
  for(int i=0; i < t->count && at < 0 && seen[0]; i++) {
    if(t->posts[i].kind == X_KIND_POST && seen_has(seen, t->posts[i].id)) {
      at = i;
    }
  }
  if(at == 1) {
    toast(0, "%s", L("1 post novo desde a \xc3\xbaltima vez", "1 new post since last time"));
  } else if(at > 1) {
    toast(0, L("%d posts novos desde a \xc3\xbaltima vez", "%d new posts since last time"), at);
  } else if(!at) {
    toast(0, "%s", L("Est\xc3\xa1s em dia: nada de novo desde a \xc3\xbaltima vez",
                     "You are all caught up: nothing new since last time"));
  } else if(seen[0] && t->count) {
    toast(0, L("Mais de %d posts novos desde a \xc3\xbaltima vez", "Over %d new posts since last time"),
          t->count);
  }
  t->seen_at = at > 0 ? at : 0;

  for(int i=0; i < t->count && kept < SEEN_IDS; i++) {
    const char *id = t->posts[i].id;
    n = strlen(top);
    if(t->posts[i].kind == X_KIND_POST && id[0] && strspn(id, "0123456789") == strlen(id) &&
       n + 1 + strlen(id) < sizeof top) {
      snprintf(top + n, sizeof top - n, "%s%s", kept ? "," : "", id);
      kept++;
    }
  }
  if(kept && strcmp(top, seen)) {
    snprintf(seen, sizeof g_settings.seen[0], "%s", top);
    g_settings_dirty = 1;
  }
}


/**
 * how fast p climbs: its likes, reposts (twice) and replies an hour since it
 * was posted (half an hour at least), or -1 without its counts
 **/
static float
climb(const x_post_t *p, long long now) {
  long long age = now - p->time;

  if(p->kind != X_KIND_POST || p->time <= 0 || p->likes < 0) {
    return -1;
  }
  age = age < 1800 ? 1800 : age;
  return (float)(p->likes + 2 * (p->reposts > 0 ? p->reposts : 0) + (p->replies > 0 ? p->replies : 0)) *
         3600 / (float)age;
}


/**
 * "Rising" by the names of the posts climbing fastest of the list's: of the
 * last day, with some likes, among its tenth that climbs fastest and three
 * times as fast as its middle one at least; x.com has nothing like it, the
 * counts it sends are enough
 **/
static void
mark_rising(tab_t *t, long long now) {
  float v[MAX_POSTS], x, middle, cut;
  int n = 0, j;

  for(int i=0; i < t->count; i++) {
    t->cards[i].rising = 0;
  }
  if(t->feed != UI_FOR_YOU && t->feed != UI_FOLLOWING && t->feed != UI_SEARCH && t->feed != UI_LIST &&
     t->feed != UI_USER && t->feed != UI_ME && t->feed != UI_BOOKMARKS && t->feed != UI_GAME) {
    return;
  }
  for(int i=0; i < t->count && n < MAX_POSTS; i++) {
    if((x=climb(&t->posts[i], now)) < 0) {
      continue;
    }
    // fastest first
    for(j=n++; j > 0 && v[j - 1] < x; j--) {
      v[j] = v[j - 1];
    }
    v[j] = x;
  }
  if(n < 5) {
    return; // too few to tell
  }
  middle = v[n / 2];
  cut = v[(n / 10 < 1 ? 1 : n / 10) - 1];
  for(int i=0; i < t->count; i++) {
    const x_post_t *p = &t->posts[i];
    x = climb(p, now);
    t->cards[i].rising = x > 0 && x >= cut && x >= 3 * middle && p->likes >= RISING_LIKES &&
                         now - p->time <= RISING_AGE;
  }
}


void
ui_result(const ui_request_t *req, x_post_t *posts, int count, const char *cursor,
          const x_user_t *user, int error, int code, int x_code, long long when) {
  tab_t *t = find_view(req->view);

  g_under_stale = 1;

  if(req->view == SIDE_VIEW && req->side == UI_SIDE_SCHEDULED) {
    mem_free(g_sched.items);
    g_sched.items = posts;
    g_sched.count = posts ? count : 0;
    g_sched.sel = g_sched.sel < g_sched.count ? g_sched.sel : 0;
    g_sched.loading = 0;
    g_sched.error = posts || error == ERR_EMPTY ? ERR_NONE : error;
    g_sched.code = code;
    return;
  }
  if(req->view == SIDE_VIEW && req->side == UI_SIDE_PLAYING) {
    // of the people the user follows, the ones playing; none is an answer too
    int n = 0;
    g_playing.loading = 0;
    g_playing.error = posts || error == ERR_EMPTY ? ERR_NONE : error;
    for(int i=0; posts && i < count && n < SIDE_ITEMS; i++) {
      if(posts[i].kind == X_KIND_USER && posts[i].playing[0]) {
        g_playing.items[n++] = posts[i];
      }
    }
    if(posts || error == ERR_EMPTY) {
      g_playing.count = n;
      g_playing.at = g_now;
    }
    mem_free(posts);
    return;
  }
  if(req->view == SIDE_VIEW) {
    side_box_t *b = req->side == UI_SIDE_WHO ? &g_who : &g_trends;
    b->loading = 0;
    b->error = posts ? ERR_NONE : error;
    if(posts) {
      side_keep(b, posts, count, req->side == UI_SIDE_WHO ? X_KIND_USER : X_KIND_TREND);
    }
    if(b == &g_trends && !b->count && !b->source) {
      // x.com's side column of trends did not come: Explore's, in a moment
      b->source = 1;
      b->at = g_now - SIDE_RETRY + 3;
    }
    mem_free(posts);
    return;
  }
  if(!t) {
    // the list was closed while it was read
    mem_free(posts);
    return;
  }
  if(user && req->feed == UI_ARTICLES) {
    // read for the user's number: who is signed in
    if(!g_tabs[UI_ME].has_user) {
      g_tabs[UI_ME].user = *user;
      g_tabs[UI_ME].has_user = 1;
      remember_user(user);
      layout_profile(&g_tabs[UI_ME]);
      place(&g_tabs[UI_ME]);
    }
  } else if(user) {
    t->user = *user;
    t->has_user = 1;
    if(t == &g_tabs[UI_ME]) {
      remember_user(user);
    }
    layout_profile(t);
    place(t);
  }
  if(t->loading == LOAD_MORE) {
    t->loading = LOAD_NONE;
    more_result(t, posts, count, cursor, error, code);
    return;
  }
  t->loading = LOAD_NONE;
  t->error = error;
  t->code = code;
  t->x_code = x_code;
  if(!posts) {
    return;
  }

  mem_free(t->cards);
  mem_free(t->posts);
  t->posts = posts;
  t->count = count;
  t->fetched_at = when;
  if(req->feed == UI_SEARCH && !req->query[0] && !req->latest && !req->cursor[0]) {
    side_keep(&g_trends, posts, count, X_KIND_TREND); // Explore's trends are the side column's too
  }
  t->sel = t->scroll = t->target = 0;
  t->more_error = ERR_NONE;
  set_cursor(t, cursor);
  if(!(t->cards=mem_calloc(count ? count : 1, sizeof *t->cards))) {
    mem_free(t->posts);
    t->posts = NULL;
    t->count = 0;
    t->cursor[0] = 0;
    t->error = ERR_MEMORY;
    place(t);
    return;
  }
  if(t->feed == UI_DM) {
    dm_mark(t, posts, count);
  }
  for(int i=0; i<count; i++) {
    t->cards[i].post = &posts[i];
    layout_card(&t->cards[i]);
  }
  t->seen_at = 0;
  if(t->feed == UI_FOLLOWING) {
    mark_seen(t);
  }
  mark_rising(t, when > 0 ? when : g_now);
  place(t);
  if(t->feed == UI_POST) {
    focus(t);
  }
  if(t->feed == UI_DM && count) {
    // the newest message, at the bottom, as a conversation opens
    t->sel = count - 1;
    show_selected(t);
    t->scroll = t->target;
  }
}


/**
 * who is signed in, once, when nothing else is read (x.com reads its
 * Viewer first): for the bar's account and the menu of the user's own posts
 **/
static int
want_me(ui_request_t *req) {
  tab_t *me = &g_tabs[UI_ME];

  if(me->has_user || me->loading || g_me_asked) {
    return 0;
  }
  g_me_asked = 1;
  memset(req, 0, sizeof *req);
  req->view = me->serial;
  req->feed = UI_ME;
  req->profile = 1;
  me->loading = LOAD_TOP;
  me->wanted = 0;
  return 1;
}


/**
 * the first of posts, count of them, of kind (X_KIND_TREND or X_KIND_USER),
 * for the box b of the side column
 **/
static void
side_keep(side_box_t *b, const x_post_t *posts, int count, int kind) {
  int n = 0;

  for(int i=0; i < count && n < SIDE_ITEMS; i++) {
    if(posts[i].kind == kind) {
      b->items[n++] = posts[i];
    }
  }
  if(n) {
    b->count = n;
    b->error = ERR_NONE;
    b->at = g_now;
  }
}


/**
 * the user's scheduled posts, once their sheet asks for them
 **/
static int
want_sched(ui_request_t *req) {
  if(!g_sched.wanted) {
    return 0;
  }
  memset(req, 0, sizeof *req);
  req->view = SIDE_VIEW;
  req->feed = UI_ME;
  req->side = UI_SIDE_SCHEDULED;
  g_sched.wanted = 0;
  g_sched.loading = 1;
  return 1;
}


/**
 * the side column's boxes, when they are old or never came; while the list
 * shown wants nothing
 **/
static int
want_side(ui_request_t *req) {
  side_box_t *boxes[2] = {&g_trends, &g_who};

  if(!g_now) {
    return 0;
  }
  for(int i=0; i<2; i++) {
    side_box_t *b = boxes[i];
    if(b->loading || (b->at && g_now - b->at < (b->count ? SIDE_EVERY : SIDE_RETRY))) {
      continue;
    }
    memset(req, 0, sizeof *req);
    req->view = SIDE_VIEW;
    req->feed = UI_SEARCH; // as Explore reads its trends, with no words
    req->side = b == &g_who ? UI_SIDE_WHO : b->source ? UI_SIDE_EXPLORE : UI_SIDE_TRENDS;
    b->loading = 1;
    b->at = g_now;
    return 1;
  }
  // who of the people the user follows plays now, read more often
  if(strcmp(g_playing_who, g_settings.ids[g_settings.account - 1])) {
    memset(&g_playing, 0, sizeof g_playing); // another account's
    snprintf(g_playing_who, sizeof g_playing_who, "%s", g_settings.ids[g_settings.account - 1]);
  }
  if(g_playing_who[0] && !g_playing.loading &&
     (!g_playing.at || g_now - g_playing.at >= (g_playing.error ? SIDE_RETRY : SIDE_PLAYING_EVERY))) {
    memset(req, 0, sizeof *req);
    req->view = SIDE_VIEW;
    req->feed = UI_FOLLOWING;
    req->side = UI_SIDE_PLAYING;
    snprintf(req->id, sizeof req->id, "%s", g_playing_who);
    g_playing.loading = 1;
    g_playing.at = g_now;
    return 1;
  }
  return 0;
}


int
ui_wanted(ui_request_t *req) {
  tab_t *t = view(), *me = &g_tabs[UI_ME];

  // the scheduled posts' sheet, over whatever list is shown
  if(want_sched(req)) {
    return 1;
  }
  if(t->loading) {
    return 0;
  }
  memset(req, 0, sizeof *req);
  // the pages that read nothing; History is the screen's own
  if(t->feed >= UI_CALLS && t->feed <= UI_HISTORY) {
    if(t->wanted && t->feed == UI_HISTORY) {
      fill_history();
    }
    t->wanted = 0;
    return want_me(req) || want_side(req);
  }
  // the game's tab, without a game to look for
  if(t->feed == UI_GAME && !t->query[0]) {
    if(t->wanted) {
      t->wanted = 0;
      t->error = ERR_EMPTY;
    }
    return want_me(req) || want_side(req);
  }
  // whose conversations Chat reads: the signed-in user's number first
  if(t->feed == UI_CHAT && t->wanted && !me->has_user) {
    if(me->loading) {
      return 0;
    }
    if(want_me(req)) {
      return 1;
    }
  }
  req->view = t->serial;
  req->feed = t->feed;
  snprintf(req->id, sizeof req->id, "%s", t->feed == UI_ME ? t->user.id :
           t->feed == UI_ARTICLES || t->feed == UI_CHAT ? (me->has_user ? me->user.id : "") : t->arg);
  snprintf(req->query, sizeof req->query, "%s", t->query);
  req->latest = t->latest;
  if(t->wanted) {
    t->wanted = 0;
    t->loading = LOAD_TOP;
    req->profile = t->feed == UI_ME || t->feed == UI_USER || (t->feed == UI_ARTICLES && !req->id[0]);
    return 1;
  }
  // near the end of the posts, or with it less than a screen below
  if(t->count && t->cursor[0] && !t->more_error && ((t->feed != UI_ME && t->feed != UI_ARTICLES) ||
                                                     req->id[0]) &&
     (t->sel >= t->count - MORE_AHEAD || t->target + 2 * VIEW_H >= t->content_h)) {
    snprintf(req->cursor, sizeof req->cursor, "%s", t->cursor);
    t->loading = LOAD_MORE;
    return 1;
  }
  return want_me(req) || want_side(req);
}


/*
 * The controller and the scrolling.
 */

static int
max_scroll(const tab_t *t) {
  return t->content_h > VIEW_H ? t->content_h - VIEW_H : 0;
}


static void
clamp_target(tab_t *t) {
  t->target = t->target > max_scroll(t) ? max_scroll(t) : t->target;
  t->target = t->target < 0 ? 0 : t->target;
}


/**
 * where post i ends in the list, with the footer under the last one
 **/
static int
bottom(const tab_t *t, int i) {
  return t->cards[i].y + t->cards[i].height + (i == t->count - 1 ? FOOTER_H : 0);
}


/**
 * scroll so the selected post shows whole, or its top when it is taller
 * than the screen
 **/
static void
show_selected(tab_t *t) {
  const card_t *c = &t->cards[t->sel];
  int end = bottom(t, t->sel);

  if(t->sel == 0 && t->header_h) {
    // the first post shows under the profile
    t->target = 0;
    clamp_target(t);
    return;
  }
  if(c->y < t->target || end - c->y > VIEW_H) {
    t->target = c->y;
  } else if(end > t->target + VIEW_H) {
    t->target = end - VIEW_H;
  }
  clamp_target(t);
}


static void
move(tab_t *t, unsigned keys) {
  const card_t *c;
  int y;

  if(!t->count) {
    return;
  }
  c = &t->cards[t->sel];
  if(keys & UI_DOWN) {
    if(bottom(t, t->sel) > t->target + VIEW_H) {
      // the rest of a long post first
      t->target += VIEW_H / 2;
      y = bottom(t, t->sel) - VIEW_H;
      t->target = t->target > y ? y : t->target;
      clamp_target(t);
    } else if(t->sel + 1 < t->count) {
      t->sel++;
      show_selected(t);
    } else if(t->more_error && t->cursor[0]) {
      // ↓ at the end tries the posts after it again
      t->more_error = ERR_NONE;
    }
  }
  if(keys & UI_UP) {
    if(c->y < t->target) {
      t->target -= VIEW_H / 2;
      t->target = t->target < c->y ? c->y : t->target;
      clamp_target(t);
    } else if(t->sel > 0) {
      t->sel--;
      c = &t->cards[t->sel];
      if(c->height > VIEW_H) {
        // a long post above shows its end first, the way back up
        t->target = c->y + c->height - VIEW_H;
        clamp_target(t);
      } else {
        show_selected(t);
      }
    } else {
      t->target = 0; // up to the profile over the first post
    }
  }
  if(keys & UI_R1) {
    y = c->y + VIEW_H;
    while(t->sel + 1 < t->count && t->cards[t->sel + 1].y <= y) {
      t->sel++;
    }
    t->target = t->cards[t->sel].y;
    clamp_target(t);
  }
  if(keys & UI_L1) {
    y = c->y - VIEW_H;
    while(t->sel > 0 && t->cards[t->sel - 1].y >= y) {
      t->sel--;
    }
    t->target = t->cards[t->sel].y;
    clamp_target(t);
  }
  if(keys & UI_TRIANGLE) {
    t->sel = 0;
    t->target = 0;
  }
}


/**
 * ✕ shows the whole text of a long post, and again shortens it
 **/
static int
toggle_open(tab_t *t) {
  card_t *c;

  if(!t->count) {
    return 0;
  }
  c = &t->cards[t->sel];
  if(!c->more && !c->open) {
    return 0;
  }
  c->open = !c->open;
  layout_card(c);
  place(t);
  // closing a post scrolled past its top goes back to it
  if(c->y < t->target) {
    t->target = c->y;
  }
  clamp_target(t);
  return 1;
}


static void
toast(int error, const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  vsnprintf(g_toast.text, sizeof g_toast.text, fmt, args);
  va_end(args);
  g_toast.error = error;
  g_toast.frames = TOAST_FRAMES;
}


/*
 * The lists: the sections of the bar, and the posts and profiles opened
 * from them, which ○ closes again.
 */

/**
 * every list there is in turn, from 0, then NULL
 **/
static tab_t *
list_at(int i) {
  return i < SECTIONS ? &g_tabs[i] : i < SECTIONS + g_depth ? &g_stack[i - SECTIONS] : NULL;
}


/**
 * the cards laid out again, of the post id or (NULL) all of them, in every
 * list; each keeps its selected post where it was on the screen
 **/
static void
relayout(const char *id) {
  tab_t *t;
  int shown, moving, changed;

  for(int i=0; (t=list_at(i)); i++) {
    if(!t->cards || !t->count) {
      continue;
    }
    changed = 0;
    shown = t->cards[t->sel].y - t->target;
    moving = t->scroll - t->target;
    for(int j=0; j < t->count; j++) {
      if(!id || !strcmp(t->posts[j].id, id)) {
        layout_card(&t->cards[j]);
        changed = 1;
      }
    }
    if(changed) {
      place(t);
      t->target = t->cards[t->sel].y - shown;
      clamp_target(t);
      t->scroll = t->target + moving;
    }
  }
}


/**
 * ✕ on a covered post: what it says, here and in every list
 **/
static int
uncover(tab_t *t) {
  char id[X_ID_MAX];

  snprintf(id, sizeof id, "%s", t->posts[t->sel].id);
  snprintf(g_revealed[g_revealed_n++ % REVEALED], sizeof g_revealed[0], "%s", id);
  relayout(id);
  return 1;
}


static void
free_list(tab_t *t) {
  mem_free(t->cards);
  mem_free(t->posts);
  memset(t, 0, sizeof *t);
}


/**
 * whether Home has the tab of the game: while one runs, and while it is
 * shown once it stopped
 **/
static int
game_tab_shown(void) {
  return g_tabs[UI_GAME].query[0] && g_game.tab[0] && (g_game.name[0] || g_section == UI_GAME);
}


/**
 * the timeline the house goes back to: the last of Home's tabs shown, the
 * game's while it is there
 **/
static int
home_section(void) {
  return g_home == UI_GAME && !game_tab_shown() ? UI_FOLLOWING : g_home;
}


static void
show_section(int section) {
  tab_t *t = &g_tabs[section];

  while(g_depth) {
    free_list(&g_stack[--g_depth]);
  }
  g_section = section;
  g_pick = section == UI_CHAT && g_pick;
  if(section == UI_FOR_YOU || section == UI_FOLLOWING || section == UI_GAME) {
    g_home = section;
  }
  if(section == UI_HISTORY) {
    fill_history();
    return;
  }
  if(!t->count && !t->loading && !t->error) {
    t->wanted = 1;
  }
}


/**
 * the search's posts go, to be read from the top: for other words, or
 * its other tab; what is still being read for the old ones is dropped
 **/
static void
reset_search(tab_t *t) {
  mem_free(t->cards);
  mem_free(t->posts);
  t->cards = NULL;
  t->posts = NULL;
  t->count = t->sel = t->scroll = t->target = 0;
  t->error = t->code = t->more_error = t->more_code = 0;
  t->x_code = -1;
  t->cursor[0] = 0;
  t->fetched_at = 0;
  t->loading = LOAD_NONE;
  t->serial = ++g_serial;
  t->wanted = 1; // the words, or Explore's trends without them
  place(t);
}


void
ui_search(const char *text) {
  tab_t *t = &g_tabs[UI_SEARCH];
  size_t n;

  memset(&g_act, 0, sizeof g_act);
  while(*text == ' ' || *text == '\n' || *text == '\t') {
    text++;
  }
  n = strlen(text);
  while(n && (text[n - 1] == ' ' || text[n - 1] == '\n' || text[n - 1] == '\t')) {
    n--;
  }
  if(n > X_QUERY_MAX) {
    // not in the middle of a character
    for(n=X_QUERY_MAX; n && ((unsigned char)text[n] & 0xC0) == 0x80; n--) {
    }
  }
  if(!n) {
    return;
  }
  memcpy(t->query, text, n);
  t->query[n] = 0;
  show_section(UI_SEARCH);
  reset_search(t);
}


/**
 * the search's tab of top posts, or with latest of the latest ones; the
 * one shown goes back to its top
 **/
static int
set_product(int latest) {
  tab_t *t = &g_tabs[UI_SEARCH];

  if(t->latest == latest) {
    t->sel = 0;
    t->target = 0;
    return 1;
  }
  t->latest = latest;
  reset_search(t);
  return 1;
}


/**
 * a new list over the others, to be read from the top; the oldest goes
 * when there are too many
 **/
static tab_t *
push(int feed, const char *arg) {
  tab_t *t;

  if(g_depth == STACK_MAX) {
    free_list(&g_stack[0]);
    memmove(&g_stack[0], &g_stack[1], (STACK_MAX - 1) * sizeof *g_stack);
    g_depth--;
  }
  t = &g_stack[g_depth++];
  memset(t, 0, sizeof *t);
  t->feed = feed;
  t->serial = ++g_serial;
  snprintf(t->arg, sizeof t->arg, "%s", arg);
  t->wanted = 1;
  return t;
}


/**
 * the post with its replies, showing the post at once while they load;
 * returns whether it opened
 **/
static int
open_post(const x_post_t *p) {
  x_post_t copy;
  tab_t *t;

  if(!p->id[0] || (p->kind != X_KIND_POST && p->kind != X_KIND_NOTICE)) {
    return 0;
  }
  copy = *p;
  t = push(UI_POST, copy.id);
  if(copy.kind != X_KIND_POST) {
    return 1;
  }
  history_add(&copy);
  copy.reposter[0] = copy.reposter_name[0] = 0;
  copy.pinned = 0;
  if((t->posts=mem_calloc(1, sizeof *t->posts)) && (t->cards=mem_calloc(1, sizeof *t->cards))) {
    t->posts[0] = copy;
    t->count = 1;
    t->cards[0].post = &t->posts[0];
    t->cards[0].open = 1;
    layout_card(&t->cards[0]);
    place(t);
  }
  return 1;
}


/**
 * the profile of p's author (of a notice, the first person in it), with
 * what the post knows of them until the rest comes
 **/
static int
open_user(const x_post_t *p) {
  tab_t *t;

  if(!p->user_id[0]) {
    toast(1, "%s", L("Este perfil n\xc3\xa3o se deixa abrir", "This profile cannot be opened"));
    return 1;
  }
  if(view()->feed == UI_USER && !strcmp(view()->arg, p->user_id)) {
    return 0; // it is the one shown
  }
  t = push(UI_USER, p->user_id);
  t->has_user = 1;
  snprintf(t->user.id, sizeof t->user.id, "%s", p->user_id);
  snprintf(t->user.user, sizeof t->user.user, "%s", p->user);
  snprintf(t->user.name, sizeof t->user.name, "%s", p->name);
  snprintf(t->user.avatar, sizeof t->user.avatar, "%s", p->avatar);
  t->user.badge = p->badge;
  t->user.following = t->user.followers = t->user.posts = -1;
  layout_profile(t);
  place(t);
  return 1;
}


/*
 * History: the posts opened (with ✕ or their pictures), newest first, for
 * as long as the app is open.
 */

#define HISTORY_MAX UI_HISTORY_MAX

static x_post_t *g_history; // mem_alloc'd, HISTORY_MAX of them
static int       g_history_count;
static int       g_history_changed; // since ui_history_get


int
ui_history_get(const x_post_t **posts, int *changed) {
  *posts = g_history;
  *changed = g_history_changed;
  g_history_changed = 0;
  return g_history ? g_history_count : 0;
}


void
ui_history_set(const x_post_t *posts, int count) {
  int n = 0;

  if(count <= 0 || (!g_history && !(g_history=mem_calloc(HISTORY_MAX, sizeof *g_history)))) {
    return;
  }
  for(int i=0; i < count && n < HISTORY_MAX; i++) {
    if(posts[i].kind == X_KIND_POST && posts[i].id[0]) {
      g_history[n++] = posts[i];
    }
  }
  g_history_count = n;
}


static void
history_add(const x_post_t *p) {
  int at;

  if(!p->id[0] || p->kind != X_KIND_POST ||
     (!g_history && !(g_history=mem_calloc(HISTORY_MAX, sizeof *g_history)))) {
    return;
  }
  // the same post again goes to the top; a new one pushes the oldest out
  at = g_history_count < HISTORY_MAX ? g_history_count : HISTORY_MAX - 1;
  for(int i=0; i < g_history_count; i++) {
    if(!strcmp(g_history[i].id, p->id)) {
      at = i;
      break;
    }
  }
  if(at == g_history_count) {
    g_history_count++;
  }
  memmove(&g_history[1], &g_history[0], (size_t)at * sizeof *g_history);
  g_history[0] = *p;
  g_history[0].reposter[0] = g_history[0].reposter_name[0] = 0;
  g_history[0].pinned = 0;
  g_history_changed = 1;
}


/**
 * the History section's list, from what was opened
 **/
static void
fill_history(void) {
  tab_t *t = &g_tabs[UI_HISTORY];
  x_post_t *posts = NULL;
  card_t *cards = NULL;
  int n = g_history_count;

  mem_free(t->cards);
  mem_free(t->posts);
  t->cards = NULL;
  t->posts = NULL;
  t->count = t->sel = t->scroll = t->target = 0;
  t->error = ERR_NONE;
  if(n && (posts=mem_calloc(n, sizeof *posts)) && (cards=mem_calloc(n, sizeof *cards))) {
    memcpy(posts, g_history, (size_t)n * sizeof *posts);
    t->posts = posts;
    t->cards = cards;
    t->count = n;
    for(int i=0; i < n; i++) {
      cards[i].post = &posts[i];
      layout_card(&cards[i]);
    }
  } else {
    mem_free(posts);
  }
  place(t);
}


/**
 * ○: the picture, or the list on top, closes; from a section, back to the
 * timeline
 **/
static int
back(void) {
  if(g_viewer.open) {
    g_viewer.open = 0;
    return 1;
  }
  if(g_pick && !g_depth) {
    // no conversation picked: back to the post
    g_pick = 0;
    g_sheet = SHEET_COMPOSE;
    return 1;
  }
  if(g_depth) {
    free_list(&g_stack[--g_depth]);
    return 1;
  }
  if(g_section == UI_SEARCH && g_tabs[UI_SEARCH].query[0]) {
    g_tabs[UI_SEARCH].query[0] = 0;
    g_tabs[UI_SEARCH].latest = 0;
    reset_search(&g_tabs[UI_SEARCH]);
    return 1;
  }
  if(g_section != UI_FOR_YOU && g_section != UI_FOLLOWING && g_section != UI_GAME) {
    show_section(home_section());
    return 1;
  }
  return 0;
}


/**
 * a trend of Explore: its top posts, as x.com searches for it
 **/
static int
open_trend(const x_post_t *p) {
  char query[sizeof p->text]; // ui_search cuts it to X_QUERY_MAX

  snprintf(query, sizeof query, "%s", p->text);
  g_tabs[UI_SEARCH].latest = 0;
  ui_search(query);
  return 1;
}


/**
 * a list: its posts, under its name
 **/
static int
open_list(const x_post_t *p) {
  x_post_t copy;
  tab_t *t;

  if(!p->id[0] || (view()->feed == UI_LIST && !strcmp(view()->arg, p->id))) {
    return 0;
  }
  copy = *p;
  t = push(UI_LIST, copy.id);
  snprintf(t->user.name, sizeof t->user.name, "%s", copy.name);
  snprintf(t->user.user, sizeof t->user.user, "%s", copy.user);
  return 1;
}


/**
 * a conversation of Chat, its messages to be read
 **/
static int
open_chat(const x_post_t *p) {
  x_post_t copy;
  tab_t *t;

  if(!p->id[0] || (view()->feed == UI_DM && !strcmp(view()->arg, p->id))) {
    return 0;
  }
  copy = *p;
  t = push(UI_DM, copy.id);
  // who it is with, for its title; a group has no screen name
  snprintf(t->user.id, sizeof t->user.id, "%s", copy.group ? "" : copy.user_id);
  snprintf(t->user.user, sizeof t->user.user, "%s", copy.group ? "" : copy.user);
  snprintf(t->user.name, sizeof t->user.name, "%s", copy.name);
  snprintf(t->user.avatar, sizeof t->user.avatar, "%s", copy.avatar);
  t->user.badge = copy.badge;
  return 1;
}


/**
 * the keyboard for a question to Grok
 **/
static int
grok_ask(void) {
  if(g_act.kind || g_grok.asking) {
    return 0;
  }
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = UI_ACT_GROK_TEXT;
  return 1;
}


/**
 * □ on Grok's page: the next question starts a new conversation
 **/
static int
grok_fresh(void) {
  if(g_grok.asking || !g_grok.question[0]) {
    return 0;
  }
  g_grok.question[0] = g_grok.answer[0] = 0;
  g_grok.error = 0;
  g_grok.fresh = 1;
  toast(0, "%s", L("Nova conversa com o Grok.", "A new conversation with Grok."));
  return 1;
}


/**
 * the keyboard for a message to the conversation shown, starting with what
 * was written to it and did not go
 **/
static int
dm_write(tab_t *t) {
  if(t->feed != UI_DM || g_act.kind) {
    return 0;
  }
  if(strcmp(g_dm.conv, t->arg)) {
    snprintf(g_dm.conv, sizeof g_dm.conv, "%s", t->arg);
    g_dm.text[0] = 0;
  }
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = UI_ACT_DM_TEXT;
  snprintf(g_act.id, sizeof g_act.id, "%s", t->arg);
  snprintf(g_act.user, sizeof g_act.user, "%s", t->user.user);
  return 1;
}


/**
 * an invite to play to the user with the number id (screen name user): to
 * the game that runs here, else to the one they play (theirs, or NULL), a
 * message of their one-to-one conversation
 **/
static int
invite(const char *id, const char *user, const char *theirs) {
  const char *me = g_settings.ids[g_settings.account - 1];
  const char *game = g_game.name[0] ? g_game.name : theirs;
  char conv[X_ID_MAX];

  if(g_act.kind) {
    toast(1, "%s", L("Espera que acabe o que est\xc3\xa1 a ser enviado", "Wait for what is being sent"));
    return 1;
  }
  if(!game || !game[0]) {
    toast(1, "%s", L("Abre um jogo primeiro: o convite \xc3\xa9 para o jogo que est\xc3\xa1 a correr.",
                     "Open a game first: the invite is for the game that runs."));
    return 1;
  }
  if(play_conversation(me, id, conv, sizeof conv) ||
     play_invite(game, g_game.name[0] && game_id_ok(g_game.id) ? g_game.id : NULL, L("bora jogar?", "let's play?"),
                 g_play_act.text, sizeof g_play_act.text)) {
    toast(1, "%s", L("Este convite n\xc3\xa3o se pode enviar.", "This invite cannot be sent."));
    return 1;
  }
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = UI_ACT_INVITE;
  snprintf(g_act.id, sizeof g_act.id, "%s", conv);
  snprintf(g_act.user, sizeof g_act.user, "%s", user);
  return 1;
}


/**
 * ✕ on an invite to play: its game, opened by the payload
 **/
static int
launch_invite(const x_post_t *p) {
  if(g_act.kind) {
    toast(1, "%s", L("Espera que acabe o que est\xc3\xa1 a ser enviado", "Wait for what is being sent"));
    return 1;
  }
  if(!play_invite_parse(p->text, g_play_act.game, sizeof g_play_act.game, g_play_act.id, sizeof g_play_act.id)) {
    return 0;
  }
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = UI_ACT_LAUNCH;
  snprintf(g_act.id, sizeof g_act.id, "%s", g_play_act.id[0] ? g_play_act.id : "-");
  return 1;
}


int
ui_invite_get(char *text, size_t size) {
  snprintf(text, size, "%s", g_play_act.text);
  return (int)strlen(text);
}


int
ui_launch_get(char *id, size_t isize, char *name, size_t nsize) {
  snprintf(id, isize, "%s", g_play_act.id);
  snprintf(name, nsize, "%s", g_play_act.game);
  return g_play_act.game[0] ? 0 : -1;
}


/**
 * ✕ on a post: its replies; on the post they belong to, its whole text; on
 * a notice without a post, the profile of who it is about; on a
 * conversation, its messages, and on a message the keyboard for the next
 **/
static int
open_selected(tab_t *t) {
  const x_post_t *p;

  if(!t->count) {
    return 0;
  }
  p = &t->posts[t->sel];
  if(t->cards[t->sel].spoiler) {
    return uncover(t);
  }
  if(p->kind == X_KIND_CHAT) {
    return g_pick ? share_to(p) : open_chat(p);
  }
  if(p->kind == X_KIND_DM) {
    return p->icon == X_DM_INVITE ? launch_invite(p) : dm_write(t);
  }
  if(p->kind == X_KIND_TREND) {
    return open_trend(p);
  }
  if(p->kind == X_KIND_LIST) {
    return open_list(p);
  }
  if(t->feed == UI_POST && !strcmp(p->id, t->arg)) {
    return toggle_open(t);
  }
  if(open_post(p)) {
    return 1;
  }
  if(p->kind == X_KIND_NOTICE) {
    return open_user(p);
  }
  return toggle_open(t);
}


/**
 * a post's pictures over the whole screen, from picture index
 **/
static int
open_viewer(const x_post_t *p, int index) {
  if(!p->media_count) {
    return 0;
  }
  history_add(p);
  g_viewer.open = 1;
  g_viewer.input_us = g_ui_us;
  g_viewer.count = p->media_count;
  g_viewer.index = index >= 0 && index < p->media_count ? index : 0;
  memcpy(g_viewer.media, p->media, sizeof g_viewer.media);
  return 1;
}


/*
 * What the buttons do to a post.
 */

/**
 * every copy of the post with that number, in every list, liked or not
 **/
static void
set_liked(const char *id, int liked) {
  x_post_t *p;
  tab_t *t;

  for(int i=0; (t=list_at(i)); i++) {
    for(int k=0; k < t->count; k++) {
      p = &t->posts[k];
      if(!strcmp(p->id, id) && p->liked != liked) {
        p->liked = liked;
        p->likes += p->likes < 0 ? 0 : liked ? 1 : -1;
      }
    }
  }
}


static void
set_bookmarked(const char *id, int bookmarked) {
  tab_t *t;

  for(int i=0; (t=list_at(i)); i++) {
    for(int k=0; k < t->count; k++) {
      x_post_t *p = &t->posts[k];
      if(!strcmp(p->id, id) && p->bookmarked != bookmarked) {
        p->bookmarked = bookmarked;
        p->bookmarks += p->bookmarks < 0 ? 0 : bookmarked ? 1 : -1;
      }
    }
  }
}


static void
set_reposted(const char *id, int reposted) {
  x_post_t *p;
  tab_t *t;

  for(int i=0; (t=list_at(i)); i++) {
    for(int k=0; k < t->count; k++) {
      p = &t->posts[k];
      if(!strcmp(p->id, id) && p->reposted != reposted) {
        p->reposted = reposted;
        p->reposts += p->reposts < 0 ? 0 : reposted ? 1 : -1;
      }
    }
  }
}


/**
 * R2 likes the selected post or takes the like back, the pointer on its
 * arrows reposts it and on its bookmark keeps it, at once on the screen and
 * then on X; L2 asks for the keyboard to reply to it
 **/
static int
act(tab_t *t, int kind) {
  x_post_t *p;

  if(t->feed == UI_DM) {
    // L2 writes to the conversation; R2 does nothing to a message
    return kind == UI_ACT_REPLY ? dm_write(t) : 0;
  }
  if(!t->count || g_act.kind) {
    return 0;
  }
  p = &t->posts[t->sel];
  if(p->kind == X_KIND_CHAT) {
    return kind == UI_ACT_REPLY ? open_chat(p) : 0;
  }
  if(p->kind == X_KIND_TREND || p->kind == X_KIND_LIST) {
    return 0; // nothing to like or reply to
  }
  if(!p->id[0]) {
    toast(1, "%s", L("Este post n\xc3\xa3o se deixa responder nem gostar",
                     "This post cannot be liked or replied to"));
    return 1;
  }
  if(kind == UI_ACT_REPLY) {
    return open_composer(p);
  }
  if(kind == UI_ACT_LIKE) {
    kind = p->liked ? UI_ACT_UNLIKE : UI_ACT_LIKE;
    set_liked(p->id, !p->liked);
  } else if(kind == UI_ACT_REPOST) {
    kind = p->reposted ? UI_ACT_UNREPOST : UI_ACT_REPOST;
    set_reposted(p->id, !p->reposted);
  } else if(kind == UI_ACT_BOOKMARK) {
    kind = p->bookmarked ? UI_ACT_UNBOOKMARK : UI_ACT_BOOKMARK;
    set_bookmarked(p->id, !p->bookmarked);
  }
  g_act.kind = kind;
  g_act.taken = 0;
  snprintf(g_act.id, sizeof g_act.id, "%s", p->id);
  // a notice's post is the one it quotes
  snprintf(g_act.user, sizeof g_act.user, "%s", p->kind == X_KIND_NOTICE ? p->quote_user : p->user);
  return 1;
}


/**
 * every post of the user with the number id, and their profile, followed
 * or not
 **/
static void
set_following(const char *id, int following) {
  tab_t *t;

  for(int i=0; (t=list_at(i)); i++) {
    for(int k=0; k < t->count; k++) {
      if(!strcmp(t->posts[k].user_id, id)) {
        t->posts[k].following = following;
      }
    }
    if(t->has_user && !strcmp(t->user.id, id)) {
      t->user.followed = following;
    }
  }
  for(int k=0; k < g_who.count; k++) {
    if(!strcmp(g_who.items[k].user_id, id)) {
      g_who.items[k].following = following;
    }
  }
}


/**
 * the profiles of the user with the number id, muted and blocked or not
 * (-1 leaves it as it is)
 **/
static void
set_relation(const char *id, int muted, int blocked) {
  tab_t *t;

  for(int i=0; (t=list_at(i)); i++) {
    if(t->has_user && !strcmp(t->user.id, id)) {
      t->user.muted = muted >= 0 ? muted : t->user.muted;
      t->user.blocked = blocked >= 0 ? blocked : t->user.blocked;
    }
  }
}


/**
 * the posts of a user muted or blocked leave the lists, as on x.com, but
 * for their own profile
 **/
static void
remove_user_posts(const char *id) {
  tab_t *t;
  int kept, before, above;

  for(int i=0; (t=list_at(i)); i++) {
    if((t->feed == UI_USER || t->feed == UI_ME) && t->has_user && !strcmp(t->user.id, id)) {
      continue;
    }
    kept = before = above = 0;
    for(int k=0; k < t->count; k++) {
      if(t->posts[k].kind == X_KIND_POST && !strcmp(t->posts[k].user_id, id)) {
        before += k < t->sel;
        above += k < t->seen_at;
        continue;
      }
      if(kept != k) {
        t->posts[kept] = t->posts[k];
        t->cards[kept] = t->cards[k];
      }
      kept++;
    }
    if(kept == t->count) {
      continue;
    }
    t->count = kept;
    for(int k=0; k < kept; k++) {
      t->cards[k].post = &t->posts[k];
    }
    t->sel -= before;
    t->sel = t->sel >= kept ? kept - 1 : t->sel;
    t->sel = t->sel < 0 ? 0 : t->sel;
    t->seen_at -= above;
    t->seen_at = t->seen_at > 0 && t->seen_at < kept ? t->seen_at : 0;
    t->error = kept ? t->error : ERR_EMPTY;
    place(t);
    clamp_target(t);
  }
}


/**
 * the poll of every copy of the post id: the choice voted (1 to 4), or
 * with choice 0 the vote taken back, when X did not take it
 **/
static void
set_voted(const char *id, int choice) {
  x_post_t *p;
  tab_t *t;

  for(int i=0; (t=list_at(i)); i++) {
    for(int k=0; k < t->count; k++) {
      p = &t->posts[k];
      if(!p->poll_count || strcmp(p->id, id)) {
        continue;
      }
      if(choice && !p->poll_voted && choice <= p->poll_count) {
        p->poll_voted = choice;
        p->poll_votes[choice - 1]++;
      } else if(!choice && p->poll_voted) {
        p->poll_votes[p->poll_voted - 1] -= p->poll_votes[p->poll_voted - 1] > 0;
        p->poll_voted = 0;
      }
    }
  }
}


/**
 * the pointer on a choice (1 to 4) of the selected post's poll: the vote,
 * on the screen at once, then on X
 **/
static int
vote(tab_t *t, int choice) {
  const x_post_t *p = &t->posts[t->sel];
  char id[sizeof p->id];

  if(choice < 1 || choice > p->poll_count || p->poll_voted || !p->poll_card[0] || own_post(p)) {
    return 0;
  }
  if(!poll_open(p, g_now)) {
    toast(1, "%s", L("Esta sondagem j\xc3\xa1 fechou.", "This poll has closed."));
    return 1;
  }
  if(g_act.kind) {
    toast(1, "%s", L("Espera que acabe o que est\xc3\xa1 a ser enviado", "Wait for what is being sent"));
    return 1;
  }
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = UI_ACT_VOTE;
  snprintf(g_act.id, sizeof g_act.id, "%s", p->id);
  snprintf(g_act.user, sizeof g_act.user, "%s", p->user);
  snprintf(g_act.card, sizeof g_act.card, "%s", p->poll_card);
  snprintf(g_act.card_name, sizeof g_act.card_name, "%s", p->poll_name);
  g_act.choice = choice;
  snprintf(id, sizeof id, "%s", p->id);
  set_voted(id, choice);
  return 1;
}


/**
 * kind (UI_ACT_FOLLOW to UI_ACT_UNBLOCK) to the user with the number id,
 * or pinning the post id: on the screen at once, then on X
 **/
static int
act_on(int kind, const char *id, const char *user) {
  if(!id[0]) {
    return 0;
  }
  if(g_act.kind) {
    toast(1, "%s", L("Espera que acabe o que est\xc3\xa1 a ser enviado", "Wait for what is being sent"));
    return 1;
  }
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = kind;
  snprintf(g_act.id, sizeof g_act.id, "%s", id);
  snprintf(g_act.user, sizeof g_act.user, "%s", user);
  if(kind == UI_ACT_FOLLOW || kind == UI_ACT_UNFOLLOW) {
    set_following(id, kind == UI_ACT_FOLLOW);
  } else if(kind == UI_ACT_MUTE || kind == UI_ACT_UNMUTE) {
    set_relation(id, kind == UI_ACT_MUTE, -1);
  } else if(kind == UI_ACT_BLOCK || kind == UI_ACT_UNBLOCK) {
    set_relation(id, -1, kind == UI_ACT_BLOCK);
  }
  return 1;
}


/**
 * a profile's Follow button: follows, or stops following
 **/
static int
follow_toggle(tab_t *t) {
  if(!t->has_user || !t->user.id[0]) {
    return 0;
  }
  if(t->user.blocked) {
    return act_on(UI_ACT_UNBLOCK, t->user.id, t->user.user);
  }
  return act_on(t->user.followed ? UI_ACT_UNFOLLOW : UI_ACT_FOLLOW, t->user.id, t->user.user);
}


/**
 * the bar's pen, or the options button: the composer for a post of the
 * user's own
 **/
static int
compose(void) {
  return open_composer(NULL);
}


/**
 * the keyboard for words to look for on X
 **/
static int
ask_search(void) {
  if(g_act.kind) {
    return 0;
  }
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = UI_ACT_SEARCH;
  return 1;
}


int
ui_action(char *id, size_t isize, char *user, size_t usize) {
  if(!g_act.kind || g_act.taken) {
    return UI_ACT_NONE;
  }
  g_act.taken = 1;
  snprintf(id, isize, "%s", g_act.id);
  snprintf(user, usize, "%s", g_act.user);
  return g_act.kind;
}


/**
 * why an action failed, in a few words
 **/
static void
action_error(int error, int code, int x_code, char *buf, size_t size) {
  if(error == ERR_HTTP && x_code > 0) {
    snprintf(buf, size, L("erro %d do X", "X error %d"), x_code);
  } else if(error == ERR_HTTP) {
    snprintf(buf, size, "HTTP %d", code);
  } else if(error == ERR_NETWORK) {
    snprintf(buf, size, "%s", L("sem liga\xc3\xa7\xc3\xa3o", "no connection"));
  } else if(error == ERR_NO_SESSION || error == ERR_NO_COOKIE) {
    snprintf(buf, size, "%s", L("falta a sess\xc3\xa3o", "no session"));
  } else if(error == ERR_ANSWER) {
    snprintf(buf, size, "%s", L("resposta inesperada do X", "an unexpected answer from X"));
  } else if(error == ERR_CAPTURE) {
    snprintf(buf, size, L("a captura n\xc3\xa3o se deixou ler, %d", "a capture could not be read, %d"), code);
  } else if(error == ERR_UPLOAD && x_code > 0) {
    snprintf(buf, size, L("o X recusou o ficheiro, erro %d", "X refused the file, error %d"), x_code);
  } else if(error == ERR_UPLOAD) {
    snprintf(buf, size, L("o X recusou o ficheiro, HTTP %d", "X refused the file, HTTP %d"), code);
  } else if(error == ERR_PROCESSING) {
    snprintf(buf, size, "%s", L("o X n\xc3\xa3o conseguiu processar o v\xc3\xad" "deo",
                                "X could not process the video"));
  } else if(error == ERR_TOO_BIG) {
    snprintf(buf, size, "%s", L("ficheiro grande demais", "the file is too big"));
  } else if(error == ERR_LAUNCH) {
    snprintf(buf, size, "0x%08X", (unsigned)code);
  } else {
    snprintf(buf, size, L("erro %d", "error %d"), error);
  }
}


void
ui_action_done(int error, int code, int x_code) {
  int kind = g_act.kind, mark = kind == UI_ACT_BOOKMARK || kind == UI_ACT_UNBOOKMARK;
  const char *fmt;
  char why[64];
  x_post_t *p;
  tab_t *t;

  g_under_stale = 1;

  if(kind == UI_ACT_REPLY && !error) {
    for(int i=0; (t=list_at(i)); i++) {
      for(int k=0; k < t->count; k++) {
        p = &t->posts[k];
        p->replies += strcmp(p->id, g_act.id) || p->replies < 0 ? 0 : 1;
      }
    }
  } else if((kind == UI_ACT_LIKE || kind == UI_ACT_UNLIKE) && error) {
    // the like did not happen: as it was before
    set_liked(g_act.id, kind == UI_ACT_UNLIKE);
  } else if((kind == UI_ACT_REPOST || kind == UI_ACT_UNREPOST) && error) {
    set_reposted(g_act.id, kind == UI_ACT_UNREPOST);
  } else if(mark && error) {
    set_bookmarked(g_act.id, kind == UI_ACT_UNBOOKMARK);
  } else if(mark && g_section != UI_BOOKMARKS) {
    g_tabs[UI_BOOKMARKS].wanted = 1; // read again when they are shown
  } else if(kind == UI_ACT_POST && !error && (g_tabs[UI_ME].count || g_tabs[UI_ME].has_user)) {
    g_tabs[UI_ME].wanted = 1; // the profile shows it the next time
  } else if(kind == UI_ACT_DELETE && !error) {
    remove_post(g_act.id);
  } else if(kind == UI_ACT_UNSCHEDULE && !error) {
    for(int i=0; i < g_sched.count; i++) {
      if(!strcmp(g_sched.items[i].id, g_act.id)) {
        memmove(&g_sched.items[i], &g_sched.items[i + 1], (size_t)(g_sched.count - i - 1) * sizeof *g_sched.items);
        g_sched.count--;
        break;
      }
    }
    g_sched.sel = g_sched.sel >= g_sched.count && g_sched.count ? g_sched.count - 1 : g_sched.sel;
  }

  action_error(error, code, x_code, why, sizeof why);
  if(error == ERR_KEYBOARD) {
    toast(1, L("O teclado da PS5 n\xc3\xa3o abriu (0x%08X)", "The PS5 keyboard did not open (0x%08X)"),
          (unsigned)code);
  } else if(kind == UI_ACT_SCHEDULE) {
    char when[64];
    compose_when(when, sizeof when);
    compose_done(error);
    if(!error) {
      toast(0, L("O teu post foi agendado (%s). V\xc3\xaa-o nos posts por enviar do X.",
                 "Your post was scheduled (%s). It is in X's unsent posts."), when);
    } else if(error != ERR_CANCELLED) {
      toast(1, L("O post n\xc3\xa3o foi agendado (%s)", "The post was not scheduled (%s)"), why);
    }
  } else if(kind == UI_ACT_REPLY || kind == UI_ACT_POST) {
    compose_done(error);
    if(!error) {
      toast(0, "%s", kind == UI_ACT_REPLY ? L("A tua resposta foi enviada.", "Your reply was sent.") :
            L("O teu post foi publicado.", "Your post was published."));
    } else if(error != ERR_CANCELLED) {
      toast(1, kind == UI_ACT_REPLY ?
            L("A resposta n\xc3\xa3o foi enviada (%s)", "The reply was not sent (%s)") :
            L("O post n\xc3\xa3o foi publicado (%s)", "The post was not published (%s)"), why);
    }
  } else if(kind == UI_ACT_UNSCHEDULE) {
    if(!error) {
      toast(0, "%s", L("O post agendado foi apagado.", "The scheduled post was deleted."));
    } else if(error != ERR_CANCELLED) {
      toast(1, L("O post agendado n\xc3\xa3o foi apagado (%s)", "The scheduled post was not deleted (%s)"), why);
    }
  } else if(kind == UI_ACT_DELETE) {
    if(!error) {
      toast(0, "%s", L("O teu post foi apagado.", "Your post was deleted."));
    } else if(error != ERR_CANCELLED) {
      toast(1, L("O post n\xc3\xa3o foi apagado (%s)", "The post was not deleted (%s)"), why);
    }
  } else if(kind == UI_ACT_QUOTE || kind == UI_ACT_EDIT) {
    compose_done(error);
    if(!error) {
      toast(0, "%s", kind == UI_ACT_EDIT ? L("O teu post foi editado.", "Your post was edited.") :
            L("O teu post foi publicado.", "Your post was published."));
      // the list shows the post as it is now
      view()->wanted = kind == UI_ACT_EDIT;
      g_tabs[UI_ME].wanted = g_tabs[UI_ME].count || g_tabs[UI_ME].has_user;
    } else if(error != ERR_CANCELLED) {
      toast(1, kind == UI_ACT_EDIT ? L("O post n\xc3\xa3o foi editado (%s)", "The post was not edited (%s)") :
            L("O post n\xc3\xa3o foi publicado (%s)", "The post was not published (%s)"), why);
    }
  } else if(kind == UI_ACT_PIN || kind == UI_ACT_UNPIN) {
    if(!error) {
      toast(0, "%s", kind == UI_ACT_PIN ? L("O post foi fixado no teu perfil.", "Your post was pinned to your profile.") :
            L("O post foi desafixado do teu perfil.", "Your post was unpinned from your profile."));
      g_tabs[UI_ME].wanted = 1;
    } else {
      toast(1, kind == UI_ACT_PIN ? L("O post n\xc3\xa3o foi fixado (%s)", "The post was not pinned (%s)") :
            L("O post n\xc3\xa3o foi desafixado (%s)", "The post was not unpinned (%s)"), why);
    }
  } else if(kind >= UI_ACT_FOLLOW && kind <= UI_ACT_UNBLOCK) {
    if(error) {
      // as it was before
      if(kind == UI_ACT_FOLLOW || kind == UI_ACT_UNFOLLOW) {
        set_following(g_act.id, kind == UI_ACT_UNFOLLOW);
      } else if(kind == UI_ACT_MUTE || kind == UI_ACT_UNMUTE) {
        set_relation(g_act.id, kind == UI_ACT_UNMUTE, -1);
      } else {
        set_relation(g_act.id, -1, kind == UI_ACT_UNBLOCK);
      }
      toast(1, L("N\xc3\xa3o deu (%s)", "It did not go through (%s)"), why);
    } else {
      switch(kind) {
      case UI_ACT_FOLLOW:
        toast(0, L("Est\xc3\xa1s a seguir @%s.", "You are following @%s."), g_act.user);
        g_tabs[UI_FOLLOWING].wanted = 1;
        break;
      case UI_ACT_UNFOLLOW:
        toast(0, L("Deixaste de seguir @%s.", "You unfollowed @%s."), g_act.user);
        break;
      case UI_ACT_MUTE:
        toast(0, L("Silenciaste @%s.", "You muted @%s."), g_act.user);
        remove_user_posts(g_act.id);
        break;
      case UI_ACT_UNMUTE:
        toast(0, L("Deixaste de silenciar @%s.", "You unmuted @%s."), g_act.user);
        break;
      case UI_ACT_BLOCK:
        toast(0, L("Bloqueaste @%s.", "You blocked @%s."), g_act.user);
        set_following(g_act.id, 0);
        remove_user_posts(g_act.id);
        break;
      default:
        toast(0, L("Desbloqueaste @%s.", "You unblocked @%s."), g_act.user);
        break;
      }
    }
  } else if(kind == UI_ACT_VOTE) {
    if(error) {
      set_voted(g_act.id, 0); // as it was
      if(error != ERR_CANCELLED) {
        toast(1, L("O voto n\xc3\xa3o foi contado (%s)", "Your vote was not counted (%s)"), why);
      }
    }
  } else if(kind == UI_ACT_GROK) {
    g_grok.asking = 0;
    g_grok.error = error;
    g_grok.code = code;
    g_grok.x_code = x_code;
    g_grok.fresh |= error != ERR_NONE; // after a failure, a new conversation
  } else if(kind == UI_ACT_GROK_TEXT) {
    // the keyboard closed without a question
  } else if(kind == UI_ACT_POLL_TEXT) {
    // the keyboard closed: the choice stays as it was
  } else if(kind == UI_ACT_DM) {
    if(!error) {
      g_dm.text[0] = 0;
      toast(0, "%s", L("Mensagem enviada.", "Message sent."));
      // the conversation and Chat read again, with it
      for(int i=0; (t=list_at(i)); i++) {
        if(t->feed == UI_DM && !strcmp(t->arg, g_act.id)) {
          t->wanted = 1;
        }
      }
      g_tabs[UI_CHAT].wanted = 1;
    } else if(error != ERR_CANCELLED) {
      toast(1, L("A mensagem n\xc3\xa3o foi enviada (%s)", "The message was not sent (%s)"), why);
    }
  } else if(kind == UI_ACT_INVITE) {
    if(!error) {
      toast(0, L("Convite enviado a @%s.", "Invite sent to @%s."), g_act.user);
      g_tabs[UI_CHAT].wanted = 1;
    } else if(x_code == 349 || x_code == 150) {
      // X: the user does not take messages from the user, or only from who they follow
      toast(1, L("@%s n\xc3\xa3o recebe mensagens tuas.", "@%s does not take your messages."), g_act.user);
    } else if(error != ERR_CANCELLED) {
      toast(1, L("O convite n\xc3\xa3o foi enviado (%s)", "The invite was not sent (%s)"), why);
    }
  } else if(kind == UI_ACT_LAUNCH) {
    if(!error) {
      toast(0, L("A abrir %s\xe2\x80\xa6", "Opening %s\xe2\x80\xa6"), g_play_act.game);
    } else if(error == ERR_NO_GAME) {
      toast(1, L("N\xc3\xa3o tens %s instalado.", "You do not have %s installed."), g_play_act.game);
    } else if(error == ERR_GAME_OPEN) {
      toast(1, "%s", L("Fecha primeiro o jogo que est\xc3\xa1 aberto.", "Close the game that is open first."));
    } else {
      toast(1, L("O jogo n\xc3\xa3o abriu (%s)", "The game did not open (%s)"), why);
    }
  } else if(kind == UI_ACT_SHARE_DM) {
    compose_done(error);
    if(!error) {
      toast(0, L("Enviado para %s.", "Sent to %s."), g_act.user);
      g_tabs[UI_CHAT].wanted = 1;
    } else if(error != ERR_CANCELLED) {
      toast(1, L("A mensagem n\xc3\xa3o foi enviada (%s)", "The message was not sent (%s)"), why);
      g_sheet = SHEET_COMPOSE; // the draft, back where it was
    }
  } else if(mark && !error) {
    toast(0, "%s", kind == UI_ACT_BOOKMARK ?
          L("Adicionado aos itens guardados.", "Added to your Bookmarks.") :
          L("Removido dos itens guardados.", "Removed from your Bookmarks."));
  } else if(error && error != ERR_CANCELLED && kind != UI_ACT_SEARCH && kind != UI_ACT_COMPOSE_TEXT &&
            kind != UI_ACT_CAPTURES && kind != UI_ACT_DM_TEXT) {
    switch(kind) {
    case UI_ACT_LIKE:
      fmt = L("O gosto n\xc3\xa3o ficou (%s)", "The like did not go through (%s)");
      break;
    case UI_ACT_UNLIKE:
      fmt = L("O gosto n\xc3\xa3o saiu (%s)", "The like was not taken back (%s)");
      break;
    case UI_ACT_REPOST:
      fmt = L("O repost n\xc3\xa3o ficou (%s)", "The repost did not go through (%s)");
      break;
    case UI_ACT_BOOKMARK:
      fmt = L("O post n\xc3\xa3o foi guardado (%s)", "The post was not bookmarked (%s)");
      break;
    case UI_ACT_UNBOOKMARK:
      fmt = L("O post n\xc3\xa3o saiu dos itens guardados (%s)", "The bookmark was not removed (%s)");
      break;
    default:
      fmt = L("O repost n\xc3\xa3o saiu (%s)", "The repost was not taken back (%s)");
      break;
    }
    toast(1, fmt, why);
  }
  memset(&g_act, 0, sizeof g_act);
}


/*
 * The pointer: where it can press, what the bar's buttons do, and the
 * sticks.
 */

/**
 * a place the pointer can press, at x, y of what is being drawn; returns
 * whether the pointer is on it
 **/
static int
zone(int kind, int arg, int sub, int x, int y, int w, int h) {
  int top = y + g_off_y, end = top + h;

  top = top < g_clip_top ? g_clip_top : top;
  end = end > g_clip_bottom ? g_clip_bottom : end;
  if(end <= top || w <= 0) {
    return 0;
  }
  if(g_zone_count < ZONES) {
    g_zones[g_zone_count++] = (zone_t){kind, arg, sub, {x, top, w, end - top}};
  }
  return g_ptr.shown && g_ptr.x >= x && g_ptr.x < x + w && g_ptr.y >= top && g_ptr.y < end;
}


/**
 * what the pointer would press at x, y: the last one drawn there
 **/
static const zone_t *
zone_at(float x, float y) {
  for(int i=g_zone_count - 1; i >= 0; i--) {
    const rect_t *r = &g_zones[i].r;
    if(x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
      return &g_zones[i];
    }
  }
  return NULL;
}


/**
 * after a frame: whether the pointer moved onto something that lit up. The
 * screen moving under a pointer that stays still makes no tick, nor does
 * going from a post's button back onto its card, which stayed lit
 **/
static void
hover_check(void) {
  const zone_t *z = g_ptr.shown ? zone_at(g_ptr.x, g_ptr.y) : NULL;
  const int moved = g_ptr.x != g_lit.x || g_ptr.y != g_lit.y;
  int kind = -1, arg = 0, sub = 0, card = -1;

  // the backgrounds: around a sheet, the sheet itself, the viewer and its
  // picture
  if(z && z->kind != Z_SHEET && z->kind != Z_PANE && z->kind != Z_VIEWER && z->kind != Z_V_PLAY &&
     z->kind != Z_LG_PAGE) {
    kind = z->kind;
    arg = z->arg;
    sub = z->sub;
    card = kind >= Z_CARD ? arg : -1;
  }
  if((kind != g_lit.kind || arg != g_lit.arg || sub != g_lit.sub) && kind >= 0 && moved &&
     !(kind == Z_CARD && g_lit.card == arg) && g_settings.hover) {
    g_lit.pending = 1;
    g_lit.pan = (g_ptr.x / UI_WIDTH - 0.5f) * 0.7f;
  }
  g_lit.kind = kind;
  g_lit.arg = arg;
  g_lit.sub = sub;
  g_lit.card = card;
  g_lit.x = g_ptr.x;
  g_lit.y = g_ptr.y;
}


int
ui_hover_take(float *pan) {
  const int pending = g_lit.pending;

  g_lit.pending = 0;
  *pan = g_lit.pan;
  return pending;
}


/**
 * what the pointer is on, lit the same way everywhere: lighter, with a
 * bright rim; with fill 0, a button of a colour of its own keeps it and
 * gets the rim around it, a little out
 **/
static void
lit_box(int x, int y, int w, int h, float r, int fill) {
  if(fill) {
    draw_box(&g_dst, x, y, w, h, r, 0, HOVER);
    draw_box(&g_dst, x, y, w, h, r, 2, HOVER_RIM);
  } else {
    draw_box(&g_dst, x - 5, y - 5, w + 10, h + 10, r + 5, 2.5f, HOVER_RIM);
  }
}


static void
lit_circle(float cx, float cy, float r, int fill) {
  if(fill) {
    draw_circle(&g_dst, cx, cy, r, HOVER);
    draw_arc(&g_dst, cx, cy, r - 1, 2, 0, 6.2832f, HOVER_RIM);
  } else {
    draw_arc(&g_dst, cx, cy, r + 4, 2.5f, 0, 6.2832f, HOVER_RIM);
  }
}


// the sections of the bar's items, in x.com's order (Home is the timeline
// last shown); -2 is More, whose menu has the bookmarks and the settings
static const int g_nav_sections[NAV_ITEMS] = {
  UI_FOLLOWING, UI_SEARCH, UI_NOTIFICATIONS, UI_CHAT, UI_CALLS, UI_GROK, UI_PREMIUM, UI_HISTORY,
  UI_ARTICLES, UI_ME, -2,
};


/**
 * the bar's item of the section shown
 **/
static int
nav_item(void) {
  if(g_sheet == SHEET_SETTINGS || g_section == UI_BOOKMARKS || g_section == UI_LISTS) {
    return NAV_ITEMS - 1;
  }
  for(int i=1; i < NAV_ITEMS - 1; i++) {
    if(g_nav_sections[i] == g_section) {
      return i;
    }
  }
  return 0;
}


/**
 * the section before or after the one shown, in the bar's order (the
 * timeline's two tabs first, the bookmarks of More last), or -1
 **/
static int
section_step(int d) {
  static const int order[] = {
    UI_FOR_YOU, UI_FOLLOWING, UI_GAME, UI_SEARCH, UI_NOTIFICATIONS, UI_CHAT, UI_CALLS, UI_GROK, UI_PREMIUM,
    UI_HISTORY, UI_ARTICLES, UI_ME, UI_BOOKMARKS, UI_LISTS,
  };
  const int n = sizeof order / sizeof order[0];

  for(int i=0; i < n; i++) {
    if(order[i] == g_section) {
      i += d;
      if(i >= 0 && i < n && order[i] == UI_GAME && !game_tab_shown()) {
        i += d; // no game, no tab of it
      }
      return i >= 0 && i < n ? order[i] : -1;
    }
  }
  return -1;
}


/**
 * a button of the bar: its section (the house, the timeline last shown),
 * or the top of it when it is the one shown; the pen writes a post, and
 * the glass opens the keyboard when there is nothing searched yet or the
 * search is at its top
 **/
static int
nav(int item) {
  int section = g_nav_sections[item];
  tab_t *t;

  if(section == -2) {
    return open_more();
  }
  if(section < 0) {
    return compose();
  }
  if(section == UI_FOLLOWING) {
    section = home_section();
  }
  if(!g_depth && section == g_section) {
    t = view();
    if(section == UI_SEARCH && !t->sel && !t->target) {
      return ask_search();
    }
    t->sel = 0;
    t->target = 0;
    return 1;
  }
  show_section(section);
  return 1;
}


/**
 * ✕ with the pointer shown: what is under it
 **/
static int
click(void) {
  const zone_t *z = zone_at(g_ptr.x, g_ptr.y);
  tab_t *t = view();

  if(!z) {
    return 0;
  }
  if(z->kind >= Z_SHEET && z->kind < Z_CARD) {
    return sheet_click(z);
  }
  if(z->kind >= Z_CARD) {
    if(z->arg < 0 || z->arg >= t->count) {
      return 0;
    }
    t->sel = z->arg;
  }
  switch(z->kind) {
  case Z_NAV:
    return nav(z->arg);
  case Z_NAV_POST:
    return compose();
  case Z_NAV_ME:
    return open_accounts();
  case Z_DOTS:
    return open_post_menu(&t->posts[t->sel], z->r.x + z->r.w, z->r.y, z->r.y + z->r.h);
  case Z_FOLLOW:
    return follow_toggle(t);
  case Z_UMENU:
    return open_user_menu(t, z->r.x + z->r.w, z->r.y, z->r.y + z->r.h);
  case Z_LINK:
    return open_signin(SHEET_NONE);
  case Z_GROK:
    return grok_ask();
  case Z_MINI:
    return mini_back(t);
  case Z_MINI_CLOSE:
    return mini_close();
  case Z_TAB:
    show_section(z->arg);
    return 1;
  case Z_PRODUCT:
    return set_product(z->arg);
  case Z_REFRESH:
    t->wanted = 1;
    images_retry();
    return 1;
  case Z_BACK:
    return back();
  case Z_WELL:
    return t->feed == UI_SEARCH ? ask_search() : act(t, UI_ACT_REPLY);
  case Z_SIDE_TREND:
    return z->arg < g_trends.count ? open_trend(&g_trends.items[z->arg]) : 0;
  case Z_SIDE_USER:
    return z->arg < g_who.count ? open_user(&g_who.items[z->arg]) : 0;
  case Z_SIDE_PLAYER:
    return z->arg < g_playing.count ? open_user(&g_playing.items[z->arg]) : 0;
  case Z_SIDE_INVITE:
    if(z->arg < g_playing.count) {
      const x_post_t *p = &g_playing.items[z->arg];
      return invite(p->user_id, p->user, p->playing);
    }
    return 0;
  case Z_SIDE_FOLLOW:
    if(z->arg < g_who.count) {
      const x_post_t *p = &g_who.items[z->arg];
      return act_on(p->following ? UI_ACT_UNFOLLOW : UI_ACT_FOLLOW, p->user_id, p->user);
    }
    return 0;
  case Z_SIDE_MORE:
    // Explore's trends, without the words searched for
    if(g_tabs[UI_SEARCH].query[0] || g_tabs[UI_SEARCH].latest) {
      g_tabs[UI_SEARCH].query[0] = 0;
      g_tabs[UI_SEARCH].latest = 0;
      reset_search(&g_tabs[UI_SEARCH]);
    }
    show_section(UI_SEARCH);
    return 1;
  case Z_VIEWER:
    g_viewer.open = 0;
    return 1;
  case Z_CARD:
    open_selected(t);
    return 1;
  case Z_AUTHOR:
    open_user(&t->posts[t->sel]);
    return 1;
  case Z_LIKE:
    act(t, UI_ACT_LIKE);
    return 1;
  case Z_REPLY:
    act(t, UI_ACT_REPLY);
    return 1;
  case Z_REPOST:
    act(t, UI_ACT_REPOST);
    return 1;
  case Z_BOOKMARK:
    act(t, UI_ACT_BOOKMARK);
    return 1;
  case Z_MORE:
    toggle_open(t);
    return 1;
  case Z_T_SOUND:
    // the timeline's video with its sound, or without, where it plays
    g_inline_sound = !g_inline_sound;
    player_sound(g_inline_sound);
    return 1;
  case Z_MEDIA:
    open_viewer(&t->posts[t->sel], z->sub);
    return 1;
  case Z_POLL:
    return vote(t, z->sub + 1);
  }
  return 0;
}


/**
 * the post under the pointer becomes the selected one
 **/
static void
hover(tab_t *t) {
  int y = (int)g_ptr.y - LIST_TOP - LIST_PAD + t->scroll;

  if(g_viewer.open || g_sheet || g_ptr.x < MAIN_X || g_ptr.x >= MAIN_X + MAIN_W || g_ptr.y < LIST_TOP ||
     g_ptr.y >= LIST_TOP + LIST_H) {
    return;
  }
  for(int i=0; i < t->count; i++) {
    if(y >= t->cards[i].y && y < t->cards[i].y + t->cards[i].height - CARD_GAP) {
      t->sel = i;
      return;
    }
  }
}


/**
 * after the right stick scrolled, with the pointer away: a post on the
 * screen stays selected
 **/
static void
keep_selection(tab_t *t) {
  const card_t *c;

  if(!t->count) {
    return;
  }
  c = &t->cards[t->sel];
  if(c->y + c->height > t->target + 60 && c->y < t->target + VIEW_H - 60) {
    return;
  }
  for(int i=0; i < t->count; i++) {
    c = &t->cards[i];
    if(c->y + c->height > t->target + 60) {
      t->sel = i;
      return;
    }
  }
}


/**
 * whether the list t did not come for want of a session X takes
 **/
static int
session_error(const tab_t *t) {
  return !t->loading && (t->error == ERR_NO_SESSION || t->error == ERR_NO_COOKIE ||
                         (t->error == ERR_HTTP && (t->code == 401 || t->code == 403)));
}


int
ui_keys(unsigned keys) {
  tab_t *t = view();
  int changed = 0, s;

  keys &= ~UI_TOUCHPAD; // the bar is always there
  g_under_stale = 1;     // a button on a sheet may change what is under it
  g_input_us = g_ui_us;

  if(g_viewer.open) {
    // the pointer on the video's controls presses them
    const zone_t *z = g_ptr.shown && (keys & UI_CROSS) ? zone_at(g_ptr.x, g_ptr.y) : NULL;
    if(z && z->kind >= Z_V_PLAY && z->kind <= Z_V_BAR) {
      return click();
    }
    return viewer_keys(keys);
  }
  if(g_sheet) {
    if((keys & UI_CROSS) && g_ptr.shown) {
      return click();
    }
    return sheet_keys(keys);
  }
  if((keys & UI_R3) && g_mini.shown) {
    return mini_close();
  }
  // ← and → go through the sections in the bar's order; ← also closes
  // what was opened from them
  if((keys & UI_LEFT) && g_depth) {
    changed |= back();
  } else if((keys & (UI_LEFT | UI_RIGHT)) && !g_depth) {
    if((s=section_step(keys & UI_RIGHT ? 1 : -1)) >= 0) {
      show_section(s);
      changed = 1;
    }
  }
  if(keys & UI_CIRCLE) {
    changed |= back();
  }
  if(keys & UI_CROSS) {
    t = view();
    // a search without posts: the keyboard for it
    changed |= g_ptr.shown ? click() : t->feed == UI_GROK ? grok_ask() :
      t->feed == UI_SEARCH && !t->count ? ask_search() :
      t->feed == UI_DM && !t->count ? dm_write(t) : !t->count && session_error(t) ? open_signin(SHEET_NONE) :
      open_selected(t);
  }
  t = view();
  // △ goes to the top, and at the top of a search to the keyboard for it
  if((keys & UI_TRIANGLE) && t->feed == UI_SEARCH && !t->sel && !t->target) {
    changed |= ask_search();
    keys &= ~UI_TRIANGLE;
  }
  if(keys & (UI_R2 | UI_L2)) {
    changed |= act(t, keys & UI_L2 ? UI_ACT_REPLY : UI_ACT_LIKE);
  }
  if(keys & UI_OPTIONS) {
    changed |= compose();
  }
  if((keys & UI_SQUARE) && t->feed == UI_GROK) {
    changed |= grok_fresh();
  } else if(keys & UI_SQUARE) {
    t->wanted = 1;
    images_retry();
    changed = 1;
  }
  if(keys & (UI_UP | UI_DOWN | UI_L1 | UI_R1 | UI_TRIANGLE)) {
    g_ptr.shown = 0;
    move(t, keys);
    changed = 1;
  }
  return changed;
}


static int   set_scroll(int d);
static void  reader_stick(int d);
static float g_set_stick; // the right stick's turn through the settings' rows, in rows


int
ui_sticks(int lx, int ly, int rx, int ry) {
  tab_t *t = view();
  float m = sqrtf((float)(lx * lx + ly * ly)), s;
  int changed = 0, d;

  (void)rx;
  if(m > STICK_DEAD || ry > STICK_DEAD || ry < -STICK_DEAD) {
    g_input_us = g_ui_us;
  }
  if(m > STICK_DEAD) {
    // slow near the middle, for small things, fast at the edge
    s = (m - STICK_DEAD) / (128.0f - STICK_DEAD);
    s = s > 1 ? 1 : s;
    s = (1.5f + 26 * s * s) * g_step;
    g_ptr.x += lx / m * s;
    g_ptr.y += ly / m * s;
    g_ptr.x = g_ptr.x < 0 ? 0 : g_ptr.x > UI_WIDTH - 1 ? UI_WIDTH - 1 : g_ptr.x;
    g_ptr.y = g_ptr.y < 0 ? 0 : g_ptr.y > UI_HEIGHT - 1 ? UI_HEIGHT - 1 : g_ptr.y;
    g_ptr.shown = 1;
    g_viewer.input_us = g_ui_us; // a video's controls come back with the pointer
    hover(t);
    changed = 1;
  }
  if(g_sheet == SHEET_LOGIN) {
    return login_sticks(changed, ry);
  }
  if(g_sheet == SHEET_GALLERY && (ry > STICK_DEAD || ry < -STICK_DEAD)) {
    return changed | sheet_scroll(ry);
  }
  if(g_sheet == SHEET_READER && (ry > STICK_DEAD || ry < -STICK_DEAD)) {
    s = (ry > 0 ? ry - STICK_DEAD : ry + STICK_DEAD) / (128.0f - STICK_DEAD);
    s = s > 1 ? 1 : s < -1 ? -1 : s;
    reader_stick((int)((s * (s < 0 ? -s : s) * SCROLL_MAX + (s < 0 ? -1.5f : 1.5f)) * g_step));
    return 1;
  }
  if(g_sheet == SHEET_SETTINGS && (ry > STICK_DEAD || ry < -STICK_DEAD)) {
    s = (ry > 0 ? ry - STICK_DEAD : ry + STICK_DEAD) / (128.0f - STICK_DEAD);
    g_set_stick += s * 0.12f * g_step;
    d = (int)g_set_stick;
    g_set_stick -= (float)d;
    return changed | (d ? set_scroll(d) : 0);
  }
  if(!g_viewer.open && !g_sheet && (ry > STICK_DEAD || ry < -STICK_DEAD)) {
    s = (ry > 0 ? ry - STICK_DEAD : ry + STICK_DEAD) / (128.0f - STICK_DEAD);
    s = s > 1 ? 1 : s < -1 ? -1 : s;
    d = (int)((s * (s < 0 ? -s : s) * SCROLL_MAX + (s < 0 ? -1.5f : 1.5f)) * g_step);
    t->target += d;
    clamp_target(t);
    if(t->target != t->scroll) {
      t->scroll = t->target;
      if(g_ptr.shown) {
        hover(t);
      } else {
        keep_selection(t);
      }
      changed = 1;
    }
  }
  return changed;
}



int
ui_light(unsigned char rgb[3]) {
  float top = 1, v;

  for(int c=0; c<3; c++) {
    top = g_ptr_col[c] > top ? g_ptr_col[c] : top;
  }
  for(int c=0; c<3; c++) {
    v = g_ptr_col[c] * 255 / top + 0.5f;
    rgb[c] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
  }
  return g_settings.light;
}


void
ui_pointer(int *x, int *y) {
  *x = g_ptr.shown ? (int)g_ptr.x : -1;
  *y = g_ptr.shown ? (int)g_ptr.y : -1;
}


#define IDLE_US  (3ull * 60 * 1000000)  // without a button or a video, XPS5 is not in use
#define AWAY_US  (10ull * 60 * 1000000) // and that long without them was a break


/**
 * the break reminder (Settings), once XPS5 has been in use that long
 **/
static void
break_check(void) {
  char spent[40];

  if(!g_settings.pause || g_active_us < (uint64_t)g_settings.pause * 60 * 1000000) {
    return;
  }
  g_active_us = 0;
  if(g_settings.pause % 60) {
    snprintf(spent, sizeof spent, "%d min", g_settings.pause);
  } else if(g_settings.pause == 60) {
    snprintf(spent, sizeof spent, "%s", L("1 hora", "1 hour"));
  } else {
    snprintf(spent, sizeof spent, L("%d horas", "%d hours"), g_settings.pause / 60);
  }
  toast(0, L("J\xc3\xa1 est\xc3\xa1s h\xc3\xa1 %s na XPS5 \xc2\xb7 que tal uma pausa?",
             "You have been on XPS5 for %s \xc2\xb7 time for a break?"), spent);
  g_toast.frames = 3 * TOAST_FRAMES;
}


void
ui_elapsed(unsigned us) {
  // in use while the buttons, the sticks or a video say so
  if(g_ui_us - g_input_us < IDLE_US || g_player_wanted) {
    g_active_us += us;
  } else if(g_ui_us - g_input_us > AWAY_US) {
    g_active_us = 0;
  }
  break_check();
  g_ui_us += us;
  g_step = us / 16667.0f;
  g_step = g_step < 0.5f ? 0.5f : g_step > 3 ? 3 : g_step;
}


void
ui_pictures_arrived(void) {
  g_under_stale = 1;
}

/**
 * whether the footer shows more posts loading, or about to
 **/
static int
footer_spins(const tab_t *t) {
  return t->loading == LOAD_MORE || (t->cursor[0] && !t->more_error);
}


/**
 * where the footer starts, from the top of the timeline on the screen
 **/
static int
footer_top(const tab_t *t) {
  return LIST_PAD + t->content_h - FOOTER_H - t->scroll;
}


int
ui_animate(void) {
  tab_t *t = view();
  int diff = t->target - t->scroll, more = sheet_animate();

  // a new picture of the video, or its state
  more |= player_changed();
  // the backdrop going over to a banner, or the other theme
  more |= backdrop_move();
  // the pointer turning into its ring, or back
  more |= pointer_turn();

  if(diff > -3 && diff < 3) {
    t->scroll = t->target;
  } else {
    t->scroll += diff * 2 / 5;
  }
  if(g_toast.frames > 0) {
    g_toast.frames--;
    return 1;
  }
  return more || diff != 0 || t->loading || g_spinning || (g_viewer.open && g_viewer.waiting) ||
         (t->count && footer_spins(t) && footer_top(t) < LIST_H);
}


/*
 * The parts of a post.
 */

/**
 * the URL of a bigger avatar than the 48-pixel one in the answer
 **/
static const char *
avatar_url(const char *url, char *buf, size_t size) {
  const char *at = strstr(url, "_normal.");

  if(!at || strlen(url) + 8 >= size) {
    return url;
  }
  snprintf(buf, size, "%.*s_200x200%s", (int)(at - url), url, at + 7);
  return buf;
}


/**
 * the URL of a picture at a size x.com asks for: medium for one alone,
 * small in a grid, large over the whole screen
 **/
static const char *
media_url(const char *url, const char *name, char *buf, size_t size) {
  snprintf(buf, size, "%s%cname=%s", url, strchr(url, '?') ? '&' : '?', name);
  return buf;
}


/**
 * the URL of a profile's banner at the size its page shows it
 **/
static const char *
banner_url(const char *url, char *buf, size_t size) {
  const char *last = strrchr(url, '/');

  // one that already has its size (".../1500x500") keeps it
  snprintf(buf, size, last && strchr(last, 'x') ? "%s" : "%s/1500x500", url);
  return buf;
}


/**
 * a button with a name, like L1, drawn as a key 38 high; returns its width
 **/
static int
draw_key(const char *name, int x, int y) {
  text_font_t *f = font(F_KEY);
  int w = width_of(f, name) + 22;

  draw_box(&g_dst, x, y, w, 38, 11, 0, CHIP_ON);
  text(f, x + 11, baseline(f, y, 38), name, FG);
  return w;
}


static void
draw_avatar(const char *url, const char *user, const char *name, int x, int y, int size) {
  static const uint32_t colors[] = {
    0xFFF09B1Du, 0xFF7CBA00u, 0xFF8C18F9u, 0xFF0080FFu, 0xFFC45E79u, 0xFF4BBBFFu,
  };
  char buf[IMAGES_URL_MAX], initial[2] = {user[0] ? user[0] : '?', 0};
  const char *s = name, *end = s + strlen(s);
  const uint32_t *pixels;
  text_font_t *f = font(size > 50 ? F_NAME : F_SMALL_BOLD);
  unsigned hash = 5381;
  uint32_t cp;

  if(url[0]) {
    if((pixels=picture(avatar_url(url, buf, sizeof buf), size, size))) {
      draw_picture_round(&g_dst, x, y, size, size, size / 2.0f, pixels);
    } else {
      draw_circle(&g_dst, x + size / 2.0f, y + size / 2.0f, size / 2.0f, PLACEHOLDER);
    }
    return;
  }

  // no picture: the first letter or digit of the name on a color of its own
  for(const char *p=user; *p; p++) {
    hash = hash * 33 + (unsigned char)*p;
  }
  draw_circle(&g_dst, x + size / 2.0f, y + size / 2.0f, size / 2.0f,
              colors[hash % (sizeof colors / sizeof *colors)]);
  while((cp=text_next(&s, end))) {
    if((cp | 0x20) - 'a' < 26 || cp - '0' < 10) {
      initial[0] = (char)cp;
      break;
    }
  }
  if(initial[0] >= 'a' && initial[0] <= 'z') {
    initial[0] -= 0x20;
  }
  text(f, x + (size - width_of(f, initial)) / 2, baseline(f, y, size), initial, WHITE);
}


static void
draw_badge(int badge, int x, int y, int size) {
  uint32_t color = badge == X_BADGE_GOLD ? GOLD : badge == X_BADGE_GREY ? STEEL : BLUE;

  draw_icon(&g_dst, ICON_BADGE, x, y, size, color);
  draw_icon(&g_dst, ICON_CHECK, x, y, size, badge == X_BADGE_GOLD ? INK : WHITE);
}


/**
 * the name, its badge and "@user" in a row h high, with how long ago on the
 * right when there is; the name gives way first, down to half the room
 **/
/**
 * the badge of someone who plays, on their picture at x, y of size: a
 * DualSense-like controller (white, its black middle, the light bar in the
 * game's green) on a dark disc ringed in green, at the picture's lower right.
 * All of it vectors, so as sharp on a 4K TV as at 1080p
 **/
static void
playing_badge(int x, int y, int size) {
  const float r = 5 + size * 0.13f, ring = r * 0.11f > 1.2f ? r * 0.11f : 1.2f, inner = r - ring;
  const float cx = x + size * 0.84f, cy = y + size * 0.84f;
  // the controller's 24 units are 1.98 times the inner disc's radius (its
  // widest point then 0.92 of the radius from the middle), its middle (12,
  // 12.5 of them) on the disc's
  const int box = (int)(inner * 1.98f + 0.5f);
  const int bx = (int)floorf(cx - box / 2.0f + 0.5f), by = (int)floorf(cy - box * 12.5f / 24 + 0.5f);

  draw_circle(&g_dst, cx, cy, r + 2.5f, INK);
  draw_circle(&g_dst, cx, cy, r, GREEN);
  draw_circle(&g_dst, cx, cy, inner, RGBA(12, 14, 19, 255));
  draw_icon(&g_dst, ICON_PAD_BODY, bx, by, box, RGBA(242, 243, 247, 255));
  draw_icon(&g_dst, ICON_PAD_CORE, bx, by, box, RGBA(40, 44, 54, 255));
  draw_icon(&g_dst, ICON_PAD_STICKS, bx, by, box, RGBA(92, 98, 110, 255));
  draw_icon(&g_dst, ICON_PAD_MARKS, bx, by, box, RGBA(118, 124, 138, 255));
  draw_icon(&g_dst, ICON_PAD_LIGHT, bx, by, box, GREEN);
}


/**
 * a name row, and after the screen name the game its user plays (playing,
 * or NULL) in green
 **/
static void
draw_name_row_playing(text_font_t *bold, text_font_t *regular, int x, int top, int h, int width,
                      const char *name, int badge, const char *user, long long t, long long now,
                      const char *playing) {
  char handle[96], when[48];
  int size = text_line_height(bold) - 4, base = baseline(bold, top, h), right = x + width;
  int badge_w = badge ? size + 6 : 0, name_w, room;

  when[0] = 0;
  if(t) {
    age(t, now, when, sizeof when);
  }
  if(when[0]) {
    text(regular, right - width_of(regular, when), base, when, GREY);
    right -= width_of(regular, when) + 22;
  }
  if((room=right - x) < 40) {
    return;
  }
  snprintf(handle, sizeof handle, "@%s", user);
  name_w = room - width_of(regular, handle) - 12 - badge_w;
  name_w = name_w < room / 2 ? room / 2 : name_w;
  if(name[0]) {
    x = text_fitted(bold, x, base, name, name_w, FG);
  }
  if(badge) {
    draw_badge(badge, x + 6, top + (h - size) / 2, size);
    x += badge_w;
  }
  x += 12;
  if(x < right) {
    int room_handle = right - x, is = text_line_height(regular) - 6;
    if(playing && playing[0] && room_handle > 220) {
      // the screen name whole when it fits, the game in what is left
      room_handle = width_of(regular, handle) < room_handle - 170 ? width_of(regular, handle) : room_handle - 170;
    }
    x = text_fitted(regular, x, base, handle, room_handle, GREY);
    if(playing && playing[0] && right - x > 90) {
      draw_icon(&g_dst, ICON_GAME, x + 16, top + (h - is) / 2, is, GREEN);
      text_fitted(regular, x + 24 + is, base, playing, right - x - 24 - is, GREEN);
    }
  }
}


static void
draw_name_row(text_font_t *bold, text_font_t *regular, int x, int top, int h, int width,
              const char *name, int badge, const char *user, long long t, long long now) {
  draw_name_row_playing(bold, regular, x, top, h, width, name, badge, user, t, now, NULL);
}


/**
 * whether m, p's video, is the one the selection left playing: it goes on in
 * its tile while the post is on the screen, and small in the corner after
 **/
static int
mini_post(const x_post_t *p, const x_media_t *m) {
  const char *url = video_of(m);

  return g_mini.on && !strcmp(g_mini.post, p->id) && g_mini.serial == view()->serial && url &&
         player_is(url);
}


/**
 * a round play button over a video's still
 **/
static void
draw_play(float cx, float cy, float r) {
  draw_circle(&g_dst, cx, cy, r, RGBA(20, 16, 14, 170));
  draw_arc(&g_dst, cx, cy, r - 1.5f, 3, 0, 6.2832f, RGBA(255, 255, 255, 210));
  draw_icon(&g_dst, ICON_PLAY, (int)(cx - r * 0.55f), (int)(cy - r * 0.57f), (int)(r * 1.1f), WHITE);
}


static void
draw_media(const card_t *c, int index, int top, unsigned frame) {
  const x_post_t *p = c->post;
  const zone_t *z;
  const uint32_t *pixels;
  char buf[IMAGES_URL_MAX];
  int y = top + c->media_y, over, v;
  float r = p->media_count == 1 ? RADIUS : TILE_R;

  for(int i=0; i < p->media_count; i++) {
    const rect_t *m = &c->media[i];
    int x = TEXT_X + m->x, my = y + m->y;
    over = zone(Z_MEDIA, index, i, x, my, m->w, m->h);
    if((pixels=picture(media_url(p->media[i].url, p->media_count == 1 ? "medium" : "small", buf,
                                    sizeof buf), m->w, m->h))) {
      draw_picture_round(&g_dst, x, my, m->w, m->h, r, pixels);
    } else {
      draw_box(&g_dst, x, my, m->w, m->h, r, 0, PLACEHOLDER);
    }
    if(p->media[i].video) {
      // the selected post's video plays in its tile; the play button only
      // where nothing plays, or it is paused
      v = p->media_count == 1 && (index == view()->sel || mini_post(p, &p->media[i])) &&
          g_settings.autoplay && !g_viewer.open && !g_sheet ?
          inline_video(&p->media[i], index, x, my, m->w, m->h, r) : INLINE_NONE;
      if(v == INLINE_PLAYING || v == INLINE_PAUSED) {
        mini_keep(p, &p->media[i]);
      }
      if(v == INLINE_WAIT) {
        g_spinning = 1;
        draw_circle(&g_dst, x + m->w / 2.0f, my + m->h / 2.0f, 40, RGBA(20, 16, 14, 150));
        spinner(x + m->w / 2.0f, my + m->h / 2.0f, 24, 5, frame, WHITE);
      } else if(v != INLINE_PLAYING) {
        draw_play(x + m->w / 2.0f, my + m->h / 2.0f, 46);
      }
    }
    // lit under the pointer, unless it is on the video's sound
    if(over && (z=zone_at(g_ptr.x, g_ptr.y)) && z->kind == Z_MEDIA) {
      draw_box(&g_dst, x, my, m->w, m->h, r, 3, RGBA(255, 255, 255, 210));
    }
  }
}


static void
draw_quote(const card_t *c, int top, long long now) {
  const x_post_t *p = c->post;
  int y = top + c->quote_y, x = TEXT_X + QUOTE_PAD, width = TEXT_W - 2 * QUOTE_PAD;
  text_font_t *f = font(F_QUOTE);

  draw_box(&g_dst, TEXT_X, y, TEXT_W, c->quote_h, RADIUS, 0, RGBA(0, 0, 0, 40));
  draw_box(&g_dst, TEXT_X, y, TEXT_W, c->quote_h, RADIUS, 1.5f, EDGE);
  y += QUOTE_PAD;
  draw_avatar(p->quote_avatar, p->quote_user, p->quote_name, x,
              y + (QUOTE_NAME_H - QUOTE_AVATAR) / 2, QUOTE_AVATAR);
  draw_name_row(font(F_QUOTE_BOLD), font(F_SMALL), x + QUOTE_AVATAR + 10, y, QUOTE_NAME_H,
                width - QUOTE_AVATAR - 10, p->quote_name, p->quote_badge, p->quote_user, 0, now);
  y += QUOTE_NAME_H;
  for(int i=0; i < c->quote_lines; i++) {
    int end = text_draw(f, &g_dst, x, baseline(f, y, QUOTE_LINE_H), p->quote_text + c->quote_start[i],
                        c->quote_len[i], FG);
    if(c->quote_more && i == c->quote_lines - 1) {
      text(f, end, baseline(f, y, QUOTE_LINE_H), "\xe2\x80\xa6", FG);
    }
    y += QUOTE_LINE_H;
  }
}


/**
 * whether the poll of p still takes votes
 **/
static int
poll_open(const x_post_t *p, long long now) {
  return p->poll_count && !p->poll_final && (!p->poll_end || p->poll_end > now);
}


/**
 * whether p is the signed-in user's own post, whose poll they cannot vote
 **/
static int
own_post(const x_post_t *p) {
  return g_tabs[UI_ME].has_user && p->user_id[0] && !strcmp(p->user_id, g_tabs[UI_ME].user.id);
}


/**
 * how long a poll still takes votes, or that it closed
 **/
static void
poll_left(const x_post_t *p, long long now, char *buf, size_t size) {
  long long d = p->poll_end - now;

  if(!poll_open(p, now)) {
    snprintf(buf, size, "%s", L("Resultados finais", "Final results"));
  } else if(!p->poll_end) {
    buf[0] = 0;
  } else if(d >= 2 * 86400) {
    snprintf(buf, size, L("Faltam %lld dias", "%lld days left"), d / 86400);
  } else if(d >= 86400) {
    snprintf(buf, size, "%s", L("Falta 1 dia", "1 day left"));
  } else if(d >= 7200) {
    snprintf(buf, size, L("Faltam %lld horas", "%lld hours left"), d / 3600);
  } else if(d >= 3600) {
    snprintf(buf, size, "%s", L("Falta 1 hora", "1 hour left"));
  } else if(d >= 120) {
    snprintf(buf, size, L("Faltam %lld minutos", "%lld minutes left"), d / 60);
  } else {
    snprintf(buf, size, "%s", L("Falta menos de 1 minuto", "Less than a minute left"));
  }
}


/**
 * a post's poll, as x.com shows it: a button for each choice while the
 * user may vote; else a bar for each, as long as its share of the votes,
 * the leading one in blue and the user's own ticked; the votes and the
 * time left under them
 **/
static void
draw_poll(const card_t *c, int index, int top, long long now) {
  const x_post_t *p = c->post;
  text_font_t *f = font(F_QUOTE), *bold = font(F_QUOTE_BOLD), *small = font(F_SMALL), *lf;
  const int votable = poll_open(p, now) && !p->poll_voted && !own_post(p) && p->poll_card[0];
  int y = top + c->poll_y, over, w, end, fill, pct;
  long long total = 0, best = 0, v;
  char line[160], n[32], left[64];

  for(int i=0; i < p->poll_count; i++) {
    v = p->poll_votes[i] > 0 ? p->poll_votes[i] : 0;
    total += v;
    best = v > best ? v : best;
  }
  for(int i=0; i < p->poll_count; i++, y += POLL_ROW + POLL_GAP) {
    v = p->poll_votes[i] > 0 ? p->poll_votes[i] : 0;
    if(votable) {
      over = zone(Z_POLL, index, i, TEXT_X, y, TEXT_W, POLL_ROW);
      if(over) {
        draw_box(&g_dst, TEXT_X, y, TEXT_W, POLL_ROW, POLL_ROW / 2.0f, 0, RGBA(29, 155, 240, 90));
        lit_box(TEXT_X, y, TEXT_W, POLL_ROW, POLL_ROW / 2.0f, 0);
      }
      draw_box(&g_dst, TEXT_X, y, TEXT_W, POLL_ROW, POLL_ROW / 2.0f, 2, BLUE);
      w = width_of(bold, p->poll_label[i]);
      w = w > TEXT_W - 48 ? TEXT_W - 48 : w;
      text_fitted(bold, TEXT_X + (TEXT_W - w) / 2, baseline(bold, y, POLL_ROW), p->poll_label[i], TEXT_W - 48,
                  BLUE);
      continue;
    }
    lf = v == best && best > 0 ? bold : f;
    fill = total ? (int)(TEXT_W * v / total) : 0;
    fill = fill && fill < 14 ? 14 : fill;
    pct = total ? (int)((v * 100 + total / 2) / total) : 0;
    if(fill) {
      draw_box(&g_dst, TEXT_X, y, fill, POLL_ROW, 10, 0, lf == bold ? RGBA(29, 155, 240, 120) :
               RGBA(150, 150, 160, 70));
    }
    snprintf(n, sizeof n, "%d%%", pct);
    w = width_of(lf, n);
    text(lf, TEXT_X + TEXT_W - 16 - w, baseline(lf, y, POLL_ROW), n, FG);
    end = text_fitted(lf, TEXT_X + 18, baseline(lf, y, POLL_ROW), p->poll_label[i], TEXT_W - 18 - w - 76, FG);
    if(p->poll_voted == i + 1) {
      draw_icon(&g_dst, ICON_CHECK, end + 10, y + (POLL_ROW - 28) / 2, 28, FG);
    }
  }
  // the votes, and how long it stays open
  count_text(total, n, sizeof n);
  poll_left(p, now, left, sizeof left);
  snprintf(line, sizeof line, total == 1 ? L("%s voto%s%s", "%s vote%s%s") : L("%s votos%s%s", "%s votes%s%s"), n,
           left[0] ? "  \xc2\xb7  " : "", left);
  text_fitted(small, TEXT_X + 4, baseline(small, y - POLL_GAP, POLL_FOOT), line, TEXT_W - 8, GREY);
}


/**
 * "15 mil visualizações", under the post opened
 **/
static void
meta_line(const x_post_t *p, char *buf, size_t size) {
  char n[32], rate[32];
  long long did, permille;
  size_t len;

  buf[0] = 0;
  if(p->views <= 0) {
    return;
  }
  count_text(p->views, n, sizeof n);
  snprintf(buf, size, "%s %s", n, lang_count_word(g_lang, 3, p->views != 1));
  // how many of those who saw it did something with it, which x.com shows
  // its author alone
  did = (p->likes > 0 ? p->likes : 0) + (p->reposts > 0 ? p->reposts : 0) +
        (p->replies > 0 ? p->replies : 0) + (p->bookmarks > 0 ? p->bookmarks : 0);
  if(did <= 0 || p->views < 100) {
    return;
  }
  permille = did >= p->views ? 1000 : did * 1000 / p->views;
  if(!permille) {
    snprintf(rate, sizeof rate, "<0%c1%%", lang_decimal(g_lang));
  } else if(permille >= 100) {
    snprintf(rate, sizeof rate, "%lld%%", permille / 10);
  } else {
    snprintf(rate, sizeof rate, "%lld%c%lld%%", permille / 10, lang_decimal(g_lang), permille % 10);
  }
  len = strlen(buf);
  snprintf(buf + len, size - len, " \xc2\xb7 ");
  len = strlen(buf);
  snprintf(buf + len, size - len, L("%s de envolvimento", "%s engagement"), rate);
}


/**
 * the post's buttons in x.com's order, each with its count: reply, repost,
 * like and bookmark, spread over the width of the text with the last one at
 * its end; grey, in their colour when done (or under the pointer, which also
 * lights a ring around it)
 **/
static void
draw_actions(const card_t *c, int index, int top, int selected) {
  const x_post_t *p = c->post;
  const struct { int kind, icon; long long count; int on; uint32_t color, glow; } buttons[4] = {
    {Z_REPLY, ICON_REPLY, p->replies, 0, BLUE, RGBA(29, 155, 240, 64)},
    {Z_REPOST, ICON_REPOST, p->reposts, p->reposted, GREEN, RGBA(0, 186, 124, 64)},
    {Z_LIKE, p->liked ? ICON_LIKED : ICON_LIKE, p->likes, p->liked, PINK, RGBA(249, 24, 128, 64)},
    {Z_BOOKMARK, p->bookmarked ? ICON_BOOKMARKED : ICON_BOOKMARK, p->bookmarks, p->bookmarked, BLUE,
     RGBA(29, 155, 240, 64)},
  };
  text_font_t *f = font(F_SMALL);
  int y = top + c->actions_y, x = TEXT_X - 4, iy = y + (ACTIONS_H - ICON) / 2, bx, over, last, step;
  char counts[4][32], line[200];
  uint32_t color;

  (void)selected; // the side column says what the triggers do
  for(int i=0; i < 4; i++) {
    counts[i][0] = 0;
    if(buttons[i].count > 0) {
      count_text(buttons[i].count, counts[i], sizeof counts[i]);
    }
  }
  // the last one ends where the text does
  last = ICON + (counts[3][0] ? 8 + width_of(f, counts[3]) : 0);
  step = (TEXT_W + 4 - last) / 3;
  for(int i=0; i < 4; i++) {
    bx = x + i * step;
    over = zone(buttons[i].kind, index, 0, bx - 12, iy - 12, ICON + 24, ICON + 24);
    if(over) {
      draw_circle(&g_dst, bx + ICON / 2.0f, iy + ICON / 2.0f, ICON / 2.0f + 12, buttons[i].glow);
    }
    color = buttons[i].on || over ? buttons[i].color : GREY;
    draw_icon(&g_dst, buttons[i].icon, bx, iy, ICON, color);
    if(counts[i][0]) {
      text_fitted(f, bx + ICON + 8, baseline(f, y, ACTIONS_H), counts[i], step - ICON - 16, color);
    }
  }
  if(c->meta) {
    meta_line(p, line, sizeof line);
    text_fitted(font(F_SMALL), TEXT_X, baseline(font(F_SMALL), y + ACTIONS_H, META_H), line, TEXT_W,
                GREY);
  }
}


/**
 * a notification: its icon, the people in it, what it says and, in grey,
 * the post it is about
 **/
static void
draw_notice(const card_t *c, int index, int top, long long now) {
  static const struct { int icon; uint32_t color; } looks[] = {
    [X_ICON_OTHER] = {ICON_BELL, ACCENT}, [X_ICON_LIKE] = {ICON_LIKED, PINK},
    [X_ICON_REPOST] = {ICON_REPOST, GREEN}, [X_ICON_FOLLOW] = {ICON_PERSON, BLUE},
    [X_ICON_REPLY] = {ICON_REPLY, BLUE},
  };
  const x_post_t *p = c->post;
  int look = p->icon > X_ICON_OTHER && p->icon <= X_ICON_REPLY ? p->icon : X_ICON_OTHER;
  text_font_t *body = font(F_BODY), *quote = font(F_QUOTE), *small = font(F_SMALL);
  int y = top + c->avatar_y, x = TEXT_X, end;
  char when[48];

  draw_icon(&g_dst, looks[look].icon, AVATAR_X + AVATAR - NOTICE_ICON - 2,
            y + (FACE - NOTICE_ICON) / 2, NOTICE_ICON, looks[look].color);
  for(int i=0; i < p->face_count; i++, x += FACE + 8) {
    draw_avatar(p->faces[i], i ? "" : p->user, i ? "" : p->name, x, y, FACE);
  }
  if(p->face_count && zone(Z_AUTHOR, index, 0, TEXT_X - 4, y - 4, FACE + 8, FACE + 8)) {
    draw_arc(&g_dst, TEXT_X + FACE / 2.0f, y + FACE / 2.0f, FACE / 2.0f + 4, 3, 0, 6.2832f, ACCENT);
  }
  when[0] = 0;
  if(p->time) {
    age(p->time, now, when, sizeof when);
  }
  if(when[0]) {
    text(small, TEXT_X + TEXT_W - width_of(small, when), baseline(small, y, FACE), when, GREY);
  }

  y = top + c->body_y;
  for(int i=0; i < c->lines; i++, y += LINE_H) {
    end = text_draw(body, &g_dst, TEXT_X, baseline(body, y, LINE_H), p->text + c->start[i],
                    c->len[i], FG);
    if(c->more && i == c->lines - 1) {
      text(body, end, baseline(body, y, LINE_H), "\xe2\x80\xa6", FG);
    }
  }
  y = top + c->quote_y;
  for(int i=0; i < c->quote_lines; i++, y += QUOTE_LINE_H) {
    end = text_draw(quote, &g_dst, TEXT_X, baseline(quote, y, QUOTE_LINE_H),
                    p->quote_text + c->quote_start[i], c->quote_len[i], GREY);
    if(c->quote_more && i == c->quote_lines - 1) {
      text(quote, end, baseline(quote, y, QUOTE_LINE_H), "\xe2\x80\xa6", GREY);
    }
  }
}


/**
 * a conversation of Chat, as x.com lists them: the picture, who it is with,
 * when, and the last message, in white while it is unread
 **/
static void
draw_chat(const card_t *c, int index, int top, long long now) {
  const x_post_t *p = c->post;
  text_font_t *name = font(F_NAME), *small = font(F_SMALL), *body = font(F_BODY);
  text_font_t *bold = font(F_SMALL_BOLD);
  int y = top + c->avatar_y, right = TEXT_X + TEXT_W, icon, w;
  const char *carries = dm_carries(p->icon, &icon), *label;
  const char *from = !p->mine && p->group && p->quote_name[0] ? p->quote_name : "";
  char line[1400], when[48];

  if(p->group && !p->avatar[0] && p->face_count >= 2) {
    draw_avatar(p->faces[1], "", p->name, AVATAR_X + AVATAR - GROUP_FACE, y + AVATAR - GROUP_FACE, GROUP_FACE);
    draw_avatar(p->faces[0], "", p->name, AVATAR_X, y, GROUP_FACE);
  } else {
    draw_avatar(p->group && !p->avatar[0] && p->face_count ? p->faces[0] : p->avatar,
                p->user[0] ? p->user : p->name, p->name, AVATAR_X, y, AVATAR);
    if(!p->group && p->playing[0]) {
      playing_badge(AVATAR_X, y, AVATAR);
    }
    // the picture of who it is with opens their profile
    if(!p->group && p->user_id[0] && zone(Z_AUTHOR, index, 0, AVATAR_X - 6, y - 6, AVATAR + 12, AVATAR + 12)) {
      draw_arc(&g_dst, AVATAR_X + AVATAR / 2.0f, y + AVATAR / 2.0f, AVATAR / 2.0f + 5, 3, 0, 6.2832f, ACCENT);
    }
  }
  if(p->user[0]) {
    draw_name_row_playing(name, small, TEXT_X, y, NAME_H, TEXT_W, p->name, p->badge, p->user, p->time, now,
                          p->group ? NULL : p->playing);
  } else {
    when[0] = 0;
    if(p->time) {
      age(p->time, now, when, sizeof when);
    }
    w = when[0] ? width_of(small, when) + 22 : 0;
    if(when[0]) {
      text(small, right - width_of(small, when), baseline(name, y, NAME_H), when, GREY);
    }
    text_fitted(name, TEXT_X, baseline(name, y, NAME_H), p->name[0] ? p->name : L("Grupo", "Group"),
                TEXT_W - w, FG);
  }

  // the last message, who wrote it first: the user, or in a group its author
  y = top + c->body_y;
  if(p->text[0]) {
    snprintf(line, sizeof line, "%s%s%s%.*s", p->mine ? L("Tu: ", "You: ") : "", from, from[0] ? ": " : "",
             dm_shown(p), p->text);
  } else {
    snprintf(line, sizeof line, "%s%s%s%s", p->mine ? L("Tu: ", "You: ") : "", from, from[0] ? ": " : "",
             carries);
  }
  for(char *s=line; *s; s++) {
    *s = *s == '\n' || *s == '\r' || *s == '\t' ? ' ' : *s;
  }
  if(p->unread) {
    draw_circle(&g_dst, right - 9, y + LINE_H / 2.0f, 9, BLUE);
    right -= 34;
  }
  if(p->request) {
    label = L("Pedido", "Request");
    w = width_of(bold, label) + 26;
    draw_box(&g_dst, right - w, y + 5, w, LINE_H - 10, (LINE_H - 10) / 2.0f, 0, CHIP_ON);
    text(bold, right - w + 13, baseline(bold, y + 5, LINE_H - 10), label, FG);
    right -= w + 16;
  }
  if(line[0] && right > TEXT_X + 40) {
    text_fitted(body, TEXT_X, baseline(body, y, LINE_H), line, right - TEXT_X, p->unread ? FG : GREY);
  }
}


/**
 * a message of a conversation: its bubble, blue for the user's own, the
 * picture of who sent the others', and when
 **/
static void
draw_dm(const card_t *c, int index, int top, int selected, long long now) {
  const x_post_t *p = c->post;
  text_font_t *body = font(F_BODY), *small = font(F_SMALL), *bold = font(F_SMALL_BOLD);
  uint32_t ink = p->mine ? WHITE : FG;
  int x = c->bubble_x, y = top + c->bubble_y, ty, icon, fy = top + c->avatar_y;
  const char *carries = dm_carries(p->icon, &icon);
  char when[48];

  zone(Z_CARD, index, 0, x, y, c->bubble_w, c->bubble_h);
  if(p->group && !p->mine) {
    text_fitted(bold, x + 8, baseline(bold, y - 38, 36), p->name[0] ? p->name : p->user, BUBBLE_MAX, GREY);
  }
  if(!p->mine) {
    draw_avatar(p->avatar, p->user, p->name, MAIN_X + CARD_PAD, fy, FACE);
    if(p->user_id[0] && zone(Z_AUTHOR, index, 0, MAIN_X + CARD_PAD - 4, fy - 4, FACE + 8, FACE + 8)) {
      draw_arc(&g_dst, MAIN_X + CARD_PAD + FACE / 2.0f, fy + FACE / 2.0f, FACE / 2.0f + 4, 3, 0, 6.2832f, ACCENT);
    }
  }
  if(selected) {
    draw_box(&g_dst, x - 5, y - 5, c->bubble_w + 10, c->bubble_h + 10, BUBBLE_R + 5, 2.5f,
             p->mine ? RGBA(255, 255, 255, 150) : ACCENT);
  }
  draw_box(&g_dst, x, y, c->bubble_w, c->bubble_h, BUBBLE_R, 0, p->mine ? BLUE : RGBA(255, 255, 255, 38));
  ty = top + c->body_y;
  for(int i=0; i < c->lines; i++, ty += LINE_H) {
    text_draw(body, &g_dst, x + BUBBLE_PAD, baseline(body, ty, LINE_H), p->text + c->start[i], c->len[i], ink);
  }
  if(c->quote_lines && icon >= 0) {
    draw_icon(&g_dst, icon, x + BUBBLE_PAD, ty + (LINE_H - 30) / 2, 30, ink);
    text(body, x + BUBBLE_PAD + 40, baseline(body, ty, LINE_H), carries, ink);
  }
  when[0] = 0;
  if(p->time) {
    age(p->time, now, when, sizeof when);
  }
  if(when[0]) {
    ty = top + c->actions_y;
    text(small, p->mine ? x + c->bubble_w - width_of(small, when) - 8 : x + 8, baseline(small, ty, DM_TIME_H),
         when, GREY);
  }
}


/**
 * a trend of Explore: its rank and where it trends, its name, how much it is
 * posted, and a story's picture
 **/
static void
draw_trend(const card_t *c, int top) {
  const x_post_t *p = c->post;
  text_font_t *small = font(F_SMALL), *name = font(F_NAME);
  int x = MAIN_X + CARD_PAD + 8, w = MAIN_W - 2 * CARD_PAD - 16, y = top + PAD_TOP, px, end;
  char line[200], buf[IMAGES_URL_MAX];
  const uint32_t *pixels;

  if(p->avatar[0]) {
    px = MAIN_X + MAIN_W - CARD_PAD - TREND_PIC;
    if((pixels=picture(media_url(p->avatar, "small", buf, sizeof buf), TREND_PIC, TREND_PIC))) {
      draw_picture_round(&g_dst, px, y, TREND_PIC, TREND_PIC, 20, pixels);
    } else {
      draw_box(&g_dst, px, y, TREND_PIC, TREND_PIC, 20, 0, PLACEHOLDER);
    }
    w -= TREND_PIC + 20;
  }
  if(p->icon > 0 && p->quote_name[0]) {
    snprintf(line, sizeof line, "%d \xc2\xb7 %s", p->icon, p->quote_name);
  } else if(p->quote_name[0]) {
    snprintf(line, sizeof line, "%s", p->quote_name);
  } else if(p->icon > 0) {
    snprintf(line, sizeof line, L("%d \xc2\xb7 Tend\xc3\xaa" "ncia", "%d \xc2\xb7 Trending"), p->icon);
  } else {
    snprintf(line, sizeof line, "%s", p->group ? L("Not\xc3\xad" "cias", "News") : L("Tend\xc3\xaa" "ncia", "Trending"));
  }
  text_fitted(small, x, baseline(small, y, TREND_INFO), line, w, GREY);
  y = top + c->body_y;
  for(int i=0; i < c->lines; i++, y += LINE_H) {
    end = text_draw(name, &g_dst, x, baseline(name, y, LINE_H), p->name + c->start[i], c->len[i], FG);
    if(c->more && i == c->lines - 1) {
      text(name, end, baseline(name, y, LINE_H), "\xe2\x80\xa6", FG);
    }
  }
  if(p->quote_text[0]) {
    text_fitted(small, x, baseline(small, top + c->actions_y, TREND_INFO), p->quote_text, w, GREY);
  }
}


/**
 * how many a number of people is, in a few words: "42 membros"
 **/
static void
people_text(long long n, const char *one, const char *many, char *buf, size_t size) {
  char count[32];

  count_text(n, count, sizeof count);
  snprintf(buf, size, "%s %s", count, n == 1 ? one : many);
}


/**
 * a list: its banner, its name (with a padlock when private), whose it is,
 * its description, and its members and followers
 **/
static void
draw_list_card(const card_t *c, int top) {
  const x_post_t *p = c->post;
  text_font_t *name = font(F_NAME), *small = font(F_SMALL), *bold = font(F_SMALL_BOLD);
  int px = MAIN_X + CARD_PAD, y = top + PAD_TOP, x = px + LIST_PIC + 22, right = MAIN_X + MAIN_W - CARD_PAD, end;
  char line[200], part[80], buf[IMAGES_URL_MAX];
  const uint32_t *pixels;

  if(p->avatar[0] && (pixels=picture(media_url(p->avatar, "small", buf, sizeof buf), LIST_PIC, LIST_PIC))) {
    draw_picture_round(&g_dst, px, y, LIST_PIC, LIST_PIC, 20, pixels);
  } else {
    draw_box(&g_dst, px, y, LIST_PIC, LIST_PIC, 20, 0, p->avatar[0] ? PLACEHOLDER : CHIP_ON);
    if(!p->avatar[0]) {
      draw_icon(&g_dst, ICON_LIST, px + (LIST_PIC - 44) / 2, y + (LIST_PIC - 44) / 2, 44, GREY);
    }
  }
  end = text_fitted(name, x, baseline(name, y, NAME_H), p->name, right - x - (p->request ? 40 : 0), FG);
  if(p->request) {
    draw_icon(&g_dst, ICON_LOCK, end + 8, y + (NAME_H - 28) / 2, 28, GREY);
  }
  y += NAME_H;
  if(p->user[0]) {
    draw_avatar(p->quote_avatar, p->user, p->quote_name, x, y + (TREND_INFO - LIST_FACE) / 2 + 2, LIST_FACE);
    end = text_fitted(bold, x + LIST_FACE + 10, baseline(bold, y + 2, TREND_INFO), p->quote_name[0] ? p->quote_name :
                      p->user, (right - x) / 2, FG);
    if(p->badge) {
      draw_badge(p->badge, end + 6, y + 2 + (TREND_INFO - 24) / 2, 24);
      end += 34;
    }
    snprintf(line, sizeof line, "@%s", p->user);
    text_fitted(small, end + 10, baseline(small, y + 2, TREND_INFO), line, right - end - 10, GREY);
  }
  y = top + c->body_y;
  for(int i=0; i < c->lines; i++, y += TREND_INFO) {
    end = text_draw(small, &g_dst, x, baseline(small, y, TREND_INFO), p->quote_text + c->start[i], c->len[i], FG);
    if(c->more && i == c->lines - 1) {
      text(small, end, baseline(small, y, TREND_INFO), "\xe2\x80\xa6", FG);
    }
  }
  line[0] = 0;
  if(p->likes >= 0) {
    people_text(p->likes, L("membro", "member"), L("membros", "members"), part, sizeof part);
    snprintf(line, sizeof line, "%s", part);
  }
  if(p->reposts >= 0) {
    people_text(p->reposts, L("seguidor", "follower"), L("seguidores", "followers"), part, sizeof part);
    snprintf(line + strlen(line), sizeof line - strlen(line), "%s%s", line[0] ? " \xc2\xb7 " : "", part);
  }
  if(p->following) {
    snprintf(line + strlen(line), sizeof line - strlen(line), "%s%s", line[0] ? " \xc2\xb7 " : "",
             L("A seguir", "Following"));
  }
  if(p->pinned) {
    snprintf(line + strlen(line), sizeof line - strlen(line), "%s%s", line[0] ? " \xc2\xb7 " : "",
             L("Fixada", "Pinned"));
  }
  text_fitted(small, x, baseline(small, top + c->actions_y, TREND_INFO), line, right - x, GREY);
}


/**
 * "Rising" on a post climbing fast (see mark_rising), its row NAME_H high
 * from top, ending at right; how wide it is
 **/
static int
draw_rising(int right, int top) {
  text_font_t *f = font(F_SMALL_BOLD);
  const char *label = L("A subir", "Rising");
  int h = 36, icon = 24, w = 14 + icon + 7 + width_of(f, label) + 16, x = right - w;
  int y = top + (NAME_H - h) / 2;

  draw_box(&g_dst, x, y, w, h, h / 2.0f, 0, RGBA(242, 122, 26, 46));
  draw_icon(&g_dst, ICON_RISING, x + 14, y + (h - icon) / 2, icon, ACCENT);
  text(f, x + 14 + icon + 7, baseline(f, y, h), label, ACCENT);
  return w;
}


/**
 * the cover over a post that may give the game that runs away (covered)
 **/
static void
draw_cover(const card_t *c, int top) {
  text_font_t *f = font(F_SIDE_BOLD), *small = font(F_SMALL);
  const int x = TEXT_X, y = top + c->body_y + 2, w = TEXT_W, h = COVER_H - 14, cy = y + h / 2;
  char line[GAME_NAME + 64];

  draw_box(&g_dst, x, y, w, h, 24, 0, RGBA(255, 255, 255, 16));
  draw_box(&g_dst, x, y, w, h, 24, 1.5f, RGBA(255, 255, 255, 46));
  draw_circle(&g_dst, x + 50, cy, 30, RGBA(242, 122, 26, 46));
  draw_icon(&g_dst, ICON_EYE_OFF, x + 32, cy - 18, 36, ACCENT);
  snprintf(line, sizeof line, L("Pode ter spoilers de %s", "May have spoilers for %s"), g_spoil_name);
  text_fitted(f, x + 100, cy - 6, line, w - 124, FG);
  text_fitted(small, x + 100, cy + 28,
              L("\xe2\x9c\x95 para ver \xc2\xb7 o modo anti-spoiler est\xc3\xa1 nas Defini\xc3\xa7\xc3\xb5" "es",
                "\xe2\x9c\x95 to see it \xc2\xb7 anti-spoiler mode is in Settings"),
              w - 124, GREY);
}


static void
draw_card(const card_t *c, int index, int top, int selected, long long now) {
  const x_post_t *p = c->post;
  text_font_t *body = font(F_BODY), *small = font(F_SMALL_BOLD);
  const char *more = L("Mostrar mais", "Show more");
  char line[200];
  int y, end, cx = AVATAR_X + AVATAR / 2, w, over;

  if(p->kind == X_KIND_DM) {
    draw_dm(c, index, top, selected, now);
    return;
  }
  over = zone(Z_CARD, index, 0, MAIN_X, top, MAIN_W, c->height - CARD_GAP);
  if(selected) {
    // brighter under the pointer, which selects the post it is on
    draw_box(&g_dst, MAIN_X, top, MAIN_W, c->height - CARD_GAP, CARD_R, 0, SELECTED);
    draw_box(&g_dst, MAIN_X, top, MAIN_W, c->height - CARD_GAP, CARD_R, over ? 2 : 1.5f,
             over ? HOVER_CARD : EDGE);
  }
  if(p->kind == X_KIND_NOTICE) {
    draw_notice(c, index, top, now);
    return;
  }
  if(p->kind == X_KIND_CHAT) {
    draw_chat(c, index, top, now);
    return;
  }
  if(p->kind == X_KIND_TREND) {
    draw_trend(c, top);
    return;
  }
  if(p->kind == X_KIND_LIST) {
    draw_list_card(c, top);
    return;
  }

  if(p->reposter[0] || p->pinned) {
    y = top + PAD_TOP;
    draw_icon(&g_dst, p->pinned ? ICON_TOP : ICON_REPOST, AVATAR_X + AVATAR - 30,
              y + (REPOST_H - 30) / 2 - 2, 30, GREY);
    if(p->pinned) {
      snprintf(line, sizeof line, "%s", L("Post fixado", "Pinned"));
    } else {
      snprintf(line, sizeof line, L("%s repostou", "%s reposted"),
               p->reposter_name[0] ? p->reposter_name : p->reposter);
    }
    text_fitted(small, TEXT_X, baseline(small, y, REPOST_H - 4), line, TEXT_W, GREY);
  }

  // the line down from the avatar to the buttons, as in a thread
  y = top + c->avatar_y + AVATAR + 12;
  end = top + c->actions_y + ACTIONS_H / 2;
  if(end - y > 16) {
    draw_box(&g_dst, cx - 1, y, 3, end - y, 1.5f, 0, THREAD);
  }

  // the avatar and the name open the profile
  y = top + c->avatar_y;
  draw_avatar(p->avatar, p->user, p->name, AVATAR_X, y, AVATAR);
  if(p->playing[0]) {
    playing_badge(AVATAR_X, y, AVATAR);
  }
  w = width_of(font(F_NAME), p->name[0] ? p->name : p->user) + 12;
  w = w > TEXT_W / 2 ? TEXT_W / 2 : w;
  over = zone(Z_AUTHOR, index, 0, TEXT_X, y, w, NAME_H);
  if(zone(Z_AUTHOR, index, 0, AVATAR_X - 6, y - 6, AVATAR + 12, AVATAR + 12) || over) {
    draw_arc(&g_dst, cx, y + AVATAR / 2.0f, AVATAR / 2.0f + 5, 3, 0, 6.2832f, ACCENT);
  }
  if(over) {
    // the name underlined, as on x.com
    draw_rect(&g_dst, TEXT_X, baseline(font(F_NAME), y, NAME_H) + 5, w - 12, 2, FG);
  }
  w = TEXT_W - (p->id[0] ? 56 : 0);
  if(c->rising) {
    w -= draw_rising(TEXT_X + w, y) + 16;
  }
  draw_name_row_playing(font(F_NAME), font(F_SMALL), TEXT_X, y, NAME_H, w, p->name, p->badge, p->user, p->time,
                        now, p->playing);
  // its menu, at the right of the name, as on x.com
  if(p->id[0]) {
    int dx = TEXT_X + TEXT_W - 26, dy = y + NAME_H / 2;
    if(zone(Z_DOTS, index, 0, dx - 28, dy - 28, 56, 56)) {
      lit_circle(dx, dy, 28, 1);
    }
    draw_icon(&g_dst, ICON_DOTS, dx - 16, dy - 16, 32, GREY);
  }

  y = top + c->body_y;
  for(int i=0; i < c->lines; i++, y += LINE_H) {
    if(y > g_dst.height) {
      break;
    }
    end = text_draw(body, &g_dst, TEXT_X, baseline(body, y, LINE_H), p->text + c->start[i],
                    c->len[i], FG);
    if(c->more && c->open && i == c->lines - 1) {
      text(body, end, baseline(body, y, LINE_H), "\xe2\x80\xa6", FG);
    }
  }
  if(c->more && !c->open) {
    text(body, TEXT_X, baseline(body, y, LINE_H), more, ACCENT);
    if(zone(Z_MORE, index, 0, TEXT_X, y, width_of(body, more), LINE_H)) {
      draw_rect(&g_dst, TEXT_X, y + LINE_H - 7, width_of(body, more), 2, ACCENT);
    }
  }

  if(c->spoiler) {
    draw_cover(c, top);
  }
  if(c->media_h) {
    draw_media(c, index, top, g_frame_no);
  }
  if(c->poll_h) {
    draw_poll(c, index, top, now);
  }
  if(c->quote_h) {
    draw_quote(c, top, now);
  }
  draw_actions(c, index, top, selected);
}


/**
 * ask for the pictures of a post before it scrolls into view
 **/
static void
want_pictures(const card_t *c) {
  const x_post_t *p = c->post;
  char buf[IMAGES_URL_MAX];

  if(p->kind == X_KIND_NOTICE) {
    for(int i=0; i < p->face_count; i++) {
      if(p->faces[i][0]) {
        picture(avatar_url(p->faces[i], buf, sizeof buf), FACE, FACE);
      }
    }
    return;
  }
  if(p->kind == X_KIND_DM) {
    if(p->avatar[0] && !p->mine) {
      picture(avatar_url(p->avatar, buf, sizeof buf), FACE, FACE);
    }
    return;
  }
  if(p->kind == X_KIND_CHAT && p->group && !p->avatar[0]) {
    for(int i=0; i < p->face_count && i < 2; i++) {
      picture(avatar_url(p->faces[i], buf, sizeof buf), p->face_count >= 2 ? GROUP_FACE : AVATAR,
                 p->face_count >= 2 ? GROUP_FACE : AVATAR);
    }
    return;
  }
  if(p->avatar[0]) {
    picture(avatar_url(p->avatar, buf, sizeof buf), AVATAR, AVATAR);
  }
  for(int i=0; i < (c->spoiler ? 0 : p->media_count); i++) {
    picture(media_url(p->media[i].url, p->media_count == 1 ? "medium" : "small", buf, sizeof buf),
               c->media[i].w, c->media[i].h);
  }
}


/**
 * "Your week on X", under the user's own profile (top): their posts of the
 * last seven days that came, with their likes, reposts, replies and views,
 * and the one liked most; "12+" while older ones may still come
 **/
static void
draw_week(const tab_t *t, int top) {
  text_font_t *head = font(F_SIDE_BOLD), *small = font(F_SMALL), *num = font(F_TITLE);
  const int x = MAIN_X + 22, w = MAIN_W - 44, h = WEEK_H - 26, cw = (w - 56) / 5;
  const char *labels[5] = {
    "posts", L("gostos", "likes"), "reposts", L("respostas", "replies"), L("visualiza\xc3\xa7\xc3\xb5" "es", "views"),
  };
  const long long since = g_now - 7 * 86400;
  long long sum[5] = {0, 0, 0, 0, 0};
  const x_post_t *best = NULL;
  char n[40], line[1100];
  int older = 0, again, cx, y;

  for(int i=0; i < t->count; i++) {
    const x_post_t *p = &t->posts[i];
    if(p->kind != X_KIND_POST || p->reposter[0] || (strcmp(p->user, t->user.user) && !own_post(p))) {
      continue;
    }
    if(p->time < since) {
      older |= !p->pinned;
      continue;
    }
    again = 0;
    for(int j=0; j<i; j++) {
      again |= !strcmp(t->posts[j].id, p->id); // the pinned one, again in its place
    }
    if(again) {
      continue;
    }
    sum[0]++;
    sum[1] += p->likes > 0 ? p->likes : 0;
    sum[2] += p->reposts > 0 ? p->reposts : 0;
    sum[3] += p->replies > 0 ? p->replies : 0;
    sum[4] += p->views > 0 ? p->views : 0;
    best = !best || p->likes > best->likes ? p : best;
  }

  draw_box(&g_dst, x, top, w, h, CARD_R, 0, RGBA(255, 255, 255, 14));
  draw_box(&g_dst, x, top, w, h, CARD_R, 1.5f, RGBA(255, 255, 255, 34));
  draw_circle(&g_dst, x + 48, top + 44, 26, RGBA(242, 122, 26, 46));
  draw_icon(&g_dst, ICON_POLL, x + 32, top + 28, 32, ACCENT);
  text(head, x + 90, baseline(head, top + 20, 48), L("A tua semana no X", "Your week on X"), FG);
  snprintf(n, sizeof n, "%s", L("\xc3\x9altimos 7 dias", "Last 7 days"));
  text(small, x + w - 28 - width_of(small, n), baseline(small, top + 20, 48), n, GREY);

  y = top + 82;
  for(int i=0; i<5; i++) {
    cx = x + 28 + i * cw;
    count_text(sum[i], n, sizeof n);
    if(!older && t->cursor[0] && sum[0]) {
      snprintf(n + strlen(n), sizeof n - strlen(n), "+"); // more of the week may come
    }
    text_fitted(num, cx, baseline(num, y, 60), n, cw - 16, i == 1 && sum[1] ? ACCENT : FG);
    text_fitted(small, cx, baseline(small, y + 56, 34), labels[i], cw - 16, GREY);
  }

  y = top + h - 56;
  draw_rect(&g_dst, x + 28, y - 6, w - 56, 2, RGBA(255, 255, 255, 22));
  if(best && best->likes > 0) {
    char shown[300];
    size_t k = 0;
    // its first words, on one line
    for(const char *c=best->text; *c && k + 1 < sizeof shown; c++) {
      shown[k++] = *c == '\n' ? ' ' : *c;
    }
    shown[k] = 0;
    count_text(best->likes, n, sizeof n);
    draw_icon(&g_dst, ICON_LIKED, x + w - 28 - width_of(font(F_SMALL_BOLD), n) - 38, y + 12, 28, RED);
    text(font(F_SMALL_BOLD), x + w - 28 - width_of(font(F_SMALL_BOLD), n), baseline(small, y, 52), n, FG);
    snprintf(line, sizeof line, L("Mais gostado: %s", "Most liked: %s"),
             shown[0] ? shown : L("um post com fotos", "a post with pictures"));
    text_fitted(small, x + 28, baseline(small, y, 52), line, w - 56 - width_of(font(F_SMALL_BOLD), n) - 64, GREY);
  } else {
    text_fitted(small, x + 28, baseline(small, y, 52),
                sum[0] ? L("Ainda sem gostos esta semana \xc2\xb7 \xe2\x96\xa1 atualiza",
                           "No likes yet this week \xc2\xb7 \xe2\x96\xa1 refreshes") :
                         L("Ainda n\xc3\xa3o publicaste nada esta semana \xc2\xb7 \xe2\x98\xb0 escreve um post",
                           "Nothing posted this week yet \xc2\xb7 \xe2\x98\xb0 writes a post"),
                w - 56, GREY);
  }
}


/**
 * "Juntou-se em nov de 2020", "Joined Nov 2020"
 **/
static void
joined_text(long long t, char *buf, size_t size) {
  time_t when = (time_t)t;
  struct tm *tm = gmtime(&when);

  if(!tm) {
    buf[0] = 0;
    return;
  }
  snprintf(buf, size, L("Juntou-se em %s de %d", "Joined %s %d"), lang_month(g_lang, tm->tm_mon),
           tm->tm_year + 1900);
}


/**
 * the game a profile's location says (play_game_of), into game. X keeps 30
 * characters of a location, so a long name comes back cut ("Uncharted™:
 * Drake's…"); on the user's own profile, when the location is the one the
 * game this console runs gives (play_status, as the payload wrote it), that
 * game's whole name. Someone else's stays as X has it: another game can
 * start with the same 20 characters. 1, or 0 when it says no game.
 **/
static int
profile_game(const x_user_t *u, char *game, size_t size) {
  char mine[PLAY_STATUS];

  if(!play_game_of(u->location, game, size)) {
    return 0;
  }
  if(g_game.name[0] && g_tabs[UI_ME].has_user && u->id[0] && !strcmp(g_tabs[UI_ME].user.id, u->id) &&
     !play_status(g_game.name, mine, sizeof mine) && !strcmp(mine, u->location)) {
    snprintf(game, size, "%s", g_game.name);
  }
  return 1;
}


/**
 * whether a profile's game takes a line of its own, over the date they
 * joined: when "Playing <game>  ·  Joined <month>" with the pad is wider
 * than the profile
 **/
static int
game_line_alone(const x_user_t *u) {
  text_font_t *small = font(F_SMALL);
  char game[GAME_NAME], what[GAME_NAME + 48], joined[64], line[80];

  if(!u->joined || !profile_game(u, game, sizeof game)) {
    return 0;
  }
  joined_text(u->joined, joined, sizeof joined);
  snprintf(what, sizeof what, L("A jogar %s", "Playing %s"), game);
  snprintf(line, sizeof line, "  \xc2\xb7  %s", joined);
  // the pad's 30 and 10 after it
  return 40 + width_of(small, what) + width_of(small, line) > MAIN_W - 2 * CARD_PAD;
}


/**
 * a profile on top of its posts: the banner, the picture over its edge, the
 * names, the bio, where and since when, and the counts
 **/
static void
draw_profile(const tab_t *t, int top) {
  const x_user_t *u = &t->user;
  text_font_t *title = font(F_TITLE), *small = font(F_SMALL), *body = font(F_BODY);
  text_font_t *bold = font(F_SMALL_BOLD);
  char buf[IMAGES_URL_MAX], line[200], joined[64], n[32], game[GAME_NAME];
  const uint32_t *pixels;
  int x, y = top + BANNER_H - BIG_AVATAR / 2, size;

  if(u->banner[0] && (pixels=picture(banner_url(u->banner, buf, sizeof buf), MAIN_W, BANNER_H))) {
    draw_picture_round(&g_dst, MAIN_X, top, MAIN_W, BANNER_H, CARD_R, pixels);
  } else {
    draw_box(&g_dst, MAIN_X, top, MAIN_W, BANNER_H, CARD_R, 0,
             u->banner[0] ? PLACEHOLDER : RGBA(242, 122, 26, 70));
  }
  draw_circle(&g_dst, MAIN_X + 36 + BIG_AVATAR / 2.0f, y + BIG_AVATAR / 2.0f, BIG_AVATAR / 2.0f + 6,
              RGBA(62, 52, 46, 255));
  draw_avatar(u->avatar, u->user, u->name, MAIN_X + 36, y, BIG_AVATAR);
  if(play_game_of(u->location, game, sizeof game)) {
    playing_badge(MAIN_X + 36, y, BIG_AVATAR);
  }

  // someone else's: Follow (Following, Blocked), and its menu
  if(t->feed == UI_USER && u->id[0] && !(g_tabs[UI_ME].has_user && !strcmp(g_tabs[UI_ME].user.id, u->id))) {
    text_font_t *pf = font(F_PILL);
    const char *label = u->blocked ? L("Bloqueado", "Blocked") : u->followed ? L("A seguir", "Following") :
                        L("Seguir", "Follow");
    const int bh = 58, by = top + BANNER_H + 18;
    int bw = width_of(pf, label) + 56, bx = MAIN_X + MAIN_W - CARD_PAD - bw, dx = bx - 14 - bh, over;

    over = zone(Z_FOLLOW, 0, 0, bx, by, bw, bh);
    if(u->followed || u->blocked) {
      draw_box(&g_dst, bx, by, bw, bh, bh / 2.0f, 0, CHIP);
      draw_box(&g_dst, bx, by, bw, bh, bh / 2.0f, 1.5f, RGBA(255, 255, 255, 90));
      if(over) {
        lit_box(bx, by, bw, bh, bh / 2.0f, 1);
      }
      text(pf, bx + 28, baseline(pf, by, bh), label, u->blocked ? RED : FG);
    } else {
      draw_box(&g_dst, bx, by, bw, bh, bh / 2.0f, 0, over ? RGBA(225, 225, 225, 255) : WHITE);
      if(over) {
        lit_box(bx, by, bw, bh, bh / 2.0f, 0);
      }
      text(pf, bx + 28, baseline(pf, by, bh), label, INK);
    }
    over = zone(Z_UMENU, 0, 0, dx, by, bh, bh);
    draw_circle(&g_dst, dx + bh / 2.0f, by + bh / 2.0f, bh / 2.0f, CHIP);
    draw_arc(&g_dst, dx + bh / 2.0f, by + bh / 2.0f, bh / 2.0f - 1, 1.5f, 0, 6.2832f, RGBA(255, 255, 255, 90));
    if(over) {
      lit_circle(dx + bh / 2.0f, by + bh / 2.0f, bh / 2.0f, 1);
    }
    draw_icon(&g_dst, ICON_DOTS, dx + (bh - 32) / 2, by + (bh - 32) / 2, 32, FG);
  }

  y = top + BANNER_H + 78;
  size = text_line_height(title) - 10;
  x = text_fitted(title, MAIN_X + CARD_PAD, baseline(title, y, 56), u->name[0] ? u->name : u->user,
                  MAIN_W - 2 * CARD_PAD - size - 12, FG);
  if(u->badge) {
    draw_badge(u->badge, x + 10, y + (56 - size) / 2, size);
  }
  y += 56;
  snprintf(line, sizeof line, "@%s", u->user);
  x = text_fitted(small, MAIN_X + CARD_PAD, baseline(small, y, 40), line, MAIN_W - 2 * CARD_PAD - 200, GREY);
  if(u->follows_you) {
    const char *says = L("Segue-te", "Follows you");
    int cw = width_of(font(F_KEY), says) + 20;
    draw_box(&g_dst, x + 14, y + 5, cw, 30, 8, 0, CHIP_ON);
    text(font(F_KEY), x + 24, baseline(font(F_KEY), y + 5, 30), says, GREY);
  }
  y += 40;
  for(int i=0; i < t->bio_lines; i++, y += LINE_H) {
    text_draw(body, &g_dst, MAIN_X + CARD_PAD, baseline(body, y, LINE_H), u->bio + t->bio_start[i],
              t->bio_len[i], FG);
  }
  y += t->bio_lines ? 12 : 0;

  // where, and since when
  joined[0] = 0;
  if(u->joined) {
    joined_text(u->joined, joined, sizeof joined);
  }
  if(profile_game(u, game, sizeof game)) {
    // the game they play, which their location says while they play: the
    // whole width, and a line of its own over the date when both do not fit
    int is = 30, gx = MAIN_X + CARD_PAD, alone = t->game_alone;
    char what[GAME_NAME + 48];
    draw_icon(&g_dst, ICON_GAME, gx, y + (44 - is) / 2, is, GREEN);
    snprintf(what, sizeof what, L("A jogar %s", "Playing %s"), game);
    gx = text_fitted(small, gx + is + 10, baseline(small, y, 44), what, MAIN_W - 2 * CARD_PAD - is - 10, GREEN);
    if(alone) {
      y += 44;
      gx = MAIN_X + CARD_PAD;
    }
    snprintf(line, sizeof line, "%s%s", joined[0] && !alone ? "  \xc2\xb7  " : "", joined);
    text_fitted(small, gx, baseline(small, y, 44), line, MAIN_X + MAIN_W - CARD_PAD - gx, GREY);
  } else {
    snprintf(line, sizeof line, "%s%s%s", u->location,
             u->location[0] && joined[0] ? "  \xc2\xb7  " : "", joined);
    text_fitted(small, MAIN_X + CARD_PAD, baseline(small, y, 44), line, MAIN_W - 2 * CARD_PAD, GREY);
  }
  y += 44;

  // the counts, their numbers in bold
  x = MAIN_X + CARD_PAD;
  if(u->following >= 0) {
    count_text(u->following, n, sizeof n);
    x = text(bold, x, baseline(bold, y, 48), n, FG);
    x = text(small, x + 8, baseline(small, y, 48), L("A seguir", "Following"), GREY) + 34;
  }
  if(u->followers >= 0) {
    count_text(u->followers, n, sizeof n);
    x = text(bold, x, baseline(bold, y, 48), n, FG);
    x = text(small, x + 8, baseline(small, y, 48),
             u->followers == 1 ? L("Seguidor", "Follower") : L("Seguidores", "Followers"), GREY) + 34;
  }
  if(u->posts >= 0) {
    count_text(u->posts, n, sizeof n);
    x = text(bold, x, baseline(bold, y, 48), n, FG);
    text(small, x + 8, baseline(small, y, 48), u->posts == 1 ? "post" : "posts", GREY);
  }
  if(t->feed == UI_ME) {
    draw_week(t, top + t->header_h - WEEK_H);
  }
}


/*
 * The timeline.
 */

static void
spinner(float cx, float cy, float r, float t, unsigned frame, uint32_t color) {
  float a = frame * 0.11f;

  draw_arc(&g_dst, cx, cy, r, t, 0, 6.2832f, (color & 0xFFFFFFu) | 0x50000000u);
  draw_arc(&g_dst, cx, cy, r, t, a, a + 4.2f, color);
}


static void
error_text(const tab_t *t, char *title, size_t tsize, char *hint, size_t hsize) {
  // X error 326, with HTTP 200 and nothing in it: the account is locked
  // until it is unlocked on x.com (seen 2026-11-13 on a new login)
  if(t->x_code == 326 && (t->error == ERR_EMPTY || t->error == ERR_HTTP)) {
    snprintf(title, tsize, "%s", L("O X bloqueou esta conta por agora (erro 326)",
                                   "X has locked this account for now (error 326)"));
    snprintf(hint, hsize, "%s", L("Entra no x.com no telem\xc3\xb3vel ou no PC e faz o que o X pedir para a desbloquear. "
                                  "Depois carrega em \xe2\x96\xa1.",
                                  "Log in at x.com on your phone or a PC and do what X asks to unlock it. "
                                  "Then press \xe2\x96\xa1."));
    return;
  }
  switch(t->error) {
  case ERR_NO_SESSION:
    snprintf(title, tsize, "%s", L("Falta a tua sess\xc3\xa3o do X", "Your X session is missing"));
    if(g_login.ready) {
      snprintf(hint, hsize, "%s", L("Entra com a tua conta na p\xc3\xa1gina do pr\xc3\xb3prio X, aqui na PS5.",
                                    "Log in with your account on X's own page, here on the PS5."));
      break;
    }
    snprintf(hint, hsize, L("Liga a conta pelo PC, com a extens\xc3\xa3o XPS5 no Chrome ou no Edge. Tamb\xc3\xa9m d\xc3\xa1 "
                            "para copiar os cookies auth_token e ct0 do x.com para %s e carregar em \xe2\x96\xa1.",
                            "Link your account from a PC, with the XPS5 extension in Chrome or Edge. You can also "
                            "copy the auth_token and ct0 cookies of x.com into %s and press \xe2\x96\xa1."),
             X_SESSION_PATH);
    break;
  case ERR_NO_COOKIE:
    snprintf(title, tsize, L("Falta o cookie %s", "The %s cookie is missing"),
             t->code == -1 ? "auth_token" : "ct0");
    snprintf(hint, hsize, L("O ficheiro %s precisa do auth_token e do ct0, ou liga a conta pelo PC.",
                            "The file %s needs both auth_token and ct0, or link your account from a PC."),
             X_SESSION_PATH);
    break;
  case ERR_HTTPS:
    snprintf(title, tsize, L("A rede da PS5 n\xc3\xa3o arrancou (0x%08X)",
                             "The PS5 network did not start (0x%08X)"), (unsigned)t->code);
    snprintf(hint, hsize, "%s", L("Fecha o XPS5 e abre-o outra vez.",
                                  "Close XPS5 and open it again."));
    break;
  case ERR_NETWORK:
    snprintf(title, tsize, L("N\xc3\xa3o foi poss\xc3\xadvel ligar ao x.com (0x%08X)",
                             "Could not reach x.com (0x%08X)"), (unsigned)t->code);
    snprintf(hint, hsize, "%s", L("V\xc3\xaa se a PS5 tem internet e carrega em \xe2\x96\xa1.",
                                  "Check the PS5's internet connection and press \xe2\x96\xa1."));
    break;
  case ERR_HTTP:
    if(t->code == 401 || t->code == 403) {
      snprintf(title, tsize, L("O X n\xc3\xa3o aceitou a sess\xc3\xa3o (HTTP %d)",
                               "X did not accept the session (HTTP %d)"), t->code);
      snprintf(hint, hsize, "%s", g_login.ready ?
               L("A sess\xc3\xa3o pode ter terminado: entra outra vez no X.",
                 "The session may have ended: log in to X again.") :
               L("A sess\xc3\xa3o pode ter terminado: liga a conta outra vez pelo PC.",
                 "The session may have ended: link your account from a PC again."));
    } else if(t->code == 429) {
      snprintf(title, tsize, "%s", L("O X pediu para esperar (HTTP 429)",
                                     "X asked to wait (HTTP 429)"));
      snprintf(hint, hsize, "%s", L("Tenta outra vez daqui a uns minutos.",
                                    "Try again in a few minutes."));
    } else {
      snprintf(title, tsize, L("O X respondeu HTTP %d", "X answered HTTP %d"), t->code);
      if(t->x_code >= 0) {
        snprintf(hint, hsize, L("Erro %d do X. Carrega em \xe2\x96\xa1 para tentar outra vez.",
                                "X error %d. Press \xe2\x96\xa1 to try again."), t->x_code);
      } else {
        snprintf(hint, hsize, "%s", L("Carrega em \xe2\x96\xa1 para tentar outra vez.",
                                      "Press \xe2\x96\xa1 to try again."));
      }
    }
    break;
  case ERR_ANSWER:
    snprintf(title, tsize, "%s", L("O X mandou uma resposta que n\xc3\xa3o se percebe",
                                   "X sent an answer that cannot be read"));
    snprintf(hint, hsize, "%s", L("Carrega em \xe2\x96\xa1 para tentar outra vez.",
                                  "Press \xe2\x96\xa1 to try again."));
    break;
  case ERR_EMPTY:
    snprintf(title, tsize, "%s", L("Ainda n\xc3\xa3o h\xc3\xa1 nada aqui", "Nothing to see here yet"));
    if(t->feed == UI_FOR_YOU || t->feed == UI_FOLLOWING) {
      snprintf(hint, hsize, "%s", L("Segue algu\xc3\xa9m no X, ou carrega em \xe2\x96\xa1 para tentar outra vez.",
                                    "Follow someone on X, or press \xe2\x96\xa1 to try again."));
    } else if(t->feed == UI_SEARCH) {
      snprintf(title, tsize, "%s", L("Sem resultados", "No results"));
      snprintf(hint, hsize, "%s", L("O X n\xc3\xa3o encontrou nada. Carrega em \xe2\x9c\x95 para procurar outras palavras.",
                                    "X found nothing. Press \xe2\x9c\x95 to look for other words."));
    } else if(t->feed == UI_ARTICLES) {
      snprintf(title, tsize, "%s", L("Ainda n\xc3\xa3o h\xc3\xa1 artigos", "No Articles yet"));
      snprintf(hint, hsize, "%s", L("Os artigos que publicas no X aparecem aqui. Carrega em \xe2\x96\xa1 para ver outra vez.",
                                    "The Articles you publish on X show up here. Press \xe2\x96\xa1 to look again."));
    } else if(t->feed == UI_CHAT) {
      snprintf(title, tsize, "%s", L("Ainda n\xc3\xa3o h\xc3\xa1 conversas", "No conversations yet"));
      snprintf(hint, hsize, "%s", L("As tuas mensagens diretas aparecem aqui. As conversas cifradas do X Chat s\xc3\xb3 "
                                    "abrem nas apps do X. Carrega em \xe2\x96\xa1 para ver outra vez.",
                                    "Your direct messages show up here. Encrypted X Chat conversations only open "
                                    "in X's apps. Press \xe2\x96\xa1 to look again."));
    } else if(t->feed == UI_DM) {
      snprintf(title, tsize, "%s", L("Ainda n\xc3\xa3o h\xc3\xa1 mensagens", "No messages yet"));
      snprintf(hint, hsize, "%s", L("Carrega em L2 para escrever a primeira. Uma conversa cifrada do X Chat s\xc3\xb3 "
                                    "abre nas apps do X.",
                                    "Press L2 to write the first one. An encrypted X Chat conversation only opens "
                                    "in X's apps."));
    } else if(t->feed == UI_GAME) {
      snprintf(title, tsize, "%s", L("Ainda ningu\xc3\xa9m publicou sobre o jogo", "Nobody has posted about the game yet"));
      snprintf(hint, hsize, "%s", L("Escreve tu o primeiro com \xe2\x98\xb0, ou carrega em \xe2\x96\xa1 para ver outra vez.",
                                    "Write the first one with \xe2\x98\xb0, or press \xe2\x96\xa1 to look again."));
    } else if(t->feed == UI_BOOKMARKS) {
      snprintf(hint, hsize, "%s", L("Os posts que guardas no X aparecem aqui. Carrega em \xe2\x96\xa1 para ver outra vez.",
                                    "The posts you bookmark on X show up here. Press \xe2\x96\xa1 to look again."));
    } else if(t->feed == UI_LISTS) {
      snprintf(title, tsize, "%s", L("Ainda n\xc3\xa3o tens listas", "You have no Lists yet"));
      snprintf(hint, hsize, "%s", L("As listas que crias ou segues no X aparecem aqui. Carrega em \xe2\x96\xa1 para ver "
                                    "outra vez.",
                                    "The Lists you make or follow on X show up here. Press \xe2\x96\xa1 to look again."));
    } else if(t->feed == UI_LIST) {
      snprintf(title, tsize, "%s", L("Esta lista ainda n\xc3\xa3o tem posts", "This List has no posts yet"));
      snprintf(hint, hsize, "%s", L("Os posts das contas da lista aparecem aqui. Carrega em \xe2\x96\xa1 para ver "
                                    "outra vez.",
                                    "Posts from the accounts in the List show up here. Press \xe2\x96\xa1 to look "
                                    "again."));
    } else {
      snprintf(hint, hsize, "%s", L("Carrega em \xe2\x96\xa1 para tentar outra vez.",
                                    "Press \xe2\x96\xa1 to try again."));
    }
    break;
  default:
    snprintf(title, tsize, "%s", L("Falta mem\xc3\xb3ria", "Out of memory"));
    snprintf(hint, hsize, "%s", L("Fecha o XPS5 e abre-o outra vez.",
                                  "Close XPS5 and open it again."));
    break;
  }
}


static void
centred(text_font_t *f, int top, int h, const char *s, uint32_t color) {
  text_fitted(f, MAIN_X + (MAIN_W - width_of(f, s)) / 2, baseline(f, top, h), s,
              MAIN_W - 2 * CARD_PAD, color);
}


/**
 * under the last post: the spinner of the posts after it, why they did not
 * come, or the end
 **/
static void
draw_footer(const tab_t *t, int top, unsigned frame) {
  char line[160];

  if(footer_spins(t)) {
    spinner(MAIN_X + MAIN_W / 2.0f, top + FOOTER_H / 2.0f, 22, 5, frame, ACCENT);
    return;
  }
  if(t->feed == UI_CHAT || t->feed == UI_DM) {
    return; // all of them came at once
  }
  if(t->more_error) {
    if(t->more_error == ERR_HTTP) {
      snprintf(line, sizeof line, L("N\xc3\xa3o vieram mais posts (HTTP %d)",
                                    "No more posts came (HTTP %d)"), t->more_code);
    } else if(t->more_error == ERR_NETWORK) {
      snprintf(line, sizeof line, "%s", L("N\xc3\xa3o vieram mais posts (sem liga\xc3\xa7\xc3\xa3o)",
                                          "No more posts came (no connection)"));
    } else {
      snprintf(line, sizeof line, "%s", L("N\xc3\xa3o vieram mais posts", "No more posts came"));
    }
    centred(font(F_BODY), top + 24, 50, line, FG);
    centred(font(F_SMALL), top + 74, 44, L("Carrega em \xe2\x86\x93 para tentar outra vez.",
                                          "Press \xe2\x86\x93 to try again."), GREY);
    return;
  }
  if(t->count >= MAX_POSTS) {
    snprintf(line, sizeof line, L("Chegaste aos %d posts que o XPS5 guarda",
                                  "You reached the %d posts XPS5 keeps"), MAX_POSTS);
  } else if(t->feed == UI_LISTS) {
    snprintf(line, sizeof line, "%s", L("N\xc3\xa3o h\xc3\xa1 mais listas", "There are no more Lists"));
  } else if(t->feed == UI_SEARCH && !t->query[0]) {
    snprintf(line, sizeof line, "%s", L("Por agora \xc3\xa9 tudo", "That is all for now"));
  } else {
    snprintf(line, sizeof line, "%s", L("N\xc3\xa3o h\xc3\xa1 mais posts para mostrar",
                                        "There are no more posts to show"));
  }
  centred(font(F_SMALL), top + 24, 50, line, GREY);
}


/**
 * "You left off here" over the first post that was on top of Following the
 * last time, with how many came since: a pill on a line across, which fades
 * out to the sides; top is where its room starts
 **/
static void
draw_seen(const tab_t *t, int top) {
  text_font_t *f = font(F_SMALL_BOLD);
  char line[160];
  int h = 46, icon = 26, w, x, y = top + (SEEN_H - CARD_GAP - h) / 2, mid = y + h / 2, l, r;
  int left = MAIN_X + CARD_PAD, right = MAIN_X + MAIN_W - CARD_PAD;

  if(t->seen_at == 1) {
    snprintf(line, sizeof line, "%s", L("Ficaste por aqui \xc2\xb7 1 post novo acima",
                                        "You left off here \xc2\xb7 1 new post above"));
  } else {
    snprintf(line, sizeof line, L("Ficaste por aqui \xc2\xb7 %d posts novos acima",
                                  "You left off here \xc2\xb7 %d new posts above"), t->seen_at);
  }
  w = 20 + icon + 10 + width_of(f, line) + 24;
  w = w > right - left - 80 ? right - left - 80 : w;
  x = MAIN_X + (MAIN_W - w) / 2;
  // the line, strongest by the pill
  for(int i=0; i < 16; i++) {
    uint32_t color = RGBA(242, 122, 26, 12 + i * 9);
    l = left + (x - 16 - left) * i / 16;
    r = left + (x - 16 - left) * (i + 1) / 16;
    draw_rect(&g_dst, l, mid - 1, r - l, 2, color);
    l = right - (right - x - w - 16) * (i + 1) / 16;
    r = right - (right - x - w - 16) * i / 16;
    draw_rect(&g_dst, l, mid - 1, r - l, 2, color);
  }
  draw_box(&g_dst, x, y, w, h, h / 2.0f, 0, RGBA(242, 122, 26, 44));
  draw_box(&g_dst, x, y, w, h, h / 2.0f, 1.5f, RGBA(242, 122, 26, 150));
  draw_icon(&g_dst, ICON_TOP, x + 20, y + (h - icon) / 2, icon, ACCENT);
  text_fitted(f, x + 20 + icon + 10, baseline(f, y, h), line, w - 20 - icon - 10 - 20, FG);
}


static void
draw_list(const tab_t *t, long long now, unsigned frame) {
  int top = LIST_PAD - t->scroll;

  if(t->header_h && top + t->header_h > 0) {
    draw_profile(t, top);
  }
  for(int i=0; i < t->count; i++) {
    const card_t *c = &t->cards[i];
    top = LIST_PAD + c->y - t->scroll;
    if(i && i == t->seen_at && top > 0 && top - SEEN_H < LIST_H) {
      draw_seen(t, top - SEEN_H);
    }
    if(top + c->height > 0 && top < LIST_H) {
      draw_card(c, i, top, i == t->sel, now);
    } else if(top >= LIST_H && top < 2 * LIST_H) {
      want_pictures(c);
    }
  }
  if((top=footer_top(t)) < LIST_H) {
    draw_footer(t, top, frame);
  }
}


/**
 * a page of a section X keeps to its own apps: what it is, and where
 **/
static void
draw_page(const tab_t *t) {
  const tab_t *me = &g_tabs[UI_ME];
  const char *title = list_title(t), *says;
  int icon, cx = MAIN_X + MAIN_W / 2, y;

  switch(t->feed) {
  case UI_CALLS:
    icon = ICON_CALL;
    says = L("As chamadas de voz e de v\xc3\xad" "deo do X fazem-se no Chat, que s\xc3\xb3 existe nas apps do X.",
             "X's voice and video calls are made in Chat, which only exists in X's apps.");
    break;
  case UI_GROK:
    icon = ICON_SPARK;
    says = L("O Grok, o assistente do X, responde na app do X e em x.com/i/grok.",
             "Grok, X's assistant, answers in the X app and at x.com/i/grok.");
    break;
  case UI_PREMIUM:
    icon = ICON_PREMIUM;
    says = me->has_user && me->user.badge == X_BADGE_BLUE ?
      L("A tua conta tem o visto azul do X Premium. A subscri\xc3\xa7\xc3\xa3o gere-se na app do X e em x.com.",
        "Your account has X Premium's blue check. The subscription is managed in the X app and on x.com.") :
      L("O X Premium (o visto azul, posts mais longos, menos an\xc3\xban" "cios) subscreve-se na app do X e em x.com.",
        "X Premium (the blue check, longer posts, fewer ads) is subscribed to in the X app and on x.com.");
    break;
  default:
    icon = ICON_HISTORY;
    says = L("Os posts que abrires aparecem aqui, o mais recente primeiro, at\xc3\xa9 fechares a app.",
             "The posts you open show up here, newest first, until you close the app.");
    break;
  }
  draw_circle(&g_dst, cx, 150, 76, CHIP);
  draw_icon(&g_dst, icon, cx - 42, 108, 84, FG);
  centred(font(F_TITLE), 250, 70, title, FG);
  y = 340;
  text_wrapped(font(F_BODY), MAIN_X + 110, y, LINE_H, says, MAIN_W - 220, GREY);
}


/**
 * Grok's page: hints without spoilers for the game that runs, the button
 * that asks, and the question with its answer
 **/
static void
draw_grok(unsigned frame) {
  text_font_t *head = font(F_HEADING), *body = font(F_BODY), *small = font(F_SMALL), *pill = font(F_PILL);
  const int x = MAIN_X + 70, w = MAIN_W - 140, cx = MAIN_X + MAIN_W / 2, h = 64;
  const char *game = g_grok.question[0] ? g_grok.game : g_game.name, *label;
  char line[X_GROK_QUESTION + 64], why[64];
  int y, bw, bx, kw, over;

  draw_circle(&g_dst, cx, 100, 54, CHIP);
  draw_icon(&g_dst, ICON_SPARK, cx - 30, 70, 60, ACCENT);
  if(game[0]) {
    snprintf(line, sizeof line, L("Dicas sem spoilers para %s", "Hints without spoilers for %s"), game);
  } else {
    snprintf(line, sizeof line, "%s", L("Dicas sem spoilers", "Hints without spoilers"));
  }
  centred(head, 170, 50, line, FG);

  // ✕ asks, the first time or again
  label = g_grok.question[0] ? L("Outra pergunta", "Ask again") : L("Pedir uma dica", "Ask for a hint");
  kw = width_of(font(F_KEY), "\xe2\x9c\x95") + 22;
  bw = 22 + kw + 14 + width_of(pill, label) + 30;
  bx = cx - bw / 2;
  y = 238;
  over = zone(Z_GROK, 0, 0, bx, y, bw, h);
  draw_box(&g_dst, bx, y, bw, h, h / 2.0f, 0, g_grok.asking ? CHIP : CHIP_ON);
  if(over) {
    lit_box(bx, y, bw, h, h / 2.0f, 1);
  }
  draw_key("\xe2\x9c\x95", bx + 22, y + (h - 38) / 2);
  text(pill, bx + 22 + kw + 14, baseline(pill, y, h), label, g_grok.asking ? GREY : FG);
  y += h + 18;
  if(g_grok.question[0] && !g_grok.asking) {
    centred(small, y, 36, L("\xe2\x96\xa1 come\xc3\xa7" "a uma conversa nova", "\xe2\x96\xa1 starts a new conversation"),
            GREY);
  }
  y += 56;

  if(!g_grok.question[0]) {
    text_wrapped(body, x, y, LINE_H, game[0] ?
                 L("Diz ao Grok onde est\xc3\xa1s encravado e ele d\xc3\xa1-te uma pista curta, sem contar a "
                   "hist\xc3\xb3ria nem o final.",
                   "Tell Grok where you are stuck and it gives you a short hint, without telling the story or "
                   "the ending.") :
                 L("Abre um jogo e pede ao Grok uma pista sobre ele, sem contar a hist\xc3\xb3ria nem o final.",
                   "Open a game and ask Grok for a hint about it, without telling the story or the ending."),
                 w, GREY);
    return;
  }
  // the question, then the answer, Grok thinking, or why it did not come
  snprintf(line, sizeof line, "\xe2\x80\x9c%s\xe2\x80\x9d", g_grok.question);
  y = text_wrapped(small, x, y, 38, line, w, GREY) + 14;
  if(g_grok.asking) {
    spinner(x + 14, y + LINE_H / 2.0f, 12, 4, frame, ACCENT);
    text(body, x + 44, baseline(body, y, LINE_H), L("O Grok est\xc3\xa1 a pensar\xe2\x80\xa6", "Grok is thinking\xe2\x80\xa6"),
         GREY);
  } else if(g_grok.error) {
    action_error(g_grok.error, g_grok.code, g_grok.x_code, why, sizeof why);
    snprintf(line, sizeof line, L("O Grok n\xc3\xa3o respondeu (%s)", "Grok did not answer (%s)"), why);
    text_wrapped(body, x, y, LINE_H, line, w, RED);
  } else {
    text_wrapped(body, x, y, LINE_H, g_grok.answer, w, FG);
  }
}


static void
draw_empty(const tab_t *t, unsigned frame) {
  char title[160], hint[400];
  int y = 0;

  if(t->feed == UI_GROK) {
    draw_grok(frame);
    return;
  }
  if(t->feed >= UI_CALLS && t->feed <= UI_HISTORY) {
    draw_page(t);
    return;
  }
  // a profile without posts, or whose posts did not come
  if(t->header_h) {
    draw_profile(t, LIST_PAD - t->scroll);
    y = LIST_PAD + t->header_h - t->scroll;
  }
  // Explore without trends: what to search for
  if(t->feed == UI_SEARCH && !t->query[0] && !t->loading && !t->wanted && (!t->error || t->error == ERR_EMPTY)) {
    y = text_wrapped(font(F_TITLE), MAIN_X + 48, y + 70, 60, L("Pesquisar no X", "Search X"),
                     MAIN_W - 96, FG);
    text_wrapped(font(F_BODY), MAIN_X + 48, y + 18, LINE_H,
                 L("Carrega em \xe2\x9c\x95 e escreve o que procuras: palavras, #temas ou @pessoas.",
                   "Press \xe2\x9c\x95 and type what you are looking for: words, #topics or @people."),
                 MAIN_W - 96, GREY);
    return;
  }
  if(t->loading || !t->error) {
    spinner(MAIN_X + MAIN_W / 2.0f, y + 110, 22, 5, frame, ACCENT);
    return;
  }
  error_text(t, title, sizeof title, hint, sizeof hint);
  y = text_wrapped(font(F_TITLE), MAIN_X + 48, y + 70, 60, title, MAIN_W - 96, FG);
  y = text_wrapped(font(F_BODY), MAIN_X + 48, y + 18, LINE_H, hint, MAIN_W - 96, GREY);
  if(session_error(t)) {
    text_font_t *f = font(F_PILL);
    const char *label = g_login.ready ? L("\xe2\x9c\x95  Entrar no X", "\xe2\x9c\x95  Log in to X") :
                        L("\xe2\x9c\x95  Ligar pelo PC", "\xe2\x9c\x95  Link from a PC");
    int w = width_of(f, label) + 64, over = zone(Z_LINK, 0, 0, MAIN_X + 48, y + 30, w, 64);

    draw_box(&g_dst, MAIN_X + 48, y + 30, w, 64, 32, 0, over ? RGBA(255, 152, 72, 255) : ACCENT);
    if(over) {
      lit_box(MAIN_X + 48, y + 30, w, 64, 32, 0);
    }
    text(f, MAIN_X + 48 + 32, baseline(f, y + 30, 64), label, WHITE);
  }
}


/**
 * src over dst, a of 255
 **/
static uint32_t
mix(uint32_t dst, uint32_t src, unsigned a) {
  uint32_t rb = ((dst & 0xFF00FFu) * (255 - a) + (src & 0xFF00FFu) * a) >> 8;
  uint32_t g = ((dst & 0xFF00u) * (255 - a) + (src & 0xFF00u) * a) >> 8;

  return 0xFF000000u | (rb & 0xFF00FFu) | (g & 0xFF00u);
}


/**
 * the posts, or why there are none, cut to their place on the glass and
 * fading out at its bottom
 **/
static void
draw_timeline(const tab_t *t, long long now, unsigned frame) {
  text_surface_t screen = g_dst;
  int from = LIST_TOP + LIST_H - FADE;

  if(screen.width < UI_WIDTH || screen.height < UI_HEIGHT) {
    return;
  }
  g_dst = text_band(&screen, LIST_TOP, LIST_H);
  g_off_y = g_clip_top = LIST_TOP;
  g_clip_bottom = LIST_TOP + LIST_H;
  if(t->count) {
    draw_list(t, now, frame);
  } else {
    draw_empty(t, frame);
  }
  g_dst = screen;
  g_off_y = g_clip_top = 0;
  g_clip_bottom = UI_HEIGHT;

  if(!g_backdrop) {
    return;
  }
  // in pixels; row is the point a pixel's row is at
  const text_surface_t px = text_pixels(&screen);
  const int r0 = text_py(&screen, LIST_TOP), r1 = text_py(&screen, LIST_TOP + LIST_H);
  const int c0 = text_px(&screen, PANEL_X + 2), c1 = text_px(&screen, DIV_X);
  for(int prow=r0; prow < r1; prow++) {
    uint32_t *line = px.pixels + (size_t)prow * px.width;
    const uint32_t *glass = g_backdrop + (size_t)prow * px.width;
    const float row = (prow + 0.5f) / g_scale - 0.5f;
    float k;
    unsigned a;
    if(row < LIST_TOP + FADE_TOP) {
      k = (LIST_TOP + FADE_TOP - row) / (FADE_TOP + 1);
    } else if(row >= from) {
      k = (row - from + 1) / FADE;
    } else {
      continue;
    }
    a = (unsigned)(255 * (k < 0 ? 0 : k > 1 ? 1 : k));
    for(int col=c0; col < c1; col++) {
      line[col] = mix(line[col], glass[col], a);
    }
  }
}


/*
 * Around the timeline.
 */

/**
 * under the app's name: when the tab was read, or why it was not
 **/
static void
status_line(const tab_t *t, long long now, char *line, size_t size, uint32_t *color) {
  long long diff;
  char n[32];

  *color = GREY;
  if(t->error && t->count && t->loading != LOAD_TOP) {
    *color = RED;
    if(t->error == ERR_HTTP) {
      snprintf(line, size, L("N\xc3\xa3o atualizou (HTTP %d)", "Not refreshed (HTTP %d)"), t->code);
    } else if(t->error == ERR_NETWORK) {
      snprintf(line, size, "%s", L("N\xc3\xa3o atualizou (sem liga\xc3\xa7\xc3\xa3o)",
                                   "Not refreshed (no connection)"));
    } else {
      snprintf(line, size, "%s", L("N\xc3\xa3o atualizou", "Not refreshed"));
    }
  } else if(t->has_user && t->user.posts >= 0 && t->loading != LOAD_TOP) {
    count_text(t->user.posts, n, sizeof n);
    snprintf(line, size, "@%s \xc2\xb7 %s posts", t->user.user, n);
  } else if(t->has_user && t->user.user[0] && !t->fetched_at) {
    snprintf(line, size, "@%s", t->user.user);
  } else if(t->feed == UI_SEARCH && !t->query[0]) {
    snprintf(line, size, "%s", L("Posts e pessoas de todo o X", "Posts and people from all of X"));
  } else if(t->feed == UI_GROK) {
    snprintf(line, size, "%s", L("Dicas sem spoilers", "Hints without spoilers"));
  } else if(t->feed >= UI_CALLS && t->feed <= UI_PREMIUM) {
    snprintf(line, size, "%s", L("Nas apps do X", "In X's apps"));
  } else if(t->feed == UI_DM && t->user.user[0] && t->loading != LOAD_TOP) {
    snprintf(line, size, "@%s", t->user.user);
  } else if(t->feed == UI_DM && t->loading != LOAD_TOP) {
    snprintf(line, size, "%s", L("Conversa de grupo", "Group chat"));
  } else if(t->feed == UI_HISTORY) {
    snprintf(line, size, "%s", L("Os posts que abriste", "The posts you opened"));
  } else if(t->feed == UI_LIST && t->user.user[0] && t->loading != LOAD_TOP) {
    snprintf(line, size, L("Lista de @%s", "A List by @%s"), t->user.user);
  } else if(t->fetched_at) {
    diff = now - t->fetched_at;
    if(diff < 60) {
      snprintf(line, size, "%s", L("Atualizado agora", "Updated just now"));
    } else if(diff < 3600) {
      snprintf(line, size, L("Atualizado h\xc3\xa1 %lld min", "Updated %lld min ago"), diff / 60);
    } else {
      snprintf(line, size, L("Atualizado h\xc3\xa1 %lld h", "Updated %lld h ago"), diff / 3600);
    }
  } else {
    snprintf(line, size, "%s", L("Cliente n\xc3\xa3o oficial do X", "An unofficial X client"));
  }
}


/**
 * what the list is called, over it
 **/
static const char *
list_title(const tab_t *t) {
  switch(t->feed) {
  case UI_SEARCH:
    return L("Explorar", "Explore");
  case UI_CHAT:
    return g_pick ? L("Enviar a\xe2\x80\xa6", "Send to\xe2\x80\xa6") : L("Chat", "Chat");
  case UI_CALLS:
    return L("Chamadas", "Calls");
  case UI_GROK:
    return L("Grok", "Grok");
  case UI_PREMIUM:
    return L("Subscri\xc3\xa7\xc3\xa3o", "Subscription");
  case UI_HISTORY:
    return L("Hist\xc3\xb3rico", "History");
  case UI_ARTICLES:
    return L("Artigos", "Articles");
  case UI_NOTIFICATIONS:
    return L("Notifica\xc3\xa7\xc3\xb5" "es", "Notifications");
  case UI_BOOKMARKS:
    return L("Itens guardados", "Bookmarks");
  case UI_LISTS:
    return L("Listas", "Lists");
  case UI_LIST:
    return t->user.name[0] ? t->user.name : L("Lista", "List");
  case UI_ME:
  case UI_USER:
    return t->has_user && t->user.name[0] ? t->user.name : t->has_user ? t->user.user :
      L("Perfil", "Profile");
  case UI_POST:
    return "Post";
  case UI_DM:
    return t->user.name[0] ? t->user.name : t->user.user[0] ? t->user.user : L("Conversa", "Conversation");
  default:
    return "XPS5";
  }
}


/**
 * the icon of a section, the one of its item in the bar
 **/
static int
section_icon(int feed) {
  switch(feed) {
  case UI_SEARCH:
    return ICON_SEARCH;
  case UI_NOTIFICATIONS:
    return ICON_BELL;
  case UI_CHAT:
    return ICON_CHAT;
  case UI_CALLS:
    return ICON_CALL;
  case UI_GROK:
    return ICON_SPARK;
  case UI_PREMIUM:
    return ICON_PREMIUM;
  case UI_HISTORY:
    return ICON_HISTORY;
  case UI_ARTICLES:
    return ICON_ARTICLE;
  case UI_ME:
    return ICON_PERSON;
  case UI_BOOKMARKS:
    return ICON_BOOKMARK;
  case UI_LISTS:
    return ICON_LIST;
  default:
    return ICON_HOME;
  }
}


/**
 * over the list: the section's icon or the way back, what the list is, the
 * tabs of the timeline, refreshing, and where a reply goes
 **/
static void
draw_top(const tab_t *t, long long now, unsigned frame) {
  const char *const names[3] = {L("Para ti", "For you"), L("A seguir", "Following"), g_game.tab};
  const char *const products[2] = {L("Principais", "Top"), L("Recentes", "Latest")};
  const char *const explore[2] = {L("Tend\xc3\xaa" "ncias", "Trending"), L("Not\xc3\xad" "cias", "News")};
  text_font_t *brand = font(F_BRAND), *small = font(F_SMALL), *on = font(F_PILL);
  text_font_t *off = font(F_PILL_OFF), *legend = font(F_LEGEND);
  const int pill_h = 60, py = TOP_Y + (TOP_H - pill_h) / 2;
  const x_post_t *p = t->count ? &t->posts[t->sel] : NULL;
  float cx = MAIN_X + 32, cy = TOP_Y + TOP_H / 2.0f;
  const char *const *tabs = NULL;
  const char *label, *key;
  char line[160];
  uint32_t color;
  int x, w, seg[3], tabs_w = 10, key_x, over, lit = 0, kind = Z_TAB, n_tabs = 2, game_w = 0, title_w;

  if(g_depth) {
    // ○, ← or the pointer go back
    over = zone(Z_BACK, 0, 0, MAIN_X, TOP_Y + 2, 64, 64);
    draw_circle(&g_dst, cx, cy, 32, CHIP_ON);
    if(over) {
      lit_circle(cx, cy, 32, 1);
    }
    draw_icon(&g_dst, ICON_BACK, (int)cx - 16, (int)cy - 16, 32, FG);
  } else {
    // the section's own icon, as the bar has it, on the same glass as the
    // way back deeper in
    draw_circle(&g_dst, cx, cy, 32, CHIP_ON);
    draw_icon(&g_dst, section_icon(t->feed), (int)cx - NAV_ICON / 2, (int)cy - NAV_ICON / 2, NAV_ICON, FG);
  }
  status_line(t, now, line, sizeof line, &color);

  // the pages say where X has them, and nothing more
  if(t->feed >= UI_CALLS && t->feed <= UI_PREMIUM) {
    text_fitted(brand, MAIN_X + 82, TOP_Y + 31, list_title(t), 400, FG);
    text_fitted(small, MAIN_X + 82, TOP_Y + 63, line, 400, color);
    if(g_toast.frames > 0) {
      draw_circle(&g_dst, MAIN_X + 36, INPUT_Y + INPUT_H / 2.0f, 9, g_toast.error ? RED : ACCENT);
      text_fitted(legend, MAIN_X + 64, baseline(legend, INPUT_Y, INPUT_H), g_toast.text, MAIN_W - 84, FG);
    }
    return;
  }

  // □ refreshes
  label = t->loading == LOAD_TOP ? L("A atualizar\xe2\x80\xa6", "Refreshing\xe2\x80\xa6") :
                                   L("Atualizar", "Refresh");
  w = 24 + 30 + 12 + width_of(on, label) + 26;
  x = MAIN_X + MAIN_W - w;
  over = zone(Z_REFRESH, 0, 0, x, py, w, pill_h);
  draw_box(&g_dst, x, py, w, pill_h, pill_h / 2.0f, 0, CHIP);
  if(over) {
    lit_box(x, py, w, pill_h, pill_h / 2.0f, 1);
  }
  if(t->loading == LOAD_TOP) {
    spinner(x + 24 + 15, py + pill_h / 2.0f, 12, 4, frame, FG);
  } else {
    draw_icon(&g_dst, ICON_SQUARE, x + 24, py + (pill_h - 30) / 2, 30, FG);
  }
  text(on, x + 24 + 42, baseline(on, py, pill_h), label, FG);

  // the tabs of the timeline (the game's too, while one runs), or of the search
  if(!g_depth && (g_section == UI_FOR_YOU || g_section == UI_FOLLOWING || g_section == UI_GAME)) {
    tabs = names;
    lit = g_section == UI_GAME ? 2 : g_section;
    if(game_tab_shown()) {
      n_tabs = 3;
      game_w = width_of(on, g_game.tab);
      game_w = game_w > GAME_TAB_W ? GAME_TAB_W : game_w;
    }
  } else if(!g_depth && g_section == UI_SEARCH) {
    tabs = t->query[0] ? products : explore;
    lit = t->latest;
    kind = Z_PRODUCT;
  }
  if(tabs) {
    for(int i=0; i < n_tabs; i++) {
      seg[i] = (i == 2 ? GAME_TAB_ICON + 8 + game_w : width_of(on, tabs[i])) + (n_tabs == 3 ? 40 : 52);
      tabs_w += seg[i];
    }
    x -= 16 + tabs_w;
    draw_box(&g_dst, x, py, tabs_w, pill_h, pill_h / 2.0f, 0, WELL);
    x += 5;
    for(int i=0; i < n_tabs; i++) {
      int shown = i == lit;
      text_font_t *f = shown ? on : off;
      const char *s = tabs[i];
      uint32_t ink;
      over = zone(kind, kind == Z_TAB && i == 2 ? UI_GAME : i, 0, x, py + 5, seg[i], pill_h - 10);
      if(shown) {
        draw_box(&g_dst, x, py + 5, seg[i], pill_h - 10, (pill_h - 10) / 2.0f, 0, CHIP_ON);
      }
      if(over) {
        lit_box(x, py + 5, seg[i], pill_h - 10, (pill_h - 10) / 2.0f, 1);
      }
      ink = shown || over ? FG : GREY;
      if(i == 2) {
        // a controller, and the game's name as far as it fits
        draw_icon(&g_dst, ICON_GAME, x + 20, py + (pill_h - GAME_TAB_ICON) / 2, GAME_TAB_ICON, shown ? ACCENT : ink);
        text_fitted(f, x + 20 + GAME_TAB_ICON + 8, baseline(f, py, pill_h), s, game_w + 4, ink);
      } else {
        text(f, x + (seg[i] - width_of(f, s)) / 2, baseline(f, py, pill_h), s, ink);
      }
      x += seg[i];
    }
    x -= tabs_w - 5;
  }
  // what the list is, in what the tabs leave
  title_w = x - 16 - (MAIN_X + 82);
  title_w = title_w > 400 ? 400 : title_w;
  text_fitted(brand, MAIN_X + 82, TOP_Y + 31, list_title(t), title_w, FG);
  text_fitted(small, MAIN_X + 82, TOP_Y + 63, line, title_w, color);

  // where L2's reply goes (in a search, the words searched for, which △
  // changes), which also says for a few seconds how a reply or a like
  // went: a dot of the accent when it went well, red when not
  over = zone(Z_WELL, 0, 0, MAIN_X, INPUT_Y, MAIN_W, INPUT_H);
  draw_box(&g_dst, MAIN_X, INPUT_Y, MAIN_W, INPUT_H, 20, 0, over ? RGBA(255, 255, 255, 26) : WELL);
  if(over) {
    draw_box(&g_dst, MAIN_X, INPUT_Y, MAIN_W, INPUT_H, 20, 2, HOVER_RIM);
  }
  key = t->feed == UI_SEARCH ? "\xe2\x96\xb3" : "L2";
  key_x = MAIN_X + MAIN_W - 14 - (width_of(font(F_KEY), key) + 22);
  draw_key(key, key_x, INPUT_Y + (INPUT_H - 38) / 2);
  if(g_toast.frames > 0) {
    if(g_toast.error) {
      draw_box(&g_dst, MAIN_X, INPUT_Y, MAIN_W, INPUT_H, 20, 0, RGBA(244, 33, 46, 44));
    }
    draw_circle(&g_dst, MAIN_X + 36, INPUT_Y + INPUT_H / 2.0f, 9, g_toast.error ? RED : ACCENT);
    text_fitted(legend, MAIN_X + 64, baseline(legend, INPUT_Y, INPUT_H), g_toast.text,
                key_x - MAIN_X - 84, FG);
    return;
  }
  if(t->feed == UI_SEARCH) {
    draw_icon(&g_dst, ICON_SEARCH, MAIN_X + 22, INPUT_Y + (INPUT_H - 28) / 2, 28,
              t->query[0] || over ? FG : DIM);
    text_fitted(legend, MAIN_X + 64, baseline(legend, INPUT_Y, INPUT_H),
                t->query[0] ? t->query : L("Pesquisar no X\xe2\x80\xa6", "Search X\xe2\x80\xa6"),
                key_x - MAIN_X - 84, t->query[0] || over ? FG : DIM);
    return;
  }
  // in Chat, the conversation ✕ opens; in one, the message L2 writes
  if(t->feed == UI_CHAT || t->feed == UI_DM) {
    const char *say = line;
    draw_icon(&g_dst, ICON_CHAT, MAIN_X + 22, INPUT_Y + (INPUT_H - 28) / 2, 28, over ? FG : DIM);
    if(t->feed == UI_DM && g_dm.text[0] && !strcmp(g_dm.conv, t->arg)) {
      say = g_dm.text;
    } else if(t->feed == UI_DM) {
      snprintf(line, sizeof line, "%s", L("Escrever uma mensagem\xe2\x80\xa6", "Start a message\xe2\x80\xa6"));
    } else if(p && p->user[0]) {
      snprintf(line, sizeof line, L("Abrir a conversa com @%s\xe2\x80\xa6", "Open the conversation with @%s\xe2\x80\xa6"),
               p->user);
    } else if(p) {
      snprintf(line, sizeof line, "%s", L("Abrir a conversa de grupo\xe2\x80\xa6", "Open the group chat\xe2\x80\xa6"));
    } else {
      snprintf(line, sizeof line, "%s", L("Ainda n\xc3\xa3o h\xc3\xa1 conversas", "No conversations yet"));
    }
    text_fitted(legend, MAIN_X + 64, baseline(legend, INPUT_Y, INPUT_H), say, key_x - MAIN_X - 84,
                over || say != line ? FG : DIM);
    return;
  }
  if(t->feed == UI_LISTS) {
    draw_icon(&g_dst, ICON_LIST, MAIN_X + 22, INPUT_Y + (INPUT_H - 28) / 2, 28, over ? FG : DIM);
    if(p) {
      snprintf(line, sizeof line, L("\xe2\x9c\x95 abre a lista %s", "\xe2\x9c\x95 opens the List %s"), p->name);
    } else {
      snprintf(line, sizeof line, "%s", L("As tuas listas do X", "Your Lists on X"));
    }
    text_fitted(legend, MAIN_X + 64, baseline(legend, INPUT_Y, INPUT_H), line, key_x - MAIN_X - 84,
                over ? FG : DIM);
    return;
  }
  draw_icon(&g_dst, ICON_REPLY, MAIN_X + 22, INPUT_Y + (INPUT_H - 28) / 2, 28, over ? FG : DIM);
  if(p && p->id[0]) {
    snprintf(line, sizeof line, L("Responder a @%s\xe2\x80\xa6", "Reply to @%s\xe2\x80\xa6"),
             p->kind == X_KIND_NOTICE ? p->quote_user : p->user);
  } else {
    snprintf(line, sizeof line, "%s", L("Escolhe um post para lhe responder", "Pick a post to reply to"));
  }
  text_fitted(legend, MAIN_X + 64, baseline(legend, INPUT_Y, INPUT_H), line, key_x - MAIN_X - 84,
              over ? FG : DIM);
}


/**
 * the bar of X's sections, drawn as x.com draws it: the app's logo on top
 * (where x.com has its own), an icon and the name of each section, the one
 * shown in bold, a pill under the pointer; the Post button under them, and
 * at the bottom who is signed in
 **/
static void
draw_nav(void) {
  static uint32_t *logo;
  static int logo_tried;
  static const int icons[NAV_ITEMS] = {
    ICON_HOME, ICON_SEARCH, ICON_BELL, ICON_CHAT, ICON_CALL, ICON_SPARK, ICON_PREMIUM, ICON_HISTORY,
    ICON_ARTICLE, ICON_PERSON, ICON_MORE,
  };
  const char *const labels[NAV_ITEMS] = {
    L("In\xc3\xad" "cio", "Home"), L("Explorar", "Explore"),
    L("Notifica\xc3\xa7\xc3\xb5" "es", "Notifications"), L("Chat", "Chat"), L("Chamadas", "Calls"),
    L("Grok", "Grok"), L("Subscri\xc3\xa7\xc3\xa3o", "Subscription"), L("Hist\xc3\xb3rico", "History"),
    L("Artigos", "Articles"), L("Perfil", "Profile"), L("Mais", "More"),
  };
  text_font_t *f = font(F_NAV), *bold = font(F_NAV_BOLD), *small = font(F_SMALL);
  text_font_t *name = font(F_SIDE_BOLD);
  const char *post = L("Publicar", "Post");
  const tab_t *me = &g_tabs[UI_ME];
  const int text_x = NAV_X + 26 + NAV_ICON + 24;
  int lit = nav_item(), over, y, w;

  // the logo, lined up with the icons; made once (it is the size it shows)
  if(!logo_tried) {
    logo_tried = 1;
    logo = image_fit(xps5_logo, sizeof xps5_logo, pxs(NAV_LOGO_W), pxs(NAV_LOGO_H));
  }
  if(logo) {
    draw_picture(&g_dst, NAV_X + 26, NAV_LOGO_Y, NAV_LOGO_W, NAV_LOGO_H, logo);
  }

  for(int i=0; i < NAV_ITEMS; i++) {
    text_font_t *tf = i == lit ? bold : f;
    y = NAV_Y + i * NAV_ITEM;
    over = zone(Z_NAV, i, 0, NAV_X, y, NAV_W, NAV_ITEM);
    if(over) {
      // the pill hugs the item, as on x.com
      w = text_x - NAV_X + width_of(tf, labels[i]) + 30;
      w = w > NAV_W ? NAV_W : w;
      lit_box(NAV_X, y + 3, w, NAV_ITEM - 6, (NAV_ITEM - 6) / 2.0f, 1);
    }
    draw_icon(&g_dst, icons[i], NAV_X + 26, y + (NAV_ITEM - NAV_ICON) / 2, NAV_ICON, FG);
    if(i == lit) {
      // bold: the icon once more, a pixel over
      draw_icon(&g_dst, icons[i], NAV_X + 27, y + (NAV_ITEM - NAV_ICON) / 2, NAV_ICON, FG);
    }
    text_fitted(tf, text_x, baseline(tf, y, NAV_ITEM), labels[i], NAV_X + NAV_W - text_x - 10, FG);
  }

  // Post, the big round button
  over = zone(Z_NAV_POST, 0, 0, NAV_X + 6, NAV_POST_Y, NAV_W - 36, NAV_POST_H);
  draw_box(&g_dst, NAV_X + 6, NAV_POST_Y, NAV_W - 36, NAV_POST_H, NAV_POST_H / 2.0f, 0,
           over ? RGBA(224, 219, 214, 255) : WHITE);
  if(over) {
    lit_box(NAV_X + 6, NAV_POST_Y, NAV_W - 36, NAV_POST_H, NAV_POST_H / 2.0f, 0);
  }
  text(bold, NAV_X + 6 + (NAV_W - 36 - width_of(bold, post)) / 2, baseline(bold, NAV_POST_Y, NAV_POST_H), post,
       INK);

  // who is signed in
  if(!me->has_user) {
    return;
  }
  over = zone(Z_NAV_ME, 0, 0, NAV_X, NAV_ME_Y, NAV_W, NAV_ME_H);
  if(over) {
    lit_box(NAV_X, NAV_ME_Y, NAV_W, NAV_ME_H, NAV_ME_H / 2.0f, 1);
  }
  draw_avatar(me->user.avatar, me->user.user, me->user.name, NAV_X + 14, NAV_ME_Y + (NAV_ME_H - 56) / 2, 56);
  text_fitted(name, NAV_X + 84, NAV_ME_Y + 38, me->user.name[0] ? me->user.name : me->user.user,
              NAV_W - 84 - 54, FG);
  if(me->user.user[0]) {
    char at[80];
    snprintf(at, sizeof at, "@%s", me->user.user);
    text_fitted(small, NAV_X + 84, NAV_ME_Y + 70, at, NAV_W - 84 - 54, GREY);
  }
  draw_icon(&g_dst, ICON_DOTS, NAV_X + NAV_W - 46, NAV_ME_Y + (NAV_ME_H - 28) / 2, 28, FG);
}


/**
 * a box of the side column, from top to bottom: frosted a little more than
 * the panel, with a hairline rim, as the cards of x.com's side column
 **/
static void
side_card(int top, int bottom) {
  draw_box(&g_dst, SIDE_X - 22, top, SIDE_W + 44, bottom - top, 28, 0, RGBA(255, 255, 255, 12));
  draw_box(&g_dst, SIDE_X - 22, top, SIDE_W + 44, bottom - top, 28, 1.2f, RGBA(255, 255, 255, 30));
}


/**
 * a box's title at y
 **/
static void
side_title(int y, const char *title) {
  text_font_t *heading = font(F_HEADING);

  text_fitted(heading, SIDE_X, baseline(heading, y, 50), title, SIDE_W, FG);
}


/**
 * a box that is still coming (spinning) or did not come, in words, at y
 **/
static void
side_state(int y, const side_box_t *b, const char *coming, const char *failed, unsigned frame) {
  text_font_t *f = font(F_SIDE);

  if(b->loading || !b->at) {
    spinner(SIDE_X + 15, y + 22, 13, 4, frame, ACCENT);
    g_spinning = 1;
    text(f, SIDE_X + 44, baseline(f, y, 44), coming, GREY);
  } else {
    text_wrapped(f, SIDE_X, y + 4, SIDE_LINE_H, failed, SIDE_W, GREY);
  }
}


/**
 * one of the side column's buttons, in a cell of w at x, y: a round icon,
 * a stick (stick, its letter in key) or keys; then what it does
 **/
static void
draw_key_cell(int x, int y, int w, int icon, const char *key, const char *label) {
  text_font_t *f = font(F_LEGEND), *kf = font(F_KEY);
  const int cy = y + SIDE_ROW / 2;
  int lx = x + 52;

  if(icon == -2) {
    // a stick, seen from above
    draw_circle(&g_dst, x + 19, cy, 19, CHIP_ON);
    draw_arc(&g_dst, x + 19, cy, 12, 2, 0, 6.2832f, RGBA(255, 255, 255, 90));
    text(kf, x + 19 - width_of(kf, key) / 2, baseline(kf, cy - 19, 38), key, FG);
  } else if(icon >= 0) {
    draw_circle(&g_dst, x + 19, cy, 19, CHIP);
    draw_icon(&g_dst, icon, x + 7, cy - 12, 24, FG);
  } else {
    int end = x + draw_key(key, x, cy - 19) + 10;
    lx = end > lx ? end : lx;
  }
  text_fitted(f, lx, baseline(f, y, SIDE_ROW), label, x + w - lx - 6, GREY);
}


/**
 * what the main buttons do on the list t, two to a row, at the bottom of
 * the side column: on Explore's trends and in Chat the ones that work there
 **/
static void
draw_side_keys(const tab_t *t) {
  const int chat = t->feed == UI_CHAT || t->feed == UI_DM;
  const int trends = t->feed == UI_SEARCH && !t->query[0];
  struct cell { int icon; const char *key, *label; };
  const struct cell cells[SIDE_ROWS * 2] = {
    chat || trends ? (struct cell){ICON_UP_DOWN, NULL, L("Mover", "Move")} :
                     (struct cell){-1, "R2", L("Gosto", "Like")},
    chat ? (struct cell){-1, "L2", L("Escrever", "Write")} :
    trends ? (struct cell){ICON_LEFT_RIGHT, NULL, L("Sec\xc3\xa7\xc3\xb5" "es", "Sections")} :
             (struct cell){-1, "L2", L("Responder", "Reply")},
    {ICON_CROSS, NULL, g_pick && t->feed == UI_CHAT ? L("Enviar", "Send") :
                       t->count && t->cards && t->cards[t->sel].spoiler ? L("Mostrar", "Show") : L("Abrir", "Open")},
    {ICON_CIRCLE, NULL, L("Voltar", "Back")},
    {ICON_TRIANGLE, NULL, trends ? L("Pesquisar", "Search") : L("Topo", "Top")},
    {-1, "\xe2\x98\xb0", L("Novo post", "New post")},
  };
  const int cell_w = SIDE_W / 2;

  for(int i=0; i < SIDE_ROWS * 2; i++) {
    draw_key_cell(SIDE_X + (i % 2) * cell_w, SIDE_KEYS_Y + (i / 2) * SIDE_ROW, cell_w, cells[i].icon, cells[i].key,
                  cells[i].label);
  }
}


/**
 * "What's happening": X's first trends, as x.com's side column has them
 * (the pointer or ✕ opens one, "Show all" opens Explore), in a box from
 * top; returns where the box ends
 **/
static int
draw_side_trends(int top, unsigned frame) {
  text_font_t *bold = font(F_SIDE_BOLD), *small = font(F_SMALL);
  char line[200];
  int y = top + 14, n = g_trends.count < SIDE_TRENDS ? g_trends.count : SIDE_TRENDS;
  int bottom = y + SIDE_HEAD_H + (n ? n * SIDE_TREND_H + SIDE_MORE_H : 50) + 10;

  side_card(top, bottom);
  side_title(y, L("O que est\xc3\xa1 a acontecer", "What's happening"));
  y += SIDE_HEAD_H;
  if(!n) {
    side_state(y, &g_trends, L("A ler as tend\xc3\xaa" "ncias\xe2\x80\xa6", "Reading the trends\xe2\x80\xa6"),
               L("As tend\xc3\xaa" "ncias n\xc3\xa3o vieram agora. Volto a pedi-las daqui a pouco.",
                 "The trends did not come now. They are asked for again in a while."), frame);
    return bottom;
  }
  for(int i=0; i < n; i++, y += SIDE_TREND_H) {
    const x_post_t *p = &g_trends.items[i];
    if(zone(Z_SIDE_TREND, i, 0, SIDE_X - 14, y, SIDE_W + 28, SIDE_TREND_H - 6)) {
      lit_box(SIDE_X - 14, y, SIDE_W + 28, SIDE_TREND_H - 6, 18, 1);
    }
    if(p->icon > 0 && p->quote_name[0]) {
      snprintf(line, sizeof line, "%d \xc2\xb7 %s", p->icon, p->quote_name);
    } else if(p->quote_name[0]) {
      snprintf(line, sizeof line, "%s", p->quote_name);
    } else {
      snprintf(line, sizeof line, L("%d \xc2\xb7 Tend\xc3\xaa" "ncia", "%d \xc2\xb7 Trending"), i + 1);
    }
    text_fitted(small, SIDE_X, baseline(small, y + 2, 26), line, SIDE_W, GREY);
    text_fitted(bold, SIDE_X, baseline(bold, y + 27, 30), p->name[0] ? p->name : p->text, SIDE_W, FG);
    if(p->quote_text[0]) {
      text_fitted(small, SIDE_X, baseline(small, y + 53, 26), p->quote_text, SIDE_W, GREY);
    }
  }
  // Explore, with all of them
  if(zone(Z_SIDE_MORE, 0, 0, SIDE_X - 14, y, SIDE_W + 28, SIDE_MORE_H)) {
    lit_box(SIDE_X - 14, y, SIDE_W + 28, SIDE_MORE_H, 18, 1);
  }
  text(font(F_SIDE), SIDE_X, baseline(font(F_SIDE), y, SIDE_MORE_H), L("Mostrar mais", "Show more"), ACCENT);
  return bottom;
}


/**
 * "Who to follow", as x.com's side column has it: a picture, the names and
 * a Follow button (white; "Following" with a rim once followed), in a box
 * from top; the pointer on a name opens the profile
 **/
static void
draw_side_people(int top, unsigned frame) {
  text_font_t *bold = font(F_SIDE_BOLD), *small = font(F_SMALL), *pill = font(F_SMALL_BOLD);
  const char *follow = L("Seguir", "Follow"), *following = L("A seguir", "Following");
  int y = top + 14, n = g_who.count < SIDE_PEOPLE ? g_who.count : SIDE_PEOPLE;
  int bottom = y + SIDE_HEAD_H + (n ? n * SIDE_USER_H : 50) + 10, bw, bx, x, end, size, over;
  char at[80];

  if(bottom > SIDE_KEYS_Y - 24) {
    return; // no room for it
  }
  side_card(top, bottom);
  side_title(y, L("Quem seguir", "Who to follow"));
  y += SIDE_HEAD_H;
  if(!n) {
    side_state(y, &g_who, L("A procurar contas\xe2\x80\xa6", "Looking for accounts\xe2\x80\xa6"),
               L("As sugest\xc3\xb5" "es n\xc3\xa3o vieram agora.", "The suggestions did not come now."), frame);
    return;
  }
  bw = width_of(pill, following) > width_of(pill, follow) ? width_of(pill, following) + 36 : width_of(pill, follow) + 36;
  bx = SIDE_X + SIDE_W - bw;
  size = text_line_height(bold) - 6;
  for(int i=0; i < n; i++, y += SIDE_USER_H) {
    const x_post_t *p = &g_who.items[i];
    if(zone(Z_SIDE_USER, i, 0, SIDE_X - 14, y, bx - SIDE_X + 4, SIDE_USER_H - 6)) {
      lit_box(SIDE_X - 14, y, bx - SIDE_X + 4, SIDE_USER_H - 6, 18, 1);
    }
    draw_avatar(p->avatar, p->user, p->name, SIDE_X, y + 7, 52);
    x = SIDE_X + 66;
    end = text_fitted(bold, x, y + 32, p->name[0] ? p->name : p->user, bx - x - 12 - (p->badge ? size + 6 : 0), FG);
    if(p->badge) {
      draw_badge(p->badge, end + 5, y + 32 - text_ascent(bold) + 2, size);
    }
    snprintf(at, sizeof at, "@%s", p->user);
    text_fitted(small, x, y + 61, at, bx - x - 12, GREY);
    over = zone(Z_SIDE_FOLLOW, i, 0, bx, y + 14, bw, 42);
    if(p->following) {
      draw_box(&g_dst, bx, y + 14, bw, 42, 21, 1.4f, RGBA(255, 255, 255, 90));
      if(over) {
        lit_box(bx, y + 14, bw, 42, 21, 1);
      }
      text(pill, bx + (bw - width_of(pill, following)) / 2, baseline(pill, y + 14, 42), following, FG);
    } else {
      draw_box(&g_dst, bx, y + 14, bw, 42, 21, 0, over ? RGBA(224, 219, 214, 255) : WHITE);
      if(over) {
        lit_box(bx, y + 14, bw, 42, 21, 0);
      }
      text(pill, bx + (bw - width_of(pill, follow)) / 2, baseline(pill, y + 14, 42), follow, INK);
    }
  }
}


/**
 * "Playing now": the people the user follows whose profile says a game,
 * each with the game in green and an Invite button, in a box from top; the
 * pointer on a name opens the profile
 **/
static void
draw_side_playing(int top) {
  text_font_t *bold = font(F_SIDE_BOLD), *small = font(F_SMALL), *pill = font(F_SMALL_BOLD);
  const char *label = L("Convidar", "Invite");
  int y = top + 14, n = g_playing.count < SIDE_PEOPLE ? g_playing.count : SIDE_PEOPLE;
  int bottom = y + SIDE_HEAD_H + n * SIDE_USER_H + 10, bw = width_of(pill, label) + 36;
  int bx = SIDE_X + SIDE_W - bw, x, end, size = text_line_height(bold) - 6, is = 24, over;

  if(!n || bottom > SIDE_KEYS_Y - 24) {
    return;
  }
  side_card(top, bottom);
  side_title(y, L("A jogar agora", "Playing now"));
  y += SIDE_HEAD_H;
  for(int i=0; i < n; i++, y += SIDE_USER_H) {
    const x_post_t *p = &g_playing.items[i];
    if(zone(Z_SIDE_PLAYER, i, 0, SIDE_X - 14, y, bx - SIDE_X + 4, SIDE_USER_H - 6)) {
      lit_box(SIDE_X - 14, y, bx - SIDE_X + 4, SIDE_USER_H - 6, 18, 1);
    }
    draw_avatar(p->avatar, p->user, p->name, SIDE_X, y + 7, 52);
    playing_badge(SIDE_X, y + 7, 52);
    x = SIDE_X + 66;
    end = text_fitted(bold, x, y + 32, p->name[0] ? p->name : p->user, bx - x - 12 - (p->badge ? size + 6 : 0), FG);
    if(p->badge) {
      draw_badge(p->badge, end + 5, y + 32 - text_ascent(bold) + 2, size);
    }
    // the game under the name and the button, the whole width: its name is what matters
    draw_icon(&g_dst, ICON_GAME, x, y + 43, is, GREEN);
    text_fitted(small, x + is + 8, y + 63, p->playing, SIDE_X + SIDE_W - x - is - 8, GREEN);
    over = zone(Z_SIDE_INVITE, i, 0, bx, y + 6, bw, 36);
    draw_box(&g_dst, bx, y + 6, bw, 36, 18, 0, over ? RGBA(224, 219, 214, 255) : WHITE);
    if(over) {
      lit_box(bx, y + 6, bw, 36, 18, 0);
    }
    text(pill, bx + (bw - width_of(pill, label)) / 2, baseline(pill, y + 6, 36), label, INK);
  }
}


/**
 * the side column: "What's happening" and "Who to follow" in boxes, and
 * at the bottom what the main buttons do; on Explore's trends, in Chat and
 * on the Lists a word on what is there in place of the trends; "Playing
 * now" in place of "Who to follow" while someone the user follows plays
 **/
static void
draw_side(const tab_t *t, unsigned frame) {
  text_font_t *heading = font(F_HEADING), *f = font(F_SIDE);
  int y = SIDE_Y - 14;

  draw_side_keys(t);

  if(t->feed == UI_CHAT || t->feed == UI_DM || (t->feed == UI_SEARCH && !t->query[0]) || t->feed == UI_LISTS) {
    const char *title = g_pick && t->feed == UI_CHAT ? L("Enviar a\xe2\x80\xa6", "Send to\xe2\x80\xa6") :
                        t->feed == UI_CHAT || t->feed == UI_DM ? "Chat" : list_title(t);
    const char *words = g_pick && t->feed == UI_CHAT ?
      L("Escolhe a conversa para onde vai a tua captura. \xe2\x9c\x95 envia-a e \xe2\x97\x8b volta ao post.",
        "Pick the conversation your capture goes to. \xe2\x9c\x95 sends it and \xe2\x97\x8b goes back to the post.") :
      t->feed == UI_DM ?
      L("\xe2\x9c\x95 ou L2 escreve uma mensagem, \xe2\x86\x91 e \xe2\x86\x93 percorrem a conversa e "
        "\xe2\x96\xa1 v\xc3\xaa se chegaram novas.",
        "\xe2\x9c\x95 or L2 writes a message, \xe2\x86\x91 and \xe2\x86\x93 go through the "
        "conversation and \xe2\x96\xa1 checks for new ones.") :
      t->feed == UI_CHAT ?
      L("As tuas mensagens diretas do X. \xe2\x9c\x95 abre uma conversa e \xe2\x96\xa1 atualiza. "
        "As conversas cifradas do X Chat s\xc3\xb3 abrem nas apps do X.",
        "Your direct messages on X. \xe2\x9c\x95 opens a conversation and \xe2\x96\xa1 refreshes. "
        "Encrypted X Chat conversations only open in X's apps.") :
      t->feed == UI_LISTS ?
      L("As listas que criaste e as que segues. \xe2\x9c\x95 mostra os posts de uma e \xe2\x96\xa1 "
        "atualiza.",
        "The Lists you made and the ones you follow. \xe2\x9c\x95 shows the posts of one and "
        "\xe2\x96\xa1 refreshes.") :
      L("O que est\xc3\xa1 a acontecer no X. \xe2\x9c\x95 mostra os posts de uma tend\xc3\xaa" "ncia, "
        "\xe2\x96\xb3 pesquisa outras palavras e \xe2\x96\xa1 atualiza.",
        "What is happening on X. \xe2\x9c\x95 shows the posts of a trend, \xe2\x96\xb3 searches for "
        "other words and \xe2\x96\xa1 refreshes.");
    // as tall as its words: some languages take five lines where English takes four
    int bottom = y + 14 + SIDE_HEAD_H + 8 + wrapped_lines(f, words, SIDE_W) * SIDE_LINE_H + 16;
    side_card(y, bottom);
    text(heading, SIDE_X, baseline(heading, y + 14, 52), title, FG);
    text_wrapped(f, SIDE_X, y + 14 + SIDE_HEAD_H + 8, SIDE_LINE_H, words, SIDE_W, GREY);
    y = bottom;
  } else {
    y = draw_side_trends(y, frame);
  }
  if(g_playing.count) {
    draw_side_playing(y + 22);
  } else {
    draw_side_people(y + 22, frame);
  }
}


/**
 * the picture the pointer opened, as big as the screen lets it be; a video
 * fills the screen
 **/
static void
draw_viewer(unsigned frame) {
  const x_media_t *m = &g_viewer.media[g_viewer.index];
  const char *video = video_of(m);
  text_font_t *f = font(F_LEGEND);
  const int max_w = UI_WIDTH - 160, max_h = UI_HEIGHT - 170;
  char buf[IMAGES_URL_MAX], line[200];
  const uint32_t *pixels;
  int w = max_w, h = max_h, x, y;

  g_viewer.waiting = 0;
  if(video) {
    draw_viewer_video(m, media_url(m->url, "large", buf, sizeof buf), frame);
    return;
  }
  draw_rect(&g_dst, 0, 0, UI_WIDTH, UI_HEIGHT, RGBA(10, 8, 7, 228));
  zone(Z_VIEWER, 0, 0, 0, 0, UI_WIDTH, UI_HEIGHT);
  if(m->width > 0 && m->height > 0) {
    if((long long)m->width * max_h > (long long)m->height * max_w) {
      h = (int)((long long)max_w * m->height / m->width);
    } else {
      w = (int)((long long)max_h * m->width / m->height);
    }
  }
  w = (w < 64 ? 64 : w) & ~1;
  h = (h < 64 ? 64 : h) & ~1;
  x = (UI_WIDTH - w) / 2;
  y = 50 + (max_h - h) / 2;
  if((pixels=picture(media_url(m->url, "large", buf, sizeof buf), w, h))) {
    draw_picture_round(&g_dst, x, y, w, h, 16, pixels);
  } else {
    g_viewer.waiting = 1;
    spinner(UI_WIDTH / 2.0f, y + h / 2.0f, 30, 6, frame, ACCENT);
  }
  if(m->video) {
    draw_play(x + w / 2.0f, y + h / 2.0f, 56);
  }

  // under it: the way to the others and out
  if(m->video) {
    snprintf(line, sizeof line, "%s", L("X n\xc3\xa3o mandou o v\xc3\xad" "deo deste post  \xc2\xb7  \xe2\x97\x8b Fechar",
                                        "X sent no video for this post  \xc2\xb7  \xe2\x97\x8b Close"));
  } else if(g_viewer.count > 1) {
    snprintf(line, sizeof line, L("%d de %d  \xc2\xb7  \xe2\x86\x90 \xe2\x86\x92 Mudar  \xc2\xb7  \xe2\x97\x8b Fechar",
                                  "%d of %d  \xc2\xb7  \xe2\x86\x90 \xe2\x86\x92 Switch  \xc2\xb7  \xe2\x97\x8b Close"),
             g_viewer.index + 1, g_viewer.count);
  } else {
    snprintf(line, sizeof line, "%s", L("\xe2\x97\x8b Fechar", "\xe2\x97\x8b Close"));
  }
  text(f, (UI_WIDTH - width_of(f, line)) / 2, UI_HEIGHT - 50, line, FG);
}


/**
 * a colour k of the way from a to b, alpha too
 **/
static uint32_t
toward(uint32_t a, uint32_t b, float k) {
  uint32_t out = 0;

  for(int s=0; s < 32; s += 8) {
    float ca = (float)(a >> s & 0xff), cb = (float)(b >> s & 0xff);
    out |= (uint32_t)(ca + (cb - ca) * k + 0.5f) << s;
  }
  return out;
}


/**
 * a step of the pointer turning into the ring over something to press, or
 * back, by the zones of the last frame; 1 while it turns
 **/
static int
pointer_turn(void) {
  const zone_t *z = g_ptr.shown ? zone_at(g_ptr.x, g_ptr.y) : NULL;
  const float live = z && z->kind != Z_CARD && z->kind != Z_VIEWER && z->kind != Z_SHEET &&
                     z->kind != Z_PANE && z->kind != Z_LG_PAGE ? 1.0f : 0.0f;

  if(fabsf(g_ptr.live - live) < 0.03f) {
    g_ptr.live = live;
    return 0;
  }
  g_ptr.live += (live - g_ptr.live) * 0.4f;
  return 1;
}


/**
 * the pointer: a bead of frosted glass like the panel, with a lit rim, a
 * glint and a soft shadow, in a banner's colour when one tints the backdrop;
 * over something to press it grows, gently, into a ring of that colour (or
 * the accent's) around a white dot
 **/
static void
draw_pointer(void) {
  const float x = g_ptr.x, y = g_ptr.y, k = g_ptr.live, r = POINTER_R + 7 * k;
  // its colour: the ring's over something to press, a lighter one for its
  // rim; when a banner gave it, the bead takes it too, its rim almost all
  // of it and its glass about half
  const uint32_t hue = RGBA((int)(g_ptr_col[0] + 0.5f), (int)(g_ptr_col[1] + 0.5f),
                            (int)(g_ptr_col[2] + 0.5f), 255);
  const uint32_t light = toward(hue, WHITE, 0.4f);
  const float banner = g_ptr_col[3];
  const uint32_t glass = toward(RGBA(255, 255, 255, 62), (hue & 0xFFFFFF) | 88u << 24, 0.55f * banner);
  const uint32_t rim = toward(RGBA(255, 255, 255, 235), (light & 0xFFFFFF) | 245u << 24, 0.85f * banner);

  draw_circle(&g_dst, x, y + 5, r + 7, RGBA(0, 0, 0, 20));
  draw_circle(&g_dst, x, y + 2.5f, r + 3, RGBA(0, 0, 0, 44));
  draw_circle(&g_dst, x, y, r, toward(glass, (hue & 0xFFFFFF) | 92u << 24, k));
  draw_arc(&g_dst, x, y, r + 1.2f, 1.2f, 0, 6.2832f, RGBA(0, 0, 0, 70)); // against white pictures
  draw_arc(&g_dst, x, y, r - 1.2f, 2.4f, 0, 6.2832f, toward(rim, light, k));
  draw_arc(&g_dst, x, y, r - 5.5f, 2, 4.9f, 6.0f, toward(RGBA(255, 255, 255, 150), RGBA(255, 255, 255, 0), k));
  draw_circle(&g_dst, x, y, 3 + 1.5f * k, toward(RGBA(255, 255, 255, 210), WHITE, k));
}


/*
 * Videos: the selected post's in its tile, muted, the way x.com plays them in
 * the timeline, and the one the viewer opened with its sound and controls.
 * The player is open only while something on the screen shows it: ui_draw
 * closes it after a frame that did not.
 */



/**
 * the MP4 of a video or GIF to play, in the timeline and over the whole
 * screen alike: the sharpest X has, or the smallest when saving data
 **/
static const char *
video_of(const x_media_t *m) {
  if(!m->video) {
    return NULL;
  }
  if(g_settings.saver && m->video_small[0]) {
    return m->video_small;
  }
  return m->video_url[0] ? m->video_url : m->video_small[0] ? m->video_small : NULL;
}


/**
 * w x h of a picture of the video that fits the box, its sides or its top
 * and bottom left dark; even, for the scaler
 **/
static void
fit_video(const x_media_t *m, int box_w, int box_h, int *w, int *h) {
  *w = box_w;
  *h = box_h;
  if(m->width > 0 && m->height > 0) {
    if((long long)m->width * box_h > (long long)m->height * box_w) {
      *h = (int)((long long)box_w * m->height / m->width);
    } else {
      *w = (int)((long long)box_h * m->width / m->height);
    }
  }
  *w = (*w < 16 ? 16 : *w) & ~1;
  *h = (*h < 16 ? 16 : *h) & ~1;
}


/**
 * the player's latest picture, if it has the size w x h, at x, y; returns
 * whether it was drawn
 **/
static int
draw_player(int x, int y, int w, int h, float r) {
  const uint32_t *pixels;
  int fw, fh, pw, ph, drawn = 0;

  video_px(w, h, &pw, &ph);
  if((pixels=player_frame(&fw, &fh))) {
    if(fw == pw && fh == ph) {
      draw_picture_scaled(&g_dst, x, y, w, h, r, pixels, fw, fh);
      drawn = 1;
    }
    player_frame_done();
  }
  return drawn;
}


/**
 * whether the player has a picture of w x h
 **/
static int
player_ready(int w, int h) {
  int fw = 0, fh = 0, pw, ph;

  if(!player_frame(&fw, &fh)) {
    return 0;
  }
  player_frame_done();
  video_px(w, h, &pw, &ph);
  return fw == pw && fh == ph;
}


/**
 * the video of the post p playing in its tile: the one the corner takes over
 **/
static void
mini_keep(const x_post_t *p, const x_media_t *m) {
  const tab_t *t = view();

  if(!g_mini.on || strcmp(g_mini.post, p->id) || g_mini.serial != t->serial ||
     strcmp(g_mini.media.video_url, m->video_url)) {
    g_mini.media = *m;
    snprintf(g_mini.post, sizeof g_mini.post, "%s", p->id);
    g_mini.serial = t->serial;
    g_mini.since_us = g_ui_us;
    g_mini.on = 1;
  }
  g_mini.here = 1;
}


/**
 * the selected post's video (post index) in its tile, without sound until
 * its button turns it on: INLINE_PLAYING or INLINE_PAUSED when a picture of it was
 * drawn, INLINE_WAIT while it comes (or takes the tile's size),
 * INLINE_NONE when it cannot play
 **/
static int
inline_video(const x_media_t *m, int index, int x, int y, int w, int h, float r) {
  const char *url = video_of(m);
  player_status_t st;
  int vw, vh, pw, ph, over;

  if(!url || g_sheet) { // under a sheet it stops
    return INLINE_NONE;
  }
  fit_video(m, w, h, &vw, &vh);
  video_px(vw, vh, &pw, &ph);
  g_player_wanted = 1;
  if(!player_is(url)) {
    player_open(url, pw, ph, g_inline_sound, m->duration_ms);
    return INLINE_WAIT;
  }
  player_resize(pw, ph);
  player_sound(g_inline_sound);
  player_status(&st);
  if(st.state == PLAYER_FAILED) {
    return INLINE_NONE;
  }
  if((st.state != PLAYER_PLAYING && st.state != PLAYER_PAUSED) || !player_ready(vw, vh)) {
    return INLINE_WAIT;
  }
  draw_box(&g_dst, x, y, w, h, r, 0, RGBA(0, 0, 0, 255));
  if(!draw_player(x + (w - vw) / 2, y + (h - vh) / 2, vw, vh, r)) {
    return INLINE_WAIT;
  }
  if(st.buffering) {
    // more of the video is coming
    g_spinning = 1;
    draw_circle(&g_dst, x + w / 2.0f, y + h / 2.0f, 40, RGBA(20, 16, 14, 150));
    spinner(x + w / 2.0f, y + h / 2.0f, 24, 5, g_frame_no, WHITE);
  }
  // its sound, off at first as on x.com: the pointer turns it on here,
  // and the videos after it keep it
  over = zone(Z_T_SOUND, index, 0, x + w - 72, y + h - 72, 68, 68);
  draw_circle(&g_dst, x + w - 38, y + h - 38, over ? 28 : 24, over ? RGBA(0, 0, 0, 210) : RGBA(0, 0, 0, 160));
  if(over) {
    draw_arc(&g_dst, x + w - 38, y + h - 38, 27, 2, 0, 6.2832f, WHITE);
  }
  draw_icon(&g_dst, st.sound ? ICON_SOUND : ICON_MUTE, x + w - 53, y + h - 53, 30, st.has_audio ? WHITE : DIM);
  return st.state == PLAYER_PAUSED ? INLINE_PAUSED : INLINE_PLAYING;
}


/**
 * the video the selection left while it played, small in the timeline's
 * bottom right corner once its post is off the screen, on top of the posts;
 * gone once something else plays or shows, or the list changes
 **/
static void
draw_mini(const tab_t *t) {
  const char *url = video_of(&g_mini.media);
  const int x = MAIN_X + MAIN_W - MINI_W - 20, y = LIST_TOP + LIST_H - MINI_H - 24;
  player_status_t st;
  int vw, vh, pw, ph, over, done;

  g_mini.shown = 0;
  if(!g_mini.on || g_mini.here) {
    return;
  }
  if(!url || g_sheet || g_viewer.open || t->serial != g_mini.serial || !g_settings.autoplay ||
     !player_is(url) || (g_ui_us - g_mini.since_us < MINI_AFTER && !g_inline_sound)) {
    g_mini.on = 0; // gone, or only seen in passing
    return;
  }
  player_status(&st);
  if(st.state == PLAYER_FAILED || st.state == PLAYER_IDLE) {
    g_mini.on = 0;
    return;
  }
  fit_video(&g_mini.media, MINI_W, MINI_H, &vw, &vh);
  video_px(vw, vh, &pw, &ph);
  g_player_wanted = 1;
  g_mini.shown = 1;
  player_resize(pw, ph);
  draw_shadow(&g_dst, x, y, MINI_W, MINI_H, 22, 36, RGBA(0, 0, 0, 120));
  draw_box(&g_dst, x, y, MINI_W, MINI_H, 22, 0, RGBA(0, 0, 0, 255));
  if(!draw_player(x + (MINI_W - vw) / 2, y + (MINI_H - vh) / 2, vw, vh, 22) || st.buffering) {
    g_spinning = 1;
    spinner(x + MINI_W / 2.0f, y + MINI_H / 2.0f, 22, 5, g_frame_no, WHITE);
  }
  if(st.len_ms > 0) {
    // how far it is
    done = st.pos_ms < 0 ? 0 : st.pos_ms > st.len_ms ? st.len_ms : st.pos_ms;
    done = (int)((long long)(MINI_W - 48) * done / st.len_ms);
    draw_box(&g_dst, x + 24, y + MINI_H - 16, MINI_W - 48, 5, 2.5f, 0, RGBA(255, 255, 255, 70));
    draw_box(&g_dst, x + 24, y + MINI_H - 16, done > 5 ? done : 5, 5, 2.5f, 0, ACCENT);
  }
  over = zone(Z_MINI, 0, 0, x, y, MINI_W, MINI_H);
  draw_box(&g_dst, x, y, MINI_W, MINI_H, 22, over ? 3 : 1.5f, over ? RGBA(255, 255, 255, 220) : RGBA(255, 255, 255, 70));
  draw_key("R3", x + 16, y + 16);
  over = zone(Z_MINI_CLOSE, 0, 0, x + MINI_W - 64, y + 8, 56, 56);
  draw_circle(&g_dst, x + MINI_W - 36, y + 36, over ? 25 : 22, over ? RGBA(0, 0, 0, 220) : RGBA(0, 0, 0, 160));
  draw_icon(&g_dst, ICON_CROSS, x + MINI_W - 49, y + 23, 26, WHITE);
}


/**
 * the pointer on the small video: back to its post, where it goes on
 **/
static int
mini_back(tab_t *t) {
  for(int i=0; i < t->count; i++) {
    if(!strcmp(t->posts[i].id, g_mini.post)) {
      t->sel = i;
      show_selected(t);
      return 1;
    }
  }
  return 0;
}


static int
mini_close(void) {
  g_mini.on = 0;
  g_mini.shown = 0;
  return 1;
}


/**
 * "1:05", "12:40"
 **/
static void
clock_text(int ms, char *buf, size_t size) {
  int s = ms < 0 ? 0 : ms / 1000;

  if(s >= 3600) {
    snprintf(buf, size, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
  } else {
    snprintf(buf, size, "%d:%02d", s / 60, s % 60);
  }
}


/**
 * why the player stopped, in a few words
 **/
static void
player_error(const player_status_t *st, char *buf, size_t size) {
  switch(st->error) {
  case PLAYER_ERR_NETWORK:
    snprintf(buf, size, L("O v\xc3\xad" "deo n\xc3\xa3o veio (sem liga\xc3\xa7\xc3\xa3o, 0x%08X)",
                          "The video did not come (no connection, 0x%08X)"), (unsigned)st->code);
    break;
  case PLAYER_ERR_HTTP:
    snprintf(buf, size, L("O v\xc3\xad" "deo n\xc3\xa3o veio (HTTP %d)", "The video did not come (HTTP %d)"),
             st->code);
    break;
  case PLAYER_ERR_FORMAT:
    snprintf(buf, size, L("Este v\xc3\xad" "deo n\xc3\xa3o se deixa ler (%d)", "This video cannot be read (%d)"),
             st->code);
    break;
  case PLAYER_ERR_DECODER:
    snprintf(buf, size, L("O descodificador falhou (%d)", "The decoder failed (%d)"), st->code);
    break;
  default:
    snprintf(buf, size, "%s", L("Falta mem\xc3\xb3ria para o v\xc3\xad" "deo", "Out of memory for the video"));
  }
}


/**
 * the viewer's video over the whole screen, as big as it fits (X's 1080p
 * pixel for pixel), with the still under it until the first picture. Its
 * controls go over the bottom of it while it is paused, waits or failed,
 * and for CONTROLS_US after a button or the stick; then they go, and the
 * pointer with them, leaving the picture alone
 **/
#define CONTROLS_US 3000000

static void
draw_viewer_video(const x_media_t *m, const char *still, unsigned frame) {
  const char *url = video_of(m);
  text_font_t *f = font(F_LEGEND), *small = font(F_SMALL);
  const int left = (UI_WIDTH - 1100) / 2, bar_y = UI_HEIGHT - 168, shade = 300;
  const uint32_t *pixels;
  player_status_t st;
  char line[200], pos[24], len[24];
  int x, y, w, h, pw, ph, bx, bw, over, shown = 0;
  float done, k;

  fit_video(m, UI_WIDTH, UI_HEIGHT, &w, &h);
  video_px(w, h, &pw, &ph);
  x = (UI_WIDTH - w) / 2;
  y = (UI_HEIGHT - h) / 2;
  draw_rect(&g_dst, 0, 0, UI_WIDTH, UI_HEIGHT, RGBA(0, 0, 0, 255));
  zone(Z_VIEWER, 0, 0, 0, 0, UI_WIDTH, UI_HEIGHT);
  g_player_wanted = 1;
  if(!player_is(url)) {
    player_open(url, pw, ph, g_settings.sound, m->duration_ms);
  } else {
    // the timeline's goes on, at the screen's size and with the sound of the settings
    player_resize(pw, ph);
    player_sound(g_settings.sound);
  }
  player_status(&st);
  if(st.state == PLAYER_PLAYING || st.state == PLAYER_PAUSED) {
    shown = draw_player(x, y, w, h, 0);
  }
  if(!shown && (pixels=picture(still, w, h))) {
    draw_picture_round(&g_dst, x, y, w, h, 0, pixels);
  }
  over = zone(Z_V_PLAY, 0, 0, x, y, w, h);
  if(st.state == PLAYER_LOADING || (st.state == PLAYER_PLAYING && !shown) || st.buffering) {
    g_spinning = 1;
    spinner(UI_WIDTH / 2.0f, UI_HEIGHT / 2.0f, 34, 6, frame, ACCENT);
  } else if(st.state == PLAYER_FAILED) {
    draw_rect(&g_dst, x, y, w, h, RGBA(0, 0, 0, 150));
    player_error(&st, line, sizeof line);
    text_fitted(f, (UI_WIDTH - width_of(f, line)) / 2, UI_HEIGHT / 2 - 8, line, UI_WIDTH - 200, FG);
    snprintf(line, sizeof line, "%s", L("\xe2\x9c\x95 Tentar outra vez", "\xe2\x9c\x95 Try again"));
    text(small, (UI_WIDTH - width_of(small, line)) / 2, UI_HEIGHT / 2 + 40, line, GREY);
  } else if(st.state == PLAYER_PAUSED || (over && st.state != PLAYER_PLAYING)) {
    draw_play(UI_WIDTH / 2.0f, UI_HEIGHT / 2.0f, 56);
  }
  if(st.state == PLAYER_PLAYING && shown && !st.buffering && g_ui_us - g_viewer.input_us >= CONTROLS_US) {
    g_ptr.shown = 0; // the picture alone, until a button or the stick
    return;
  }

  // a shade at the bottom, so the controls read over any picture
  for(int i=0; i < shade; i += 2) {
    k = (float)i / shade;
    draw_rect(&g_dst, 0, UI_HEIGHT - shade + i, UI_WIDTH, 2, RGBA(0, 0, 0, (int)(200 * k * k)));
  }

  // the controls: play or pause, where it is, the bar, the sound
  draw_box(&g_dst, left, bar_y, 1100, 64, 32, 0, BAR);
  draw_box(&g_dst, left, bar_y, 1100, 64, 32, 1.5f, EDGE);
  draw_icon(&g_dst, st.state == PLAYER_PLAYING ? ICON_PAUSE : ICON_PLAY, left + 24, bar_y + 16, 32, FG);
  clock_text(st.pos_ms, pos, sizeof pos);
  clock_text(st.len_ms ? st.len_ms : m->duration_ms, len, sizeof len);
  snprintf(line, sizeof line, "%s / %s", pos, len);
  text(small, left + 76, baseline(small, bar_y, 64), line, FG);
  bx = left + 76 + width_of(small, "00:00 / 00:00") + 30;
  bw = left + 1100 - 84 - bx;
  done = st.len_ms > 0 ? (float)st.pos_ms / st.len_ms : 0;
  done = done < 0 ? 0 : done > 1 ? 1 : done;
  over = zone(Z_V_BAR, 0, 0, bx, bar_y + 12, bw, 40);
  draw_box(&g_dst, bx, bar_y + 29, bw, over ? 8 : 6, 4, 0, RGBA(255, 255, 255, 60));
  draw_box(&g_dst, bx, bar_y + 29, (int)(bw * done), over ? 8 : 6, 4, 0, ACCENT);
  draw_circle(&g_dst, bx + bw * done, bar_y + 32, over ? 11 : 8, WHITE);
  over = zone(Z_V_SOUND, 0, 0, left + 1100 - 70, bar_y, 64, 64);
  if(over) {
    lit_circle(left + 1100 - 40, bar_y + 32, 26, 1);
  }
  draw_icon(&g_dst, st.sound || (g_settings.sound && !st.has_audio && st.state != PLAYER_PLAYING) ?
            ICON_SOUND : ICON_MUTE, left + 1100 - 57, bar_y + 15, 34, st.has_audio ? FG : DIM);

  // and under them, the buttons
  if(g_viewer.count > 1) {
    snprintf(line, sizeof line, L("%d de %d  \xc2\xb7  ", "%d of %d  \xc2\xb7  "), g_viewer.index + 1,
             g_viewer.count);
  } else {
    line[0] = 0;
  }
  snprintf(line + strlen(line), sizeof line - strlen(line), "%s",
           L("\xe2\x9c\x95 Pausa  \xc2\xb7  \xe2\x86\x90 \xe2\x86\x92 5 s  \xc2\xb7  \xe2\x96\xa1 Som  \xc2\xb7  L1 R1 Mudar  \xc2\xb7  \xe2\x97\x8b Fechar",
             "\xe2\x9c\x95 Pause  \xc2\xb7  \xe2\x86\x90 \xe2\x86\x92 5 s  \xc2\xb7  \xe2\x96\xa1 Sound  \xc2\xb7  L1 R1 Switch  \xc2\xb7  \xe2\x97\x8b Close"));
  text(f, (UI_WIDTH - width_of(f, line)) / 2, UI_HEIGHT - 50, line, FG);
}


/**
 * ✕ plays or pauses the viewer's video (and tries again after a failure),
 * ← and → go 5 seconds back or on, □ turns its sound on or off
 **/
static int
video_keys(const char *url, unsigned keys) {
  player_status_t st;

  player_status(&st);
  if(keys & UI_CROSS) {
    if(!player_is(url) || st.state == PLAYER_FAILED) {
      player_close(); // the next frame opens it again
    } else {
      player_pause(st.state != PLAYER_PAUSED);
    }
    return 1;
  }
  if(keys & (UI_LEFT | UI_RIGHT)) {
    player_seek(keys & UI_LEFT ? -5000 : 5000);
    return 1;
  }
  if(keys & UI_SQUARE) {
    g_settings.sound = !g_settings.sound;
    g_settings_dirty = 1;
    player_sound(g_settings.sound);
    toast(0, "%s", g_settings.sound ? L("Som ligado", "Sound on") : L("Som desligado", "Sound off"));
    return 1;
  }
  return 0;
}


/**
 * the viewer's buttons: ○ closes it, L1 and R1 go through its pictures
 * (← and → too for pictures), and the video's own; ✕ closes a picture
 **/
static int
viewer_keys(unsigned keys) {
  const char *url = video_of(&g_viewer.media[g_viewer.index]);
  int d = 0;

  g_viewer.input_us = g_ui_us; // a video's controls come back

  if(keys & UI_CIRCLE) {
    g_viewer.open = 0;
    return 1;
  }
  if(keys & (UI_L1 | UI_R1)) {
    d = keys & UI_L1 ? -1 : 1;
  } else if(!url && (keys & (UI_LEFT | UI_RIGHT))) {
    d = keys & UI_LEFT ? -1 : 1;
  }
  if(d) {
    if(g_viewer.index + d < 0 || g_viewer.index + d >= g_viewer.count) {
      return 0;
    }
    g_viewer.index += d;
    return 1;
  }
  if(url) {
    return video_keys(url, keys);
  }
  if(keys & UI_CROSS) {
    g_viewer.open = 0;
    return 1;
  }
  return 0;
}


/*
 * The sheets over the panel: the composer of a post or a reply, with the
 * PS5's captures it carries; the captures to pick them from; and the
 * settings. Each is a pane of the same glass in the middle of the screen.
 */

#define SHEET_R   40
#define POST_MAX  X_POST_CHARS // characters, as X counts them
#define LONG_MAX  2000 // of a long post of X Premium, as the PS5's keyboard takes them

// the composer
#define C_X       360
#define C_Y       70
#define C_W       1200
#define C_H       940
#define C_PAD     44
#define C_AVATAR  68
#define C_LINE_H  52
#define C_MEDIA_H 300
#define C_BAR_H   76  // the row of buttons at the bottom
#define C_BAR_Y   (C_Y + C_H - C_BAR_H - 76)

// what the buttons go through in the composer: the text, the media, adding
// more, posting
enum { C_FOCUS_TEXT, C_FOCUS_MEDIA, C_FOCUS_ADD = C_FOCUS_MEDIA + UI_MEDIA_MAX, C_FOCUS_SEND,
       C_FOCUS_POLL, // the bar's poll button
       C_FOCUS_CHOICE, C_FOCUS_CHOICE_ADD = C_FOCUS_CHOICE + X_POLL_MAX, C_FOCUS_LENGTH, // and the poll's rows
       C_FOCUS_DM }; // the bar's button that sends the capture in a message

// the captures
#define G_X       220
#define G_Y       56
#define G_W       1480
#define G_H       968
#define G_COLS    4
#define G_GAP     20
#define G_MARGIN  48
#define G_TOP     (G_Y + 140)         // of the grid
#define G_BOTTOM  (G_Y + G_H - 76)
#define G_TILE_W  ((G_W - 2 * G_MARGIN - (G_COLS - 1) * G_GAP) / G_COLS)
#define G_TILE_H  (G_TILE_W * 9 / 16)
#define G_PITCH   (G_TILE_H + G_GAP)
#define G_VIEW_H  (G_BOTTOM - G_TOP - 16)

enum { GALLERY_NONE, GALLERY_LOADING, GALLERY_READY, GALLERY_FAILED };

// the settings
#define S_X       400
#define S_Y       34
#define S_W       1120
#define S_H       1012
#define S_ROW_H   78

enum {
  S_LANG, S_THEME, S_SCREEN, S_NOTIFY, S_EVERY, S_SOUND, S_HOVER, S_AUTOPLAY, S_SAVER, S_SPOILER, S_LIGHT,
  S_BREAK, S_RISING, S_SHARE, S_PLAYING, S_SESSION, S_UPDATE, S_ROWS
};
#define S_SHOWN   10 // the rows that fit; ↑ ↓ and the right stick go through the rest
#define S_CHOICES S_ROWS // the rows that change something; S_SESSION the account

static const int g_everies[] = {30, 60, 120, 300, 600};
static const int g_screens[] = {0, 1080, 1440, 2160}; // the rows drawn; 0 for the TV's

static struct {
  int       reply;   // to the post below, not a post of its own
  int       quote;   // a post of its own quoting the post below
  int       edit;    // the new text of the user's post below
  x_post_t  to;
  char      text[X_REPLY_MAX + 1];
  capture_t media[UI_MEDIA_MAX];
  int       count;
  int       focus;   // C_FOCUS_*, the media's from C_FOCUS_MEDIA
  int       sending; // it went to main.c and has not come back
  long long done, total; // bytes of its media uploaded
  int       schedule; // of g_schedules: when a post of its own goes out
  int       poll;     // the choices of its poll (2 to 4), or 0 for none
  char      choice[X_POLL_MAX][X_POLL_LABEL];
  int       length;   // of g_poll_lengths: how long the poll stays open
  int       editing;  // the choice the keyboard writes, 1 to 4
} g_compose;

// how long after now a post of the user's own goes out: at once, or later
// (X Premium)
static const int g_schedules[] = {0, 30 * 60, 60 * 60, 3 * 3600, 8 * 3600, 24 * 3600, 7 * 24 * 3600};

// how long a poll stays open, in minutes, as x.com offers it (5 minutes to 7 days)
static const int g_poll_lengths[] = {60, 6 * 60, 24 * 60, 3 * 24 * 60, 7 * 24 * 60};
#define POLL_LENGTHS    5
#define POLL_LENGTH_DAY 2

/**
 * the choices of the composer's poll that have words
 **/
static int
poll_filled(void) {
  int n = 0;

  for(int i=0; i < g_compose.poll; i++) {
    n += g_compose.choice[i][0] != 0;
  }
  return n;
}
#define SCHEDULES ((int)(sizeof g_schedules / sizeof *g_schedules))

static struct {
  capture_t *list;   // mem_alloc'd
  int        count;
  int        state;  // GALLERY_*
  int        error, code;
  int        sel, scroll, target;
  int        picked[UI_MEDIA_MAX]; // places in list, in the order picked
  int        picks;
  int        of_game; // the first ones, of the game that runs
} g_gallery;

#define C_GAME_H 50 // the row of the game that runs (g_game) in the composer

static int g_set_row; // the settings' row the buttons are on
static int g_set_top; // and the first one shown


/**
 * a post's length as X counts it: most scripts' characters 1, the rest
 * (CJK, emoji) 2
 **/
static int
post_length(const char *s) {
  const char *end = s + strlen(s);
  uint32_t cp;
  int n = 0;

  while((cp=text_next(&s, end))) {
    n += cp <= 0x10FF || (cp >= 0x2000 && cp <= 0x200D) || (cp >= 0x2010 && cp <= 0x201F) ||
         (cp >= 0x2032 && cp <= 0x2037) ? 1 : 2;
  }
  return n;
}


// a notice's post is the one it quotes
static const char *
post_user(const x_post_t *p) {
  return p->kind == X_KIND_NOTICE ? p->quote_user : p->user;
}


static const char *
post_name(const x_post_t *p) {
  return p->kind == X_KIND_NOTICE ? p->quote_name : p->name;
}


static const char *
post_avatar(const x_post_t *p) {
  return p->kind == X_KIND_NOTICE ? p->quote_avatar : p->avatar;
}


static const char *
post_text(const x_post_t *p) {
  return p->kind == X_KIND_NOTICE ? p->quote_text : p->text;
}


static int
is_video(const capture_t *c) {
  return c->kind == CAPTURE_MP4 || c->kind == CAPTURE_WEBM;
}


/**
 * the address the picture loaders read a capture by, or NULL when it is too
 * long for them
 **/
static const char *
capture_url(const capture_t *c, char *buf, size_t size) {
  int n = snprintf(buf, size, "file://%s", c->path);

  return n > 0 && (size_t)n < size ? buf : NULL;
}


/**
 * "23,4 MB"
 **/
static void
size_text(long long bytes, char *buf, size_t size) {
  long long tenths = (bytes + 50000) / 100000;

  if(tenths >= 1000) {
    snprintf(buf, size, "%lld MB", tenths / 10);
  } else {
    snprintf(buf, size, "%lld%c%lld MB", tenths / 10, lang_decimal(g_lang), tenths % 10);
  }
}


/**
 * the signed-in user has X Premium, whose posts can be long
 **/
static int
premium(void) {
  const tab_t *me = &g_tabs[UI_ME];

  return me->has_user && me->user.badge != X_BADGE_NONE;
}


/**
 * the longest post the user can publish: X Premium's are long ones, up to
 * what the PS5's keyboard takes
 **/
static int
post_max(void) {
  return premium() ? LONG_MAX : POST_MAX;
}


/**
 * when a post goes out, in a few words: "Daqui a 3 h"
 **/
static void
schedule_text(int i, char *buf, size_t size) {
  int s = i > 0 && i < SCHEDULES ? g_schedules[i] : 0;

  if(!s) {
    snprintf(buf, size, "%s", L("Agendar", "Schedule"));
  } else if(s < 3600) {
    snprintf(buf, size, L("Daqui a %d min", "In %d min"), s / 60);
  } else if(s < 24 * 3600) {
    snprintf(buf, size, L("Daqui a %d h", "In %d h"), s / 3600);
  } else if(s == 24 * 3600) {
    snprintf(buf, size, "%s", L("Amanh\xc3\xa3 a esta hora", "Tomorrow, this time"));
  } else {
    snprintf(buf, size, "%s", L("Daqui a uma semana", "In a week"));
  }
}


/**
 * when the composer's post goes out, in a few words
 **/
static void
compose_when(char *buf, size_t size) {
  schedule_text(g_compose.schedule, buf, size);
}


static int
compose_scheduled(void) {
  return g_compose.schedule > 0 && !g_compose.reply && !g_compose.quote && !g_compose.edit;
}


int
ui_schedule_delay(void) {
  return compose_scheduled() ? g_schedules[g_compose.schedule] : 0;
}


/**
 * L1 and R1 in the composer: a post of the user's own goes out later, or at
 * once again, as X Premium schedules them
 **/
static int
compose_schedule(int d) {
  if(g_compose.reply || g_compose.quote || g_compose.edit || g_compose.sending) {
    return 0;
  }
  if(!premium()) {
    toast(1, "%s", L("Agendar posts \xc3\xa9 do X Premium.", "Scheduling posts is part of X Premium."));
    return 1;
  }
  if(g_compose.poll) {
    toast(1, "%s", L("Um post com sondagem sai logo: n\xc3\xa3o se agenda.", "A post with a poll goes out now: it is not scheduled."));
    return 1;
  }
  g_compose.schedule = (g_compose.schedule + d + SCHEDULES) % SCHEDULES;
  return 1;
}


int
ui_compose_max(void) {
  return premium() ? LONG_MAX : POST_MAX;
}


static int
compose_can_send(void) {
  return !g_compose.sending && (g_compose.text[0] || g_compose.count || g_compose.quote) &&
         post_length(g_compose.text) <= post_max() && (!g_compose.poll || (g_compose.text[0] && poll_filled() >= 2));
}


/**
 * the keyboard for the composer's text
 **/
static int
ask_text(void) {
  if(g_act.kind || g_compose.sending) {
    return 0;
  }
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = UI_ACT_COMPOSE_TEXT;
  if(g_compose.reply) {
    snprintf(g_act.id, sizeof g_act.id, "%s", g_compose.to.id);
    snprintf(g_act.user, sizeof g_act.user, "%s", post_user(&g_compose.to));
  }
  return 1;
}


enum { CM_POST, CM_REPLY, CM_QUOTE, CM_EDIT };

/**
 * the composer for a post of the user's own, a reply to the post to, a
 * post quoting it, or a new text of the user's post to; what was written
 * stays for the same, and the keyboard opens at once when nothing is
 **/
static int
compose_open(const x_post_t *to, int mode) {
  int same;

  if(g_act.kind || g_compose.sending) {
    return 0;
  }
  same = g_compose.reply == (mode == CM_REPLY) && g_compose.quote == (mode == CM_QUOTE) &&
         g_compose.edit == (mode == CM_EDIT) && (!to || !strcmp(g_compose.to.id, to->id));
  if(!same) {
    memset(&g_compose, 0, sizeof g_compose);
    g_compose.reply = mode == CM_REPLY;
    g_compose.quote = mode == CM_QUOTE;
    g_compose.edit = mode == CM_EDIT;
    if(to) {
      g_compose.to = *to;
    }
    if(mode == CM_EDIT && to) {
      snprintf(g_compose.text, sizeof g_compose.text, "%s", to->text);
    }
  }
  g_sheet = SHEET_COMPOSE;
  g_compose.focus = compose_can_send() ? C_FOCUS_SEND : C_FOCUS_TEXT;
  if(!g_compose.text[0] && !g_compose.count) {
    ask_text();
  }
  return 1;
}


/**
 * the composer, for a reply to the post to or for a post of the user's own
 * (to NULL), keeping what was written for the same; the keyboard opens at
 * once when nothing is written yet
 **/
static int
open_composer(const x_post_t *to) {
  return compose_open(to, to ? CM_REPLY : CM_POST);
}


/**
 * whether the composer's capture can go in a message: a post of the user's
 * own with one, and without a poll
 **/
static int
share_dm_ok(void) {
  return g_compose.count > 0 && !g_compose.reply && !g_compose.quote && !g_compose.edit && !g_compose.poll;
}


/**
 * the composer's capture in a message: Chat, for the conversation; the
 * draft stays, and ○ comes back to it
 **/
static int
pick_chat(void) {
  if(!share_dm_ok() || g_compose.sending || g_act.kind) {
    return 0;
  }
  g_pick = 1;
  g_sheet = SHEET_NONE;
  show_section(UI_CHAT);
  g_pick = 1;
  // X's messages carry one picture or one video
  toast(0, "%s", g_compose.count > 1 ?
        L("Uma mensagem leva s\xc3\xb3 uma foto ou um v\xc3\xad" "deo: vai a primeira captura.",
          "A message carries one photo or video: the first capture goes.") :
        L("Escolhe a conversa \xc2\xb7 \xe2\x9c\x95 envia \xc2\xb7 \xe2\x97\x8b volta ao post",
          "Pick the conversation \xc2\xb7 \xe2\x9c\x95 sends \xc2\xb7 \xe2\x97\x8b goes back to the post"));
  return 1;
}


/**
 * the composer's text and first capture, a message to the conversation p
 **/
static int
share_to(const x_post_t *p) {
  g_pick = 0;
  if(g_act.kind || g_compose.sending || !share_dm_ok() || !p->id[0]) {
    return 0;
  }
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = UI_ACT_SHARE_DM;
  snprintf(g_act.id, sizeof g_act.id, "%s", p->id);
  {
    // a group's name may be longer: cut on a whole character
    const char *who = p->name[0] ? p->name : p->user;
    size_t n = strlen(who);

    if(n >= sizeof g_act.user) {
      for(n=sizeof g_act.user - 1; n && ((unsigned char)who[n] & 0xC0) == 0x80; n--) {
      }
    }
    memcpy(g_act.user, who, n);
    g_act.user[n] = 0;
  }
  g_compose.sending = 1;
  g_compose.done = g_compose.total = 0;
  toast(0, L("A enviar para %s\xe2\x80\xa6", "Sending to %s\xe2\x80\xa6"), g_act.user);
  return 1;
}


static int
close_composer(void) {
  if(g_compose.sending) {
    return 0;
  }
  g_sheet = SHEET_NONE;
  if(g_compose.text[0] || g_compose.count || g_compose.poll) {
    toast(0, "%s", L("O rascunho fica guardado at\xc3\xa9 o publicares.", "The draft stays until you post it."));
  }
  return 1;
}


static int
publish(void) {
  if(g_compose.sending || g_act.kind) {
    return 0;
  }
  if(!g_compose.text[0] && !g_compose.count && !g_compose.quote) {
    toast(1, "%s", L("Escreve alguma coisa, ou junta uma foto ou um v\xc3\xad" "deo.",
                     "Write something, or add a photo or a video."));
    return 1;
  }
  if(g_compose.poll && (!g_compose.text[0] || poll_filled() < 2)) {
    toast(1, "%s", !g_compose.text[0] ? L("Escreve a pergunta da sondagem.", "Write the poll's question.") :
          L("Escreve pelo menos 2 op\xc3\xa7\xc3\xb5" "es da sondagem.", "Write at least 2 poll choices."));
    return 1;
  }
  if(post_length(g_compose.text) > post_max()) {
    toast(1, "%s", premium() ? L("O post passa dos 2000 caracteres que o teclado da PS5 aceita.",
                                 "The post is over the 2000 characters the PS5 keyboard takes.") :
          L("O post passa dos 280 caracteres.", "The post is over 280 characters."));
    return 1;
  }
  if(compose_scheduled() && post_length(g_compose.text) > POST_MAX) {
    toast(1, "%s", L("Um post agendado vai at\xc3\xa9 aos 280 caracteres.",
                     "A scheduled post takes 280 characters at most."));
    return 1;
  }
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = g_compose.reply ? UI_ACT_REPLY : g_compose.quote ? UI_ACT_QUOTE : g_compose.edit ? UI_ACT_EDIT :
               compose_scheduled() ? UI_ACT_SCHEDULE : UI_ACT_POST;
  if(g_compose.reply || g_compose.quote || g_compose.edit) {
    snprintf(g_act.id, sizeof g_act.id, "%s", g_compose.to.id);
    snprintf(g_act.user, sizeof g_act.user, "%s", post_user(&g_compose.to));
  }
  g_compose.sending = 1;
  g_compose.done = g_compose.total = 0;
  return 1;
}


static void
remove_media(int i) {
  if(i < 0 || i >= g_compose.count || g_compose.sending) {
    return;
  }
  memmove(&g_compose.media[i], &g_compose.media[i + 1], (size_t)(g_compose.count - i - 1) *
          sizeof g_compose.media[0]);
  g_compose.count--;
  if(g_compose.focus >= C_FOCUS_MEDIA + g_compose.count && g_compose.focus < C_FOCUS_ADD) {
    g_compose.focus = g_compose.count ? C_FOCUS_MEDIA + g_compose.count - 1 : C_FOCUS_ADD;
  }
}


/**
 * the composer's next place for the buttons, d of -1 or 1: the text, its
 * media, adding, posting
 **/
static void
compose_move(int d) {
  int order[8 + UI_MEDIA_MAX + X_POLL_MAX], n = 0, at = 0;

  order[n++] = C_FOCUS_TEXT;
  for(int i=0; i < g_compose.count; i++) {
    order[n++] = C_FOCUS_MEDIA + i;
  }
  for(int i=0; i < g_compose.poll; i++) {
    order[n++] = C_FOCUS_CHOICE + i;
  }
  if(g_compose.poll && g_compose.poll < X_POLL_MAX) {
    order[n++] = C_FOCUS_CHOICE_ADD;
  }
  if(g_compose.poll) {
    order[n++] = C_FOCUS_LENGTH;
  }
  order[n++] = C_FOCUS_ADD;
  if(!g_compose.quote && !g_compose.edit) {
    order[n++] = C_FOCUS_POLL;
  }
  if(share_dm_ok()) {
    order[n++] = C_FOCUS_DM;
  }
  order[n++] = C_FOCUS_SEND;
  for(int i=0; i<n; i++) {
    at = order[i] == g_compose.focus ? i : at;
  }
  at += d;
  g_compose.focus = order[at < 0 ? 0 : at >= n ? n - 1 : at];
}


/**
 * the captures, read again from the PS5; what the composer carries comes
 * picked
 **/
static int
open_gallery(void) {
  if(g_compose.sending || g_act.kind) {
    return 0;
  }
  if(g_compose.edit) {
    toast(1, "%s", L("Ao editar um post s\xc3\xb3 muda o texto.", "Editing a post only changes its text."));
    return 1;
  }
  if(g_compose.poll) {
    toast(1, "%s", L("Uma sondagem n\xc3\xa3o leva fotos nem v\xc3\xad" "deos.", "A poll takes no photos or videos."));
    return 1;
  }
  g_sheet = SHEET_GALLERY;
  g_gallery.state = GALLERY_LOADING;
  g_gallery.picks = 0;
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = UI_ACT_CAPTURES;
  return 1;
}


static void
gallery_show_sel(void) {
  int top = (g_gallery.sel / G_COLS) * G_PITCH, rows = (g_gallery.count + G_COLS - 1) / G_COLS;
  int max = rows * G_PITCH - G_GAP - G_VIEW_H;

  if(top < g_gallery.target) {
    g_gallery.target = top;
  }
  if(top + G_TILE_H > g_gallery.target + G_VIEW_H) {
    g_gallery.target = top + G_TILE_H - G_VIEW_H;
  }
  g_gallery.target = g_gallery.target > max ? max : g_gallery.target;
  g_gallery.target = g_gallery.target < 0 ? 0 : g_gallery.target;
}


/**
 * ✕ on a capture: picked, or not any more; a video goes alone, and four
 * pictures at most, as X takes them
 **/
static int
pick(int i) {
  const capture_t *c;
  int at = -1;

  if(g_gallery.state != GALLERY_READY || i < 0 || i >= g_gallery.count) {
    return 0;
  }
  c = &g_gallery.list[i];
  for(int k=0; k < g_gallery.picks; k++) {
    at = g_gallery.picked[k] == i ? k : at;
  }
  if(at >= 0) {
    memmove(&g_gallery.picked[at], &g_gallery.picked[at + 1], (size_t)(g_gallery.picks - at - 1) *
            sizeof g_gallery.picked[0]);
    g_gallery.picks--;
    return 1;
  }
  if(c->kind == CAPTURE_WEBM) {
    toast(1, "%s", L("O X n\xc3\xa3o aceita WebM: na PS5, grava os v\xc3\xad" "deos em MP4 (Capturas e transmiss\xc3\xb5" "es).",
                     "X does not take WebM: on the PS5, record videos as MP4 (Captures and Broadcasts)."));
    return 1;
  }
  if(c->size > (is_video(c) ? CAPTURE_VIDEO_MAX : CAPTURE_PHOTO_MAX)) {
    toast(1, "%s", is_video(c) ? L("Este v\xc3\xad" "deo passa dos 512 MB que o X aceita.",
                                   "This video is over the 512 MB X takes.") :
          L("Esta foto \xc3\xa9 grande demais.", "This photo is too big."));
    return 1;
  }
  if(is_video(c) || (g_gallery.picks && is_video(&g_gallery.list[g_gallery.picked[0]]))) {
    if(g_gallery.picks) {
      toast(0, "%s", L("Um v\xc3\xad" "deo vai sozinho num post.", "A video goes alone in a post."));
    }
    g_gallery.picks = 0;
  } else if(g_gallery.picks >= UI_MEDIA_MAX) {
    toast(1, "%s", L("At\xc3\xa9 4 fotos por post.", "Up to 4 photos a post."));
    return 1;
  }
  g_gallery.picked[g_gallery.picks++] = i;
  return 1;
}


/**
 * the picked captures go into the composer
 **/
static int
gallery_done(void) {
  if(g_gallery.state == GALLERY_READY) {
    g_compose.count = 0;
    for(int k=0; k < g_gallery.picks; k++) {
      g_compose.media[g_compose.count++] = g_gallery.list[g_gallery.picked[k]];
    }
  }
  g_sheet = SHEET_COMPOSE;
  g_compose.focus = compose_can_send() ? C_FOCUS_SEND : C_FOCUS_TEXT;
  return 1;
}


static int
lang_shown(void) {
  int lang = g_settings.lang == LANG_SYSTEM ? g_system_lang : g_settings.lang;

  return lang >= 0 && lang < LANGS ? lang : LANG_EN;
}


/**
 * the next account with a session file, d of -1 or 1 away: every list goes,
 * to be read again with its session, and so does the draft
 **/
static int
switch_account(int d) {
  int at = g_settings.account;

  for(int i=0; i < SETTINGS_ACCOUNTS - 1; i++) {
    at = (at - 1 + d + SETTINGS_ACCOUNTS) % SETTINGS_ACCOUNTS + 1;
    if(g_accounts & (1u << (at - 1))) {
      break;
    }
  }
  if(at == g_settings.account || !(g_accounts & (1u << (at - 1)))) {
    toast(0, "%s", g_login.ready ?
          L("Para juntar outra conta, carrega em \xe2\x9c\x95 e entra com ela no X.",
            "To add another account, press \xe2\x9c\x95 and log in to X with it.") :
          L("Para juntar outra conta, carrega em \xe2\x9c\x95 e liga-a pelo PC.",
            "To add another account, press \xe2\x9c\x95 and link it from a PC."));
    return 1;
  }
  if(g_compose.sending || g_act.kind) {
    return 0;
  }
  read_account(at);
  toast(0, L("Conta %d: a ler as listas outra vez.", "Account %d: reading the lists again."), at);
  return 1;
}


/**
 * the lists of the account at, read again with its session: every list
 * goes, and so does the draft
 **/
static void
read_account(int at) {
  while(g_depth) {
    free_list(&g_stack[--g_depth]);
  }
  for(int i=0; i < SECTIONS; i++) {
    if(i == UI_HISTORY) {
      continue; // what was opened on this console stays
    }
    free_list(&g_tabs[i]);
    g_tabs[i].feed = i;
    g_tabs[i].serial = ++g_serial;
  }
  memset(&g_compose, 0, sizeof g_compose);
  memset(&g_dm, 0, sizeof g_dm);
  g_settings.account = at;
  g_settings_dirty = 1;
  g_session_found = 0;
  show_section(g_home);
}


/*
 * Linking an account from a PC (src/link.h): the screen with the console's
 * address and the code for the XPS5 extension, while main.c listens.
 */

static int
open_link(int from) {
  if(g_compose.sending) {
    return 0;
  }
  memset(&g_linking, 0, sizeof g_linking);
  g_linking.account = g_settings.account;
  g_linking.from = from;
  g_linking.st.state = UI_LINK_STARTING;
  g_sheet = SHEET_LINK;
  return 1;
}


/**
 * back where it was opened from, or to the lists once an account is linked
 **/
static int
close_link(void) {
  g_sheet = g_linking.st.state == UI_LINK_DONE ? SHEET_NONE : g_linking.from;
  return 1;
}


/**
 * the account the session goes to, d of -1 or 1 away
 **/
static int
link_account_step(int d) {
  if(g_linking.st.state == UI_LINK_CHECKING || g_linking.st.state == UI_LINK_DONE) {
    return 0;
  }
  g_linking.account = (g_linking.account - 1 + d + SETTINGS_ACCOUNTS) % SETTINGS_ACCOUNTS + 1;
  return 1;
}


int
ui_link_wanted(void) {
  return g_sheet == SHEET_LINK ? g_linking.account : 0;
}


/*
 * Logging in on the console: X's own login page in a window of XPS5's
 * (src/loginproto.h). Its page is LG_PW x LG_PH points, 520 x 600 of X's
 * CSS pixels at LG_ZOOM, where X shows its login as one card filling it.
 */

#define LG_W    1400
#define LG_H    936
#define LG_X    ((UI_WIDTH - LG_W) / 2)
#define LG_Y    ((UI_HEIGHT - LG_H) / 2)
#define LG_PW   728
#define LG_PH   840
#define LG_PX   (LG_X + LG_W - 48 - LG_PW)
#define LG_PY   (LG_Y + 48)
#define LG_R    26
#define LG_ZOOM 140      // percent, at a pixel a point
#define LG_NOTE 4000000  // how long a note shows, in microseconds

void
ui_login_ready(int ready) {
  g_login.ready = ready;
}


static int
open_login(int from) {
  if(g_compose.sending) {
    return 0;
  }
  memset(&g_login.st, 0, sizeof g_login.st);
  g_login.st.state = UI_LOGIN_STARTING;
  g_login.account = g_settings.account;
  g_login.from = from;
  g_login.pixels = NULL;
  g_login.first = g_login.count = 0;
  g_login.px = g_login.py = -1;
  g_login.wheel = 0;
  g_login.note = 0;
  // the pointer in the page's middle, shown with the page (ui_login_status)
  g_ptr.x = LG_PX + LG_PW / 2.0f;
  g_ptr.y = LG_PY + LG_PH / 2.0f;
  g_ptr.shown = 0;
  g_sheet = SHEET_LOGIN;
  return 1;
}


static int
close_login(void) {
  g_sheet = g_login.from;
  g_login.pixels = NULL;
  return 1;
}


/**
 * the login window while the engine is on the console, else the linking
 **/
static int
open_signin(int from) {
  return g_login.ready ? open_login(from) : open_link(from);
}


static int
login_account_step(int d) {
  g_login.account = (g_login.account - 1 + d + SETTINGS_ACCOUNTS) % SETTINGS_ACCOUNTS + 1;
  return 1;
}


/**
 * where the pointer is in the page's pixels; 0 when it is not on the page
 **/
static int
login_at(int *x, int *y) {
  if(!g_ptr.shown || g_ptr.x < LG_PX || g_ptr.y < LG_PY || g_ptr.x >= LG_PX + LG_PW || g_ptr.y >= LG_PY + LG_PH) {
    return 0;
  }
  *x = (int)((g_ptr.x - LG_PX) * g_scale);
  *y = (int)((g_ptr.y - LG_PY) * g_scale);
  return 1;
}


/**
 * something for main.c to pass on; a move takes the place of a move
 * before it, and when the queue is full the oldest gives way
 **/
static void
login_push(int kind, int x, int y, int d) {
  ui_login_input_t *in;

  if(g_login.st.state != UI_LOGIN_SHOWN && kind != UI_LOGIN_RETRY) {
    return; // nothing on the page yet
  }
  if(kind == UI_LOGIN_MOVE && g_login.count &&
     g_login.in[(g_login.first + g_login.count - 1) % LOGIN_INPUTS].kind == UI_LOGIN_MOVE) {
    g_login.count--;
  } else if(g_login.count == LOGIN_INPUTS) {
    g_login.first = (g_login.first + 1) % LOGIN_INPUTS;
    g_login.count--;
  }
  in = &g_login.in[(g_login.first + g_login.count++) % LOGIN_INPUTS];
  in->kind = kind;
  in->x = x;
  in->y = y;
  in->d = d;
}


int
ui_login_input(ui_login_input_t *in) {
  if(!g_login.count) {
    return 0;
  }
  *in = g_login.in[g_login.first];
  g_login.first = (g_login.first + 1) % LOGIN_INPUTS;
  g_login.count--;
  return 1;
}


static int
login_click(void) {
  int x, y;

  if(!login_at(&x, &y)) {
    return 0;
  }
  g_login.px = x;
  g_login.py = y;
  login_push(UI_LOGIN_MOVE, x, y, 0);
  login_push(UI_LOGIN_CLICK, x, y, 0);
  return 1;
}


static int
login_sticks(int moved, int ry) {
  int x, y, d;
  float s;

  if(moved && login_at(&x, &y) && (x != g_login.px || y != g_login.py)) {
    g_login.px = x;
    g_login.py = y;
    login_push(UI_LOGIN_MOVE, x, y, 0);
  }
  if(ry > STICK_DEAD || ry < -STICK_DEAD) {
    // as the lists scroll, in the page's pixels
    s = (ry > 0 ? ry - STICK_DEAD : ry + STICK_DEAD) / (128.0f - STICK_DEAD);
    s = s > 1 ? 1 : s < -1 ? -1 : s;
    g_login.wheel += s * (s < 0 ? -s : s) * SCROLL_MAX * g_step * g_scale;
    if((d=(int)g_login.wheel)) {
      g_login.wheel -= (float)d;
      if(!login_at(&x, &y)) {
        x = pxs(LG_PW) / 2;
        y = pxs(LG_PH) / 2;
      }
      login_push(UI_LOGIN_WHEEL, x, y, d);
    }
  }
  return moved;
}


static int
login_keys(unsigned keys) {
  if(keys & UI_CIRCLE) {
    return close_login();
  }
  if(keys & UI_SQUARE) {
    login_push(UI_LOGIN_RETRY, 0, 0, 0);
    return 1;
  }
  if(keys & (UI_LEFT | UI_RIGHT)) {
    return login_account_step(keys & UI_LEFT ? -1 : 1);
  }
  if(keys & UI_TRIANGLE) {
    login_push(UI_LOGIN_FIELD, 0, 0, 0);
    return 1;
  }
  if(keys & (UI_UP | UI_DOWN)) {
    login_push(UI_LOGIN_KEY, 0, 0, keys & UI_UP ? LOGIN_KEY_BACKTAB : LOGIN_KEY_TAB);
    return 1;
  }
  if(keys & UI_CROSS) {
    // the pointer was hidden: back, in the page's middle
    g_ptr.x = LG_PX + LG_PW / 2.0f;
    g_ptr.y = LG_PY + LG_PH / 2.0f;
    g_ptr.shown = 1;
    return 1;
  }
  return 0;
}


int
ui_login_wanted(int *width, int *height, int *zoom) {
  if(g_sheet != SHEET_LOGIN) {
    return 0;
  }
  *width = pxs(LG_PW);
  *height = pxs(LG_PH);
  *zoom = (int)(LG_ZOOM * g_scale + 0.5f);
  return g_login.account;
}


void
ui_login_status(const ui_login_t *st) {
  if(st->blocked > g_login.st.blocked) {
    g_login.note = 1;
    g_login.note_us = g_ui_us;
  }
  if(st->no_field > g_login.st.no_field) {
    g_login.note = 2;
    g_login.note_us = g_ui_us;
  }
  if(g_sheet == SHEET_LOGIN && st->state == UI_LOGIN_SHOWN && g_login.st.state != UI_LOGIN_SHOWN) {
    g_ptr.shown = 1; // the page came: the pointer on it, ready for its first field
  }
  g_login.st = *st;
}


void
ui_login_pixels(const uint32_t *pixels, int width, int height) {
  g_login.pixels = pixels;
  g_login.pw = width;
  g_login.ph = height;
}


void
ui_login_session(int account, int status, const char *user) {
  if(account < 1 || account > SETTINGS_ACCOUNTS) {
    return;
  }
  switch(status) {
  case UI_SESSION_CHECKING:
    toast(0, L("A confirmar a conta %d no X\xe2\x80\xa6", "Checking account %d with X\xe2\x80\xa6"), account);
    return;
  case 0:
    break;
  case UI_SESSION_NO_NETWORK:
    toast(1, "%s", L("N\xc3\xa3o deu para chegar ao X e confirmar a conta. Tenta entrar outra vez.",
                     "Could not reach X to check the account. Try signing in again."));
    return;
  case UI_SESSION_UNWRITTEN:
    toast(1, "%s", L("N\xc3\xa3o deu para guardar a sess\xc3\xa3o na consola.",
                     "Could not save the session on the console."));
    return;
  default:
    toast(1, L("O X n\xc3\xa3o aceitou a sess\xc3\xa3o (%d). Tenta entrar outra vez.",
               "X did not accept the session (%d). Try signing in again."), status);
    return;
  }
  snprintf(g_settings.users[account - 1], sizeof g_settings.users[0], "%.15s", user ? user : "");
  g_settings_dirty = 1;
  if(g_sheet == SHEET_LOGIN) {
    close_login();
  }
  if(!g_compose.sending && !g_act.kind) {
    read_account(account); // its lists, read with the new session
  }
  toast(0, L("Conta %d ligada: @%s", "Account %d linked: @%s"), account, g_settings.users[account - 1]);
}


void
ui_link_status(const ui_link_t *st) {
  int was = g_linking.st.state;

  g_linking.st = *st;
  if(st->state != UI_LINK_DONE || was == UI_LINK_DONE || st->account < 1 || st->account > SETTINGS_ACCOUNTS) {
    return;
  }
  snprintf(g_settings.users[st->account - 1], sizeof g_settings.users[0], "%.15s", st->user);
  g_settings_dirty = 1;
  if(!g_compose.sending && !g_act.kind) {
    read_account(st->account); // its lists, read again with the session
  }
  toast(0, L("Conta %d ligada: @%s", "Account %d linked: @%s"), st->account, st->user);
}


/**
 * the settings' row the buttons are on, row, shown
 **/
static void
set_row(int row) {
  g_set_row = row < 0 ? 0 : row >= S_CHOICES ? S_CHOICES - 1 : row;
  g_set_top = g_set_row < g_set_top ? g_set_row : g_set_row >= g_set_top + S_SHOWN ?
              g_set_row - S_SHOWN + 1 : g_set_top;
}


/**
 * ✕ on the Updates row: install the version found, or look for one
 **/
static int
update_press(void) {
  switch(g_update.state) {
  case UI_UPDATE_FOUND:
    g_update.wanted = 2;
    return 1;
  case UI_UPDATE_FAILED:
    g_update.wanted = g_update.version[0] ? 2 : 1;
    return 1;
  case UI_UPDATE_NONE:
  case UI_UPDATE_LATEST:
    g_update.wanted = 1;
    return 1;
  default:
    return 0;
  }
}


/**
 * the settings' rows shown d further down, or up
 **/
static int
set_scroll(int d) {
  int top = g_set_top + d;

  top = top < 0 ? 0 : top > S_ROWS - S_SHOWN ? S_ROWS - S_SHOWN : top;
  if(top == g_set_top) {
    return 0;
  }
  g_set_top = top;
  g_set_row = g_set_row < top ? top : g_set_row >= top + S_SHOWN ? top + S_SHOWN - 1 : g_set_row;
  return 1;
}


/**
 * a row of the settings changed, d of -1 or 1 for its choices; returns
 * whether it changes something
 **/
static int
setting_change(int row, int d) {
  settings_t *s = &g_settings;
  int n = sizeof g_everies / sizeof g_everies[0], at = 0;

  switch(row) {
  case S_LANG:
    s->lang = (s->lang + 1 + d + LANGS + 1) % (LANGS + 1) - 1;
    g_lang = lang_shown();
    break;
  case S_THEME:
    s->theme = s->theme == SETTINGS_NIGHT ? SETTINGS_DAY : SETTINGS_NIGHT;
    break;
  case S_SCREEN:
    n = sizeof g_screens / sizeof g_screens[0];
    for(int i=0; i<n; i++) {
      at = g_screens[i] == s->screen ? i : at;
    }
    s->screen = g_screens[(at + d + n) % n];
    break;
  case S_NOTIFY:
    s->notify = !s->notify;
    break;
  case S_EVERY:
    for(int i=0; i<n; i++) {
      at = g_everies[i] <= s->every ? i : at;
    }
    at = (at + d + n) % n;
    s->every = g_everies[at];
    break;
  case S_SOUND:
    s->sound = !s->sound;
    break;
  case S_HOVER:
    s->hover = !s->hover;
    break;
  case S_AUTOPLAY:
    s->autoplay = !s->autoplay;
    break;
  case S_SAVER:
    s->saver = !s->saver;
    break;
  case S_SPOILER:
    s->spoilers = !s->spoilers;
    relayout(NULL);
    break;
  case S_LIGHT:
    s->light = !s->light;
    break;
  case S_BREAK: {
    static const int pauses[] = SETTINGS_PAUSES;
    n = 1 + (int)(sizeof pauses / sizeof pauses[0]);
    for(int i=0; i<n; i++) {
      at = (i ? pauses[i - 1] : 0) == s->pause ? i : at;
    }
    at = (at + d + n) % n;
    s->pause = at ? pauses[at - 1] : 0;
    g_active_us = 0; // counted again from now
    break;
  }
  case S_RISING:
    s->rising = !s->rising;
    break;
  case S_SHARE:
    s->share = !s->share;
    break;
  case S_PLAYING:
    s->playing = !s->playing;
    break;
  case S_SESSION:
    return switch_account(d);
  case S_UPDATE:
    return update_press();
  default:
    return 0;
  }
  g_settings_dirty = 1;
  return 1;
}


static int
open_settings(void) {
  if(g_compose.sending) {
    return 0;
  }
  g_sheet = SHEET_SETTINGS;
  g_set_row = g_set_top = 0;
  g_set_stick = 0;
  return 1;
}


/*
 * The menus, as x.com has them: More in the bar (the bookmarks and the
 * settings) and a post's ••• (deleting it when it is the user's own), and
 * the question before a post is deleted.
 */

enum {
  M_BOOKMARKS, M_SETTINGS, M_DELETE, M_OPEN, M_PROFILE, M_BOOKMARK, M_QUOTE, M_EDIT, M_PIN, M_UNPIN,
  M_FOLLOW, M_UNFOLLOW, M_MUTE, M_UNMUTE, M_BLOCK, M_UNBLOCK, M_LISTS, M_SCHEDULED, M_UNSCHEDULE, M_THREAD,
  M_INVITE,
  M_ACCOUNT, M_ADD_ACCOUNT, M_LOGOUT, // the bar's account menu
};

#define MENU_ROWS    11
#define MENU_W       470
#define MENU_ROW     76
#define MENU_ACC_ROW 100 // a row of the account menu, with its avatar
#define MENU_GAP     17  // the line between its accounts and what follows them

static struct {
  int      count, sel;
  int      what[MENU_ROWS];
  int      arg[MENU_ROWS]; // of M_ACCOUNT: which account
  int      x, y;   // its top left
  x_post_t post;   // of a post's menu, or who a profile's is about (user_id, user, name)
  int      thread, thread_from; // its thread in the list (thread_of), for M_THREAD
} g_menu;

static struct {
  int  what;   // M_DELETE, M_BLOCK, M_UNSCHEDULE or M_LOGOUT
  char id[X_ID_MAX]; // the post to delete, or who to block, or the scheduled post
  char user[64];
  int  account; // of M_LOGOUT
  int  focus;  // 0: delete, 1: cancel
} g_ask;


/**
 * the thread the post at of a post's list (UI_POST) is in: the posts by its
 * author around it, one after the other; how many, from *from
 **/
static int
thread_of(const tab_t *t, int at, int *from) {
  const char *who;
  int a = at, b = at;

  *from = at;
  if(t->feed != UI_POST || at < 0 || at >= t->count || t->posts[at].kind != X_KIND_POST ||
     !t->posts[at].user_id[0]) {
    return 0;
  }
  who = t->posts[at].user_id;
  while(a > 0 && t->posts[a - 1].kind == X_KIND_POST && !strcmp(t->posts[a - 1].user_id, who)) {
    a--;
  }
  while(b + 1 < t->count && t->posts[b + 1].kind == X_KIND_POST && !strcmp(t->posts[b + 1].user_id, who)) {
    b++;
  }
  *from = a;
  return b - a + 1;
}


/*
 * The reader: a thread as one text to read, its posts one after the other
 * with their pictures, from the menu of one of them.
 */

#define R_X      300
#define R_Y      34
#define R_W      1320
#define R_H      1012
#define R_TEXT_X (R_X + 110)
#define R_TEXT_W (R_W - 220)
#define R_TOP    (R_Y + 150)      // where its text starts on the screen
#define R_BOTTOM (R_Y + R_H - 86) // and where it ends, over the hints
#define R_LINE_H 54
#define R_PART_H 70
#define R_POSTS  30
#define R_ITEMS  1600

enum { R_LINE, R_MEDIA, R_PART };

static struct {
  x_post_t *posts; // the thread's, copied
  int       count;
  int       items;
  struct {
    short    post, kind; // R_*
    uint16_t start, len; // of a line, in its post's text
    int      y, h;       // from the top of the text
  } item[R_ITEMS];
  int       height;
  int       scroll, target;
  int       minutes;     // to read it
} g_reader;


static void
reader_add(int post, int kind, int start, int len, int y, int h) {
  if(g_reader.items < R_ITEMS) {
    g_reader.item[g_reader.items].post = (short)post;
    g_reader.item[g_reader.items].kind = (short)kind;
    g_reader.item[g_reader.items].start = (uint16_t)start;
    g_reader.item[g_reader.items].len = (uint16_t)len;
    g_reader.item[g_reader.items].y = y;
    g_reader.item[g_reader.items].h = h;
    g_reader.items++;
  }
}


/**
 * where the pictures of the post p go in the reader: one as big as it fits,
 * several side by side; their height
 **/
static int
reader_media(const x_post_t *p, rect_t *r) {
  int n = p->media_count > X_MAX_MEDIA ? X_MAX_MEDIA : p->media_count, w, h;

  if(n == 1) {
    w = R_TEXT_W < 880 ? R_TEXT_W : 880;
    h = p->media[0].width > 0 && p->media[0].height > 0 ?
        (int)((long long)w * p->media[0].height / p->media[0].width) : w * 9 / 16;
    if(h > 560) {
      w = (int)((long long)w * 560 / h);
      h = 560;
    }
    h = h < 200 ? 200 : h;
    r[0] = (rect_t){0, 0, w, h};
    return h;
  }
  w = (R_TEXT_W - (n - 1) * 10) / n;
  for(int i=0; i<n; i++) {
    r[i] = (rect_t){i * (w + 10), 0, w, 340};
  }
  return 340;
}


static int
close_reader(void) {
  mem_free(g_reader.posts);
  g_reader.posts = NULL;
  g_reader.count = g_reader.items = 0;
  if(g_sheet == SHEET_READER) {
    g_sheet = SHEET_NONE;
  }
  return 1;
}


/**
 * the count posts of the list t from from, as one text in the reader
 **/
static int
open_reader(const tab_t *t, int from, int count) {
  static uint16_t start[MAX_LINES], len[MAX_LINES];
  rect_t r[X_MAX_MEDIA];
  int lines, more, y = 0, words = 0, h;

  close_reader();
  count = count > R_POSTS ? R_POSTS : count;
  if(count < 1 || from < 0 || from + count > t->count ||
     !(g_reader.posts=mem_alloc((size_t)count * sizeof *g_reader.posts))) {
    toast(1, "%s", L("O fio n\xc3\xa3o abriu.", "The thread did not open."));
    return 1;
  }
  memcpy(g_reader.posts, t->posts + from, (size_t)count * sizeof *g_reader.posts);
  g_reader.count = count;
  for(int i=0; i < count; i++) {
    const x_post_t *p = &g_reader.posts[i];
    if(i) {
      reader_add(i, R_PART, 0, 0, y, R_PART_H);
      y += R_PART_H;
    }
    lines = break_lines(font(F_COMPOSE), p->text, R_TEXT_W, MAX_LINES, start, len, &more);
    for(int l=0; l < lines; l++) {
      reader_add(i, R_LINE, start[l], len[l], y, R_LINE_H);
      y += R_LINE_H;
    }
    if(p->media_count) {
      y += lines ? 18 : 0;
      h = reader_media(p, r);
      reader_add(i, R_MEDIA, 0, 0, y, h);
      y += h;
    }
    // its words, for how long it takes to read
    for(const char *c=p->text; *c; c++) {
      words += *c != ' ' && *c != '\n' && (c == p->text || c[-1] == ' ' || c[-1] == '\n');
    }
  }
  g_reader.height = y;
  g_reader.minutes = words / 200 + 1;
  g_reader.scroll = g_reader.target = 0;
  g_sheet = SHEET_READER;
  return 1;
}


static void
reader_clamp(void) {
  int max = g_reader.height - (R_BOTTOM - R_TOP) + 40;

  max = max < 0 ? 0 : max;
  g_reader.target = g_reader.target < 0 ? 0 : g_reader.target > max ? max : g_reader.target;
}


static void
reader_stick(int d) {
  g_reader.target += d;
  reader_clamp();
  g_reader.scroll = g_reader.target;
}


static void
draw_reader(void) {
  text_font_t *title = font(F_BRAND), *small = font(F_SMALL), *body = font(F_COMPOSE);
  const x_post_t *first = g_reader.posts;
  const int band = R_BOTTOM - R_TOP;
  text_surface_t screen = g_dst;
  const uint32_t *pixels;
  char line[300], buf[IMAGES_URL_MAX];
  rect_t r[X_MAX_MEDIA];
  int y;

  draw_pane(R_X, R_Y, R_W, R_H);
  if(!first) {
    return;
  }
  draw_avatar(first->avatar, first->user, first->name, R_X + 40, R_Y + 34, 72);
  snprintf(line, sizeof line, L("Fio de %s", "Thread by %s"), first->name[0] ? first->name : first->user);
  text_fitted(title, R_X + 134, R_Y + 66, line, R_W - 134 - 150, FG);
  snprintf(line, sizeof line, L("@%s \xc2\xb7 %d posts \xc2\xb7 %d min de leitura", "@%s \xc2\xb7 %d posts \xc2\xb7 %d min read"),
           first->user, g_reader.count, g_reader.minutes);
  text_fitted(small, R_X + 134, R_Y + 102, line, R_W - 134 - 150, GREY);
  round_button(Z_R_CLOSE, R_X + R_W - 70, R_Y + 66, ICON_CROSS);
  draw_rect(&g_dst, R_X + 40, R_TOP - 26, R_W - 80, 2, RGBA(255, 255, 255, 26));

  // its text in a band of the screen, which cuts what scrolls past
  g_dst = text_band(&screen, R_TOP, band);
  g_off_y = g_clip_top = R_TOP;
  g_clip_bottom = R_BOTTOM;
  for(int i=0; i < g_reader.items; i++) {
    const x_post_t *p = &g_reader.posts[g_reader.item[i].post];
    y = g_reader.item[i].y - g_reader.scroll;
    if(y + g_reader.item[i].h < 0 || y > band) {
      continue;
    }
    switch(g_reader.item[i].kind) {
    case R_LINE:
      text_draw(body, &g_dst, R_TEXT_X, baseline(body, y, R_LINE_H), p->text + g_reader.item[i].start,
                g_reader.item[i].len, FG);
      break;
    case R_PART:
      snprintf(line, sizeof line, "%d/%d", g_reader.item[i].post + 1, g_reader.count);
      draw_rect(&g_dst, R_TEXT_X + 70, y + R_PART_H / 2, R_TEXT_W - 70, 2, RGBA(255, 255, 255, 20));
      text(small, R_TEXT_X, baseline(small, y, R_PART_H), line, ACCENT);
      break;
    case R_MEDIA:
      reader_media(p, r);
      for(int m=0; m < p->media_count && m < X_MAX_MEDIA; m++) {
        int mx = R_TEXT_X + r[m].x, my = y + r[m].y;
        if((pixels=picture(media_url(p->media[m].url, p->media_count == 1 ? "medium" : "small", buf, sizeof buf),
                           r[m].w, r[m].h))) {
          draw_picture_round(&g_dst, mx, my, r[m].w, r[m].h, RADIUS, pixels);
        } else {
          draw_box(&g_dst, mx, my, r[m].w, r[m].h, RADIUS, 0, PLACEHOLDER);
        }
        if(p->media[m].video) {
          draw_play(mx + r[m].w / 2.0f, my + r[m].h / 2.0f, 46);
        }
      }
      break;
    }
  }
  g_dst = screen;
  g_off_y = g_clip_top = 0;
  g_clip_bottom = UI_HEIGHT;

  if(g_reader.height > band) {
    // how far down it is
    const int th = band - 20, kh = th * band / g_reader.height < 60 ? 60 : th * band / g_reader.height;
    const int max = g_reader.height - band + 40, ky = R_TOP + 10 + (th - kh) * g_reader.scroll / (max > 0 ? max : 1);
    draw_box(&g_dst, R_X + R_W - 34, R_TOP + 10, 6, th, 3, 0, RGBA(255, 255, 255, 22));
    draw_box(&g_dst, R_X + R_W - 34, ky, 6, kh, 3, 0, RGBA(255, 255, 255, 120));
  }
  draw_hints(R_X + R_W / 2, R_Y + R_H - 62,
             L("\xe2\x86\x91 \xe2\x86\x93 Ler  \xc2\xb7  L1 R1 P\xc3\xa1gina  \xc2\xb7  \xe2\x96\xb3 In\xc3\xad" "cio  \xc2\xb7  \xe2\x97\x8b Fechar",
               "\xe2\x86\x91 \xe2\x86\x93 Read  \xc2\xb7  L1 R1 Page  \xc2\xb7  \xe2\x96\xb3 Start  \xc2\xb7  \xe2\x97\x8b Close"));
}


/**
 * the post is the signed-in user's own
 **/
static int
is_mine(const x_post_t *p) {
  const tab_t *me = &g_tabs[UI_ME];

  return me->has_user && p->kind == X_KIND_POST &&
         ((p->user_id[0] && !strcmp(p->user_id, me->user.id)) || (p->user[0] && !strcmp(p->user, me->user.user)));
}


/**
 * how far down the menu its row i starts (at g_menu.count, where its rows end)
 **/
static int
menu_offset(int i) {
  int y = 12;

  for(int k=0; k < i; k++) {
    y += g_menu.what[k] == M_ACCOUNT ? MENU_ACC_ROW : MENU_ROW;
    if(g_menu.what[k] == M_ACCOUNT && k + 1 < g_menu.count && g_menu.what[k + 1] != M_ACCOUNT) {
      y += MENU_GAP;
    }
  }
  return y;
}


static int
menu_height(void) {
  return menu_offset(g_menu.count) + 12;
}


/**
 * More: its menu over its item
 **/
static int
open_more(void) {
  if(g_compose.sending) {
    return 0;
  }
  memset(&g_menu, 0, sizeof g_menu);
  g_menu.what[g_menu.count++] = M_BOOKMARKS;
  g_menu.what[g_menu.count++] = M_LISTS;
  g_menu.what[g_menu.count++] = M_SCHEDULED;
  g_menu.what[g_menu.count++] = M_SETTINGS;
  g_menu.x = NAV_X + 8;
  g_menu.y = NAV_Y + (NAV_ITEMS - 1) * NAV_ITEM - menu_height();
  g_sheet = SHEET_MENU;
  return 1;
}


/**
 * the bar's account: its menu over it, as x.com has it: the accounts with a
 * session on this console (the one in use first, ticked), adding another
 * one, and logging out of the one in use
 **/
static int
open_accounts(void) {
  const int at = g_settings.account;

  if(g_compose.sending) {
    return 0;
  }
  memset(&g_menu, 0, sizeof g_menu);
  g_menu.what[g_menu.count] = M_ACCOUNT;
  g_menu.arg[g_menu.count++] = at;
  for(int a=1; a <= SETTINGS_ACCOUNTS; a++) {
    if(a != at && (g_accounts >> (a - 1) & 1)) {
      g_menu.what[g_menu.count] = M_ACCOUNT;
      g_menu.arg[g_menu.count++] = a;
    }
  }
  g_menu.what[g_menu.count++] = M_ADD_ACCOUNT;
  g_menu.what[g_menu.count++] = M_LOGOUT;
  g_menu.x = NAV_X;
  g_menu.y = NAV_ME_Y - 12 - menu_height();
  g_sheet = SHEET_MENU;
  return 1;
}


/**
 * the lists read again with another account's session
 **/
static int
use_account(int at) {
  if(at == g_settings.account) {
    return 1;
  }
  if(g_compose.sending || g_act.kind) {
    toast(1, "%s", L("Espera que acabe o que est\xc3\xa1 a ser enviado", "Wait for what is being sent"));
    return 1;
  }
  read_account(at);
  toast(0, L("Conta %d: a ler as listas outra vez.", "Account %d: reading the lists again."), at);
  return 1;
}


/**
 * the login window (or the linking) for the first account without a session
 **/
static int
add_account(void) {
  int free = 0;

  for(int a=SETTINGS_ACCOUNTS; a >= 1; a--) {
    if(!(g_accounts >> (a - 1) & 1)) {
      free = a;
    }
  }
  if(!free) {
    toast(1, L("O XPS5 guarda at\xc3\xa9 %d contas. Termina a sess\xc3\xa3o de uma para juntar outra.",
               "XPS5 keeps up to %d accounts. Log out of one to add another."), SETTINGS_ACCOUNTS);
    return 1;
  }
  if(!open_signin(SHEET_NONE)) {
    return 0;
  }
  if(g_sheet == SHEET_LOGIN) {
    g_login.account = free;
  } else {
    g_linking.account = free;
  }
  return 1;
}


static int g_logout; // the account logged out, for main.c (ui_logout_take), or 0

/**
 * logged out of account on this console: main.c leaves its session file
 * empty, and the lists are read with the next account that has a session,
 * or with none, which asks to log in
 **/
static int
log_out(int account, const char *user) {
  int next = 0;

  if(account < 1 || account > SETTINGS_ACCOUNTS) {
    return 0;
  }
  if(g_compose.sending || g_act.kind) {
    toast(1, "%s", L("Espera que acabe o que est\xc3\xa1 a ser enviado", "Wait for what is being sent"));
    return 1;
  }
  g_logout = account;
  g_accounts &= ~(1u << (account - 1));
  g_settings.users[account - 1][0] = 0;
  g_settings.ids[account - 1][0] = 0;
  for(int a=1; a <= SETTINGS_ACCOUNTS && !next; a++) {
    if(g_accounts >> (a - 1) & 1) {
      next = a;
    }
  }
  read_account(next ? next : account);
  if(user[0]) {
    toast(0, L("Terminaste a sess\xc3\xa3o de @%s nesta PS5.", "Logged out @%s on this PS5."), user);
  } else {
    toast(0, "%s", L("Terminaste a sess\xc3\xa3o nesta PS5.", "Logged out on this PS5."));
  }
  return 1;
}


int
ui_logout_take(void) {
  int account = g_logout;

  g_logout = 0;
  return account;
}


/**
 * a post's •••: its menu under the dots (over them near the bottom), its
 * right edge at right
 **/
static int
open_post_menu(const x_post_t *p, int right, int top, int bottom) {
  const tab_t *t = view();

  if(g_compose.sending || !p->id[0]) {
    return 0;
  }
  memset(&g_menu, 0, sizeof g_menu);
  g_menu.post = *p;
  for(int i=0; i < t->count; i++) {
    if(!strcmp(t->posts[i].id, p->id)) {
      if((g_menu.thread=thread_of(t, i, &g_menu.thread_from)) >= 2) {
        g_menu.what[g_menu.count++] = M_THREAD; // read as one text
      }
      break;
    }
  }
  if(is_mine(p)) {
    g_menu.what[g_menu.count++] = M_DELETE;
    // X Premium edits a post's text for an hour; with media it stays as it is here
    if(p->edit_until > g_now && !p->media_count && premium()) {
      g_menu.what[g_menu.count++] = M_EDIT;
    }
    g_menu.what[g_menu.count++] = p->pinned ? M_UNPIN : M_PIN;
  }
  if(!(t->feed == UI_POST && !strcmp(t->arg, p->id))) {
    g_menu.what[g_menu.count++] = M_OPEN;
  }
  g_menu.what[g_menu.count++] = M_QUOTE;
  if(p->user_id[0] && !is_mine(p)) {
    g_menu.what[g_menu.count++] = p->following ? M_UNFOLLOW : M_FOLLOW;
    g_menu.what[g_menu.count++] = M_PROFILE;
    g_menu.what[g_menu.count++] = M_MUTE;
    g_menu.what[g_menu.count++] = M_BLOCK;
  }
  g_menu.what[g_menu.count++] = M_BOOKMARK;
  g_menu.x = right - MENU_W;
  g_menu.y = bottom + 6 + menu_height() <= UI_HEIGHT - 20 ? bottom + 6 : top - 6 - menu_height();
  g_menu.y = g_menu.y < 20 ? 20 : g_menu.y;
  g_sheet = SHEET_MENU;
  return 1;
}


/**
 * a profile's menu: muting and blocking, or taking it back
 **/
static int
open_user_menu(const struct tab *t, int right, int top, int bottom) {
  if(!t->has_user || !t->user.id[0]) {
    return 0;
  }
  memset(&g_menu, 0, sizeof g_menu);
  snprintf(g_menu.post.user_id, sizeof g_menu.post.user_id, "%s", t->user.id);
  snprintf(g_menu.post.user, sizeof g_menu.post.user, "%s", t->user.user);
  snprintf(g_menu.post.name, sizeof g_menu.post.name, "%s", t->user.name);
  g_menu.post.following = t->user.followed;
  play_game_of(t->user.location, g_menu.post.playing, sizeof g_menu.post.playing);
  if(strcmp(t->user.id, g_settings.ids[g_settings.account - 1])) {
    g_menu.what[g_menu.count++] = M_INVITE;
  }
  g_menu.what[g_menu.count++] = t->user.muted ? M_UNMUTE : M_MUTE;
  g_menu.what[g_menu.count++] = t->user.blocked ? M_UNBLOCK : M_BLOCK;
  g_menu.x = right - MENU_W;
  g_menu.y = bottom + 6;
  (void)top;
  g_sheet = SHEET_MENU;
  return 1;
}


/**
 * a row of the menu: its words, icon and colour
 **/
static void
menu_row(int what, char *label, size_t size, int *icon, uint32_t *color) {
  *color = FG;
  switch(what) {
  case M_BOOKMARKS:
    *icon = ICON_BOOKMARK;
    snprintf(label, size, "%s", L("Itens guardados", "Bookmarks"));
    break;
  case M_SETTINGS:
    *icon = ICON_GEAR;
    snprintf(label, size, "%s", L("Defini\xc3\xa7\xc3\xb5" "es", "Settings"));
    break;
  case M_LISTS:
    *icon = ICON_LIST;
    snprintf(label, size, "%s", L("Listas", "Lists"));
    break;
  case M_SCHEDULED:
    *icon = ICON_CLOCK;
    snprintf(label, size, "%s", L("Posts agendados", "Scheduled posts"));
    break;
  case M_DELETE:
    *icon = ICON_TRASH;
    *color = RED;
    snprintf(label, size, "%s", L("Apagar", "Delete"));
    break;
  case M_OPEN:
    *icon = ICON_REPLY;
    snprintf(label, size, "%s", L("Abrir o post", "Open the post"));
    break;
  case M_THREAD:
    *icon = ICON_ARTICLE;
    snprintf(label, size, L("Ler o fio (%d posts)", "Read the thread (%d posts)"), g_menu.thread);
    break;
  case M_PROFILE:
    *icon = ICON_PERSON;
    snprintf(label, size, L("Ver o perfil de @%s", "View @%s's profile"), g_menu.post.user);
    break;
  case M_QUOTE:
    *icon = ICON_REPOST;
    snprintf(label, size, "%s", L("Citar", "Quote"));
    break;
  case M_EDIT:
    *icon = ICON_ARTICLE;
    snprintf(label, size, "%s", L("Editar", "Edit"));
    break;
  case M_PIN:
    *icon = ICON_TOP;
    snprintf(label, size, "%s", L("Fixar no teu perfil", "Pin to your profile"));
    break;
  case M_UNPIN:
    *icon = ICON_TOP;
    snprintf(label, size, "%s", L("Desafixar do perfil", "Unpin from profile"));
    break;
  case M_FOLLOW:
    *icon = ICON_PERSON;
    snprintf(label, size, L("Seguir @%s", "Follow @%s"), g_menu.post.user);
    break;
  case M_UNFOLLOW:
    *icon = ICON_PERSON;
    snprintf(label, size, L("Deixar de seguir @%s", "Unfollow @%s"), g_menu.post.user);
    break;
  case M_MUTE:
    *icon = ICON_MUTE;
    snprintf(label, size, L("Silenciar @%s", "Mute @%s"), g_menu.post.user);
    break;
  case M_UNMUTE:
    *icon = ICON_SOUND;
    snprintf(label, size, L("Deixar de silenciar @%s", "Unmute @%s"), g_menu.post.user);
    break;
  case M_BLOCK:
    *icon = ICON_CROSS;
    *color = RED;
    snprintf(label, size, L("Bloquear @%s", "Block @%s"), g_menu.post.user);
    break;
  case M_UNBLOCK:
    *icon = ICON_CROSS;
    snprintf(label, size, L("Desbloquear @%s", "Unblock @%s"), g_menu.post.user);
    break;
  case M_INVITE:
    *icon = ICON_GAME;
    snprintf(label, size, "%s", L("Convidar para jogar", "Invite to play"));
    break;
  case M_ADD_ACCOUNT: // words only, as x.com's account menu
    *icon = -1;
    snprintf(label, size, "%s", L("Adicionar uma conta existente", "Add an existing account"));
    break;
  case M_LOGOUT:
    *icon = -1;
    if(g_settings.users[g_settings.account - 1][0]) {
      snprintf(label, size, L("Terminar a sess\xc3\xa3o de @%s", "Log out @%s"),
               g_settings.users[g_settings.account - 1]);
    } else {
      snprintf(label, size, "%s", L("Terminar a sess\xc3\xa3o", "Log out"));
    }
    break;
  default:
    *icon = g_menu.post.bookmarked ? ICON_BOOKMARKED : ICON_BOOKMARK;
    snprintf(label, size, "%s", g_menu.post.bookmarked ? L("Remover dos itens guardados", "Remove from Bookmarks") :
             L("Guardar", "Bookmark"));
    break;
  }
}


/**
 * what a row of the menu does
 **/
static int
menu_choose(int row) {
  tab_t *t = view();
  int what;

  if(row < 0 || row >= g_menu.count) {
    return 0;
  }
  what = g_menu.what[row];
  g_sheet = SHEET_NONE;
  switch(what) {
  case M_BOOKMARKS:
    show_section(UI_BOOKMARKS);
    return 1;
  case M_SETTINGS:
    return open_settings();
  case M_LISTS:
    show_section(UI_LISTS);
    return 1;
  case M_SCHEDULED:
    return open_scheduled();
  case M_DELETE:
  case M_BLOCK:
    // asked first, as x.com does
    memset(&g_ask, 0, sizeof g_ask);
    g_ask.what = what;
    snprintf(g_ask.id, sizeof g_ask.id, "%s", what == M_DELETE ? g_menu.post.id : g_menu.post.user_id);
    snprintf(g_ask.user, sizeof g_ask.user, "%s", g_menu.post.user);
    g_sheet = SHEET_CONFIRM;
    return 1;
  case M_OPEN:
    return open_post(&g_menu.post);
  case M_THREAD:
    return open_reader(t, g_menu.thread_from, g_menu.thread);
  case M_QUOTE:
    return compose_open(&g_menu.post, CM_QUOTE);
  case M_EDIT:
    return compose_open(&g_menu.post, CM_EDIT);
  case M_PIN:
  case M_UNPIN:
    return act_on(what == M_PIN ? UI_ACT_PIN : UI_ACT_UNPIN, g_menu.post.id, g_menu.post.user);
  case M_FOLLOW:
  case M_UNFOLLOW:
  case M_MUTE:
  case M_UNMUTE:
  case M_UNBLOCK:
    return act_on(what == M_FOLLOW ? UI_ACT_FOLLOW : what == M_UNFOLLOW ? UI_ACT_UNFOLLOW :
                  what == M_MUTE ? UI_ACT_MUTE : what == M_UNMUTE ? UI_ACT_UNMUTE : UI_ACT_UNBLOCK,
                  g_menu.post.user_id, g_menu.post.user);
  case M_PROFILE:
    open_user(&g_menu.post);
    return 1;
  case M_INVITE:
    g_sheet = SHEET_NONE;
    return invite(g_menu.post.user_id, g_menu.post.user, g_menu.post.playing);
  case M_ACCOUNT:
    return use_account(g_menu.arg[row]);
  case M_ADD_ACCOUNT:
    return add_account();
  case M_LOGOUT:
    // asked first, as x.com does
    memset(&g_ask, 0, sizeof g_ask);
    g_ask.what = M_LOGOUT;
    g_ask.account = g_settings.account;
    snprintf(g_ask.user, sizeof g_ask.user, "%s", g_settings.users[g_settings.account - 1]);
    g_sheet = SHEET_CONFIRM;
    return 1;
  default:
    // the post the menu opened on is the selected one
    if(t->count && !strcmp(t->posts[t->sel].id, g_menu.post.id)) {
      return act(t, UI_ACT_BOOKMARK);
    }
    return 1;
  }
}


/**
 * the sheet of the user's scheduled posts, read again from X
 **/
static int
open_scheduled(void) {
  if(g_compose.sending) {
    return 0;
  }
  g_sheet = SHEET_SCHEDULED;
  if(!g_sched.loading) {
    g_sched.wanted = 1;
    g_sched.loading = 1;
  }
  return 1;
}


/**
 * the question before the scheduled post i is taken back
 **/
static int
ask_unschedule(int i) {
  if(i < 0 || i >= g_sched.count) {
    return 0;
  }
  memset(&g_ask, 0, sizeof g_ask);
  g_ask.what = M_UNSCHEDULE;
  snprintf(g_ask.id, sizeof g_ask.id, "%s", g_sched.items[i].id);
  g_sheet = SHEET_CONFIRM;
  return 1;
}


/**
 * the question's yes: the post goes to X to be deleted, or the user to be
 * blocked, or the scheduled post to be taken back
 **/
static int
delete_post(void) {
  g_sheet = g_ask.what == M_UNSCHEDULE ? SHEET_SCHEDULED : SHEET_NONE;
  if(g_ask.what == M_LOGOUT) {
    return log_out(g_ask.account, g_ask.user);
  }
  if(g_ask.what == M_BLOCK) {
    return act_on(UI_ACT_BLOCK, g_ask.id, g_ask.user);
  }
  if(g_act.kind) {
    toast(1, "%s", L("Espera que acabe o que est\xc3\xa1 a ser enviado", "Wait for what is being sent"));
    return 1;
  }
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = g_ask.what == M_UNSCHEDULE ? UI_ACT_UNSCHEDULE : UI_ACT_DELETE;
  snprintf(g_act.id, sizeof g_act.id, "%s", g_ask.id);
  return 1;
}


/**
 * every copy of a deleted post goes from the lists (not the notices about
 * it), and a list opened on it closes
 **/
static void
remove_post(const char *id) {
  tab_t *t;
  int kept, before, above;

  while(g_depth && view()->feed == UI_POST && !strcmp(view()->arg, id)) {
    free_list(&g_stack[--g_depth]);
  }
  for(int i=0; (t=list_at(i)); i++) {
    kept = before = above = 0;
    for(int k=0; k < t->count; k++) {
      if(t->posts[k].kind != X_KIND_NOTICE && !strcmp(t->posts[k].id, id)) {
        before += k < t->sel;
        above += k < t->seen_at;
        continue;
      }
      if(kept != k) {
        t->posts[kept] = t->posts[k];
        t->cards[kept] = t->cards[k];
      }
      kept++;
    }
    if(kept == t->count) {
      continue;
    }
    t->count = kept;
    for(int k=0; k < kept; k++) {
      t->cards[k].post = &t->posts[k];
    }
    t->sel -= before;
    t->sel = t->sel >= kept ? kept - 1 : t->sel;
    t->sel = t->sel < 0 ? 0 : t->sel;
    t->seen_at -= above;
    t->seen_at = t->seen_at > 0 && t->seen_at < kept ? t->seen_at : 0;
    t->error = kept ? t->error : ERR_EMPTY;
    place(t);
    clamp_target(t);
  }
  for(int k=0; k < g_history_count; ) {
    if(!strcmp(g_history[k].id, id)) {
      memmove(&g_history[k], &g_history[k + 1], (size_t)(g_history_count - k - 1) * sizeof *g_history);
      g_history_count--;
      g_history_changed = 1;
    } else {
      k++;
    }
  }
  if(g_tabs[UI_ME].has_user && g_tabs[UI_ME].user.posts > 0) {
    g_tabs[UI_ME].user.posts--;
  }
}


/**
 * the keyboard for the choice i of the composer's poll
 **/
static int
ask_choice(int i) {
  if(g_act.kind || g_compose.sending || i < 0 || i >= g_compose.poll) {
    return 0;
  }
  g_compose.focus = C_FOCUS_CHOICE + i;
  g_compose.editing = i + 1;
  memset(&g_act, 0, sizeof g_act);
  g_act.kind = UI_ACT_POLL_TEXT;
  return 1;
}


/**
 * the bar's poll button: a poll of two choices, the first being written;
 * or none again
 **/
static int
poll_toggle(void) {
  if(g_compose.sending || g_compose.quote || g_compose.edit) {
    return 0;
  }
  if(g_compose.poll) {
    g_compose.poll = 0;
    memset(g_compose.choice, 0, sizeof g_compose.choice);
    g_compose.focus = C_FOCUS_POLL;
    return 1;
  }
  if(g_compose.count) {
    toast(1, "%s", L("Uma sondagem n\xc3\xa3o leva fotos nem v\xc3\xad" "deos: tira-os com \xe2\x96\xa1.",
                     "A poll takes no photos or videos: remove them with \xe2\x96\xa1."));
    return 1;
  }
  if(compose_scheduled()) {
    toast(1, "%s", L("Um post com sondagem sai logo: volta a Agora com L1 ou R1.",
                     "A post with a poll goes out now: go back to Now with L1 or R1."));
    return 1;
  }
  g_compose.poll = 2;
  g_compose.length = POLL_LENGTH_DAY;
  return ask_choice(0);
}


/**
 * one more choice in the composer's poll, written at once
 **/
static int
add_choice(void) {
  if(g_compose.sending || !g_compose.poll || g_compose.poll >= X_POLL_MAX || g_act.kind) {
    return 0;
  }
  g_compose.choice[g_compose.poll][0] = 0;
  g_compose.poll++;
  return ask_choice(g_compose.poll - 1);
}


/**
 * the choice i taken from the composer's poll, which keeps two
 **/
static int
remove_choice(int i) {
  if(g_compose.sending || i < 0 || i >= g_compose.poll) {
    return 0;
  }
  if(g_compose.poll <= 2) {
    toast(1, "%s", L("Uma sondagem tem pelo menos 2 op\xc3\xa7\xc3\xb5" "es.", "A poll has at least 2 choices."));
    return 1;
  }
  memmove(&g_compose.choice[i], &g_compose.choice[i + 1], (size_t)(g_compose.poll - i - 1) * sizeof g_compose.choice[0]);
  g_compose.poll--;
  g_compose.choice[g_compose.poll][0] = 0;
  if(g_compose.focus >= C_FOCUS_CHOICE + g_compose.poll && g_compose.focus < C_FOCUS_CHOICE_ADD) {
    g_compose.focus = C_FOCUS_CHOICE + g_compose.poll - 1;
  }
  return 1;
}


/**
 * how long the composer's poll stays open, d of -1 or 1
 **/
static int
poll_length(int d) {
  if(!g_compose.poll || g_compose.sending) {
    return 0;
  }
  g_compose.length = (g_compose.length + d + POLL_LENGTHS) % POLL_LENGTHS;
  return 1;
}


/**
 * the buttons while a sheet is up
 **/
static int
sheet_keys(unsigned keys) {
  int d = keys & (UI_UP | UI_LEFT) ? -1 : 1, rows, sel;

  if(g_sheet != SHEET_LOGIN && (keys & (UI_UP | UI_DOWN | UI_LEFT | UI_RIGHT | UI_L1 | UI_R1))) {
    g_ptr.shown = 0;
  }
  switch(g_sheet) {
  case SHEET_LOGIN:
    return login_keys(keys);
  case SHEET_COMPOSE:
    if(keys & UI_CIRCLE) {
      return close_composer();
    }
    if(g_compose.sending) {
      return 0;
    }
    if(keys & (UI_R2 | UI_OPTIONS)) {
      return publish();
    }
    if(keys & (UI_L1 | UI_R1)) {
      return compose_schedule(keys & UI_R1 ? 1 : -1);
    }
    if(keys & UI_L2) {
      return compose_tag_game();
    }
    if(keys & UI_TRIANGLE) {
      return open_gallery();
    }
    if(keys & UI_SQUARE) {
      if(g_compose.focus >= C_FOCUS_CHOICE && g_compose.focus < C_FOCUS_CHOICE_ADD) {
        return remove_choice(g_compose.focus - C_FOCUS_CHOICE);
      }
      remove_media(g_compose.focus >= C_FOCUS_MEDIA && g_compose.focus < C_FOCUS_ADD ?
                   g_compose.focus - C_FOCUS_MEDIA : g_compose.count - 1);
      return 1;
    }
    if(keys & UI_CROSS) {
      if(g_compose.focus == C_FOCUS_POLL) {
        return poll_toggle();
      }
      if(g_compose.focus == C_FOCUS_DM) {
        return pick_chat();
      }
      if(g_compose.focus >= C_FOCUS_CHOICE && g_compose.focus < C_FOCUS_CHOICE_ADD) {
        return ask_choice(g_compose.focus - C_FOCUS_CHOICE);
      }
      if(g_compose.focus == C_FOCUS_CHOICE_ADD) {
        return add_choice();
      }
      if(g_compose.focus == C_FOCUS_LENGTH) {
        return poll_length(1);
      }
      if(g_compose.focus == C_FOCUS_TEXT) {
        return ask_text();
      }
      if(g_compose.focus == C_FOCUS_ADD) {
        return open_gallery();
      }
      if(g_compose.focus == C_FOCUS_SEND) {
        return publish();
      }
      remove_media(g_compose.focus - C_FOCUS_MEDIA);
      return 1;
    }
    if((keys & (UI_LEFT | UI_RIGHT)) && g_compose.focus == C_FOCUS_LENGTH) {
      return poll_length(keys & UI_RIGHT ? 1 : -1);
    }
    if(keys & (UI_UP | UI_DOWN | UI_LEFT | UI_RIGHT)) {
      compose_move(d);
      return 1;
    }
    return 0;

  case SHEET_GALLERY:
    if(keys & UI_CIRCLE) {
      g_sheet = SHEET_COMPOSE;
      return 1;
    }
    if(keys & (UI_R2 | UI_OPTIONS | UI_TRIANGLE)) {
      return gallery_done();
    }
    if(keys & UI_SQUARE) {
      return open_gallery();
    }
    if(keys & UI_CROSS) {
      return pick(g_gallery.sel);
    }
    if(g_gallery.state != GALLERY_READY || !g_gallery.count) {
      return 0;
    }
    rows = (g_gallery.count + G_COLS - 1) / G_COLS;
    sel = g_gallery.sel;
    if(keys & UI_LEFT) {
      sel--;
    } else if(keys & UI_RIGHT) {
      sel++;
    } else if(keys & UI_UP) {
      sel -= G_COLS;
    } else if(keys & UI_DOWN) {
      sel = sel / G_COLS + 1 < rows ? sel + G_COLS : sel;
    } else if(keys & (UI_L1 | UI_R1)) {
      sel += (keys & UI_L1 ? -3 : 3) * G_COLS;
    }
    sel = sel < 0 ? (keys & UI_L1 ? 0 : g_gallery.sel) : sel >= g_gallery.count ? g_gallery.count - 1 : sel;
    if(sel == g_gallery.sel) {
      return 0;
    }
    g_gallery.sel = sel;
    gallery_show_sel();
    return 1;

  case SHEET_MENU:
    if(keys & UI_CIRCLE) {
      g_sheet = SHEET_NONE;
      return 1;
    }
    if(keys & (UI_UP | UI_DOWN)) {
      g_menu.sel += keys & UI_UP ? -1 : 1;
      g_menu.sel = g_menu.sel < 0 ? 0 : g_menu.sel >= g_menu.count ? g_menu.count - 1 : g_menu.sel;
      return 1;
    }
    return keys & UI_CROSS ? menu_choose(g_menu.sel) : 0;

  case SHEET_CONFIRM:
    if(keys & UI_CIRCLE) {
      g_sheet = g_ask.what == M_UNSCHEDULE ? SHEET_SCHEDULED : SHEET_NONE;
      return 1;
    }
    if(keys & (UI_UP | UI_DOWN | UI_LEFT | UI_RIGHT)) {
      g_ask.focus = !g_ask.focus;
      return 1;
    }
    if(keys & UI_CROSS) {
      if(g_ask.focus) {
        g_sheet = g_ask.what == M_UNSCHEDULE ? SHEET_SCHEDULED : SHEET_NONE;
        return 1;
      }
      return delete_post();
    }
    return 0;

  case SHEET_SCHEDULED:
    if(keys & UI_CIRCLE) {
      g_sheet = SHEET_NONE;
      return 1;
    }
    if(keys & UI_SQUARE) {
      return open_scheduled();
    }
    if((keys & UI_CROSS) && g_sched.sel < g_sched.count) {
      return ask_unschedule(g_sched.sel);
    }
    if((keys & (UI_UP | UI_DOWN)) && g_sched.count) {
      g_sched.sel += keys & UI_UP ? -1 : 1;
      g_sched.sel = g_sched.sel < 0 ? 0 : g_sched.sel >= g_sched.count ? g_sched.count - 1 : g_sched.sel;
      return 1;
    }
    return 0;

  case SHEET_LINK:
    if((keys & UI_CIRCLE) || ((keys & UI_CROSS) && g_linking.st.state == UI_LINK_DONE)) {
      return close_link();
    }
    if(keys & (UI_LEFT | UI_RIGHT)) {
      return link_account_step(keys & UI_LEFT ? -1 : 1);
    }
    return 0;

  case SHEET_READER:
    if(keys & UI_CIRCLE) {
      return close_reader();
    }
    if(keys & (UI_UP | UI_DOWN | UI_L1 | UI_R1 | UI_TRIANGLE)) {
      g_reader.target += keys & UI_UP ? -3 * R_LINE_H : keys & UI_DOWN ? 3 * R_LINE_H :
                         keys & UI_L1 ? -(R_BOTTOM - R_TOP - 2 * R_LINE_H) :
                         keys & UI_R1 ? R_BOTTOM - R_TOP - 2 * R_LINE_H : -g_reader.target;
      reader_clamp();
      return 1;
    }
    return 0;
  case SHEET_SETTINGS:
    if(keys & UI_CIRCLE) {
      g_sheet = SHEET_NONE;
      return 1;
    }
    if(keys & (UI_UP | UI_DOWN)) {
      set_row(g_set_row + (keys & UI_UP ? -1 : 1));
      return 1;
    }
    if((keys & UI_CROSS) && g_set_row == S_SESSION) {
      return open_signin(SHEET_SETTINGS); // left and right still go through the accounts
    }
    if(g_set_row == S_UPDATE) {
      return keys & UI_CROSS ? update_press() : 0; // not with ← →
    }
    if(keys & (UI_LEFT | UI_RIGHT | UI_CROSS)) {
      return setting_change(g_set_row, keys & UI_LEFT ? -1 : 1);
    }
    return 0;
  }
  return 0;
}


/**
 * the pointer pressed one of the sheets' places, or of the viewer's video
 **/
static int
sheet_click(const zone_t *z) {
  const char *url = g_viewer.open ? video_of(&g_viewer.media[g_viewer.index]) : NULL;
  player_status_t st;

  switch(z->kind) {
  case Z_SHEET:
    if(g_sheet == SHEET_GALLERY) {
      g_sheet = SHEET_COMPOSE;
      return 1;
    }
    if(g_sheet == SHEET_COMPOSE) {
      return close_composer();
    }
    if(g_sheet == SHEET_LINK) {
      return close_link();
    }
    if(g_sheet == SHEET_READER) {
      return close_reader();
    }
    if(g_sheet == SHEET_LOGIN) {
      return 0; // beside the window: it stays, with what was typed in it
    }
    g_sheet = SHEET_NONE;
    return 1;
  case Z_R_CLOSE:
    return close_reader();
  case Z_LG_PAGE:
    if(g_login.st.state == UI_LOGIN_FAILED) { // its "Try again"
      login_push(UI_LOGIN_RETRY, 0, 0, 0);
      return 1;
    }
    return login_click();
  case Z_LG_CLOSE:
    return close_login();
  case Z_LG_PREV:
  case Z_LG_NEXT:
    return login_account_step(z->kind == Z_LG_PREV ? -1 : 1);
  case Z_L_PREV:
  case Z_L_NEXT:
    return link_account_step(z->kind == Z_L_PREV ? -1 : 1);
  case Z_L_CLOSE:
    return close_link();
  case Z_C_CLOSE:
    return close_composer();
  case Z_C_TEXT:
    g_compose.focus = C_FOCUS_TEXT;
    return ask_text();
  case Z_C_MEDIA:
    g_compose.focus = C_FOCUS_MEDIA + z->sub;
    return 1;
  case Z_C_REMOVE:
    remove_media(z->sub);
    return 1;
  case Z_C_ADD:
    g_compose.focus = C_FOCUS_ADD;
    return open_gallery();
  case Z_C_SEND:
    g_compose.focus = C_FOCUS_SEND;
    return publish();
  case Z_C_GAME:
    return compose_tag_game();
  case Z_C_SCHEDULE:
    return compose_schedule(1);
  case Z_C_POLL:
    g_compose.focus = C_FOCUS_POLL;
    return poll_toggle();
  case Z_C_DM:
    g_compose.focus = C_FOCUS_DM;
    return pick_chat();
  case Z_C_CHOICE:
    return ask_choice(z->arg);
  case Z_C_CHOICE_ADD:
    g_compose.focus = C_FOCUS_CHOICE_ADD;
    return add_choice();
  case Z_C_LENGTH:
    g_compose.focus = C_FOCUS_LENGTH;
    return poll_length(1);
  case Z_G_BACK:
    g_sheet = SHEET_COMPOSE;
    return 1;
  case Z_G_ADD:
    return gallery_done();
  case Z_G_TILE:
    g_gallery.sel = z->arg;
    return pick(z->arg);
  case Z_S_PAGE:
    return set_scroll(z->arg * (S_ROWS - S_SHOWN));
  case Z_S_ROW:
    g_set_row = z->arg;
    return z->arg == S_SESSION ? open_signin(SHEET_SETTINGS) : setting_change(z->arg, 1);
  case Z_S_CLOSE:
    g_sheet = SHEET_NONE;
    return 1;
  case Z_M_ROW:
    return menu_choose(z->arg);
  case Z_Q_YES:
    return delete_post();
  case Z_Q_NO:
    g_sheet = g_ask.what == M_UNSCHEDULE ? SHEET_SCHEDULED : SHEET_NONE;
    return 1;
  case Z_SCHED_ROW:
    g_sched.sel = z->arg;
    return ask_unschedule(z->arg);
  case Z_SCHED_CLOSE:
    g_sheet = SHEET_NONE;
    return 1;
  case Z_V_PLAY:
    return url ? video_keys(url, UI_CROSS) : 0;
  case Z_V_SOUND:
    return url ? video_keys(url, UI_SQUARE) : 0;
  case Z_V_BAR:
    player_status(&st);
    if(url && st.len_ms > 0 && z->r.w > 0) {
      player_seek((int)((g_ptr.x - z->r.x) / z->r.w * st.len_ms) - st.pos_ms);
      return 1;
    }
    return 0;
  }
  return 0;
}


/**
 * the right stick on the captures, -128 to 127: they scroll, and the
 * selection stays on the screen
 **/
static int
sheet_scroll(int ry) {
  float s = (ry > 0 ? ry - STICK_DEAD : ry + STICK_DEAD) / (128.0f - STICK_DEAD);
  int rows = (g_gallery.count + G_COLS - 1) / G_COLS, max = rows * G_PITCH - G_GAP - G_VIEW_H, row;

  if(g_gallery.state != GALLERY_READY || !g_gallery.count) {
    return 0;
  }
  s = s > 1 ? 1 : s < -1 ? -1 : s;
  g_gallery.target += (int)(s * (s < 0 ? -s : s) * SCROLL_MAX + (s < 0 ? -1.5f : 1.5f));
  g_gallery.target = g_gallery.target > max ? max : g_gallery.target;
  g_gallery.target = g_gallery.target < 0 ? 0 : g_gallery.target;
  row = g_gallery.sel / G_COLS;
  if(row * G_PITCH < g_gallery.target) {
    row = (g_gallery.target + G_PITCH - 1) / G_PITCH;
  } else if(row * G_PITCH + G_TILE_H > g_gallery.target + G_VIEW_H) {
    row = (g_gallery.target + G_VIEW_H - G_TILE_H) / G_PITCH;
  }
  row = row < 0 ? 0 : row >= rows ? rows - 1 : row;
  g_gallery.sel = row * G_COLS + g_gallery.sel % G_COLS;
  g_gallery.sel = g_gallery.sel >= g_gallery.count ? g_gallery.count - 1 : g_gallery.sel;
  return 1;
}


/**
 * how the composer's post went: it closes once it went, and keeps what it
 * has if not
 **/
static void
compose_done(int error) {
  if(error) {
    g_compose.sending = 0;
    return;
  }
  memset(&g_compose, 0, sizeof g_compose);
  g_sheet = g_sheet == SHEET_COMPOSE ? SHEET_NONE : g_sheet;
}


/**
 * a step of the sheets' scrolling; returns whether the next frame looks
 * different
 **/
static int
sheet_animate(void) {
  int diff = g_gallery.target - g_gallery.scroll, read = g_reader.target - g_reader.scroll;

  if(diff > -3 && diff < 3) {
    g_gallery.scroll = g_gallery.target;
  } else {
    g_gallery.scroll += diff * 2 / 5;
  }
  if(read > -3 && read < 3) {
    g_reader.scroll = g_reader.target;
  } else {
    g_reader.scroll += read * 2 / 5;
  }
  return (g_sheet == SHEET_GALLERY && (diff || g_gallery.state == GALLERY_LOADING)) ||
         (g_sheet == SHEET_READER && read) || (g_sheet && g_compose.sending);
}


int
ui_compose_get(char *text, size_t size, capture_t *media, int max) {
  int n = g_compose.count < max ? g_compose.count : max;

  snprintf(text, size, "%s", g_compose.text);
  memcpy(media, g_compose.media, (size_t)n * sizeof *media);
  return n;
}


void
ui_dm_text(const char *text) {
  const char *s = text;

  while(*s == ' ' || *s == '\n') {
    s++;
  }
  snprintf(g_dm.text, sizeof g_dm.text, "%s", *s ? text : "");
  if(!*s || g_act.kind != UI_ACT_DM_TEXT) {
    memset(&g_act, 0, sizeof g_act); // nothing to send
    return;
  }
  g_act.kind = UI_ACT_DM; // to the same conversation, next
  g_act.taken = 0;
}


int
ui_dm_get(char *text, size_t size) {
  snprintf(text, size, "%s", g_dm.text);
  return (int)strlen(text);
}


void
ui_grok_text(const char *text) {
  const char *s = text;
  size_t n;

  while(*s == ' ' || *s == '\n') {
    s++;
  }
  if(!*s || g_act.kind != UI_ACT_GROK_TEXT) {
    memset(&g_act, 0, sizeof g_act); // nothing to ask
    return;
  }
  // not in the middle of a character
  n = strlen(s);
  if(n >= sizeof g_grok.question) {
    for(n=sizeof g_grok.question - 1; n && ((unsigned char)s[n] & 0xC0) == 0x80; n--) {
    }
  }
  memcpy(g_grok.question, s, n);
  g_grok.question[n] = 0;
  snprintf(g_grok.game, sizeof g_grok.game, "%s", g_game.name);
  g_grok.answer[0] = 0;
  g_grok.error = 0;
  g_grok.asking = 1;
  g_under_stale = 1;
  g_act.kind = UI_ACT_GROK; // sent next
  g_act.taken = 0;
}


int
ui_grok_get(char *question, size_t qsize, char *game, size_t gsize, int *fresh) {
  snprintf(question, qsize, "%s", g_grok.question);
  snprintf(game, gsize, "%s", g_grok.game);
  *fresh = g_grok.fresh;
  g_grok.fresh = 0;
  return (int)strlen(question);
}


void
ui_grok_answer(const char *answer) {
  snprintf(g_grok.answer, sizeof g_grok.answer, "%s", answer);
  g_under_stale = 1;
}


void
ui_update(int state, const char *version, int percent, int error, int code) {
  int was = g_update.state;

  g_update.state = state;
  if(version && version[0]) {
    snprintf(g_update.version, sizeof g_update.version, "%s", version);
  }
  g_update.percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
  g_update.error = error;
  g_update.code = code;
  g_under_stale = 1;
  // found by the look at start: said once, over whatever is shown
  if(state == UI_UPDATE_FOUND && was == UI_UPDATE_CHECKING && g_sheet != SHEET_SETTINGS) {
    toast(0, L("A XPS5 %s chegou \xc2\xb7 instala-a nas Defini\xc3\xa7\xc3\xb5" "es",
               "XPS5 %s is out \xc2\xb7 install it from Settings"), g_update.version);
  }
}


int
ui_update_wanted(void) {
  int wanted = g_update.wanted;

  g_update.wanted = 0;
  return wanted;
}


int
ui_poll_get(char labels[][X_POLL_LABEL], int *minutes) {
  int n = 0;

  *minutes = g_poll_lengths[g_compose.length >= 0 && g_compose.length < POLL_LENGTHS ? g_compose.length :
                            POLL_LENGTH_DAY];
  if(g_compose.quote || g_compose.edit) {
    return 0;
  }
  for(int i=0; i < g_compose.poll; i++) {
    if(g_compose.choice[i][0]) {
      snprintf(labels[n++], X_POLL_LABEL, "%s", g_compose.choice[i]);
    }
  }
  return n >= 2 ? n : 0;
}


int
ui_poll_choice(char *text, size_t size) {
  int i = g_compose.editing >= 1 && g_compose.editing <= g_compose.poll ? g_compose.editing : 1;

  snprintf(text, size, "%s", g_compose.choice[i - 1]);
  return i;
}


void
ui_poll_text(const char *text) {
  int i = g_compose.editing - 1;
  char *s;

  if(i >= 0 && i < g_compose.poll) {
    // on one line, without spaces at its ends
    while(*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') {
      text++;
    }
    snprintf(g_compose.choice[i], sizeof g_compose.choice[i], "%s", text);
    for(s=g_compose.choice[i]; *s; s++) {
      *s = *s == '\n' || *s == '\r' || *s == '\t' ? ' ' : *s;
    }
    while(s > g_compose.choice[i] && s[-1] == ' ') {
      *--s = 0;
    }
    g_compose.focus = C_FOCUS_CHOICE + i;
  }
  g_compose.editing = 0;
  memset(&g_act, 0, sizeof g_act);
}


int
ui_vote_get(char *card, size_t csize, char *name, size_t nsize) {
  snprintf(card, csize, "%s", g_act.card);
  snprintf(name, nsize, "%s", g_act.card_name);
  return g_act.choice;
}


void
ui_compose_text(const char *text) {
  snprintf(g_compose.text, sizeof g_compose.text, "%s", text);
  g_compose.focus = compose_can_send() ? C_FOCUS_SEND : C_FOCUS_TEXT;
  memset(&g_act, 0, sizeof g_act);
}


void
ui_game(const char *id, const char *name) {
  tab_t *t = &g_tabs[UI_GAME];
  char query[X_QUERY_MAX + 1];

  const int renamed = strcmp(g_game.name, name);

  g_under_stale = 1;
  snprintf(g_game.id, sizeof g_game.id, "%s", id);
  snprintf(g_game.name, sizeof g_game.name, "%s", name);
  for(int i=0; renamed && i < SECTIONS; i++) {
    // the user's own profile says the game whole (profile_game): measured again
    if(g_tabs[i].has_user) {
      layout_profile(&g_tabs[i]);
      place(&g_tabs[i]);
    }
  }
  // Home's tab of it: what it has stays while it is shown after the game stops
  if(g_game.name[0] && strcmp(g_game.tab, g_game.name) && !game_query(g_game.name, query, sizeof query)) {
    snprintf(g_game.tab, sizeof g_game.tab, "%s", g_game.name);
    snprintf(t->query, sizeof t->query, "%s", query);
    t->latest = 1;
    reset_search(t);
  }
  if(strcmp(g_spoil_name, g_game.name)) {
    // another game: the posts about it covered, the others not
    snprintf(g_spoil_name, sizeof g_spoil_name, "%s", g_game.name);
    game_keys(g_spoil_name, &g_spoil);
    relayout(NULL);
  }
}


/**
 * whether the capture c is of the game with the title id: the PS5 keeps
 * them in a folder named after it
 **/
static int
of_game(const capture_t *c, const char *id) {
  const char *at = id[0] ? strstr(c->path, id) : NULL;

  return at && at > c->path && at[-1] == '/' && at[strlen(id)] == '/';
}


/**
 * the game's hashtag at the end of the composer's text, unless it is
 * there already
 **/
static int
compose_tag_game(void) {
  char tag[GAME_NAME + 2];
  size_t len = strlen(g_compose.text);

  if(!g_game.name[0] || g_compose.sending || game_hashtag(g_game.name, tag, sizeof tag)) {
    return 0;
  }
  if(strstr(g_compose.text, tag)) {
    toast(0, L("%s j\xc3\xa1 est\xc3\xa1 no post.", "%s is in the post already."), tag);
    return 1;
  }
  if(len + 1 + strlen(tag) >= sizeof g_compose.text) {
    toast(1, "%s", L("N\xc3\xa3o cabe mais texto.", "No more text fits."));
    return 1;
  }
  snprintf(g_compose.text + len, sizeof g_compose.text - len, "%s%s",
           len && g_compose.text[len - 1] != ' ' && g_compose.text[len - 1] != '\n' ? " " : "", tag);
  return 1;
}


/**
 * the game's hashtag at the end of the composer's text, quietly, unless it
 * is there or does not fit
 **/
static void
compose_tag_quietly(void) {
  char tag[GAME_NAME + 2];
  size_t len = strlen(g_compose.text);

  if(g_game.name[0] && !game_hashtag(g_game.name, tag, sizeof tag) && !strstr(g_compose.text, tag) &&
     len + 1 + strlen(tag) < sizeof g_compose.text) {
    snprintf(g_compose.text + len, sizeof g_compose.text - len, "%s%s",
             len && g_compose.text[len - 1] != ' ' && g_compose.text[len - 1] != '\n' ? " " : "", tag);
  }
}


void
ui_trophy(int grade, const char *name) {
  const char *said;
  char line[300];
  size_t len;

  if(g_act.kind || g_compose.sending || grade < 0 || grade > 3) {
    return;
  }
  if(g_compose.reply || g_compose.quote || g_compose.edit) {
    memset(&g_compose, 0, sizeof g_compose); // a post of the user's own
  }
  said = grade == 3 ? L("Trof\xc3\xa9u de platina desbloqueado", "Platinum trophy unlocked") :
         grade == 2 ? L("Trof\xc3\xa9u de ouro desbloqueado", "Gold trophy unlocked") :
         grade == 1 ? L("Trof\xc3\xa9u de prata desbloqueado", "Silver trophy unlocked") :
                      L("Trof\xc3\xa9u de bronze desbloqueado", "Bronze trophy unlocked");
  if(name && name[0]) {
    snprintf(line, sizeof line, "%s: \xe2\x80\x9c%s\xe2\x80\x9d", said, name);
  } else {
    snprintf(line, sizeof line, "%s", said);
  }
  len = strlen(g_compose.text);
  if(!strstr(g_compose.text, line) && len + 1 + strlen(line) < sizeof g_compose.text) {
    // before what is written, on a line of its own
    memmove(g_compose.text + strlen(line) + (len ? 1 : 0), g_compose.text, len + 1);
    memcpy(g_compose.text, line, strlen(line));
    if(len) {
      g_compose.text[strlen(line)] = '\n';
    }
  }
  compose_tag_quietly();
  if(g_sheet != SHEET_NONE && g_sheet != SHEET_COMPOSE) {
    toast(0, "%s", L("O teu trof\xc3\xa9u est\xc3\xa1 no post \xc2\xb7 \xe2\x98\xb0 abre-o",
                     "Your trophy is in your post \xc2\xb7 \xe2\x98\xb0 opens it"));
    return;
  }
  if(compose_open(NULL, CM_POST)) {
    toast(0, "%s", L("Pronto a publicar \xc2\xb7 R2 publica \xc2\xb7 \xe2\x9c\x95 escreve",
                     "Ready to post \xc2\xb7 R2 posts it \xc2\xb7 \xe2\x9c\x95 to write"));
  }
}


void
ui_shot(const capture_t *c) {
  int video = c->kind == CAPTURE_MP4 || c->kind == CAPTURE_WEBM, has_video = 0, at = -1;

  if(!c->path[0] || g_act.kind || g_compose.sending) {
    return;
  }
  if(g_compose.reply || g_compose.quote || g_compose.edit) {
    memset(&g_compose, 0, sizeof g_compose); // a post of the user's own
  }
  for(int i=0; i < g_compose.count; i++) {
    has_video |= g_compose.media[i].kind == CAPTURE_MP4 || g_compose.media[i].kind == CAPTURE_WEBM;
    at = !strcmp(g_compose.media[i].path, c->path) ? i : at;
  }
  if(at < 0) {
    // X takes four pictures, or one video
    if(video || has_video || g_compose.count >= UI_MEDIA_MAX) {
      g_compose.count = 0;
    }
    g_compose.media[g_compose.count++] = *c;
  }
  compose_tag_quietly();
  if(g_sheet != SHEET_NONE && g_sheet != SHEET_COMPOSE) {
    toast(0, "%s", video ? L("O teu v\xc3\xad" "deo do jogo est\xc3\xa1 no post \xc2\xb7 \xe2\x98\xb0 abre-o",
                             "Your clip of the game is in your post \xc2\xb7 \xe2\x98\xb0 opens it")
                         : L("A tua captura do jogo est\xc3\xa1 no post \xc2\xb7 \xe2\x98\xb0 abre-o",
                             "Your screenshot of the game is in your post \xc2\xb7 \xe2\x98\xb0 opens it"));
    return;
  }
  if(compose_open(NULL, CM_POST)) {
    toast(0, "%s", L("Pronto a publicar \xc2\xb7 R2 publica \xc2\xb7 \xe2\x9c\x95 escreve",
                     "Ready to post \xc2\xb7 R2 posts it \xc2\xb7 \xe2\x9c\x95 to write"));
  }
}


void
ui_captures(const capture_t *list, int count, int error, int code) {
  mem_free(g_gallery.list);
  g_gallery.list = NULL;
  g_gallery.count = 0;
  g_gallery.picks = 0;
  g_gallery.of_game = 0;
  g_gallery.error = error;
  g_gallery.code = code;
  g_gallery.state = GALLERY_FAILED;
  if(count >= 0 && (!count || (g_gallery.list=mem_alloc((size_t)count * sizeof *list)))) {
    // the game's that runs first, each part newest first
    for(int pass=0, n=0; pass<2; pass++) {
      for(int i=0; i < count; i++) {
        if(of_game(&list[i], g_game.id) == !pass) {
          g_gallery.list[n++] = list[i];
          g_gallery.of_game += !pass;
        }
      }
    }
    list = g_gallery.list;
    g_gallery.count = count;
    g_gallery.state = GALLERY_READY;
    // what the composer carries comes picked
    for(int k=0; k < g_compose.count; k++) {
      for(int i=0; i < count; i++) {
        if(!strcmp(list[i].path, g_compose.media[k].path) && g_gallery.picks < UI_MEDIA_MAX) {
          g_gallery.picked[g_gallery.picks++] = i;
          break;
        }
      }
    }
  } else if(count >= 0) {
    g_gallery.error = ERR_MEMORY;
  }
  g_gallery.sel = g_gallery.sel < g_gallery.count ? g_gallery.sel : 0;
  g_gallery.scroll = g_gallery.target = 0;
  gallery_show_sel();
  g_gallery.scroll = g_gallery.target;
  memset(&g_act, 0, sizeof g_act);
}


void
ui_progress(long long done, long long total) {
  g_compose.done = done;
  g_compose.total = total;
}


/**
 * the shadow a pane drops: wide and faint below it, and close all round;
 * drawn into what is kept under the sheet, so once while it stays
 **/
static void
pane_shadow(int x, int y, int w, int h, float r, float spread) {
  if(g_under_used && g_under_pane.x == x && g_under_pane.y == y && g_under_pane.w == w &&
     g_under_pane.h == h) {
    return; // the frame began with it
  }
  draw_shadow(&g_dst, x - 10, y + spread / 4, w + 20, h, r + 10, spread,
              NIGHT ? RGBA(0, 0, 0, 160) : RGBA(0, 0, 0, 130));
  draw_shadow(&g_dst, x, y + 8, w, h, r, 26, RGBA(0, 0, 0, 105));
  if(g_under_fresh) {
    screen_copy(g_under, g_dst.pixels, (size_t)pxs(UI_WIDTH) * pxs(UI_HEIGHT) * sizeof *g_under);
    g_under_fresh = 0;
    g_under_pane.x = x;
    g_under_pane.y = y;
    g_under_pane.w = w;
    g_under_pane.h = h;
  } else if(g_under_used) {
    g_under_stale = 1; // another pane: the next frame keeps it with its shadow
  }
}


/**
 * the pane of glass of a sheet over the dimmed screen, and the place
 * around it that closes it
 **/
static void
draw_pane(int x, int y, int w, int h) {
  zone(Z_SHEET, 0, 0, 0, 0, UI_WIDTH, UI_HEIGHT);
  pane_shadow(x, y, w, h, SHEET_R, 120);
  draw_box(&g_dst, x, y, w, h, SHEET_R, 0, PANE);
  draw_rim(&g_dst, x, y, w, h, SHEET_R, 2, RGBA(255, 255, 255, 96), 0.3f);
  zone(Z_PANE, 0, 0, x, y, w, h);
}


/**
 * a round button with an icon, lit under the pointer; returns whether it
 * is under it
 **/
static int
round_button(int kind, int cx, int cy, int icon) {
  int over = zone(kind, 0, 0, cx - 32, cy - 32, 64, 64);

  draw_circle(&g_dst, cx, cy, 30, CHIP);
  if(over) {
    lit_circle(cx, cy, 30, 1);
  }
  draw_icon(&g_dst, icon, cx - 16, cy - 16, 32, FG);
  return over;
}


/**
 * a pill of the accent with a label, ending at right; dim when it cannot
 * be pressed, ringed when the buttons are on it; returns its x
 **/
static int
accent_pill(int kind, const char *label, int right, int y, int h, int enabled, int focused, int spin,
            unsigned frame) {
  text_font_t *f = font(F_PILL);
  int w = width_of(f, label) + 64 + (spin ? 40 : 0), x = right - w, over;

  over = zone(kind, 0, 0, x, y, w, h);
  draw_box(&g_dst, x, y, w, h, h / 2.0f, 0, !enabled ? RGBA(242, 122, 26, 80) :
           over ? RGBA(255, 152, 72, 255) : ACCENT);
  if(focused) {
    draw_box(&g_dst, x - 6, y - 6, w + 12, h + 12, h / 2.0f + 6, 3, WHITE);
  } else if(over && enabled) {
    lit_box(x, y, w, h, h / 2.0f, 0);
  }
  if(spin) {
    spinner(x + 40, y + h / 2.0f, 12, 4, frame, WHITE);
  }
  text(f, x + 32 + (spin ? 40 : 0), baseline(f, y, h), label, enabled ? WHITE : RGBA(255, 255, 255, 140));
  return x;
}


/**
 * a line of the buttons a sheet takes, in the middle at y
 **/
static void
draw_hints(int cx, int y, const char *line) {
  text_font_t *f = font(F_SMALL);

  text(f, cx - width_of(f, line) / 2, baseline(f, y, 40), line, DIM);
}


/**
 * a capture's tile: its picture, or for a video a dark tile with its mark
 **/
static void
draw_capture(const capture_t *c, int x, int y, int w, int h, float r) {
  char buf[IMAGES_URL_MAX];
  const uint32_t *pixels;

  if(is_video(c)) {
    draw_box(&g_dst, x, y, w, h, r, 0, TILE);
    draw_box(&g_dst, x, y, w, h, r, 1.5f, EDGE);
    draw_icon(&g_dst, ICON_VIDEO, x + w / 2 - 30, y + h / 2 - 38, 60, c->kind == CAPTURE_WEBM ? DIM : FG);
    return;
  }
  if(capture_url(c, buf, sizeof buf) && (pixels=picture(buf, w, h))) {
    draw_picture_round(&g_dst, x, y, w, h, r, pixels);
  } else {
    draw_box(&g_dst, x, y, w, h, r, 0, PLACEHOLDER);
  }
}


/**
 * a label on a dark pill, at x and the bottom of a tile at y + h
 **/
static void
tile_label(const char *s, int x, int y, int h) {
  text_font_t *f = font(F_SMALL);
  int w = width_of(f, s) + 24;

  draw_box(&g_dst, x + 10, y + h - 46, w, 36, 18, 0, RGBA(0, 0, 0, 160));
  text(f, x + 22, baseline(f, y + h - 46, 36), s, WHITE);
}


/**
 * the composer's media, w x h at x, y: one alone, two side by side, three
 * with the first tall, four in a grid
 **/
static void
draw_compose_media(int x, int y, int w, int h) {
  const int gap = 12;
  int half = (w - gap) / 2, top = (h - gap) / 2, n = g_compose.count;
  rect_t r[UI_MEDIA_MAX];
  char line[64], size[32];
  int over;

  for(int i=0; i<n; i++) {
    if(n == 1) {
      r[i] = (rect_t){0, 0, h * 16 / 9 < w ? h * 16 / 9 : w, h};
    } else if(n == 2 || (n == 3 && i == 0)) {
      r[i] = (rect_t){i ? half + gap : 0, 0, half, h};
    } else if(n == 3) {
      r[i] = (rect_t){half + gap, i == 1 ? 0 : top + gap, half, top};
    } else {
      r[i] = (rect_t){i % 2 ? half + gap : 0, i < 2 ? 0 : top + gap, half, top};
    }
  }
  for(int i=0; i<n; i++) {
    const capture_t *c = &g_compose.media[i];
    int tx = x + r[i].x, ty = y + r[i].y, tw = r[i].w, th = r[i].h;
    over = zone(Z_C_MEDIA, 0, i, tx, ty, tw, th);
    draw_capture(c, tx, ty, tw, th, 20);
    if(is_video(c)) {
      size_text(c->size, size, sizeof size);
      snprintf(line, sizeof line, "MP4 \xc2\xb7 %s", size);
      tile_label(line, tx, ty, th);
    }
    if(g_compose.focus == C_FOCUS_MEDIA + i || over) {
      draw_box(&g_dst, tx - 5, ty - 5, tw + 10, th + 10, 25, 3, g_compose.focus == C_FOCUS_MEDIA + i ?
               WHITE : HOVER_RIM);
    }
    // ✕ in the corner takes it out
    over = zone(Z_C_REMOVE, 0, i, tx + tw - 58, ty + 6, 52, 52);
    draw_circle(&g_dst, tx + tw - 32, ty + 32, 22, over ? RGBA(0, 0, 0, 220) : RGBA(0, 0, 0, 160));
    if(over) {
      draw_arc(&g_dst, tx + tw - 32, ty + 32, 21, 2, 0, 6.2832f, HOVER_RIM);
    }
    draw_icon(&g_dst, ICON_CROSS, tx + tw - 44, ty + 20, 24, WHITE);
  }
}


/**
 * X's ring of how much of the 280 characters is used, and how many are
 * left once there are 20 or fewer
 **/
static void
draw_counter(float cx, float cy, int used) {
  text_font_t *f = font(F_KEY);
  char count[16];

  // a long post of X Premium: how long it is
  if(used > POST_MAX && premium()) {
    snprintf(count, sizeof count, "%d", used);
    text(f, (int)cx + 16 - width_of(f, count), baseline(f, (int)cy - 20, 40), count, used > LONG_MAX ? RED : ACCENT);
    return;
  }
  {
  float part = used >= POST_MAX ? 1 : (float)used / POST_MAX;
  uint32_t color = used > POST_MAX ? RED : used >= POST_MAX - 20 ? GOLD : ACCENT;
  int left = POST_MAX - used, r = left <= 20 ? 20 : 16;
  char n[16];

  draw_arc(&g_dst, cx, cy, r, 3, 0, 6.2832f, RGBA(255, 255, 255, 44));
  if(part > 0) {
    draw_arc(&g_dst, cx, cy, r, 3, 0, 6.2832f * part, color); // from the top, clockwise
  }
  if(left <= 20) {
    snprintf(n, sizeof n, "%d", left);
    text(f, (int)cx - width_of(f, n) / 2, baseline(f, (int)cy - 20, 40), n, left < 0 ? RED : GREY);
  }
  }
}


/**
 * the post a quote carries, in a box under what the user wrote
 **/
static int
draw_compose_quote(const x_post_t *to, int x, int y, int w, long long now) {
  text_font_t *q = font(F_QUOTE);
  uint16_t start[2], len[2];
  int lines, more = 0, h;

  lines = break_lines(q, post_text(to), w - 2 * QUOTE_PAD, 2, start, len, &more);
  h = QUOTE_PAD + QUOTE_NAME_H + lines * QUOTE_LINE_H + QUOTE_PAD - 4;
  draw_box(&g_dst, x, y, w, h, RADIUS, 1.5f, RGBA(255, 255, 255, 60));
  draw_avatar(post_avatar(to), post_user(to), post_name(to), x + QUOTE_PAD, y + QUOTE_PAD + 3, QUOTE_AVATAR);
  draw_name_row(font(F_QUOTE_BOLD), font(F_SMALL), x + QUOTE_PAD + QUOTE_AVATAR + 12, y + QUOTE_PAD, QUOTE_NAME_H,
                w - 2 * QUOTE_PAD - QUOTE_AVATAR - 12, post_name(to),
                to->kind == X_KIND_NOTICE ? to->quote_badge : to->badge, post_user(to), to->time, now);
  for(int i=0; i < lines; i++) {
    int ly = y + QUOTE_PAD + QUOTE_NAME_H + i * QUOTE_LINE_H;
    int end = text_draw(q, &g_dst, x + QUOTE_PAD, baseline(q, ly, QUOTE_LINE_H), post_text(to) + start[i], len[i],
                        FG);
    if(more && i == lines - 1) {
      text(q, end, baseline(q, ly, QUOTE_LINE_H), "\xe2\x80\xa6", FG);
    }
  }
  return h;
}


/**
 * the game that runs, in a pill at x, y: what it is, and its hashtag, which
 * the pointer or L2 adds to the text (ticked once it is there)
 **/
static void
draw_compose_game(int x, int y) {
  text_font_t *f = font(F_LEGEND), *bold = font(F_SMALL_BOLD);
  char line[GAME_NAME + 32], tag[GAME_NAME + 2];
  int has_tag = !game_hashtag(g_game.name, tag, sizeof tag), in = has_tag && strstr(g_compose.text, tag);
  int w, over, end, tw = has_tag ? width_of(bold, tag) : 0, room;

  snprintf(line, sizeof line, L("A jogar %s", "Playing %s"), g_game.name);
  room = C_W - 2 * C_PAD - 60 - (has_tag ? tw + 120 : 0);
  w = 60 + (width_of(f, line) < room ? width_of(f, line) : room) + (has_tag ? tw + 120 : 24);
  over = has_tag && !in && zone(Z_C_GAME, 0, 0, x - 8, y, w, C_GAME_H);
  draw_box(&g_dst, x - 8, y, w, C_GAME_H, C_GAME_H / 2.0f, 0, CHIP);
  if(over) {
    lit_box(x - 8, y, w, C_GAME_H, C_GAME_H / 2.0f, 1);
  }
  draw_icon(&g_dst, ICON_GAME, x + 8, y + (C_GAME_H - 34) / 2, 34, ACCENT);
  end = text_fitted(f, x + 52, baseline(f, y, C_GAME_H), line, room, FG);
  if(!has_tag) {
    return;
  }
  end += 16;
  if(in) {
    draw_icon(&g_dst, ICON_CHECK, end, y + (C_GAME_H - 26) / 2, 26, GREEN);
  } else {
    end += draw_key("L2", end, y + (C_GAME_H - 38) / 2) - 26;
  }
  text(bold, end + 32, baseline(bold, y, C_GAME_H), tag, in ? GREY : BLUE);
}


#define C_CHOICE_H   50
#define C_CHOICE_GAP 10

/**
 * how high the composer's poll is: its choices, adding one, how long
 **/
static int
compose_poll_h(void) {
  int rows = g_compose.poll + (g_compose.poll < X_POLL_MAX) + 1;

  return rows * (C_CHOICE_H + C_CHOICE_GAP) - C_CHOICE_GAP;
}


/**
 * a row of the composer's poll: its box, filled under the pointer, ringed
 * while the buttons are on it
 **/
static void
choice_box(int x, int y, int w, int over, int focus) {
  if(focus) {
    draw_box(&g_dst, x, y, w, C_CHOICE_H, 16, 0, CHIP);
  }
  draw_box(&g_dst, x, y, w, C_CHOICE_H, 16, focus ? 2.5f : 1.5f, focus ? RGBA(255, 255, 255, 170) : EDGE);
  if(over && !focus) {
    lit_box(x, y, w, C_CHOICE_H, 16, 1);
  }
}


/**
 * the composer's poll: a box for each choice, which ✕ writes and □ takes
 * away; adding one more; how long it stays open, which ✕ ← → change
 **/
static void
draw_compose_poll(int x, int y, int w) {
  text_font_t *f = font(F_LEGEND), *small = font(F_SMALL);
  char line[160], len[48];
  int over, m;

  for(int i=0; i < g_compose.poll; i++, y += C_CHOICE_H + C_CHOICE_GAP) {
    over = zone(Z_C_CHOICE, i, 0, x - 16, y, w + 16, C_CHOICE_H);
    choice_box(x - 16, y, w + 16, over, g_compose.focus == C_FOCUS_CHOICE + i);
    if(g_compose.choice[i][0]) {
      snprintf(len, sizeof len, "%d/25", post_length(g_compose.choice[i]));
      text_fitted(f, x + 4, baseline(f, y, C_CHOICE_H), g_compose.choice[i], w - 110, FG);
      text(small, x + w - 18 - width_of(small, len), baseline(small, y, C_CHOICE_H), len, GREY);
    } else {
      snprintf(line, sizeof line, i < 2 ? L("Op\xc3\xa7\xc3\xa3o %d", "Choice %d") :
               L("Op\xc3\xa7\xc3\xa3o %d (opcional)", "Choice %d (optional)"), i + 1);
      text_fitted(f, x + 4, baseline(f, y, C_CHOICE_H), line, w - 40, DIM);
    }
  }
  if(g_compose.poll < X_POLL_MAX) {
    over = zone(Z_C_CHOICE_ADD, 0, 0, x - 16, y, w + 16, C_CHOICE_H);
    choice_box(x - 16, y, w + 16, over, g_compose.focus == C_FOCUS_CHOICE_ADD);
    snprintf(line, sizeof line, "+  %s", L("Adicionar uma op\xc3\xa7\xc3\xa3o", "Add a choice"));
    text_fitted(f, x + 4, baseline(f, y, C_CHOICE_H), line, w - 40, ACCENT);
    y += C_CHOICE_H + C_CHOICE_GAP;
  }
  // how long it stays open
  over = zone(Z_C_LENGTH, 0, 0, x - 16, y, w + 16, C_CHOICE_H);
  choice_box(x - 16, y, w + 16, over, g_compose.focus == C_FOCUS_LENGTH);
  draw_icon(&g_dst, ICON_CLOCK, x + 2, y + (C_CHOICE_H - 30) / 2, 30, GREY);
  text_fitted(f, x + 46, baseline(f, y, C_CHOICE_H), L("Dura\xc3\xa7\xc3\xa3o da sondagem", "Poll length"), w - 300, GREY);
  m = g_poll_lengths[g_compose.length >= 0 && g_compose.length < POLL_LENGTHS ? g_compose.length : POLL_LENGTH_DAY];
  if(m < 24 * 60) {
    snprintf(len, sizeof len, m == 60 ? L("1 hora", "1 hour") : L("%d horas", "%d hours"), m / 60);
  } else {
    snprintf(len, sizeof len, m == 24 * 60 ? L("1 dia", "1 day") : L("%d dias", "%d days"), m / (24 * 60));
  }
  snprintf(line, sizeof line, "\xe2\x86\x90  %s  \xe2\x86\x92", len);
  text(font(F_SMALL_BOLD), x + w - 18 - width_of(font(F_SMALL_BOLD), line), baseline(font(F_SMALL_BOLD), y, C_CHOICE_H),
       line, FG);
}


static void
draw_compose(long long now, unsigned frame) {
  text_font_t *title = font(F_BRAND), *body = font(F_COMPOSE), *small = font(F_SMALL);
  const x_user_t *me = g_tabs[UI_ME].has_user ? &g_tabs[UI_ME].user : NULL;
  const x_post_t *to = &g_compose.to;
  uint16_t start[8], len[8];
  int x = C_X + C_PAD, y = C_Y + 30, tx, tw, th, lines, more = 0, over, used, max_lines, px, enabled, qx, bar_end;
  char line[200];

  draw_pane(C_X, C_Y, C_W, C_H);
  round_button(Z_C_CLOSE, x + 30, y + 32, ICON_CROSS);
  text(title, x + 84, baseline(title, y, 64), g_compose.reply ? L("Responder", "Reply") :
       g_compose.quote ? L("Citar o post", "Quote post") : g_compose.edit ? L("Editar o post", "Edit post") :
       L("Novo post", "New post"), FG);
  y += 98;

  // a reply: the post it answers, a thread down to the user's picture
  if(g_compose.reply) {
    text_font_t *q = font(F_QUOTE);
    draw_avatar(post_avatar(to), post_user(to), post_name(to), x + 6, y, 56);
    draw_name_row(font(F_QUOTE_BOLD), small, x + 82, y, 44, C_W - 2 * C_PAD - 82, post_name(to),
                  to->kind == X_KIND_NOTICE ? to->quote_badge : to->badge, post_user(to), to->time, now);
    lines = break_lines(q, post_text(to), C_W - 2 * C_PAD - 82, 2, start, len, &more);
    for(int i=0; i < lines; i++) {
      int end = text_draw(q, &g_dst, x + 82, baseline(q, y + 46 + i * 38, 38), post_text(to) + start[i], len[i],
                          GREY);
      if(more && i == lines - 1) {
        text(q, end, baseline(q, y + 46 + i * 38, 38), "\xe2\x80\xa6", GREY);
      }
    }
    th = 46 + lines * 38 + 10;
    draw_rect(&g_dst, x + 33, y + 64, 3, th - 64 + 58, THREAD);
    y += th;
    snprintf(line, sizeof line, "%s ", L("A responder a", "Replying to"));
    tx = text(small, x + 82, baseline(small, y, 40), line, GREY);
    snprintf(line, sizeof line, "@%s", post_user(to));
    text(small, tx, baseline(small, y, 40), line, BLUE);
    y += 52;
  }

  // the user's picture and what they wrote, which ✕ changes
  draw_avatar(me ? me->avatar : "", me ? me->user : "", me ? me->name : "", x, y, C_AVATAR);
  tx = x + C_AVATAR + 24;
  tw = C_X + C_W - C_PAD - tx;
  max_lines = (C_BAR_Y - 30 - y - (g_compose.count ? C_MEDIA_H + 26 : 0) - (g_compose.quote ? 190 : 0) -
               (g_game.name[0] ? C_GAME_H + 16 : 0) - (g_compose.poll ? compose_poll_h() + 26 : 0)) / C_LINE_H;
  max_lines = max_lines < 1 ? 1 : max_lines > 8 ? 8 : max_lines;
  lines = g_compose.text[0] ? break_lines(body, g_compose.text, tw - 24, max_lines, start, len, &more) : 1;
  th = (lines < 2 && !g_compose.count ? 2 : lines) * C_LINE_H + 16;
  over = zone(Z_C_TEXT, 0, 0, tx - 16, y - 8, tw + 16, th);
  if(g_compose.focus == C_FOCUS_TEXT) {
    draw_box(&g_dst, tx - 16, y - 8, tw + 16, th, 22, 0, SELECTED);
    draw_box(&g_dst, tx - 16, y - 8, tw + 16, th, 22, 2.5f, RGBA(255, 255, 255, 150));
  } else if(over) {
    lit_box(tx - 16, y - 8, tw + 16, th, 22, 1);
  }
  if(!g_compose.text[0]) {
    text_fitted(body, tx, baseline(body, y, C_LINE_H), g_compose.reply ?
                L("Publica a tua resposta", "Post your reply") : g_compose.quote ?
                L("Adiciona um coment\xc3\xa1rio", "Add a comment") :
                L("O que est\xc3\xa1 a acontecer?!", "What is happening?!"), tw - 24, DIM);
  } else {
    for(int i=0; i < lines; i++) {
      int end = text_draw(body, &g_dst, tx, baseline(body, y + i * C_LINE_H, C_LINE_H),
                          g_compose.text + start[i], len[i], FG);
      if(more && i == lines - 1) {
        text(body, end, baseline(body, y + i * C_LINE_H, C_LINE_H), "\xe2\x80\xa6", FG);
      }
    }
  }
  y += th + 18;
  if(g_compose.count) {
    draw_compose_media(tx, y, tw, C_MEDIA_H);
    y += C_MEDIA_H + 26;
  }
  if(g_compose.poll) {
    draw_compose_poll(tx, y, tw);
    y += compose_poll_h() + 26;
  }
  if(g_compose.quote) {
    draw_compose_quote(to, tx, y, tw - 8, now);
  }

  // the game that runs, whose hashtag L2 adds
  if(g_game.name[0]) {
    draw_compose_game(x, C_BAR_Y - 20 - 16 - C_GAME_H);
  }

  // the line, then the bar: adding photos and videos, what is left, posting
  draw_rect(&g_dst, C_X + C_PAD, C_BAR_Y - 20, C_W - 2 * C_PAD, 2, RGBA(255, 255, 255, 26));
  snprintf(line, sizeof line, "%s", L("Fotos e v\xc3\xad" "deos da PS5", "PS5 photos and videos"));
  if(g_compose.count) {
    snprintf(line + strlen(line), sizeof line - strlen(line), "  \xc2\xb7  %d/%d", g_compose.count,
             is_video(&g_compose.media[0]) ? 1 : UI_MEDIA_MAX);
  }
  px = width_of(font(F_LEGEND), line) + 96;
  over = zone(Z_C_ADD, 0, 0, x - 8, C_BAR_Y, px, C_BAR_H);
  if(g_compose.focus == C_FOCUS_ADD) {
    draw_box(&g_dst, x - 8, C_BAR_Y, px, C_BAR_H, C_BAR_H / 2.0f, 0, CHIP);
    draw_box(&g_dst, x - 14, C_BAR_Y - 6, px + 12, C_BAR_H + 12, C_BAR_H / 2.0f + 6, 3, WHITE);
  } else if(over) {
    lit_box(x - 8, C_BAR_Y, px, C_BAR_H, C_BAR_H / 2.0f, 1);
  }
  draw_icon(&g_dst, ICON_PHOTO, x + 14, C_BAR_Y + (C_BAR_H - 38) / 2, 38, ACCENT);
  text(font(F_LEGEND), x + 66, baseline(font(F_LEGEND), C_BAR_Y, C_BAR_H), line, FG);
  qx = x - 8 + px + 14;
  // a poll, for a post or a reply
  if(!g_compose.quote && !g_compose.edit) {
    over = zone(Z_C_POLL, 0, 0, qx, C_BAR_Y, C_BAR_H, C_BAR_H);
    if(g_compose.poll || g_compose.focus == C_FOCUS_POLL) {
      draw_box(&g_dst, qx, C_BAR_Y, C_BAR_H, C_BAR_H, C_BAR_H / 2.0f, 0, g_compose.poll ? CHIP_ON : CHIP);
    }
    if(g_compose.focus == C_FOCUS_POLL) {
      draw_box(&g_dst, qx - 6, C_BAR_Y - 6, C_BAR_H + 12, C_BAR_H + 12, C_BAR_H / 2.0f + 6, 3, WHITE);
    } else if(over) {
      lit_box(qx, C_BAR_Y, C_BAR_H, C_BAR_H, C_BAR_H / 2.0f, 1);
    }
    draw_icon(&g_dst, ICON_POLL, qx + (C_BAR_H - 38) / 2, C_BAR_Y + (C_BAR_H - 38) / 2, 38, ACCENT);
    qx += C_BAR_H + 14;
  }
  // the capture in a message, to a friend
  if(share_dm_ok()) {
    over = zone(Z_C_DM, 0, 0, qx, C_BAR_Y, C_BAR_H, C_BAR_H);
    if(g_compose.focus == C_FOCUS_DM) {
      draw_box(&g_dst, qx, C_BAR_Y, C_BAR_H, C_BAR_H, C_BAR_H / 2.0f, 0, CHIP);
      draw_box(&g_dst, qx - 6, C_BAR_Y - 6, C_BAR_H + 12, C_BAR_H + 12, C_BAR_H / 2.0f + 6, 3, WHITE);
    } else if(over) {
      lit_box(qx, C_BAR_Y, C_BAR_H, C_BAR_H, C_BAR_H / 2.0f, 1);
    }
    draw_icon(&g_dst, ICON_SEND, qx + (C_BAR_H - 36) / 2, C_BAR_Y + (C_BAR_H - 36) / 2, 36, ACCENT);
    qx += C_BAR_H + 14;
  }
  // a post of its own: when it goes out, which L1 and R1 change
  bar_end = qx;
  if(!g_compose.reply && !g_compose.quote && !g_compose.edit) {
    int sx = qx, sw;
    schedule_text(g_compose.schedule, line, sizeof line);
    sw = width_of(font(F_LEGEND), line) + 92;
    bar_end = sx + sw;
    over = zone(Z_C_SCHEDULE, 0, 0, sx, C_BAR_Y, sw, C_BAR_H);
    if(g_compose.schedule) {
      draw_box(&g_dst, sx, C_BAR_Y, sw, C_BAR_H, C_BAR_H / 2.0f, 0, CHIP);
    }
    if(over) {
      lit_box(sx, C_BAR_Y, sw, C_BAR_H, C_BAR_H / 2.0f, 1);
    }
    draw_icon(&g_dst, ICON_CLOCK, sx + 20, C_BAR_Y + (C_BAR_H - 34) / 2, 34, ACCENT);
    text(font(F_LEGEND), sx + 66, baseline(font(F_LEGEND), C_BAR_Y, C_BAR_H), line,
         g_compose.schedule ? FG : GREY);
  }

  enabled = compose_can_send() || g_compose.sending;
  if(g_compose.sending) {
    if(g_compose.total > 0) {
      snprintf(line, sizeof line, L("A enviar\xe2\x80\xa6 %lld%%", "Sending\xe2\x80\xa6 %lld%%"),
               g_compose.done * 100 / g_compose.total);
    } else {
      snprintf(line, sizeof line, "%s", L("A enviar\xe2\x80\xa6", "Sending\xe2\x80\xa6"));
    }
  } else {
    snprintf(line, sizeof line, "%s", g_compose.reply ? L("Responder", "Reply") :
             g_compose.edit ? L("Guardar", "Save") : compose_scheduled() ? L("Agendar", "Schedule") :
             L("Publicar", "Post"));
  }
  px = accent_pill(Z_C_SEND, line, C_X + C_W - C_PAD, C_BAR_Y + 6, C_BAR_H - 12, enabled,
                   g_compose.focus == C_FOCUS_SEND && !g_compose.sending, g_compose.sending, frame);
  // what is left, between the bar and the button; not while it goes out,
  // when "Sending… 58%" takes its room
  if(!g_compose.sending && px - 92 >= bar_end && (used=post_length(g_compose.text))) {
    draw_rect(&g_dst, px - 28, C_BAR_Y + 18, 2, C_BAR_H - 36, RGBA(255, 255, 255, 40));
    draw_counter(px - 64, C_BAR_Y + C_BAR_H / 2.0f, used);
  }

  if(g_compose.sending && g_compose.total > 0) {
    draw_box(&g_dst, C_X + C_PAD, C_Y + C_H - 30, C_W - 2 * C_PAD, 8, 4, 0, RGBA(255, 255, 255, 30));
    draw_box(&g_dst, C_X + C_PAD, C_Y + C_H - 30, (int)((C_W - 2 * C_PAD) * g_compose.done / g_compose.total),
             8, 4, 0, ACCENT);
  } else if(g_compose.focus == C_FOCUS_DM && share_dm_ok()) {
    draw_hints(C_X + C_W / 2, C_Y + C_H - 56,
               L("\xe2\x9c\x95 Enviar numa mensagem direta  \xc2\xb7  R2 Publicar  \xc2\xb7  \xe2\x97\x8b Fechar",
                 "\xe2\x9c\x95 Send in a direct message  \xc2\xb7  R2 Post  \xc2\xb7  \xe2\x97\x8b Close"));
  } else {
    draw_hints(C_X + C_W / 2, C_Y + C_H - 56, g_compose.reply || g_compose.quote || g_compose.edit ?
               L("\xe2\x9c\x95 Escrever  \xc2\xb7  \xe2\x96\xb3 Fotos e v\xc3\xad" "deos  \xc2\xb7  \xe2\x96\xa1 Tirar  \xc2\xb7  R2 Publicar  \xc2\xb7  \xe2\x97\x8b Fechar",
                 "\xe2\x9c\x95 Write  \xc2\xb7  \xe2\x96\xb3 Photos and videos  \xc2\xb7  \xe2\x96\xa1 Remove  \xc2\xb7  R2 Post  \xc2\xb7  \xe2\x97\x8b Close") :
               L("\xe2\x9c\x95 Escrever  \xc2\xb7  \xe2\x96\xb3 Fotos e v\xc3\xad" "deos  \xc2\xb7  \xe2\x96\xa1 Tirar  \xc2\xb7  L1 R1 Agendar  \xc2\xb7  R2 Publicar  \xc2\xb7  \xe2\x97\x8b Fechar",
                 "\xe2\x9c\x95 Write  \xc2\xb7  \xe2\x96\xb3 Photos and videos  \xc2\xb7  \xe2\x96\xa1 Remove  \xc2\xb7  L1 R1 Schedule  \xc2\xb7  R2 Post  \xc2\xb7  \xe2\x97\x8b Close"));
  }
}


static void
draw_gallery(long long now, unsigned frame) {
  text_font_t *title = font(F_BRAND), *small = font(F_SMALL), *f = font(F_LEGEND);
  text_surface_t screen = g_dst;
  int x = G_X + G_MARGIN, y = G_Y + 30, rows, top, over, at;
  char line[160], when[48];

  draw_pane(G_X, G_Y, G_W, G_H);
  round_button(Z_G_BACK, x + 30, y + 32, ICON_BACK);
  text(title, x + 84, y + 31, L("Capturas da PS5", "PS5 captures"), FG);
  if(g_gallery.state == GALLERY_READY && g_gallery.of_game && g_game.name[0]) {
    snprintf(line, sizeof line, L("At\xc3\xa9 4 fotos ou 1 v\xc3\xad" "deo \xc2\xb7 primeiro as %d de %s",
                                  "Up to 4 photos or 1 video \xc2\xb7 first the %d of %s"), g_gallery.of_game, g_game.name);
  } else {
    snprintf(line, sizeof line, "%s", L("Escolhe at\xc3\xa9 4 fotos ou 1 v\xc3\xad" "deo", "Pick up to 4 photos or 1 video"));
  }
  text_fitted(small, x + 84, y + 63, line, G_W - 2 * G_MARGIN - 84 - 260, GREY);
  if(g_gallery.picks) {
    snprintf(line, sizeof line, L("Juntar %d", "Add %d"), g_gallery.picks);
  } else {
    snprintf(line, sizeof line, "%s", L("Conclu\xc3\xad" "do", "Done"));
  }
  accent_pill(Z_G_ADD, line, G_X + G_W - G_MARGIN, y + 2, 60, 1, 0, 0, frame);

  if(g_gallery.state == GALLERY_LOADING || g_gallery.state == GALLERY_NONE) {
    spinner(G_X + G_W / 2.0f, G_Y + G_H / 2.0f - 30, 30, 6, frame, ACCENT);
    snprintf(line, sizeof line, "%s", L("A ler as capturas da PS5\xe2\x80\xa6", "Reading the PS5's captures\xe2\x80\xa6"));
    text(f, G_X + (G_W - width_of(f, line)) / 2, G_Y + G_H / 2 + 50, line, GREY);
  } else if(g_gallery.state == GALLERY_FAILED || !g_gallery.count) {
    draw_icon(&g_dst, ICON_PHOTO, G_X + G_W / 2 - 40, G_Y + G_H / 2 - 120, 80, DIM);
    if(g_gallery.state == GALLERY_FAILED) {
      snprintf(line, sizeof line, L("N\xc3\xa3o foi poss\xc3\xadvel ler as capturas (%d)",
                                    "The captures could not be read (%d)"), g_gallery.code);
    } else {
      snprintf(line, sizeof line, "%s", L("Ainda n\xc3\xa3o h\xc3\xa1 capturas", "No captures yet"));
    }
    text(font(F_HEADING), G_X + (G_W - width_of(font(F_HEADING), line)) / 2, G_Y + G_H / 2 + 10, line, FG);
    snprintf(line, sizeof line, "%s", g_gallery.state == GALLERY_FAILED ?
             L("Carrega em \xe2\x96\xa1 para tentar outra vez.", "Press \xe2\x96\xa1 to try again.") :
             L("Carrega no bot\xc3\xa3o Create do comando para tirar uma foto ou gravar um v\xc3\xad" "deo.",
               "Press the controller's Create button to take a picture or record a video."));
    text(small, G_X + (G_W - width_of(small, line)) / 2, G_Y + G_H / 2 + 56, line, GREY);
  } else {
    // the grid, cut to its part of the pane
    g_dst = text_band(&screen, G_TOP, G_BOTTOM - G_TOP);
    g_off_y = g_clip_top = G_TOP;
    g_clip_bottom = G_BOTTOM;
    rows = (g_gallery.count + G_COLS - 1) / G_COLS;
    for(int i=0; i < g_gallery.count; i++) {
      const capture_t *c = &g_gallery.list[i];
      int tx = x + i % G_COLS * (G_TILE_W + G_GAP), ty = 8 + i / G_COLS * G_PITCH - g_gallery.scroll;
      if(ty + G_TILE_H < 0 || ty > G_BOTTOM - G_TOP) {
        continue;
      }
      over = zone(Z_G_TILE, i, 0, tx, ty, G_TILE_W, G_TILE_H);
      draw_capture(c, tx, ty, G_TILE_W, G_TILE_H, 18);
      age(c->time, now, when, sizeof when);
      if(is_video(c)) {
        size_text(c->size, line, sizeof line);
        snprintf(line + strlen(line), sizeof line - strlen(line), " \xc2\xb7 %s \xc2\xb7 %s",
                 c->kind == CAPTURE_WEBM ? "WebM" : "MP4", when);
      } else {
        snprintf(line, sizeof line, "%s", when);
      }
      tile_label(line, tx, ty, G_TILE_H);
      if(c->kind == CAPTURE_WEBM) {
        draw_box(&g_dst, tx, ty, G_TILE_W, G_TILE_H, 18, 0, RGBA(0, 0, 0, 110));
      }
      // the place in the order picked, or an empty ring
      at = -1;
      for(int k=0; k < g_gallery.picks; k++) {
        at = g_gallery.picked[k] == i ? k : at;
      }
      if(at >= 0) {
        draw_box(&g_dst, tx, ty, G_TILE_W, G_TILE_H, 18, 4, ACCENT);
        draw_circle(&g_dst, tx + G_TILE_W - 34, ty + 34, 20, ACCENT);
        snprintf(line, sizeof line, "%d", at + 1);
        text(font(F_SMALL_BOLD), tx + G_TILE_W - 34 - width_of(font(F_SMALL_BOLD), line) / 2,
             baseline(font(F_SMALL_BOLD), ty + 14, 40), line, WHITE);
      } else if(c->kind != CAPTURE_WEBM) {
        draw_circle(&g_dst, tx + G_TILE_W - 34, ty + 34, 20, RGBA(0, 0, 0, 90));
        draw_arc(&g_dst, tx + G_TILE_W - 34, ty + 34, 18.5f, 2.5f, 0, 6.2832f, WHITE);
      }
      if(i == g_gallery.sel && !g_ptr.shown) {
        draw_box(&g_dst, tx - 7, ty - 7, G_TILE_W + 14, G_TILE_H + 14, 25, 3.5f, WHITE);
      } else if(over) {
        draw_box(&g_dst, tx - 6, ty - 6, G_TILE_W + 12, G_TILE_H + 12, 24, 3, HOVER_RIM);
      }
    }
    g_dst = screen;
    g_off_y = g_clip_top = 0;
    g_clip_bottom = UI_HEIGHT;
    // the scroll bar
    top = rows * G_PITCH;
    if(top > G_VIEW_H) {
      int bar = (G_BOTTOM - G_TOP) * G_VIEW_H / top;
      int at_y = G_TOP + (int)((long long)(G_BOTTOM - G_TOP - bar) * g_gallery.scroll / (top - G_VIEW_H));
      draw_box(&g_dst, G_X + G_W - 22, at_y, 6, bar, 3, 0, RGBA(255, 255, 255, 60));
    }
  }
  snprintf(line, sizeof line, "%s", L("\xe2\x9c\x95 Escolher  \xc2\xb7  R2 Juntar  \xc2\xb7  \xe2\x96\xa1 Ler outra vez  \xc2\xb7  \xe2\x97\x8b Voltar",
                                      "\xe2\x9c\x95 Pick  \xc2\xb7  R2 Add  \xc2\xb7  \xe2\x96\xa1 Read again  \xc2\xb7  \xe2\x97\x8b Back"));
  draw_hints(G_X + G_W / 2, G_Y + G_H - 58, line);
}


static void
draw_toggle(int right, int cy, int on) {
  int x = right - 80;

  draw_box(&g_dst, x, cy - 22, 80, 44, 22, 0, on ? ACCENT : RGBA(255, 255, 255, 44));
  draw_circle(&g_dst, on ? x + 80 - 22 : x + 22, cy, 17, on ? WHITE : RGBA(214, 206, 199, 255));
}


/**
 * a picture of rows rows, as Settings names it: 1080p, 1440p, 4K
 **/
static void
screen_name(char *buf, size_t size, int rows) {
  if(rows >= 2160) {
    snprintf(buf, size, "4K");
  } else {
    snprintf(buf, size, "%dp", rows);
  }
}


static void
draw_settings(void) {
  static const int icons[S_ROWS] = { // -1: the pointer's own ring
    ICON_GLOBE, ICON_MOON, ICON_SCREEN, ICON_BELL, ICON_REFRESH, ICON_SOUND, -1, ICON_VIDEO, ICON_VIEWS,
    ICON_EYE_OFF, ICON_LIGHT, ICON_CUP, ICON_RISING, ICON_PHOTO, ICON_PERSON, ICON_REFRESH,
  };
  text_font_t *title = font(F_BRAND), *small = font(F_SMALL), *label = font(F_SIDE_BOLD), *value = font(F_PILL);
  const x_user_t *me = g_tabs[UI_ME].has_user ? &g_tabs[UI_ME].user : NULL;
  int x = S_X + 40, y = S_Y + 30, right = S_X + S_W - 48, ry, cy, over, focused, vw, accounts;
  const char *name, *desc;
  char shown[96], sub[200];
  uint32_t color;

  draw_pane(S_X, S_Y, S_W, S_H);
  draw_circle(&g_dst, x + 30, y + 32, 30, ACCENT);
  draw_icon(&g_dst, ICON_GEAR, x + 12, y + 14, 36, WHITE);
  text(title, x + 84, y + 31, L("Defini\xc3\xa7\xc3\xb5" "es", "Settings"), FG);
  snprintf(sub, sizeof sub, "XPS5 \xc2\xb7 %s", L("Cliente n\xc3\xa3o oficial do X \xc2\xb7 v\xc3\xad" "deo com FFmpeg (LGPL 2.1)",
                                                   "An unofficial X client \xc2\xb7 video by FFmpeg (LGPL 2.1)"));
  text_fitted(small, x + 84, y + 63, sub, S_W - 84 - 40 - 130, GREY);
  round_button(Z_S_CLOSE, S_X + S_W - 70, y + 32, ICON_CROSS);
  y += 96;

  if(S_ROWS > S_SHOWN) {
    // how far down the rows are, and where the pointer turns the page
    const int ty = y + 6, th = S_SHOWN * S_ROW_H + 4, tx = S_X + S_W - 14;
    const int kh = th * S_SHOWN / S_ROWS, ky = ty + (th - kh) * g_set_top / (S_ROWS - S_SHOWN);
    draw_box(&g_dst, tx - 3, ty, 6, th, 3, 0, RGBA(255, 255, 255, 22));
    draw_box(&g_dst, tx - 3, ky, 6, kh, 3, 0, RGBA(255, 255, 255, 120));
    if(ky > ty) {
      zone(Z_S_PAGE, -1, 0, tx - 14, ty, 28, ky - ty);
    }
    if(ky + kh < ty + th) {
      zone(Z_S_PAGE, 1, 0, tx - 14, ky + kh, 28, ty + th - ky - kh);
    }
  }
  for(int row=g_set_top; row < S_ROWS && row < g_set_top + S_SHOWN; row++) {
    ry = y + (row - g_set_top) * S_ROW_H + (row >= S_SESSION ? 20 : 0);
    cy = ry + (S_ROW_H - 8) / 2;
    focused = row == g_set_row && row < S_CHOICES && !g_ptr.shown;
    over = row < S_CHOICES && zone(Z_S_ROW, row, 0, S_X + 20, ry, S_W - 40, S_ROW_H - 8);
    if(row == S_SESSION) {
      draw_rect(&g_dst, S_X + 40, ry - 14, S_W - 80, 2, RGBA(255, 255, 255, 26));
    }
    if(focused) {
      draw_box(&g_dst, S_X + 20, ry, S_W - 40, S_ROW_H - 8, 26, 0, CHIP_ON);
    } else if(over) {
      lit_box(S_X + 20, ry, S_W - 40, S_ROW_H - 8, 26, 1);
    }
    color = (row == S_EVERY || row == S_RISING) && !g_settings.notify ? DIM : FG;
    draw_circle(&g_dst, x + 30, cy, 28, CHIP);
    if(icons[row] >= 0) {
      draw_icon(&g_dst, icons[row], x + 13, cy - 17, 34, color);
    } else {
      draw_arc(&g_dst, x + 30, cy, 13, 2.6f, 0, 6.2832f, color);
      draw_circle(&g_dst, x + 30, cy, 3.5f, color);
    }
    shown[0] = 0;
    switch(row) {
    case S_LANG:
      name = L("Idioma", "Language");
      desc = L("O idioma da app e das notifica\xc3\xa7\xc3\xb5" "es", "The language of the app and of the notifications");
      if(g_settings.lang == LANG_SYSTEM) {
        snprintf(shown, sizeof shown, L("O da PS5 (%s)", "The PS5's (%s)"), lang_name(g_system_lang));
      } else {
        snprintf(shown, sizeof shown, "%s", lang_name(g_settings.lang));
      }
      break;
    case S_THEME:
      name = L("Tema", "Theme");
      desc = L("Dia: o fundo quente \xc2\xb7 Noite: o fundo preto \xc2\xb7 o banner d\xc3\xa1-lhe cor",
               "Day: the warm backdrop \xc2\xb7 Night: the black one \xc2\xb7 banners colour it");
      snprintf(shown, sizeof shown, "%s", NIGHT ? L("Noite", "Night") : L("Dia", "Day"));
      break;
    case S_SCREEN:
      // what it is now, and what it will be when the app opens again
      name = L("Resolu\xc3\xa7\xc3\xa3o", "Resolution");
      vw = pxs(UI_HEIGHT);
      screen_name(shown, sizeof shown, vw);
      snprintf(sub, sizeof sub, L("Agora %s \xc2\xb7 muda ao reabrir a XPS5 \xc2\xb7 a Autom\xc3\xa1tica segue a TV",
                                  "Now %s \xc2\xb7 changes when XPS5 opens again \xc2\xb7 Auto follows the TV"), shown);
      desc = sub;
      if(!g_settings.screen) {
        snprintf(shown, sizeof shown, "%s", L("Autom\xc3\xa1tica", "Auto"));
      } else {
        screen_name(shown, sizeof shown, g_settings.screen);
      }
      break;
    case S_NOTIFY:
      name = L("Notifica\xc3\xa7\xc3\xb5" "es sobre o jogo", "Notifications over the game");
      desc = L("Gostos, respostas, men\xc3\xa7\xc3\xb5" "es e seguidores novos do X", "New X likes, replies, mentions and followers");
      break;
    case S_EVERY:
      name = L("Ver se h\xc3\xa1 novas a cada", "Check for new ones every");
      desc = L("Mais vezes faz mais pedidos ao X", "More often makes more requests to X");
      if(g_settings.every < 60) {
        snprintf(shown, sizeof shown, "%d s", g_settings.every);
      } else {
        snprintf(shown, sizeof shown, "%d min", g_settings.every / 60);
      }
      break;
    case S_SOUND:
      name = L("Som dos v\xc3\xad" "deos", "Video sound");
      desc = L("Os v\xc3\xad" "deos abertos tocam com som; \xe2\x96\xa1 muda-o enquanto tocam",
               "Opened videos play with sound; \xe2\x96\xa1 changes it as they play");
      break;
    case S_HOVER:
      name = L("Som do cursor", "Pointer sound");
      desc = L("Um toque suave quando o cursor acende um bot\xc3\xa3o ou um post",
               "A soft tick when the pointer lights up a button or a post");
      break;
    case S_AUTOPLAY:
      name = L("V\xc3\xad" "deos na cronologia", "Videos in the timeline");
      desc = L("O v\xc3\xad" "deo do post escolhido toca sem som, como no x.com",
               "The chosen post's video plays muted, as on x.com");
      break;
    case S_SAVER:
      name = L("Poupan\xc3\xa7" "a de dados", "Data saver");
      desc = L("V\xc3\xad" "deos na qualidade mais baixa, que chegam mais depressa",
               "Videos at the lowest quality, which come sooner");
      break;
    case S_SPOILER:
      name = L("Modo anti-spoiler", "Anti-spoiler mode");
      if(g_spoil_name[0]) {
        snprintf(sub, sizeof sub, L("Tapa os posts sobre %s, o jogo que est\xc3\xa1 a correr",
                                    "Covers the posts about %s, the game that is running"), g_spoil_name);
        desc = sub;
      } else {
        desc = L("Tapa os posts que falam do jogo que est\xc3\xa1 a correr \xc2\xb7 \xe2\x9c\x95 mostra-os",
                 "Covers the posts about the game that is running \xc2\xb7 \xe2\x9c\x95 shows them");
      }
      break;
    case S_LIGHT:
      name = L("Luz do comando", "Controller light");
      desc = L("A barra de luz do comando ganha a cor do banner que se v\xc3\xaa",
               "The controller's light bar takes the colour of the banner shown");
      break;
    case S_BREAK:
      name = L("Lembrete de pausa", "Break reminder");
      desc = L("Diz quando j\xc3\xa1 est\xc3\xa1s h\xc3\xa1 muito tempo na XPS5",
               "Tells you when you have been on XPS5 for a long time");
      if(!g_settings.pause) {
        snprintf(shown, sizeof shown, "%s", L("Desligado", "Off"));
      } else if(g_settings.pause % 60) {
        snprintf(shown, sizeof shown, "%d min", g_settings.pause);
      } else {
        snprintf(shown, sizeof shown, "%d h", g_settings.pause / 60);
      }
      break;
    case S_RISING:
      name = L("Os teus posts a subir", "Your posts rising");
      desc = L("Uma notifica\xc3\xa7\xc3\xa3o sobre o jogo quando um post teu ganha gostos depressa",
               "A notification over the game when a post of yours gets likes fast");
      break;
    case S_SHARE:
      name = L("Partilhar capturas e trof\xc3\xa9us", "Share captures and trophies");
      desc = L("Cada captura ou trof\xc3\xa9u novo do jogo chega numa notifica\xc3\xa7\xc3\xa3o e abre num post",
               "Each new capture or trophy of the game comes in a notification and opens in a post");
      break;
    case S_PLAYING:
      name = L("A jogar no teu perfil", "Playing on your profile");
      desc = L("A localiza\xc3\xa7\xc3\xa3o do teu perfil do X mostra o jogo e volta quando paras",
               "Your X profile's location shows the game, then goes back when you stop");
      break;
    case S_SESSION:
      name = L("Conta do X", "X account");
      accounts = 0;
      for(int i=0; i < SETTINGS_ACCOUNTS; i++) {
        accounts += g_accounts >> i & 1;
      }
      if(accounts > 1 && g_login.ready) {
        snprintf(sub, sizeof sub, L("%d contas \xc2\xb7 \xe2\x86\x90 \xe2\x86\x92 troca \xc2\xb7 \xe2\x9c\x95 entra noutra",
                                    "%d accounts \xc2\xb7 \xe2\x86\x90 \xe2\x86\x92 switch \xc2\xb7 \xe2\x9c\x95 log in to another"),
                 accounts);
      } else if(g_login.ready) {
        snprintf(sub, sizeof sub, "%s", L("\xe2\x9c\x95 Entra no X aqui na PS5, na p\xc3\xa1gina do pr\xc3\xb3prio X",
                                          "\xe2\x9c\x95 Log in to X here on the PS5, on X's own page"));
      } else if(accounts > 1) {
        snprintf(sub, sizeof sub, L("%d contas \xc2\xb7 \xe2\x86\x90 \xe2\x86\x92 troca \xc2\xb7 \xe2\x9c\x95 liga outra",
                                    "%d accounts \xc2\xb7 \xe2\x86\x90 \xe2\x86\x92 switch \xc2\xb7 \xe2\x9c\x95 link another"),
                 accounts);
      } else {
        snprintf(sub, sizeof sub, "%s", L("\xe2\x9c\x95 Liga-a com o telem\xc3\xb3vel ou o PC: l\xc3\xaa o QR",
                                          "\xe2\x9c\x95 Link it with your phone or PC: scan the QR"));
      }
      desc = sub;
      if(me && me->user[0]) {
        snprintf(shown, sizeof shown, "@%s", me->user);
      } else if(g_settings.users[g_settings.account - 1][0] && g_session_found) {
        snprintf(shown, sizeof shown, "@%s", g_settings.users[g_settings.account - 1]);
      } else {
        snprintf(shown, sizeof shown, "%s", g_session_found ? L("Lida", "Read") : L("Em falta", "Missing"));
      }
      break;
    case S_UPDATE:
      name = L("Atualiza\xc3\xa7\xc3\xb5" "es", "Updates");
      snprintf(shown, sizeof shown, "v%s", XPS5_VERSION);
      switch(g_update.state) {
      case UI_UPDATE_OFF:
        snprintf(sub, sizeof sub, "%s", L("Esta vers\xc3\xa3o n\xc3\xa3o procura atualiza\xc3\xa7\xc3\xb5" "es",
                                          "This build does not look for updates"));
        break;
      case UI_UPDATE_CHECKING:
        snprintf(sub, sizeof sub, "%s", L("A procurar uma vers\xc3\xa3o nova\xe2\x80\xa6", "Looking for a new version\xe2\x80\xa6"));
        break;
      case UI_UPDATE_LATEST:
        snprintf(sub, sizeof sub, "%s", L("Tens a vers\xc3\xa3o mais recente \xc2\xb7 \xe2\x9c\x95 procura outra vez",
                                          "You have the latest version \xc2\xb7 \xe2\x9c\x95 looks again"));
        break;
      case UI_UPDATE_FOUND:
        snprintf(sub, sizeof sub, L("A vers\xc3\xa3o %s chegou \xc2\xb7 \xe2\x9c\x95 instala-a (a XPS5 fecha para isso)",
                                    "Version %s is out \xc2\xb7 \xe2\x9c\x95 installs it (XPS5 closes for it)"),
                 g_update.version);
        break;
      case UI_UPDATE_GETTING:
        snprintf(sub, sizeof sub, L("A descarregar a vers\xc3\xa3o %s\xe2\x80\xa6 %d%%", "Downloading version %s\xe2\x80\xa6 %d%%"),
                 g_update.version, g_update.percent);
        break;
      case UI_UPDATE_INSTALLING:
        snprintf(sub, sizeof sub, "%s", L("A instalar: a XPS5 fecha, depois abre-a no ecr\xc3\xa3 inicial",
                                          "Installing: XPS5 closes, then open it from the home screen"));
        break;
      case UI_UPDATE_FAILED: {
        char why[64];
        action_error(g_update.error, g_update.code, -1, why, sizeof why);
        snprintf(sub, sizeof sub, L("N\xc3\xa3o deu (%s) \xc2\xb7 \xe2\x9c\x95 tenta outra vez",
                                    "It did not work (%s) \xc2\xb7 \xe2\x9c\x95 tries again"), why);
        break;
      }
      default:
        snprintf(sub, sizeof sub, "%s", L("\xe2\x9c\x95 procura uma vers\xc3\xa3o nova", "\xe2\x9c\x95 looks for a new version"));
        break;
      }
      desc = sub;
      break;
    default:
      name = desc = "";
      break;
    }
    text_fitted(label, x + 84, cy - 6, name, 560, color);
    // the line under the name stops short of a value (and its ‹ ›) on the right
    vw = shown[0] ? width_of(value, shown) + (focused && row != S_UPDATE ? 64 : 28) : 100;
    text_fitted(small, x + 84, cy + 28, desc, right - vw - x - 84, GREY);
    if(row == S_NOTIFY || row == S_SOUND || row == S_HOVER || row == S_AUTOPLAY || row == S_SAVER ||
       row == S_SPOILER || row == S_LIGHT || row == S_RISING || row == S_SHARE || row == S_PLAYING) {
      draw_toggle(right, cy, row == S_NOTIFY ? g_settings.notify : row == S_SOUND ? g_settings.sound :
                  row == S_HOVER ? g_settings.hover : row == S_AUTOPLAY ? g_settings.autoplay :
                  row == S_SPOILER ? g_settings.spoilers : row == S_LIGHT ? g_settings.light :
                  row == S_RISING ? g_settings.rising : row == S_SHARE ? g_settings.share :
                  row == S_PLAYING ? g_settings.playing : g_settings.saver);
    } else if(shown[0]) {
      vw = width_of(value, shown);
      if(focused && row == S_UPDATE) { // ← and → do not change it
        text(value, right - vw, baseline(value, ry, S_ROW_H - 8), shown, FG);
      } else if(focused) {
        // ‹ and › say ← and → change it
        text(value, right - vw - 40, baseline(value, ry, S_ROW_H - 8), "\xe2\x80\xb9", GREY);
        text(value, right - 14, baseline(value, ry, S_ROW_H - 8), "\xe2\x80\xba", GREY);
        text(value, right - vw - 22, baseline(value, ry, S_ROW_H - 8), shown, FG);
      } else {
        text(value, right - vw, baseline(value, ry, S_ROW_H - 8), shown,
             row == S_SESSION && !me && !g_session_found ? RED : row >= S_CHOICES ? GREY : color);
      }
    }
  }
  draw_hints(S_X + S_W / 2, S_Y + S_H - 56,
             L("\xe2\x86\x91 \xe2\x86\x93 Escolher  \xc2\xb7  \xe2\x86\x90 \xe2\x86\x92 \xe2\x9c\x95 Mudar  \xc2\xb7  \xe2\x97\x8b Fechar",
               "\xe2\x86\x91 \xe2\x86\x93 Choose  \xc2\xb7  \xe2\x86\x90 \xe2\x86\x92 \xe2\x9c\x95 Change  \xc2\xb7  \xe2\x97\x8b Close"));
}


/**
 * the line of a toast over the viewer, at the bottom, or over a sheet, at
 * the top, clear of its buttons' line
 **/
static void
draw_snackbar(void) {
  text_font_t *f = font(F_LEGEND);
  int w = width_of(f, g_toast.text) + 90, x, y = g_sheet ? 14 : UI_HEIGHT - 30 - 64;

  w = w > UI_WIDTH - 200 ? UI_WIDTH - 200 : w;
  x = (UI_WIDTH - w) / 2;
  draw_box(&g_dst, x, y, w, 64, 32, 0, g_toast.error ? RGBA(120, 24, 30, 245) : RGBA(52, 43, 37, 248));
  draw_box(&g_dst, x, y, w, 64, 32, 1.5f, EDGE);
  draw_circle(&g_dst, x + 34, y + 32, 9, g_toast.error ? WHITE : ACCENT);
  text_fitted(f, x + 60, baseline(f, y, 64), g_toast.text, w - 84, FG);
}


/**
 * an account's row of the account menu at x, y: its avatar, name and
 * screen name, and a tick on the one in use. Only the one in use has its
 * profile read; another shows the screen name it last had here
 **/
static void
menu_account(int x, int y, int account) {
  const tab_t *me = &g_tabs[UI_ME];
  text_font_t *bold = font(F_NAV_BOLD), *small = font(F_SMALL);
  const char *user = g_settings.users[account - 1];
  const int in_use = account == g_settings.account, right = x + MENU_W - 30 - (in_use ? 50 : 0);
  char line[sizeof me->user.name + 8], under[sizeof me->user.user + 8], digit[2] = {(char)('0' + account), 0};

  if(in_use && me->has_user) {
    draw_avatar(me->user.avatar, me->user.user, me->user.name, x + 26, y + (MENU_ACC_ROW - 60) / 2, 60);
    snprintf(line, sizeof line, "%s", me->user.name[0] ? me->user.name : me->user.user);
    snprintf(under, sizeof under, "@%s", me->user.user);
  } else if(user[0]) {
    draw_avatar("", user, user, x + 26, y + (MENU_ACC_ROW - 60) / 2, 60);
    snprintf(line, sizeof line, "@%s", user);
    snprintf(under, sizeof under, L("Conta %d", "Account %d"), account);
  } else {
    draw_avatar("", digit, digit, x + 26, y + (MENU_ACC_ROW - 60) / 2, 60);
    snprintf(line, sizeof line, L("Conta %d", "Account %d"), account);
    under[0] = 0;
  }
  text_fitted(bold, x + 106, y + (under[0] ? 46 : baseline(bold, 0, MENU_ACC_ROW)), line, right - x - 106, FG);
  if(under[0]) {
    text_fitted(small, x + 106, y + 78, under, right - x - 106, GREY);
  }
  if(in_use) {
    draw_icon(&g_dst, ICON_CHECK, x + MENU_W - 30 - 34, y + (MENU_ACC_ROW - 34) / 2, 34, FG);
  }
}


/**
 * a menu: a small pane by what opened it, the screen around it as it is
 **/
static void
draw_menu(void) {
  text_font_t *f = font(F_NAV);
  int h = menu_height(), x = g_menu.x, y = g_menu.y, ry, rh, icon, over, focused;
  char label[160];
  uint32_t color;

  zone(Z_SHEET, 0, 0, 0, 0, UI_WIDTH, UI_HEIGHT);
  pane_shadow(x, y, MENU_W, h, 24, 70);
  draw_box(&g_dst, x, y, MENU_W, h, 24, 0, PANE);
  draw_rim(&g_dst, x, y, MENU_W, h, 24, 2, RGBA(255, 255, 255, 110), 0.3f);
  zone(Z_PANE, 0, 0, x, y, MENU_W, h);
  for(int i=0; i < g_menu.count; i++) {
    ry = y + menu_offset(i);
    rh = g_menu.what[i] == M_ACCOUNT ? MENU_ACC_ROW : MENU_ROW;
    if(i && g_menu.what[i - 1] == M_ACCOUNT && g_menu.what[i] != M_ACCOUNT) {
      // the line under the accounts, across the pane
      draw_rect(&g_dst, x + 2, ry - MENU_GAP / 2 - 1, MENU_W - 4, 2, RGBA(255, 255, 255, 30));
    }
    over = zone(Z_M_ROW, i, 0, x + 8, ry, MENU_W - 16, rh);
    focused = i == g_menu.sel && !g_ptr.shown;
    if(focused) {
      draw_box(&g_dst, x + 8, ry, MENU_W - 16, rh, 18, 0, CHIP_ON);
    } else if(over) {
      lit_box(x + 8, ry, MENU_W - 16, rh, 18, 1);
    }
    if(g_menu.what[i] == M_ACCOUNT) {
      menu_account(x, ry, g_menu.arg[i]);
      continue;
    }
    menu_row(g_menu.what[i], label, sizeof label, &icon, &color);
    if(icon < 0) {
      text_fitted(f, x + 30, baseline(f, ry, MENU_ROW), label, MENU_W - 30 - 24, color);
      continue;
    }
    draw_icon(&g_dst, icon, x + 30, ry + (MENU_ROW - 34) / 2, 34, color);
    text_fitted(f, x + 86, baseline(f, ry, MENU_ROW), label, MENU_W - 86 - 24, color);
  }
}


/**
 * the QR code of the address a phone or a PC opens to link (the code after
 * '#', which the browser keeps to itself), made again when they change;
 * NULL while there is none
 **/
static const qr_t *
link_qr(const char *ip, const char *code) {
  static qr_t qr;
  static char made[64];
  char url[64];

  if(!ip[0] || !code[0]) {
    return NULL;
  }
  snprintf(url, sizeof url, "http://%s:%d/#%s", ip, LINK_PORT, code);
  if(strcmp(url, made)) {
    if(qr_encode(url, strlen(url), &qr)) {
      made[0] = 0;
      return NULL;
    }
    snprintf(made, sizeof made, "%s", url);
  }
  return &qr;
}


/**
 * a QR code on white, its quiet zone of four modules around it, in a square
 * of side px at x, y
 **/
static void
draw_qr(const qr_t *qr, int x, int y, int px, int dim) {
  const int cells = qr->size + 8, m = px / cells, side = m * cells, ox = x + (px - side) / 2, oy = y + (px - side) / 2;
  const uint32_t ink = dim ? RGBA(120, 112, 106, 255) : RGBA(14, 12, 12, 255);

  draw_box(&g_dst, x - 14, y - 14, px + 28, px + 28, 30, 0, RGBA(255, 255, 255, dim ? 150 : 255));
  for(int r = 0; r < qr->size; r++) {
    for(int c = 0; c < qr->size; c++) {
      if(qr->dark[r][c]) {
        draw_rect(&g_dst, ox + (c + 4) * m, oy + (r + 4) * m, m, m, ink);
      }
    }
  }
}


/**
 * the login window: on the left what it is, the account and the buttons;
 * on the right X's page, or while it is not there what goes on
 **/
static void
draw_login(unsigned frame) {
  const char *const points[2] = {
    L("A palavra-passe vai s\xc3\xb3 para o X, por HTTPS. O XPS5 n\xc3\xa3o a guarda.",
      "Your password goes only to X, over HTTPS. XPS5 does not keep it."),
    L("S\xc3\xb3 as p\xc3\xa1ginas do X abrem nesta janela.", "Only X's pages open in this window."),
  };
  const char *const keys[7][2] = { // the button and its job: the sticks on a row each, then two to a row
    {L("Man\xc3\xadpulo esquerdo", "Left stick"), L("Ponteiro", "Pointer")},
    {L("Man\xc3\xadpulo direito", "Right stick"), L("Deslizar a p\xc3\xa1gina", "Scroll the page")},
    {"\xe2\x9c\x95", L("Clicar", "Click")}, {"\xe2\x96\xb3", L("Teclado", "Keyboard")},
    {"\xe2\x86\x91 \xe2\x86\x93", L("Campo seguinte", "Next field")}, {"\xe2\x96\xa1", L("Recarregar", "Reload")},
    {"\xe2\x97\x8b", L("Fechar", "Close")},
  };
  text_font_t *title = font(F_TITLE), *body = font(F_SIDE), *small = font(F_SMALL), *key = font(F_KEY),
              *value = font(F_PILL);
  const ui_login_t *st = &g_login.st;
  const int x = LG_X, top = LG_Y, in = x + 64, cw = LG_PX - 64 - in;
  const int account = g_login.account;
  const char *user = account >= 1 && account <= SETTINGS_ACCOUNTS ? g_settings.users[account - 1] : "";
  const int shown = st->state == UI_LOGIN_SHOWN && g_login.pixels && g_login.pw == pxs(LG_PW) &&
                    g_login.ph == pxs(LG_PH);
  char line[300];
  int y, lw, over, mark = 0;

  draw_pane(x, top, LG_W, LG_H);

  // what it is
  text_fitted(title, in, baseline(title, top + 52, 64), L("Entrar no X", "Log in to X"), cw - 84, FG);
  round_button(Z_LG_CLOSE, in + cw - 32, top + 84, ICON_CROSS);
  y = text_wrapped(body, in, top + 136, 38,
                   L("A p\xc3\xa1gina de entrada do pr\xc3\xb3prio X, aqui na tua PS5. Sem PC nem telem\xc3\xb3vel.",
                     "X's own login page, right here on your PS5. No PC or phone needed."), cw, GREY) + 26;
  for(int i=0; i<2; i++) {
    draw_circle(&g_dst, in + 7, y + 19, 6, ACCENT);
    y = text_wrapped(body, in + 30, y, 38, points[i], cw - 30, FG) + 14;
  }
  y += 12;

  // the account the session is for: ‹ Account 2 · @name ›
  if((g_accounts >> (account - 1) & 1) && user[0]) {
    snprintf(line, sizeof line, L("Conta %d \xc2\xb7 fica no lugar de @%s", "Account %d \xc2\xb7 replaces @%s"),
             account, user);
  } else if(g_accounts >> (account - 1) & 1) {
    snprintf(line, sizeof line, L("Conta %d \xc2\xb7 fica no lugar da sess\xc3\xa3o que l\xc3\xa1 est\xc3\xa1",
                                  "Account %d \xc2\xb7 replaces the session there"), account);
  } else {
    snprintf(line, sizeof line, L("Conta %d \xc2\xb7 nova", "Account %d \xc2\xb7 new"), account);
  }
  draw_box(&g_dst, in, y, cw, 64, 32, 0, CHIP);
  lw = width_of(small, L("Na PS5", "On the PS5"));
  text(small, in + 26, baseline(small, y, 64), L("Na PS5", "On the PS5"), GREY);
  text_fitted(value, in + 26 + lw + 20, baseline(value, y, 64), line, cw - lw - 20 - 26 - 130, FG);
  for(int i=0; i<2; i++) {
    const int cx = in + cw - (i ? 34 : 96);
    over = zone(i ? Z_LG_NEXT : Z_LG_PREV, 0, 0, cx - 32, y, 64, 64);
    draw_circle(&g_dst, cx, y + 32, 24, CHIP);
    if(over) {
      lit_circle(cx, y + 32, 24, 1);
    }
    text(value, cx - width_of(value, i ? "\xe2\x80\xba" : "\xe2\x80\xb9") / 2, baseline(value, y, 64),
         i ? "\xe2\x80\xba" : "\xe2\x80\xb9", FG);
  }
  y += 64 + 26;

  // how it goes
  line[0] = 0;
  if(st->state == UI_LOGIN_FAILED) {
    mark = 3;
    switch(st->why) {
    case UI_LOGIN_NO_ENGINE:
      snprintf(line, sizeof line, "%s", L("O motor da janela de entrada n\xc3\xa3o est\xc3\xa1 nesta PS5. Instala o XPS5 outra vez.",
                                          "The login window's engine is not on this PS5. Install XPS5 again."));
      break;
    case UI_LOGIN_NO_READ:
      snprintf(line, sizeof line, L("O XPS5 n\xc3\xa3o conseguiu ler a janela de entrada (%d). Fecha o XPS5 e abre-o outra vez.",
                                    "XPS5 could not read its login window (%d). Close XPS5 and open it again."),
               st->code);
      break;
    case UI_LOGIN_NO_LOADER:
      snprintf(line, sizeof line, L("A janela precisa do carregador de ELF do etaHEN (porta 9021): liga-o e carrega em \xe2\x96\xa1 (0x%08X).",
                                    "The window needs etaHEN's ELF loader (port 9021): turn it on and press \xe2\x96\xa1 (0x%08X)."),
               (unsigned)st->code);
      break;
    case UI_LOGIN_CRASHED:
      snprintf(line, sizeof line, "%s", L("A p\xc3\xa1gina do X parou. Carrega em \xe2\x96\xa1 para a abrir outra vez.",
                                          "X's page stopped. Press \xe2\x96\xa1 to open it again."));
      break;
    default:
      snprintf(line, sizeof line, L("A janela de entrada parou (%d). Carrega em \xe2\x96\xa1 para tentar outra vez.",
                                    "The login window stopped (%d). Press \xe2\x96\xa1 to try again."), st->code);
      break;
    }
  } else if(st->state == UI_LOGIN_STARTING) {
    mark = 1;
    snprintf(line, sizeof line, "%s", L("A abrir a p\xc3\xa1gina de entrada do X\xe2\x80\xa6",
                                        "Opening X's login page\xe2\x80\xa6"));
  } else if(st->why == UI_LOGIN_PAGE) {
    mark = 3;
    snprintf(line, sizeof line, L("A p\xc3\xa1gina do X n\xc3\xa3o abriu (erro %d). V\xc3\xaa a internet da PS5 e carrega em \xe2\x96\xa1.",
                                  "X's page did not load (error %d). Check the PS5's internet and press \xe2\x96\xa1."), st->code);
  } else if(g_login.note && g_ui_us - g_login.note_us < LG_NOTE) {
    mark = 4;
    snprintf(line, sizeof line, "%s", g_login.note == 1 ?
             L("Esse link sai do X: s\xc3\xb3 as p\xc3\xa1ginas do X abrem aqui.",
               "That link leaves X: only X's pages open here.") :
             L("Nenhum campo escolhido: aponta para um campo da p\xc3\xa1gina e carrega em \xe2\x9c\x95.",
               "No field chosen: point at a field of the page and press \xe2\x9c\x95."));
  } else if(st->loading) {
    mark = 1;
    snprintf(line, sizeof line, L("A carregar\xe2\x80\xa6 %d%%", "Loading\xe2\x80\xa6 %d%%"), st->progress);
  } else if(st->on_x) {
    mark = 2;
    snprintf(line, sizeof line, "%s", L("x.com \xc2\xb7 liga\xc3\xa7\xc3\xa3o segura (HTTPS)",
                                        "x.com \xc2\xb7 secure connection (HTTPS)"));
  }
  if(line[0]) {
    if(mark == 1) {
      spinner(in + 14, y + 19, 12, 4, frame, ACCENT);
      g_spinning = 1;
    } else {
      draw_circle(&g_dst, in + 14, y + 19, 7, mark == 2 ? GREEN : mark == 4 ? ACCENT : RED);
    }
    text_wrapped(body, in + 40, y, 38, line, cw - 40, mark >= 3 ? FG : GREY);
  }

  // the buttons at the bottom
  y = top + LG_H - 48 - 5 * 48;
  for(int i=0; i<7; i++) {
    // the first column a little wider, for "Next field" in the longer languages
    const int row = i < 2 ? i : 2 + (i - 2) / 2, col = i < 2 ? 0 : (i - 2) % 2, first = cw * 11 / 20;
    const int kx = in + col * first, ky = y + row * 48, room = (i < 2 ? cw : col ? cw - first : first) - 12;
    const int kw = width_of(key, keys[i][0]) + 28;
    draw_box(&g_dst, kx, ky + 5, kw, 38, 12, 0, CHIP);
    text(key, kx + 14, baseline(key, ky + 5, 38), keys[i][0], FG);
    text_fitted(small, kx + kw + 14, baseline(small, ky + 5, 38), keys[i][1], room - kw - 14, GREY);
  }

  // the page
  draw_shadow(&g_dst, LG_PX, LG_PY, LG_PW, LG_PH, LG_R, 36, RGBA(0, 0, 0, 150));
  if(shown) {
    draw_picture_round(&g_dst, LG_PX, LG_PY, LG_PW, LG_PH, LG_R, g_login.pixels);
    if(st->loading) {
      const int bw = (LG_PW - 2 * LG_R) * (st->progress < 5 ? 5 : st->progress) / 100;
      draw_box(&g_dst, LG_PX + LG_R, LG_PY + 8, bw, 6, 3, 0, ACCENT);
    }
  } else {
    draw_box(&g_dst, LG_PX, LG_PY, LG_PW, LG_PH, LG_R, 0, TILE);
    if(st->state == UI_LOGIN_FAILED) {
      draw_circle(&g_dst, LG_PX + LG_PW / 2, LG_PY + LG_PH / 2 - 30, 40, CHIP);
      text(title, LG_PX + LG_PW / 2 - width_of(title, "!") / 2, baseline(title, LG_PY + LG_PH / 2 - 70, 80),
           "!", RED);
      lw = width_of(value, L("\xe2\x96\xa1  Tentar outra vez", "\xe2\x96\xa1  Try again"));
      text(value, LG_PX + (LG_PW - lw) / 2, baseline(value, LG_PY + LG_PH / 2 + 40, 50),
           L("\xe2\x96\xa1  Tentar outra vez", "\xe2\x96\xa1  Try again"), FG);
    } else {
      spinner(LG_PX + LG_PW / 2.0f, LG_PY + LG_PH / 2.0f, 26, 6, frame, ACCENT);
      g_spinning = 1;
    }
  }
  draw_box(&g_dst, LG_PX, LG_PY, LG_PW, LG_PH, LG_R, 1.5f, EDGE);
  zone(Z_LG_PAGE, 0, 0, LG_PX, LG_PY, LG_PW, LG_PH);
}


/**
 * linking an account: the QR code a phone reads (or the address a PC opens),
 * the steps, the code, how it goes, and the account the session goes to
 **/
#define LK_W  1500
#define LK_H  880
#define LK_QR 400 // the QR code's side

static void
draw_link(unsigned frame) {
  static const char *const steps[3][2] = {
    {"Instala a extens\xc3\xa3o XPS5 Login no Chrome, no Edge ou no Firefox (tamb\xc3\xa9m no Firefox para Android).",
     "Install the XPS5 Login extension in Chrome, Edge or Firefox (Firefox for Android too)."},
    {"Entra no x.com nesse browser, com a conta que queres na PS5.",
     "Sign in to x.com in that browser, with the account you want on the PS5."},
    {"L\xc3\xaa o QR com o telem\xc3\xb3vel ou abre o endere\xc3\xa7o no PC, toca no bot\xc3\xa3o da extens\xc3\xa3o e depois em Ligar.",
     "Scan the QR with your phone or open the address on the PC, tap the extension's button, then Link."},
  };
  text_font_t *title = font(F_BRAND), *small = font(F_SMALL), *body = font(F_SIDE), *bold = font(F_SMALL_BOLD),
              *mid = font(F_TITLE), *value = font(F_PILL);
  const ui_link_t *st = &g_linking.st;
  const int x = (UI_WIDTH - LK_W) / 2, top = (UI_HEIGHT - LK_H) / 2, in = x + 52;
  const int qx = in + 14, qy = top + 176, cx = qx + LK_QR + 84, width = x + LK_W - 52 - cx, bw = (width - 24) / 2;
  const int ready = st->state != UI_LINK_STARTING && st->state != UI_LINK_NO_NETWORK &&
                    st->state != UI_LINK_NO_HELPER;
  const int busy = st->state == UI_LINK_CHECKING || st->state == UI_LINK_DONE;
  const int account = st->state == UI_LINK_DONE ? st->account : g_linking.account;
  const char *user = account >= 1 && account <= SETTINGS_ACCOUNTS ? g_settings.users[account - 1] : "";
  const qr_t *qr = ready ? link_qr(st->ip, st->code) : NULL;
  const char *label;
  char line[300], num[4], shown[40], tag[96];
  int y = top + 30, lw, mark = 0, over;

  draw_pane(x, top, LK_W, LK_H);
  text(title, in, y + 31, L("Liga a tua conta do X", "Link your X account"), FG);
  text(small, in, y + 63, L("Com o telem\xc3\xb3vel ou com o PC: o browser onde tens o x.com aberto manda a sess\xc3\xa3o para a PS5",
                            "With your phone or your PC: the browser where x.com is open sends the session to the PS5"),
       GREY);
  round_button(Z_L_CLOSE, x + LK_W - 70, y + 32, ICON_CROSS);

  // the QR code, on the left
  if(qr) {
    draw_qr(qr, qx, qy, LK_QR, st->state == UI_LINK_DONE);
  } else {
    draw_box(&g_dst, qx - 14, qy - 14, LK_QR + 28, LK_QR + 28, 30, 0, CHIP);
    draw_box(&g_dst, qx - 14, qy - 14, LK_QR + 28, LK_QR + 28, 30, 1.5f, EDGE);
    if(st->state == UI_LINK_STARTING) {
      spinner(qx + LK_QR / 2.0f, qy + LK_QR / 2.0f, 26, 6, frame, ACCENT);
      g_spinning = 1;
    } else {
      draw_icon(&g_dst, ICON_CROSS, qx + LK_QR / 2 - 30, qy + LK_QR / 2 - 30, 60, DIM);
    }
  }
  label = L("L\xc3\xaa com a c\xc3\xa2mara do telem\xc3\xb3vel", "Scan it with your phone's camera");
  text(bold, qx + (LK_QR - width_of(bold, label)) / 2, baseline(bold, qy + LK_QR + 30, 40), label, FG);

  // the steps, on the right
  y = top + 150;
  for(int i=0; i<3; i++) {
    draw_circle(&g_dst, cx + 19, y + 19, 19, CHIP_ON);
    snprintf(num, sizeof num, "%d", i + 1);
    text(bold, cx + 19 - width_of(bold, num) / 2, baseline(bold, y, 38), num, FG);
    y = text_wrapped(body, cx + 58, y, 38, L(steps[i][0], steps[i][1]), width - 58, i == 2 ? FG : GREY) + 14;
  }
  y += 10;

  // the address for a PC, and the code the extension fills in from the QR
  for(int i=0; i<2; i++) {
    int bx = cx + i * (bw + 24);
    if(i) {
      label = L("C\xc3\xb3" "digo", "Code");
      snprintf(shown, sizeof shown, "%.3s %.3s", st->code, st->code + 3);
    } else {
      snprintf(tag, sizeof tag, "%s", L("No PC, abre", "On a PC, open"));
      label = tag;
      snprintf(shown, sizeof shown, "%s:%d", st->ip, LINK_PORT);
    }
    draw_box(&g_dst, bx, y, bw, 112, 24, 0, CHIP);
    draw_box(&g_dst, bx, y, bw, 112, 24, 1.5f, EDGE);
    text(small, bx + (bw - width_of(small, label)) / 2, baseline(small, y + 10, 34), label, GREY);
    if(ready) {
      int sw = width_of(mid, shown);
      text_fitted(mid, sw < bw - 40 ? bx + (bw - sw) / 2 : bx + 20, baseline(mid, y + 44, 56), shown,
                  bw - 40, st->state == UI_LINK_DONE ? DIM : FG);
    } else {
      text(mid, bx + (bw - width_of(mid, "\xe2\x80\x93")) / 2, baseline(mid, y + 44, 56), "\xe2\x80\x93", DIM);
    }
  }
  y += 112 + 24;

  // how it goes
  switch(st->state) {
  case UI_LINK_STARTING:
    snprintf(line, sizeof line, "%s", L("A preparar a liga\xc3\xa7\xc3\xa3o\xe2\x80\xa6", "Getting ready\xe2\x80\xa6"));
    mark = 1;
    break;
  case UI_LINK_CHECKING:
    snprintf(line, sizeof line, "%s", L("A confirmar a sess\xc3\xa3o com o X\xe2\x80\xa6",
                                        "Checking the session with X\xe2\x80\xa6"));
    mark = 1;
    break;
  case UI_LINK_DONE:
    snprintf(line, sizeof line, L("Ligada: @%s, na conta %d. J\xc3\xa1 podes fechar o browser.",
                                  "Linked: @%s, as account %d. You can close the browser."),
             st->user, st->account);
    mark = 2;
    break;
  case UI_LINK_NO_NETWORK:
    snprintf(line, sizeof line, L("A PS5 n\xc3\xa3o est\xc3\xa1 na rede (0x%08X). Liga-a e abre este ecr\xc3\xa3 outra vez.",
                                  "The PS5 is not on the network (0x%08X). Connect it and open this screen again."),
             (unsigned)st->error);
    mark = 3;
    break;
  case UI_LINK_NO_HELPER:
    snprintf(line, sizeof line, "%s",
             L("O payload do XPS5 n\xc3\xa3o corre na PS5: precisa do carregador de ELF do etaHEN (porta 9021). "
               "Abre este ecr\xc3\xa3 outra vez.",
               "The XPS5 payload does not run on the PS5: it needs etaHEN's ELF loader (port 9021). "
               "Open this screen again."));
    mark = 3;
    break;
  default:
    mark = st->last == UI_LINK_FINE ? 1 : 3;
    if(st->last == UI_LINK_WRONG_CODE && st->renewed) {
      snprintf(line, sizeof line, "%s", L("Demasiados c\xc3\xb3" "digos errados: a PS5 mostra agora um novo.",
                                          "Too many wrong codes: the PS5 now shows a new one."));
    } else if(st->last == UI_LINK_WRONG_CODE) {
      snprintf(line, sizeof line, L("C\xc3\xb3" "digo errado (%d de %d). Escreve o que est\xc3\xa1 acima.",
                                    "Wrong code (%d of %d). Type the one above."), st->wrong, LINK_TRIES);
    } else if(st->last == UI_LINK_REFUSED && st->refused > 0) {
      snprintf(line, sizeof line, L("O X n\xc3\xa3o aceitou essa sess\xc3\xa3o (HTTP %d). Abre o x.com no PC e tenta outra vez.",
                                    "X did not accept that session (HTTP %d). Open x.com on the PC and try again."),
               st->refused);
    } else if(st->last == UI_LINK_REFUSED) {
      snprintf(line, sizeof line, "%s", L("A PS5 n\xc3\xa3o chegou ao X para confirmar a sess\xc3\xa3o. V\xc3\xaa a internet dela.",
                                          "The PS5 could not reach X to check the session. Check its internet."));
    } else if(st->last == UI_LINK_UNWRITTEN) {
      snprintf(line, sizeof line, "%s", L("A sess\xc3\xa3o n\xc3\xa3o se deixou gravar na PS5.",
                                          "The session could not be saved on the PS5."));
    } else if(st->seen) {
      snprintf(line, sizeof line, "%s", L("P\xc3\xa1gina aberta. Toca no bot\xc3\xa3o da extens\xc3\xa3o XPS5 Login e depois em Ligar.",
                                          "Page open. Tap the XPS5 Login extension's button, then Link."));
    } else {
      snprintf(line, sizeof line, "%s", L("\xc3\x80 espera do telem\xc3\xb3vel ou do PC. A PS5 s\xc3\xb3 ouve enquanto este ecr\xc3\xa3 est\xc3\xa1 aberto.",
                                          "Waiting for your phone or PC. The PS5 only listens while this screen is open."));
    }
    break;
  }
  if(mark == 1) {
    spinner(cx + 19, y + 19, 13, 4, frame, ACCENT);
    g_spinning = 1;
  } else if(mark == 2) {
    draw_circle(&g_dst, cx + 19, y + 19, 19, GREEN);
    draw_icon(&g_dst, ICON_CHECK, cx + 5, y + 5, 28, WHITE);
  } else {
    draw_circle(&g_dst, cx + 19, y + 19, 9, RED);
  }
  y = text_wrapped(body, cx + 58, y, 38, line, width - 58, mark >= 2 ? FG : GREY) + 20;

  // the account it goes to: ‹ Account 2 · new ›
  if(st->state == UI_LINK_DONE) {
    snprintf(line, sizeof line, L("Conta %d \xc2\xb7 @%s", "Account %d \xc2\xb7 @%s"), account, st->user);
  } else if((g_accounts >> (account - 1) & 1) && user[0]) {
    snprintf(line, sizeof line, L("Conta %d \xc2\xb7 fica no lugar de @%s", "Account %d \xc2\xb7 replaces @%s"),
             account, user);
  } else if(g_accounts >> (account - 1) & 1) {
    snprintf(line, sizeof line, L("Conta %d \xc2\xb7 fica no lugar da sess\xc3\xa3o que l\xc3\xa1 est\xc3\xa1",
                                  "Account %d \xc2\xb7 replaces the session there"), account);
  } else {
    snprintf(line, sizeof line, L("Conta %d \xc2\xb7 nova", "Account %d \xc2\xb7 new"), account);
  }
  draw_box(&g_dst, cx, y, width, 64, 32, 0, CHIP);
  lw = width_of(small, L("Na PS5", "On the PS5"));
  text(small, cx + 30, baseline(small, y, 64), L("Na PS5", "On the PS5"), GREY);
  text_fitted(value, cx + 30 + lw + 24, baseline(value, y, 64), line, width - lw - 24 - 30 - 120, FG);
  if(!busy) {
    over = zone(Z_L_PREV, 0, 0, cx + width - 128, y, 64, 64);
    draw_circle(&g_dst, cx + width - 96, y + 32, 24, CHIP);
    if(over) {
      lit_circle(cx + width - 96, y + 32, 24, 1);
    }
    text(value, cx + width - 96 - width_of(value, "\xe2\x80\xb9") / 2, baseline(value, y, 64), "\xe2\x80\xb9", FG);
    over = zone(Z_L_NEXT, 0, 0, cx + width - 64, y, 64, 64);
    draw_circle(&g_dst, cx + width - 34, y + 32, 24, CHIP);
    if(over) {
      lit_circle(cx + width - 34, y + 32, 24, 1);
    }
    text(value, cx + width - 34 - width_of(value, "\xe2\x80\xba") / 2, baseline(value, y, 64), "\xe2\x80\xba", FG);
  }

  // what to keep in mind
  text_wrapped(small, in, top + LK_H - 128, 34,
               L("N\xc3\xa3o termines a sess\xc3\xa3o do X nesse browser: isso fecha-a tamb\xc3\xa9m na PS5. "
                 "A sess\xc3\xa3o vai pela tua rede de casa e fica s\xc3\xb3 na PS5.",
                 "Do not sign out of X in that browser: that also signs the PS5 out. "
                 "The session goes over your home network and stays on the PS5."),
               LK_W - 104, DIM);

  draw_hints(x + LK_W / 2, top + LK_H - 56, busy ?
             L("\xe2\x97\x8b Fechar", "\xe2\x97\x8b Close") :
             L("\xe2\x86\x90 \xe2\x86\x92 Conta  \xc2\xb7  \xe2\x97\x8b Fechar", "\xe2\x86\x90 \xe2\x86\x92 Account  \xc2\xb7  \xe2\x97\x8b Close"));
}


/**
 * the question before a post is deleted, as x.com asks it
 **/
static void
draw_confirm(void) {
  text_font_t *title = font(F_TITLE), *body = font(F_SIDE), *pill = font(F_NAV_BOLD);
  int block = g_ask.what == M_BLOCK, unschedule = g_ask.what == M_UNSCHEDULE, logout = g_ask.what == M_LOGOUT;
  const char *yes = logout ? L("Terminar a sess\xc3\xa3o", "Log out") : block ? L("Bloquear", "Block") :
                    L("Apagar", "Delete"), *no = L("Cancelar", "Cancel");
  char whose[200];
  const char *why = logout && g_ask.user[0] ?
    (snprintf(whose, sizeof whose,
              L("O XPS5 apaga a sess\xc3\xa3o de @%s desta PS5. Podes voltar a entrar quando quiseres.",
                "XPS5 removes @%s's session from this PS5. You can log back in at any time."), g_ask.user),
     whose) : logout ?
    L("O XPS5 apaga a sess\xc3\xa3o desta conta da PS5. Podes voltar a entrar quando quiseres.",
      "XPS5 removes this account's session from this PS5. You can log back in at any time.") : unschedule ?
    L("O X deixa de o publicar e ele sai dos teus posts por enviar.",
      "X will not post it, and it leaves your unsent posts.") : block ?
    L("Essa conta deixa de te poder seguir, ver os teus posts e mandar-te mensagens, e os posts dela saem das "
      "tuas cronologias.",
      "They will not be able to follow you, see your posts or message you, and their posts leave your timelines.") :
    L("N\xc3\xa3o d\xc3\xa1 para desfazer. O post sai do teu perfil, das cronologias de quem te "
      "segue e dos resultados de pesquisa do X.",
      "This can't be undone. The post leaves your profile, the timelines of the accounts that "
      "follow you and X's search results.");
  const int w = 620, bh = 74, x = (UI_WIDTH - w) / 2;
  int lines = 0, h, y, by, over;
  char heading[160];

  // as high as what it says: the title, the lines, both buttons and the keys
  for(const char *at = why, *next; *at; at = next, lines++) {
    text_break(body, at, w - 104, &next);
  }
  h = 40 + 70 + 18 + lines * 40 + 40 + 2 * bh + 18 + 26 + 40 + 22;
  y = (UI_HEIGHT - h) / 2;
  draw_pane(x, y, w, h);
  if(logout) {
    snprintf(heading, sizeof heading, "%s", L("Terminar a sess\xc3\xa3o?", "Log out?"));
  } else if(block) {
    snprintf(heading, sizeof heading, L("Bloquear @%s?", "Block @%s?"), g_ask.user);
  } else if(unschedule) {
    snprintf(heading, sizeof heading, "%s", L("Apagar o post agendado?", "Delete scheduled post?"));
  } else {
    snprintf(heading, sizeof heading, "%s", L("Apagar o post?", "Delete post?"));
  }
  text_fitted(title, x + 52, baseline(title, y + 40, 70), heading, w - 104, FG);
  by = text_wrapped(body, x + 52, y + 40 + 70 + 18, 40, why, w - 104, GREY) + 40;

  over = zone(Z_Q_YES, 0, 0, x + 52, by, w - 104, bh);
  // logging out is not lost: white, as x.com has it, where deleting is red
  draw_box(&g_dst, x + 52, by, w - 104, bh, bh / 2.0f, 0,
           logout ? (over ? RGBA(224, 219, 214, 255) : WHITE) : over ? RGBA(255, 70, 80, 255) : RED);
  if(!g_ask.focus && !g_ptr.shown) {
    draw_box(&g_dst, x + 46, by - 6, w - 92, bh + 12, bh / 2.0f + 6, 3, logout ? ACCENT : WHITE);
  } else if(over) {
    lit_box(x + 52, by, w - 104, bh, bh / 2.0f, 0);
  }
  text(pill, x + (w - width_of(pill, yes)) / 2, baseline(pill, by, bh), yes, logout ? INK : WHITE);
  by += bh + 18;
  over = zone(Z_Q_NO, 0, 0, x + 52, by, w - 104, bh);
  draw_box(&g_dst, x + 52, by, w - 104, bh, bh / 2.0f, 0, CHIP);
  draw_box(&g_dst, x + 52, by, w - 104, bh, bh / 2.0f, 1.5f, RGBA(255, 255, 255, 70));
  if(over) {
    lit_box(x + 52, by, w - 104, bh, bh / 2.0f, 1);
  }
  if(g_ask.focus && !g_ptr.shown) {
    draw_box(&g_dst, x + 46, by - 6, w - 92, bh + 12, bh / 2.0f + 6, 3, WHITE);
  }
  text(pill, x + (w - width_of(pill, no)) / 2, baseline(pill, by, bh), no, FG);
  draw_hints(UI_WIDTH / 2, by + bh + 26,
             L("\xe2\x86\x91 \xe2\x86\x93 Escolher  \xc2\xb7  \xe2\x9c\x95 Carregar  \xc2\xb7  \xe2\x97\x8b Cancelar",
               "\xe2\x86\x91 \xe2\x86\x93 Choose  \xc2\xb7  \xe2\x9c\x95 Press  \xc2\xb7  \xe2\x97\x8b Cancel"));
}


/**
 * how long until a scheduled post goes out, in a few words
 **/
static void
until_text(long long when, long long now, char *buf, size_t size) {
  long long d = when - now;

  if(d <= 60) {
    snprintf(buf, size, "%s", L("A sair agora", "Going out now"));
  } else if(d < 3600) {
    snprintf(buf, size, L("Sai daqui a %lld min", "Goes out in %lld min"), d / 60);
  } else if(d < 2 * 86400) {
    snprintf(buf, size, L("Sai daqui a %lld h", "Goes out in %lld h"), d / 3600);
  } else {
    snprintf(buf, size, L("Sai daqui a %lld dias", "Goes out in %lld days"), d / 86400);
  }
}


#define SCHED_ROW_H 104

/**
 * the user's scheduled posts, as x.com's unsent posts list them: when each
 * goes out and its words; ✕ or the pointer takes one back, after a question
 **/
static void
draw_scheduled(long long now, unsigned frame) {
  text_font_t *title = font(F_BRAND), *small = font(F_SMALL), *bold = font(F_SIDE_BOLD), *f = font(F_LEGEND);
  const int x = S_X + 40, right = S_X + S_W - 48, top = S_Y + 140, rows = (S_H - 140 - 90) / SCHED_ROW_H;
  int y = S_Y + 30, first, over;
  char line[200];

  draw_pane(S_X, S_Y, S_W, S_H);
  draw_circle(&g_dst, x + 30, y + 32, 30, ACCENT);
  draw_icon(&g_dst, ICON_CLOCK, x + 12, y + 14, 36, WHITE);
  text(title, x + 84, y + 31, L("Posts agendados", "Scheduled posts"), FG);
  text(small, x + 84, y + 63, L("Os teus posts por enviar no X", "Your unsent posts on X"), GREY);
  if(round_button(Z_SCHED_CLOSE, S_X + S_W - 70, y + 32, ICON_CROSS)) {
    // the pointer is on it
  }

  if(g_sched.loading && !g_sched.count) {
    spinner(S_X + S_W / 2.0f, S_Y + S_H / 2.0f - 30, 30, 6, frame, ACCENT);
    g_spinning = 1;
    snprintf(line, sizeof line, "%s", L("A ler os teus posts agendados\xe2\x80\xa6", "Reading your scheduled posts\xe2\x80\xa6"));
    text(f, S_X + (S_W - width_of(f, line)) / 2, S_Y + S_H / 2 + 50, line, GREY);
  } else if(!g_sched.count) {
    draw_icon(&g_dst, ICON_CLOCK, S_X + S_W / 2 - 40, S_Y + S_H / 2 - 130, 80, DIM);
    snprintf(line, sizeof line, "%s", g_sched.error ? L("Os posts agendados n\xc3\xa3o vieram", "The scheduled posts did not come") :
             L("N\xc3\xa3o tens posts agendados", "You have no scheduled posts"));
    text(font(F_HEADING), S_X + (S_W - width_of(font(F_HEADING), line)) / 2, S_Y + S_H / 2 + 10, line, FG);
    snprintf(line, sizeof line, "%s", g_sched.error ?
             L("Carrega em \xe2\x96\xa1 para tentar outra vez.", "Press \xe2\x96\xa1 to try again.") :
             L("No compositor, L1 e R1 escolhem quando um post sai.", "In the composer, L1 and R1 pick when a post goes out."));
    text(small, S_X + (S_W - width_of(small, line)) / 2, S_Y + S_H / 2 + 56, line, GREY);
  } else {
    first = g_sched.sel >= rows ? g_sched.sel - rows + 1 : 0;
    for(int i=first; i < g_sched.count && i < first + rows; i++) {
      const x_post_t *p = &g_sched.items[i];
      const int ry = top + (i - first) * SCHED_ROW_H, cy = ry + (SCHED_ROW_H - 8) / 2;
      const int focused = i == g_sched.sel && !g_ptr.shown;
      over = zone(Z_SCHED_ROW, i, 0, S_X + 20, ry, S_W - 40, SCHED_ROW_H - 8);
      if(focused) {
        draw_box(&g_dst, S_X + 20, ry, S_W - 40, SCHED_ROW_H - 8, 26, 0, CHIP_ON);
      } else if(over) {
        lit_box(S_X + 20, ry, S_W - 40, SCHED_ROW_H - 8, 26, 1);
      }
      draw_circle(&g_dst, x + 30, cy, 28, p->icon ? RGBA(244, 33, 46, 60) : CHIP);
      draw_icon(&g_dst, ICON_CLOCK, x + 13, cy - 17, 34, p->icon ? RED : ACCENT);
      if(p->icon) {
        snprintf(line, sizeof line, "%s", L("N\xc3\xa3o saiu: o X n\xc3\xa3o o conseguiu publicar", "Not sent: X could not post it"));
      } else {
        until_text(p->time, now, line, sizeof line);
      }
      text_fitted(bold, x + 84, cy - 8, line, right - x - 84 - 140, p->icon ? RED : FG);
      text_fitted(small, x + 84, cy + 28, p->text[0] ? p->text : L("(s\xc3\xb3 multim\xc3\xa9" "dia)", "(media only)"),
                  right - x - 84, GREY);
      if(focused || over) {
        draw_icon(&g_dst, ICON_TRASH, right - 34, cy - 17, 34, RED);
      }
    }
    if(g_sched.loading) {
      spinner(S_X + S_W - 150, S_Y + 62, 13, 4, frame, ACCENT);
      g_spinning = 1;
    }
  }
  draw_hints(S_X + S_W / 2, S_Y + S_H - 56,
             L("\xe2\x86\x91 \xe2\x86\x93 Escolher  \xc2\xb7  \xe2\x9c\x95 Apagar  \xc2\xb7  \xe2\x96\xa1 Atualizar  \xc2\xb7  \xe2\x97\x8b Fechar",
               "\xe2\x86\x91 \xe2\x86\x93 Choose  \xc2\xb7  \xe2\x9c\x95 Delete  \xc2\xb7  \xe2\x96\xa1 Refresh  \xc2\xb7  \xe2\x97\x8b Close"));
}


static void
draw_sheet(long long now, unsigned frame) {
  switch(g_sheet) {
  case SHEET_SCHEDULED:
    draw_scheduled(now, frame);
    break;
  case SHEET_MENU:
    draw_menu();
    break;
  case SHEET_CONFIRM:
    draw_confirm();
    break;
  case SHEET_COMPOSE:
    draw_compose(now, frame);
    break;
  case SHEET_GALLERY:
    draw_gallery(now, frame);
    break;
  case SHEET_SETTINGS:
    draw_settings();
    break;
  case SHEET_LINK:
    draw_link(frame);
    break;
  case SHEET_READER:
    draw_reader();
    break;
  case SHEET_LOGIN:
    draw_login(frame);
    break;
  }
}


/*
 * The backdrop: a room seen through frosted glass, and over it the banner
 * shown (a profile's, or else the signed-in account's) as an album's art
 * fills a player behind it: blurred into its colours from the bar on the
 * left to the side column. The room is the warm one of the day or the dark
 * one of the night (the theme). It is worked out on a point every BG_STEP
 * pixels, goes over to a new one gently, and is drawn into g_backdrop
 * through the glass and its shadows, which are worked out once.
 */

typedef struct glow {
  float x, y, r;
  float rgb[3];
} glow_t;


static float
smooth(float t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}


#define BG_STEP 16                         // pixels between the backdrop's points
#define BG_W    (UI_WIDTH / BG_STEP + 1)
#define BG_H    (UI_HEIGHT / BG_STEP + 2)
#define BG_BLUR 5                          // points each side a banner is blurred over, three times
#define BG_EASE 0.1f                       // how much nearer each frame takes it

static const unsigned char g_bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

// the backdrop's points as drawn and where they are going; what they go to
// (the theme, a profile or not, the banner's address); and for each pixel
// how much of the room shows through (255 all of it), how much glass is
// over it and how much of the glass's white lines
static float          g_bg[BG_H][BG_W][3], g_bg_to[BG_H][BG_W][3];
static int            g_bg_moving, g_bg_dirty, g_bg_theme = -1, g_bg_profile;
static char           g_bg_url[IMAGES_URL_MAX];
static unsigned char *g_bg_mul, *g_bg_glass, *g_bg_line;


/**
 * the room behind the glass at (x, y), 0 to 255 a channel: of the day,
 * warm and lit from the top left; of the night, nearly black
 **/
static void
room(int night, float x, float y, float rgb[3]) {
  static const glow_t glows[2][4] = {
    {
      {1760, 160, 760, {96, 80, 70}},
      {200, 1040, 720, {30, 72, 90}},    // a cool blue low on the left
      {1840, 1010, 660, {54, 82, 42}},   // green on the right
      {90, 110, 1000, {150, 96, 52}},    // the warm light from the top left
    }, {
      {1760, 160, 760, {40, 40, 54}},
      {200, 1040, 720, {10, 28, 46}},
      {1840, 1010, 660, {16, 28, 18}},
      {90, 110, 1000, {50, 44, 48}},     // a lamp left on
    },
  };
  static const float top[2][3] = {{92, 80, 71}, {36, 37, 44}}, bottom[2][3] = {{56, 47, 42}, {15, 15, 18}};
  float k = 0.62f * y / UI_HEIGHT + 0.38f * x / UI_WIDTH, g, dx, dy;

  for(int c=0; c<3; c++) {
    rgb[c] = top[night][c] + (bottom[night][c] - top[night][c]) * k;
  }
  for(int i=0; i<4; i++) {
    dx = x - glows[night][i].x;
    dy = y - glows[night][i].y;
    g = smooth(1 - sqrtf(dx * dx + dy * dy) / glows[night][i].r) * 0.36f;
    for(int c=0; c<3; c++) {
      rgb[c] += glows[night][i].rgb[c] * g;
    }
  }
}


/**
 * from blurred into to along the rows (dx 1) or the columns (dy 1)
 **/
static void
blur(float from[BG_H][BG_W][3], float to[BG_H][BG_W][3], int dx, int dy) {
  for(int j=0; j < BG_H; j++) {
    for(int i=0; i < BG_W; i++) {
      float sum[3] = {0, 0, 0};
      for(int k=-BG_BLUR; k <= BG_BLUR; k++) {
        int jj = j + k * dy, ii = i + k * dx;
        jj = jj < 0 ? 0 : jj >= BG_H ? BG_H - 1 : jj;
        ii = ii < 0 ? 0 : ii >= BG_W ? BG_W - 1 : ii;
        for(int c=0; c<3; c++) {
          sum[c] += from[jj][ii][c];
        }
      }
      for(int c=0; c<3; c++) {
        to[j][i][c] = sum[c] / (2 * BG_BLUR + 1);
      }
    }
  }
}


/**
 * a banner of w x h pixels on the backdrop's points, as an album's art
 * behind its player: stretched over the screen, melted into its colours,
 * a little livelier, darker going down, and never brighter than cap, so
 * the white words over it keep standing out
 **/
static void
banner_art(const uint32_t *px, int w, int h, float cap, float art[BG_H][BG_W][3]) {
  static float tmp[BG_H][BG_W][3];
  const int half_w = w / (2 * (BG_W - 1)) + 1, half_h = h / (2 * (BG_H - 2)) + 1;
  const float knee = cap * 0.55f;

  // each point: the mean of the banner around where it falls
  for(int j=0; j < BG_H; j++) {
    int cy = (int)((long long)j * BG_STEP * h / UI_HEIGHT);
    int y0 = cy - half_h, y1 = cy + half_h;
    y0 = y0 < 0 ? 0 : y0 >= h ? h - 1 : y0;
    y1 = y1 > h ? h : y1 <= y0 ? y0 + 1 : y1;
    for(int i=0; i < BG_W; i++) {
      int cx = (int)((long long)i * BG_STEP * w / UI_WIDTH);
      int x0 = cx - half_w, x1 = cx + half_w, n = 0;
      float sum[3] = {0, 0, 0};
      x0 = x0 < 0 ? 0 : x0 >= w ? w - 1 : x0;
      x1 = x1 > w ? w : x1 <= x0 ? x0 + 1 : x1;
      for(int y=y0; y < y1; y++) {
        for(int x=x0; x < x1; x++, n++) {
          uint32_t p = px[(size_t)y * w + x];
          sum[0] += p & 0xff;
          sum[1] += p >> 8 & 0xff;
          sum[2] += p >> 16 & 0xff;
        }
      }
      for(int c=0; c<3; c++) {
        art[j][i][c] = sum[c] / n;
      }
    }
  }
  for(int pass=0; pass<3; pass++) {
    blur(art, tmp, 1, 0);
    blur(tmp, art, 0, 1);
  }
  for(int j=0; j < BG_H; j++) {
    for(int i=0; i < BG_W; i++) {
      float *c = art[j][i], mean = (c[0] + c[1] + c[2]) / 3, luma, aim;
      for(int k=0; k<3; k++) {
        c[k] = mean + (c[k] - mean) * 1.3f;
        c[k] = c[k] < 0 ? 0 : c[k] > 255 ? 255 : c[k];
      }
      luma = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
      aim = luma * (1 - 0.22f * j / (BG_H - 1));
      if(aim > knee) {
        aim = knee + (cap - knee) * (1 - expf(-(aim - knee) / (cap - knee)));
      }
      for(int k=0; k<3; k++) {
        c[k] = luma > 0.5f ? c[k] * aim / luma : 0;
        c[k] = c[k] > 255 ? 255 : c[k];
      }
    }
  }
}


#define LUMA(c) (0.2126f * (c)[0] + 0.7152f * (c)[1] + 0.0722f * (c)[2])

/**
 * the room in the banner's colours, as a room lit through it: each point
 * keeps the room's light (its lamp, its glows, its dark corners), a little
 * of the banner's own light and shade, and takes the banner's colour there;
 * k of the way. vivid is how strong the colour comes out on the whole: a
 * banner of soft colours (most photos) is pushed up to it, three times at
 * most, so a grey one stays grey, and a loud one is calmed down to it.
 **/
static void
tint_room(float art[BG_H][BG_W][3], float k, float vivid) {
  float mean = 0, strength = 0, boost, lr, la, light, n, v, d;

  for(int j=0; j < BG_H; j++) {
    for(int i=0; i < BG_W; i++) {
      float *c = art[j][i], most = 0;
      la = LUMA(c);
      mean += la;
      for(int ch=0; ch<3 && la > 4; ch++) {
        d = c[ch] / la - 1;
        d = d < 0 ? -d : d;
        most = d > most ? d : most;
      }
      strength += most;
    }
  }
  mean /= BG_W * BG_H;
  mean = mean < 8 ? 8 : mean;
  strength /= BG_W * BG_H;
  boost = strength > 0.01f ? vivid / strength : 1;
  boost = boost < 0.5f ? 0.5f : boost > 3 ? 3 : boost;
  for(int j=0; j < BG_H; j++) {
    for(int i=0; i < BG_W; i++) {
      float *to = g_bg_to[j][i], *c = art[j][i];
      lr = LUMA(to);
      la = LUMA(c);
      light = lr * (1 + 0.5f * (la / mean - 1));
      light = light < lr * 0.5f ? lr * 0.5f : light > lr * 1.6f ? lr * 1.6f : light;
      for(int ch=0; ch<3; ch++) {
        n = la > 4 ? c[ch] / la : 1;
        v = (1 + (n - 1) * boost) * light;
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        to[ch] += (v - to[ch]) * k;
      }
    }
  }
}


/**
 * the pointer's colour for a banner, into out: the hue its colourful parts
 * share, vivid and light enough to stand out as the accent does, with
 * out[3] 1; or for a grey banner, the accent's, with out[3] 0
 **/
static void
pointer_hue(float art[BG_H][BG_W][3], float out[4]) {
  float sum[3] = {0, 0, 0}, weight = 0, top, low, s;

  for(int j=0; j < BG_H; j++) {
    for(int i=0; i < BG_W; i++) {
      const float *c = art[j][i];
      top = fmaxf(c[0], fmaxf(c[1], c[2]));
      low = fminf(c[0], fminf(c[1], c[2]));
      // the more colourful a part, the more it counts
      for(int ch=0; ch<3; ch++) {
        sum[ch] += c[ch] * (top - low) * (top - low);
      }
      weight += (top - low) * (top - low);
    }
  }
  out[0] = 242;
  out[1] = 122;
  out[2] = 26;
  out[3] = 0;
  if(weight < 8.0f * 8 * BG_W * BG_H) {
    return; // greys, with hardly a colour among them
  }
  for(int ch=0; ch<3; ch++) {
    sum[ch] /= weight;
  }
  top = fmaxf(sum[0], fmaxf(sum[1], sum[2]));
  low = fminf(sum[0], fminf(sum[1], sum[2]));
  if(top - low < 1) {
    return;
  }
  // its hue, at a saturation of 0.55 to 0.9 and almost full light
  s = (top - low) / top;
  s = s < 0.55f ? 0.55f : s > 0.9f ? 0.9f : s;
  for(int ch=0; ch<3; ch++) {
    out[ch] = 250 * (1 - s * (top - sum[ch]) / (top - low));
  }
  out[3] = 1;
}


/**
 * where the backdrop goes for the tab t: the theme's room, coloured by the
 * banner of the profile shown or, fainter, the signed-in account's; as it
 * is while a profile or a banner is on its way. The first one is there at
 * once. The pointer takes the banner's colour along with it.
 **/
static void
backdrop_aim(const tab_t *t) {
  static float art[BG_H][BG_W][3];
  const int night = NIGHT, profile = t->has_user;
  const tab_t *me = &g_tabs[UI_ME];
  const x_user_t *u = profile ? (t->user.banner[0] ? &t->user : NULL) :
                      me->has_user && me->user.banner[0] ? &me->user : NULL;
  const uint32_t *px = NULL;
  char buf[IMAGES_URL_MAX] = "";

  if(!g_bg_mul) {
    return;
  }
  if(u) {
    banner_url(u->banner, buf, sizeof buf);
    if(!(px=picture(buf, MAIN_W, BANNER_H))) {
      if(g_bg_theme == night) {
        return; // the banner is on its way
      }
      buf[0] = 0;
    }
  } else if(profile && t->feed == UI_USER && t->loading && g_bg_theme == night) {
    return; // the profile is still coming, and maybe a banner with it
  }
  if(g_bg_theme == night && g_bg_profile == profile && !strcmp(buf, g_bg_url)) {
    return;
  }
  for(int j=0; j < BG_H; j++) {
    for(int i=0; i < BG_W; i++) {
      room(night, (float)(i * BG_STEP), (float)(j * BG_STEP), g_bg_to[j][i]);
    }
  }
  g_ptr_to[0] = 242;
  g_ptr_to[1] = 122;
  g_ptr_to[2] = 26;
  g_ptr_to[3] = 0;
  if(px) {
    // a profile's banner all the way and strong, so going into a profile
    // (the user's own too) shows; the account's, over the other lists, less
    banner_art(px, pxs(MAIN_W), pxs(BANNER_H), 200, art);
    pointer_hue(art, g_ptr_to);
    tint_room(art, profile ? 1 : 0.45f, profile ? 0.6f : 0.3f);
  }
  if(g_bg_theme < 0) {
    memcpy(g_bg, g_bg_to, sizeof g_bg);
    memcpy(g_ptr_col, g_ptr_to, sizeof g_ptr_col);
    g_bg_dirty = 1;
  } else {
    g_bg_moving = 1;
  }
  g_bg_theme = night;
  g_bg_profile = profile;
  snprintf(g_bg_url, sizeof g_bg_url, "%s", buf);
}


/**
 * the backdrop a step nearer where it is going; 1 while it moves
 **/
static int
backdrop_move(void) {
  float most = 0, d;

  if(!g_bg_moving) {
    return 0;
  }
  for(int j=0; j < BG_H; j++) {
    for(int i=0; i < BG_W; i++) {
      for(int c=0; c<3; c++) {
        d = g_bg_to[j][i][c] - g_bg[j][i][c];
        g_bg[j][i][c] += d * BG_EASE;
        d = d < 0 ? -d : d;
        most = d > most ? d : most;
      }
    }
  }
  for(int c=0; c<4; c++) {
    d = g_ptr_to[c] - g_ptr_col[c];
    g_ptr_col[c] += d * BG_EASE;
    d = d < 0 ? -d : d;
    most = c < 3 && d > most ? d : most;
  }
  if(most < 0.5f) {
    memcpy(g_bg, g_bg_to, sizeof g_bg);
    memcpy(g_ptr_col, g_ptr_to, sizeof g_ptr_col);
    g_bg_moving = 0;
  }
  g_bg_dirty = 1;
  return 1;
}


/**
 * g_backdrop from its points: each pixel what shows of the room past the
 * shadows and through the glass, plus the glass's tint and its sheen at
 * the top, then the lines,
 * in fixed point (16ths) with a dither against bands
 **/
// which of the points each pixel's column lies after, and how far on, in
// 256ths (at scale 1, x / BG_STEP and x % BG_STEP)
static short         g_bg_col_i[4096];
static unsigned char g_bg_col_f[4096];

static void
backdrop_rows(void *arg, int part, int parts) {
  // what the glass adds of its own (a fifth of tints), at its top more,
  // and how much of the room it lets through
  static const float tints[2][3] = {{176, 160, 148}, {58, 58, 68}}, sheens[2] = {16, 8}, sees[2] = {0.64f, 0.72f};
  const short *col_i = g_bg_col_i;
  const unsigned char *col_f = g_bg_col_f;
  const int night = g_bg_theme == SETTINGS_NIGHT, see = (int)(sees[night] * 257 + 0.5f);
  const int pw = pxs(UI_WIDTH), ph = pxs(UI_HEIGHT);
  int row[BG_W][3], from, to;

  (void)arg;
  workers_rows(ph, part, parts, &from, &to);
  for(int y=from; y < to; y++) {
    const float v = y / g_scale;
    const int j = (int)(v / BG_STEP);
    const float fy = v - j * BG_STEP;
    const float sheen = sheens[night] * smooth(1 - (v - PANEL_Y) / 420);
    const size_t at = (size_t)y * pw;
    const unsigned char *mul = g_bg_mul + at, *glass = g_bg_glass + at, *line = g_bg_line + at;
    const unsigned char *dither = g_bayer + (y & 3) * 4;
    uint32_t *out = g_backdrop + at;
    int k[3];

    for(int c=0; c<3; c++) {
      k[c] = (int)((tints[night][c] * 0.2f + sheen) * 16 + 0.5f);
    }
    for(int i=0; i < BG_W; i++) {
      for(int c=0; c<3; c++) {
        row[i][c] = (int)(g_bg[j][i][c] * (BG_STEP - fy) + g_bg[j + 1][i][c] * fy + 0.5f);
      }
    }
    for(int x=0; x < pw; x++) {
      const int i = col_i[x], f = col_f[x], a = mul[x] * 257 + glass[x] * see, g = glass[x] * 257;
      const int l = line[x];
      int v[3];
      for(int c=0; c<3; c++) {
        int s = row[i][c] + ((row[i + 1][c] - row[i][c]) * f >> 8);
        int o = (((s < 0 ? 0 : s) * a + g * k[c]) >> 16) + dither[x & 3];
        o >>= 4;
        o = o > 255 ? 255 : o;
        v[c] = o + ((255 - o) * l >> 8);
      }
      out[x] = RGBA(v[0], v[1], v[2], 255);
    }
  }
}


static void
backdrop_draw(void) {
  const int pw = pxs(UI_WIDTH);

  g_bg_dirty = 0;
  if(!g_backdrop || !g_bg_mul || pw > 4096) {
    return;
  }
  for(int x=0; x < pw; x++) {
    const float u = x / g_scale;
    const int i = (int)(u / BG_STEP);
    g_bg_col_i[x] = (short)i;
    g_bg_col_f[x] = (unsigned char)((u - i * BG_STEP) * 256 / BG_STEP);
  }
  workers_run(backdrop_rows, NULL);
}


/**
 * how much of the pixel (x, y) a shape covers, and how dark its shadow,
 * dropped drop pixels and spread over spread, makes it
 **/
static void
cover(float x, float y, int bx, int by, int bw, int bh, float r, float drop, float spread, float dark,
      float *glass, float *shade) {
  float d = draw_distance(x, y, bx, by, bw, bh, r);
  float s = draw_distance(x, y - drop, bx, by, bw, bh, r);
  float c = 0.5f - d * g_scale; // the glass's edge over a pixel

  c = c < 0 ? 0 : c > 1 ? 1 : c;
  *glass = c > *glass ? c : *glass;
  if(s > 0) {
    s = smooth(1 - s / spread) * dark;
    *shade = s > *shade ? s : *shade;
  } else {
    *shade = dark > *shade ? dark : *shade;
  }
}


/**
 * what the backdrop is drawn through, once: the panel's glass and the
 * shadows it drops on the room (a wide soft one, as from a lamp above, and
 * a close one where it nearly touches), and the glass's rim, lit from
 * above, and the lines in the panel, white over black first
 **/
static void
paint_rows(void *arg, int part, int parts) {
  const int pw = pxs(UI_WIDTH), ph = pxs(UI_HEIGHT);
  float glass, wide, close, shade, x, y;
  int from, to;

  (void)arg;
  workers_rows(ph, part, parts, &from, &to);
  for(int py=from; py < to; py++) {
    for(int px=0; px < pw; px++) {
      size_t at = (size_t)py * pw + px;
      x = (px + 0.5f) / g_scale; // the point in the pixel's middle
      y = (py + 0.5f) / g_scale;
      glass = wide = close = 0;
      cover(x, y, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, PANEL_R, 80, 190, 0.7f, &glass, &wide);
      cover(x, y, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, PANEL_R, 12, 40, 0.5f, &glass, &close);
      shade = 1 - (1 - wide) * (1 - close);
      g_bg_mul[at] = (unsigned char)((1 - shade) * (1 - glass) * 255 + 0.5f);
      g_bg_glass[at] = (unsigned char)(glass * 255 + 0.5f);
    }
  }
}


static void
paint_backdrop(void) {
  const int pw = pxs(UI_WIDTH), ph = pxs(UI_HEIGHT);
  const size_t count = (size_t)pw * ph;
  text_surface_t s = {NULL, UI_WIDTH, UI_HEIGHT, g_scale, 0};

  if(!(g_backdrop=mem_alloc(count * sizeof *g_backdrop))) {
    return; // ui_draw paints it flat on every frame
  }
  if(!(g_bg_mul=mem_alloc(count * 3))) {
    mem_free(g_backdrop);
    g_backdrop = NULL;
    return;
  }
  g_bg_glass = g_bg_mul + count;
  g_bg_line = g_bg_glass + count;
  workers_run(paint_rows, NULL);
  memset(g_backdrop, 0, count * sizeof *g_backdrop);
  s.pixels = g_backdrop;
  draw_rim(&s, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, PANEL_R, 2, RGBA(255, 255, 255, 120), 0.28f);
  draw_rect(&s, DIV_X, PANEL_Y + 34, 2, PANEL_H - 68, RGBA(255, 255, 255, 26));
  draw_rect(&s, MAIN_X, LIST_TOP - 2, MAIN_W, 2, RGBA(255, 255, 255, 22));
  draw_rect(&s, DIV_X + 2, SIDE_KEYS_Y - 22, PANEL_X + PANEL_W - DIV_X - 3, 2, RGBA(255, 255, 255, 22));
  for(size_t i=0; i < count; i++) {
    g_bg_line[i] = (unsigned char)(g_backdrop[i] & 0xff);
  }
  memset(g_backdrop, 0, count * sizeof *g_backdrop);
}


void
ui_draw(const text_surface_t *dst, long long now, unsigned frame) {
  const tab_t *t = view();
  const size_t bytes = (size_t)pxs(UI_WIDTH) * pxs(UI_HEIGHT) * sizeof *dst->pixels;
  const int whole = dst->width == UI_WIDTH && dst->height == UI_HEIGHT && text_scale(dst) == g_scale;
  int keep;

  g_dst = *dst;
  g_now = now;
  g_zone_count = 0;
  g_player_wanted = 0;
  g_mini.here = 0;
  g_frame_no = frame;
  g_spinning = 0;
  images_frame();
  backdrop_aim(t);
  if(g_bg_dirty) {
    backdrop_draw();
    g_under_stale = 1;
  }
  // under a sheet, what was drawn there before while nothing there changed
  // (the ages too, every half a minute)
  keep = whole && g_sheet && !g_viewer.open;
  if(keep && !g_under && !(g_under=mem_alloc(bytes))) {
    keep = 0;
  }
  g_under_used = keep && !g_under_stale && g_under_sheet == g_sheet && g_under_scroll == t->scroll &&
                 g_under_theme == NIGHT && g_under_now / 30 == now / 30;
  g_under_fresh = 0;
  if(g_under_used) {
    screen_copy(dst->pixels, g_under, bytes);
  } else {
    if(g_backdrop && whole) {
      screen_copy(dst->pixels, g_backdrop, bytes);
    } else {
      draw_rect(&g_dst, 0, 0, g_dst.width, g_dst.height, FLAT);
      draw_box(&g_dst, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, PANEL_R, 0, RGBA(255, 255, 255, 22));
    }
    draw_side(t, frame);
    draw_timeline(t, now, frame);
    draw_mini(t);
    draw_top(t, now, frame);
    draw_nav();
    if(g_viewer.open) {
      draw_viewer(frame);
    }
    if(g_sheet && g_sheet != SHEET_MENU) {
      draw_rect(&g_dst, 0, 0, UI_WIDTH, UI_HEIGHT, SCRIM); // a menu leaves the screen as it is
    }
    if(keep) {
      screen_copy(g_under, dst->pixels, bytes);
      g_under_fresh = 1;
      g_under_stale = 0;
      g_under_sheet = g_sheet;
      g_under_scroll = t->scroll;
      g_under_theme = NIGHT;
      g_under_now = now;
      g_under_pane.w = 0;
    }
  }
  if(g_sheet) {
    draw_sheet(now, frame);
  }
  g_under_fresh = 0;
  if((g_sheet || g_viewer.open) && g_toast.frames > 0) {
    draw_snackbar();
  }
  if(g_ptr.shown) {
    draw_pointer();
  }
  hover_check();
  // nothing shows the video any more: it stops
  if(!g_player_wanted) {
    player_close();
  }
}
