/* Certificates the payloads trust for X's servers. Payloads get no root
certificates from the system (sceHttpsGetCaList answers 0x80020002), so they
bring their own, embedded at build time from certs/ (build/certs.h).

ISRG Root X1 anchors the Let's Encrypt chains of x.com, api.x.com and
*.twimg.com: leaf <- YR1 or YR2 <- Root YR, cross-signed by X1 (checked
2026-11-03). X2 is the ECDSA sibling of X1. Root YR and its intermediates
are needed too: the PS5's verifier does not follow the cross-signature to
X1, and leaves the root unknown with X1 alone. They were checked against X1
with openssl verify. */

#pragma once

#include <stddef.h>

#define TLS_ROOTS 2 // ISRG Root X1 and X2
#define TLS_CERTS 5 // the roots, then Root YR (signed by X1), YR1 and YR2


typedef struct tls_cert {
  const unsigned char *data;
  size_t               size;
} tls_cert_t;


/**
 * the certificates as PEM text, the roots first
 **/
void tls_certs_pem(tls_cert_t certs[TLS_CERTS]);

/**
 * turn the first count PEM certificates into DER, decoded into buf; returns
 * 0, or -1 when buf is too small
 **/
int tls_certs_der(tls_cert_t *certs, int count, unsigned char *buf, size_t size);

/**
 * DER bytes of a PEM certificate, written to out; returns how many, or 0
 * when they do not fit
 **/
size_t tls_pem_to_der(const unsigned char *pem, size_t len, unsigned char *out, size_t size);
