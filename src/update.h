/* XPS5's own updates. update.json, published in a folder that XPS5_UPDATE_URL
names, says the newest version, the install ELF's file in that same folder,
its size and its SHA-256:

  {"version":"1.1.0","file":"XPS5.elf","size":21378312,"sha256":"<64 hex>"}

The title compares the version with its own, fetches the file over verified
TLS, checks its size and SHA-256, and hands it to the ELF loader, which runs
the installer (src/install.c): that closes XPS5 and writes the new one.

Both live on the project's GitHub Pages, which .github/workflows/pages.yml
fills from each published release. Pages is used because its certificate is
a Let's Encrypt one of tls.h (*.github.io <- YR1 <- Root YR <- ISRG Root X1,
checked 2026-11-13); github.com, and so a release's download links, is
Sectigo's, which XPS5 does not carry. */

#pragma once

#include <stddef.h>

#define XPS5_VERSION "1.0.1"

// update.json's address (https); empty, this build does not look for updates.
// A fork points it at its own Pages
#ifndef XPS5_UPDATE_URL
#define XPS5_UPDATE_URL "https://ramalhoboss.github.io/XPS5/update.json"
#endif

#define UPDATE_VERSION 24          // "1.10.3", with room
#define UPDATE_FILE    64          // the file's name
#define UPDATE_MAX     (64 << 20)  // bytes of the install ELF, at most

typedef struct update {
  char          version[UPDATE_VERSION];
  char          file[UPDATE_FILE];
  size_t        size;
  unsigned char sha256[32];
} update_t;

/**
 * update.json (json, len bytes, changed in place) into u: a version of
 * numbers and dots, a file name of letters, digits, '.', '-' and '_' that
 * ends in ".elf", a size up to UPDATE_MAX and a SHA-256 in hex; 0, or -1
 **/
int update_parse(char *json, size_t len, update_t *u);

/**
 * whether the version found is newer than have ("1.10.0" after "1.9.2");
 * 0 when either is not a version
 **/
int update_newer(const char *have, const char *found);

/**
 * the address of file in the folder of update.json's address manifest (an
 * https one, without '?' or '#'); 0, or -1
 **/
int update_file_url(const char *manifest, const char *file, char *out, size_t size);
