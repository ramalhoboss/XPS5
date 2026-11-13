/* TwitterPS5: posts a notification whose button opens X in the ShellUI
browser card next to the running game (see toast.c).

The texts follow the system language, with English as the fallback. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ps5/klog.h>

#include "toast.h"

#if __has_include("config.h")
#include "config.h"
#endif

#ifndef TWITTERPS5_URL
#define TWITTERPS5_URL "https://x.com/home"
#endif

#define SCE_SYSTEM_SERVICE_PARAM_ID_LANG 1


int sceSystemServiceParamGetInt(int paramId, int *value);


typedef struct text {
  const char *sub;    // line under "XPS5"
  const char *action; // button label
} text_t;


/**
 * notification texts for the system language (SceSystemParamLang)
 **/
static text_t
lang_text(int lang) {
  switch(lang) {
  case 2:  // French
  case 22: // French (Canada)
    return (text_t){"X à côté de votre jeu", "Ouvrir X"};
  case 3:  // Spanish
  case 20: // Spanish (Latin America)
    return (text_t){"X junto a tu juego", "Abrir X"};
  case 4:  // German
    return (text_t){"X neben deinem Spiel", "X öffnen"};
  case 5:  // Italian
    return (text_t){"X accanto al tuo gioco", "Apri X"};
  case 6:  // Dutch
    return (text_t){"X naast je game", "X openen"};
  case 7:  // Portuguese (Portugal)
  case 17: // Portuguese (Brazil)
    return (text_t){"X lado a lado com o jogo", "Abrir o X"};
  default:
    return (text_t){"X side by side with your game", "Open X"};
  }
}


int
main(void) {
  int lang = 1;
  unsigned id;
  text_t text;
  int err;

  if(sceSystemServiceParamGetInt(SCE_SYSTEM_SERVICE_PARAM_ID_LANG, &lang)) {
    lang = 1;
  }
  text = lang_text(lang);

  err = toast_post(TWITTERPS5_URL, text.sub, text.action, &id);
  printf("sceNotificationSend: 0x%08x (language %d, id %u)\n",
         (unsigned)err, lang, id);
  klog_printf("[TwitterPS5] sceNotificationSend: 0x%08x (language %d, id %u)\n",
              (unsigned)err, lang, id);

  // libSceNotification throws "resource deadlock would occur" from its exit
  // handlers, so skip them
  fflush(stdout);
  _exit(err ? EXIT_FAILURE : EXIT_SUCCESS);
}
