/* The user's X notifications as PS5 toasts: which ones in an answer of
NotificationsTimeline are new since the last reading, and the JSON that
sceNotificationSend takes for each (the layout of the PS5's own interactive
toasts, which src/toast.c showed working). Apart from the PS5, so the host
tests cover it; src/notify.c is the payload that runs it. */

#pragma once

#include <stddef.h>

#include "xapi.h"

#define XNOTIFY_SEEN  128 // notices remembered, more than a few readings hold
#define XNOTIFY_TEXT  240 // bytes of a toast's line, at most
#define XNOTIFY_JSON  8192

// what is remembered between readings
typedef struct xnotify {
  unsigned  seen[XNOTIFY_SEEN]; // a key for each notice already met
  int       count, next;
  long long newest;             // the latest time met, Unix time
  int       primed;             // the first reading was taken
} xnotify_t;

// one toast
typedef struct xnotify_toast {
  char title[XNOTIFY_TEXT];     // who did what, or who wrote
  char body[XNOTIFY_TEXT];      // the post it is about, or what was written
  char extra[XNOTIFY_TEXT];     // a third line, or empty: the PS5 shows it in its list of
                                // notifications and when toasts come together, not on one
  char avatar[256];             // a bigger picture of who it is, or empty
  const char *icon;             // the PS5's own icon when there is no picture
} xnotify_toast_t;


#define XNOTIFY_RISING_KEPT 16 // posts already said to be rising, remembered

// the user's posts already said to be rising (xnotify_rising)
typedef struct xnotify_rising {
  char told[XNOTIFY_RISING_KEPT][X_ID_MAX];
  int  next;
} xnotify_rising_t;


/**
 * the notices and posts of an answer (newest first) that came since the last
 * reading: up to max of their indexes in fresh, newest first; returns how
 * many there are, which can be more than max. The first reading only
 * remembers what is there and returns 0.
 **/
int xnotify_fresh(xnotify_t *n, const x_post_t *posts, int count, int *fresh, int max);

/**
 * what the toast for p says, cut to fit, in lang (LANG_* of settings.h;
 * the notices come worded by X, in the language x_set_language set)
 **/
void xnotify_toast(const x_post_t *p, int lang, xnotify_toast_t *t);

/**
 * among posts (count, as UserTweets answers), the one of the user with the
 * number user_id rising at the Unix time now, and not said before: under a
 * day old, with 50 likes or more, and getting likes, reposts and replies
 * three times as fast as the middle of the user's posts there; its index,
 * remembered from then on, or -1
 **/
int xnotify_rising(xnotify_rising_t *r, const x_post_t *posts, int count, const char *user_id, long long now);

/**
 * the toast for the user's post p rising, titled title (in the payload's
 * language)
 **/
void xnotify_rising_toast(const x_post_t *p, const char *title, xnotify_toast_t *t);

/**
 * the JSON of sceNotificationSend for t, with a button named action that
 * opens link (or none when link is NULL), created at the Unix time now and
 * with that id; returns 0, or -1 when it does not fit
 **/
int xnotify_json(char *buf, size_t size, const xnotify_toast_t *t, const char *action,
                 const char *link, long long now, unsigned id);
