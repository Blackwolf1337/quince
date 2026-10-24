# Phase 0 spike: Slint window with mpv video in a sub-rect, a Qt core thread
# and a tray icon. Not part of the quince build.
# cmake -S spike -B build-spike -G Ninja && cmake --build build-spike
cmake_minimum_required(VERSION 3.28)
project(quince-spike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(MOLD_PROGRAM mold)
    if(MOLD_PROGRAM)
        set(CMAKE_LINKER_TYPE MOLD)
    endif()
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
if(WIN32)
    # same as quince: MSYS2's .pc files name /ucrt64/include
    get_target_property(dirs PkgConfig::MPV INTERFACE_INCLUDE_DIRECTORIES)
    set(found)
    foreach(dir IN LISTS dirs)
        if(EXISTS "${dir}")
            list(APPEND found "${dir}")
        endif()
    endforeach()
    if(MSYS2_SYSROOT)
        # cross build: the .pc path only exists inside the Docker image
        list(APPEND found "${MSYS2_SYSROOT}/ucrt64/include")
    endif()
    set_target_properties(PkgConfig::MPV PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${found}")
endif()

# FemtoVG (GL) for the mpv underlay, software renderer as fallback. Static on
# Linux only: Slint's headers always declare dllimport on Windows, and a static
# libslint_cpp.a does not carry its macOS frameworks to the final link
include(FetchContent)
if(WIN32 OR APPLE)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
else()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()
set(SLINT_FEATURE_INTERPRETER OFF CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_SKIA OFF CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_FEMTOVG ON CACHE BOOL "" FORCE)
set(SLINT_FEATURE_SYSTEM_TRAY ON CACHE BOOL "" FORCE)
FetchContent_Declare(Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG v1.18.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR api/cpp
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/patch-slint.cmake
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(Slint)

add_executable(quince-spike main.cpp)
target_compile_options(quince-spike PRIVATE -Wall -Wextra)
slint_target_sources(quince-spike ui.slint)
set_property(TARGET quince-spike PROPERTY SLINT_EMBED_RESOURCES embed-files)
target_link_libraries(quince-spike PRIVATE Slint::Slint Qt6::Core PkgConfig::MPV)
if(WIN32)
    target_link_libraries(quince-spike PRIVATE opengl32)
elseif(APPLE)
    # glProc() looks the GL entry points up with dlsym(RTLD_DEFAULT)
    target_link_libraries(quince-spike PRIVATE "-framework OpenGL")
else()
    # static Slint finds system fonts through fontconfig but does not link it
    pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
    # SPIKE_OWNCTX: mpv's own EGL context
    pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
    target_link_libraries(quince-spike PRIVATE PkgConfig::FONTCONFIG PkgConfig::EGL ${CMAKE_DL_LIBS})
endif()
