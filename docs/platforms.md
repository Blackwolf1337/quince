# Platforms

What is tested on which system, and what each system lacks.

## Linux

Quince is developed on Linux (Wayland, Hyprland). Media controls use MPRIS. MPRIS and portal shortcuts are Linux only. The sign-in window needs WebKitGTK 6 at build time.

## Windows

Windows builds exist and are made by the GitHub workflow. They were tested under Wine and on Windows 11 in a virtual machine (start, home page, playback, lyrics, settings). The very first start on Windows can take a while; later starts take about a second.

Windows media controls use SystemMediaTransportControls: the media overlay with title, artist, album and cover, the media keys, the lock screen, and the timeline with seeking in the flyout of Windows 10 and 11. They were tested only under Wine, where the calls go through. They have not been seen on a real Windows PC yet.

The sign-in window works on Windows and needs Microsoft Edge WebView2. When it is missing, sign in by importing cookies. Chrome and Edge cookies cannot be read on Windows.

## macOS

macOS builds exist (Apple Silicon), but the developer has never run them. Now Playing (Control Center and the menu bar: title, artist, album, cover, progress, seeking) and the media keys through the remote commands are written. They were tested only in GitHub's CI, where the app starts and plays with them. They have not been seen on a real Mac yet. The sign-in window uses WKWebView.

## All systems

The window uses OpenGL on every system (mpv draws video through it).
