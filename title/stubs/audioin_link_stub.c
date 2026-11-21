/* The names of libSceAudioIn, the system's microphone library, for the
linker: the SDK has no stub of it. build-title.sh makes a shared object of
this file with the library's name, libSceAudioIn.sprx, so the title imports
these functions from it. Nothing here is packaged or runs on the console;
the system's own library answers. */

#include <stdint.h>

int sceAudioInOpen(int user, uint32_t type, uint32_t index, uint32_t len, uint32_t freq, uint32_t param) {
  (void)user; (void)type; (void)index; (void)len; (void)freq; (void)param;
  return -1;
}

int sceAudioInHqOpen(int user, uint32_t type, uint32_t index, uint32_t len, uint32_t freq, uint32_t param) {
  (void)user; (void)type; (void)index; (void)len; (void)freq; (void)param;
  return -1;
}

int sceAudioInInput(int handle, void *dest) {
  (void)handle; (void)dest;
  return -1;
}

int sceAudioInClose(int handle) {
  (void)handle;
  return -1;
}

int sceAudioInGetSilentState(int handle) {
  (void)handle;
  return -1;
}
