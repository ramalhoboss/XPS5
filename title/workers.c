/* The screen's helper threads, see workers.h. A job is posted with a new
generation number; each thread runs its part once per generation and the
last one to finish wakes the caller. */

#include <pthread.h>
#include <stdint.h>

#include "workers.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_go = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  g_done = PTHREAD_COND_INITIALIZER;
static int             g_count;    // threads running
static int             g_left;     // parts of the job still running
static unsigned        g_gen;      // the job's number
static void          (*g_fn)(void *arg, int part, int parts);
static void           *g_arg;
static pthread_t       g_owner;    // the thread that runs jobs
static int             g_busy;     // it is running one (its own to read)


static void *
worker_main(void *arg) {
  const int part = (int)(intptr_t)arg;
  unsigned seen = 0;
  void (*fn)(void *, int, int);
  void *job;
  int parts;

  for(;;) {
    pthread_mutex_lock(&g_lock);
    while(g_gen == seen) {
      pthread_cond_wait(&g_go, &g_lock);
    }
    seen = g_gen;
    fn = g_fn;
    job = g_arg;
    parts = g_count + 1;
    pthread_mutex_unlock(&g_lock);

    fn(job, part, parts);

    pthread_mutex_lock(&g_lock);
    if(--g_left == 0) {
      pthread_cond_signal(&g_done);
    }
    pthread_mutex_unlock(&g_lock);
  }
  return NULL;
}


int
workers_init(int count) {
  pthread_attr_t attr;
  pthread_t thread;

  count = count > WORKERS_MAX ? WORKERS_MAX : count;
  g_owner = pthread_self();
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 256 << 10);
  for(int i=0; i < count; i++) {
    // part 0 is the caller's; a thread that did not start leaves the rest
    // to fewer parts
    if(pthread_create(&thread, &attr, worker_main, (void *)(intptr_t)(g_count + 1))) {
      break;
    }
    pthread_detach(thread);
    pthread_mutex_lock(&g_lock);
    g_count++;
    pthread_mutex_unlock(&g_lock);
  }
  pthread_attr_destroy(&attr);
  return g_count;
}


void
workers_run(void (*fn)(void *arg, int part, int parts), void *arg) {
  int parts;

  // another thread's job, or a job inside a job, runs whole where it is
  if(!g_count || !pthread_equal(pthread_self(), g_owner) || g_busy) {
    fn(arg, 0, 1);
    return;
  }
  g_busy = 1;
  pthread_mutex_lock(&g_lock);
  parts = g_count + 1;
  if(parts > 1) {
    g_fn = fn;
    g_arg = arg;
    g_left = g_count;
    g_gen++;
    pthread_cond_broadcast(&g_go);
  }
  pthread_mutex_unlock(&g_lock);

  fn(arg, 0, parts);
  if(parts > 1) {
    pthread_mutex_lock(&g_lock);
    while(g_left) {
      pthread_cond_wait(&g_done, &g_lock);
    }
    pthread_mutex_unlock(&g_lock);
  }
  g_busy = 0;
}


void
workers_rows(int count, int part, int parts, int *from, int *to) {
  *from = (int)((long long)count * part / parts);
  *to = (int)((long long)count * (part + 1) / parts);
}
