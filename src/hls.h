/* What a Space's sound comes as: HLS (RFC 8216), a playlist of segments of
AAC in ADTS frames, each segment led by an ID3 tag with its time. Only the
text and the bytes are read here: the playlists (a master one naming its
variants, a media one naming the segments, which a live one adds to as it
goes) and the ADTS frames of a segment, which the title's AAC decoder
takes one at a time. Every URI is made whole against the playlist's own
address; nothing here goes to the network. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define HLS_SEGMENTS 64   // of a media playlist, the newest kept
#define HLS_URL      768

typedef struct hls_media {
  long long sequence;  // EXT-X-MEDIA-SEQUENCE: the number of its first segment
  int       target_s;  // EXT-X-TARGETDURATION: the longest segment, in seconds
  int       ended;     // EXT-X-ENDLIST: no more will come (a recording)
  int       count;
  long long first;     // the number of uri[0]: sequence plus the segments left out
  int       more;      // segments after the ones kept (a long recording read from its start)
  int       ms[HLS_SEGMENTS];        // each one's length (EXTINF)
  char      uri[HLS_SEGMENTS][HLS_URL];
} hls_media_t;

/**
 * ref made whole against base (the playlist's address): as it is when it
 * has a scheme, on base's host when it starts with "/", else next to base
 * (its query left out); 0, or -1 when it does not fit or is not https
 **/
int hls_resolve(const char *base, const char *ref, char *out, size_t size);

/**
 * a master playlist's variant with the most BANDWIDTH, made whole: 0; 1
 * when text is a media playlist already (no EXT-X-STREAM-INF); -1 when it
 * is no playlist
 **/
int hls_variant(const char *text, size_t len, const char *base, char *out, size_t size);

/**
 * a media playlist's segments into m: with from below 0, the newest
 * HLS_SEGMENTS of them (live); else up to HLS_SEGMENTS from the one
 * numbered from on (a recording, read in turn); 0, or -1 when it is no
 * playlist
 **/
int hls_media(const char *text, size_t len, const char *base, long long from, hls_media_t *m);

/**
 * how many bytes at p are ID3v2 tags (with their footers), 0 for none;
 * more than len when the last one is not all in yet
 **/
size_t hls_id3(const uint8_t *p, size_t len);

/**
 * the ADTS frame at p: its whole length (header and all) into *frame and
 * 1; 0 when it does not all fit in len yet; -1 when p is no ADTS frame
 **/
int hls_adts(const uint8_t *p, size_t len, size_t *frame);
