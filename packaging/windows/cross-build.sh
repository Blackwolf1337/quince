#!/bin/bash
# Builds the Windows zip on Linux in Docker, like the windows job on GitHub.
# usage: [QUINCE_SLINT=ON] packaging/windows/cross-build.sh [cache dir]
#   cache (default ~/.cache/quince-windows): MSYS2 packages, the sysroot, the
#   build tree and dist/Quince; the zip lands in the repo's root folder
#   QUINCE_SLINT=ON also builds and packs quince-slint (the Slint frontend)
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
cache=${1:-$HOME/.cache/quince-windows}
# home holds Wine's prefix (it runs the build's Windows tools) between runs
mkdir -p "$cache/home/run" && chmod 700 "$cache/home/run"

# the host's first mirror, else Arch's worldwide one
mirror=$(sed -n 's/^Server *= *//p' /etc/pacman.d/mirrorlist 2>/dev/null | head -1)
docker build -q -t quince-windows-cross ${mirror:+--build-arg "MIRROR=$mirror"} "$repo/packaging/windows" > /dev/null
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/cache/home -e XDG_RUNTIME_DIR=/cache/home/run \
    -e CARGO_HOME=/cache/cargo -e QUINCE_SLINT="${QUINCE_SLINT:-OFF}" \
    -v "$repo:/src" -v "$cache:/cache" -w /src quince-windows-cross bash -euo pipefail -c '
python3 packaging/windows/fetch-msys2.py /cache /cache/msys \
    qt6-base qt6-declarative qt6-svg qt6-imageformats qt6-translations mpv ffmpeg icu libiconv
slint=()
[ "$QUINCE_SLINT" = ON ] && slint=(-DQUINCE_SLINT=ON -DRust_CARGO_TARGET=x86_64-pc-windows-gnu)
cmake -S . -B /cache/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DQUINCE_LOGIN=OFF "${slint[@]}" \
    -DCMAKE_TOOLCHAIN_FILE=/src/packaging/windows/mingw-toolchain.cmake -DMSYS2_SYSROOT=/cache/msys
cmake --build /cache/build
rm -rf /cache/dist && cmake --install /cache/build --prefix /cache/dist/Quince
python3 packaging/windows/deploy.py /cache/dist/Quince /cache/msys/ucrt64 /src/qml
ver=$(git -c safe.directory=/src describe --tags --always)
rm -f /src/Quince-*-windows-x64.zip
(cd /cache/dist && zip -qr9 "/src/Quince-$ver-windows-x64.zip" Quince)
ls -la /src/Quince-*-windows-x64.zip
'
