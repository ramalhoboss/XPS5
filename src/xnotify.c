/* New X notifications and their PS5 toasts, see xnotify.h. */

#define _POSIX_C_SOURCE 200809L // gmtime_r

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "xnotify.h"


/**
 * what tells a notice from the others: X keeps a notice and changes its
 * time and text when more people like the post, which makes it new again
 **/
static unsigned
key_of(const x_post_t *p) {
  char head[48];
  const char *parts[] = {head, p->id, p->text};
  unsigned h = 2166136261u; // FNV-1a

  snprintf(head, sizeof head, "%d|%lld|", p->kind, p->time);
  for(size_t i=0; i < sizeof parts / sizeof *parts; i++) {
    for(const unsigned char *s=(const unsigned char *)parts[i]; *s; s++) {
      h = (h ^ *s) * 16777619u;
    }
    h = (h ^ '|') * 16777619u;
  }
  return h ? h : 1;
}


static int
seen(const xnotify_t *n, unsigned key) {
  for(int i=0; i < n->count; i++) {
    if(n->seen[i] == key) {
      return 1;
    }
  }
  return 0;
}


static void
remember(xnotify_t *n, unsigned key) {
  n->seen[n->next] = key;
  n->next = (n->next + 1) % XNOTIFY_SEEN;
  if(n->count < XNOTIFY_SEEN) {
    n->count++;
  }
}


int
xnotify_fresh(xnotify_t *n, const x_post_t *posts, int count, int *fresh, int max) {
  long long since = n->newest;
  int found = 0;
  unsigned key;

  for(int i=0; i < count; i++) {
    const x_post_t *p = &posts[i];
    n->newest = p->time > n->newest ? p->time : n->newest;
    if(seen(n, (key=key_of(p)))) {
      continue;
    }
    remember(n, key);
    // one that was forgotten, or older ones the list shows again when a
    // newer one goes away, are older than the last reading
    if(n->primed && p->time > 0 && p->time >= since) {
      if(found < max) {
        fresh[found] = i;
      }
      found++;
    }
  }
  n->primed = 1;
  return found;
}


/**
 * s on one line in buf, cut at a character with an ellipsis when it does
 * not fit
 **/
static void
line(char *buf, size_t size, const char *s) {
  size_t n = 0, cut = 0;

  for(; *s && n + 1 < size; s++) {
    buf[n++] = *s == '\n' || *s == '\r' || *s == '\t' ? ' ' : *s;
    if(((unsigned char)s[1] & 0xC0) != 0x80 && n + 4 < size) {
      cut = n; // after a whole character, with room for the ellipsis
    }
  }
  buf[n] = 0;
  if(*s && size > 4) {
    memcpy(buf + cut, "\xe2\x80\xa6", 4);
  }
}


/**
 * the picture at 200x200 instead of the 48x48 one the answer names
 **/
static void
bigger(char *buf, size_t size, const char *url) {
  const char *at = strstr(url, "_normal.");

  buf[0] = 0;
  if(!url[0]) {
    return;
  }
  if(!at) {
    snprintf(buf, size, "%s", url);
  } else if(snprintf(buf, size, "%.*s_200x200%s", (int)(at - url), url,
                     at + strlen("_normal")) >= (int)size) {
    buf[0] = 0;
  }
}


void
xnotify_toast(const x_post_t *p, int lang, xnotify_toast_t *t) {
  // in the order of LANG_* (settings.h)
  static const char *const wrote[] = {
    "%s (@%s) wrote to you", "%s (@%s) escreveu-te", "%s (@%s) te escribi\xc3\xb3",
    "%s (@%s) t'a \xc3\xa9" "crit", "%s (@%s) hat dir geschrieben", "%s (@%s) ti ha scritto",
    "%s (@%s) schreef je",
  };
  char who[256];

  memset(t, 0, sizeof *t);
  // under them, who it is and where it came from
  if(p->user[0]) {
    snprintf(who, sizeof who, "@%s \xc2\xb7 XPS5", p->user);
    line(t->extra, sizeof t->extra, who);
  } else {
    snprintf(t->extra, sizeof t->extra, "XPS5");
  }
  if(p->kind == X_KIND_NOTICE) {
    // X words the notice: "Ana Lopes liked your post"
    line(t->title, sizeof t->title, p->text);
    line(t->body, sizeof t->body, p->quote_text[0] ? p->quote_text : "XPS5");
    bigger(t->avatar, sizeof t->avatar, p->face_count ? p->faces[0] : p->avatar);
    t->icon = p->icon == X_ICON_LIKE ? "favorite" : p->icon == X_ICON_FOLLOW ? "ps_user" : "messages";
    return;
  }
  // a reply or a mention
  lang = lang >= 0 && lang < (int)(sizeof wrote / sizeof *wrote) ? lang : 0;
  snprintf(who, sizeof who, wrote[lang], p->name[0] ? p->name : p->user, p->user);
  line(t->title, sizeof t->title, who);
  line(t->body, sizeof t->body, p->text);
  bigger(t->avatar, sizeof t->avatar, p->avatar);
  t->icon = "messages";
}


/**
 * whether p is a post of the user's own, not a repost
 **/
static int
own_post(const x_post_t *p, const char *user_id) {
  return p->kind == X_KIND_POST && !p->reposter[0] && p->id[0] && !strcmp(p->user_id, user_id);
}


/**
 * how fast p gets likes, reposts (twice) and replies, an hour, from half an
 * hour old
 **/
static double
climb(const x_post_t *p, long long now) {
  long long age = now - p->time, got = 0;

  age = age < 1800 ? 1800 : age;
  got += p->likes > 0 ? p->likes : 0;
  got += p->reposts > 0 ? 2 * p->reposts : 0;
  got += p->replies > 0 ? p->replies : 0;
  return (double)got * 3600 / (double)age;
}


int
xnotify_rising(xnotify_rising_t *r, const x_post_t *posts, int count, const char *user_id, long long now) {
  double v[64], x, middle = 0, top = 0;
  int n = 0, best = -1, told;

  if(!user_id || !user_id[0]) {
    return -1;
  }
  for(int i=0; i < count && n < (int)(sizeof v / sizeof v[0]); i++) {
    if(own_post(&posts[i], user_id) && posts[i].time > 0) {
      // in order, as they come
      x = climb(&posts[i], now);
      int j = n++;
      for(; j > 0 && v[j - 1] > x; j--) {
        v[j] = v[j - 1];
      }
      v[j] = x;
    }
  }
  if(n >= 3) {
    middle = v[n / 2];
  }
  for(int i=0; i < count; i++) {
    const x_post_t *p = &posts[i];
    if(!own_post(p, user_id) || p->time <= 0 || now - p->time > 24 * 3600 || p->likes < 50) {
      continue;
    }
    x = climb(p, now);
    told = 0;
    for(int k=0; k < XNOTIFY_RISING_KEPT; k++) {
      told |= !strcmp(r->told[k], p->id);
    }
    if(x < 10 || x < 3 * middle || told || x <= top) {
      continue;
    }
    top = x;
    best = i;
  }
  if(best >= 0) {
    snprintf(r->told[r->next++ % XNOTIFY_RISING_KEPT], sizeof r->told[0], "%s", posts[best].id);
  }
  return best;
}


void
xnotify_rising_toast(const x_post_t *p, const char *title, xnotify_toast_t *t) {
  memset(t, 0, sizeof *t);
  line(t->title, sizeof t->title, title);
  line(t->body, sizeof t->body, p->text[0] ? p->text : p->quote_text[0] ? p->quote_text : "XPS5");
  snprintf(t->extra, sizeof t->extra, "XPS5");
  bigger(t->avatar, sizeof t->avatar, p->avatar);
  t->icon = "favorite";
}


/**
 * s as the inside of a JSON string at buf + *n; returns 0 or -1
 **/
static int
put_json(char *buf, size_t size, size_t *n, const char *s) {
  static const char hex[] = "0123456789abcdef";

  for(; *s; s++) {
    unsigned char c = (unsigned char)*s;
    char esc[7] = {c, 0};
    if(c == '"' || c == '\\') {
      esc[0] = '\\';
      esc[1] = (char)c;
      esc[2] = 0;
    } else if(c < 0x20) {
      memcpy(esc, "\\u00", 4);
      esc[4] = hex[c >> 4];
      esc[5] = hex[c & 15];
      esc[6] = 0;
    }
    if(*n + strlen(esc) >= size) {
      return -1;
    }
    memcpy(buf + *n, esc, strlen(esc));
    *n += strlen(esc);
  }
  buf[*n] = 0;
  return 0;
}


/**
 * buf + *n gets text, then from the NULL-ended list in turn a string
 * escaped for JSON and a text as it is; returns 0 or -1
 **/
static int
put(char *buf, size_t size, size_t *n, const char *text, ...) {
  va_list args;
  const char *s;
  size_t len = strlen(text);
  int ret = 0;

  if(*n + len >= size) {
    return -1;
  }
  memcpy(buf + *n, text, len + 1);
  *n += len;
  va_start(args, text);
  while(!ret && (s=va_arg(args, const char *))) {
    ret = put_json(buf, size, n, s);
    if(!ret && (s=va_arg(args, const char *))) {
      len = strlen(s);
      if(*n + len >= size) {
        ret = -1;
      } else {
        memcpy(buf + *n, s, len + 1);
        *n += len;
      }
    } else {
      break;
    }
  }
  va_end(args);
  return ret;
}


/**
 * the icon's JSON: the picture, else the PS5's own
 **/
static int
put_icon(char *buf, size_t size, size_t *n, const xnotify_toast_t *t) {
  if(t->avatar[0]) {
    return put(buf, size, n, "{\"type\": \"Url\", \"parameters\": {\"url\": \"", t->avatar, "\"}}", NULL);
  }
  return put(buf, size, n, "{\"type\": \"Predefined\", \"parameters\": {\"icon\": \"",
             t->icon ? t->icon : "messages", "\"}}", NULL);
}


int
xnotify_json(char *buf, size_t size, const xnotify_toast_t *t, const char *action,
             const char *link, long long now, unsigned id) {
  time_t when = (time_t)now;
  struct tm tm;
  char created[32], number[16];
  size_t n = 0;

  if(!gmtime_r(&when, &tm) || !strftime(created, sizeof created, "%Y-%m-%dT%H:%M:%S.000Z", &tm)) {
    return -1;
  }
  snprintf(number, sizeof number, "%u", id);
  if(put(buf, size, &n, "{\"rawData\": {\"viewTemplateType\": \"InteractiveToastTemplateB\", "
         "\"channelType\": \"Downloads\", \"useCaseId\": \"IDC\", \"toastOverwriteType\": \"No\", "
         "\"isImmediate\": true, \"priority\": 100, \"viewData\": {\"icon\": ", NULL) ||
     put_icon(buf, size, &n, t) ||
     put(buf, size, &n, ", \"message\": {\"body\": \"", t->title, "\"}, \"subMessage\": {\"body\": \"",
         t->body, "\"}", NULL) ||
     (t->extra[0] && put(buf, size, &n, ", \"extraMessage\": {\"body\": \"", t->extra, "\"}", NULL)) ||
     (link && put(buf, size, &n, ", \"actions\": [{\"actionName\": \"", action, "\", \"actionType\": "
                  "\"DeepLink\", \"defaultFocus\": true, \"parameters\": {\"actionUrl\": \"", link,
                  "\"}}]", NULL)) ||
     put(buf, size, &n, "}, \"platformViews\": {\"previewDisabled\": {\"viewData\": {\"icon\": ", NULL) ||
     put_icon(buf, size, &n, t) ||
     put(buf, size, &n, ", \"message\": {\"body\": \"X: ", t->title, "\"}}}}}, \"createdDateTime\": \"",
         created, "\", \"localNotificationId\": \"", number, "\"}", NULL)) {
    return -1;
  }
  return 0;
}
