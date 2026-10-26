#pragma once

// The parts of the Slint frontend, each bound in a file of its own and called
// from bindCore() on the core thread. Rules: src/slint/Bridge.h.

#include "Bridge.h"

#include <QString>
#include <QVariantMap>

#include <functional>
#include <string_view>
#include <vector>

class AppController;
class ImageCache;
class SlintHost;
class VideoRenderer;

// Item menus (area B) act on items that other areas show. Each source
// ("queue", "page", ...) gives the item at (a, b), e.g. row and card; an empty
// map when there is none. Core thread.
using ItemLookup = std::function<QVariantMap(int a, int b)>;
void registerItemSource(const QString &source, ItemLookup lookup);
QVariantMap lookupItem(const QString &source, int a, int b);

// The tracks selected on the open page (Slint thread, BrowseBindings.cpp):
// the selection when row is one of at least two selected, else none
std::vector<int> selectedPageRows(ui::MainWindow &w, int row);
// mode: toggle or range (from the last toggled row) at row, all, clear
void selectPageRows(ui::MainWindow &w, int row, std::string_view mode);

// area A, BrowseBindings.cpp: pages, the page stack, back and forward, search results
void bindBrowse(AppController &app, const bridge::Window &window, ImageCache *images);
// area B, SearchBindings.cpp: the search field's suggestions, item menus, tooltips
void bindSearch(AppController &app, const bridge::Window &window, ImageCache *images);
// area C, NowPlayingBindings.cpp: Now Playing, lyrics, mini player
void bindNowPlaying(AppController &app, const bridge::Window &window, ImageCache *images, VideoRenderer *video);
// area D, DialogsBindings.cpp: settings, equalizer, sign-in, playlist picker, downloads
void bindDialogs(AppController &app, const bridge::Window &window, ImageCache *images, SlintHost *host);
