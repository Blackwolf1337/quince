#include "FilePicker.h"
#include "main.h"

#include <nfd.h>

#include <atomic>
#include <cstdio>
#include <optional>
#include <thread>

namespace filepicker {

namespace {

// nfd keeps its D-Bus connection (portal) and its error in globals
std::atomic<bool> s_open = false;

struct Request
{
    bool folder = false;
    std::string title;
    std::string filterName;
    std::string extension;
    std::string start;
};

// blocks until the picker closes; the path, or nothing when cancelled
std::optional<std::string> run(const Request &request)
{
    if (NFD_Init() != NFD_OKAY) {
        std::fprintf(stderr, "quince: no file picker: %s\n", NFD_GetError());
        NFD_ClearError();
        return std::nullopt;
    }
    nfdu8char_t *path = nullptr;
    nfdresult_t result;
    if (request.folder) {
        nfdpickfolderu8args_t args = {};
        args.defaultPath = request.start.empty() ? nullptr : request.start.c_str();
        result = NFD_PickFolderU8_With(&path, &args);
    } else {
        const nfdu8filteritem_t filter = { request.filterName.c_str(), request.extension.c_str() };
        nfdopendialogu8args_t args = {};
        args.filterList = &filter;
        args.filterCount = 1;
        args.title = request.title.c_str();
        result = NFD_OpenDialogU8_With(&path, &args);
    }
    std::optional<std::string> picked;
    if (result == NFD_OKAY) {
        picked = path;
        NFD_FreePathU8(path);
    } else if (result == NFD_ERROR) {
        std::fprintf(stderr, "quince: file picker: %s\n", NFD_GetError());
        NFD_ClearError();
    }
    NFD_Quit();
    return picked;
}

void start(Request request, Done done)
{
    if (s_open.exchange(true))
        return;
#if defined(__APPLE__)
    // after the click that asked for it; the panel runs its own modal loop
    slint::invoke_from_event_loop([request = std::move(request), done = std::move(done)] {
        const std::optional<std::string> path = run(request);
        s_open = false;
        if (path)
            done(*path);
    });
#else
    // Detached: an open picker must not hold up quitting. Its result goes
    // through the Slint event loop, which drops it once the loop has ended.
    std::thread([request = std::move(request), done = std::move(done)]() mutable {
        std::optional<std::string> path = run(request);
        s_open = false;
        if (path)
            slint::invoke_from_event_loop([done = std::move(done), path = std::move(*path)] { done(path); });
    }).detach();
#endif
}

} // namespace

void openFile(const std::string &title, const std::string &filterName, const std::string &extension, Done done)
{
    start({ false, title, filterName, extension, {} }, std::move(done));
}

void pickFolder(const std::string &from, Done done)
{
    start({ true, {}, {}, {}, from }, std::move(done));
}

} // namespace filepicker
