# Usage details

Command line options, environment variables, window rules, sign-in internals and the files Quince keeps.

## Command line

```
--play <id or link>   play a YouTube or YouTube Music video id or link on start
--no-session          do not restore the last queue
--ao <driver>         mpv audio output (null for silence)
--app-id <id>         Wayland app id / window class (default quince)
```

Test options: `--quit-after <ms>`, `--screenshot <file>`, `--steps <file>` (replays clicks, typing and keys from a script, see `src/qml/TestDriver.h`).

## Environment variables

For diagnosis, `QUINCE_CLIENT=web` or `QUINCE_CLIENT=vision` forces one stream client. `QUINCE_MPV_LOG=info` (or `v`, `debug`) shows more of mpv's log.

`QUINCE_MECAB_DIC` can point to another compiled MeCab dictionary (see building.md).

## Global shortcuts

Media keys work while the window has focus, and on Linux system-wide through MPRIS.

Global shortcuts go through the desktop portal, with the ids play-pause, next, previous, volume-up, volume-down, like and show. On Hyprland they are targets of the global dispatcher, for example:

```
bind = SUPER, P, global, quince:play-pause
```

This needs quince.desktop installed, so the portal knows the app id "quince". On KDE and GNOME the desktop asks which keys to use.

## Mini player on Hyprland

Wayland lets no app keep its own window on top. The mini player's window title is always "Quince mini player" (it is not translated), so a window rule can float and pin it. For a Hyprland Lua config:

```
hl.window_rule({ name = "quince-mini", match = { title = "^(Quince mini player)$" }, float = true, pin = true })
```

## Signing in, technical details

"Sign in with Google" opens a small separate browser window (quince-login, QtWebEngine, profile only in memory) with Google's sign-in. When it reaches YouTube Music it hands the session cookies to Quince and closes. The player itself never loads a browser engine.

Quince reads a browser's cookie database itself, without yt-dlp. The Firefox family (Firefox, Zen, LibreWolf, Floorp, Waterfox) works on all systems. The Chromium family (Chrome, Chromium, Brave, Vivaldi, Edge) works on Linux and macOS. On Linux the key comes from the Secret Service or KWallet. On Windows, Chrome's app-bound encryption (used since version 127) prevents it. Only browsers found on the machine are offered, and only YouTube and Google cookies are kept.

A cookies.txt file (Netscape format) can be imported as well.

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

Downloads go into `~/Music/Quince/<artist>/<album>/`.
