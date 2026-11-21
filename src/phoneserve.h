/* Writing on the phone, served by the notify payload for the title, whose
sandbox may not listen; see phone.h for the protocol. While the title's
window is open it asks every moment ("type"), and a thread here listens on
PHONE_PORT of the console's address: it serves the page, gives it the
title's draft, and keeps the text the phone writes for the title. It stops
when the title says so ("untype"), or when the title has not asked for
PHONE_IDLE seconds. The klog gets codes and counts only: never the text, the
token or the phone's address. */

#pragma once

#include <stddef.h>

#include "phone.h"

/**
 * the title's "type": the server started for this window (screen) when it
 * does not run, the draft taken while the phone wrote nothing yet; where
 * it is into st, and the text into text (size bytes) with its length into
 * len when its number is not seq (len 0 otherwise)
 **/
void phoneserve_ask(unsigned screen, unsigned seq, int max, const char *what, const char *lang,
                    const char *draft, phone_state_t *st, char *text, size_t size, size_t *len);

/**
 * the title's "untype": its window closed, and the server with it; what
 * was written is forgotten
 **/
void phoneserve_close(void);
