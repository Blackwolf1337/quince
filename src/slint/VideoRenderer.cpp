#include "VideoRenderer.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <EGL/egl.h>
#define QUINCE_OWN_EGL 1
#endif

using namespace gl;

namespace {

void *mpvProcAddress(void *, const char *name)
{
    return procAddress(name);
}

// GL state mpv touches when it renders in Slint's context (no context of its
// own); Slint expects it unchanged afterwards
struct SavedState
{
    GLint drawFbo = 0, readFbo = 0, activeTexture = 0, texture = 0;
    GLboolean scissor = 0;

    void save(const Functions &f)
    {
        f.GetIntegerv(DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        f.GetIntegerv(READ_FRAMEBUFFER_BINDING, &readFbo);
        f.GetIntegerv(ACTIVE_TEXTURE, &activeTexture);
        f.ActiveTexture(TEXTURE0);
        f.GetIntegerv(TEXTURE_BINDING_2D, &texture);
        scissor = f.IsEnabled(SCISSOR_TEST);
    }
    void restore(const Functions &f) const
    {
        f.BindFramebuffer(DRAW_FRAMEBUFFER, drawFbo);
        f.BindFramebuffer(READ_FRAMEBUFFER, readFbo);
        f.ActiveTexture(TEXTURE0);
        f.BindTexture(TEXTURE_2D, texture);
        f.ActiveTexture(activeTexture);
        scissor ? f.Enable(SCISSOR_TEST) : f.Disable(SCISSOR_TEST);
    }
};

} // namespace

namespace {

#ifdef QUINCE_OWN_EGL
// Slint swaps with interval 1. On Wayland winit already waits for the
// compositor's frame callback before a frame, so a blocking swap waits for a
// second one: half the display's rate (72 fps at 144 Hz). A hidden window gets
// no frame callbacks, and a blocking swap then hangs the main thread until the
// compositor reports it as not responding. Slint's context is current here.
// QUINCE_SWAP=1 keeps the blocking swap (for comparisons).
void unblockSwap()
{
    if (std::getenv("QUINCE_SWAP"))
        return;
    EGLDisplay dpy = eglGetCurrentDisplay();
    if (dpy != EGL_NO_DISPLAY && !eglSwapInterval(dpy, 0))
        std::fprintf(stderr, "quince: eglSwapInterval(0) failed\n");
}
#else
void unblockSwap() {}
#endif

} // namespace

#ifdef QUINCE_OWN_EGL
// mpv's EGL context, sharing objects (the video texture) with Slint's
struct VideoRenderer::OwnContext
{
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLContext ctx = EGL_NO_CONTEXT, slintCtx = EGL_NO_CONTEXT;
    EGLSurface slintDraw = EGL_NO_SURFACE, slintRead = EGL_NO_SURFACE;

    static OwnContext *create()
    {
        auto *own = new OwnContext;
        own->dpy = eglGetCurrentDisplay();
        own->slintCtx = eglGetCurrentContext();
        EGLint cfgId = 0, clientVersion = 2;
        eglQueryContext(own->dpy, own->slintCtx, EGL_CONFIG_ID, &cfgId);
        eglQueryContext(own->dpy, own->slintCtx, EGL_CONTEXT_CLIENT_VERSION, &clientVersion);
        const EGLint cfgAttr[] = { EGL_CONFIG_ID, cfgId, EGL_NONE };
        EGLConfig cfg;
        EGLint n = 0;
        eglChooseConfig(own->dpy, cfgAttr, &cfg, 1, &n);
        const EGLint attr[] = { EGL_CONTEXT_CLIENT_VERSION, clientVersion, EGL_NONE };
        if (n)
            own->ctx = eglCreateContext(own->dpy, cfg, own->slintCtx, attr);
        if (own->ctx == EGL_NO_CONTEXT) {
            std::fprintf(stderr, "quince: no EGL context for mpv, it shares Slint's\n");
            delete own;
            return nullptr;
        }
        return own;
    }
    ~OwnContext()
    {
        if (ctx != EGL_NO_CONTEXT)
            eglDestroyContext(dpy, ctx);
    }
    void enter()
    {
        slintDraw = eglGetCurrentSurface(EGL_DRAW);
        slintRead = eglGetCurrentSurface(EGL_READ);
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    }
    void leave() { eglMakeCurrent(dpy, slintDraw, slintRead, slintCtx); }
};
#else
struct VideoRenderer::OwnContext
{
    static OwnContext *create() { return nullptr; }
    void enter() {}
    void leave() {}
};
#endif

VideoRenderer::~VideoRenderer()
{
    delete m_own;
}

void VideoRenderer::operator()(slint::RenderingState state, slint::GraphicsAPI api)
{
    switch (state) {
    case slint::RenderingState::RenderingSetup:
        if (api != slint::GraphicsAPI::NativeOpenGL) {
            std::fprintf(stderr, "quince: Slint does not render with OpenGL, no video\n");
            return;
        }
        m_glOk = load(m_gl);
        unblockSwap();
        // QUINCE_SHARED_GL=1: mpv in Slint's context, as without EGL (comparisons)
        if (m_glOk && !m_own && !std::getenv("QUINCE_SHARED_GL"))
            m_own = OwnContext::create();
        break;
    case slint::RenderingState::BeforeRendering:
        frameStats(true);
        render();
        break;
    case slint::RenderingState::AfterRendering:
        frameStats(false);
        break;
    case slint::RenderingState::RenderingTeardown:
        freeContext();
        delete m_own;
        m_own = nullptr;
        m_glOk = false;
        break;
    }
}

// QUINCE_FPS=1: frames per second and the time Slint draws one, once a second
void VideoRenderer::frameStats(bool before)
{
    static const bool on = std::getenv("QUINCE_FPS") != nullptr;
    if (!on)
        return;
    using Clock = std::chrono::steady_clock;
    static Clock::time_point second = Clock::now(), start;
    static int frames = 0;
    static double drawMs = 0;
    const auto now = Clock::now();
    if (before) {
        start = now;
        return;
    }
    frames++;
    drawMs += std::chrono::duration<double, std::milli>(now - start).count();
    if (now - second >= std::chrono::seconds(1)) {
        std::fprintf(stderr, "quince: %d fps, %.1f ms per frame\n", frames, drawMs / frames);
        frames = 0;
        drawMs = 0;
        second = now;
    }
}

void VideoRenderer::setSource(mpv_handle *mpv)
{
    m_wanted = mpv;
    m_redraw();
}

void VideoRenderer::release(std::function<void()> done)
{
    m_released = true;
    if (!m_ctx) {
        slint::invoke_from_event_loop(std::move(done));
        return;
    }
    m_onReleased = std::move(done);
    m_redraw();
}

void VideoRenderer::resume()
{
    m_released = false;
    m_onReleased = nullptr;
    m_redraw();
}

bool VideoRenderer::ensureContext(const View &view)
{
    if (m_current != m_wanted)
        freeContext();
    if (m_ctx || !m_wanted)
        return m_ctx != nullptr;

    mpv_opengl_init_params init { mpvProcAddress, nullptr };
    int advanced = 1;
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL) },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init },
        { MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced },
        { MPV_RENDER_PARAM_INVALID, nullptr },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    };
    // VAAPI interop wants the Wayland display
    if (view.waylandDisplay)
        params[3] = { MPV_RENDER_PARAM_WL_DISPLAY, view.waylandDisplay };
    // what mpv does not draw stays transparent: the visualizer lies over the
    // backdrop; the scene puts black behind a video (letterbox bars)
    mpv_set_property_string(m_wanted, "background", "none");
    if (m_own)
        m_own->enter();
    const int created = mpv_render_context_create(&m_ctx, m_wanted, params);
    if (m_own)
        m_own->leave();
    if (created < 0) {
        std::fprintf(stderr, "quince: mpv_render_context_create failed\n");
        m_ctx = nullptr;
        return false;
    }
    m_current = m_wanted;
    // mpv calls this from its own threads: a redraw on the Slint thread
    mpv_render_context_set_update_callback(m_ctx, [](void *data) {
        slint::invoke_from_event_loop(*static_cast<const std::function<void()> *>(data));
    }, &m_redraw);
    if (onOutput)
        onOutput(true);
    return true;
}

void VideoRenderer::freeContext()
{
    if (!m_ctx)
        return;
    m_setFrame({});
    if (m_own)
        m_own->enter();
    mpv_render_context_free(m_ctx);
    if (m_fbo) {
        m_gl.DeleteFramebuffers(1, &m_fbo);
        m_gl.DeleteTextures(1, &m_tex);
    }
    m_tex = 0;
    if (m_own)
        m_own->leave();
    m_ctx = nullptr;
    m_current = nullptr;
    m_fbo = 0;
    m_fboW = m_fboH = 0;
    if (onOutput)
        onOutput(false);
}

// in the context current now (mpv's own, or Slint's). One texture for the
// context's life: another size gets new storage under the same id, and the
// scene a new image of it (deleting and making textures while the slot grows
// left Slint drawing a texture that was gone)
bool VideoRenderer::ensureFbo(int w, int h)
{
    if (m_fbo && w == m_fboW && h == m_fboH)
        return false;
    if (!m_fbo) {
        m_gl.GenFramebuffers(1, &m_fbo);
        m_gl.GenTextures(1, &m_tex);
    }
    m_gl.BindTexture(TEXTURE_2D, m_tex);
    m_gl.TexImage2D(TEXTURE_2D, 0, RGBA, w, h, 0, RGBA, UNSIGNED_BYTE, nullptr);
    m_gl.TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, LINEAR);
    m_gl.TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, LINEAR);
    m_gl.BindFramebuffer(FRAMEBUFFER, m_fbo);
    m_gl.FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_2D, m_tex, 0);
    if (m_gl.CheckFramebufferStatus(FRAMEBUFFER) != FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "quince: video framebuffer incomplete\n");
    m_fboW = w;
    m_fboH = h;
    return true;
}

void VideoRenderer::render()
{
    View view;
    if (!m_glOk || !m_view(view))
        return;
    if (m_released || !view.shown || view.w <= 0 || view.h <= 0) {
        // the core decodes the picture only while it is drawn
        freeContext();
        if (m_released && m_onReleased) {
            slint::invoke_from_event_loop(std::move(m_onReleased));
            m_onReleased = nullptr;
        }
        return;
    }
    if (!ensureContext(view))
        return;

    SavedState state;
    if (m_own)
        m_own->enter();
    else
        state.save(m_gl);
    const bool fresh = ensureFbo(view.w, view.h);
    if ((mpv_render_context_update(m_ctx) & MPV_RENDER_UPDATE_FRAME) || fresh) {
        mpv_opengl_fbo fbo { GLint(m_fbo), view.w, view.h, 0 };
        int flip = 0;
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
            { MPV_RENDER_PARAM_FLIP_Y, &flip },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };
        mpv_render_context_render(m_ctx, params);
    }
    if (m_own) {
        // Slint samples the texture right after, in its own context
        m_gl.Finish();
        m_own->leave();
    } else {
        state.restore(m_gl);
    }
    if (fresh) {
        // mpv renders top-down, the image's default origin
        // shows from the next frame on (setting it redraws)
        m_setFrame(slint::Image::create_from_borrowed_gl_2d_rgba_texture(
            m_tex, { uint32_t(view.w), uint32_t(view.h) }));
    }
    mpv_render_context_report_swap(m_ctx);
}
