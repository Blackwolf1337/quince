# Platforms

What is tested on which system, and what each system lacks.

## Linux

Quince is developed on Linux (Wayland, Hyprland). MPRIS and portal shortcuts are Linux only.

## Windows

Windows builds exist and are made by the GitHub workflow. They were tested under Wine and on Windows 11 in a virtual machine (start, home page, playback, lyrics, settings). The very first start on Windows can take a while; later starts take about a second.

Windows media controls (taskbar, SMTC) are not written yet. The Windows build has no sign-in window, so signing in works by cookie import from Firefox. Chrome and Edge cookies cannot be read on Windows.

## macOS

macOS builds exist (Apple Silicon), but the developer has never run them. The macOS Now Playing panel is not written yet.

## All systems

The window uses OpenGL on every system (mpv draws video through it).
