/* Root certificates for X's servers, see tls.h. */

#include <string.h>

#include "certs.h"
#include "tls.h"


void
tls_certs_pem(tls_cert_t certs[TLS_CERTS]) {
  certs[0] = (tls_cert_t){isrg_root_x1_pem, sizeof isrg_root_x1_pem};
  certs[1] = (tls_cert_t){isrg_root_x2_pem, sizeof isrg_root_x2_pem};
  certs[2] = (tls_cert_t){le_root_yr_pem, sizeof le_root_yr_pem};
  certs[3] = (tls_cert_t){le_yr1_pem, sizeof le_yr1_pem};
  certs[4] = (tls_cert_t){le_yr2_pem, sizeof le_yr2_pem};
}


int
tls_certs_der(tls_cert_t *certs, int count, unsigned char *buf, size_t size) {
  size_t used = 0, n;

  for(int i=0; i<count; i++) {
    if(!(n=tls_pem_to_der(certs[i].data, certs[i].size, buf + used, size - used))) {
      return -1;
    }
    certs[i] = (tls_cert_t){buf + used, n};
    used += n;
  }
  return 0;
}


size_t
tls_pem_to_der(const unsigned char *pem, size_t len, unsigned char *out, size_t size) {
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const unsigned char *end = pem + len;
  const unsigned char *p = memchr(pem, '\n', len);
  const char *c;
  unsigned bits = 0, nbits = 0;
  size_t n = 0;

  // the base64 runs from the line after BEGIN to the padding or the END line
  for(p = p ? p + 1 : end; p < end && *p != '-' && *p != '='; p++) {
    if(!*p || !(c=memchr(b64, *p, 64))) {
      continue; // line breaks
    }
    bits = ((bits << 6) | (unsigned)(c - b64)) & 0xFFFF;
    if((nbits += 6) >= 8) {
      nbits -= 8;
      if(n == size) {
        return 0;
      }
      out[n++] = (bits >> nbits) & 0xFF;
    }
  }
  return n;
}
