/* A few threads that share the screen's heaviest passes, each split into
parts by rows: the backdrop's pixels, the frame's start from it, and the
copy into the TV's buffer. Only the thread that draws runs jobs; without
workers_init (the host tests) a job runs whole on that thread. */

#pragma once

#define WORKERS_MAX 7

/**
 * start count threads (at most WORKERS_MAX); returns how many started
 **/
int workers_init(int count);

/**
 * run fn(arg, part, parts) for each part from 0 to parts - 1, parts being
 * the threads plus the caller, which does part 0; returns once all are done
 **/
void workers_run(void (*fn)(void *arg, int part, int parts), void *arg);

/**
 * the rows from..to of part of parts, of count rows
 **/
void workers_rows(int count, int part, int parts, int *from, int *to);
