# Changelog

## Unreleased

### Selecting tracks

- Pages with tracks (playlists, albums, search results and others) support multi-select. Ctrl+click selects or unselects a track, Shift+click selects the tracks from the last Ctrl+clicked one to this one, Ctrl+A selects every track of the page and Esc clears the selection. A plain click plays as before and clears it. On macOS, Ctrl here is the Command key.
- The right-click menu of a selected track acts on all selected tracks (its first line shows how many): play next, add to queue, add to playlist, download and remove from playlist. Start radio, copy link, go to album and go to artist are for one track only. Tracks added to an empty queue become the queue.

### Playlists

- The right-click menu of a track on the page of one of your own playlists has "Remove from playlist".
- Right-clicking one of your own playlists in the sidebar gives "Rename" and "Delete". Delete asks first.

### Fixes

- In rows that mix albums or playlists with videos (for example "Listen again" when signed in), the square covers were cut off and the titles under them were missing.

## 0.2.0

The interface was rebuilt with Slint, downloads are much smaller, and media controls and sign-in now work on Windows and macOS too.

### Interface

- The interface uses Slint 1.18 instead of Qt Quick. Qt 6 still runs the core (network, database, player logic).
- It looks the same as before. The frosted glass is now a pre-blurred picture of the cover with a tint, because Slint has no live blur.
- New page transitions, and scroll bars that hide when not used.

### Downloads

- The Windows zip shrinks from 132 MB to 53 MB, the macOS zip from 82 MB to 45 MB and the Arch tarball from 38 MB to 28 MB.
- The reasons are Slint instead of Qt Quick, small own builds of mpv and FFmpeg with only the codecs Quince uses, trimmed ICU data and stripped binaries.

### Media controls

- Windows: the media overlay with title, artist, album and cover, the media keys, the lock screen, and the timeline with seeking (SystemMediaTransportControls).
- macOS: Now Playing in Control Center and the menu bar, and the media keys through the remote commands.
- Linux had MPRIS already.

### Sign in

- "Sign in with Google" now works on Linux, Windows and macOS. The sign-in window (quince-login) uses the web view of the system: WebKitGTK 6 on Linux, WKWebView on macOS and Microsoft Edge WebView2 on Windows. It replaces QtWebEngine. In 0.1.1 it was not available in the Windows build.
- The browser profile of the window lives in memory only. The password never reaches Quince.

### Command line

- `--help` and `--version` work without opening a window.
- `--app-id` sets the Wayland app id or the X11 class.

### Building

- Building needs a Rust toolchain 1.92 or newer, because Slint is built with cargo.
- Qt Quick, QML, Qt Widgets and QtWebEngine are no longer needed.
- On Linux, install webkitgtk-6.0 instead of qt6-webengine and qt6-declarative.

### Testing

Everything was tested on Linux, including a real Google sign-in. No real Windows PC has run the 0.2.0 build yet (version 0.1 was tested on Windows 11 in a virtual machine). On GitHub's Windows runner, the zip starts and plays, Windows lists the media session with the right title, artist and "Playing" status, and Google's sign-in page lets WebView2 past the email step. The build was also tested under Wine. On GitHub's Mac runner, the packed app starts and plays a track, and Google's sign-in page lets WKWebView past the email step. Now Playing has not been seen on a real Mac.
