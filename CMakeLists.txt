cmake_minimum_required(VERSION 3.28)
project(quince VERSION 0.1.0 LANGUAGES CXX)

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

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Network Widgets)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.8 REQUIRED COMPONENTS DBus)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(quince
    src/main.cpp
)

set_source_files_properties(qml/Style.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(quince
    URI Quince
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Style.qml
        qml/Art.qml
        qml/BrowseView.qml
        qml/Card.qml
        qml/CardRow.qml
        qml/CarouselRow.qml
        qml/ChipsRow.qml
        qml/GridRow.qml
        qml/HeaderRow.qml
        qml/ItemMenu.qml
        qml/ItemRow.qml
        qml/LyricsView.qml
        qml/NavButton.qml
        qml/PlayButtons.qml
        qml/PlayerBar.qml
        qml/PlaylistPicker.qml
        qml/QueuePanel.qml
        qml/SearchField.qml
        qml/SettingsDialog.qml
        qml/Sidebar.qml
        qml/StyledMenu.qml
        qml/SignInDialog.qml
        qml/TextRow.qml
        qml/TitleRow.qml
    RESOURCES
        qml/icons/account.svg
        qml/icons/back.svg
        qml/icons/chevron-left.svg
        qml/icons/chevron-right.svg
        qml/icons/dislike.svg
        qml/icons/explore.svg
        qml/icons/heart.svg
        qml/icons/history.svg
        qml/icons/home.svg
        qml/icons/library.svg
        qml/icons/like.svg
        qml/icons/next.svg
        qml/icons/pause.svg
        qml/icons/play.svg
        qml/icons/playlist.svg
        qml/icons/previous.svg
        qml/icons/queue.svg
        qml/icons/quince.svg
        qml/icons/radio.svg
        qml/icons/search.svg
        qml/icons/settings.svg
        qml/icons/shuffle.svg
        qml/icons/volume.svg
        qml/icons/volume-off.svg
    SOURCES
        src/app/AppController.h
        src/app/AppController.cpp
        src/app/ArtNetwork.h
        src/app/Settings.h
        src/app/Settings.cpp
        src/app/ColorTheme.h
        src/app/ColorTheme.cpp
        src/integrations/Desktop.h
        src/integrations/Desktop.cpp
        src/integrations/Discord.h
        src/integrations/Discord.cpp
        src/lyrics/Lyrics.h
        src/lyrics/Lyrics.cpp
        src/app/ArtNetwork.cpp
        src/app/TestDriver.h
        src/app/TestDriver.cpp
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
        src/ytm/BrowsePage.h
        src/ytm/BrowsePage.cpp
        src/ytm/InnerTube.h
        src/ytm/InnerTube.cpp
        src/ytm/Account.h
        src/ytm/Account.cpp
        src/ytm/AccountCookies.h
        src/ytm/AccountCookies.cpp
        src/ytm/Json.h
        src/ytm/NetscapeCookies.h
        src/ytm/NetscapeCookies.cpp
        src/ytm/Parser.h
        src/ytm/Parser.cpp
        src/ytm/SearchSuggestions.h
        src/ytm/SearchSuggestions.cpp
)

# qmltyperegistrar includes QML-exposed headers by file name only
target_include_directories(quince PRIVATE src src/app src/player src/ytm src/lyrics)
target_compile_definitions(quince PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")

if(NOT MSVC)
    target_compile_options(quince PRIVATE -Wall -Wextra)
endif()

target_link_libraries(quince PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Widgets
    PkgConfig::MPV
)
if(TARGET Qt6::DBus)
    # MPRIS and notifications over the session bus
    target_sources(quince PRIVATE
        src/integrations/GlobalShortcuts.h
        src/integrations/GlobalShortcuts.cpp
        src/integrations/Mpris.h
        src/integrations/Mpris.cpp
    )
    target_link_libraries(quince PRIVATE Qt6::DBus)
    target_compile_definitions(quince PRIVATE QUINCE_DBUS)
endif()

set_target_properties(quince PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# Sign-in window. Built only when QtWebEngine is there; the player itself
# never loads Chromium.
find_package(Qt6 6.8 QUIET COMPONENTS WebEngineWidgets)
if(Qt6WebEngineWidgets_FOUND)
    qt_add_executable(quince-login
        login/main.cpp
        src/ytm/NetscapeCookies.cpp
    )
    target_include_directories(quince-login PRIVATE src)
    if(NOT MSVC)
        target_compile_options(quince-login PRIVATE -Wall -Wextra)
    endif()
    target_link_libraries(quince-login PRIVATE Qt6::WebEngineWidgets Qt6::Network)
    set_target_properties(quince-login PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE OFF)
else()
    message(STATUS "QtWebEngine not found: no quince-login, sign in by importing cookies")
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

include(GNUInstallDirs)
install(TARGETS quince
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(TARGET quince-login)
    # found next to the player or on PATH
    install(TARGETS quince-login RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(UNIX AND NOT APPLE)
    install(FILES packaging/quince.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES qml/icons/quince.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
