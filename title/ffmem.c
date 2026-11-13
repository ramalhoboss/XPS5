/* FFmpeg's memory and threads in the title. The title's malloc has a heap of
a few MiB, which one H.264 decoder alone outgrows, and a thread whose creator
names no stack size gets 64 KiB on the PS5 (BrowserPS5 found both). So
build-title.sh renames FFmpeg's calls of malloc, realloc, free,
posix_memalign and pthread_create, in its copy of the archives, to these:
the memory comes through mem.h (the big blocks from the flexible memory), and
each thread FFmpeg starts gets 512 KiB. */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "mem.h"

#define ALIGN (64)         // what FFmpeg's SIMD wants
#define STACK (512 << 10)

// just before each block: where mem_alloc's block starts, and what was asked
typedef struct head {
  void  *raw;
  size_t size;
} head_t;

void *ff_sys_malloc(size_t size);
void *ff_sys_realloc(void *p, size_t size);
void ff_sys_free(void *p);
int ff_sys_posix_memalign(void **out, size_t align, size_t size);
int ff_sys_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *),
                          void *arg);


static void *
take(size_t size, size_t align) {
  head_t *h;
  char *raw;
  uintptr_t at;

  align = align < ALIGN ? ALIGN : align;
  if((align & (align - 1)) || size > SIZE_MAX - align - sizeof *h) {
    return NULL;
  }
  if(!(raw=mem_alloc(size + align + sizeof *h))) {
    return NULL;
  }
  at = ((uintptr_t)raw + sizeof *h + align - 1) & ~(uintptr_t)(align - 1);
  h = (head_t *)at - 1;
  h->raw = raw;
  h->size = size;
  return (void *)at;
}


void *
ff_sys_malloc(size_t size) {
  return take(size ? size : 1, ALIGN);
}


void
ff_sys_free(void *p) {
  if(p) {
    mem_free(((head_t *)p - 1)->raw);
  }
}


void *
ff_sys_realloc(void *p, size_t size) {
  size_t old;
  void *q;

  if(!p) {
    return ff_sys_malloc(size);
  }
  if(!size) {
    ff_sys_free(p);
    return NULL;
  }
  old = ((head_t *)p - 1)->size;
  if(!(q=ff_sys_malloc(size))) {
    return NULL;
  }
  memcpy(q, p, old < size ? old : size);
  ff_sys_free(p);
  return q;
}


int
ff_sys_posix_memalign(void **out, size_t align, size_t size) {
  if(align < sizeof(void *) || (align & (align - 1))) {
    return EINVAL;
  }
  return (*out=take(size ? size : 1, align)) ? 0 : ENOMEM;
}


int
ff_sys_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *), void *arg) {
  pthread_attr_t own;
  int ret;

  if(attr) {
    return pthread_create(thread, attr, fn, arg); // FFmpeg passes none
  }
  pthread_attr_init(&own);
  pthread_attr_setstacksize(&own, STACK);
  ret = pthread_create(thread, &own, fn, arg);
  pthread_attr_destroy(&own);
  return ret;
}


/*
 * The title's libc has localtime_s and gmtime_s but not the _r ones, which
 * FFmpeg calls for the time in its log lines and in av_parse_time (which
 * the player never calls). Both give the UTC time here.
 */

struct tm *ff_sys_gmtime_r(const time_t *when, struct tm *tm);
struct tm *ff_sys_localtime_r(const time_t *when, struct tm *tm);


struct tm *
ff_sys_gmtime_r(const time_t *when, struct tm *tm) {
  static const int before[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  long long t = (long long)*when, days = t / 86400, rem = t % 86400, era, doe, yoe, doy, mp, y, m, d;

  if(rem < 0) {
    rem += 86400;
    days--;
  }
  memset(tm, 0, sizeof *tm);
  tm->tm_hour = (int)(rem / 3600);
  tm->tm_min = (int)(rem % 3600 / 60);
  tm->tm_sec = (int)(rem % 60);
  tm->tm_wday = (int)(((days + 4) % 7 + 7) % 7); // 1 January 1970 was a Thursday

  // the civil date of a day count (H. Hinnant's days_from_civil, backwards)
  days += 719468;
  era = (days >= 0 ? days : days - 146096) / 146097;
  doe = days - era * 146097;
  yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = yoe + era * 400 + (m <= 2);
  tm->tm_year = (int)(y - 1900);
  tm->tm_mon = (int)(m - 1);
  tm->tm_mday = (int)d;
  tm->tm_yday = before[m - 1] + (int)d - 1 + (m > 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  return tm;
}


struct tm *
ff_sys_localtime_r(const time_t *when, struct tm *tm) {
  return ff_sys_gmtime_r(when, tm);
}
