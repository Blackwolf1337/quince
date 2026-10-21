# Cross-compiles Quince for Windows on Linux: Arch's MinGW (UCRT, the same
# GCC as MSYS2) and Arch's Qt as host tools, MSYS2's UCRT64 packages as the
# Windows libraries. Used by packaging/windows/cross-build.sh.
FROM archlinux:latest
# the image's default mirror can be very slow; cross-build.sh hands in the host's
ARG MIRROR=https://geo.mirror.pkgbuild.com/\$repo/os/\$arch
RUN echo "Server = $MIRROR" > /etc/pacman.d/mirrorlist \
    && sed -i 's/^#\?ParallelDownloads.*/ParallelDownloads = 8/' /etc/pacman.conf \
    && pacman -Syu --noconfirm --needed \
        mingw-w64-gcc cmake ninja pkgconf python libarchive curl zip git \
        qt6-base qt6-declarative qt6-tools qt6-shadertools wine \
        xorg-server-xvfb xorg-xauth mesa \
    && pacman -Scc --noconfirm
