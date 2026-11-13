/* The PS5's keyboard, see ime.h. The structures are SceImeDialogParam and
SceImeDialogResult as the PS4 SDK has them, which shadPS4 and OpenOrbis
describe the same way. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ime.h"
#include "text.h"

// a title's sandbox shows the system's libraries under a random directory,
// /<word>/common/lib, as libSceSysmodule finds them; /system is the path
// outside a sandbox
#define IME_LIBRARY "libSceImeDialog.sprx"
#define IME_MAX     2048 // UTF-16 units of the text, two for some characters

#define ENTER_LABEL_SEND 1
#define ENTER_LABEL_GO   3
#define ALIGN_CENTRE     1

// SceImeType and SceImeOption
enum { TYPE_DEFAULT, TYPE_BASIC_LATIN, TYPE_URL, TYPE_MAIL, TYPE_NUMBER };
#define OPTION_NO_AUTO_CAPS 0x02
#define OPTION_PASSWORD     0x04
#define OPTION_NO_LEARNING  0x20

enum { STATUS_NONE, STATUS_RUNNING, STATUS_FINISHED };
enum { END_OK, END_CANCELLED, END_ABORTED };


typedef struct ime_param {        // SceImeDialogParam
  int32_t         user;
  int32_t         type;           // 0: any text
  uint64_t        languages;      // 0: the user's
  int32_t         enter_label;
  int32_t         input_method;   // 0: the default
  void           *filter;
  uint32_t        option;         // 0: one line
  uint32_t        max_length;
  uint16_t       *text;
  float           x, y;           // in 1920 x 1080
  int32_t         align_x, align_y;
  const uint16_t *placeholder;
  const uint16_t *title;
  int8_t          reserved[16];
} ime_param_t;

typedef struct ime_result {       // SceImeDialogResult
  int32_t status;                 // END_*
  int32_t reserved[12];
} ime_result_t;


int sceKernelLoadStartModule(const char *path, size_t args, const void *argp, uint32_t flags,
                             void *opt, int *res);
const char *sceKernelGetFsSandboxRandomWord(void);

// linked from the SDK's libSceImeDialog stub, the way games call them:
// sceKernelDlsym does not find them by name in a title (0x80020003)
int sceImeDialogInit(const ime_param_t *param, const void *extended);
int sceImeDialogGetStatus(void);
int sceImeDialogGetResult(ime_result_t *result);
int sceImeDialogTerm(void);


static void (*g_log)(const char *fmt, ...);

static int (*g_init)(const ime_param_t *param, const void *extended);
static int (*g_status)(void);
static int (*g_result)(ime_result_t *result);
static int (*g_term)(void);

static uint16_t g_text[IME_MAX + 1];
static uint16_t g_title[128], g_placeholder[128];


void
ime_set_log(void (*log)(const char *fmt, ...)) {
  g_log = log;
}


/**
 * the library from where the sandbox shows it, else from /system; returns
 * its handle or the SCE error of the last try
 **/
static int
load_library(void) {
  char path[160];
  const char *word = sceKernelGetFsSandboxRandomWord();
  int handle = -1;

  if(word && *word) {
    snprintf(path, sizeof path, "/%s/common/lib/" IME_LIBRARY, word);
    handle = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, NULL);
    if(g_log) {
      g_log("keyboard: library from the sandbox 0x%08x", (unsigned)handle);
    }
  } else if(g_log) {
    g_log("keyboard: no sandbox directory");
  }
  if(handle < 0) {
    handle = sceKernelLoadStartModule("/system/common/lib/" IME_LIBRARY, 0, NULL, 0, NULL, NULL);
    if(g_log) {
      g_log("keyboard: library from /system 0x%08x", (unsigned)handle);
    }
  }
  return handle;
}


/**
 * the library loaded, so that the linked functions have somewhere to go;
 * returns 0 or the SCE error of loading it
 **/
static int
load(void) {
  int handle = load_library();

  if(handle < 0) {
    return handle;
  }
  g_status = sceImeDialogGetStatus;
  g_result = sceImeDialogGetResult;
  g_term = sceImeDialogTerm;
  g_init = sceImeDialogInit;
  return 0;
}


static int
open_as(int user, const char *title, const char *placeholder, const char *text, int max, int type,
        uint32_t option, int enter) {
  ime_param_t param;
  int ret;

  if(!g_init && (ret=load())) {
    return ret;
  }
  memset(g_text, 0, sizeof g_text);
  if(text && *text) {
    text_to_utf16(text, g_text, sizeof g_text / sizeof g_text[0]);
  }
  text_to_utf16(title, g_title, sizeof g_title / sizeof g_title[0]);
  text_to_utf16(placeholder, g_placeholder, sizeof g_placeholder / sizeof g_placeholder[0]);

  memset(&param, 0, sizeof param);
  param.user = user;
  param.type = type;
  param.option = option;
  param.enter_label = enter;
  param.max_length = max < IME_MAX ? max : IME_MAX;
  param.text = g_text;
  param.x = 960;
  param.y = 540;
  param.align_x = ALIGN_CENTRE;
  param.align_y = ALIGN_CENTRE;
  param.placeholder = g_placeholder;
  param.title = g_title;
  return g_init(&param, NULL);
}


int
ime_open(int user, const char *title, const char *placeholder, const char *text, int max) {
  return open_as(user, title, placeholder, text, max, TYPE_DEFAULT, 0, ENTER_LABEL_SEND);
}


int
ime_open_field(int user, const char *title, const char *placeholder, const char *text, int max, int field) {
  const uint32_t plain = OPTION_NO_AUTO_CAPS | OPTION_NO_LEARNING;
  int ret;

  switch(field) {
  case IME_FIELD_PASSWORD:
    // hidden characters: the PS4 SDK's keyboard has them with its Latin
    // type; then with any, but never shown in clear
    if(!(ret=open_as(user, title, placeholder, NULL, max, TYPE_BASIC_LATIN, plain | OPTION_PASSWORD,
                     ENTER_LABEL_GO))) {
      return 0;
    }
    if(g_log) {
      g_log("keyboard: no password keyboard with the Latin type, 0x%08x", (unsigned)ret);
    }
    return open_as(user, title, placeholder, NULL, max, TYPE_DEFAULT, plain | OPTION_PASSWORD, ENTER_LABEL_GO);
  case IME_FIELD_EMAIL:
    return open_as(user, title, placeholder, text, max, TYPE_MAIL, plain, ENTER_LABEL_GO);
  case IME_FIELD_NUMBER:
    return open_as(user, title, placeholder, text, max, TYPE_NUMBER, OPTION_NO_LEARNING, ENTER_LABEL_GO);
  default:
    return open_as(user, title, placeholder, text, max, TYPE_DEFAULT, plain, ENTER_LABEL_GO);
  }
}


int
ime_poll(char *text, size_t size) {
  ime_result_t result;
  int status;

  text[0] = 0;
  if(!g_init) {
    return IME_FAILED;
  }
  if((status=g_status()) == STATUS_RUNNING) {
    return IME_RUNNING;
  }
  if(status != STATUS_FINISHED) {
    g_term();
    return IME_FAILED;
  }
  memset(&result, 0, sizeof result);
  if((status=g_result(&result)) || result.status != END_OK) {
    if(g_log) {
      g_log("keyboard: result 0x%08x, end %d", (unsigned)status, (int)result.status);
    }
    g_term();
    memset(g_text, 0, sizeof g_text);
    return IME_CANCELLED;
  }
  text_from_utf16(g_text, text, size);
  memset(g_text, 0, sizeof g_text);
  g_term();
  return IME_DONE;
}
