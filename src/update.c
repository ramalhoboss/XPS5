/* XPS5's own updates: reading update.json (see update.h). */

#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "update.h"


/**
 * the numbers of a version ("1.10.3") into n, at most 4 of 5 digits each;
 * how many, or 0 when it is not one
 **/
static int
version_parts(const char *v, long n[4]) {
  int parts = 0, digits = 0;

  if(!v || !*v) {
    return 0;
  }
  n[0] = n[1] = n[2] = n[3] = 0;
  for(; *v; v++) {
    if(*v >= '0' && *v <= '9') {
      if(++digits > 5) {
        return 0;
      }
      n[parts] = n[parts] * 10 + (*v - '0');
    } else if(*v == '.' && digits && parts < 3) {
      parts++;
      digits = 0;
    } else {
      return 0;
    }
  }
  return digits ? parts + 1 : 0;
}


int
update_newer(const char *have, const char *found) {
  long a[4], b[4];

  if(!version_parts(have, a) || !version_parts(found, b)) {
    return 0;
  }
  for(int i=0; i < 4; i++) {
    if(a[i] != b[i]) {
      return b[i] > a[i];
    }
  }
  return 0;
}


static int
hex_value(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
         c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}


/**
 * whether name can be the install ELF's file: no folder, no hidden file
 **/
static int
file_ok(const char *name) {
  size_t n = name ? strlen(name) : 0;

  if(n < 5 || n >= UPDATE_FILE || name[0] == '.' || strcmp(name + n - 4, ".elf")) {
    return 0;
  }
  for(size_t i=0; i < n; i++) {
    char c = name[i];
    if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
         c == '_')) {
      return 0;
    }
  }
  return 1;
}


int
update_parse(char *json, size_t len, update_t *u) {
  const json_node_t *root, *size;
  const char *version, *file, *sha;
  json_doc_t *doc;
  size_t err_off;
  long n[4];
  char *end;
  unsigned long long bytes;
  int ret = -1;

  memset(u, 0, sizeof *u);
  if(!(doc=json_parse(json, len, &err_off))) {
    return -1;
  }
  root = json_root(doc);
  version = json_string(json_get(root, "version"));
  file = json_string(json_get(root, "file"));
  sha = json_string(json_get(root, "sha256"));
  size = json_get(root, "size");
  if(version && strlen(version) < sizeof u->version && version_parts(version, n) && file_ok(file) &&
     sha && strlen(sha) == 64 && size && size->type == JSON_NUMBER && size->str && size->str[0] >= '1' &&
     size->str[0] <= '9') {
    bytes = strtoull(size->str, &end, 10);
    ret = (size_t)(end - size->str) == size->len && bytes <= UPDATE_MAX ? 0 : -1;
    for(int i=0; !ret && i < 32; i++) {
      int hi = hex_value(sha[2 * i]), lo = hex_value(sha[2 * i + 1]);
      if(hi < 0 || lo < 0) {
        ret = -1;
      } else {
        u->sha256[i] = (unsigned char)(hi << 4 | lo);
      }
    }
    if(!ret) {
      memcpy(u->version, version, strlen(version) + 1);
      memcpy(u->file, file, strlen(file) + 1);
      u->size = (size_t)bytes;
    }
  }
  json_free(doc);
  if(ret) {
    memset(u, 0, sizeof *u);
  }
  return ret;
}


int
update_file_url(const char *manifest, const char *file, char *out, size_t size) {
  const char *slash, *host;
  size_t dir;

  if(size) {
    out[0] = 0;
  }
  if(!manifest || strncmp(manifest, "https://", 8) || strpbrk(manifest, "?# \"\\") || !file_ok(file)) {
    return -1;
  }
  host = manifest + 8;
  if(!(slash=strchr(host, '/')) || slash == host) {
    return -1;
  }
  slash = strrchr(manifest, '/');
  dir = (size_t)(slash - manifest) + 1;
  if(dir + strlen(file) + 1 > size) {
    return -1;
  }
  memcpy(out, manifest, dir);
  memcpy(out + dir, file, strlen(file) + 1);
  return 0;
}
