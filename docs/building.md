# Building Quince

How to build, install and package Quince from source.

## Requirements

Package names for Arch / EndeavourOS.

Build: cmake, ninja, gcc or clang, qt6-base, qt6-declarative, qt6-svg, qt6-tools (translations), icu (icu-uc and icu-i18n through pkg-config; Qt already depends on it), mpv (libmpv), ffmpeg (libavformat, libavcodec, libswresample; mpv needs it anyway), and on Linux openssl (for the Chromium cookie import).

Optional for building: qt6-webengine, which gives the sign-in window quince-login. Without it, sign in by importing cookies. ccache and mold are used when present.

Runtime: nothing beyond these libraries. There is no yt-dlp and no deno.

```
sudo pacman -S --needed cmake ninja qt6-base qt6-declarative qt6-svg qt6-tools qt6-webengine icu mpv ffmpeg
```

## Downloads during the build

The first CMake run downloads QuickJS-ng v0.17.0 (pinned by hash), so it needs internet once. The build also downloads MeCab 0.996.13 and mecab-ipadic-2.7.0-20070801 from the shogo82148/mecab GitHub releases (pinned by SHA256, like QuickJS-ng) and compiles the dictionary at build time.

The dictionary is installed to `share/quince/mecab/ipadic`. In the build tree it sits next to the binary at `build/mecab/ipadic`. The environment variable `QUINCE_MECAB_DIC` can point to another compiled dictionary.

The dictionary (about 50 MB) is loaded on the first Japanese song and unloaded after a minute without one. Japanese lyrics are romanized with MeCab and the IPADIC dictionary (readings of words, written in Hepburn, particles as spoken: wa, e, o). The build is done by `cmake/Mecab.cmake`.

## Build and run

```
git clone https://github.com/Blackwolf1337/quince.git
cd quince
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/quince
```

## Slint frontend (work in progress)

`-DQUINCE_SLINT=ON` also builds `quince-slint`, a frontend made with Slint instead of Qt Quick, on the same core. It needs a Rust toolchain (1.92 or newer); CMake downloads Slint 1.18.1 and builds it with cargo, which also downloads its crates. So far it has the player bar, the queue, video and the tray.

## Install

Install (binaries, desktop file, icon), user-local or system-wide:

```
# for your user
cmake --install build --prefix ~/.local
# or for all users
sudo cmake --install build --prefix /usr
```

## Windows build

The Windows build can be made on Linux with Docker: `packaging/windows/cross-build.sh`. It uses MSYS2's Windows libraries, keeps a cache of about 3 GB in `~/.cache/quince-windows` and puts the zip in the repo folder.

The Windows build has no sign-in window, because quince-login needs QtWebEngine, which it lacks. The Windows and macOS downloads contain GPL builds of mpv and FFmpeg.

## macOS build

macOS can only be built on a Mac, or by the GitHub workflow in `.github/workflows/build.yml`. The release is for Apple Silicon only. The app is not notarized.

Homebrew builds from source with Homebrew's Qt and mpv (see the Install section of the README for the commands).

## Arch package

The tarball for Arch (`quince-<version>-x86_64.tar.zst`, files under usr/) on the releases page is built against Arch's libraries of the release day. There is no AUR package yet.
