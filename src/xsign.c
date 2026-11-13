/* The x-client-transaction-id's page and chunk, see xsign.h. */

#include <string.h>

#include "mem.h"
#include "xsign.h"


int
xsign_load(xtid_t *t, const x_session_t *s, void (*log)(const char *fmt, ...)) {
  static const char *const pages[] = {"https://x.com/home", "https://x.com/i/jf/"};
  char chunk_url[256];
  int indices[XTID_INDICES_MAX], count = -1, ret = -1;
  https_response_t page = {0}, chunk;
  https_header_t headers[3];
  x_request_t request;

  x_request_init(&request, s);
  headers[0] = (https_header_t){"cookie", request.cookie};
  headers[1] = (https_header_t){"accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"};
  headers[2] = (https_header_t){"accept-language", "en-US,en;q=0.9"};
  for(size_t i=0; i < sizeof pages / sizeof *pages; i++) {
    if((ret=https_get(pages[i], headers, 3, &page))) {
      log("tid: page %zu 0x%08x", i, (unsigned)ret);
      continue;
    }
    log("tid: page %zu: HTTP %d, %zu bytes", i, page.status, page.len);
    if(page.status == 200 && !xtid_ondemand_url(page.body, chunk_url, sizeof chunk_url)) {
      break;
    }
    mem_free(page.body);
    page.body = NULL;
    ret = -1;
  }
  memset(&request, 0, sizeof request);
  memset(headers, 0, sizeof headers);
  if(ret) {
    log("tid: no page names the chunk");
    return -1;
  }

  if(!(ret=https_get(chunk_url, NULL, 0, &chunk))) {
    if(chunk.status == 200) {
      count = xtid_indices(chunk.body, indices, XTID_INDICES_MAX);
    }
    log("tid: chunk HTTP %d, %zu bytes, %d indices", chunk.status, chunk.len, count);
    mem_free(chunk.body);
  } else {
    log("tid: chunk 0x%08x", (unsigned)ret);
  }
  ret = count > 0 ? xtid_init(t, page.body, indices, count) : -3;
  mem_free(page.body);
  log("tid: %s", !ret ? "ready" : ret == -1 ? "no key in the page" :
                 ret == -2 ? "no animation for the key" : "no indices");
  return ret ? -1 : 0;
}
