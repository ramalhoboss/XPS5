/* Linking an X account from a PC. While the title's screen for it is open,
the notify payload answers a little HTTP on LINK_PORT of the console's
address (src/linkserve.c; the title, a media app, may not listen in its
sandbox, so it asks the payload over its control port and shows the state
line below); the XPS5 Login extension for Chrome, Edge and Firefox
(tools/xps5-login-extension) sends it the x.com session of the browser it
runs in, with the six digits the screen shows. The screen's QR opens
http://<console>:LINK_PORT/#<code> on a phone or a PC, a page that says
where the extension's button is; the button, pressed there, reads the
address and the code from the tab. What is here touches no network, so
tests/link_test.c runs it on the host.

The requests:
  GET /           the page the QR opens (src/linkpage/, with /link.js and
                  /link.css): static, nothing of the session in it
  GET /hello      {"app":"XPS5","v":1,"lang":"en"}, so the extension knows it
                  found the console before it reads any cookie, and speaks the
                  language XPS5 shows (LANG_* of settings.h, by its two letters)
  POST /session   {"code":"123456","auth_token":"...","ct0":"..."}
  OPTIONS         the CORS preflight, private network access included
Only a browser extension's Origin, or none, is answered. */

#pragma once

#include <stddef.h>

#define LINK_PORT        9070  // on the console's address, while the screen is open
#define LINK_CODE_LEN    6
#define LINK_TRIES       5     // wrong codes before the screen shows a new one
#define LINK_REQUEST_MAX 8192  // a request, headers and body

enum { LINK_GET, LINK_POST, LINK_OPTIONS, LINK_OTHER };

typedef struct link_request {
  int         method;      // LINK_*
  char        path[32];    // without its query; empty when longer
  char        origin[160]; // the Origin header, or empty; cut when longer
  int         origin_ok;   // none, or a browser extension's
  const char *body;        // in the buffer parsed
  size_t      body_len;
} link_request_t;

/**
 * how many bytes of buf (len of them so far) a whole request takes, its
 * headers and the body their Content-Length announces: 0 while more must
 * come, -1 when it cannot be one (bigger than LINK_REQUEST_MAX, or a length
 * that does not read)
 **/
long link_request_size(const char *buf, size_t len);

/**
 * the request in buf, len bytes as link_request_size measured them;
 * returns 0, or -1 when it is not one
 **/
int link_parse_request(const char *buf, size_t len, link_request_t *r);

/**
 * the session in a POST /session body: returns 0, -1 when it is not JSON
 * or misses a field, -2 when a cookie cannot be one (auth_token 20 to 127
 * hex digits, ct0 20 to 255), -3 when the code is not LINK_CODE_LEN digits;
 * what it returns into is cleared first
 **/
int link_parse_session(const char *body, size_t len, char code[LINK_CODE_LEN + 1],
                       char *auth_token, size_t auth_size, char *ct0, size_t ct0_size);

/**
 * an answer of status with the JSON json (NULL for none), and the CORS
 * headers that let the extension read it, origin given back when it is a
 * browser extension's; returns its length, or -1 when it does not fit
 **/
int link_response(char *buf, size_t size, int status, const char *origin, const char *json);

/**
 * the headers of a file of the QR's page, len bytes of type, with a policy
 * that lets it load only its own script and style; returns their length,
 * or -1 when they do not fit
 **/
int link_file_head(char *buf, size_t size, const char *type, size_t len);

/**
 * the code a screen shows, LINK_CODE_LEN digits, from 64 random bits
 **/
void link_code(unsigned long long bits, char code[LINK_CODE_LEN + 1]);

/**
 * bits mixed (splitmix64), for the code's randomness
 **/
unsigned long long link_mix(unsigned long long bits);

/**
 * whether a and b are the same, in a time that does not tell where they
 * differ
 **/
int link_same(const char *a, const char *b);

/**
 * the text of a session file (src/xapi.h, x_session_parse reads it);
 * returns its length, or -1 when it does not fit
 **/
int link_session_text(char *buf, size_t size, const char *auth_token, const char *ct0);

// where a linking is (the title's UI_LINK_* go in the same order)
enum { LINK_STARTING, LINK_WAITING, LINK_CHECKING, LINK_DONE, LINK_NO_NETWORK };
// what the last try of the PC did
enum { LINK_FINE, LINK_WRONG_CODE, LINK_REFUSED, LINK_UNWRITTEN };

typedef struct link_state {
  int  state;                   // LINK_STARTING...
  int  last;                    // LINK_FINE...
  int  wrong;                   // wrong codes since the code was drawn
  int  renewed;                 // the code was drawn again after LINK_TRIES of them
  int  refused;                 // X's HTTP status for the session it refused, -1 no network
  int  error;                   // the network's, for LINK_NO_NETWORK
  int  account;                 // the account it went to, once LINK_DONE
  char ip[16];                  // the console's address, once waiting
  char code[LINK_CODE_LEN + 1]; // the code the screen shows, once waiting
  char user[16];                // whose session it is, a screen name, once LINK_DONE
  int  seen;                    // the QR's page was opened, on a phone or a PC
} link_state_t;

/**
 * the line the payload answers the title's "link" with:
 * "link <state> <last> <wrong> <renewed> <refused> <error> <account> <ip> <code> <user> <seen>\n",
 * "-" for an empty text; returns its length, or -1 when it does not fit
 **/
int link_state_line(char *buf, size_t size, const link_state_t *s);

/**
 * a line of link_state_line (with or without its '\n', and with or without
 * <seen>, which a payload before version 6 does not write) into s; returns
 * 0, or -1 when it is not one
 **/
int link_state_parse(const char *line, link_state_t *s);
