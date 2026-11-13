/* The title's side of the login window (src/loginproto.h): a thread that
starts the login engine through the ELF loader and meets it on 127.0.0.1,
then keeps the page's pixels as they come, how the page goes and the fields
it says are in focus, for main.c, which hands them to the screen
(ui_login_*); what the controller does goes to the engine from main.c's
thread. The sockets go through login_net_t: the PS5's sceNet in main.c, the
computer's in tests/login_test.c. */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct login_net {
  int  (*connect)(int port, int seconds);              // to 127.0.0.1:port, or below 0
  int  (*send)(int s, const void *buf, size_t len);    // what went, or 0 or below on an error
  int  (*recv)(int s, void *buf, size_t len);          // what came; 0 at the end, below 0 on an error
  void (*shut)(int s, int both);                       // its sending side, or both sides
  void (*close)(int s);
  void (*random)(void *buf, size_t len);               // bytes nobody can guess
  void (*log)(const char *fmt, ...);                   // codes and counts only
} login_net_t;

// how it goes; why (LOGIN_LINK_*) once it failed
enum { LOGIN_LINK_IDLE, LOGIN_LINK_STARTING, LOGIN_LINK_SHOWN, LOGIN_LINK_FAILED };
// NO_ENGINE: the launcher is not there (code: errno, or -2 for an empty or
// too big file); NO_READ: it is there but could not be read (code: errno)
enum { LOGIN_LINK_FINE, LOGIN_LINK_NO_ENGINE, LOGIN_LINK_NO_LOADER, LOGIN_LINK_NO_START, LOGIN_LINK_LOST,
       LOGIN_LINK_NO_READ };

typedef struct login_link_state {
  int      state;    // LOGIN_LINK_*
  int      why;      // LOGIN_LINK_* once failed
  int      code;     // with it: the loader's error, a LOGIN_END_* or -1
  int      width, height; // of the page, once shown
  uint8_t  loading, progress, on_x, crashed;
  int32_t  error;    // the page's last load's WebKit error, or 0
  int32_t  blocked;  // links out of X not followed
  unsigned fields;   // LOGIN_FIELDs so far
  unsigned frames;   // LOGIN_FRAMEs so far
  unsigned sessions; // LOGIN_SESSIONs so far
} login_link_state_t;

/**
 * start the engine for a page of width x height pixels at zoom percent in
 * lang ("en", "pt"...), from the launcher at path; 0, or -1 when a link
 * still runs or its thread did not start
 **/
int loginlink_start(const login_net_t *net, const char *path, int width, int height, int zoom,
                    const char *lang);

/**
 * stop it: the window closed (the engine then ends, and its thread)
 **/
void loginlink_stop(void);

/**
 * the page's memory given back, once its thread has ended
 **/
void loginlink_free(void);

/**
 * whether its thread still runs
 **/
int loginlink_running(void);

/**
 * how it goes now
 **/
void loginlink_state(login_link_state_t *st);

/**
 * the page as last shown, width x height pixels 0xAABBGGRR, once it is up
 * (else NULL); *changed when it changed since the last call. Only from the
 * thread that draws.
 **/
const uint32_t *loginlink_pixels(int *width, int *height, int *changed);

/**
 * the field in focus the page last told of, once (LOGIN_FIELD_* in *kind,
 * with LOGIN_FIELD_ASKED): its text into text (none for a password); 0
 * when none came since
 **/
int loginlink_field(int *kind, char *text, size_t size);

/**
 * X's session the page signed in to, once (LOGIN_SESSION: auth_token and
 * ct0 as name=value lines) into text, and wiped here; its length, or 0 when
 * none came since. The caller wipes text after use.
 **/
int loginlink_session(char *text, size_t size);

/**
 * to the engine, from main.c's thread: a message of loginproto.h; 0 or -1
 **/
int loginlink_send(uint16_t type, uint16_t arg, const void *body, size_t size);
