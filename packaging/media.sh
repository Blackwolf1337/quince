# shellcheck shell=bash disable=SC2034
# What Quince's own FFmpeg, libplacebo and mpv are made of, for
# packaging/windows/build-media.sh and packaging/macos/build-media.sh
# (sourced). Only what Quince plays, filters and writes:
#   playback   https, WebM and MP4, Opus, AAC, VP9, H.264, AV1 (dav1d),
#              WebVTT captions
#   downloads  opus, ipod (.m4a) and mp3 muxers, MP3 encoding (LAME)
#   lavfi      the audio effects and visualizers (src/player/AudioEffects.cpp,
#              src/app/AppController.cpp) and what mpv's filter glue needs
# The platform scripts add TLS and hardware decoding.

FFMPEG=9.0.2
FFMPEG_SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
MPV=0.41.0
MPV_SHA256=ee21092a5ee427353392360929dc64645c54479aefdb5babc5cfbb5fad626209
PLACEBO=7.360.1

media_join() { local IFS=,; echo "$*"; }

# configure options of FFmpeg common to all platforms
FFMPEG_OPTIONS=(
    --enable-shared --disable-static --disable-programs --disable-doc --disable-debug
    --disable-autodetect --disable-everything --disable-avdevice
    --enable-zlib --enable-libdav1d --enable-libmp3lame
    --enable-protocol="$(media_join file http https tcp tls)"
    --enable-demuxer="$(media_join matroska mov webvtt)"
    --enable-muxer="$(media_join opus ogg ipod mp4 mp3)"
    --enable-decoder="$(media_join opus aac vp9 h264 av1 libdav1d webvtt mjpeg png)"
    --enable-encoder=libmp3lame
    --enable-parser="$(media_join opus aac vp9 h264 av1 mpegaudio)"
    --enable-bsf="$(media_join vp9_superframe_split extract_extradata aac_adtstoasc)"
    --enable-filter="$(media_join equalizer alimiter acompressor silencedetect asplit colorkey \
                                  showfreqs showwaves showcqt avectorscope \
                                  abuffer abuffersink buffer buffersink aformat format aresample scale \
                                  null anull volume)"
)

# hardware decoders of FFmpeg for an API (d3d11va, dxva2, videotoolbox)
media_hwaccels() {
    local api codec out=()
    for api in "$@"; do
        for codec in h264 vp9 av1; do
            out+=("${codec}_$api")
        done
    done
    media_join "${out[@]}"
}

# libplacebo for mpv's OpenGL renderer, nothing else
PLACEBO_OPTIONS=(
    -Dvulkan=disabled -Dopengl=enabled -Dd3d11=disabled -Dglslang=disabled -Dshaderc=disabled
    -Dlcms=disabled -Ddovi=disabled -Dlibdovi=disabled -Dunwind=disabled -Dxxhash=disabled
    -Ddemos=false -Dtests=false -Dbench=false -Dfuzz=false
)

# everything of mpv off but libmpv's OpenGL render API; the platform scripts
# add the audio output and hardware decoding
MPV_OPTIONS=(
    -Dauto_features=disabled -Dcplayer=false -Dlibmpv=true -Dtests=false
    -Dgl=enabled -Dplain-gl=enabled -Dzlib=enabled
)

# the sources, checked against their hashes, into <dir>
media_fetch() {
    local dir=$1
    curl -sSfL -o "$dir/ffmpeg.tar.xz" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG.tar.xz"
    curl -sSfL -o "$dir/mpv.tar.gz" "https://github.com/mpv-player/mpv/archive/refs/tags/v$MPV.tar.gz"
    local sum=(sha256sum)
    command -v sha256sum > /dev/null || sum=(shasum -a 256)
    (cd "$dir" && printf '%s  ffmpeg.tar.xz\n%s  mpv.tar.gz\n' $FFMPEG_SHA256 $MPV_SHA256 | "${sum[@]}" -c --quiet)
    tar -xf "$dir/ffmpeg.tar.xz" -C "$dir"
    tar -xf "$dir/mpv.tar.gz" -C "$dir"
    # its release archives leave out the submodules it builds with (glad, jinja)
    git clone -q --depth 1 --branch "v$PLACEBO" --recurse-submodules --shallow-submodules \
        https://code.videolan.org/videolan/libplacebo.git "$dir/libplacebo-$PLACEBO"
}
