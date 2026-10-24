#!/bin/bash
# Builds Quince's own FFmpeg, libplacebo and mpv for Windows (MinGW, inside the
# cross-build image; what goes in: packaging/media.sh) in place of MSYS2's,
# which bring every codec there is (x265, aom, SvtAv1, rav1e, vpx, librsvg,
# shaderc, Python: about 180 MB of DLLs). TLS through Windows (schannel),
# hardware decoding through D3D11VA and DXVA2, sound through WASAPI. Builds
# with prefix /ucrt64 into <cache>/media-root once per version, then copies
# that over the MSYS2 packages in the sysroot.
# usage: build-media.sh <cache dir> <sysroot>
set -euo pipefail
cache=$1
sysroot=$2
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=packaging/media.sh
. "$here/../media.sh"

src=$cache/media-src
stage=$cache/media-root
want="ffmpeg-$FFMPEG libplacebo-$PLACEBO mpv-$MPV $(cat "$0" "$here/../media.sh" | sha256sum | cut -c1-16)"
host=x86_64-w64-mingw32

export PKG_CONFIG_LIBDIR=$sysroot/ucrt64/lib/pkgconfig:$sysroot/ucrt64/share/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=$sysroot

# what is built so far, over the MSYS2 packages
install_stage() {
    cp -a "$stage/ucrt64/." "$sysroot/ucrt64/"
}

if [ "$(cat "$stage/.built" 2>/dev/null)" = "$want" ]; then
    install_stage
    exit 0
fi
rm -rf "$stage" "$src"
mkdir -p "$src" "$stage"
media_fetch "$src"

cat > "$src/cross.ini" <<INI
[binaries]
c = '$host-gcc'
cpp = '$host-g++'
ar = '$host-ar'
strip = '$host-strip'
windres = '$host-windres'
dlltool = '$host-dlltool'
pkg-config = 'pkg-config'

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
INI

cd "$src/ffmpeg-$FFMPEG"
./configure --prefix=/ucrt64 --target-os=mingw32 --arch=x86_64 --cross-prefix=$host- \
    --pkg-config=pkg-config --extra-cflags="-I$sysroot/ucrt64/include" --extra-ldflags="-L$sysroot/ucrt64/lib" \
    "${FFMPEG_OPTIONS[@]}" --enable-w32threads --enable-schannel --enable-d3d11va --enable-dxva2 \
    --enable-hwaccel="$(media_hwaccels d3d11va d3d11va2 dxva2)"
make -j"$(nproc)"
make install DESTDIR="$stage"
install_stage

cd "$src/libplacebo-$PLACEBO"
meson setup build --cross-file "$src/cross.ini" --prefix=/ucrt64 --buildtype=release "${PLACEBO_OPTIONS[@]}"
meson compile -C build
DESTDIR="$stage" meson install -C build
install_stage

cd "$src/mpv-$MPV"
meson setup build --cross-file "$src/cross.ini" --prefix=/ucrt64 --buildtype=release "${MPV_OPTIONS[@]}" \
    -Dgl-win32=enabled -Dd3d-hwaccel=enabled -Dgl-dxinterop=enabled -Dwin32-threads=enabled -Dwasapi=enabled
meson compile -C build
DESTDIR="$stage" meson install -C build
install_stage

echo "$want" > "$stage/.built"
