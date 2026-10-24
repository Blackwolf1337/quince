#pragma once

// The system's file and folder pickers (nativefiledialog-extended; Slint has
// none): the xdg desktop portal on Linux, IFileOpenDialog on Windows,
// NSOpenPanel on macOS. Called on the Slint thread. The picker runs on a
// thread of its own, so the window keeps drawing; on macOS AppKit wants the
// main thread, so it runs modal there, after the current event. done(path)
// runs on the Slint thread, only when something was picked. One picker at a
// time: a second call while one is open does nothing.

#include <functional>
#include <string>

namespace filepicker {

using Done = std::function<void(const std::string &path)>;

// a file; filterName for files ending in .extension (all files are offered too)
void openFile(const std::string &title, const std::string &filterName, const std::string &extension, Done done);
// a folder, starting in from (may be empty)
void pickFolder(const std::string &from, Done done);

} // namespace filepicker
