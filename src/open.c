/* TwitterPS5 open: brings up the TwitterPS5 title (TWPS00001) over the
running game in one step.

Opened from the home screen while a game is in front, the title fails the
first time: the system suspends the game, then refuses its own relaunch
because the title was not installed from a pkg (0x80940033, m_is_pkg). A
plain launch made while the game is already suspended works, and the system
swaps the game out to the SSD. This payload does the same: suspend the game
in front, then launch the title, or resume it if it is already open. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ps5/klog.h>

#define TITLE_ID "TWPS00001"


typedef struct lnc_app_param {
  uint32_t size;
  int32_t  user_id;
  uint32_t app_opt;
  uint64_t crash_report;
  uint32_t check_flag;
} lnc_app_param_t;

int sceLncUtilGetAppId(const char *title_id);
int sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilSuspendApp(int app_id, int cause);
int sceLncUtilResumeApp(int app_id, int cause);
int sceLncUtilLaunchApp(const char *title_id, const char **argv,
                        lnc_app_param_t *param);

int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(int *user_id);


static void
say(const char *what, int ret) {
  printf("%s: 0x%08x\n", what, (unsigned)ret);
  klog_printf("[TwitterPS5 open] %s: 0x%08x\n", what, (unsigned)ret);
}


int
main(void) {
  lnc_app_param_t param = {.size = sizeof param};
  int title = sceLncUtilGetAppId(TITLE_ID);
  int front = sceLncUtilGetAppIdOfRunningBigApp();
  int ret;

  say("title app id", title);
  say("big app in front", front);

  // a suspended game stays on the SSD while the title runs, and comes back
  // where it was when it is picked again
  if(front > 0 && front != title) {
    say("suspend the game", sceLncUtilSuspendApp(front, 0));
    usleep(500 * 1000);
  }

  if(title > 0) {
    ret = sceLncUtilResumeApp(title, 0);
    say("resume " TITLE_ID, ret);
  } else {
    sceUserServiceInitialize(0);
    if(sceUserServiceGetForegroundUser(&param.user_id)) {
      param.user_id = -1;
    }
    ret = sceLncUtilLaunchApp(TITLE_ID, 0, &param);
    say("launch " TITLE_ID, ret);
  }

  fflush(stdout);
  _exit(ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
}
