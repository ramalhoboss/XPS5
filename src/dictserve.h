/* The dictation payload's side of src/dictproto.h: one request read from a
connection, its words made with src/dictate.h and sent back. The payload
(src/dictated.c) does the listening; tests/dictserve_test.c talks to this
over a socket pair. */

#pragma once

#include "dictate.h"

/**
 * the request on connection c answered: the model at path opened into *d
 * the first time (and kept there for the next), threads at work on it; log
 * gets counts and codes only. The status sent (DICT_*), or -100 when the
 * connection broke before it could be
 **/
int dictserve_answer(int c, dictate_t **d, const char *path, int threads, void (*log)(const char *fmt, ...));
