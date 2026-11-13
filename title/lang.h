/* The app's languages (LANG_* of settings.h): the screen's texts are
written in Portuguese and English in ui.c, L("pt", "en"); the others come
from a table keyed by the English text (lang_table.h). A text the table
lacks shows in English. */

#pragma once

#include <stddef.h>

#include "settings.h"

/**
 * the text in that language
 **/
const char *lang_text(int lang, const char *pt, const char *en);

/**
 * the language's name in itself ("Português", "Deutsch", ...)
 **/
const char *lang_name(int lang);

/**
 * a day as x.com writes it: "Nov 3" or "3 de nov", with the year when
 * year is not 0
 **/
void lang_date(int lang, int day, int month, int year, char *buf, size_t size);

/**
 * a month's short name (0 is January)
 **/
const char *lang_month(int lang, int month);

/**
 * a count's short units (thousands, millions) and its decimal mark
 **/
const char *lang_unit(int lang, int millions);
char lang_decimal(int lang);

/**
 * a count's word, singular or plural: what is 0 replies, 1 reposts, 2
 * likes, 3 views
 **/
const char *lang_count_word(int lang, int what, int plural);
