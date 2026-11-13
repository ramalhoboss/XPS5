/* QR codes, for the address the linking screen shows (a phone's camera
opens it): byte mode, error correction level M, versions 1 to 6 (up to 106
bytes), the mask with the lowest penalty. Made after the QR Code standard
(ISO/IEC 18004); nothing here touches the PS5, so tests/title_test.c checks
it on the host. */

#pragma once

#include <stddef.h>

#define QR_MAX_VERSION 6
#define QR_MAX_SIZE    (QR_MAX_VERSION * 4 + 17) // modules on a side
#define QR_MAX_BYTES   106                       // what version 6 at level M takes

typedef struct qr {
  int           size;                            // modules on a side, 21 to QR_MAX_SIZE
  int           version, mask;
  unsigned char dark[QR_MAX_SIZE][QR_MAX_SIZE];  // [row][column], 1 dark
} qr_t;

/**
 * the QR code of len bytes of text, in the smallest version they fit;
 * returns 0, or -1 when they are more than QR_MAX_BYTES
 **/
int qr_encode(const char *text, size_t len, qr_t *qr);
