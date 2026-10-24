#pragma once

#include "Gl.h"
#include "main.h"

#include <functional>

struct mpv_handle;
struct mpv_render_context;

// Renders mpv's picture into a texture the size of a window's video area
// (the component's video-shown, video-width and video-height: the main
// window's, or the mini player's) and hands it to the component's
// video-frame as a slint::Image, which the scene draws like any picture.
// Slint's rendering notifier: runs on the Slint thread with Slint's GL
// context current, before Slint draws, so a new frame shows at once. The
// render context lives while the area is shown; one mpv instance has one at
// a time, so a window that takes the video over waits for release() of the
// other's.
//
// mpv renders in a GL context of its own (EGL, WGL or CGL) that shares
// objects with Slint's: FemtoVG's GL state broke mpv's rendering of hardware
// decoded frames (broken rows with nvdec and vaapi). Where that context cannot
// be made, mpv renders in Slint's with its GL state saved around it.
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
            view = { w.get_video_shown(), int(w.get_video_width() * scale), int(w.get_video_height() * scale),
                     nullptr };
#if !defined(_WIN32) && !defined(__APPLE__)
            view.waylandDisplay = w.window().wayland_display();
#endif
            return true;
        })
        , m_redraw([component] {
            if (auto c = component.lock())
                (*c)->window().request_redraw();
        })
        // after the frame being drawn: the window is busy while it draws
        , m_setFrame([component](const slint::Image &frame) {
            slint::invoke_from_event_loop([component, frame] {
                if (auto c = component.lock())
                    (*c)->set_video_frame(frame);
            });
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
    // the video area's size in framebuffer pixels
    struct View
    {
        bool shown;
        int w, h;
        void *waylandDisplay;
    };
    struct OwnContext;

    bool ensureContext(const View &view);
    void freeContext();
    // true when the texture has a new size (or is new)
    bool ensureFbo(int w, int h);
    void render();
    void frameStats(bool before);

    std::function<bool(View &)> m_view; // false once the window is gone
    std::function<void()> m_redraw;
    std::function<void(const slint::Image &)> m_setFrame;
    gl::Functions m_gl;
    bool m_glOk = false;
    OwnContext *m_own = nullptr;

    mpv_handle *m_wanted = nullptr;
    mpv_handle *m_current = nullptr;
    mpv_render_context *m_ctx = nullptr;
    bool m_released = false;
    std::function<void()> m_onReleased;
    gl::GLuint m_fbo = 0, m_tex = 0;
    int m_fboW = 0, m_fboH = 0;
};
