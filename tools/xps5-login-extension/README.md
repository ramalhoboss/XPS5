# XPS5 Login

A browser extension for Chrome, Edge and Firefox (Firefox for Android too)
that links the X account signed in to that browser to XPS5 on the PS5, so
there is no `session.txt` to copy by FTP.

## Install (once)

- **Chrome or Edge:** open `chrome://extensions` (or `edge://extensions`), turn
  on **Developer mode** and load this folder (`tools/xps5-login-extension`) as
  an unpacked extension.
- **Firefox (PC):** open `about:debugging#/runtime/this-firefox`, press **Load
  Temporary Add-on** and pick this folder's `manifest.json`. A temporary add-on
  lasts until Firefox closes; one signed by addons.mozilla.org stays.
- **Firefox for Android:** extensions there come from addons.mozilla.org, so
  this one works there once it is published on that site (it is not yet).
- **iPhone and iPad:** their browsers do not run this extension. Use a PC, or an
  Android phone with Firefox.

## Link an account

1. On the PS5, open XPS5 → Settings → X account and press ✕, or press
   **Link from a PC** where XPS5 says the session is missing. The screen shows a
   QR code, the PS5's address and a six-digit code. ← → choose which of the five
   accounts the session goes to.
2. Sign in to x.com in the browser that has the extension.
3. Either:
   - **With the QR:** scan it with the phone (or open the address on the PC).
     It opens a page the PS5 serves, which says where the extension's button
     is. Press the button there: the extension's page opens with the address
     and the code filled in. Press **Link**.
   - **By hand:** click the extension's button anywhere, type the address and
     the code, and press **Link**.

   The browser asks once for access to that address.

The PS5 checks the session with X before it saves it in
`/data/TwitterPS5/session.txt` (or `session2.txt` to `session5.txt`), then
reads the lists again with it. XPS5's notify payload serves the page and
answers the extension on port 9070 while the PS5's screen is open; XPS5
starts it for that. The screen says when the QR's page was opened.

Do not sign out of X in that browser afterwards: X ends the session on the PS5
too. Closing the tab or the browser is fine.

## Language

The page speaks English until the PS5 answers, then the language XPS5 shows
on the console (the PS5's own unless changed in XPS5's Settings): English,
Português, Español, Français, Deutsch, Italiano or Nederlands. It opens in
that one the next time. The texts are in `lang/<two letters>.json`. The page
the PS5 serves (`src/linkpage/`) speaks the phone's or the PC's language.

## What it does with the session

- It reads only the `auth_token` and `ct0` cookies of x.com, and only after the
  PS5 answered at the address given.
- It sends them only to that address, which must be a home network one
  (10.x, 172.16–31.x, 192.168.x, 169.254.x), over plain HTTP on port 9070:
  the same network the FTP way used.
- Pressed on the PS5's page, it reads that tab's address once (the
  `activeTab` permission: only the tab the button was pressed on, only then).
  The code travels after `#`, so it is never sent to any server, and the
  extension's page takes it out of its own address at once.
- It keeps only the address and the PS5's language (`storage.local`), never
  the cookies or the code, and gives the access to the address back once the
  account is linked.
- The PS5 listens only while its linking screen is open, answers the session
  only to browser extensions, and draws a new code after five wrong ones. Its
  page is static, with a policy that lets it load only its own script and
  style.

The protocol is in `src/link.h`.
