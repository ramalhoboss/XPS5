/* HLS playlists and ADTS frames, see hls.h. */

#include <stdlib.h>
#include <string.h>

#include "hls.h"


/**
 * the next line of text from *at: where it starts and how long it is
 * (without its \r\n); 0 at the end
 **/
static int
next_line(const char *text, size_t len, size_t *at, const char **line, size_t *n) {
  size_t i = *at, e;

  if(i >= len) {
    return 0;
  }
  for(e=i; e < len && text[e] != '\n'; e++) {
  }
  *line = text + i;
  *n = e - i;
  if(*n && text[e - 1] == '\r') {
    (*n)--;
  }
  *at = e < len ? e + 1 : len;
  return 1;
}


static int
starts(const char *line, size_t n, const char *with) {
  size_t k = strlen(with);

  return n >= k && !memcmp(line, with, k);
}


/**
 * the whole number at s (n bytes), or -1
 **/
static long long
number(const char *s, size_t n) {
  long long v = 0;
  size_t i;

  for(i=0; i < n && s[i] >= '0' && s[i] <= '9' && v < 100000000000000LL; i++) {
    v = v * 10 + (s[i] - '0');
  }
  return i ? v : -1;
}


static int
is_playlist(const char *text, size_t len) {
  if(len >= 3 && !memcmp(text, "\xef\xbb\xbf", 3)) {
    text += 3; // a byte order mark
    len -= 3;
  }
  return starts(text, len, "#EXTM3U");
}


int
hls_resolve(const char *base, const char *ref, char *out, size_t size) {
  const char *host, *path, *end;
  size_t keep, rlen = strlen(ref);

  if(!size) {
    return -1;
  }
  out[0] = 0;
  if(!strncmp(ref, "https://", 8)) {
    keep = 0;
    base = "";
  } else if(!strncmp(ref, "//", 2)) {
    if(rlen + 6 >= size) {
      return -1;
    }
    memcpy(out, "https:", 6);
    memcpy(out + 6, ref, rlen + 1);
    return 0;
  } else if(strstr(ref, "://") && strcspn(ref, "/?#") > (size_t)(strstr(ref, "://") - ref)) {
    return -1; // another scheme (http://, data:// …)
  } else {
    if(strncmp(base, "https://", 8)) {
      return -1;
    }
    host = base + 8;
    path = host + strcspn(host, "/?#");
    if(ref[0] == '/') {
      keep = (size_t)(path - base);
    } else {
      // next to base: to its last "/" before its query
      end = path + strcspn(path, "?#");
      while(end > path && end[-1] != '/') {
        end--;
      }
      keep = (size_t)(end - base);
      if(end == path) {
        // no path at all: "https://host" and "/"
        if(keep + 1 + rlen >= size) {
          return -1;
        }
        memcpy(out, base, keep);
        out[keep] = '/';
        memcpy(out + keep + 1, ref, rlen + 1);
        return 0;
      }
    }
  }
  if(keep + rlen >= size) {
    out[0] = 0;
    return -1;
  }
  memcpy(out, base, keep);
  memcpy(out + keep, ref, rlen + 1);
  return 0;
}


int
hls_variant(const char *text, size_t len, const char *base, char *out, size_t size) {
  const char *line, *b;
  size_t at = 0, n;
  long long best = -1, bw = -1;
  int found = 0, pending = 0;
  char ref[HLS_URL], url[HLS_URL];

  out[0] = 0;
  if(!is_playlist(text, len)) {
    return -1;
  }
  while(next_line(text, len, &at, &line, &n)) {
    if(starts(line, n, "#EXT-X-STREAM-INF:")) {
      found = 1;
      pending = 1;
      bw = 0;
      for(b=line; b + 10 <= line + n; b++) {
        // BANDWIDTH=, not AVERAGE-BANDWIDTH=
        if(!memcmp(b, "BANDWIDTH=", 10) && (b == line || b[-1] == ':' || b[-1] == ',')) {
          bw = number(b + 10, (size_t)(line + n - b - 10));
          break;
        }
      }
    } else if(n && line[0] != '#' && pending) {
      pending = 0;
      if(bw > best && n < sizeof ref) {
        memcpy(ref, line, n);
        ref[n] = 0;
        // into url first, so one that does not resolve keeps the best before it
        if(!hls_resolve(base, ref, url, sizeof url) && strlen(url) < size) {
          memcpy(out, url, strlen(url) + 1);
          best = bw;
        }
      }
    }
  }
  if(!found) {
    return 1;
  }
  return best >= 0 ? 0 : -1;
}


int
hls_media(const char *text, size_t len, const char *base, long long from, hls_media_t *m) {
  const char *line;
  size_t at = 0, n;
  int all = 0, k = 0, ms = 0, skip;
  long long sequence = 0;
  char ref[HLS_URL];

  memset(m, 0, sizeof *m);
  if(!is_playlist(text, len)) {
    return -1;
  }
  // how many segments and the first one's number, to know which to keep
  while(next_line(text, len, &at, &line, &n)) {
    all += n && line[0] != '#';
    if(starts(line, n, "#EXT-X-MEDIA-SEQUENCE:") && (sequence=number(line + 22, n - 22)) < 0) {
      sequence = 0;
    }
  }
  if(from < 0) {
    skip = all > HLS_SEGMENTS ? all - HLS_SEGMENTS : 0;
  } else {
    skip = from <= sequence ? 0 : from - sequence >= all ? all : (int)(from - sequence);
  }
  m->more = all - skip > HLS_SEGMENTS ? all - skip - HLS_SEGMENTS : 0;
  at = 0;
  while(next_line(text, len, &at, &line, &n)) {
    if(starts(line, n, "#EXT-X-MEDIA-SEQUENCE:")) {
      m->sequence = sequence;
    } else if(starts(line, n, "#EXT-X-TARGETDURATION:")) {
      m->target_s = (int)number(line + 22, n - 22);
    } else if(starts(line, n, "#EXT-X-ENDLIST")) {
      m->ended = 1;
    } else if(starts(line, n, "#EXTINF:")) {
      // seconds, maybe with a fraction: to milliseconds
      const char *s = line + 8, *e = line + n;
      long long whole = number(s, (size_t)(e - s));
      ms = whole < 0 ? 0 : (int)(whole > 86400 ? 86400000 : whole * 1000);
      while(s < e && *s >= '0' && *s <= '9') {
        s++;
      }
      if(s < e && *s == '.') {
        int scale = 100;
        for(s++; s < e && *s >= '0' && *s <= '9' && scale; s++, scale /= 10) {
          ms += (*s - '0') * scale;
        }
      }
    } else if(n && line[0] != '#') {
      if(k++ < skip || m->count == HLS_SEGMENTS) {
        ms = 0;
        continue;
      }
      if(n < sizeof ref) {
        memcpy(ref, line, n);
        ref[n] = 0;
      } else {
        ref[0] = 0;
      }
      if(!ref[0] || hls_resolve(base, ref, m->uri[m->count], sizeof m->uri[m->count])) {
        m->uri[m->count][0] = 0; // kept in its place, so the numbers stay right; skipped when read
      }
      m->ms[m->count++] = ms;
      ms = 0;
    }
  }
  m->sequence = sequence;
  m->first = sequence + skip;
  return 0;
}


size_t
hls_id3(const uint8_t *p, size_t len) {
  size_t at = 0, size;

  // "ID3", the version (2 bytes), flags, then the size in 4 bytes of 7 bits
  while(len - at >= 10 && !memcmp(p + at, "ID3", 3) && p[at + 3] != 0xff && p[at + 4] != 0xff &&
        !((p[at + 6] | p[at + 7] | p[at + 8] | p[at + 9]) & 0x80)) {
    size = (size_t)p[at + 6] << 21 | (size_t)p[at + 7] << 14 | (size_t)p[at + 8] << 7 | p[at + 9];
    at += 10 + size + (p[at + 5] & 0x10 ? 10 : 0);
    if(at > len) {
      break; // more than came: the caller waits for it
    }
  }
  return at;
}


int
hls_adts(const uint8_t *p, size_t len, size_t *frame) {
  size_t n, head;

  if(len >= 1 && p[0] != 0xff) {
    return -1;
  }
  if(len >= 2 && (p[1] & 0xf6) != 0xf0) {
    return -1; // the sync's last bits, then layer 0
  }
  if(len < 7) {
    return 0;
  }
  head = p[1] & 1 ? 7 : 9; // protection_absent, else a CRC
  n = (size_t)(p[3] & 3) << 11 | (size_t)p[4] << 3 | p[5] >> 5;
  if(n < head) {
    return -1;
  }
  if(n > len) {
    return 0;
  }
  *frame = n;
  return 1;
}
