/* Memory for the big things, see mem.h. Each block starts with a header
saying how it was made, so mem_free knows where it goes back to. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"


// 16 bytes, so the block after it stays aligned like malloc's
typedef struct header {
  size_t size;   // what was asked for
  size_t mapped; // bytes mapped, header included, or 0 from malloc
} header_t;


static mem_map_t   g_map;
static mem_unmap_t g_unmap;


void
mem_use(mem_map_t map, mem_unmap_t unmap) {
  g_map = map;
  g_unmap = unmap;
}


static header_t *
header(void *p) {
  return (header_t *)p - 1;
}


void *
mem_alloc(size_t size) {
  header_t *h;
  size_t len;

  if(size > SIZE_MAX - MEM_PAGE - sizeof *h) {
    return NULL;
  }
  if(g_map && size >= MEM_MAP_MIN) {
    len = (size + sizeof *h + MEM_PAGE - 1) / MEM_PAGE * MEM_PAGE;
    if(!(h=g_map(len))) {
      return NULL;
    }
    h->mapped = len;
  } else {
    if(!(h=malloc(sizeof *h + size))) {
      return NULL;
    }
    h->mapped = 0;
  }
  h->size = size;
  return h + 1;
}


void *
mem_calloc(size_t count, size_t size) {
  void *p;

  if(size && count > SIZE_MAX / size) {
    return NULL;
  }
  if((p=mem_alloc(count * size))) {
    memset(p, 0, count * size);
  }
  return p;
}


void *
mem_realloc(void *p, size_t size) {
  header_t *h;
  void *q;

  if(!p) {
    return mem_alloc(size);
  }
  h = header(p);
  // a mapped block grows into its last page
  if(h->mapped && size + sizeof *h <= h->mapped && size >= MEM_MAP_MIN) {
    h->size = size;
    return p;
  }
  if(!(q=mem_alloc(size))) {
    return NULL;
  }
  memcpy(q, p, h->size < size ? h->size : size);
  mem_free(p);
  return q;
}


void
mem_free(void *p) {
  header_t *h;

  if(!p) {
    return;
  }
  h = header(p);
  if(h->mapped) {
    g_unmap(h, h->mapped);
  } else {
    free(h);
  }
}
