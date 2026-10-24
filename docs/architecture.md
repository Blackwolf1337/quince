# Architecture

How Quince gets its data and plays audio, and where the code lives.

## Overview

Quince is written in C++23 with Qt 6 (Qt Quick/QML). It loads no web page. It talks to YouTube Music's internal API (InnerTube) directly and plays audio through libmpv. yt-dlp and deno are no longer used or needed. Stream URLs come from YouTube's player API directly, with two clients.

WEB_REMIX is YouTube Music's own client, used with the account. It gives loudness values, history tracking and, for Premium accounts, format 774 (256 kbit/s Opus). Its URLs need YouTube's signature and "n" puzzles solved. Quince runs yt-dlp's own solver script (yt-dlp/ejs, public domain) inside an embedded JavaScript engine (QuickJS-ng). A new YouTube player is solved once in the background (a few seconds) and cached.

The VISIONOS client gives format 251 (160 kbit/s Opus) without any JavaScript. It is used when signed out and as a fallback.

A track starts about 0.4 s after the click (with yt-dlp it took 2 to 5 s).

On the author's machine Quince uses about 270 MiB of memory while playing, against about 800 MiB for Pear Desktop. Most of the difference is that there is no browser engine. The first frame shows after about 0.7 s, with the home page taken from a cache.

## Other implementation notes

The queue comes from YouTube Music. The next track is prepared while the current one plays, using two mpv instances so crossfades work.

Lyrics come from LRCLIB, then Megalobiz (its free public API, api.megalobiz.com), then YouTube Music's own, then Genius. Musixmatch is not supported, because it gives anonymous apps decoy lyrics.

Romanization: Korean uses Revised Romanization with its sound changes. Chinese uses pinyin with tone marks from ICU. Japanese uses MeCab (see building.md).

SponsorBlock lookups send only a 4 character hash prefix of the video id.

## Source layout

```
src/ytm            InnerTube client, parser, pages, account, cookies, browser import
src/player         libmpv player (two instances for crossfades), stream resolver, JS solver, effects, queue, playback
src/download       downloader and audio file writer
src/lyrics         lyrics, romanization
src/integrations   Discord, notifications, MPRIS, global shortcuts, SponsorBlock
src/app            controller, settings, language, colors, UiHost (what the core needs from a frontend)
src/qml            the Qt Quick frontend: main, QML type registration, video item, tray, test driver
qml                the interface
src/slint          the Slint frontend (QUINCE_SLINT, work in progress): threads, bindings, images, video
ui                 its interface
cmake              Mecab.cmake (builds MeCab and the dictionary)
login              quince-login
tools              quince-parsedump, quince-romanize
third_party/ejs    yt-dlp's challenge solver
i18n               translations (the .ts files; i18n/slint made from them for the Slint frontend)
packaging          desktop file
```

Everything under src except src/qml builds into the static library quince_core, which links no QtQml or QtQuick. The frontend registers the core's types for QML in src/qml/Registrations.h.

The Slint frontend runs Slint on the main thread and the core (QCoreApplication, AppController) on a second one. src/slint/Bridge.h has the rules for crossing: values cross as plain copies; Slint objects stay on the Slint thread, Qt objects on the core thread. mpv draws the video under the Slint items in an EGL context of its own, because FemtoVG's GL state breaks mpv's rendering of hardware decoded frames.
