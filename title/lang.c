/* The app's languages, see lang.h. */

#include <stdio.h>
#include <string.h>

#include "lang.h"

// a text of ui.c, keyed by its English, in Spanish, French, German, Italian
// and Dutch
typedef struct entry {
  const char *en;
  const char *t[5];
} entry_t;

static const entry_t g_table[] = {
#include "lang_table.h"
};


const char *
lang_text(int lang, const char *pt, const char *en) {
  if(lang == LANG_PT) {
    return pt;
  }
  if(lang < LANG_ES || lang >= LANGS) {
    return en;
  }
  for(size_t i=0; i < sizeof g_table / sizeof g_table[0]; i++) {
    if(!strcmp(g_table[i].en, en)) {
      return g_table[i].t[lang - LANG_ES] ? g_table[i].t[lang - LANG_ES] : en;
    }
  }
  return en;
}


const char *
lang_name(int lang) {
  static const char *const names[LANGS] = {
    "English", "Portugu\xc3\xaas", "Espa\xc3\xb1ol", ("Fran\xc3\xa7" "ais"), "Deutsch", "Italiano", "Nederlands",
  };
  return lang >= 0 && lang < LANGS ? names[lang] : names[0];
}


static const char *const g_months[LANGS][12] = {
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
    {"jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez"},
    {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sept", "oct", "nov", "dic"},
    {"janv.", "f\xc3\xa9vr.", "mars", "avr.", "mai", "juin", "juil.", "ao\xc3\xbbt", "sept.", "oct.", "nov.",
     ("d\xc3\xa9" "c.")},
    {"Jan.", "Feb.", "M\xc3\xa4rz", "Apr.", "Mai", "Juni", "Juli", "Aug.", "Sept.", "Okt.", "Nov.", "Dez."},
    {"gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic"},
    {"jan", "feb", "mrt", "apr", "mei", "jun", "jul", "aug", "sep", "okt", "nov", "dec"},
};


const char *
lang_month(int lang, int month) {
  lang = lang >= 0 && lang < LANGS ? lang : LANG_EN;
  return g_months[lang][((month % 12) + 12) % 12];
}


void
lang_date(int lang, int day, int month, int year, char *buf, size_t size) {
  const char *m = lang_month(lang, month);

  lang = lang >= 0 && lang < LANGS ? lang : LANG_EN;
  switch(lang) {
  case LANG_EN:
    if(year) {
      snprintf(buf, size, "%s %d, %d", m, day, year);
    } else {
      snprintf(buf, size, "%s %d", m, day);
    }
    break;
  case LANG_PT:
    if(year) {
      snprintf(buf, size, "%d de %s de %d", day, m, year);
    } else {
      snprintf(buf, size, "%d de %s", day, m);
    }
    break;
  case LANG_DE:
    if(year) {
      snprintf(buf, size, "%d. %s %d", day, m, year);
    } else {
      snprintf(buf, size, "%d. %s", day, m);
    }
    break;
  default:
    if(year) {
      snprintf(buf, size, "%d %s %d", day, m, year);
    } else {
      snprintf(buf, size, "%d %s", day, m);
    }
  }
}


const char *
lang_unit(int lang, int millions) {
  static const char *const units[LANGS][2] = {
    {"K", "M"}, {" mil", " M"}, {" mil", " M"}, {" k", " M"}, {" Tsd.", " Mio."}, {"K", " Mln"},
    {"K", " mln"},
  };
  return units[lang >= 0 && lang < LANGS ? lang : 0][!!millions];
}


char
lang_decimal(int lang) {
  return lang == LANG_EN || lang < 0 || lang >= LANGS ? '.' : ',';
}


const char *
lang_count_word(int lang, int what, int plural) {
  static const char *const words[LANGS][4][2] = {
    {{"reply", "replies"}, {"repost", "reposts"}, {"like", "likes"}, {"view", "views"}},
    {{"resposta", "respostas"}, {"repost", "reposts"}, {"gosto", "gostos"},
     {"visualiza\xc3\xa7\xc3\xa3o", ("visualiza\xc3\xa7\xc3\xb5" "es")}},
    {{"respuesta", "respuestas"}, {"repost", "reposts"}, {"Me gusta", "Me gusta"},
     {"visualizaci\xc3\xb3n", "visualizaciones"}},
    {{"r\xc3\xa9ponse", "r\xc3\xa9ponses"}, {"repost", "reposts"}, {"J'aime", "J'aime"}, {"vue", "vues"}},
    {{"Antwort", "Antworten"}, {"Repost", "Reposts"}, {"Like", "Likes"}, {"Aufruf", "Aufrufe"}},
    {{"risposta", "risposte"}, {"repost", "repost"}, {"Mi piace", "Mi piace"},
     {"visualizzazione", "visualizzazioni"}},
    {{"antwoord", "antwoorden"}, {"repost", "reposts"}, {"like", "likes"}, {"weergave", "weergaven"}},
  };
  lang = lang >= 0 && lang < LANGS ? lang : 0;
  what = what >= 0 && what < 4 ? what : 0;
  return words[lang][what][!!plural];
}
