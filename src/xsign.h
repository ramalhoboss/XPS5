/* The x-client-transaction-id header (xtid.h) from the network: a page of
x.com read with the user's session names the ondemand.s chunk, and the two
give xtid_init what it needs. The title and the notifications payload both
sign their requests with it. */

#pragma once

#include "xapi.h"
#include "xtid.h"

/**
 * read a page of x.com with the session s (the signed-in home page names
 * the chunk; the bare one is the fallback), then the chunk, into t; log
 * gets codes and counts only; returns 0 or -1
 **/
int xsign_load(xtid_t *t, const x_session_t *s, void (*log)(const char *fmt, ...));
