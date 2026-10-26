# Credits

The projects and services Quince builds on.

The interface is built with Slint (used under GPL-3.0), which is built with cargo through Corrosion (MIT). Qt 6 runs the core: network, database and player logic. Playback uses libmpv. File pickers: nativefiledialog-extended (zlib license). The Windows sign-in window uses the Microsoft WebView2 SDK (BSD-3-Clause). On Linux and macOS it uses the system's web view.

The API paths and parsing follow what ytmusicapi (MIT) found out. Stream client details and the challenge solver come from yt-dlp (solver: yt-dlp/ejs, Unlicense, bundling meriyah ISC and astring MIT). JavaScript engine: QuickJS-ng (MIT). Font: Inter (SIL Open Font License). Japanese romanization: MeCab (Taku Kudo, used under its BSD license option of GPL/LGPL/BSD) with IPADIC (its own permissive license, installed as `share/licenses/quince/IPADIC-COPYING`). Chinese romanization and character conversion: ICU (Unicode license). Lyrics: LRCLIB, Megalobiz and Genius. Skip segments: SponsorBlock.

Quince is licensed under GPL-3.0-or-later (see LICENSE). The bundled parts keep their own licenses, as listed above. The Windows and macOS downloads contain GPL builds of mpv and FFmpeg, together with libplacebo and dav1d.
