/* Host tests of src/https.c's streams, against a fake libSceHttp: a file
that comes whole, connections that break off and go on from where they broke
(with a Range, or from a server that ignores it), a server that stays down, a
server that answers with another part of the file, and being told to stop. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "https.h"
#include "mem.h"

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while(0)

#define FILE_LEN  (3 * 256 * 1024 + 12345)
#define READ_MAX  100000 // the fake gives at most this much a read
#define ERR_RECV  ((int)0x80431082u)
#define ERR_SEND  ((int)0x80431068u)
#define MAX_REQS  16

static unsigned char g_file[FILE_LEN];

typedef struct fake_req {
  int    used, status, range;
  size_t from, pos, sent, cut;
  char   head[256];
} fake_req_t;

static struct {
  int          ranges;         // a Range is answered with 206
  int          wrong_range;    // ... of the byte after the one asked for
  long         cut[MAX_REQS];  // bytes each answer sends before it breaks off; 0: all
  int          send_fail_from; // requests from this one on are not sent (0: none)
  int          send_fail_to;   // ... up to this one (0: all after it)
  long         stop_after;     // bytes, all answers together, before stop is set (0: never)
  int          stop_sleeps;    // sleeps before stop is set (0: never)
  int          requests, conns, reqs, sleeps;
  unsigned     recv_timeout;
  long long    slept;
  long         given;
  size_t       from[MAX_REQS];
  volatile int stop;
  fake_req_t   req[MAX_REQS + 1];
} g;


int sceNetInit(void) { return 0; }
int sceNetPoolCreate(const char *name, int size, int flags) { (void)name; (void)size; (void)flags; return 1; }
int sceNetPoolDestroy(int id) { (void)id; return 0; }
int sceSslInit(size_t pool_size) { (void)pool_size; return 1; }
int sceSslTerm(int id) { (void)id; return 0; }
int sceHttpInit(int net_id, int ssl_id, size_t pool_size) { (void)net_id; (void)ssl_id; (void)pool_size; return 1; }
int sceHttpTerm(int ctx) { (void)ctx; return 0; }
int sceHttpCreateTemplate(int ctx, const char *agent, int version, int auto_proxy) {
  (void)ctx; (void)agent; (void)version; (void)auto_proxy;
  return 1;
}
int sceHttpDeleteTemplate(int tmpl) { (void)tmpl; return 0; }
int sceHttpSetInflateGZIPEnabled(int id, int enable) { (void)id; (void)enable; return 0; }
int sceHttpSetCookieEnabled(int id, int enable) { (void)id; (void)enable; return 0; }
int sceHttpSetResponseHeaderMaxSize(int id, size_t size) { (void)id; (void)size; return 0; }
int sceHttpsLoadCert(int ctx, int count, const void **ca_list, const void *cert, const void *key) {
  (void)ctx; (void)count; (void)ca_list; (void)cert; (void)key;
  return 0;
}
int sceHttpsSetSslCallback(int id, void *cb, void *arg) { (void)id; (void)cb; (void)arg; return 0; }
int sceHttpsGetSslError(int id, int *err, unsigned *detail) { (void)id; (void)err; (void)detail; return -1; }

int sceHttpSetRecvTimeOut(int id, uint32_t usec) {
  (void)id;
  g.recv_timeout = usec;
  return 0;
}

int sceKernelUsleep(unsigned int usec) {
  g.sleeps++;
  g.slept += usec;
  if(g.stop_sleeps && g.sleeps >= g.stop_sleeps) {
    g.stop = 1;
  }
  return 0;
}

int sceHttpCreateConnectionWithURL(int tmpl, const char *url, int keep_alive) {
  (void)tmpl; (void)url; (void)keep_alive;
  g.conns++;
  return 100;
}

int sceHttpDeleteConnection(int conn) {
  CHECK(conn == 100);
  g.conns--;
  return 0;
}

int sceHttpCreateRequestWithURL(int conn, int method, const char *url, uint64_t length) {
  (void)url; (void)length;
  CHECK(conn == 100 && method == 0);
  for(int i=1; i <= MAX_REQS; i++) {
    if(!g.req[i].used) {
      memset(&g.req[i], 0, sizeof g.req[i]);
      g.req[i].used = 1;
      g.reqs++;
      return i;
    }
  }
  return -1;
}

static fake_req_t *
req_of(int id) {
  CHECK(id >= 1 && id <= MAX_REQS && g.req[id].used);
  return &g.req[id];
}

int sceHttpDeleteRequest(int id) {
  req_of(id)->used = 0;
  g.reqs--;
  return 0;
}

int sceHttpAddRequestHeader(int id, const char *name, const char *value, uint32_t mode) {
  fake_req_t *r = req_of(id);

  (void)mode;
  if(!strcmp(name, "Range")) {
    CHECK(sscanf(value, "bytes=%zu-", &r->from) == 1);
    r->range = 1;
  }
  return 0;
}

int sceHttpSendRequest(int id, const void *data, size_t size) {
  fake_req_t *r = req_of(id);
  int n = g.requests++;

  (void)data; (void)size;
  CHECK(n < MAX_REQS);
  g.from[n] = r->range ? r->from : 0;
  if(g.send_fail_from && n + 1 >= g.send_fail_from && (!g.send_fail_to || n + 1 <= g.send_fail_to)) {
    return ERR_SEND;
  }
  r->cut = g.cut[n] > 0 ? (size_t)g.cut[n] : (size_t)-1;
  if(r->range && g.ranges) {
    r->status = 206;
    r->pos = r->from + (g.wrong_range ? 1 : 0);
    snprintf(r->head, sizeof r->head, "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\n"
             "Content-Range: bytes %zu-%d/%d\r\nContent-Length: %zu\r\n", r->pos, FILE_LEN - 1, FILE_LEN,
             FILE_LEN - r->pos);
  } else {
    r->status = 200;
    r->pos = 0;
    snprintf(r->head, sizeof r->head, "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nContent-Length: %d\r\n",
             FILE_LEN);
  }
  return 0;
}

int sceHttpGetStatusCode(int id, int *status) {
  *status = req_of(id)->status;
  return 0;
}

int sceHttpGetAllResponseHeaders(int id, char **headers, size_t *size) {
  fake_req_t *r = req_of(id);

  *headers = r->head;
  *size = strlen(r->head);
  return 0;
}

int sceHttpReadData(int id, void *data, size_t size) {
  fake_req_t *r = req_of(id);
  size_t n = size < READ_MAX ? size : READ_MAX;

  if(r->sent >= r->cut) {
    // a broken connection: an error, or the end of the answer
    return r->cut & 1 ? 0 : ERR_RECV;
  }
  if(n > FILE_LEN - r->pos) {
    n = FILE_LEN - r->pos;
  }
  if(n > r->cut - r->sent) {
    n = r->cut - r->sent;
  }
  memcpy(data, g_file + r->pos, n);
  r->pos += n;
  r->sent += n;
  g.given += (long)n;
  if(g.stop_after && g.given >= g.stop_after) {
    g.stop = 1;
  }
  return (int)n;
}


static void
quiet(const char *line) {
  (void)line;
}


static void
reset(https_stream_t *st) {
  memset(&g, 0, sizeof g);
  memset(st, 0, sizeof *st);
  st->stop = &g.stop;
}


static void
done(https_stream_t *st) {
  CHECK(g.reqs == 0 && g.conns == 0); // every request and connection closed
  mem_free(st->body);
}


static void
check_whole(const https_stream_t *st, int ret) {
  CHECK(ret == 0);
  CHECK(st->state == HTTPS_STREAM_DONE && st->error == 0);
  CHECK(st->len == FILE_LEN && st->got == FILE_LEN);
  CHECK(!memcmp(st->body, g_file, FILE_LEN));
  CHECK(st->body[FILE_LEN] == 0);
}


int
main(void) {
  https_stream_t st;
  int ret;

  for(int i=0; i < FILE_LEN; i++) {
    g_file[i] = (unsigned char)(i * 7 + i / 251);
  }
  CHECK(!https_init("test", quiet));

  // all of it on the first connection
  reset(&st);
  ret = https_get_stream("https://video.twimg.com/v.mp4", 1 << 30, &st);
  check_whole(&st, ret);
  CHECK(g.requests == 1 && g.from[0] == 0 && g.sleeps == 0 && g.recv_timeout == 30000000u);
  done(&st);

  // broken off three times (a read error, the answer's end, a byte), and
  // each time it goes on from there with a Range
  reset(&st);
  g.ranges = 1;
  g.cut[0] = 300000;
  g.cut[1] = 1;
  g.cut[2] = 400001;
  ret = https_get_stream("https://video.twimg.com/v.mp4", 1 << 30, &st);
  check_whole(&st, ret);
  CHECK(g.requests == 4);
  CHECK(g.from[0] == 0 && g.from[1] == 300000 && g.from[2] == 300001 && g.from[3] == 700002);
  CHECK(g.slept == 3 * 250000); // each one after something came: the shortest wait
  done(&st);

  // a server that ignores the Range: all of the file again, read past
  reset(&st);
  g.cut[0] = 400000;
  g.cut[1] = 700000;
  ret = https_get_stream("https://video.twimg.com/v.mp4", 1 << 30, &st);
  check_whole(&st, ret);
  CHECK(g.requests == 3 && g.from[1] == 400000 && g.from[2] == 700000);
  done(&st);

  // a server that stays down: six tries, waiting longer each time, then
  // what came stays, FAILED with the last error
  reset(&st);
  g.ranges = 1;
  g.cut[0] = 200000;
  g.send_fail_from = 2;
  ret = https_get_stream("https://video.twimg.com/v.mp4", 1 << 30, &st);
  CHECK(ret == ERR_SEND && st.state == HTTPS_STREAM_FAILED && st.error == ERR_SEND);
  CHECK(st.got == 200000 && st.len == FILE_LEN && !memcmp(st.body, g_file, 200000));
  CHECK(g.requests == 7);
  CHECK(g.slept == 250000 + 500000 + 1000000 + 2000000 + 4000000 + 4000000);
  done(&st);

  // down for a while, then up again: it goes on
  reset(&st);
  g.ranges = 1;
  g.cut[0] = 200000;
  g.send_fail_from = 2;
  g.send_fail_to = 4;
  ret = https_get_stream("https://video.twimg.com/v.mp4", 1 << 30, &st);
  check_whole(&st, ret);
  CHECK(g.requests == 5 && g.from[4] == 200000);
  CHECK(g.slept == 250000 + 500000 + 1000000 + 2000000);
  done(&st);

  // a 206 of another part of the file: not taken, and no more tries
  reset(&st);
  g.ranges = 1;
  g.wrong_range = 1;
  g.cut[0] = 100000;
  ret = https_get_stream("https://video.twimg.com/v.mp4", 1 << 30, &st);
  CHECK(ret == -1 && st.state == HTTPS_STREAM_FAILED && st.got == 100000);
  CHECK(g.requests == 2);
  done(&st);

  // told to stop while it reads
  reset(&st);
  g.stop_after = 150000;
  ret = https_get_stream("https://video.twimg.com/v.mp4", 1 << 30, &st);
  CHECK(ret == -1 && st.state == HTTPS_STREAM_FAILED && st.got < FILE_LEN && g.requests == 1);
  done(&st);

  // told to stop while it waits to connect again
  reset(&st);
  g.ranges = 1;
  g.cut[0] = 100000;
  g.send_fail_from = 2;
  g.stop_sleeps = 3;
  ret = https_get_stream("https://video.twimg.com/v.mp4", 1 << 30, &st);
  CHECK(ret == -1 && st.state == HTTPS_STREAM_FAILED && g.requests == 1 && g.sleeps == 3);
  done(&st);

  // too long, and no length: not read
  reset(&st);
  ret = https_get_stream("https://video.twimg.com/v.mp4", FILE_LEN - 1, &st);
  CHECK(ret == -1 && st.state == HTTPS_STREAM_FAILED && !st.body && g.requests == 1);
  done(&st);

  https_fini();
  printf("all https stream checks passed\n");
  return 0;
}
