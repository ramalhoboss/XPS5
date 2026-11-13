/* The link between XPS5's title and its login engine (tools/login-engine):
X's login page, which the engine opens in its own WebKit, shown in the
title's login window, and what the user does there sent back to it.

A title may start payloads but not listen on a port (its bind gives EACCES),
so it goes like this:
1. the title sends the engine's launcher (LOGIN_LAUNCHER) to the ELF loader
   on 127.0.0.1:9021 and, on the same connection, a line with a key it just
   made, "key <64 hex digits>\n", then closes its side of it (the loader
   reads only the ELF: what follows is the payload's stdin);
2. the launcher starts the engine's window process, which listens on
   127.0.0.1 at the first free port from LOGIN_PORT_FIRST and says which on
   the loader's connection, "xps5-login port <n>\n", or why not,
   "xps5-login error <n>\n" (LOGIN_END_*);
3. the title connects there and sends LOGIN_HELLO with that key; the engine
   takes that one connection and no other, and ends when it closes.

Then each message is a login_msg_t and size bytes after it. Both ends run on
the PS5 (x86-64), so the numbers are little-endian, as the structures are.
Nothing of the page (its text, its fields' values) is logged at either end.

Once X's page has signed in, the engine reads X's two cookies, auth_token
and ct0, from its own cookie jar and sends them (LOGIN_SESSION); the title
tries them on X, keeps them in the account the window signs in and closes
the window, and the engine, closing, has its web data deleted. Neither end
logs them. */

#pragma once

#include <stdint.h>

#define LOGIN_ENGINE_DIR "/data/TwitterPS5/engine"
#define LOGIN_LAUNCHER   LOGIN_ENGINE_DIR "/xps5-login.elf"
#define LOGIN_MAGIC      0x4c355058u // "XP5L"
#define LOGIN_VERSION    1
#define LOGIN_KEY_BYTES  32
#define LOGIN_PORT_FIRST 9073
#define LOGIN_PORT_LAST  9080
#define LOGIN_SIDE_MAX   4096 // a page's width or height in pixels, at most
#define LOGIN_TEXT_MAX   1024 // bytes of a LOGIN_TEXT or of a LOGIN_FIELD's text
#define LOGIN_SESSION_MAX 512 // bytes of a LOGIN_SESSION

typedef struct login_msg {
  uint16_t type; // LOGIN_*
  uint16_t arg;  // as the type says
  uint32_t size; // of what follows
} login_msg_t;

// from the title
enum {
  LOGIN_HELLO = 1, // login_hello_t, first
  LOGIN_POINTER,   // arg LOGIN_MOVE, LOGIN_DOWN or LOGIN_UP; login_point_t
  LOGIN_WHEEL,     // login_wheel_t
  LOGIN_KEY,       // arg LOGIN_KEY_*
  LOGIN_TEXT,      // arg 1: in place of the field's text; the UTF-8 follows
  LOGIN_ASK_FIELD, // which field has the focus: a LOGIN_FIELD comes back
  LOGIN_RELOAD,    // the login page again, from its start
  LOGIN_BYE,       // the window closed
};

// from the engine
enum {
  LOGIN_READY = 64, // login_ready_t: the page's size; its frames follow
  LOGIN_FRAME,      // login_rect_t, then w x h pixels 0xAABBGGRR, row after row
  LOGIN_STATE,      // login_state_t, when it changes
  LOGIN_FIELD,      // arg LOGIN_FIELD_* (| LOGIN_FIELD_ASKED); the field's text
                    // follows, none for a password
  LOGIN_END,        // arg LOGIN_END_*: the engine stops
  LOGIN_SESSION,    // X's session once the page signed in: "auth_token=<hex>\nct0=<hex>\n",
                    // LOGIN_SESSION_MAX bytes at most; once for each sign-in
};

enum { LOGIN_MOVE, LOGIN_DOWN, LOGIN_UP };

enum {
  LOGIN_KEY_RETURN = 1,
  LOGIN_KEY_BACKSPACE,
  LOGIN_KEY_TAB,
  LOGIN_KEY_BACKTAB, // shift and tab: the field before
  LOGIN_KEY_ESCAPE,
};

enum {
  LOGIN_FIELD_NONE,
  LOGIN_FIELD_TEXT,
  LOGIN_FIELD_PASSWORD,
  LOGIN_FIELD_EMAIL,
  LOGIN_FIELD_NUMBER, // a number or a telephone's: a code from a text message
  LOGIN_FIELD_EDIT,   // other text in the page that takes typing
};
#define LOGIN_FIELD_ASKED 0x100 // with the kind: the title asked (LOGIN_ASK_FIELD), not a click

enum {
  LOGIN_END_CLOSED,   // the title closed the window
  LOGIN_END_NO_FILES, // the engine is not all there
  LOGIN_END_NO_START, // a process of it did not start
  LOGIN_END_NO_PORT,  // no port of LOGIN_PORT_FIRST to LOGIN_PORT_LAST was free
  LOGIN_END_NO_TITLE, // the title did not come, or not with its key
  LOGIN_END_CRASHED,  // the window process ended on its own
  LOGIN_END_TIME,     // open too long (the launcher's limit)
};

typedef struct login_hello {
  uint32_t magic;   // LOGIN_MAGIC
  uint16_t version; // LOGIN_VERSION
  uint16_t zoom;    // the page's zoom in percent: 150 draws a CSS pixel 1.5 pixels wide
  uint16_t width;   // the page's size in pixels
  uint16_t height;
  char     lang[8]; // the language the page is asked for ("en", "pt"), 0-ended
  uint8_t  key[LOGIN_KEY_BYTES];
} login_hello_t;

typedef struct login_point {
  int32_t x, y; // in the page's pixels
} login_point_t;

typedef struct login_wheel {
  int32_t x, y;   // where, in the page's pixels
  int32_t dx, dy; // how far, in pixels: dy above 0 goes down the page
} login_wheel_t;

typedef struct login_ready {
  uint16_t width, height;
} login_ready_t;

typedef struct login_rect {
  uint16_t x, y, w, h;
} login_rect_t;

typedef struct login_state {
  uint8_t loading;  // 1 while a page loads
  uint8_t progress; // how far, 0 to 100
  uint8_t on_x;     // the page is X's, over HTTPS
  uint8_t crashed;  // the page's process ended: LOGIN_RELOAD starts it again
  int32_t error;    // the last load's WebKit error code, or 0
  int32_t blocked;  // pages outside X the window did not open
} login_state_t;

#ifdef __cplusplus
static_assert(sizeof(login_msg_t) == 8 && sizeof(login_hello_t) == 52 && sizeof(login_state_t) == 12,
              "the messages' layout");
#else
_Static_assert(sizeof(login_msg_t) == 8 && sizeof(login_hello_t) == 52 && sizeof(login_state_t) == 12,
               "the messages' layout");
#endif
