/* Host tests for src/hls.c: addresses made whole, a master playlist's best
variant, a live media playlist and a recorded one (with more segments than
are kept), and a segment's ID3 tag and ADTS frames, whole and cut short. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hls.h"

static int g_failed;

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_failed++; } } while(0)


static void
test_resolve(void) {
  char out[HLS_URL];
  const char *base = "https://prod-fastly-eu-west-1.video.pscp.tv/Transcoding/v1/hls/abc/non_transcode/eu-west-1/periscope-replay-direct-prod-eu-west-1-public/audio-space/master_playlist.m3u8?type=live";

  CHECK(!hls_resolve(base, "playlist_16.m3u8", out, sizeof out));
  CHECK(!strcmp(out, "https://prod-fastly-eu-west-1.video.pscp.tv/Transcoding/v1/hls/abc/non_transcode/eu-west-1/periscope-replay-direct-prod-eu-west-1-public/audio-space/playlist_16.m3u8"));
  CHECK(!hls_resolve(base, "/Transcoding/v1/hls/abc/chunk_1.aac", out, sizeof out));
  CHECK(!strcmp(out, "https://prod-fastly-eu-west-1.video.pscp.tv/Transcoding/v1/hls/abc/chunk_1.aac"));
  CHECK(!hls_resolve(base, "https://other.example/a.aac", out, sizeof out));
  CHECK(!strcmp(out, "https://other.example/a.aac"));
  CHECK(!hls_resolve(base, "//cdn.example/b.aac", out, sizeof out));
  CHECK(!strcmp(out, "https://cdn.example/b.aac"));
  CHECK(!hls_resolve("https://host.example", "c.aac", out, sizeof out));
  CHECK(!strcmp(out, "https://host.example/c.aac"));
  CHECK(!hls_resolve("https://host.example?x=1", "/d.aac", out, sizeof out));
  CHECK(!strcmp(out, "https://host.example/d.aac"));
  CHECK(!hls_resolve("https://host.example/a/b/list.m3u8#f", "e.aac?t=1", out, sizeof out));
  CHECK(!strcmp(out, "https://host.example/a/b/e.aac?t=1"));
  // a relative one whose query names another address stays relative
  CHECK(!hls_resolve("https://host.example/a/", "f.aac?u=https://x", out, sizeof out));
  CHECK(!strcmp(out, "https://host.example/a/f.aac?u=https://x"));

  // only https, and only what fits
  CHECK(hls_resolve(base, "http://plain.example/a.aac", out, sizeof out) == -1);
  CHECK(!out[0]);
  CHECK(hls_resolve("http://plain.example/list.m3u8", "a.aac", out, sizeof out) == -1);
  CHECK(hls_resolve(base, "ftp://x/a.aac", out, sizeof out) == -1);
  CHECK(hls_resolve("https://h.example/", "abcdefghij", out, 20) == -1);
  CHECK(!out[0]);
  CHECK(!hls_resolve("https://h.example/", "abc", out, 23));
  CHECK(!strcmp(out, "https://h.example/abc"));
  CHECK(hls_resolve("https://h.example/", "abc", out, 21) == -1);
  CHECK(hls_resolve("https://h.example/", "abc", out, 0) == -1);
}


static void
test_variant(void) {
  const char *base = "https://v.example/space/master.m3u8";
  const char *master =
    "\xef\xbb\xbf#EXTM3U\r\n"
    "#EXT-X-VERSION:3\r\n"
    "#EXT-X-STREAM-INF:AVERAGE-BANDWIDTH=900000,BANDWIDTH=64000,CODECS=\"mp4a.40.2\"\r\n"
    "low/playlist.m3u8\r\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=128000,CODECS=\"mp4a.40.2\"\r\n"
    "\r\n"
    "# a comment\r\n"
    "high/playlist.m3u8\r\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=96000\r\n"
    "mid/playlist.m3u8";
  const char *media = "#EXTM3U\n#EXT-X-TARGETDURATION:3\n#EXTINF:3.0,\nchunk_1.aac\n";
  const char *broken = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nhttp://plain.example/a.m3u8\n";
  const char *one_bad =
    "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=10\ngood.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=99\nhttp://plain.example/a.m3u8\n";
  char out[HLS_URL];

  CHECK(!hls_variant(master, strlen(master), base, out, sizeof out));
  CHECK(!strcmp(out, "https://v.example/space/high/playlist.m3u8"));
  CHECK(hls_variant(media, strlen(media), base, out, sizeof out) == 1);
  CHECK(hls_variant("<html>", 6, base, out, sizeof out) == -1);
  CHECK(hls_variant("", 0, base, out, sizeof out) == -1);
  CHECK(hls_variant(broken, strlen(broken), base, out, sizeof out) == -1);
  CHECK(!hls_variant(one_bad, strlen(one_bad), base, out, sizeof out));
  CHECK(!strcmp(out, "https://v.example/space/good.m3u8"));
}


static void
test_media(void) {
  const char *base = "https://v.example/space/high/playlist.m3u8?token=1";
  const char *live =
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-TARGETDURATION:4\n"
    "#EXT-X-MEDIA-SEQUENCE:1207\n"
    "#EXT-X-PROGRAM-DATE-TIME:2026-11-16T10:00:00.000Z\n"
    "#EXTINF:2.005,\n"
    "chunk_1207.aac\n"
    "#EXTINF:3.5,live\n"
    "/abs/chunk_1208.aac\n"
    "#EXTINF:4,\n"
    "https://edge.example/chunk_1209.aac\n"
    "#EXTINF:3,\n"
    "http://plain.example/chunk_1210.aac\n";
  static hls_media_t m;
  static char rec[HLS_SEGMENTS * 64 + 4096];
  size_t n = 0;
  int i;

  CHECK(!hls_media(live, strlen(live), base, -1, &m));
  CHECK(m.sequence == 1207);
  CHECK(m.first == 1207);
  CHECK(m.target_s == 4);
  CHECK(!m.ended);
  CHECK(m.count == 4);
  CHECK(m.ms[0] == 2005 && m.ms[1] == 3500 && m.ms[2] == 4000 && m.ms[3] == 3000);
  CHECK(!strcmp(m.uri[0], "https://v.example/space/high/chunk_1207.aac"));
  CHECK(!strcmp(m.uri[1], "https://v.example/abs/chunk_1208.aac"));
  CHECK(!strcmp(m.uri[2], "https://edge.example/chunk_1209.aac"));
  CHECK(!m.uri[3][0]); // not https: kept in its place, empty
  CHECK(!m.more);
  // from a number on: what came since
  CHECK(!hls_media(live, strlen(live), base, 1209, &m));
  CHECK(m.first == 1209 && m.count == 2 && !strcmp(m.uri[0], "https://edge.example/chunk_1209.aac"));
  CHECK(!hls_media(live, strlen(live), base, 5, &m));
  CHECK(m.first == 1207 && m.count == 4);
  CHECK(!hls_media(live, strlen(live), base, 1300, &m));
  CHECK(m.first == 1211 && !m.count);

  // a recording longer than what is kept: the newest HLS_SEGMENTS of it
  n += (size_t)sprintf(rec + n, "#EXTM3U\n#EXT-X-TARGETDURATION:3\n#EXT-X-PLAYLIST-TYPE:VOD\n");
  for(i=0; i < HLS_SEGMENTS + 10; i++) {
    n += (size_t)sprintf(rec + n, "#EXTINF:%d.%d,\nchunk_%d.aac\n", 1 + i % 3, i % 10, i);
  }
  n += (size_t)sprintf(rec + n, "#EXT-X-ENDLIST\n");
  CHECK(!hls_media(rec, n, base, -1, &m));
  CHECK(m.sequence == 0); // none named: 0
  CHECK(m.first == 10);
  CHECK(m.count == HLS_SEGMENTS);
  CHECK(m.ended && !m.more);
  CHECK(!strcmp(m.uri[0], "https://v.example/space/high/chunk_10.aac"));
  CHECK(m.ms[0] == 2000);
  CHECK(!strcmp(m.uri[HLS_SEGMENTS - 1], "https://v.example/space/high/chunk_73.aac"));
  CHECK(m.ms[HLS_SEGMENTS - 1] == 2300);
  // read from its start, a batch at a time
  CHECK(!hls_media(rec, n, base, 0, &m));
  CHECK(m.first == 0 && m.count == HLS_SEGMENTS && m.more == 10 && m.ended);
  CHECK(!strcmp(m.uri[0], "https://v.example/space/high/chunk_0.aac") && m.ms[0] == 1000);
  CHECK(!strcmp(m.uri[HLS_SEGMENTS - 1], "https://v.example/space/high/chunk_63.aac") &&
        m.ms[HLS_SEGMENTS - 1] == 1300);
  CHECK(!hls_media(rec, n, base, HLS_SEGMENTS, &m));
  CHECK(m.first == HLS_SEGMENTS && m.count == 10 && !m.more);
  CHECK(!strcmp(m.uri[9], "https://v.example/space/high/chunk_73.aac") && m.ms[9] == 2300);

  CHECK(hls_media("chunk_1.aac\n", 12, base, -1, &m) == -1);
  CHECK(!m.count);
  CHECK(!hls_media("#EXTM3U", 7, base, 0, &m));
  CHECK(!m.count && !m.ended);
}


static void
test_frames(void) {
  uint8_t seg[256];
  size_t n = 0, at, frame = 0;
  int i, frames;

  memset(seg, 0, sizeof seg);
  // an ID3v2.4 tag of 20 bytes with a footer (10 more), then a v2.3 one of 5
  memcpy(seg, "ID3\x04\x00\x10\x00\x00\x00\x14", 10);
  n = 10 + 20 + 10;
  memcpy(seg + n, "ID3\x03\x00\x00\x00\x00\x00\x05", 10);
  n += 10 + 5;
  CHECK(hls_id3(seg, n) == n);
  CHECK(hls_id3(seg, 9) == 0); // not all of its head yet
  CHECK(hls_id3(seg, 12) == 40); // its head says how long: more than came
  CHECK(hls_id3((const uint8_t *)"\xff\xf1", 2) == 0);
  CHECK(hls_id3((const uint8_t *)"ID3\x04\x00\x00\x00\x00\x80\x00", 10) == 0); // not 7 bits
  // three ADTS frames: 7 + 13, 7 + 23, then 9 (with a CRC) + 1
  for(i=0; i < 3; i++) {
    size_t len = i == 0 ? 20 : i == 1 ? 30 : 10;
    seg[n] = 0xff;
    seg[n + 1] = i == 2 ? 0xf0 : 0xf1;
    seg[n + 2] = 0x50;
    seg[n + 3] = 0x80 | (uint8_t)(len >> 11);
    seg[n + 4] = (uint8_t)(len >> 3);
    seg[n + 5] = (uint8_t)((len & 7) << 5 | 0x1f);
    seg[n + 6] = 0xfc;
    n += len;
  }
  at = hls_id3(seg, n);
  for(frames=0; at < n; frames++) {
    CHECK(hls_adts(seg + at, n - at, &frame) == 1);
    at += frame;
  }
  CHECK(frames == 3 && at == n);
  at = hls_id3(seg, n);
  CHECK(hls_adts(seg + at, 19, &frame) == 0); // cut short
  CHECK(hls_adts(seg + at, 3, &frame) == 0);
  CHECK(hls_adts(seg + at, 0, &frame) == 0);
  CHECK(hls_adts((const uint8_t *)"ID3", 3, &frame) == -1);
  CHECK(hls_adts((const uint8_t *)"\xff\xf7", 2, &frame) == -1); // a layer that is not 0
  CHECK(hls_adts((const uint8_t *)"\xff\xf1\x50\x80\x00\xbf\xfc", 7, &frame) == -1); // 5 bytes long: less than its head
}


int
main(void) {
  test_resolve();
  test_variant();
  test_media();
  test_frames();
  if(g_failed) {
    printf("hls_test: %d failed\n", g_failed);
    return 1;
  }
  printf("hls_test: all passed\n");
  return 0;
}
