#pragma once

#include "Gl.h"
#include "main.h"

#include <functional>

struct mpv_handle;
struct mpv_render_context;

// Draws mpv's picture under the Slint items, into the rect of a window's
// video area (the component's video-shown and video-x/y/width/height: the
// main window's, or the mini player's). Slint's rendering notifier: runs on
// the Slint thread with Slint's GL context current. The render context lives
// while the area is shown; one mpv instance has one at a time, so a window
// that takes the video over waits for release() of the other's.
//
// On Linux mpv renders in an EGL context of its own that shares objects with
// Slint's: FemtoVG's GL state breaks mpv's rendering of hardware decoded
// frames (broken rows with nvdec and vaapi). Elsewhere it still renders in
// Slint's context (WGL/CGL sharing is not written yet).
class VideoRenderer
{
public:
    template<typename Component>
    explicit VideoRenderer(slint::ComponentWeakHandle<Component> component)
        : m_view([component](View &view) {
            auto c = component.lock();
            if (!c)
                return false;
            const Component &w = **c;
            const float scale = w.window().scale_factor();
            view = { w.get_video_shown(), int(w.window().size().height),
                     int(w.get_video_x() * scale), int(w.get_video_y() * scale),
                     int(w.get_video_width() * scale), int(w.get_video_height() * scale), nullptr };
#if !defined(_WIN32) && !defined(__APPLE__)
            view.waylandDisplay = w.window().wayland_display();
#endif
            return true;
        })
        , m_redraw([component] {
            if (auto c = component.lock())
                (*c)->window().request_redraw();
        })
    {
    }
    ~VideoRenderer();

    void operator()(slint::RenderingState state, slint::GraphicsAPI api);

    // the mpv instance to draw from (the player's active deck); the context
    // for it is made on the next frame
    void setSource(mpv_handle *mpv);
    // frees the render context with the next frame (at once when there is
    // none), then runs done() from the event loop; no new one until resume()
    void release(std::function<void()> done);
    void resume();
    // true once a render context exists, false when it goes: the core decodes
    // the picture only while someone draws it
    std::function<void(bool)> onOutput;

private:
    // the video area in framebuffer pixels, y counted from the top
    struct View
    {
        bool shown;
        int windowHeight, x, y, w, h;
        void *waylandDisplay;
    };
    struct Rect
    {
        int x, y, w, h;
    };
    struct OwnContext;

    bool ensureContext(const View &view);
    void freeContext();
    void ensureFbo(int w, int h);
    void render();
    void frameStats(bool before);
    // mpv into the video texture; leaves it bound for reading in Slint's context
    void renderInto(const Rect &r);

    std::function<bool(View &)> m_view; // false once the window is gone
    std::function<void()> m_redraw;
    gl::Functions m_gl;
    bool m_glOk = false;
    OwnContext *m_own = nullptr;

    mpv_handle *m_wanted = nullptr;
    mpv_handle *m_current = nullptr;
    mpv_render_context *m_ctx = nullptr;
    bool m_released = false;
    std::function<void()> m_onReleased;
    gl::GLuint m_fbo = 0, m_tex = 0, m_blitFbo = 0;
    int m_fboW = 0, m_fboH = 0;
};
