# Platforms

What is tested on which system, and what each system lacks.

## Linux

Quince is developed on Linux (Wayland, Hyprland). Media controls use MPRIS. MPRIS and portal shortcuts are Linux only. The sign-in window uses WebKitGTK 6 and needs it at build time. Everything was tested by the developer on Linux, including a real Google sign-in.

## Windows

Windows builds exist and are made by the GitHub workflow. A real Windows PC has not run the 0.2.0 build yet. Version 0.1 was tested on Windows 11 in a virtual machine. The very first start on Windows can take a while; later starts take about a second.

GitHub's CI runs the Windows zip on GitHub's Windows runner. It starts and plays. Windows lists Quince's media session with the right title, artist and "Playing" status. The Slint build was also tested under Wine.

Windows media controls use SystemMediaTransportControls: the media overlay with title, artist, album and cover, the media keys, the lock screen, and the timeline with seeking. Apart from the session listing above, they have not been seen on a real Windows PC.

The sign-in window works on Windows and needs Microsoft Edge WebView2. In CI, Google's sign-in page lets WebView2 past the email step (tested with an address that does not exist). A full sign-in has not been tried on Windows. When WebView2 is missing, sign in by importing cookies. Chrome and Edge cookies cannot be read on Windows.

## macOS

macOS builds exist (Apple Silicon), but the developer has never run them on a real Mac. On GitHub's Mac runner, CI starts the packed app and plays a track.

Now Playing (Control Center and the menu bar: title, artist, album, cover, progress, seeking) and the media keys through the remote commands are written. Now Playing has not been seen on a real Mac yet.

The sign-in window uses WKWebView. In CI, Google's sign-in page lets WKWebView past the email step. A full sign-in has not been tried on macOS.

## All systems

The window uses OpenGL on every system (mpv draws video through it).
