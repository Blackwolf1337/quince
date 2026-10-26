# Features

The full list of what Quince can do, with all options.

## Browsing

- Home, Explore, search with suggestions and filters, album, artist and playlist pages. Back and forward navigation.

## Playback and queue

- The queue comes from YouTube Music (album, playlist or radio, automix after an album). The next track is prepared while the current one plays. Track changes are gapless, or a crossfade of 1 to 12 s.
- Queue panel and right-click menus (play next, add to queue, radio, add to playlist, download, copy link). The last queue comes back paused on start (can be switched off). Copy link puts the music.youtube.com link of a track, album or playlist on the clipboard.
- Multi-select on pages (playlists, albums, search results and others): Ctrl+click selects or unselects a track, Shift+click selects the tracks from the last Ctrl+clicked one to this one, Ctrl+A selects every track of the open page, and Esc clears the selection. A plain click plays as before and clears it. The right-click menu of a selected track then acts on all selected tracks (its first line shows how many): play next, add to queue, add to playlist, download and remove from playlist. Start radio, copy link, go to album and go to artist are for one track only. Tracks added to an empty queue become the queue. On macOS, Ctrl here is the Command key.

## Account

- Sign in with Google: library, liked songs, history, likes and dislikes, add to playlist, new playlist. Plays count in the history. Premium accounts get 256 kbit/s Opus.
- On the page of one of your own playlists, the right-click menu of a track has "Remove from playlist". Right-clicking one of your own playlists in the sidebar gives "Rename" and "Delete" (delete asks first).
- Like or dislike a whole album or playlist from its page. Optional: skip disliked songs.

## Views

- Now Playing view (click the cover or title in the player bar, or Ctrl+P): large cover, with lyrics or the queue beside it.
- Video mode: music videos with their picture, and songs with their official music video when YouTube has one. Quality from 360p to 2160p, captions, and the video's light behind it. A visualizer (spectrum, bars, wave, stereo scope) shows when no video plays.
- Mini player: a small window with cover or video and the controls (button in the player bar, or Ctrl+Shift+M).
- Lyrics: time-synced from LRCLIB, then Megalobiz (its free public API, api.megalobiz.com), then YouTube Music's own, then Genius. Each source can be switched off, and one can be asked first ("Ask first" setting). "Show lyrics even if the match is inexact" allows a looser search. The source name under the lyrics links to the page at the source. Click a line to jump there. Musixmatch is not supported, because it gives anonymous apps decoy lyrics.
- Lyrics look: Pear's four line effects for the sung line. Fancy (large, with a glow that fades and a small wobble), Scale (the line grows), Offset (moves to the right) and Focus (only it is bright). Time codes next to the lines are optional. Empty lines of synced lyrics show a mark (the eighth note character by default, can be changed).
- Romanization: Japanese, Korean and Chinese lyrics get a line in Latin letters under each line. Japanese uses MeCab with the IPADIC dictionary (readings of words, written in Hepburn, particles as spoken: wa, e, o). Korean uses Revised Romanization with its sound changes. Chinese uses pinyin with tone marks from ICU. Chinese lyrics can also be shown in Traditional or Simplified characters. The dictionary (about 50 MB) is loaded on the first Japanese song and unloaded after a minute without one.

## Sound

- Equalizer (10 bands, presets), compressor, playback speed with the pitch kept, skip silences (or only the silence at the start of a track), loudness evened out the way YouTube does it, output device choice, and an exponential volume slider with an on-screen volume display. The crossfade has a separate fade-in length (shorter than the crossfade lets the next track come up sooner).
- SponsorBlock: skips the parts of music videos without music (also sponsor spots, self promotion, subscribe reminders, intros and outros). The categories can be chosen; intros and outros are off by default. The lookup sends only a 4 character hash prefix of the video id.

## Downloads

- Tracks, albums and playlists as Opus or M4A (copied as YouTube sends them) or MP3 (128, 192, 256 or 320 kbit/s), with title, artist, album, year, track number and cover, into `~/Music/Quince/<artist>/<album>/`. Tracks that are already in the folder are skipped (on by default), and a playlist can be limited to at most N tracks (0 for all). A Downloads page shows the progress.

## Look and integrations

- Dark, with soft light in the album's color, frosted glass on menus, dialogs and the Now Playing view, and animated page changes. Options for a compact sidebar, a clock (with seconds, 24 or 12 hours, or as the system), a see-through window (for the desktop's blur, after a restart, with the opacity of the background) and starting paused. The strength of the album color glow can be set, and whether the progress bar takes the album color. The ambient light of videos has a strength and a reach. The window size and the mini player's width are remembered. The start page can be Home, Explore, Library or Liked songs (shown at start and after signing in or out).
- Discord Rich Presence (Pear Desktop's Discord application by default, your own id in Settings). The member list can show the app name, the artist or the title. The time left can be hidden, the "Listen on YouTube Music" button can be switched off, and the status is cleared after a pause of N minutes (default 10, 0 never).
- Tray icon (a click can play or pause instead of showing the window) and notifications (also when playback goes on after a pause; urgency low, normal or critical on Linux).
- Linux: MPRIS and system-wide shortcuts through the desktop portal.
- Language: Settings has a choice of as the system, English or Deutsch. It applies at once, also to YouTube's own texts on pages loaded after the change.
