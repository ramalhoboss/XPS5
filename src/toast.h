#pragma once

/**
 * post a logged notification with one button that opens url in the ShellUI
 * browser card (ActionCardWebBrowser) with split screen enabled; returns the
 * sceNotificationSend result and stores the notification id in *id
 **/
int toast_post(const char *url, const char *sub, const char *action, unsigned *id);
