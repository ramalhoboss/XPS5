/* Host tests for src/spacecmd.c: the lines a Space goes between the title
and the payload in. A play made and read back, the parts that may not go
(no https, a space or a control in the address, names that are not names,
number 0, a line that does not fit), the answer made and read back, and
lines that are no answer (an older payload's "on 24"). */

#include <stdio.h>
#include <string.h>

#include "spacecmd.h"

static int g_failed;

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_failed++; } } while(0)

static const char *const URL =
  "https://prod-fastly-eu-west-1.video.pscp.tv/Transcoding/v1/hls/abc/non_transcode/eu-west-1/"
  "periscope-replay-direct-prod-eu-west-1-public/audio-space/master_playlist.m3u8?type=live";


static void
test_play(void) {
  char line[SPACE_URL + 96], id[SPACE_NAME], host[SPACE_NAME], url[SPACE_URL];
  unsigned n;
  int live, len;

  len = space_cmd_play(line, sizeof line, 77, 1, "1OdKrBnaEPXKX", "astro_fan", URL);
  CHECK(len > 0 && (size_t)len == strlen(line));
  CHECK(!strncmp(line, "space play 77 1 1OdKrBnaEPXKX astro_fan https://", 48));
  CHECK(line[len - 1] == '\n');
  CHECK(!space_cmd_play_args(line + 11, &n, &live, id, host, url));
  CHECK(n == 77 && live == 1 && !strcmp(id, "1OdKrBnaEPXKX") && !strcmp(host, "astro_fan") && !strcmp(url, URL));

  // no id or host known: "-", read back empty
  len = space_cmd_play(line, sizeof line, 5, 0, "", NULL, URL);
  CHECK(len > 0 && !strncmp(line, "space play 5 0 - - https://", 27));
  CHECK(!space_cmd_play_args(line + 11, &n, &live, id, host, url));
  CHECK(n == 5 && live == 0 && !id[0] && !host[0] && !strcmp(url, URL));

  // what may not go
  CHECK(space_cmd_play(line, sizeof line, 0, 1, "a", "b", URL) == -1);
  CHECK(space_cmd_play(line, sizeof line, 1, 1, "a", "b", "http://plain.example/a.m3u8") == -1);
  CHECK(space_cmd_play(line, sizeof line, 1, 1, "a", "b", "https://host.example/a b.m3u8") == -1);
  CHECK(space_cmd_play(line, sizeof line, 1, 1, "a", "b", "https://host.example/a\n.m3u8") == -1);
  CHECK(space_cmd_play(line, sizeof line, 1, 1, "a b", "b", URL) == -1);
  CHECK(space_cmd_play(line, sizeof line, 1, 1, "a", "b\n", URL) == -1);
  CHECK(space_cmd_play(line, sizeof line, 1, 1, "a", "0123456789012345678901234567890123", URL) == -1);
  CHECK(space_cmd_play(line, sizeof line, 1, 1, "a", "b", NULL) == -1);
  // too long for the line: nothing of the address is left in it
  CHECK(space_cmd_play(line, 60, 1, 1, "a", "b", URL) == -1);
  CHECK(!line[0] && !memcmp(line, line + 1, 59));

  // read back: only what space_cmd_play makes
  CHECK(space_cmd_play_args("0 1 a b https://h.example/p.m3u8", &n, &live, id, host, url) == -1);
  CHECK(space_cmd_play_args("3 2 a b https://h.example/p.m3u8", &n, &live, id, host, url) == -1);
  CHECK(space_cmd_play_args("3 1 a b http://h.example/p.m3u8", &n, &live, id, host, url) == -1);
  CHECK(space_cmd_play_args("3 1 a b https://h.example/p.m3u8 more", &n, &live, id, host, url) == -1);
  CHECK(space_cmd_play_args("3 1 a.b c https://h.example/p.m3u8", &n, &live, id, host, url) == -1);
  CHECK(!url[0] && !id[0] && !host[0]);
  CHECK(space_cmd_play_args("3 1 a b", &n, &live, id, host, url) == -1);
  CHECK(!space_cmd_play_args("3 1 a b https://h.example/p.m3u8\n", &n, &live, id, host, url));
  CHECK(!space_cmd_play_args("3 1 a b https://h.example/p.m3u8\r\n", &n, &live, id, host, url));
}


static void
test_line(void) {
  char line[SPACE_LINE], id[SPACE_NAME], host[SPACE_NAME];
  space_status_t s = {SPACE_ON, SPACE_ERR_NONE, 0, 83000, 1, 0}, t;
  unsigned n;
  int live, len;

  len = space_line_put(line, sizeof line, 77, &s, 1, "1OdKrBnaEPXKX", "astro_fan");
  CHECK(len > 0 && !strcmp(line, "space 77 2 0 0 83000 1 0 1 1OdKrBnaEPXKX astro_fan\n"));
  CHECK(!space_line_get(line, &n, &t, &live, id, host));
  CHECK(n == 77 && t.state == SPACE_ON && t.error == 0 && t.heard_ms == 83000 && t.waiting == 1 && !t.hushed);
  CHECK(live == 1 && !strcmp(id, "1OdKrBnaEPXKX") && !strcmp(host, "astro_fan"));

  // nothing playing, and a failure with its code
  memset(&s, 0, sizeof s);
  CHECK(space_line_put(line, sizeof line, 0, &s, 0, NULL, "") > 0 && !strcmp(line, "space 0 0 0 0 0 0 0 0 - -\n"));
  CHECK(!space_line_get(line, &n, &t, &live, id, host) && !n && t.state == SPACE_IDLE && !id[0] && !host[0]);
  s.state = SPACE_FAILED;
  s.error = SPACE_ERR_AUDIO;
  s.code = (int)0x80260011u;
  CHECK(space_line_put(line, sizeof line, 9, &s, 0, "x", "y") > 0);
  CHECK(!space_line_get(line, &n, &t, &live, id, host) && t.error == SPACE_ERR_AUDIO && t.code == (int)0x80260011u);
  // names that are not names go as "-"
  CHECK(space_line_put(line, sizeof line, 9, &s, 0, "a b", "y") > 0 && strstr(line, " - -\n"));

  // no answer of a Space
  CHECK(space_line_get("on 24\n", &n, &t, &live, id, host) == -1);
  CHECK(space_line_get("", &n, &t, &live, id, host) == -1);
  CHECK(space_line_get("space 1 2 0 0 5 0 0 1 a\n", &n, &t, &live, id, host) == -1);
  CHECK(space_line_get("space 1 9 0 0 5 0 0 1 a b\n", &n, &t, &live, id, host) == -1);
  CHECK(space_line_get("space 1 2 99 0 5 0 0 1 a b\n", &n, &t, &live, id, host) == -1);
  CHECK(space_line_get("space 1 2 0 0 -5 0 0 1 a b\n", &n, &t, &live, id, host) == -1);
  CHECK(space_line_get("space 1 2 0 0 5 0 0 1 a b c\n", &n, &t, &live, id, host) == -1);
  CHECK(space_line_get("space 1 2 0 0 5 0 0 1 a/b c\n", &n, &t, &live, id, host) == -1);
  CHECK(t.state == SPACE_IDLE && !id[0]);
}


int
main(void) {
  test_play();
  test_line();
  if(g_failed) {
    printf("spacecmd: %d failed\n", g_failed);
    return 1;
  }
  printf("spacecmd: ok\n");
  return 0;
}
