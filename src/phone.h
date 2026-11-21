/* Writing on the phone. The PS5's keyboard is slow with a controller, so
the title can show a QR that opens a page on a phone (or a PC) in the same
home network, where what the user types appears on the TV as they type it,
for a post, a reply or a message. The title, a media app, may not listen in
its sandbox, so the notify payload serves the page on PHONE_PORT
(src/phoneserve.c) while the title asks it to over its control port:
  "type <screen> <seq> <max> <what> <lang> <draft>"
every moment the title's window for it is open (screen counts the windows
the title opened; seq is the last text it got, 0 for none; max how many
characters the text takes; what is post, reply, dm or search; lang two
letters; draft what the title had written, in hex, or "-"), "untype" once
it closes. The payload answers phone_state_line, followed by the text the
phone wrote when its number is not seq.

The QR opens http://<console>:PHONE_PORT/#<token>: the token, random each
time, is what the page must send back, so only who sees the TV writes. Its
requests:
  GET /                    the page (src/phonepage/), with /phone.js and /phone.css
  POST /draft {"token"}    {"text":"...","max":280,"what":"post","lang":"pt","seq":3}
  POST /text  {"token","text","done"}   {"ok":true,"seq":4}; done is its Send
Only the page's own origin (or none) is answered. What is here touches no
network, so tests/phone_test.c runs it on the host. */

#pragma once

#include <stddef.h>

#define PHONE_PORT      9071 // on the console's address, while the title's window is open
#define PHONE_TOKEN_LEN 16   // hex digits
#define PHONE_TEXT_MAX  4000 // bytes of what the phone writes, at most
#define PHONE_IDLE      30   // seconds without the title asking before the payload stops

// where the payload's server is
enum { PHONE_STARTING, PHONE_WAITING, PHONE_NO_NETWORK };

typedef struct phone_state {
  int      state;                       // PHONE_*
  int      error;                       // the network's, for PHONE_NO_NETWORK
  int      seen;                        // the page was opened
  int      done;                        // the phone pressed Send for the text of seq
  unsigned seq;                         // the text's number: one more for each change
  char     ip[16];                      // the console's address, once waiting
  char     token[PHONE_TOKEN_LEN + 1];  // what the QR carries, once waiting
} phone_state_t;

/**
 * the line the payload answers "type" with:
 * "type <state> <error> <seen> <done> <seq> <ip> <token> <len>\n", "-" for
 * an empty text, len the bytes of the text that follow it (0 for none);
 * returns its length, or -1 when it does not fit
 **/
int phone_state_line(char *buf, size_t size, const phone_state_t *s, size_t len);

/**
 * a line of phone_state_line (with or without its '\n') into s and len;
 * 0, or -1 when it is not one
 **/
int phone_state_parse(const char *line, phone_state_t *s, size_t *len);

/**
 * the token of a window, PHONE_TOKEN_LEN hex digits from 64 random bits
 **/
void phone_token(unsigned long long bits, char token[PHONE_TOKEN_LEN + 1]);

/**
 * whether a request's Origin header may write: none, or the page's own
 * (http://<ip>:PHONE_PORT)
 **/
int phone_origin_ok(const char *origin, const char *ip);

/**
 * a POST body: its token, and for /text the text (at most size - 1 bytes,
 * cut on a whole character) and whether it was sent; 0, or -1 when it is
 * not JSON or misses the token
 **/
int phone_parse_body(const char *body, size_t len, char token[PHONE_TOKEN_LEN + 1], char *text,
                     size_t size, int *has_text, int *done);

/**
 * the answer to /draft: {"text":...,"max":N,"what":"...","lang":"..","seq":N};
 * returns its length, or -1 when it does not fit
 **/
int phone_draft_json(char *buf, size_t size, const char *text, int max, const char *what,
                     const char *lang, unsigned seq);

/**
 * text in hex (two digits a byte), or "-" for an empty one; returns its
 * length, or -1 when it does not fit
 **/
int phone_hex(const char *text, char *buf, size_t size);

/**
 * hex of phone_hex back into text (at most size - 1 bytes); returns its
 * length, or -1 when it is not hex
 **/
int phone_unhex(const char *hex, char *text, size_t size);
