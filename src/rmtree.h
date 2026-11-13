/* rmtree: a folder removed with everything in it, for XPS5's uninstaller
(src/uninstall.c). Symbolic links are removed, never followed, so nothing
outside the folder goes. */

#pragma once

#define RMTREE_DEPTH 32 // folders deeper than this are left, and said

typedef struct rmtree_count {
  int files;  // removed: files and links
  int dirs;   // removed: folders, the top one too
  int failed; // what could not be removed (an entry may count more than once)
  int err_no; // errno of the first failure, or 0
} rmtree_count_t;

/**
 * path and everything in it removed; counts added to *c. 0 when it is gone
 * (or was never there), -1 when something of it stays
 **/
int rmtree(const char *path, rmtree_count_t *c);
