cmake_minimum_required(VERSION 3.28)
project(quince VERSION 0.1.1 LANGUAGES CXX)
if(APPLE)
    # notifications (src/slint/DesktopNotify.mm)
    enable_language(OBJCXX)
endif()
if(WIN32)
    # the icon and version info of quince.exe
    enable_language(RC)
endif()

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

find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Network Sql LinguistTools)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.8 REQUIRED COMPONENTS DBus)
    # Chromium's cookie encryption (AES-128-CBC)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
# downloads: remuxing and MP3 encoding (the FFmpeg mpv uses)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
# romanized lyrics: pinyin and Simplified/Traditional Chinese (Qt uses the same ICU)
pkg_check_modules(ICU REQUIRED IMPORTED_TARGET icu-uc icu-i18n)
if(WIN32)
    # MSYS2's .pc files name MSYS paths (/ucrt64/include) that a native CMake
    # rejects as missing; it is the compiler's own include directory anyway
    foreach(target PkgConfig::MPV PkgConfig::FFMPEG PkgConfig::ICU)
        get_target_property(dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
        set(found)
        foreach(dir IN LISTS dirs)
            if(EXISTS "${dir}")
                list(APPEND found "${dir}")
            endif()
        endforeach()
        set_target_properties(${target} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${found}")
    endforeach()
endif()

# QuickJS-ng (MIT) runs yt-dlp's solver for YouTube's player puzzles
include(FetchContent)
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(quickjs
    URL https://github.com/quickjs-ng/quickjs/archive/refs/tags/v0.17.0.tar.gz
    URL_HASH SHA256=559bc4c420475e55c7ab4510adbc562f55d7524d75e8e89d79ce4bb02f5687d9
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_MakeAvailable(quickjs)
# its asserts carry __FILE__: keep the build machine's paths out of the binary
if(NOT MSVC)
    target_compile_options(qjs PRIVATE -ffile-prefix-map=${quickjs_SOURCE_DIR}/=quickjs/)
endif()
include(GNUInstallDirs)
# Windows: a portable folder (quince.exe, mecab\ipadic, licenses\ side by
# side); macOS: everything inside Quince.app; else the usual prefix layout
if(WIN32)
    set(QUINCE_BIN_DESTINATION .)
    set(QUINCE_DATA_DESTINATION .)
    set(QUINCE_LICENSE_DESTINATION licenses)
else()
    set(QUINCE_BIN_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(QUINCE_DATA_DESTINATION ${CMAKE_INSTALL_DATADIR}/quince)
    set(QUINCE_LICENSE_DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/quince)
endif()
include(cmake/Mecab.cmake)

qt_standard_project_setup(REQUIRES 6.8)

# The player without a user interface: YouTube Music, playback, lyrics,
# downloads, desktop integration. What it needs from the user interface goes
# through app/UiHost.h.
qt_add_library(quince_core STATIC
    src/app/AppController.h
    src/app/AppController.cpp
    src/app/ArtCache.h
    src/app/ArtCache.cpp
    src/app/ColorTheme.h
    src/app/ColorTheme.cpp
    src/app/CommandLine.h
    src/app/CommandLine.cpp
    src/app/Language.h
    src/app/Language.cpp
    src/app/Settings.h
    src/app/Settings.cpp
    src/app/TestCommands.h
    src/app/TestCommands.cpp
    src/app/UiHost.h
    src/download/AudioFile.h
    src/download/AudioFile.cpp
    src/download/Downloader.h
    src/download/Downloader.cpp
    src/integrations/Discord.h
    src/integrations/Discord.cpp
    src/integrations/Notifications.h
    src/integrations/Notifications.cpp
    src/integrations/SponsorBlock.h
    src/integrations/SponsorBlock.cpp
    src/lyrics/Lyrics.h
    src/lyrics/Lyrics.cpp
    src/lyrics/Romanizer.h
    src/lyrics/Romanizer.cpp
    src/player/AudioEffects.h
    src/player/AudioEffects.cpp
    src/player/JsSolver.h
    src/player/JsSolver.cpp
    src/player/MpvNode.h
    src/player/MpvNode.cpp
    src/player/MpvPlayer.h
    src/player/MpvPlayer.cpp
    src/player/Playback.h
    src/player/Playback.cpp
    src/player/Queue.h
    src/player/Queue.cpp
    src/player/StreamResolver.h
    src/player/StreamResolver.cpp
    src/ytm/Account.h
    src/ytm/Account.cpp
    src/ytm/AccountCookies.h
    src/ytm/AccountCookies.cpp
    src/ytm/BrowsePage.h
    src/ytm/BrowsePage.cpp
    src/ytm/BrowserCookies.h
    src/ytm/BrowserCookies.cpp
    src/ytm/InnerTube.h
    src/ytm/InnerTube.cpp
    src/ytm/Json.h
    src/ytm/NetscapeCookies.h
    src/ytm/NetscapeCookies.cpp
    src/ytm/Parser.h
    src/ytm/Parser.cpp
    src/ytm/SearchSuggestions.h
    src/ytm/SearchSuggestions.cpp
)
# yt-dlp's challenge solver (yt-dlp/ejs, Unlicense; see third_party/ejs)
qt_add_resources(quince_core ejs
    PREFIX /ejs
    BASE third_party/ejs
    FILES
        third_party/ejs/lib.min.js
        third_party/ejs/core.min.js
)

# the sources include each other's headers by file name
target_include_directories(quince_core PUBLIC src src/app src/player src/ytm src/lyrics src/download src/integrations)
target_compile_definitions(quince_core PUBLIC PROJECT_VERSION="${PROJECT_VERSION}")
if(NOT MSVC)
    target_compile_options(quince_core PRIVATE -Wall -Wextra)
endif()
target_link_libraries(quince_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Sql
    PRIVATE
        PkgConfig::MPV
        PkgConfig::FFMPEG
        PkgConfig::ICU
        qjs
        mecab
)
if(TARGET OpenSSL::Crypto)
    target_link_libraries(quince_core PRIVATE OpenSSL::Crypto)
endif()
# the system's media controls where there is no MPRIS
if(WIN32)
    target_sources(quince_core PRIVATE
        src/integrations/MediaControls.h
        src/integrations/MediaControls.cpp
        src/integrations/MediaControlsWin.cpp
    )
    # RoGetActivationFactory and the HSTRING functions
    target_link_libraries(quince_core PRIVATE runtimeobject)
elseif(APPLE)
    target_sources(quince_core PRIVATE
        src/integrations/MediaControls.h
        src/integrations/MediaControls.cpp
        src/integrations/MediaControlsMac.mm
    )
    set_source_files_properties(src/integrations/MediaControlsMac.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(quince_core PRIVATE "-framework MediaPlayer" "-framework AppKit")
endif()
if(TARGET Qt6::DBus)
    # MPRIS and notifications over the session bus
    target_sources(quince_core PRIVATE
        src/integrations/GlobalShortcuts.h
        src/integrations/GlobalShortcuts.cpp
        src/integrations/Mpris.h
        src/integrations/Mpris.cpp
    )
    target_link_libraries(quince_core PUBLIC Qt6::DBus)
    target_compile_definitions(quince_core PUBLIC QUINCE_DBUS)
endif()

# The user interface (Slint, see docs/architecture.md). Building Slint needs
# a Rust toolchain, 1.92 or newer.
#
# FemtoVG (OpenGL) for the video underlay, the software renderer as a
# fallback. Static on Linux only: Slint's headers always declare dllimport on
# Windows, and a static libslint_cpp.a does not carry its macOS frameworks to
# the final link.
if(WIN32 OR APPLE)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
else()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()
set(SLINT_FEATURE_INTERPRETER OFF CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_SKIA OFF CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_FEMTOVG ON CACHE BOOL "" FORCE)
set(SLINT_FEATURE_SYSTEM_TRAY ON CACHE BOOL "" FORCE)
set(SLINT_STYLE "fluent-dark" CACHE STRING "Slint widget style")
FetchContent_Declare(Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG v1.18.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR api/cpp
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SlintPatch.cmake
    UPDATE_DISCONNECTED TRUE
    # also keeps Slint's own install rules (compiler, headers) out of the package
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Slint)
# a smaller library: link-time optimization over all of Slint's crates
corrosion_set_env_vars(slint_cpp CARGO_PROFILE_RELEASE_LTO=fat CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1)
# the rest of the project builds its own libraries statically
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# nativefiledialog-extended (zlib): the system's file and folder pickers. On
# Linux through the xdg desktop portal over libdbus, without GTK; no parent
# window is passed, so no X11 or Wayland glue.
set(NFD_PORTAL ON CACHE BOOL "" FORCE)
set(NFD_X11 OFF CACHE BOOL "" FORCE)
set(NFD_WAYLAND OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nfd
    URL https://github.com/btzy/nativefiledialog-extended/archive/refs/tags/v1.4.1.tar.gz
    URL_HASH SHA256=4a01eafa921169f009f3e83bea8e287d9e5d0b94ca9064cf7f0a546652d7c884
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_MakeAvailable(nfd)

add_executable(quince
    src/slint/main.cpp
    src/slint/Areas.h
    src/slint/BrowseBindings.cpp
    src/slint/SearchBindings.cpp
    src/slint/NowPlayingBindings.cpp
    src/slint/DialogsBindings.cpp
    src/slint/FilePicker.h
    src/slint/FilePicker.cpp
    src/slint/PropertyTable.h
    src/slint/Blur.h
    src/slint/Blur.cpp
    src/slint/DesktopNotify.h
    src/slint/Frost.h
    src/slint/Frost.cpp
    src/slint/Bindings.h
    src/slint/Bindings.cpp
    src/slint/Bridge.h
    src/slint/Gl.h
    src/slint/Gl.cpp
    src/slint/ImageCache.h
    src/slint/ImageCache.cpp
    src/slint/MiniPlayerWindow.h
    src/slint/MiniPlayerWindow.cpp
    src/slint/ModelMirror.h
    src/slint/Pages.h
    src/slint/Pages.cpp
    src/slint/SlintHost.h
    src/slint/SlintHost.cpp
    src/slint/TestDriver.h
    src/slint/TestDriver.cpp
    src/slint/VideoRenderer.h
    src/slint/VideoRenderer.cpp
)
slint_target_sources(quince ui/main.slint NAMESPACE ui)
# the mini player's window, with globals of its own
slint_target_sources(quince ui/mini-player.slint NAMESPACE mini)
set_property(TARGET quince PROPERTY SLINT_EMBED_RESOURCES embed-files)
# i18n/slint/<lang>/LC_MESSAGES/quince.po (docs/building.md: translations)
set_property(TARGET quince PROPERTY SLINT_BUNDLE_TRANSLATIONS ${CMAKE_CURRENT_SOURCE_DIR}/i18n/slint)
target_include_directories(quince PRIVATE src/slint)
if(NOT MSVC)
    target_compile_options(quince PRIVATE -Wall -Wextra)
endif()
target_link_libraries(quince PRIVATE quince_core Slint::Slint PkgConfig::MPV nfd::nfd)
add_dependencies(quince mecab-ipadic)
if(APPLE)
    target_sources(quince PRIVATE src/slint/DesktopNotify.mm)
else()
    target_sources(quince PRIVATE src/slint/DesktopNotify.cpp)
endif()
set_target_properties(quince PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
if(WIN32)
    # the icon (also the notification's) and version info
    set(QUINCE_ICO ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/quince.ico)
    configure_file(packaging/windows/quince.rc.in ${CMAKE_CURRENT_BINARY_DIR}/quince.rc @ONLY)
    target_sources(quince PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/quince.rc)
    # ole32 and uuid: nfd's COM dialog, which its static library does not link
    target_link_libraries(quince PRIVATE opengl32 shell32 ole32 uuid)
elseif(APPLE)
    # Gl.cpp looks the GL entry points up with dlsym(RTLD_DEFAULT)
    target_link_libraries(quince PRIVATE "-framework OpenGL" "-framework Foundation"
                          "-framework UserNotifications")
    # the notification center needs a bundle identifier
    set(QUINCE_BUNDLE_ID "io.github.blackwolf1337.quince" CACHE STRING "Bundle identifier of Quince.app")
    target_sources(quince PRIVATE packaging/macos/quince.icns)
    set_source_files_properties(packaging/macos/quince.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(quince PROPERTIES
        OUTPUT_NAME Quince
        MACOSX_BUNDLE_BUNDLE_NAME Quince
        MACOSX_BUNDLE_GUI_IDENTIFIER ${QUINCE_BUNDLE_ID}
        MACOSX_BUNDLE_ICON_FILE quince.icns
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
    # the dictionary, the licenses and Slint's library travel inside the bundle
    add_custom_command(TARGET quince POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${MECAB_DIC_DIR}
                $<TARGET_BUNDLE_CONTENT_DIR:quince>/Resources/mecab/ipadic
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE ${MECAB_LICENSE_FILES}
                $<TARGET_BUNDLE_CONTENT_DIR:quince>/Resources
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_CONTENT_DIR:quince>/Frameworks
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:slint_cpp-shared>
                $<TARGET_BUNDLE_CONTENT_DIR:quince>/Frameworks/
        VERBATIM
    )
else()
    # static Slint finds fonts through fontconfig but does not link it;
    # mpv's own EGL context (VideoRenderer.cpp)
    pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
    pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
    target_link_libraries(quince PRIVATE PkgConfig::FONTCONFIG PkgConfig::EGL ${CMAKE_DL_LIBS})
endif()

install(TARGETS quince
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${QUINCE_BIN_DESTINATION}
)
if(WIN32)
    # Slint is a DLL on Windows (its headers always declare dllimport)
    install(FILES $<TARGET_FILE:slint_cpp-shared> DESTINATION ${QUINCE_BIN_DESTINATION})
endif()


# Sign-in window in the system's own web view (login/Browser.h): WKWebView on
# macOS, WebKitGTK on Linux (built when it is there); the player itself never
# loads a browser.
option(QUINCE_LOGIN "Build the sign-in window quince-login" ON)
if(QUINCE_LOGIN AND APPLE)
    set(QUINCE_LOGIN_BROWSER login/BrowserMac.mm)
    set(QUINCE_LOGIN_LIBS "-framework WebKit" "-framework AppKit")
    set_source_files_properties(login/BrowserMac.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
elseif(QUINCE_LOGIN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pkg_check_modules(WEBKIT IMPORTED_TARGET webkitgtk-6.0 gtk4)
    if(WEBKIT_FOUND)
        set(QUINCE_LOGIN_BROWSER login/BrowserGtk.cpp)
        set(QUINCE_LOGIN_LIBS PkgConfig::WEBKIT)
    else()
        message(STATUS "WebKitGTK 6 not found: no quince-login, sign in by importing cookies")
    endif()
endif()
if(QUINCE_LOGIN_BROWSER)
    qt_add_executable(quince-login
        login/main.cpp
        login/Browser.h
        ${QUINCE_LOGIN_BROWSER}
        src/ytm/NetscapeCookies.cpp
    )
    target_include_directories(quince-login PRIVATE src)
    if(NOT MSVC)
        target_compile_options(quince-login PRIVATE -Wall -Wextra)
    endif()
    target_link_libraries(quince-login PRIVATE Qt6::Core Qt6::Network ${QUINCE_LOGIN_LIBS})
    # GLib's headers use "signals" as a name
    target_compile_definitions(quince-login PRIVATE QT_NO_KEYWORDS)
    set_target_properties(quince-login PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE OFF)
    if(APPLE)
        # next to the player inside Quince.app, where Account looks for it
        add_dependencies(quince-login quince)
        add_custom_command(TARGET quince-login POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:quince-login> $<TARGET_BUNDLE_CONTENT_DIR:quince>/MacOS/
            VERBATIM
        )
    endif()
endif()

# development aid: prints what the parser makes of a saved InnerTube answer
qt_add_executable(quince-parsedump
    tools/parsedump.cpp
    src/ytm/Parser.cpp
)
target_include_directories(quince-parsedump PRIVATE src)
if(NOT MSVC)
    target_compile_options(quince-parsedump PRIVATE -Wall -Wextra)
endif()
target_link_libraries(quince-parsedump PRIVATE Qt6::Core)
set_target_properties(quince-parsedump PROPERTIES WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF)

# development aid: romanizes lines given as arguments (the Lyrics code path)
qt_add_executable(quince-romanize
    tools/romanize.cpp
    src/lyrics/Romanizer.cpp
)
target_include_directories(quince-romanize PRIVATE src)
if(NOT MSVC)
    target_compile_options(quince-romanize PRIVATE -Wall -Wextra)
endif()
target_link_libraries(quince-romanize PRIVATE Qt6::Core PkgConfig::ICU mecab)
add_dependencies(quince-romanize mecab-ipadic)
set_target_properties(quince-romanize PROPERTIES WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF)

# UI texts; the language follows the system locale. The .ts files hold the
# core's and quince-login's tr() texts; those of the .slint files are gettext
# catalogs in i18n/slint (SLINT_BUNDLE_TRANSLATIONS above)
set(QUINCE_TRANSLATED quince)
if(TARGET quince-login)
    list(APPEND QUINCE_TRANSLATED quince-login)
endif()
qt_add_translations(TARGETS ${QUINCE_TRANSLATED}
    # the core's tr() texts land in quince's .qm
    SOURCE_TARGETS ${QUINCE_TRANSLATED} quince_core
    TS_FILES i18n/quince_de.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete
)

if(TARGET quince-login)
    # found next to the player or on PATH
    install(TARGETS quince-login RUNTIME DESTINATION ${QUINCE_BIN_DESTINATION})
endif()
install(FILES LICENSE DESTINATION ${QUINCE_LICENSE_DESTINATION})
if(UNIX AND NOT APPLE)
    install(FILES packaging/quince.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ui/icons/quince.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
