/* The game that runs beside the title, see game.h. */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "json.h"
#include "settings.h"


int
game_id_ok(const char *id) {
  for(int i=0; i<4; i++) {
    if(id[i] < 'A' || id[i] > 'Z') {
      return 0;
    }
  }
  for(int i=4; i<9; i++) {
    if(id[i] < '0' || id[i] > '9') {
      return 0;
    }
  }
  return !id[9] && strncmp(id, "NPXS", 4) && strcmp(id, "TWPS00001");
}


/**
 * s, up to len bytes, into name on one line: control characters become
 * spaces, the spaces at the ends go, and a cut never splits a character;
 * 0, or -1 when nothing is left
 **/
static int
clean_name(const char *s, size_t len, char *name, size_t size) {
  size_t n = 0;

  if(!size) {
    return -1;
  }
  for(size_t i=0; i < len && s[i]; i++) {
    unsigned char c = (unsigned char)s[i];
    if(n + 1 >= size) {
      break;
    }
    if(c < 0x20 || c == 0x7f) {
      c = ' ';
    }
    if(c == ' ' && (!n || name[n - 1] == ' ')) {
      continue;
    }
    name[n++] = (char)c;
  }
  // not half a character at the end
  if(n + 1 >= size) {
    size_t at = n;
    while(at && ((unsigned char)name[at - 1] & 0xc0) == 0x80) {
      at--;
    }
    if(at && (unsigned char)name[at - 1] >= 0xc0) {
      unsigned char lead = (unsigned char)name[at - 1];
      size_t want = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : 2;
      if(n - (at - 1) < want) {
        n = at - 1;
      }
    }
  }
  while(n && name[n - 1] == ' ') {
    n--;
  }
  name[n] = 0;
  return n ? 0 : -1;
}


int
game_name_json(char *text, size_t len, const char *lang, char *name, size_t size) {
  const json_node_t *lp, *entry;
  const char *tries[4], *s;
  json_doc_t *doc;
  size_t off = 0;
  int ret = -1;

  if(!(doc=json_parse(text, len, &off))) {
    return -1;
  }
  lp = json_get(json_root(doc), "localizedParameters");
  tries[0] = lang;
  tries[1] = json_string(json_get(lp, "defaultLanguage"));
  tries[2] = "en-US";
  tries[3] = "en-GB";
  for(int i=0; i<4 && ret; i++) {
    if(tries[i] && (entry=json_get(lp, tries[i])) && (s=json_string(json_get(entry, "titleName")))) {
      ret = clean_name(s, strlen(s), name, size);
    }
  }
  json_free(doc);
  return ret;
}


int
game_category_json(char *text, size_t len) {
  const json_node_t *n;
  json_doc_t *doc;
  size_t off = 0;
  char *end;
  long v;
  int ret = -1;

  if(!(doc=json_parse(text, len, &off))) {
    return -1;
  }
  n = json_get(json_root(doc), "applicationCategoryType");
  if(n && n->type == JSON_NUMBER && n->str) {
    v = strtol(n->str, &end, 10);
    if(end != n->str && v >= 0 && v <= INT_MAX) {
      ret = (int)v;
    }
  }
  json_free(doc);
  return ret;
}


static unsigned
u16le(const unsigned char *p) {
  return p[0] | (unsigned)p[1] << 8;
}


static uint32_t
u32le(const unsigned char *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}


int
game_name_sfo(const unsigned char *buf, size_t len, char *name, size_t size) {
  uint32_t keys, data, count;

  if(len < 20 || memcmp(buf, "\0PSF", 4)) {
    return -1;
  }
  keys = u32le(buf + 8);
  data = u32le(buf + 12);
  count = u32le(buf + 16);
  if(keys >= len || data >= len || count > 1024 || 20 + (size_t)count * 16 > len) {
    return -1;
  }
  for(uint32_t i=0; i < count; i++) {
    const unsigned char *e = buf + 20 + (size_t)i * 16;
    size_t key = (size_t)keys + u16le(e), at = (size_t)data + u32le(e + 12), n = u32le(e + 4);
    if(key >= len || !memchr(buf + key, 0, len - key) || strcmp((const char *)buf + key, "TITLE") ||
       (u16le(e + 2) & 0xff) != 0x04) {
      continue;
    }
    if(at >= len || n > len - at) {
      return -1;
    }
    return clean_name((const char *)buf + at, n, name, size);
  }
  return -1;
}


int
game_hashtag(const char *name, char *tag, size_t size) {
  const unsigned char *s = (const unsigned char *)name;
  size_t n = 0;
  int letters = 0;

  if(size < 3) {
    return -1;
  }
  tag[n++] = '#';
  while(*s) {
    if((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9')) {
      if(n + 1 >= size) {
        break;
      }
      letters += !(*s >= '0' && *s <= '9');
      tag[n++] = (char)*s++;
    } else if(s[0] == 0xc3 && s[1] >= 0x80 && s[1] <= 0xbf && s[1] != 0x97 && s[1] != 0xb7) {
      // À to ÿ, without × and ÷
      if(n + 2 >= size) {
        break;
      }
      letters++;
      tag[n++] = (char)*s++;
      tag[n++] = (char)*s++;
    } else {
      s++;
    }
  }
  tag[n] = 0;
  return letters ? 0 : -1;
}


/**
 * text folded for game_keys: the ASCII letters small, the accented Latin ones
 * without their accents, digits and other letters as they are, apostrophes
 * gone and anything else one space between words; caps, when given, has 1
 * for a letter that was a capital and 2 for one after a '#'. Its length
 **/
static size_t
fold(const char *text, char *out, size_t size, unsigned char *caps) {
  // À to ÿ, without their accents; × and ÷ part words
  static const char latin[] = "aaaaaaaceeeeiiiidnooooo ouuuuyts" "aaaaaaaceeeeiiiidnooooo ouuuuyty";
  const unsigned char *s = (const unsigned char *)text;
  size_t n = 0, len;
  int space = 0, tag = 0, sep;
  unsigned char cap;
  char c;

  if(!size) {
    return 0;
  }
  while(*s) {
    if(*s == '\'' || (s[0] == 0xe2 && s[1] == 0x80 && s[2] == 0x99)) {
      s += *s == '\'' ? 1 : 3; // "Marvel's" is "marvels"
      continue;
    }
    if((s[0] == 0xc2 && (s[1] == 0xa9 || s[1] == 0xae)) || (s[0] == 0xe2 && s[1] == 0x84 && s[2] == 0xa2)) {
      s += s[0] == 0xc2 ? 2 : 3; // © ® ™
      continue;
    }
    c = 0;
    cap = 0;
    len = 1;
    sep = 0;
    if(*s >= 'A' && *s <= 'Z') {
      c = (char)(*s - 'A' + 'a');
      cap = 1;
    } else if((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9')) {
      c = (char)*s;
    } else if(s[0] == 0xc3 && s[1] >= 0x80 && s[1] <= 0xbf) {
      c = latin[s[1] - 0x80];
      cap = s[1] < 0x9f && s[1] != 0x97;
      len = 2;
      sep = c == ' ';
    } else if(*s >= 0xc2 && *s < 0xf8) {
      // another letter, as it is; but Latin-1's signs, punctuation, arrows,
      // symbols, the CJK punctuation and emoji part words
      len = *s >= 0xf0 ? 4 : *s >= 0xe0 ? 3 : 2;
      for(size_t i=1; i < len; i++) {
        if((s[i] & 0xc0) != 0x80) {
          len = 1;
          break;
        }
      }
      sep = len == 1 || *s == 0xc2 || (s[0] == 0xe2 && s[1] < 0xb0) || (s[0] == 0xe3 && s[1] == 0x80) ||
            *s >= 0xf0;
    } else {
      sep = 1;
    }
    if(sep) {
      space = n > 0;
      tag = *s == '#';
      s += len;
      continue;
    }
    if(n + (space ? 1 : 0) + len >= size) {
      break;
    }
    if(space) {
      if(caps) {
        caps[n] = 0;
      }
      out[n++] = ' ';
      space = 0;
    }
    for(size_t i=0; i < (c ? 1 : len); i++) {
      if(caps) {
        caps[n] = (unsigned char)((c ? cap : 0) | (tag && !i ? 2 : 0));
      }
      out[n++] = c ? c : (char)s[i];
    }
    tag = 0;
    s += len;
  }
  out[n] = 0;
  return n;
}


/**
 * a roman number of a sequel ("ii" to "xx"), or 0
 **/
static int
roman(const char *w) {
  static const char *const numbers[] = {
    "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii", "xiii", "xiv", "xv", "xvi",
    "xvii", "xviii", "xix", "xx",
  };

  for(int i=1; i < (int)(sizeof numbers / sizeof numbers[0]); i++) {
    if(!strcmp(w, numbers[i])) {
      return i + 1;
    }
  }
  return 0;
}


/**
 * w is all digits
 **/
static int
digits(const char *w) {
  if(!*w) {
    return 0;
  }
  for(; *w; w++) {
    if(*w < '0' || *w > '9') {
      return 0;
    }
  }
  return 1;
}


/**
 * the folded s without the words of an edition at its end ("directors cut",
 * "game of the year edition"), unless nothing would be left
 **/
static void
no_edition(char *s) {
  static const char *const words[] = {
    "edition", "remastered", "remaster", "remake", "deluxe", "complete", "definitive", "ultimate",
    "standard", "digital", "directors", "cut", "goty", "game", "of", "the", "year", "ps4", "ps5",
    "version", "bundle", "collection", "enhanced", "anniversary",
  };
  char *at;
  int found = 1;

  while(found && (at=strrchr(s, ' '))) {
    found = 0;
    for(size_t i=0; i < sizeof words / sizeof words[0]; i++) {
      if(!strcmp(at + 1, words[i])) {
        *at = 0;
        found = 1;
        break;
      }
    }
  }
}


/**
 * the folded s without its number at the end ("2", "part ii"), when words
 * are left
 **/
static int
no_number(char *s) {
  char *at = strrchr(s, ' ');

  if(!at || !(digits(at + 1) || roman(at + 1))) {
    return 0;
  }
  *at = 0;
  if((at=strrchr(s, ' ')) && !strcmp(at + 1, "part")) {
    *at = 0;
  }
  return 1;
}


/**
 * key, unless k has it; how is how a text writes it when it is all letters
 * (GAME_KEY_*), and in any case otherwise
 **/
static void
add_key(game_keys_t *k, const char *key, size_t least, int how) {
  size_t len = strlen(key);
  int letters = 1;

  if(k->count >= GAME_KEYS || len >= GAME_KEY || len < least) {
    return;
  }
  for(int i=0; i < k->count; i++) {
    if(!strcmp(k->key[i], key)) {
      return;
    }
  }
  for(const char *c=key; *c; c++) {
    letters &= !(*c >= '0' && *c <= '9') && *c != ' ';
  }
  memcpy(k->key[k->count], key, len + 1);
  k->how[k->count++] = letters ? how : GAME_KEY_ANY;
}


/**
 * key as one word, as its hashtag is
 **/
static void
add_joined(game_keys_t *k, const char *key) {
  char one[GAME_KEY];
  size_t n = 0;

  if(!strchr(key, ' ')) {
    return;
  }
  for(; *key && n + 1 < sizeof one; key++) {
    if(*key != ' ') {
      one[n++] = *key;
    }
  }
  one[n] = 0;
  add_key(k, one, 5, GAME_KEY_ANY);
}


/**
 * the initials of the folded s, its numbers as digits ("tlou2"), and as far
 * as its first number ("ff7"); with the letters alone ("TLOU") when its
 * number ends it
 **/
static void
add_initials(game_keys_t *k, const char *s) {
  char all[GAME_KEY], word[GAME_KEY], to_number[GAME_KEY] = "";
  size_t n = 0, w;
  int letters = 0, words = 0, number, last_number = 0, cut = 0;

  while(*s && n + 3 < sizeof all) {
    w = 0;
    while(s[w] && s[w] != ' ') {
      w++;
    }
    if(w >= sizeof word) {
      return;
    }
    memcpy(word, s, w);
    word[w] = 0;
    s += w + (s[w] == ' ');
    number = digits(word) ? -1 : words ? roman(word) : 0;
    if(words && !strcmp(word, "part") && *s && !strchr(s, ' ') && (digits(s) || roman(s))) {
      continue; // "part ii" is "2"
    }
    if(number) {
      if(number < 0) {
        if(n + w + 1 >= sizeof all) {
          return;
        }
        memcpy(all + n, word, w);
        n += w;
      } else {
        n += (size_t)snprintf(all + n, sizeof all - n, "%d", number);
      }
      if(!to_number[0] && letters >= 2) {
        memcpy(to_number, all, n);
        to_number[n] = 0;
      }
    } else if(word[0] >= 'a' && word[0] <= 'z') {
      all[n++] = word[0];
      letters++;
    } else {
      return; // a word in another script
    }
    last_number = number != 0;
    words++;
  }
  all[n] = 0;
  if(letters < 2 || words < 2) {
    return;
  }
  add_key(k, all, 3, GAME_KEY_CAPITALS);
  if(to_number[0]) {
    add_key(k, to_number, 3, GAME_KEY_CAPITALS);
  }
  if(last_number) {
    cut = (int)n;
    while(cut > 0 && all[cut - 1] >= '0' && all[cut - 1] <= '9') {
      cut--;
    }
    all[cut] = 0;
    if(letters >= 3 && cut == letters) {
      add_key(k, all, 3, GAME_KEY_CAPITALS);
    }
  }
}


int
game_keys(const char *name, game_keys_t *k) {
  char base[GAME_KEY * 2], plain[GAME_KEY * 2], cut[GAME_KEY * 2], whose[GAME_KEY * 2], part[GAME_NAME];
  const char *at, *seps[] = {":", " - ", " \xe2\x80\x93 ", " \xe2\x80\x94 ", " | "};
  size_t len = 0;

  memset(k, 0, sizeof *k);
  if(!name || !name[0]) {
    return 0;
  }
  fold(name, base, sizeof base, NULL);
  no_edition(base);
  snprintf(plain, sizeof plain, "%s", base);
  if(!no_number(plain) || !strchr(base, ' ')) {
    plain[0] = 0;
  }

  // the name before its subtitle
  cut[0] = 0;
  for(size_t i=0; i < sizeof seps / sizeof seps[0]; i++) {
    if((at=strstr(name, seps[i])) && at > name && (!len || (size_t)(at - name) < len)) {
      len = (size_t)(at - name);
    }
  }
  if(len && len < sizeof part) {
    memcpy(part, name, len);
    part[len] = 0;
    fold(part, cut, sizeof cut, NULL);
    no_edition(cut);
  }

  // without whose it is, in its first two words: "Marvel's", "Tom Clancy's"
  whose[0] = 0;
  if((at=strstr(name, "'s ")) || (at=strstr(name, "\xe2\x80\x99s "))) {
    int spaces = 0;
    for(const char *c=name; c < at; c++) {
      spaces += *c == ' ';
    }
    if(spaces <= 1) {
      fold(at + (*at == '\'' ? 3 : 5), whose, sizeof whose, NULL);
      no_edition(whose);
    }
  }

  add_key(k, base, 4, GAME_KEY_CAPITAL);
  if(plain[0]) {
    add_key(k, plain, 4, GAME_KEY_CAPITAL);
  }
  if(cut[0]) {
    add_key(k, cut, 5, GAME_KEY_CAPITAL);
  }
  if(whose[0] && strchr(whose, ' ')) {
    add_key(k, whose, 5, GAME_KEY_CAPITAL);
  }
  add_joined(k, base);
  if(plain[0]) {
    add_joined(k, plain);
  }
  if(cut[0]) {
    add_joined(k, cut);
  }
  add_initials(k, base);
  return k->count;
}


int
game_named(const char *text, const game_keys_t *k) {
  static char t[2048];
  static unsigned char caps[2048];
  size_t n, len, pos, end;
  int ok;

  if(!k->count || !text || !text[0]) {
    return 0;
  }
  n = fold(text, t, sizeof t, caps);
  for(int i=0; i < k->count; i++) {
    len = strlen(k->key[i]);
    for(const char *at=strstr(t, k->key[i]); at; at=strstr(at + 1, k->key[i])) {
      pos = (size_t)(at - t);
      end = pos + len;
      // words of their own: after them nothing, a space, a number after
      // letters ("TLOU2") or the s of a plural or of whose
      if((pos && t[pos - 1] != ' ') ||
         !(end == n || t[end] == ' ' ||
           (t[end] >= '0' && t[end] <= '9' && !(t[end - 1] >= '0' && t[end - 1] <= '9')) ||
           (t[end] == 's' && (end + 1 == n || t[end + 1] == ' ')))) {
        continue;
      }
      ok = 1;
      if(k->how[i] == GAME_KEY_CAPITAL) {
        ok = caps[pos] != 0;
      } else if(k->how[i] == GAME_KEY_CAPITALS && !(caps[pos] & 2)) {
        for(size_t j=pos; j < end; j++) {
          ok &= !(t[j] >= 'a' && t[j] <= 'z') || (caps[j] & 1);
        }
      }
      if(ok) {
        return 1;
      }
    }
  }
  return 0;
}


int
game_query(const char *name, char *out, size_t size) {
  static const char tail[] = " -spoiler -spoilers -filter:replies";
  game_keys_t k;
  char joined[GAME_KEY];
  const char *key;
  size_t n = 0;
  int len;

  if(!size) {
    return -1;
  }
  out[0] = 0;
  if(game_keys(name, &k) < 1) {
    return -1;
  }
  key = k.key[0]; // the whole name, folded, without its edition
  for(const char *c=key; *c && n + 1 < sizeof joined; c++) {
    if(*c != ' ') {
      joined[n++] = *c;
    }
  }
  joined[n] = 0;
  if(strchr(key, ' ')) {
    len = snprintf(out, size, "(\"%s\" OR #%s)%s", key, joined, tail);
  } else {
    // one word could be an everyday one: with what posts about games say
    len = snprintf(out, size, "(#%s OR (%s (PS5 OR PlayStation OR game OR gameplay)))%s", key, key, tail);
  }
  if(len < 0 || (size_t)len >= size) {
    out[0] = 0;
    return -1;
  }
  return 0;
}


/**
 * whether the folded text t (n bytes) has word, one or more words, as words
 * of their own
 **/
static int
has_words(const char *t, size_t n, const char *word) {
  size_t len = strlen(word), pos;

  for(const char *at=strstr(t, word); at; at=strstr(at + 1, word)) {
    pos = (size_t)(at - t);
    if((!pos || t[pos - 1] == ' ') && (pos + len == n || t[pos + len] == ' ')) {
      return 1;
    }
  }
  return 0;
}


int
game_spoils(const char *text, const game_keys_t *k) {
  // folded as game_keys folds: small letters, no accents, no apostrophes
  static const char *const words[] = {
    // English
    "spoil", "spoiled", "spoiler", "spoilers", "spoilt", "ending", "endings", "true ending", "secret ending",
    "final boss", "last boss", "plot twist", "dies", "post game", "postgame", "final chapter", "last chapter",
    // Portuguese
    "spoilar", "o final", "final do jogo", "chefe final", "ultimo chefe", "morre", "morreu", "reviravolta",
    "ultimo capitulo",
    // Spanish
    "el final", "jefe final", "ultimo jefe", "muere", "murio", "giro argumental",
    // French
    "la fin", "fin du jeu", "boss final", "dernier boss", "meurt", "rebondissement", "dernier chapitre",
    // German
    "ende", "endboss", "stirbt", "wendung", "letzter boss", "letztes kapitel",
    // Italian
    "finale", "boss finale", "ultimo boss", "muore", "colpo di scena", "ultimo capitolo",
    // Dutch
    "einde", "eindbaas", "sterft", "plotwending", "laatste baas", "laatste hoofdstuk",
  };
  static char t[2048];
  const char *name = k && k->count ? k->key[0] : "";
  size_t n;

  if(!text || !text[0]) {
    return 0;
  }
  n = fold(text, t, sizeof t, NULL);
  for(size_t i=0; i < sizeof words / sizeof words[0]; i++) {
    // the game's own name says nothing ("Final Fantasy", "Death's Door")
    if(has_words(t, n, words[i]) && !has_words(name, strlen(name), words[i])) {
      return 1;
    }
  }
  return 0;
}


const char *
game_locale(int lang) {
  static const char *const codes[LANGS] = {"en-US", "pt-PT", "es-ES", "fr-FR", "de-DE", "it-IT", "nl-NL"};

  return lang >= 0 && lang < LANGS ? codes[lang] : "en-US";
}
