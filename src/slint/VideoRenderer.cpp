#include "VideoRenderer.h"

#include <mpv/render_gl.h>

#include <cstdio>

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

// GL state mpv and the blit touch; Slint expects it unchanged afterwards
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

VideoRenderer::VideoRenderer(slint::ComponentWeakHandle<ui::MainWindow> window)
    : m_window(std::move(window))
{
}

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
        if (m_glOk && !m_own)
            m_own = OwnContext::create();
        break;
    case slint::RenderingState::BeforeRendering:
        render();
        break;
    case slint::RenderingState::AfterRendering:
        break;
    case slint::RenderingState::RenderingTeardown:
        freeContext();
        if (m_blitFbo)
            m_gl.DeleteFramebuffers(1, &m_blitFbo);
        m_blitFbo = 0;
        delete m_own;
        m_own = nullptr;
        m_glOk = false;
        break;
    }
}

void VideoRenderer::setSource(mpv_handle *mpv)
{
    m_wanted = mpv;
    if (auto w = m_window.lock())
        (*w)->window().request_redraw();
}

// the video rect in framebuffer pixels, y counted from the bottom
VideoRenderer::Rect VideoRenderer::videoRect(const ui::MainWindow &w) const
{
    const float scale = w.window().scale_factor();
    const auto size = w.window().size();
    const int x = int(w.get_video_x() * scale);
    const int y = int(w.get_video_y() * scale);
    const int vw = int(w.get_video_width() * scale);
    const int vh = int(w.get_video_height() * scale);
    return { x, int(size.height) - y - vh, vw, vh };
}

bool VideoRenderer::ensureContext(const ui::MainWindow &w)
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
#if !defined(_WIN32) && !defined(__APPLE__)
    // VAAPI interop wants the Wayland display
    if (auto *display = w.window().wayland_display())
        params[3] = { MPV_RENDER_PARAM_WL_DISPLAY, display };
#else
    (void)w;
#endif
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
        auto weak = *static_cast<slint::ComponentWeakHandle<ui::MainWindow> *>(data);
        slint::invoke_from_event_loop([weak] {
            if (auto w = weak.lock())
                (*w)->window().request_redraw();
        });
    }, &m_window);
    if (onOutput)
        onOutput(true);
    return true;
}

void VideoRenderer::freeContext()
{
    if (!m_ctx)
        return;
    if (m_own)
        m_own->enter();
    mpv_render_context_free(m_ctx);
    if (m_fbo) {
        m_gl.DeleteFramebuffers(1, &m_fbo);
        m_gl.DeleteTextures(1, &m_tex);
    }
    if (m_own)
        m_own->leave();
    m_ctx = nullptr;
    m_current = nullptr;
    m_fbo = m_tex = 0;
    m_fboW = m_fboH = 0;
    if (onOutput)
        onOutput(false);
}

// in the context current now (mpv's own, or Slint's)
void VideoRenderer::ensureFbo(int w, int h)
{
    if (m_fbo && w == m_fboW && h == m_fboH)
        return;
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
}

void VideoRenderer::renderInto(const Rect &r)
{
    ensureFbo(r.w, r.h);
    mpv_render_context_update(m_ctx);
    mpv_opengl_fbo fbo { GLint(m_fbo), r.w, r.h, 0 };
    int flip = 0;
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
        { MPV_RENDER_PARAM_FLIP_Y, &flip },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    };
    mpv_render_context_render(m_ctx, params);
    if (!m_own) {
        m_gl.BindFramebuffer(READ_FRAMEBUFFER, m_fbo);
        return;
    }
    // Slint's context reads the texture next; FBOs are not shared
    m_gl.Finish();
    m_own->leave();
    if (!m_blitFbo)
        m_gl.GenFramebuffers(1, &m_blitFbo);
    m_gl.BindFramebuffer(READ_FRAMEBUFFER, m_blitFbo);
    m_gl.FramebufferTexture2D(READ_FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_2D, m_tex, 0);
}

void VideoRenderer::render()
{
    if (!m_glOk)
        return;
    auto w = m_window.lock();
    if (!w)
        return;
    const Rect r = videoRect(**w);
    if (!(*w)->get_video_shown() || r.w <= 0 || r.h <= 0 || !ensureContext(**w))
        return;

    SavedState state;
    state.save(m_gl);
    if (m_own)
        m_own->enter();
    renderInto(r);
    m_gl.Disable(SCISSOR_TEST);
    m_gl.BindFramebuffer(DRAW_FRAMEBUFFER, state.drawFbo);
    // mpv renders top-down into the FBO: swapped target rows flip it
    m_gl.BlitFramebuffer(0, 0, r.w, r.h, r.x, r.y + r.h, r.x + r.w, r.y, COLOR_BUFFER_BIT, NEAREST);
    state.restore(m_gl);
    mpv_render_context_report_swap(m_ctx);
}
