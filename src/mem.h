/* Memory for the big things: answers, JSON trees, pictures, posts. The
title's malloc has a heap of a few MiB, while the PS5 gives it far more
flexible memory; once mem_use hands this a way to map that memory, blocks of
MEM_MAP_MIN bytes or more come from it, and the rest still from malloc.
Without mem_use (the payloads, the host tests) everything comes from malloc.
What these functions give must go back through mem_free or mem_realloc. */

#pragma once

#include <stddef.h>

#define MEM_MAP_MIN (64 << 10)
#define MEM_PAGE    (16 << 10) // what the PS5 maps in

/**
 * map len bytes (a multiple of MEM_PAGE) of readable, writable memory, or
 * return NULL; and unmap what it mapped
 **/
typedef void *(*mem_map_t)(size_t len);
typedef void (*mem_unmap_t)(void *p, size_t len);

/**
 * map the big blocks with map and unmap from now on; call it before the
 * first mem_* call and before any thread starts
 **/
void mem_use(mem_map_t map, mem_unmap_t unmap);

void *mem_alloc(size_t size);
void *mem_calloc(size_t count, size_t size);
void *mem_realloc(void *p, size_t size);
void mem_free(void *p);
