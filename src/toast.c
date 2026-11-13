/* Notification with a deep link into the ShellUI browser card. The card is
the only browser that sits next to a running game: it can be resized and
docked left or right, and the PS button pressed twice switches between game
and card. Opening any app instead closes the game. */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "toast.h"

#define SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM 0xFE


int sceNotificationSend(int userId, bool isLogged, const char *payload);


/**
 * percent-encode everything but the URL unreserved characters
 **/
static void
url_encode(const char *in, char *out, size_t size) {
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;

  for(; *in && n + 4 < size; in++) {
    unsigned char c = (unsigned char)*in;
    if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
       (c >= '0' && c <= '9') || strchr("-_.~", c)) {
      out[n++] = (char)c;
    } else {
      out[n++] = '%';
      out[n++] = hex[c >> 4];
      out[n++] = hex[c & 15];
    }
  }
  out[n] = 0;
}


/**
 * escape a string for use inside a JSON string literal
 **/
static void
json_escape(const char *in, char *out, size_t size) {
  size_t n = 0;

  for(; *in && n + 3 < size; in++) {
    if(*in == '"' || *in == '\\') {
      out[n++] = '\\';
    }
    out[n++] = *in;
  }
  out[n] = 0;
}


/**
 * deep link that opens the browser card on url; the ShellUI refuses links
 * with characters outside [:/?#[]@!$&'()*+,;=-._~%A-z0-9], so the JSON values
 * are percent-encoded. Split screen only works with the card header shown,
 * so there is no hideHeader.
 **/
static void
card_link(const char *url, char *out, size_t size) {
  char url_json[1024];
  char param[1280];
  char anchor[256];
  char enc[4096];

  json_escape(url, url_json, sizeof url_json);
  snprintf(param, sizeof param,
           "{\"url\":\"%s\",\"userAgent\":\"\","
           "\"enableSplitScreen\":true,\"enablePinP\":true}", url_json);
  url_encode("{\"vertical\":\"center\",\"horizontal\":\"center\"}", anchor, sizeof anchor);
  url_encode(param, enc, sizeof enc);
  snprintf(out, size,
           "pscontrolcenter:main?target=action-card&type=ActionCardWebBrowser"
           "&mode=selected&anchor=%s&param=%s", anchor, enc);
}


int
toast_post(const char *url, const char *sub, const char *action, unsigned *id) {
  static char toast[12288];
  char link[4608], link_json[4608];
  char sub_json[512], action_json[256];
  char created[32];
  time_t now = time(0);

  card_link(url, link, sizeof link);
  json_escape(link, link_json, sizeof link_json);
  json_escape(sub, sub_json, sizeof sub_json);
  json_escape(action, action_json, sizeof action_json);

  // a new id and date every run, so the toast shows again instead of
  // being treated as the notification already in the list
  strftime(created, sizeof created, "%Y-%m-%dT%H:%M:%S.000Z", gmtime(&now));
  *id = (unsigned)now;

  snprintf(toast, sizeof toast,
    "{\n"
    "  \"rawData\": {\n"
    "    \"viewTemplateType\": \"InteractiveToastTemplateB\",\n"
    "    \"channelType\": \"Downloads\",\n"
    "    \"useCaseId\": \"IDC\",\n"
    "    \"toastOverwriteType\": \"No\",\n"
    "    \"isImmediate\": true,\n"
    "    \"priority\": 100,\n"
    "    \"viewData\": {\n"
    "      \"icon\": {\"type\": \"Predefined\", \"parameters\": {\"icon\": \"download\"}},\n"
    "      \"message\": {\"body\": \"XPS5\"},\n"
    "      \"subMessage\": {\"body\": \"%s\"},\n"
    "      \"actions\": [\n"
    "        {\"actionName\": \"%s\", \"actionType\": \"DeepLink\",\n"
    "         \"defaultFocus\": true, \"parameters\": {\"actionUrl\": \"%s\"}}\n"
    "      ]\n"
    "    },\n"
    "    \"platformViews\": {\n"
    "      \"previewDisabled\": {\n"
    "        \"viewData\": {\n"
    "          \"icon\": {\"type\": \"Predefined\", \"parameters\": {\"icon\": \"download\"}},\n"
    "          \"message\": {\"body\": \"XPS5: %s\"}\n"
    "        }\n"
    "      }\n"
    "    }\n"
    "  },\n"
    "  \"createdDateTime\": \"%s\",\n"
    "  \"localNotificationId\": \"%u\"\n"
    "}",
    sub_json, action_json, link_json, sub_json, created, *id);

  printf("link: %s\n", link);
  return sceNotificationSend(SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM, true, toast);
}
