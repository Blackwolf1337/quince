# Cross-compiles Quince for Windows on Linux: Arch's MinGW (UCRT, the same
# GCC as MSYS2) and Arch's Qt as host tools, MSYS2's UCRT64 packages as the
# Windows libraries. Used by packaging/windows/cross-build.sh.
FROM archlinux:latest
# the image's default mirror can be very slow; cross-build.sh hands in the host's
ARG MIRROR=https://geo.mirror.pkgbuild.com/\$repo/os/\$arch
# downloads are tried three times: a mirror in the middle of a sync fails some
RUN echo "Server = $MIRROR" > /etc/pacman.d/mirrorlist \
    && sed -i 's/^#\?ParallelDownloads.*/ParallelDownloads = 8/' /etc/pacman.conf \
    && for i in 1 2 3; do \
        pacman -Syu --noconfirm --needed \
            mingw-w64-gcc cmake ninja meson nasm make pkgconf python libarchive curl zip git \
            qt6-base qt6-tools wine \
            xorg-server-xvfb xorg-xauth mesa \
        && break; [ $i = 3 ] && exit 1; sleep 20; done \
    && pacman -Scc --noconfirm
# Slint: Rust with the MinGW target, and a host C compiler for the build
# scripts of Slint's crates; cargo's downloads go to the cache (CARGO_HOME in
# cross-build.sh)
ENV RUSTUP_HOME=/opt/rustup
RUN for i in 1 2 3; do pacman -Syu --noconfirm --needed gcc rustup && break; [ $i = 3 ] && exit 1; sleep 20; done \
    && for i in 1 2 3; do \
        rustup default stable && rustup target add x86_64-pc-windows-gnu && break; [ $i = 3 ] && exit 1; sleep 20; done \
    && chmod -R a+rX /opt/rustup \
    && pacman -Scc --noconfirm
