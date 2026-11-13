/* The PS5's keyboard for writing a reply: libSceImeDialog, whose functions
the title links, loaded from the sandbox the first time it is opened. The
dialog draws over the title, which keeps presenting frames while it is up. */

#pragma once

#include <stddef.h>

enum { IME_RUNNING, IME_DONE, IME_CANCELLED, IME_FAILED };

// the kinds of field ime_open_field opens it for
enum { IME_FIELD_TEXT, IME_FIELD_PASSWORD, IME_FIELD_EMAIL, IME_FIELD_NUMBER };

/**
 * where to say how loading the library went (codes only), or NULL
 **/
void ime_set_log(void (*log)(const char *fmt, ...));

/**
 * open the keyboard for user, with a title, the grey text shown while it is
 * empty and the text it starts with, or NULL (all UTF-8), taking at most max
 * characters; returns 0 or the SCE error of loading the library, finding its
 * functions or opening it
 **/
int ime_open(int user, const char *title, const char *placeholder, const char *text, int max);

/**
 * the same for a field of a page (IME_FIELD_*): no capitals of its own and
 * nothing learnt from what is typed, the e-mail or number keyboard for
 * those, and for a password its characters hidden; its enter key says Go
 **/
int ime_open_field(int user, const char *title, const char *placeholder, const char *text, int max, int field);

/**
 * whether the keyboard is still up; on IME_DONE text holds what was
 * written, as UTF-8 in size bytes; the keyboard is closed after anything but
 * IME_RUNNING
 **/
int ime_poll(char *text, size_t size);
