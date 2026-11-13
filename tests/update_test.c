/* Host tests of src/update.c: update.json, versions and the file's address. */

#include <stdio.h>
#include <string.h>

#include "update.h"

static int g_failed;

#define CHECK(cond) do { \
  if(!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); g_failed = 1; } \
} while(0)


/**
 * update_parse of text, from a copy it may change
 **/
static int
parse(const char *text, update_t *u) {
  static char buf[1024];

  snprintf(buf, sizeof buf, "%s", text);
  return update_parse(buf, strlen(buf), u);
}


int
main(void) {
  static const char sha[] = "00112233445566778899aabbccddeeff00112233445566778899AABBCCDDEEFF";
  char text[512], url[128];
  update_t u;

  snprintf(text, sizeof text, "{\"version\":\"1.10.2\",\"file\":\"XPS5.elf\",\"size\":21378312,"
           "\"sha256\":\"%s\",\"notes\":\"anything\"}", sha);
  CHECK(!parse(text, &u) && !strcmp(u.version, "1.10.2") && !strcmp(u.file, "XPS5.elf") &&
        u.size == 21378312 && u.sha256[0] == 0x00 && u.sha256[1] == 0x11 && u.sha256[15] == 0xff &&
        u.sha256[31] == 0xff);
  // what is refused, and leaves nothing behind
  snprintf(text, sizeof text, "{\"version\":\"1.1\",\"file\":\"../x.elf\",\"size\":10,\"sha256\":\"%s\"}", sha);
  CHECK(parse(text, &u) == -1 && !u.version[0] && !u.size);
  snprintf(text, sizeof text, "{\"version\":\"1.1\",\"file\":\"x.bin\",\"size\":10,\"sha256\":\"%s\"}", sha);
  CHECK(parse(text, &u) == -1);
  snprintf(text, sizeof text, "{\"version\":\"1.1\",\"file\":\".x.elf\",\"size\":10,\"sha256\":\"%s\"}", sha);
  CHECK(parse(text, &u) == -1);
  snprintf(text, sizeof text, "{\"version\":\"v1.1\",\"file\":\"x.elf\",\"size\":10,\"sha256\":\"%s\"}", sha);
  CHECK(parse(text, &u) == -1);
  snprintf(text, sizeof text, "{\"version\":\"1.1\",\"file\":\"x.elf\",\"size\":99999999999,\"sha256\":\"%s\"}", sha);
  CHECK(parse(text, &u) == -1);
  snprintf(text, sizeof text, "{\"version\":\"1.1\",\"file\":\"x.elf\",\"size\":-5,\"sha256\":\"%s\"}", sha);
  CHECK(parse(text, &u) == -1);
  snprintf(text, sizeof text, "{\"version\":\"1.1\",\"file\":\"x.elf\",\"size\":1.5,\"sha256\":\"%s\"}", sha);
  CHECK(parse(text, &u) == -1);
  snprintf(text, sizeof text, "{\"version\":\"1.1\",\"file\":\"x.elf\",\"size\":\"10\",\"sha256\":\"%s\"}", sha);
  CHECK(parse(text, &u) == -1);
  CHECK(parse("{\"version\":\"1.1\",\"file\":\"x.elf\",\"size\":10,\"sha256\":\"0011\"}", &u) == -1);
  snprintf(text, sizeof text, "{\"version\":\"1.1\",\"file\":\"x.elf\",\"size\":10,\"sha256\":\"%.63sg\"}", sha);
  CHECK(parse(text, &u) == -1);
  CHECK(parse("[1,2", &u) == -1 && parse("{}", &u) == -1);

  // versions
  CHECK(update_newer("1.0.0", "1.0.1") && update_newer("1.9.2", "1.10.0") && update_newer("1.0", "1.0.1") &&
        update_newer("1.0.0", "2"));
  CHECK(!update_newer("1.0.0", "1.0.0") && !update_newer("1.0.1", "1.0") && !update_newer("2.0", "1.99.99"));
  CHECK(!update_newer("1.0.0", "1.0.0-beta") && !update_newer("1.0.0", "") && !update_newer("x", "2") &&
        !update_newer("1.0.0", "1..2") && !update_newer("1.0.0", "1.2.") && !update_newer("1.0.0", "123456") &&
        !update_newer("1.0.0", "1.2.3.4.5"));
  CHECK(update_newer(XPS5_VERSION, "99.0"));

  // the file beside update.json
  CHECK(!update_file_url("https://someone.github.io/xps5/update.json", "XPS5.elf", url, sizeof url) &&
        !strcmp(url, "https://someone.github.io/xps5/XPS5.elf"));
  CHECK(!update_file_url("https://someone.github.io/update.json", "a.elf", url, sizeof url) &&
        !strcmp(url, "https://someone.github.io/a.elf"));
  CHECK(update_file_url("http://someone.github.io/update.json", "a.elf", url, sizeof url) == -1 && !url[0]);
  CHECK(update_file_url("https://someone.github.io", "a.elf", url, sizeof url) == -1);
  CHECK(update_file_url("https:///update.json", "a.elf", url, sizeof url) == -1);
  CHECK(update_file_url("https://a.io/u.json?x=1", "a.elf", url, sizeof url) == -1);
  CHECK(update_file_url("https://a.io/u.json", "../a.elf", url, sizeof url) == -1);
  CHECK(update_file_url("https://a.io/u.json", "a.elf", url, 16) == -1);
  CHECK(update_file_url("", "a.elf", url, sizeof url) == -1);

  if(g_failed) {
    return 1;
  }
  printf("all update checks passed\n");
  return 0;
}
