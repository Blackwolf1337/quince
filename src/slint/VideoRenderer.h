#pragma once

#include "Gl.h"
#include "main.h"

#include <functional>

struct mpv_handle;
struct mpv_render_context;

// Draws mpv's picture under the Slint items, into the rect of the window's
// video area (MainWindow.video-*). Slint's rendering notifier: runs on the
// Slint thread with Slint's GL context current.
//
// On Linux mpv renders in an EGL context of its own that shares objects with
// Slint's: FemtoVG's GL state breaks mpv's rendering of hardware decoded
// frames (broken rows with nvdec and vaapi). Elsewhere it still renders in
// Slint's context (WGL/CGL sharing is not written yet).
class VideoRenderer
{
public:
    explicit VideoRenderer(slint::ComponentWeakHandle<MainWindow> window);
    ~VideoRenderer();

    void operator()(slint::RenderingState state, slint::GraphicsAPI api);

    // the mpv instance to draw from (the player's active deck); the context
    // for it is made on the next frame
    void setSource(mpv_handle *mpv);
    // true once a render context exists, false when it goes: the core decodes
    // the picture only while someone draws it
    std::function<void(bool)> onOutput;

private:
    struct Rect
    {
        int x, y, w, h;
    };
    struct OwnContext;

    Rect videoRect(const MainWindow &w) const;
    bool ensureContext(const MainWindow &w);
    void freeContext();
    void ensureFbo(int w, int h);
    void render();
    // mpv into the video texture; leaves it bound for reading in Slint's context
    void renderInto(const Rect &r);

    slint::ComponentWeakHandle<MainWindow> m_window;
    gl::Functions m_gl;
    bool m_glOk = false;
    OwnContext *m_own = nullptr;

    mpv_handle *m_wanted = nullptr;
    mpv_handle *m_current = nullptr;
    mpv_render_context *m_ctx = nullptr;
    gl::GLuint m_fbo = 0, m_tex = 0, m_blitFbo = 0;
    int m_fboW = 0, m_fboH = 0;
};
