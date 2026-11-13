/* HTTPS GET and POST through libSceHttp, see https.h. The signatures come from the
SDK's http2_get sample, elfldr's uri.c and shadPS4's libSceHttp. */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "https.h"
#include "mem.h"
#include "tls.h"

#define MAX_BODY   (16 << 20)
#define MAX_HEADER (64 << 10) // of an answer's headers; x.com's pages pass the library's
                              // default (0x80431073, too large a header)

#define HTTP_VERSION_1_1 2
#define HTTP_METHOD_GET  0
#define HTTP_METHOD_POST 1
#define HEADER_OVERWRITE 0


typedef struct ssl_data { // SceSslData
  const char *ptr;
  size_t      size;
} ssl_data_t;

// verify_err bits: 0x01 internal, 0x02 invalid certificate, 0x04 name
// mismatch, 0x08 expired, 0x10 not yet valid, 0x20 unknown root
typedef int (*ssl_callback_t)(int ssl_ctx, unsigned verify_err, void *const certs[],
                              int count, void *arg);

int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetPoolDestroy(int id);
int sceSslInit(size_t pool_size);
int sceSslTerm(int id);

int sceHttpInit(int net_id, int ssl_id, size_t pool_size);
int sceHttpTerm(int ctx);
int sceHttpCreateTemplate(int ctx, const char *agent, int version, int auto_proxy);
int sceHttpDeleteTemplate(int tmpl);
int sceHttpSetInflateGZIPEnabled(int id, int enable);
int sceHttpSetCookieEnabled(int id, int enable);
int sceHttpSetResponseHeaderMaxSize(int id, size_t size);
int sceHttpGetAllResponseHeaders(int req, char **headers, size_t *size);
int sceHttpCreateConnectionWithURL(int tmpl, const char *url, int keep_alive);
int sceHttpDeleteConnection(int conn);
int sceHttpCreateRequestWithURL(int conn, int method, const char *url,
                                uint64_t length);
int sceHttpDeleteRequest(int req);
int sceHttpAddRequestHeader(int id, const char *name, const char *value,
                            uint32_t mode);
int sceHttpSendRequest(int req, const void *data, size_t size);
int sceHttpGetStatusCode(int req, int *status);
int sceHttpReadData(int req, void *data, size_t size);
int sceHttpsLoadCert(int ctx, int count, const ssl_data_t **ca_list,
                     const ssl_data_t *cert, const ssl_data_t *key);
int sceHttpsSetSslCallback(int id, ssl_callback_t cb, void *arg);
int sceHttpsGetSslError(int id, int *err, unsigned *detail);


static int g_net  = -1;
static int g_ssl  = -1;
static int g_http = -1;
static int g_tmpl = -1;
static void (*g_log)(const char *line);


static void
log_line(const char *fmt, ...) {
  char buf[256];
  va_list args;

  if(!g_log) {
    return;
  }
  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  g_log(buf);
}


/**
 * the library calls this with what it found wrong in the server's
 * certificates; never accept one it could not verify
 **/
static int
ssl_check(int ssl_ctx, unsigned verify_err, void *const certs[], int count, void *arg) {
  (void)ssl_ctx;
  (void)certs;
  (void)arg;
  if(verify_err) {
    log_line("https: certificate check 0x%x over %d certificates", verify_err, count);
  }
  return verify_err ? -1 : 0;
}


/**
 * the roots and the Let's Encrypt chain of tls.h, each as PEM text with a
 * NUL after it counted in its size; DER does not load (0x8095f005) and the
 * roots alone leave x.com's root unknown
 **/
static int
load_certs(int ctx) {
  static unsigned char buf[16 << 10];
  const ssl_data_t *list[TLS_CERTS];
  ssl_data_t data[TLS_CERTS];
  tls_cert_t certs[TLS_CERTS];
  size_t used = 0;

  tls_certs_pem(certs);
  for(int i=0; i<TLS_CERTS; i++) {
    if(used + certs[i].size + 1 > sizeof buf) {
      return -1;
    }
    memcpy(buf + used, certs[i].data, certs[i].size);
    buf[used + certs[i].size] = 0;
    data[i] = (ssl_data_t){(const char*)buf + used, certs[i].size + 1};
    list[i] = &data[i];
    used += certs[i].size + 1;
  }
  return sceHttpsLoadCert(ctx, TLS_CERTS, list, NULL, NULL);
}


int
https_init(const char *agent, void (*log)(const char *line)) {
  int ret;

  g_log = log;
  if((ret=sceNetInit())) {
    log_line("https: net init 0x%08x (going on)", (unsigned)ret);
  }
  if((g_net=ret=sceNetPoolCreate("TwitterPS5", 128 << 10, 0)) < 0) {
    log_line("https: net pool 0x%08x", (unsigned)ret);
    goto fail;
  }
  if((g_ssl=ret=sceSslInit(512 << 10)) < 0) {
    log_line("https: ssl init 0x%08x", (unsigned)ret);
    goto fail;
  }
  if((g_http=ret=sceHttpInit(g_net, g_ssl, 1 << 20)) < 0) {
    log_line("https: http init 0x%08x", (unsigned)ret);
    goto fail;
  }
  if((ret=load_certs(g_http))) {
    log_line("https: certificates 0x%08x", (unsigned)ret);
    goto fail;
  }
  if((g_tmpl=ret=sceHttpCreateTemplate(g_http, agent, HTTP_VERSION_1_1, 1)) < 0) {
    log_line("https: template 0x%08x", (unsigned)ret);
    goto fail;
  }
  if((ret=sceHttpSetInflateGZIPEnabled(g_tmpl, 1))) {
    log_line("https: gzip 0x%08x (going on)", (unsigned)ret);
  }
  if((ret=sceHttpSetResponseHeaderMaxSize(g_tmpl, MAX_HEADER)) &&
     (ret=sceHttpSetResponseHeaderMaxSize(g_tmpl, MAX_HEADER / 4))) {
    log_line("https: header size 0x%08x (going on)", (unsigned)ret);
  }
  // the caller sends its own Cookie header: the library's cookie jar would
  // keep what an answer sets and add it to the next request, and x.com
  // answered such a second request with an empty 403
  if((ret=sceHttpSetCookieEnabled(g_tmpl, 0))) {
    log_line("https: cookie jar off 0x%08x (going on)", (unsigned)ret);
  }
  if((ret=sceHttpsSetSslCallback(g_tmpl, ssl_check, NULL))) {
    log_line("https: certificate callback 0x%08x", (unsigned)ret);
    goto fail;
  }
  return 0;

 fail:
  https_fini();
  return ret < 0 ? ret : -1;
}


void
https_fini(void) {
  if(g_tmpl >= 0) {
    sceHttpDeleteTemplate(g_tmpl);
  }
  if(g_http >= 0) {
    sceHttpTerm(g_http);
  }
  if(g_ssl >= 0) {
    sceSslTerm(g_ssl);
  }
  if(g_net >= 0) {
    sceNetPoolDestroy(g_net);
  }
  g_tmpl = g_http = g_ssl = g_net = -1;
}


static int
is_name(const char *s, size_t len, const char *name) {
  size_t i;

  for(i=0; i < len && name[i]; i++) {
    if((s[i] | 0x20) != name[i]) {
      return 0;
    }
  }
  return i == len && !name[i];
}


/**
 * log how many cookies an answer sets and, when it is not a 2xx, the names
 * of its headers; never their values
 **/
static void
log_headers(int req, int status) {
  char names[200];
  char *block, *line, *end, *eol, *colon;
  size_t size, len, used = 0;
  int cookies = 0;

  if(sceHttpGetAllResponseHeaders(req, &block, &size) || !block) {
    return;
  }
  names[0] = 0;
  for(line=block, end=block + size; line < end; line = eol ? eol + 1 : end) {
    eol = memchr(line, '\n', end - line);
    if(!(colon=memchr(line, ':', (eol ? eol : end) - line))) {
      continue; // the status line
    }
    len = colon - line;
    if(is_name(line, len, "set-cookie")) {
      cookies++;
    } else if(used + len + 2 < sizeof names) {
      memcpy(names + used, line, len);
      used += len;
      names[used++] = ' ';
      names[used] = 0;
    }
  }
  if(cookies) {
    log_line("https: the answer sets %d cookies", cookies);
  }
  if(status / 100 != 2) {
    log_line("https: HTTP %d headers: %s", status, names);
  }
}


static int
read_body(int req, size_t max, https_response_t *res) {
  const size_t step = 32 << 10;
  size_t cap = 0, want;
  char *body;
  int n;

  for(;;) {
    if(res->len + step + 1 > cap) {
      // most answers are pictures of a few hundred KiB
      want = cap ? cap * 2 : 128 << 10;
      want = want > max + step + 1 ? max + step + 1 : want;
      if(want <= cap || res->len > max) {
        log_line("https: answer is too big (%zu bytes so far)", res->len);
        return -1;
      }
      if(!(body=mem_realloc(res->body, want))) {
        log_line("https: no memory for %zu KiB (%zu bytes so far)", want >> 10, res->len);
        return -1;
      }
      res->body = body;
      cap = want;
    }
    if((n=sceHttpReadData(req, res->body + res->len, cap - res->len - 1)) < 0) {
      log_line("https: read 0x%08x after %zu bytes", (unsigned)n, res->len);
      return n;
    }
    if(n == 0) {
      res->body[res->len] = 0;
      return 0;
    }
    res->len += n;
  }
}


static int
request(int method, const char *url, const https_header_t *hdr, int count, const char *data,
        size_t len, size_t max, https_response_t *res) {
  int conn, req = -1, ret, err = 0;
  unsigned detail = 0;

  memset(res, 0, sizeof *res);
  if((conn=ret=sceHttpCreateConnectionWithURL(g_tmpl, url, 1)) < 0) {
    log_line("https: connection 0x%08x", (unsigned)ret);
    return ret;
  }
  if((req=ret=sceHttpCreateRequestWithURL(conn, method, url, len)) < 0) {
    log_line("https: request 0x%08x", (unsigned)ret);
    goto out;
  }
  for(int i=0; i<count; i++) {
    if((ret=sceHttpAddRequestHeader(req, hdr[i].name, hdr[i].value, HEADER_OVERWRITE))) {
      log_line("https: header %s 0x%08x", hdr[i].name, (unsigned)ret);
      goto out;
    }
  }
  if((ret=sceHttpSendRequest(req, data, len))) {
    if(!sceHttpsGetSslError(req, &err, &detail) && err) {
      log_line("https: send 0x%08x, SSL error 0x%08x, detail 0x%x",
               (unsigned)ret, (unsigned)err, detail);
    } else {
      log_line("https: send 0x%08x", (unsigned)ret);
    }
    goto out;
  }
  if((ret=sceHttpGetStatusCode(req, &res->status))) {
    log_line("https: status 0x%08x", (unsigned)ret);
    goto out;
  }
  log_headers(req, res->status);
  ret = read_body(req, max, res);

 out:
  if(req >= 0) {
    sceHttpDeleteRequest(req);
  }
  sceHttpDeleteConnection(conn);
  if(ret) {
    mem_free(res->body);
    memset(res, 0, sizeof *res);
  }
  return ret < 0 ? ret : (ret ? -1 : 0);
}


int
https_get(const char *url, const https_header_t *hdr, int count, https_response_t *res) {
  return request(HTTP_METHOD_GET, url, hdr, count, NULL, 0, MAX_BODY, res);
}


int
https_get_big(const char *url, const https_header_t *hdr, int count, size_t max,
              https_response_t *res) {
  return request(HTTP_METHOD_GET, url, hdr, count, NULL, 0, max, res);
}


/**
 * the answer's Content-Length, or 0 when it has none
 **/
static size_t
content_length(int req) {
  char *block, *line, *end, *eol, *colon;
  size_t size, len = 0;

  if(sceHttpGetAllResponseHeaders(req, &block, &size) || !block) {
    return 0;
  }
  for(line=block, end=block + size; line < end; line = eol ? eol + 1 : end) {
    eol = memchr(line, '\n', end - line);
    if(!(colon=memchr(line, ':', (eol ? eol : end) - line)) ||
       !is_name(line, (size_t)(colon - line), "content-length")) {
      continue;
    }
    for(const char *p = colon + 1; p < (eol ? eol : end); p++) {
      if(*p >= '0' && *p <= '9') {
        if(len > ((size_t)-1 - 9) / 10) {
          return 0;
        }
        len = len * 10 + (size_t)(*p - '0');
      } else if(*p != ' ' && *p != '\t' && *p != '\r') {
        return 0;
      }
    }
    return len;
  }
  return 0;
}


static int
stream_end(https_stream_t *st, int ret) {
  st->error = ret;
  __atomic_store_n(&st->state, ret ? HTTPS_STREAM_FAILED : HTTPS_STREAM_DONE, __ATOMIC_RELEASE);
  return ret;
}


int
https_get_stream(const char *url, size_t max, https_stream_t *st) {
  int conn, req = -1, ret, n, err = 0;
  unsigned detail = 0;
  size_t len, got = 0;
  char *body;

  if((conn=ret=sceHttpCreateConnectionWithURL(g_tmpl, url, 1)) < 0) {
    log_line("https: connection 0x%08x", (unsigned)ret);
    return stream_end(st, ret);
  }
  if((req=ret=sceHttpCreateRequestWithURL(conn, HTTP_METHOD_GET, url, 0)) < 0) {
    log_line("https: request 0x%08x", (unsigned)ret);
    goto out;
  }
  if((ret=sceHttpSendRequest(req, NULL, 0))) {
    if(!sceHttpsGetSslError(req, &err, &detail) && err) {
      log_line("https: send 0x%08x, SSL error 0x%08x, detail 0x%x",
               (unsigned)ret, (unsigned)err, detail);
    } else {
      log_line("https: send 0x%08x", (unsigned)ret);
    }
    goto out;
  }
  if((ret=sceHttpGetStatusCode(req, &st->status))) {
    log_line("https: status 0x%08x", (unsigned)ret);
    goto out;
  }
  log_headers(req, st->status);
  len = st->status == 200 ? content_length(req) : 0;
  if(!len || len > max || !(body=mem_alloc(len + 1))) {
    if(st->status == 200) {
      log_line("https: a stream of %zu bytes is not read", len);
    }
    ret = -1;
    goto out;
  }
  body[len] = 0;
  st->body = body;
  st->len = len;
  __atomic_store_n(&st->state, HTTPS_STREAM_READING, __ATOMIC_RELEASE);
  while(got < len) {
    if(st->stop && *st->stop) {
      ret = -1;
      goto out;
    }
    if((n=sceHttpReadData(req, body + got, len - got > (256 << 10) ? 256 << 10 : len - got)) < 0) {
      log_line("https: read 0x%08x after %zu bytes", (unsigned)n, got);
      ret = n;
      goto out;
    }
    if(!n) {
      log_line("https: the answer ended after %zu of %zu bytes", got, len);
      ret = -1;
      goto out;
    }
    got += (size_t)n;
    __atomic_store_n(&st->got, got, __ATOMIC_RELEASE);
  }
  ret = 0;

 out:
  if(req >= 0) {
    sceHttpDeleteRequest(req);
  }
  sceHttpDeleteConnection(conn);
  return stream_end(st, ret < 0 ? ret : ret ? -1 : 0);
}


int
https_post(const char *url, const https_header_t *hdr, int count, const char *data,
           https_response_t *res) {
  return request(HTTP_METHOD_POST, url, hdr, count, data, strlen(data), MAX_BODY, res);
}


int
https_post_data(const char *url, const https_header_t *hdr, int count, const void *data,
                size_t len, https_response_t *res) {
  return request(HTTP_METHOD_POST, url, hdr, count, data, len, MAX_BODY, res);
}
