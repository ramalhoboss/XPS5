/* The PS5's trophies, only read: which set a game uses (its npbind.dat),
which of the set's trophies the user has (TRPTITLE.DAT), and a trophy's
grade and name (tropconf.json and a tropmeta_<language>.json inside the
set's TROPHY.UCP). How these files are laid out was worked out on a console
by OpenPSApp (github.com/marcelorsdd/OpenPSApp, GPL-3.0); this is its own
reading of them. Nothing here opens a file, so tests/trophy_test.c runs it on
the host; src/notify.c reads the files:
  /user/appmeta/<title id>/trophy2/npbind.dat
  /user/trophy2/nobackup/conf/<set>/TROPHY.UCP
  /user/home/<user id>/trophy2/nobackup/data/<set>/TRPTITLE.DAT */

#pragma once

#include <stddef.h>

#define TROPHY_SET_LEN     16        // "NPWR12345_00" and its NUL
#define TROPHY_IDS         1024      // the trophy ids looked at, 0 to TROPHY_IDS - 1
#define TROPHY_NAME        128       // bytes of a name kept
#define TROPHY_UCP_HEAD    0x60      // a TROPHY.UCP's header; its table follows
#define TROPHY_UCP_ENTRY   64        // bytes of an entry of the table
#define TROPHY_UCP_MOST    4096      // files a TROPHY.UCP may hold
#define TROPHY_RECORD_MOST (17 << 20) // bytes of a TRPTITLE.DAT read
#define TROPHY_FILE_MOST   (4 << 20)  // bytes of a JSON file of a set read

enum { TROPHY_BRONZE, TROPHY_SILVER, TROPHY_GOLD, TROPHY_PLATINUM };

typedef struct trophy_earned {
  unsigned char bits[TROPHY_IDS / 8]; // by trophy id
  int           count;
} trophy_earned_t;

/**
 * the trophy set an npbind.dat names ("NPWR12345_00") into set; 0, or -1
 * when it names none
 **/
int trophy_set_of(const unsigned char *data, size_t len, char set[TROPHY_SET_LEN]);

/**
 * the trophies a TRPTITLE.DAT says the user has; returns how many, or -1
 * when it is not one (or was being written)
 **/
int trophy_earned(const unsigned char *data, size_t len, trophy_earned_t *e);

/**
 * whether e has the trophy id
 **/
int trophy_has(const trophy_earned_t *e, int id);

/**
 * how many files a TROPHY.UCP holds, from its first TROPHY_UCP_HEAD bytes
 * (its table, after them, is that many entries long), or -1
 **/
int trophy_ucp_files(const unsigned char *head, size_t len);

/**
 * where the file name sits in the TROPHY.UCP whose table of count entries
 * is given; 0, or -1 when it has none
 **/
int trophy_ucp_find(const unsigned char *table, size_t len, int count, const char *name,
                    unsigned long long *off, unsigned long long *size);

/**
 * the file with the trophies' names to read: tropmeta_<locale>.json
 * ("pt-PT"), else in English, else any; 0, or -1 when the set has none
 **/
int trophy_ucp_meta(const unsigned char *table, size_t len, int count, const char *locale, char *name,
                    size_t size);

/**
 * a trophy's grade (TROPHY_*) in a tropconf.json (len bytes, changed in
 * place), or -1
 **/
int trophy_grade(char *conf, size_t len, int id);

/**
 * a trophy's name and the game's in a tropmeta file (len bytes, changed in
 * place), with no control characters; 0, or -1 when it does not name it
 **/
int trophy_names(char *meta, size_t len, int id, char *name, size_t size, char *game, size_t game_size);
