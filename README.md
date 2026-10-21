# Quince

Quince is a native desktop player for YouTube Music, written in C++23 with Qt 6 (Qt Quick/QML). It replaces Pear Desktop (the Electron wrapper around music.youtube.com). It loads no web page: it talks to YouTube Music's internal API (InnerTube) directly and plays audio through libmpv. yt-dlp and deno are no longer used or needed. Stream URLs come from YouTube's player API directly, with two clients.

WEB_REMIX is YouTube Music's own client, used with the account. It gives loudness values, history tracking and, for Premium accounts, format 774 (256 kbit/s Opus). Its URLs need YouTube's signature and "n" puzzles solved. Quince runs yt-dlp's own solver script (yt-dlp/ejs, public domain) inside an embedded JavaScript engine (QuickJS-ng). A new YouTube player is solved once in the background (a few seconds) and cached.

The VISIONOS client gives format 251 (160 kbit/s Opus) without any JavaScript. It is used when signed out and as a fallback.

A track starts about 0.4 s after the click (with yt-dlp it took 2 to 5 s).

Unofficial, not affiliated with Google or YouTube.

On the author's machine it uses about 270 MiB of memory while playing, against about 800 MiB for Pear Desktop. Most of the difference is that there is no browser engine. The first frame shows after about 0.7 s, with the home page taken from a cache.

## Features

### Browsing

- Home, Explore, search with suggestions and filters, album, artist and playlist pages. Back and forward navigation.

### Playback and queue

- The queue comes from YouTube Music (album, playlist or radio, automix after an album). The next track is prepared while the current one plays. Track changes are gapless, or a crossfade of 1 to 12 s.
- Queue panel and right-click menus (play next, add to queue, radio, add to playlist, download). The last queue comes back paused on start.

### Account

- Sign in with Google: library, liked songs, history, likes and dislikes, add to playlist, new playlist. Plays count in the history. Premium accounts get 256 kbit/s Opus.
- Like or dislike a whole album or playlist from its page. Optional: skip disliked songs.

### Views

- Now Playing view (click the cover or title in the player bar, or Ctrl+P): large cover, with lyrics or the queue beside it.
- Video mode: music videos with their picture, and songs with their official music video when YouTube has one. Quality from 360p to 2160p, captions, and the video's light behind it. A visualizer (spectrum, bars, wave, stereo scope) shows when no video plays.
- Mini player: a small window with cover or video and the controls (button in the player bar, or Ctrl+Shift+M).
- Lyrics: time-synced from LRCLIB, then YouTube Music's own, then Genius. Click a line to jump there.

### Sound

- Equalizer (10 bands, presets), compressor, playback speed with the pitch kept, skip silences, loudness evened out the way YouTube does it, output device choice, and an exponential volume slider with an on-screen volume display.
- SponsorBlock: skips the parts of music videos without music (also sponsor spots, self promotion and subscribe reminders). The lookup sends only a 4 character hash prefix of the video id.

### Downloads

- Tracks, albums and playlists as Opus or M4A (copied as YouTube sends them) or MP3 (320 kbit/s), with title, artist, album, year, track number and cover, into `~/Music/Quince/<artist>/<album>/`. A Downloads page shows the progress.

### Look and integrations

- Dark, with soft light in the album's color, frosted glass on menus, dialogs and the Now Playing view, and animated page changes. Options for a compact sidebar, a clock, a see-through window (for the desktop's blur, after a restart) and starting paused.
- Discord Rich Presence (Pear Desktop's Discord application by default, your own id in Settings), tray icon, notifications.
- Linux: MPRIS and system-wide shortcuts through the desktop portal.
- German translation, chosen by the system language.

## Requirements

Package names for Arch / EndeavourOS.

Build: cmake, ninja, gcc or clang, qt6-base, qt6-declarative, qt6-svg, qt6-tools (translations), mpv (libmpv), ffmpeg (libavformat, libavcodec, libswresample; mpv needs it anyway), and on Linux openssl (for the Chromium cookie import). The first CMake run downloads QuickJS-ng v0.17.0 (pinned by hash), so it needs internet once.

Optional for building: qt6-webengine, which gives the sign-in window quince-login. Without it, sign in by importing cookies. ccache and mold are used when present.

Runtime: nothing beyond these libraries. There is no yt-dlp and no deno.

```
sudo pacman -S --needed cmake ninja qt6-base qt6-declarative qt6-svg qt6-tools qt6-webengine mpv ffmpeg
```

## Build and run

```
git clone https://github.com/Blackwolf1337/quince.git
cd quince
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/quince
```

Install (binaries, desktop file, icon), user-local or system-wide:

```
# for your user
cmake --install build --prefix ~/.local
# or for all users
sudo cmake --install build --prefix /usr
```

## Command line

```
--play <id or link>   play a YouTube or YouTube Music video id or link on start
--no-session          do not restore the last queue
--ao <driver>         mpv audio output (null for silence)
--app-id <id>         Wayland app id / window class (default quince)
```

Test options: `--quit-after <ms>`, `--screenshot <file>`, `--steps <file>` (replays clicks, typing and keys from a script, see `src/app/TestDriver.h`).

Diagnosis: the environment variable `QUINCE_CLIENT=web` or `QUINCE_CLIENT=vision` forces one stream client. `QUINCE_MPV_LOG=info` (or `v`, `debug`) shows more of mpv's log.

## Shortcuts

In the window:

| Key | Action |
|-----|--------|
| Space | play/pause |
| Ctrl+Left / Ctrl+Right | previous/next |
| Ctrl+Up / Ctrl+Down | volume |
| M | mute |
| Ctrl+F or / | search |
| Alt+Left or mouse back button | back |
| Alt+Right or mouse forward button | forward |
| Ctrl+L | lyrics |
| Ctrl+P | Now Playing view |
| Escape | closes the Now Playing view |
| Ctrl+Shift+M | mini player |

Media keys work while the window has focus, and system-wide through MPRIS.

Global shortcuts go through the portal, with the ids play-pause, next, previous, volume-up, volume-down, like and show. On Hyprland they are targets of the global dispatcher, for example:

```
bind = SUPER, P, global, quince:play-pause
```

This needs quince.desktop installed, so the portal knows the app id "quince". On KDE and GNOME the desktop asks which keys to use.

## Mini player on Hyprland

Wayland lets no app keep its own window on top. The mini player's window title is always "Quince mini player" (it is not translated), so a window rule can float and pin it. For a Hyprland Lua config:

```
hl.window_rule({ name = "quince-mini", match = { title = "^(Quince mini player)$" }, float = true, pin = true })
```

## Signing in

Use "Sign in" in the sidebar. There are three ways.

1. "Sign in with Google": a small separate browser window (quince-login, QtWebEngine, profile only in memory) opens Google's sign-in. When it reaches YouTube Music it hands the session cookies to Quince and closes. The player itself never loads a browser engine.
2. Take the session of a browser where you are already signed in. Quince reads the browser's cookie database itself, without yt-dlp. The Firefox family (Firefox, Zen, LibreWolf, Floorp, Waterfox) works on all systems. The Chromium family (Chrome, Chromium, Brave, Vivaldi, Edge) works on Linux and macOS; on Linux the key comes from the Secret Service or KWallet. On Windows, Chrome's app-bound encryption prevents it. Only browsers found on the machine are offered, and only YouTube and Google cookies are kept.
3. Import a cookies.txt file (Netscape format).

Only the session cookies are stored, in `~/.config/quince/cookies.txt`, readable by the user only (mode 0600). Sign out deletes the file.

Signed out, YouTube sometimes answers a request from an IP with "Sign in to confirm you're not a bot", especially after many requests. Then some tracks do not play until you sign in or some time passes. Signed in this is rare.

## Files

```
~/.config/quince/quince.conf          settings
~/.config/quince/cookies.txt          session cookies (only when signed in)
~/.local/share/quince/session.json    last queue and position
~/.cache/quince/                      cached home page and cover art
~/.cache/quince/player/               solved YouTube players
~/Music/Quince/                       downloads (folder can be changed in Settings)
```

## Platforms

Developed and tested on Linux (Wayland, Hyprland). The code is meant for Windows and macOS too, but those builds have not been made yet, and the macOS Chromium cookie import is written but has never been run. The window uses OpenGL on every system (mpv draws video through it). Windows media controls and the macOS Now Playing panel are not written yet. MPRIS and portal shortcuts are Linux only.

## When YouTube changes things

Streams depend on YouTube's player puzzles. The solver is the same code yt-dlp ships (yt-dlp-ejs). To update it, copy `lib.min.js` and `core.min.js` of a newer yt-dlp-ejs release into `third_party/ejs/` and rebuild. Without a rebuild, copy them into `~/.local/share/quince/ejs/`, where they take precedence.

Page layout changes are handled in `src/ytm/Parser.cpp`.

There is a development tool, `build/quince-parsedump`, that prints what the parser makes of a saved InnerTube answer:

```
quince-parsedump browse|search|continuation|suggest|next|nextmore <answer.json> [browseId]
```

## Layout

```
src/ytm            InnerTube client, parser, pages, account, cookies, browser import
src/player         libmpv player (two instances for crossfades), video item, stream resolver, JS solver, effects, queue, playback
src/download       downloader and audio file writer
src/lyrics         lyrics
src/integrations   Discord, tray and notifications, MPRIS, global shortcuts, SponsorBlock
src/app            controller, settings, colors
qml                the interface
login              quince-login
third_party/ejs    yt-dlp's challenge solver
i18n               translations
packaging          desktop file
```

## Credits

The API paths and parsing follow what ytmusicapi (MIT) found out. Stream client details and the challenge solver come from yt-dlp (solver: yt-dlp/ejs, Unlicense, bundling meriyah ISC and astring MIT). JavaScript engine: QuickJS-ng (MIT). Font: Inter (SIL Open Font License). Lyrics: LRCLIB and Genius. Skip segments: SponsorBlock.
