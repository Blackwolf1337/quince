#!/bin/bash
# Builds Quince's own FFmpeg, libplacebo and mpv on macOS (what goes in:
# packaging/media.sh) in place of Homebrew's, which bring every codec there
# is. TLS through Secure Transport, hardware decoding through VideoToolbox,
# sound through Core Audio. Takes dav1d, LAME and libass from Homebrew.
# usage: build-media.sh <work dir> <prefix>
set -euo pipefail
work=$1
prefix=$2
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=packaging/media.sh
. "$here/../media.sh"
brew=$(brew --prefix)

mkdir -p "$work" "$prefix"
media_fetch "$work"
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

cd "$work/ffmpeg-$FFMPEG"
./configure --prefix="$prefix" --extra-cflags="-I$brew/include" --extra-ldflags="-L$brew/lib" \
    "${FFMPEG_OPTIONS[@]}" --enable-pthreads --enable-securetransport --enable-videotoolbox \
    --enable-hwaccel="$(media_hwaccels videotoolbox)"
make -j"$(sysctl -n hw.ncpu)"
make install

cd "$work/libplacebo-$PLACEBO"
meson setup build --prefix="$prefix" --buildtype=release "${PLACEBO_OPTIONS[@]}"
meson install -C build

cd "$work/mpv-$MPV"
meson setup build --prefix="$prefix" --buildtype=release "${MPV_OPTIONS[@]}" -Dcoreaudio=enabled
meson install -C build
