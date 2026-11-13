/* Linking an X account from a PC, served by the notify payload for the
title (src/link.h has the protocol). The title is a media app, whose
sandbox may not listen on a port, so while its screen is open it asks the
payload over the control port (src/notify.c): "link <screen> <account>
<lang>" every moment (screen counts the screens the title opened), "unlink"
once the screen closes. A thread of the payload then
listens on LINK_PORT of the console's address, answers the XPS5 extension,
tries the session it sends on X and puts it in the account's file. It
stops when the title says so, once an account is linked, or when the title
has not asked for LINKSERVE_IDLE seconds (it closed). Only codes and counts
go to the klog: never the cookies, the code, the PC's address or whose
account it is. */

#pragma once

#include <stddef.h>

#include "link.h"

#define LINKSERVE_IDLE 60 // seconds without the title asking before it stops

/**
 * the title's "link": the server started for this screen when it does not
 * run (or stopped before it linked), the account it links (1 to 5) changed
 * while it waits, the language the extension is told (two letters) kept;
 * where it is into st
 **/
void linkserve_ask(unsigned screen, int account, const char *lang, link_state_t *st);

/**
 * the title's "unlink": its screen closed, and the server with it
 **/
void linkserve_close(void);

/**
 * a session tried on X: 0 and whose it is (a screen name) into user, or
 * X's HTTP status, or -1 when X could not be reached; notify.c's
 **/
int linkserve_check(const char *auth_token, const char *ct0, char *user, size_t size);
