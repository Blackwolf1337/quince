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
- Queue panel and right-click menus (play next, add to queue, radio, add to playlist, download, copy link). The last queue comes back paused on start (can be switched off). Copy link puts the music.youtube.com link of a track, album or playlist on the clipboard.

### Account

- Sign in with Google: library, liked songs, history, likes and dislikes, add to playlist, new playlist. Plays count in the history. Premium accounts get 256 kbit/s Opus.
- Like or dislike a whole album or playlist from its page. Optional: skip disliked songs.

### Views

- Now Playing view (click the cover or title in the player bar, or Ctrl+P): large cover, with lyrics or the queue beside it.
- Video mode: music videos with their picture, and songs with their official music video when YouTube has one. Quality from 360p to 2160p, captions, and the video's light behind it. A visualizer (spectrum, bars, wave, stereo scope) shows when no video plays.
- Mini player: a small window with cover or video and the controls (button in the player bar, or Ctrl+Shift+M).
- Lyrics: time-synced from LRCLIB, then Megalobiz (its free public API, api.megalobiz.com), then YouTube Music's own, then Genius. Each source can be switched off, and one can be asked first ("Ask first" setting). "Show lyrics even if the match is inexact" allows a looser search. The source name under the lyrics links to the page at the source. Click a line to jump there. Musixmatch is not supported, because it gives anonymous apps decoy lyrics.
- Lyrics look: Pear's four line effects for the sung line. Fancy (large, with a glow that fades and a small wobble), Scale (the line grows), Offset (moves to the right) and Focus (only it is bright). Time codes next to the lines are optional. Empty lines of synced lyrics show a mark (the eighth note character by default, can be changed).
- Romanization: Japanese, Korean and Chinese lyrics get a line in Latin letters under each line. Japanese uses MeCab with the IPADIC dictionary (readings of words, written in Hepburn, particles as spoken: wa, e, o). Korean uses Revised Romanization with its sound changes. Chinese uses pinyin with tone marks from ICU. Chinese lyrics can also be shown in Traditional or Simplified characters. The dictionary (about 50 MB) is loaded on the first Japanese song and unloaded after a minute without one.

### Sound

- Equalizer (10 bands, presets), compressor, playback speed with the pitch kept, skip silences (or only the silence at the start of a track), loudness evened out the way YouTube does it, output device choice, and an exponential volume slider with an on-screen volume display. The crossfade has a separate fade-in length (shorter than the crossfade lets the next track come up sooner).
- SponsorBlock: skips the parts of music videos without music (also sponsor spots, self promotion, subscribe reminders, intros and outros). The categories can be chosen; intros and outros are off by default. The lookup sends only a 4 character hash prefix of the video id.

### Downloads

- Tracks, albums and playlists as Opus or M4A (copied as YouTube sends them) or MP3 (128, 192, 256 or 320 kbit/s), with title, artist, album, year, track number and cover, into `~/Music/Quince/<artist>/<album>/`. Tracks that are already in the folder are skipped (on by default), and a playlist can be limited to at most N tracks (0 for all). A Downloads page shows the progress.

### Look and integrations

- Dark, with soft light in the album's color, frosted glass on menus, dialogs and the Now Playing view, and animated page changes. Options for a compact sidebar, a clock (with seconds, 24 or 12 hours, or as the system), a see-through window (for the desktop's blur, after a restart, with the opacity of the background) and starting paused. The strength of the album color glow can be set, and whether the progress bar takes the album color. The ambient light of videos has a strength and a reach. The window size and the mini player's width are remembered. The start page can be Home, Explore, Library or Liked songs (shown at start and after signing in or out).
- Discord Rich Presence (Pear Desktop's Discord application by default, your own id in Settings). The member list can show the app name, the artist or the title. The time left can be hidden, the "Listen on YouTube Music" button can be switched off, and the status is cleared after a pause of N minutes (default 10, 0 never).
- Tray icon (a click can play or pause instead of showing the window) and notifications (also when playback goes on after a pause; urgency low, normal or critical on Linux).
- Linux: MPRIS and system-wide shortcuts through the desktop portal.
- Language: Settings has a choice of as the system, English or Deutsch. It applies at once, also to YouTube's own texts on pages loaded after the change.

## Requirements

Package names for Arch / EndeavourOS.

Build: cmake, ninja, gcc or clang, qt6-base, qt6-declarative, qt6-svg, qt6-tools (translations), icu (icu-uc and icu-i18n through pkg-config; Qt already depends on it), mpv (libmpv), ffmpeg (libavformat, libavcodec, libswresample; mpv needs it anyway), and on Linux openssl (for the Chromium cookie import). The first CMake run downloads QuickJS-ng v0.17.0 (pinned by hash), so it needs internet once. The build also downloads MeCab 0.996.13 and mecab-ipadic-2.7.0-20070801 from the shogo82148/mecab GitHub releases (pinned by SHA256, like QuickJS-ng) and compiles the dictionary at build time. The dictionary is installed to `share/quince/mecab/ipadic`; in the build tree it sits next to the binary at `build/mecab/ipadic`. The environment variable `QUINCE_MECAB_DIC` can point to another compiled dictionary.

Optional for building: qt6-webengine, which gives the sign-in window quince-login. Without it, sign in by importing cookies. ccache and mold are used when present.

Runtime: nothing beyond these libraries. There is no yt-dlp and no deno.

```
sudo pacman -S --needed cmake ninja qt6-base qt6-declarative qt6-svg qt6-tools qt6-webengine icu mpv ffmpeg
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

There are two development tools. `build/quince-parsedump` prints what the parser makes of a saved InnerTube answer:

```
quince-parsedump browse|search|continuation|suggest|next|nextmore <answer.json> [browseId]
```

`build/quince-romanize` prints the romanization of the lines given as arguments:

```
$ build/quince-romanize "夜に駆ける"
language: ja
夜に駆ける
  yoru ni kakeru
```

## Layout

```
src/ytm            InnerTube client, parser, pages, account, cookies, browser import
src/player         libmpv player (two instances for crossfades), video item, stream resolver, JS solver, effects, queue, playback
src/download       downloader and audio file writer
src/lyrics         lyrics, romanization
src/integrations   Discord, tray and notifications, MPRIS, global shortcuts, SponsorBlock
src/app            controller, settings, language, colors
qml                the interface
cmake              Mecab.cmake (builds MeCab and the dictionary)
login              quince-login
tools              quince-parsedump, quince-romanize
third_party/ejs    yt-dlp's challenge solver
i18n               translations
packaging          desktop file
```

## Credits

The API paths and parsing follow what ytmusicapi (MIT) found out. Stream client details and the challenge solver come from yt-dlp (solver: yt-dlp/ejs, Unlicense, bundling meriyah ISC and astring MIT). JavaScript engine: QuickJS-ng (MIT). Font: Inter (SIL Open Font License). Japanese romanization: MeCab (Taku Kudo, used under its BSD license option of GPL/LGPL/BSD) with IPADIC (its own permissive license, installed as `share/licenses/quince/IPADIC-COPYING`). Chinese romanization and character conversion: ICU (Unicode license). Lyrics: LRCLIB, Megalobiz and Genius. Skip segments: SponsorBlock.
