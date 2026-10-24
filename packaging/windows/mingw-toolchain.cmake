# Cross build for Windows: the host's MinGW, MSYS2's UCRT64 libraries in
# MSYS2_SYSROOT (default /msys, holding ucrt64/), the host's Qt as host tools.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

if(NOT MSYS2_SYSROOT)
    set(MSYS2_SYSROOT /msys)
endif()
set(CMAKE_FIND_ROOT_PATH ${MSYS2_SYSROOT}/ucrt64)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_PREFIX_PATH ${MSYS2_SYSROOT}/ucrt64)

# moc, rcc and lrelease of the host's Qt (the same version)
set(QT_HOST_PATH /usr)

# the .pc files say prefix=/ucrt64
set(ENV{PKG_CONFIG_LIBDIR} ${MSYS2_SYSROOT}/ucrt64/lib/pkgconfig:${MSYS2_SYSROOT}/ucrt64/share/pkgconfig)
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${MSYS2_SYSROOT})

# tools built during the build (mecab-dict-index compiles the dictionary) are
# Windows programs: Wine runs them, with the DLLs of the MSYS2 tree
set(CMAKE_CROSSCOMPILING_EMULATOR
    env WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml= WINEPATH=Z:${MSYS2_SYSROOT}/ucrt64/bin wine)
