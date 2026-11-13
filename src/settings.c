/* The app's settings file, see settings.h. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "settings.h"

static const char *const g_codes[LANGS] = {"en", "pt", "es", "fr", "de", "it", "nl"};


void
settings_default(settings_t *s) {
  s->lang = LANG_SYSTEM;
  s->notify = 1;
  s->every = 60;
  s->sound = 1;
  s->autoplay = 1;
  s->saver = 0;
  s->hover = 1;
  s->theme = SETTINGS_DAY;
  s->screen = 0;
  s->spoilers = 1;
  s->light = 1;
  s->pause = 0;
  s->rising = 1;
  s->share = 1;
  s->playing = 0; // it changes what everyone sees of the profile: the user turns it on
  s->account = 1;
  memset(s->users, 0, sizeof s->users);
  memset(s->seen, 0, sizeof s->seen);
  memset(s->ids, 0, sizeof s->ids);
}


int
settings_session_path(int account, char *buf, size_t size) {
  int n;

  if(account < 1 || account > SETTINGS_ACCOUNTS) {
    return -1;
  }
  n = account == 1 ? snprintf(buf, size, "%ssession.txt", SETTINGS_SESSION_DIR) :
      snprintf(buf, size, "%ssession%d.txt", SETTINGS_SESSION_DIR, account);
  return n < 0 || (size_t)n >= size ? -1 : 0;
}


/**
 * post numbers between commas, as "seen" keeps them: digits, no empty one
 **/
static int
post_numbers(const char *s) {
  size_t n = strlen(s);

  if(!n || n >= SETTINGS_SEEN || *s == ',' || s[n - 1] == ',' || strstr(s, ",,")) {
    return 0;
  }
  for(; *s; s++) {
    if(!((*s >= '0' && *s <= '9') || *s == ',')) {
      return 0;
    }
  }
  return 1;
}


/**
 * an X screen name: 1 to 15 letters, digits and underscores
 **/
static int
screen_name(const char *s) {
  size_t n = strlen(s);

  if(!n || n > 15) {
    return 0;
  }
  for(; *s; s++) {
    if(!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_')) {
      return 0;
    }
  }
  return 1;
}


const char *
settings_lang_code(int lang) {
  return lang >= 0 && lang < LANGS ? g_codes[lang] : "en";
}


int
settings_lang_of_system(int sce_lang) {
  switch(sce_lang) {
  case 7: case 17:  return LANG_PT; // Portugal, Brazil
  case 3: case 20:  return LANG_ES; // Spain, Latin America
  case 2: case 22:  return LANG_FR; // France, Canada
  case 4:           return LANG_DE;
  case 5:           return LANG_IT;
  case 6:           return LANG_NL;
  default:          return LANG_EN;
  }
}


/**
 * a whole number from text, or def when it is not one
 **/
static int
number(const char *text, int def) {
  char *end;
  long v = strtol(text, &end, 10);

  return end != text && !*end && v >= -100000 && v <= 100000 ? (int)v : def;
}


static int
flag(const char *text, int def) {
  return !strcmp(text, "on") ? 1 : !strcmp(text, "off") ? 0 : def;
}


void
settings_parse(settings_t *s, const char *text, size_t len) {
  char line[128], *eq, *key, *value;
  size_t start = 0, end, n;
  int every;

  while(start < len) {
    for(end=start; end < len && text[end] != '\n'; end++) {
    }
    n = end - start;
    if(n < sizeof line) {
      memcpy(line, text + start, n);
      line[n] = 0;
      if(n && line[n - 1] == '\r') {
        line[n - 1] = 0;
      }
      if((eq=strchr(line, '='))) {
        *eq = 0;
        key = line;
        value = eq + 1;
        if(!strcmp(key, "lang")) {
          if(!strcmp(value, "system")) {
            s->lang = LANG_SYSTEM;
          }
          for(int i=0; i < LANGS; i++) {
            if(!strcmp(value, g_codes[i])) {
              s->lang = i;
            }
          }
        } else if(!strcmp(key, "notify")) {
          s->notify = flag(value, s->notify);
        } else if(!strcmp(key, "every")) {
          every = number(value, s->every);
          s->every = every < SETTINGS_EVERY_MIN ? SETTINGS_EVERY_MIN :
                     every > SETTINGS_EVERY_MAX ? SETTINGS_EVERY_MAX : every;
        } else if(!strcmp(key, "sound")) {
          s->sound = flag(value, s->sound);
        } else if(!strcmp(key, "autoplay")) {
          s->autoplay = flag(value, s->autoplay);
        } else if(!strcmp(key, "saver")) {
          s->saver = flag(value, s->saver);
        } else if(!strcmp(key, "hover")) {
          s->hover = flag(value, s->hover);
        } else if(!strcmp(key, "theme")) {
          s->theme = !strcmp(value, "night") ? SETTINGS_NIGHT : !strcmp(value, "day") ? SETTINGS_DAY : s->theme;
        } else if(!strcmp(key, "screen")) {
          s->screen = !strcmp(value, "auto") ? 0 : !strcmp(value, "1080p") ? 1080 :
                      !strcmp(value, "1440p") ? 1440 : !strcmp(value, "4k") ? 2160 : s->screen;
        } else if(!strcmp(key, "spoilers")) {
          s->spoilers = flag(value, s->spoilers);
        } else if(!strcmp(key, "light")) {
          s->light = flag(value, s->light);
        } else if(!strcmp(key, "rising")) {
          s->rising = flag(value, s->rising);
        } else if(!strcmp(key, "share")) {
          s->share = flag(value, s->share);
        } else if(!strcmp(key, "playing")) {
          s->playing = flag(value, s->playing);
        } else if(!strcmp(key, "break")) {
          static const int pauses[] = SETTINGS_PAUSES;
          every = !strcmp(value, "off") ? 0 : number(value, -1);
          for(size_t i=0; i < sizeof pauses / sizeof pauses[0]; i++) {
            s->pause = !every || every == pauses[i] ? every : s->pause;
          }
        } else if(!strcmp(key, "account")) {
          every = number(value, s->account);
          s->account = every >= 1 && every <= SETTINGS_ACCOUNTS ? every : s->account;
        } else if(!strncmp(key, "user", 4) && key[4] >= '1' && key[4] < '1' + SETTINGS_ACCOUNTS &&
                  !key[5] && screen_name(value)) {
          snprintf(s->users[key[4] - '1'], sizeof s->users[0], "%s", value);
        } else if(!strncmp(key, "id", 2) && key[2] >= '1' && key[2] < '1' + SETTINGS_ACCOUNTS &&
                  !key[3] && post_numbers(value) && !strchr(value, ',') &&
                  strlen(value) < sizeof s->ids[0]) {
          snprintf(s->ids[key[2] - '1'], sizeof s->ids[0], "%s", value);
        } else if(!strncmp(key, "seen", 4) && key[4] >= '1' && key[4] < '1' + SETTINGS_ACCOUNTS &&
                  !key[5] && post_numbers(value)) {
          snprintf(s->seen[key[4] - '1'], sizeof s->seen[0], "%s", value);
        }
      }
    }
    start = end + 1;
  }
}


int
settings_format(const settings_t *s, char *buf, size_t size) {
  int n = snprintf(buf, size,
                   "# XPS5 settings, written by the app's Settings screen\n"
                   "lang=%s\nnotify=%s\nevery=%d\nsound=%s\nautoplay=%s\nsaver=%s\nhover=%s\ntheme=%s\n",
                   s->lang == LANG_SYSTEM ? "system" : settings_lang_code(s->lang),
                   s->notify ? "on" : "off", s->every, s->sound ? "on" : "off",
                   s->autoplay ? "on" : "off", s->saver ? "on" : "off", s->hover ? "on" : "off",
                   s->theme == SETTINGS_NIGHT ? "night" : "day");

  if(n < 0 || (size_t)n >= size) {
    return -1;
  }
  n += snprintf(buf + n, size - n, "screen=%s\nspoilers=%s\nlight=%s\nrising=%s\nshare=%s\nplaying=%s\n",
                s->screen == 2160 ? "4k" : s->screen == 1440 ? "1440p" : s->screen == 1080 ? "1080p" : "auto",
                s->spoilers ? "on" : "off", s->light ? "on" : "off", s->rising ? "on" : "off",
                s->share ? "on" : "off", s->playing ? "on" : "off");
  if((size_t)n >= size) {
    return -1;
  }
  if(s->pause) {
    n += snprintf(buf + n, size - n, "break=%d\n", s->pause);
  } else {
    n += snprintf(buf + n, size - n, "break=off\n");
  }
  if((size_t)n >= size) {
    return -1;
  }
  n += snprintf(buf + n, size - n, "account=%d\n", s->account);
  for(int i=0; i < SETTINGS_ACCOUNTS && (size_t)n < size; i++) {
    if(s->users[i][0]) {
      n += snprintf(buf + n, size - n, "user%d=%s\n", i + 1, s->users[i]);
    }
  }
  for(int i=0; i < SETTINGS_ACCOUNTS && (size_t)n < size; i++) {
    if(s->ids[i][0]) {
      n += snprintf(buf + n, size - n, "id%d=%s\n", i + 1, s->ids[i]);
    }
  }
  for(int i=0; i < SETTINGS_ACCOUNTS && (size_t)n < size; i++) {
    if(s->seen[i][0]) {
      n += snprintf(buf + n, size - n, "seen%d=%s\n", i + 1, s->seen[i]);
    }
  }
  return (size_t)n >= size ? -1 : n;
}


/**
 * which of the files was written last, among those that open and are not
 * empty: its index, the first on a tie, or -1 for none. A title back from a
 * game can lose its /data and save beside its eboot, so the copy there can
 * be the newer one
 **/
int
settings_newest(const char *const *paths, int count) {
  struct stat st;
  long long best_time = 0;
  int best = -1;

  for(int i=0; i < count; i++) {
    if(stat(paths[i], &st) || st.st_size <= 0) {
      continue;
    }
    if(best < 0 || (long long)st.st_mtime > best_time) {
      best = i;
      best_time = (long long)st.st_mtime;
    }
  }
  return best;
}


int
settings_load(settings_t *s, const char *const *paths, int count) {
  char text[SETTINGS_TEXT];
  ssize_t n;
  int fd, first = settings_newest(paths, count);

  settings_default(s);
  for(int k=0; k < count; k++) {
    // the newest first, then the others in their order
    int i = k == 0 && first >= 0 ? first : first >= 0 && k <= first ? k - 1 : k;

    if((fd=open(paths[i], O_RDONLY)) < 0) {
      continue;
    }
    n = read(fd, text, sizeof text);
    close(fd);
    if(n > 0) {
      settings_parse(s, text, (size_t)n);
      return i;
    }
  }
  return -1;
}


/**
 * the folder of path, made when it is not there
 **/
static void
make_parent(const char *path) {
  char dir[256];
  char *slash;

  if(snprintf(dir, sizeof dir, "%s", path) >= (int)sizeof dir || !(slash=strrchr(dir, '/')) ||
     slash == dir) {
    return;
  }
  *slash = 0;
  if(mkdir(dir, 0777) && errno != EEXIST) {
    return;
  }
}


int
settings_save(const settings_t *s, const char *const *paths, int count) {
  char text[SETTINGS_TEXT];
  int len = settings_format(s, text, sizeof text), fd;
  ssize_t n;

  if(len < 0) {
    return -1;
  }
  for(int i=0; i < count; i++) {
    make_parent(paths[i]);
    if((fd=open(paths[i], O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
      continue;
    }
    n = write(fd, text, (size_t)len);
    close(fd);
    if(n == len) {
      return i;
    }
  }
  return -1;
}
