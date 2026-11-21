# XPS5 — X on your PS5, as a native app

![Platform](https://img.shields.io/badge/platform-PS5%20(jailbroken)-0070d1)
![Firmware](https://img.shields.io/badge/tested-FW%2013.60%20%2B%20Relapse-555)
![Language](https://img.shields.io/badge/code-C-3a3a3a)
![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)
![Release](https://img.shields.io/github/v/release/ramalhoboss/XPS5?color=orange&label=release)

XPS5 is an unofficial X (Twitter) client for jailbroken PS5 consoles. It is a **real native title** (`TWPS00001`) in the home screen's Media tab, beside your game. It is not a browser and not a web view: every pixel is drawn by XPS5's own renderer, and you use it with the controller.

![XPS5 on a PS5 at 4K: your profile, the side column with trends and who to follow, the controller hints, and an X notification arriving over it](docs/screenshots/profile.png)

<sub>Captured on a PS5 at 3840×2160. The notification in the top right corner is one of XPS5's own PS5 notifications.</sub>

> [!WARNING]
> **Read first.** XPS5 is an unofficial homebrew project. It is **not affiliated with, endorsed by or connected to X Corp., Sony Interactive Entertainment or anyone else**. It needs a jailbroken PS5. **Use it at your own risk**: you alone are responsible for your console and your X account. X may ask you to verify your account, or lock it for a while (error 326); if that happens, unlock it at x.com.

---

## Contents

- [Features](#features)
- [Technical specifications](#technical-specifications)
- [How it works](#how-it-works)
- [Requirements](#requirements)
- [Installation](#installation)
- [Logging in](#logging-in)
- [Controls](#controls)
- [Settings](#settings)
- [Updating and uninstalling](#updating-and-uninstalling)
- [Files on the console](#files-on-the-console)
- [Privacy and security](#privacy-and-security)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- [Project layout](#project-layout)
- [Roadmap](#roadmap)
- [Licences](#licences)

---

## Features

### X, the way you know it

- **Timelines**: For you and Following, with pictures, videos, GIFs, quotes, polls, badges and counts. Long posts show whole. Following remembers where you left off.
- **Post and reply** with the PS5 keyboard: up to 4 screenshots or 1 clip straight from your PS5 captures, polls, quotes and scheduled posts (X Premium).
- **Like, repost, bookmark, follow, mute and block**, and a post's ••• menu as on x.com: delete, pin, and edit (X Premium).
- **Explore** (Trending and News, without promoted items), **search**, **Notifications**, **Chat** (direct messages and encrypted X Chat), **Bookmarks**, **Lists**, **Scheduled posts**, **History** and **Articles**.
- **Encrypted X Chat**: your X Chat conversations open with your X Chat PIN, as in X's apps: text, photos, videos and voice notes, with reactions, replies and edits. Every message's signature is checked against its sender's key as X lists it. XPS5 asks for the PIN when you unlock X Chat and never writes it anywhere.
- **Community Notes**: when readers added context to a post, the note shows under it, as on x.com.
- **Grok, X's assistant**: *Translate with Grok* in a post's ••• menu, and *What you missed*: the posts in Following since last time, summed up in three lines (also each time XPS5 opens, with *Grok's catch-up on opening* on).
- **Profiles** with banner, bio, counts and their posts, and **Who to follow**.
- **Up to 5 X accounts** on one console, switched from the account menu at the bottom of the bar.

### Made for the PS5

- **X notifications over your game**: likes, reposts, follows, replies, mentions and messages arrive as real PS5 notifications, with an **Open** button. You choose how often notifications are read, from every 15 s to every 10 min; new messages are looked for every 6 s, and the Chat badge counts them.
- **It knows what you are playing**: the game and its hashtag go into the composer, and that game's captures come first.
- **Playing status** (optional): your X profile's location shows `🎮 <game> · PS5` while you play, and gets back what it said before when you stop.
- **Playing now**: which people you follow are playing, and what.
- **Spoiler shield**: posts about the game you are playing stay covered until you open them.
- **New screenshot or trophy?** XPS5 offers to post it on X.
- **Rising posts**: it tells you when one of your posts is taking off.
- **Grok hints** without spoilers for the game you are playing.
- **Friends**: the people you follow on X, as a PS5 friends list: who is playing and what, who is on XPS5, when each was last seen and what they played lately. Invite them to your game, ask to join theirs, or send them a message. If you like, a notification tells you when one of them starts a game or opens XPS5.
- **Trophies and Grok's tips**: the trophies of the game you are playing, the ones to get first, and a tip from Grok for each one, without story spoilers (hidden ones too).
- **Cards to post**: a card for the session you just played, one for your week on PS5, and one for each trophy you earn, from bronze to platinum.
- **Look for a group (LFG)**: post that you are looking for players for your game, see the groups that look for players, and invite whoever wrote one with R1.
- **Voice notes**: record one with the controller's microphone for a post, a reply or a direct message (up to 2:20). The voice notes you get can play on the controller's speaker.
- **Write on your phone**: scan a QR and type on your phone, and the words show on the TV as you type, for a post, a reply or a message.
- **Clips that fit**: a capture longer than X's 2:20 goes as its last 30 s, or its first 30 s, its last 15 s or whole, cut without encoding it again.
- **HDR screenshots**: the PS5's HDR screenshots (JPEG XR) show and post as they should, brought into SDR with an Ultra HDR gain map, so an HDR screen gets their light back.
- **Controller vibration** on new notifications and messages, and at the end of lists.
- **Listen to Spaces**: a post with a live or recorded Space has *Listen to the Space* in its ••• menu, and it keeps playing while you browse XPS5 and while you play a game, as XPS5's background service plays it (it goes quiet while a video plays in XPS5). Close XPS5 and it plays on; open XPS5 again to stop it.
- **Every account's notifications in one place**: with more than one account linked, *Notifications → Every account* puts them together, each with its account's colour and name. Replies and likes still go from the account in use.
- **Do not disturb in competitive games**: during Call of Duty, Fortnite, EA SPORTS FC and the like, notifications wait, and come in a single notice when the game closes.
- **Read later**: a post's ••• menu keeps it for after you play, in *History → Read later*.
- **Dictation, offline**: the composer's dictation button listens to the controller's microphone for up to 30 s and writes what you said into the post, in English (the tiny model hears other languages, European Portuguese above all, too badly). The words are made on the PS5 by Whisper (whisper.cpp with the tiny model), so the sound never leaves the console.
- **Videos** in the sharpest MP4 X offers for each one, with sound, in the timeline and full screen. Long ones play to the end: a download that breaks off goes on from where it stopped.
- **7 languages** (English, Português, Español, Français, Deutsch, Italiano, Nederlands), following your PS5's language.
- **Day and Night themes**, rendered at your TV's resolution, up to 4K. The controller's light bar takes the colour of the banner on screen.
- **Updates itself** (from 1.0.1): it tells you when a new version is out, and installs it from *Settings → Updates*.

---

## Technical specifications

| | |
| --- | --- |
| **Type** | Native PS5 title, media category (`applicationCategoryType` 65536), title ID `TWPS00001` |
| **Language** | C (63,542 lines in 120 files), plus 1,844 lines of C/C++ for the login window and 17,697 lines of host tests |
| **Rendering** | Own CPU renderer: anti-aliased shapes, TrueType text, frosted glass, pictures. The layout is 1920 × 1080 points, drawn at the picture's real pixels by 3 worker threads, then copied in 8 × 8 tiles straight into the PS5's video out buffers |
| **Resolutions** | 1080p, 1440p and 2160p (4K); *Auto* uses the TV's own resolution |
| **Text** | Inter and DejaVu Sans (stb_truetype), UTF-8 with emoji, line breaking and fallback fonts |
| **Network** | HTTPS through the PS5's own `libSceHttp` / `libSceSsl`. Certificates are **always verified**; payloads get no system CA store, so XPS5 ships its own Let's Encrypt roots (ISRG Root X1, X2, YR) |
| **X access** | X's web endpoints (GraphQL), the same requests x.com makes, with your own session and a computed `x-client-transaction-id`. No API key, nothing paid |
| **Video** | Own MP4 reader (progressive and fragmented) and FFmpeg 8.1.3's H.264 and AAC decoders, picture and sound kept in sync; seen playing up to 3840 × 2160. A video plays while it downloads, and a download that breaks off goes on from where it stopped |
| **Uploads** | Your screenshots and clips are read from `/user/av_contents` and uploaded to X in parts of up to 4 MiB, the way x.com's composer does it |
| **Pictures** | Kept in memory with a budget that follows the free flexible memory (210 MiB on a PS5 at 4K) |
| **Background payload** | A 2.1 MiB ELF that keeps running after XPS5 closes: X notifications and messages, the game you play, the playing status, your sessions of play, captures for the title, Spaces over your game, and the page for writing on your phone (port 9071, only while that screen is open). It answers XPS5 on `127.0.0.1:9047` |
| **X Chat** | Your identity key is recovered with your PIN from the Juicebox realms X names for your account (an OPRF, Shamir's shares and Noise NK, as Juicebox's own SDK does it). It opens each conversation's keys, and they open its messages, photos, videos and voice notes; libsodium and BearSSL do the cryptography. The PIN is never written anywhere |
| **Dictation payload** | A 3.2 MiB ELF with whisper.cpp, started by XPS5 the first time you dictate. It answers on `127.0.0.1:9049` only, keeps the Whisper tiny model (in 8 bits, 41.5 MiB, in `/data/TwitterPS5/whisper`) for 5 minutes, then goes. It works on one thread at the lowest priority: with XPS5 open the PS5 leaves the system only a core or two, and more threads froze its menus. On the PS5, with XPS5 open, a few seconds of speech become text in about 2 s (the first time, the model read, 2.3 s). About 185 MiB while it works (measured on a PC). Neither the sound nor the words are written anywhere |
| **Login window** | A WebKit build for the PS5 that runs as a separate process and is drawn inside XPS5 (ports 9073–9080 on the console). About 144 MiB on disk, 31.6 MiB packed in the installer |
| **Accounts** | Up to 5 |
| **Languages** | 7 |
| **Install size** | `XPS5.elf` 99.9 MiB (41.5 MiB of it the dictation model, written once), `XPS5-uninstall.elf` 109 KiB |
| **Tested on** | PS5 firmware 13.60 with Relapse and ShadowMountPlus |
| **Updates** | `update.json` (version, file, size, SHA-256) and the installer on GitHub Pages, filled from each release by a GitHub Actions workflow; the download is checked by size and SHA-256 before the ELF loader gets it |
| **Host testing** | Every module that does not need the console is tested on a PC with AddressSanitizer and UndefinedBehaviorSanitizer |

### Where the code goes

```mermaid
pie showData
    title XPS5's own code by part (lines of C)
    "Screens and layout (ui.c)" : 18505
    "Video, sound, voice notes and Spaces" : 8887
    "Background payload" : 7544
    "App core (main.c, settings, updates)" : 7271
    "X client (GraphQL, HTTPS, TLS, JSON)" : 6060
    "X Chat's encryption" : 5602
    "Languages, keyboard, linking a PC or phone" : 4039
    "Renderer (shapes, text, cards)" : 3132
    "Installer, uninstaller and tools" : 1842
    "Dictation" : 660
```

### What is inside `XPS5.elf`

```mermaid
pie showData
    title XPS5.elf, 99.9 MiB
    "Dictation model (Whisper tiny, 8 bits)" : 41.52
    "Login window (WebKit, xz-packed)" : 31.56
    "The title's eboot.bin" : 10.68
    "Home screen icon and backgrounds" : 9.39
    "Dictation payload" : 3.16
    "Background payload" : 2.10
    "libc.prx" : 1.23
    "Installer code" : 0.22
```

---

## How it works

### The pieces on the console

```mermaid
flowchart LR
    subgraph PS5["PS5 (jailbroken)"]
        direction TB
        LOADER["ELF loader<br/>port 9021"]
        INST["XPS5.elf<br/>installer"]
        SMP["ShadowMountPlus<br/>home screen tile"]
        TITLE["XPS5 title<br/>TWPS00001"]
        PAY["Background payload<br/>127.0.0.1:9047"]
        DICT["Dictation payload<br/>127.0.0.1:9049"]
        LOGIN["Login window<br/>WebKit process"]
        DATA[("/data/TwitterPS5<br/>sessions, settings,<br/>history, engine")]
        CAPS[("/user/av_contents<br/>your captures")]
    end
    X(["x.com / api.x.com<br/>pbs.twimg.com · video.twimg.com<br/>upload.x.com"])
    JB(["Juicebox realms<br/>X Chat's key backup"])

    LOADER --> INST
    INST -->|writes /data/homebrew/TWPS00001| SMP
    SMP -->|Media tab| TITLE
    TITLE -->|starts it, asks for the game, captures, settings| PAY
    TITLE -->|starts it to sign in| LOGIN
    TITLE -->|starts it, sends the sound, gets the words| DICT
    LOGIN -->|session cookies| DATA
    TITLE <--> DATA
    PAY <--> DATA
    TITLE --> CAPS
    TITLE ==>|HTTPS, verified| X
    PAY ==>|HTTPS, verified| X
    TITLE ==>|X Chat unlock, HTTPS| JB
    LOGIN ==>|X login pages only| X
```

### A frame on screen

```mermaid
flowchart LR
    PAD["Controller<br/>sticks, buttons"] --> UI["ui.c<br/>layout at 1920×1080 points"]
    NET["Timeline thread<br/>X requests, JSON"] --> UI
    IMG["Picture loaders (3)<br/>pbs.twimg.com"] --> UI
    VID["Video player<br/>MP4 + FFmpeg H.264/AAC"] --> UI
    UI --> DRAW["Renderer<br/>3 worker threads<br/>shapes, text, glass"]
    DRAW --> TILE["8×8 tiling<br/>into video out"]
    TILE --> TV["TV<br/>1080p / 1440p / 4K"]
```

### Notifications and the playing status

```mermaid
sequenceDiagram
    autonumber
    participant G as Your game
    participant P as Background payload
    participant X as X
    participant N as PS5 notifications
    loop every 15 s to 10 min (Settings)
        P->>X: read notifications (your session, HTTPS)
        X-->>P: the latest ones
        P->>N: the new ones, with an Open button
    end
    loop every 15 s while "Playing status" is on
        P->>G: which game is open (also when XPS5 is in front)
        P->>X: profile location set to 🎮 game name · PS5
    end
    Note over P,X: When the game closes, the location gets back what it said before
```

### Installing

```mermaid
sequenceDiagram
    autonumber
    actor U as You (PC)
    participant L as ELF loader (port 9021)
    participant I as XPS5.elf
    participant S as ShadowMountPlus
    U->>L: send XPS5.elf (send.ps1 or your sender)
    L->>I: run
    I->>I: close XPS5 if it is open
    I->>I: write the title to /data/homebrew/TWPS00001
    I->>I: write the login window to /data/TwitterPS5/engine (only when it changed)
    I->>I: write the dictation model to /data/TwitterPS5/whisper (only when it changed)
    I-->>U: PS5 notification "XPS5 written to /data/homebrew/TWPS00001"
    S->>S: registers the title (about 15 s)
    Note over S: XPS5 appears in Media on the home screen
```

---

## Requirements

- A **jailbroken PS5** with an **ELF loader on port 9021** (Relapse and etaHEN start one).
- **ShadowMountPlus** running, which puts titles installed in `/data/homebrew` on the home screen. Without it, XPS5's files are installed but no tile appears.
- An **internet connection** and an **X account**.
- To send the ELF from a PC: any payload sender, or `send.ps1` (Windows PowerShell) from the release.

---

## Installation

1. **Download** the latest `XPS5-<version>.zip` from the [Releases](../../releases) page and unzip it.
2. **Check the files** (optional, recommended). In PowerShell, in the unzipped folder:

   ```powershell
   Get-FileHash .\XPS5.elf -Algorithm SHA256
   ```

   The result must match the line for `XPS5.elf` in `SHA256SUMS.txt`.
3. **Load your jailbreak** on the PS5 (for example Relapse), and make sure ShadowMountPlus is running.
4. **Send `XPS5.elf` to port 9021** with the payload sender you already use. On Windows, the included `send.ps1` does it:

   ```powershell
   .\send.ps1 -Ps5 <your PS5's IP> -Elf .\XPS5.elf
   ```

   Find the PS5's IP in *Settings → Network → Connection Status → View Connection Status*. If PowerShell refuses to run the script, run it once with `powershell -ExecutionPolicy Bypass -File .\send.ps1 -Ps5 <IP> -Elf .\XPS5.elf`.
5. **Wait for the notification** *"XPS5 written to /data/homebrew/TWPS00001 …"*. The first install also writes the login window (about 144 MiB), so give it a moment.
6. **Open XPS5** from **Media** on the home screen. ShadowMountPlus adds it in about 15 seconds. The icon and background may only show after the PS5 restarts.
7. Press ✕ on **Log in to X** and sign in (see below).

---

## Logging in

Open XPS5 and press ✕ on **Log in to X**. X's own login page opens in a window inside XPS5: sign in with your username, email or phone and your password, as on x.com.

| In the login window | |
| --- | --- |
| Left stick | Move the pointer |
| ✕ | Click |
| △ | Open the PS5 keyboard for the field you are on |
| ↑ ↓ | Next or previous field |
| ○ | Close the window |

- Your password goes **only to X**, over HTTPS. XPS5 never keeps it.
- Only X's pages open in that window, so *Sign in with Google / Apple* does not work there. Its pages, cookies and cache are deleted when it closes.
- XPS5 keeps only X's two session cookies (`auth_token` and `ct0`) in `/data/TwitterPS5/session.txt` (`session2.txt` … `session5.txt` for the other accounts), on your console.
- The window tells X it is a PlayStation 5 browser, which it is.

**From a PC instead**: *Link your account from a PC* shows a QR code and an address. With the **XPS5 Login** extension (`tools/xps5-login-extension`, for Chrome, Edge and Firefox) on a browser signed in to x.com, open that address and press **Link**. The link only works on your home network while that screen is open.

**Accounts**: the account at the bottom of the bar opens a menu, as on x.com: your accounts on this console, **Add an existing account** and **Log out**. Logging out removes that account's session from the PS5.

---

## Controls

The left stick moves a pointer, and ✕ presses what is under it. The right stick scrolls. The side column always shows what each button does on the page you are on.

| Button | What it does |
| --- | --- |
| ↑ ↓ | Previous or next post |
| ← → | The sections of the bar |
| L1 / R1 | A page up or down |
| ✕ | Open the post (or press what the pointer is on) |
| ○ | Back |
| R2 | Like, or take the like back |
| L2 | Reply |
| Options | New post |
| △ | Back to the top (at the top of Explore: search) |
| □ | Refresh |
| PS | Back to your game |

---

## Settings

*More → Settings*:

| Setting | Values |
| --- | --- |
| Language | The PS5's, or one of the 7 |
| Theme | Day or Night |
| Screen | Auto (the TV's), 1080p, 1440p or 4K |
| Notifications over the game | On or off, read every 15, 20 or 30 s, 1, 2, 5 or 10 min |
| Playing status in your X profile | On or off (off by default) |
| Spoiler shield, light bar colour, rising posts | On or off |
| Grok's catch-up on opening | On or off |
| Controller vibration, voice notes on the controller's speaker | On or off |
| Friends playing and online (a notification) | On or off |
| Share captures and trophies (and the cards) | On or off |
| Online on your profile | On or off |
| Do not disturb in competitive games | On or off (on by default) |
| Video sound, pointer sound, videos in the timeline, data saver | On or off |
| Break reminder | Off, 30, 60 or 120 min |
| Updates | Your version; ✕ looks for a new one, or installs the one found |

---

## Updating and uninstalling

**Updating inside XPS5** (from 1.0.1): XPS5 looks for a new version each time it opens, and again when you press ✕ on *Settings → Updates*. When one is out, that row says so: press ✕ and XPS5 downloads it from the project's GitHub Pages, checks its size and SHA-256 against `update.json`, and hands it to the ELF loader on port 9021. The installer then closes XPS5 and writes the new version: open it again from the home screen. Version 1.0.0 does not look for updates, so install 1.0.1 by hand once. Version 1.0.1 takes installers of up to 64 MiB, and 1.1.0's is 100 MiB (it carries the dictation model), so install 1.1.0 by hand once too; from 1.1.0 on, XPS5 takes up to 160 MiB.

**Updating by hand**: send the newer `XPS5.elf` the same way as the first time. The installer closes XPS5 by itself if it is open.

Either way, your accounts and settings stay, and the login window is only written again when it changed. A running background payload from an older version is replaced the next time XPS5 opens.

**Uninstalling**: send `XPS5-uninstall.elf` the same way:

```powershell
.\send.ps1 -Ps5 <your PS5's IP> -Elf .\XPS5-uninstall.elf
```

It closes XPS5, gives each X profile that shows a game the location it had before, stops the background payload, asks ShadowMountPlus to take the tile off the home screen, and deletes everything XPS5 wrote (below). Your captures and your other titles are not touched.

---

## Files on the console

| Path | What is there |
| --- | --- |
| `/data/homebrew/TWPS00001/` | The title: `eboot.bin`, `libc.prx`, `sce_sys/` (icon, backgrounds, `param.json`), the background payload |
| `/data/TwitterPS5/session.txt`, `session2.txt` … | Each account's two X cookies |
| `/data/TwitterPS5/settings.txt` | Your settings |
| `/data/TwitterPS5/history.bin` | The posts you opened (History) |
| `/data/TwitterPS5/later.bin` | The posts you keep in Read later |
| `/data/TwitterPS5/sessions.txt` | Your sessions of play of the last weeks, for the cards |
| `/data/TwitterPS5/playingN.txt` | What each profile's location said before the playing status |
| `/data/TwitterPS5/engine/` | The login window |
| `/data/TwitterPS5/whisper/` | The dictation model |

---

## Privacy and security

- XPS5 talks **only to X**: x.com and X's servers for pictures, videos and uploads, and, in the login window, whatever X's own login page loads.
- To open X Chat, it also talks to the Juicebox realms X names for your account, X Chat's key backup, as X's apps do. They get Juicebox's own tokens and its blinded requests: never your PIN, your X session or your messages.
- The one other exception is its own updates: it reads `update.json` from `ramalhoboss.github.io` when it opens and when you look for updates, and downloads the installer from there when you choose to install. Those requests carry no account data or cookies.
- Your sessions, settings and history **stay on your console**.
- HTTPS certificates are always verified. There is no "accept any certificate" mode.
- The console's log (klog) only gets **status codes and counts**: never your session, posts, messages, keys, addresses or what you type or say.
- The ELF files contain no account data, so they can be shared.
- XPS5 does not change any PS5 system setting.

---

## Troubleshooting

| What you see | What to do |
| --- | --- |
| No XPS5 tile after installing | ShadowMountPlus is not running, or has not scanned yet: start it and wait about 15 s |
| The tile has no icon or background | Restart the PS5 once |
| *Not refreshed (no connection)* | Check the PS5's internet connection, then press □ |
| *Not refreshed (HTTP 401/403)* | The session expired or was ended from X: log in again |
| X error 326 | X locked the account for a check: unlock it at x.com |
| No notifications over the game after a restart | Open XPS5 once: it starts the background payload again |
| The installer's notification never comes | Make sure the ELF loader listens on port 9021 (after a restart, run your jailbreak again) |

---

## Building from source

For developers. You need:

- Windows with LLVM/clang in `C:\Program Files\LLVM` (`winget install LLVM.LLVM`), or any clang with `-Llvm <its bin folder>`;
- the prebuilt **ps5-payload-sdk** (`build.ps1 -Sdk <dir>`);
- WSL (Ubuntu 24.04) with clang 18 and **PS5_Vulkan** in `~/src/PS5_Vulkan`, and FFmpeg 8.1.3 built for the PS5 (see `title/build-title.sh`), its source tarball for the background service's AAC-only build (`tools/ffmpeg-payload.sh`, once, with the ps5-payload-sdk in `/opt/ps5-payload-sdk`), and jxrlib's source in `~/xps5-build/jxrlib-dl/jxrlib-master` (or `JXRLIB_SRC=<dir>`), which the title's build compiles once with `tools/jxrlib/build.sh`.

```powershell
.\build.ps1 -Wsl Ubuntu-24.04      # the title, the payloads, build\XPS5.elf and build\XPS5-uninstall.elf
.\tools\release.ps1                 # build\release\XPS5-<version>\ and its zip, with SHA256SUMS.txt
```

Without `-Wsl`, `build.ps1` reuses the title already in `build\title`.

**Publishing a release**: set the version in `src/update.h` (`XPS5_VERSION`), build, run `tools\release.ps1`, then publish a GitHub release tagged `v<version>` with `XPS5.elf` attached (and the rest of `build\release\XPS5-<version>\`). The workflow `.github/workflows/pages.yml` then puts that `XPS5.elf` and its `update.json` on GitHub Pages, where every XPS5 from 1.0.1 on finds it. It can also be run by hand from the Actions tab. Pages must be set to deploy from GitHub Actions (*Settings → Pages → Source*). A fork points `XPS5_UPDATE_URL` in `src/update.h` at its own Pages.

The login window is built separately in WSL by `tools/login-engine`: `build-engine.sh build` (WebKit), `build-login.sh` (the window), then `pack-engine.sh`, which `build.ps1 -Wsl` runs and which puts the packed engine in `build\engine`. Without it, the installer installs XPS5 without its login window.

Tests run on the PC, in WSL, with the address and undefined-behaviour sanitizers:

```sh
sh tests/host-test.sh           # JSON, X's answers, transaction ids, MP4, QR, trophies, settings, text, drawing…
sh tests/preview.sh <out dir>   # draws XPS5's screens into PNG files with made-up posts
```

---

## Project layout

```
XPS5/
├── .github/workflows/pages.yml  puts each release's installer and update.json on GitHub Pages
├── build.ps1                 builds everything (Windows + WSL)
├── send.ps1                  sends an ELF to the PS5's ELF loader
├── certs/                    Let's Encrypt root certificates bundled into the payloads
├── src/                      shared code and the payloads
│   ├── xapi.c  xtid.c  xsign.c   X's web requests and their transaction ids
│   ├── https.c tls.c json.c      HTTPS over libSceHttp, certificates, a JSON parser
│   ├── notify.c xnotify.c toast.c the background payload and PS5 notifications
│   ├── play.c  game.c  trophy.c  the game you play, the playing status, trophies
│   ├── mp4.c   jpeg.c  captures.c video files, JPEG, your PS5 captures
│   ├── install.c uninstall.c     the installer and the uninstaller
│   ├── dictated.c dictserve.c dictate.c the dictation payload and whisper.cpp's side of it
│   ├── xchat.c xchatopen.c xcrypt.c juicebox.c encrypted X Chat and its PIN
│   ├── radio.c hls.c spacecmd.c  Spaces, in the title and over your game
│   ├── voice.c clip.c ultrahdr.c voice notes, clips that fit, HDR screenshots
│   ├── seen.c sessions.c         friends as XPS5 saw them, your sessions of play
│   ├── phone.c phoneserve.c      writing on your phone
│   └── link.c linkserve.c qr.c   linking an account from a PC
├── title/                    the native title (TWPS00001)
│   ├── main.c                    threads, requests, the main loop
│   ├── ui.c                      every screen and its layout
│   ├── draw.c text.c images.c    the renderer
│   ├── video.c workers.c         video out, tiling, worker threads
│   ├── player.c sound.c          the video player and sound
│   ├── cards.c jxr.c             the cards, the PS5's JPEG XR screenshots
│   ├── mic.c space.c             the controller's microphone, Spaces
│   ├── lang.c lang_table.h       the 7 languages
│   ├── art/ fonts/               icon, backgrounds, Inter and DejaVu
│   └── build-title.sh            builds eboot.bin in WSL
├── tests/                    host tests and the screen previewer
├── third_party/stb/          stb_truetype, stb_image
├── third_party/libsodium/ bearssl/  the parts of them X Chat needs
├── third_party/whisper.cpp/  whisper.cpp 1.9.5 and its ggml (the parts for an x86 CPU), for dictation
├── models/whisper/           not in the repository: ggml-tiny.bin from https://huggingface.co/ggerganov/whisper.cpp, and the 8-bit copy tools/whisper-q8.sh makes of it for the installer
└── tools/
    ├── login-engine/             builds and packs the login window
    ├── xps5-login-extension/     the browser extension for linking from a PC
    ├── jxrlib/                   builds jxrlib for the title
    └── release.ps1               puts a release together
```

---

## Roadmap

- [ ] Test more firmware versions
- [ ] Invites to play: a DM that opens the game on your friend's PS5 (in testing)
- [ ] Send a capture straight into a DM (in testing)
- [x] Update check inside XPS5, from the GitHub releases (1.0.1)
- [x] Encrypted X Chat: read, reply, photos, videos, voice notes, with your X Chat PIN (1.1.0)
- [x] Offline dictation, Spaces over your game, do not disturb in competitive games (1.1.0)
- [x] Friends, trophies with Grok's tips, cards, looking for a group, voice notes, writing on your phone (1.1.0)
- [ ] Sign in with Google and Apple in the login window
- [ ] More PSN-style social features on top of X
- [ ] Your ideas: open an issue

**Next release: BrowserPS5 RN** 👀

---

## Licences

XPS5 is free software under the **GNU General Public License, version 3 or later** (`LICENSE`), since it includes code under that licence. It includes or links:

- video code from PS5_Vulkan (GPL-3.0-or-later);
- **FFmpeg 8.1.3** (libavcodec, libswresample and libavutil, decoders only), statically, under the GNU Lesser General Public License 2.1 or later: https://ffmpeg.org, source at https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz. `title/build-title.sh` shows how it is linked (its allocation, thread and time functions renamed to `title/ffmem.c`), so the title can be relinked with another build of it (`FFMPEG=<dir>`). The background service links its own build with the AAC decoder only (`tools/ffmpeg-payload.sh`, into `build\ffmpeg-payload`, which `build.ps1` links `notify.elf` against);
- **jxrlib** (Microsoft's JPEG XR library, for the PS5's HDR screenshots), statically, under the BSD 2-Clause licence (`tools/jxrlib/LICENSE-jxrlib.txt`): source at https://github.com/4creators/jxrlib, built by `tools/jxrlib/build.sh`;
- the Inter font (SIL Open Font License 1.1, `title/fonts/LICENSE-Inter.txt`), the DejaVu fonts (`title/fonts/LICENSE-DejaVu.txt`), and stb_truetype and stb_image (public domain);
- in the login window:
  - WebKit (GNU LGPL 2.1 or later, and BSD licences), with changes for the PS5, as a shared library (`libWebKitXps5.so.SOVERSION`) that can be replaced. Built into it, from the ps5-payload-sdk: libcurl (curl licence), OpenSSL 3.5 (Apache 2.0), ICU 74 (Unicode licence), cairo (LGPL 2.1 or MPL 1.1), FreeType (FreeType licence), Fontconfig (MIT), HarfBuzz (MIT), libpng (libpng licence), libjpeg (IJG licence), libwebp (BSD), libxml2 (MIT), libpsl (MIT), Brotli (MIT), zlib (zlib licence), libwpe (BSD) and SQLite (public domain);
  - Mesa's libEGL (MIT);
  - elfldr (GPL-3.0-or-later), changed in one line to give the processes it starts an environment;
  - Mozilla's list of root certificates (MPL 2.0), from Ubuntu's ca-certificates;
- in the installer: XZ Utils' liblzma 5.4 (public domain), from the ps5-payload-sdk;
- for X Chat, statically, the parts it needs of **libsodium** (ISC licence, `third_party/libsodium/LICENSE`), from https://github.com/jedisct1/libsodium, and of **BearSSL** (MIT, `third_party/bearssl/LICENSE.txt`), from https://bearssl.org, built by `tools/build-crypto.sh`;
- for dictation, in the dictation payload (`dictate.elf`, built by `tools/dictate-payload.sh` with the payload SDK's libc++): **whisper.cpp 1.9.5** and its ggml (MIT, `third_party/whisper.cpp/LICENSE`), from https://github.com/ggml-org/whisper.cpp, built by `tools/build-whisper.sh`, with its sample `jfk.wav` (a public-domain speech) for the tests; the installer carries the Whisper tiny model (OpenAI's weights, MIT) from `models/whisper/`, which comes from https://huggingface.co/ggerganov/whisper.cpp, with its big matrices in 8 bits (q8_0) by `tools/whisper-q8.sh` (`tools/whisper-quantize.c`, the rules of whisper.cpp's own `quantize`).

The source of the WebKit build used by the login window, with its changes for the PS5, is available on request: open an issue on this repository. The scripts that build and pack it are in `tools/login-engine`.

X is a trademark of X Corp. PlayStation and PS5 are trademarks of Sony Interactive Entertainment. XPS5 is not affiliated with either.
