#pragma once

#include "Bridge.h"
#include "mini-player.h"

#include <functional>
#include <memory>
#include <optional>

struct mpv_handle;
class VideoRenderer;

// The mini player (ui/mini-player.slint): a second window that takes the
// video over while the main window hides, like the QML frontend's. mpv has
// one render context at a time, so the main window's renderer gives it up
// (VideoRenderer::release) before the mini player's makes its own, and the
// other way round. Slint thread only; made on the first open, gone with
// destroy() before the core stops.
class MiniPlayerWindow : public std::enable_shared_from_this<MiniPlayerWindow>
{
public:
    MiniPlayerWindow(bridge::Window main, VideoRenderer *mainVideo);
    ~MiniPlayerWindow();

    void open();
    // back to the main window
    void close();
    void destroy();

    // what it shows, from the main window's Now Playing view
    void setState(const ui::MiniState &state);
    void setSource(mpv_handle *mpv);
    // the width it was left at (logical pixels)
    void setWidth(int width) { m_width = width; }
    // the user resized it: the width to keep
    std::function<void(int)> onWidthChosen;

private:
    void show();
    void apply();
    // its height for the width: wide for a video, square for the cover
    float heightFor(float width) const;

    bridge::Window m_main;
    VideoRenderer *m_mainVideo;
    std::optional<slint::ComponentHandle<mini::MiniPlayer>> m_window;
    std::unique_ptr<VideoRenderer> m_video;
    mpv_handle *m_source = nullptr;
    ui::MiniState m_state;
    int m_width = 340;
    bool m_open = false;
};
