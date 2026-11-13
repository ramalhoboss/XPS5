/* Host tests of src/trophy.c, on files made here the way the PS5 lays
them out (trophy.h): a game's npbind.dat, a set's TROPHY.UCP with its
tropconf.json and tropmeta files, and the user's TRPTITLE.DAT. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trophy.h"

static int g_failed;

#define CHECK(cond) do { \
  if(!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); g_failed = 1; } \
} while(0)


static void
put16(unsigned char *p, unsigned v) {
  p[0] = (unsigned char)(v >> 8);
  p[1] = (unsigned char)v;
}


static void
put32(unsigned char *p, unsigned v) {
  put16(p, v >> 16);
  put16(p + 2, v & 0xffff);
}


static void
put64(unsigned char *p, unsigned long long v) {
  put32(p, (unsigned)(v >> 32));
  put32(p + 4, (unsigned)v);
}


/**
 * text in a buffer of its own, which the JSON reader may change
 **/
static char *
copy(const char *text) {
  static char buf[1024];

  snprintf(buf, sizeof buf, "%s", text);
  return buf;
}


static void
test_npbind(void) {
  unsigned char d[256];
  char set[TROPHY_SET_LEN];

  memset(d, 0, sizeof d);
  put32(d, 0xd294a018u);
  // another record first, then the set's
  put16(d + 0x80, 0x0001);
  put16(d + 0x82, 4);
  put16(d + 0x88, 0x0010);
  put16(d + 0x8a, 16);
  memcpy(d + 0x8c, "NPWR12345_00", 12);
  CHECK(!trophy_set_of(d, sizeof d, set) && !strcmp(set, "NPWR12345_00"));
  memcpy(d + 0x8c, "NPWR1234X_00", 12);
  CHECK(trophy_set_of(d, sizeof d, set) == -1 && !set[0]);
  memcpy(d + 0x8c, "NPWR12345_00", 12);
  put16(d + 0x8a, 200); // past the end
  CHECK(trophy_set_of(d, sizeof d, set) == -1);
  put32(d, 0x12345678u);
  CHECK(trophy_set_of(d, sizeof d, set) == -1);
  CHECK(trophy_set_of(d, 0x80, set) == -1);
}


/**
 * a TRPTITLE.DAT of two pages whose rows say the trophies earned (ids in
 * have, the others not)
 **/
static size_t
make_record(unsigned char *out, const int *have, int n_have, int rows) {
  enum { HEADER = 64, PAGE = 1024, STRIDE = 1056, PAGES = 2 };
  static unsigned char stream[PAGE * PAGES];
  const unsigned payload = 0x30, offset = 48 + 32 * 2;

  memset(stream, 0, sizeof stream);
  memcpy(stream, "T2TD", 4);
  put32(stream + 4, 0x10000);
  put32(stream + 8, 2);
  // a directory entry of something else, then the states'
  put32(stream + 48, 0x400);
  put32(stream + 80, 0x800);
  put32(stream + 84, payload);
  put32(stream + 88, 1);
  put32(stream + 92, (unsigned)rows);
  put64(stream + 96, offset);
  for(int r=0; r < rows && offset + (r + 1) * (16 + payload) <= sizeof stream; r++) { // the rows that fit
    unsigned char *row = stream + offset + r * (16 + payload);
    int got = 0;
    for(int i=0; i < n_have; i++) {
      got |= have[i] == r;
    }
    put32(row, 0x800);
    put32(row + 0x10, (unsigned)r);
    put32(row + 0x14, got ? 0x11 : 0x00);
    if(got) {
      put64(row + 0x20, (62135596800ull + 1794500000ull) * 1000000ull);
      put64(row + 0x30, (62135596800ull + 1794500000ull) * 1000000ull);
    }
  }
  memset(out, 0, HEADER + STRIDE * PAGES);
  memcpy(out, "T2PD", 4);
  put32(out + 4, 0x10000);
  put64(out + 8, STRIDE);
  put64(out + 32, PAGES);
  for(int p=0; p < PAGES; p++) {
    memcpy(out + HEADER + p * STRIDE, stream + p * PAGE, PAGE);
  }
  return HEADER + STRIDE * PAGES;
}


static void
test_record(void) {
  static unsigned char d[64 + 1056 * 2];
  static const int have[] = {0, 3, 17};
  trophy_earned_t e;
  size_t len = make_record(d, have, 3, 20);

  CHECK(trophy_earned(d, len, &e) == 3);
  CHECK(trophy_has(&e, 0) && trophy_has(&e, 3) && trophy_has(&e, 17) && !trophy_has(&e, 1) && !trophy_has(&e, 19));
  CHECK(!trophy_has(&e, -1) && !trophy_has(&e, TROPHY_IDS));
  // rows that reach into the second page
  len = make_record(d, have, 3, 25);
  CHECK(trophy_earned(d, len, &e) == 3);
  CHECK(trophy_earned(d, len - 1, &e) == -1 && !e.count);
  d[64 + 1056 + 10] ^= 0xff; // the pages' SHA-256 is not checked: still read
  CHECK(trophy_earned(d, len, &e) == 3);
  CHECK(trophy_earned(d, make_record(d, have, 3, 40), &e) == -1); // more rows than the pages hold
  len = make_record(d, have, 3, 25);
  d[64 + 112] = 0x08; // the first row's type spoiled
  CHECK(trophy_earned(d, len, &e) == -1);
  memcpy(d, "T2PX", 4);
  CHECK(trophy_earned(d, len, &e) == -1);
}


static void
test_set(void) {
  static unsigned char ucp[TROPHY_UCP_HEAD + 4 * TROPHY_UCP_ENTRY];
  const unsigned char *table = ucp + TROPHY_UCP_HEAD;
  static const char *const names[] = {"tropconf.json", "tropmeta_fr-FR.json", "tropmeta_en-US.json", "icon000.png"};
  unsigned long long off = 0, size = 0;
  char conf[] = "{\"defaultLanguage\":\"en-US\",\"trophies\":[{\"id\":0,\"grade\":\"P\",\"hidden\":false},"
                "{\"id\":\"1\",\"grade\":\"G\"},{\"id\":2,\"grade\":\"S\"},{\"id\":3,\"grade\":\"B\",\"hidden\":true}]}";
  char meta[] = "{\"metadata\":{\"titleMetadata\":{\"name\":\" ASTRO BOT \"},\"trophyMetadata\":["
                "{\"id\":0,\"name\":\"Bot Master\"},{\"id\":3,\"name\":\"Hello\\tWorld\\u00e9\",\"detail\":\"x\"}]}}";
  char file[48], name[TROPHY_NAME], game[TROPHY_NAME], small[8];
  int count;

  memset(ucp, 0, sizeof ucp);
  put32(ucp, 0xb228c60au);
  put32(ucp + 0x10, 4);
  put32(ucp + 0x14, TROPHY_UCP_ENTRY);
  for(int i=0; i < 4; i++) {
    memcpy(ucp + TROPHY_UCP_HEAD + i * TROPHY_UCP_ENTRY, names[i], strlen(names[i]));
    put64(ucp + TROPHY_UCP_HEAD + i * TROPHY_UCP_ENTRY + 32, 0x1000ull * (i + 1));
    put64(ucp + TROPHY_UCP_HEAD + i * TROPHY_UCP_ENTRY + 40, 100ull + i);
  }
  CHECK((count=trophy_ucp_files(ucp, TROPHY_UCP_HEAD)) == 4);
  CHECK(!trophy_ucp_find(table, 4 * TROPHY_UCP_ENTRY, count, "tropconf.json", &off, &size) && off == 0x1000 &&
        size == 100);
  CHECK(trophy_ucp_find(table, 4 * TROPHY_UCP_ENTRY, count, "tropmeta_pt-PT.json", &off, &size) == -1);
  CHECK(trophy_ucp_find(table, 3 * TROPHY_UCP_ENTRY, count, "tropconf.json", &off, &size) == -1); // table cut
  CHECK(!trophy_ucp_meta(table, 4 * TROPHY_UCP_ENTRY, count, "fr-FR", file, sizeof file) &&
        !strcmp(file, "tropmeta_fr-FR.json"));
  CHECK(!trophy_ucp_meta(table, 4 * TROPHY_UCP_ENTRY, count, "pt-PT", file, sizeof file) &&
        !strcmp(file, "tropmeta_en-US.json"));
  put32(ucp + 0x14, 32);
  CHECK(trophy_ucp_files(ucp, TROPHY_UCP_HEAD) == -1);
  put32(ucp + 0x14, TROPHY_UCP_ENTRY);
  ucp[TROPHY_UCP_HEAD + 3] = '/';
  CHECK(trophy_ucp_find(table, 4 * TROPHY_UCP_ENTRY, count, "tropconf.json", &off, &size) == -1);

  CHECK(trophy_grade(copy(conf), strlen(conf), 0) == TROPHY_PLATINUM);
  CHECK(trophy_grade(copy(conf), strlen(conf), 1) == TROPHY_GOLD);
  CHECK(trophy_grade(copy(conf), strlen(conf), 2) == TROPHY_SILVER);
  CHECK(trophy_grade(copy(conf), strlen(conf), 3) == TROPHY_BRONZE);
  CHECK(trophy_grade(copy(conf), strlen(conf), 4) == -1);
  CHECK(trophy_grade(copy("[1,2"), 4, 0) == -1);

  CHECK(!trophy_names(copy(meta), strlen(meta), 0, name, sizeof name, game, sizeof game) &&
        !strcmp(name, "Bot Master") && !strcmp(game, "ASTRO BOT"));
  CHECK(!trophy_names(copy(meta), strlen(meta), 3, name, sizeof name, game, sizeof game) &&
        !strcmp(name, "Hello World\xc3\xa9"));
  // cut at a whole character
  CHECK(!trophy_names(copy(meta), strlen(meta), 3, small, sizeof small, game, sizeof game) &&
        !strcmp(small, "Hello W"));
  CHECK(!trophy_names(copy(meta), strlen(meta), 3, name, 13, game, sizeof game) && !strcmp(name, "Hello World"));
  CHECK(trophy_names(copy(meta), strlen(meta), 1, name, sizeof name, game, sizeof game) == -1 && !name[0]);
}


int
main(void) {
  test_npbind();
  test_record();
  test_set();
  if(g_failed) {
    return 1;
  }
  printf("all trophy checks passed\n");
  return 0;
}
