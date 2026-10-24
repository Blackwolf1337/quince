#!/bin/bash
# Builds a small FFmpeg, libplacebo and mpv for Windows (MinGW, inside the
# cross-build image) in place of MSYS2's, which bring every codec there is
# (x265, aom, SvtAv1, rav1e, vpx, librsvg, shaderc, Python: about 180 MB of
# DLLs). Only what Quince plays, filters and writes:
#   playback   https (schannel), WebM and MP4, Opus, AAC, VP9, H.264, AV1
#              (dav1d, D3D11VA/DXVA2), WebVTT captions
#   downloads  opus, ipod (.m4a) and mp3 muxers, MP3 encoding (LAME)
#   lavfi      the audio effects and visualizers (src/player/AudioEffects.cpp,
#              src/app/AppController.cpp) and what mpv's filter glue needs
# Builds with prefix /ucrt64 into <cache>/media-root once per version, then
# copies that over the MSYS2 packages in the sysroot.
# usage: build-media.sh <cache dir> <sysroot>
set -euo pipefail
cache=$1
sysroot=$2

FFMPEG=9.0.2
FFMPEG_SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
MPV=0.41.0
MPV_SHA256=ee21092a5ee427353392360929dc64645c54479aefdb5babc5cfbb5fad626209
PLACEBO=7.360.1

src=$cache/media-src
stage=$cache/media-root
want="ffmpeg-$FFMPEG libplacebo-$PLACEBO mpv-$MPV $(sha256sum "$0" | cut -c1-16)"
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

fetch() { # url sha256 file
    curl -sSfL -o "$src/$3" "$1"
    echo "$2  $src/$3" | sha256sum -c --quiet
}
fetch "https://ffmpeg.org/releases/ffmpeg-$FFMPEG.tar.xz" $FFMPEG_SHA256 ffmpeg.tar.xz
fetch "https://github.com/mpv-player/mpv/archive/refs/tags/v$MPV.tar.gz" $MPV_SHA256 mpv.tar.gz
tar -xf "$src/ffmpeg.tar.xz" -C "$src"
tar -xf "$src/mpv.tar.gz" -C "$src"
# its release archives leave out the submodules it builds with (glad, jinja)
git clone -q --depth 1 --branch "v$PLACEBO" --recurse-submodules --shallow-submodules \
    https://code.videolan.org/videolan/libplacebo.git "$src/libplacebo-$PLACEBO"

cat > "$src/cross.ini" <<EOF
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
EOF

join() { local IFS=,; echo "$*"; }
cd "$src/ffmpeg-$FFMPEG"
./configure --prefix=/ucrt64 --target-os=mingw32 --arch=x86_64 --cross-prefix=$host- \
    --pkg-config=pkg-config --extra-cflags="-I$sysroot/ucrt64/include" --extra-ldflags="-L$sysroot/ucrt64/lib" \
    --enable-shared --disable-static --disable-programs --disable-doc --disable-debug \
    --disable-autodetect --disable-everything --disable-avdevice \
    --enable-w32threads --enable-schannel --enable-zlib --enable-d3d11va --enable-dxva2 \
    --enable-libdav1d --enable-libmp3lame \
    --enable-protocol="$(join file http https tcp tls)" \
    --enable-demuxer="$(join matroska mov webvtt)" \
    --enable-muxer="$(join opus ogg ipod mp4 mp3)" \
    --enable-decoder="$(join opus aac vp9 h264 av1 libdav1d webvtt mjpeg png)" \
    --enable-encoder=libmp3lame \
    --enable-parser="$(join opus aac vp9 h264 av1 mpegaudio)" \
    --enable-bsf="$(join vp9_superframe_split extract_extradata aac_adtstoasc)" \
    --enable-hwaccel="$(join h264_d3d11va h264_d3d11va2 h264_dxva2 vp9_d3d11va vp9_d3d11va2 vp9_dxva2 \
                             av1_d3d11va av1_d3d11va2 av1_dxva2)" \
    --enable-filter="$(join equalizer alimiter acompressor silencedetect asplit colorkey \
                            showfreqs showwaves showcqt avectorscope \
                            abuffer abuffersink buffer buffersink aformat format aresample scale \
                            null anull volume)"
make -j"$(nproc)"
make install DESTDIR="$stage"
install_stage

cd "$src/libplacebo-$PLACEBO"
meson setup build --cross-file "$src/cross.ini" --prefix=/ucrt64 --buildtype=release \
    -Dvulkan=disabled -Dopengl=enabled -Dd3d11=disabled -Dglslang=disabled -Dshaderc=disabled \
    -Dlcms=disabled -Ddovi=disabled -Dlibdovi=disabled -Dunwind=disabled -Dxxhash=disabled \
    -Ddemos=false -Dtests=false -Dbench=false -Dfuzz=false
meson compile -C build
DESTDIR="$stage" meson install -C build
install_stage

cd "$src/mpv-$MPV"
# everything off but what libmpv's OpenGL render API, WASAPI and Windows'
# hardware decoding need
meson setup build --cross-file "$src/cross.ini" --prefix=/ucrt64 --buildtype=release \
    -Dauto_features=disabled -Dcplayer=false -Dlibmpv=true -Dtests=false \
    -Dgl=enabled -Dplain-gl=enabled -Dgl-win32=enabled -Dd3d-hwaccel=enabled -Dgl-dxinterop=enabled \
    -Dwin32-threads=enabled -Dwasapi=enabled -Dzlib=enabled
meson compile -C build
DESTDIR="$stage" meson install -C build
install_stage

echo "$want" > "$stage/.built"
