// Phase 0 spike: Slint UI on the main thread, Qt core event loop on a second
// thread, mpv video drawn under the Slint items in a sub-rect of the window.
//
// usage: quince-spike <file or URL> [--check] [--ao=null]
//   --check: after 4 s of playback read back pixels from the video rect and
//            from the overlay, print them, then quit
//   --hwdec=<mode>: mpv hwdec (default auto-safe)
//   SPIKE_NOADV=1: render context without advanced control
//   SPIKE_FINISH=1: glFinish between mpv's render and the blit
//   SPIKE_RESET=1 / SPIKE_RESET_SAMPLERS=1: reset GL state before mpv renders
//   SPIKE_SHAREDCTX=1: mpv renders in Slint's EGL context instead of its own
//   SPIKE_NOQT=1, SPIKE_NOLOOP=1, SPIKE_VIDNO=1, SPIKE_MPV="k=v ...": bisect switches

#include "ui.h"

#include <QCoreApplication>
#include <QThread>
#include <QTimer>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <atomic>
#include <chrono>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#define GLAPI_CALL __stdcall
#else
#include <dlfcn.h>
#define GLAPI_CALL
#endif
#if !defined(_WIN32) && !defined(__APPLE__)
#include <EGL/egl.h>
#define SPIKE_EGL 1
#endif

// GL types and enums used here, so no GL headers are needed (mingw ships
// only GL 1.1, macOS deprecates them)
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLbitfield = unsigned int;
using GLboolean = unsigned char;
using GLubyte = unsigned char;

constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum GL_DRAW_FRAMEBUFFER_BINDING = 0x8CA6;
constexpr GLenum GL_READ_FRAMEBUFFER_BINDING = 0x8CAA;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum GL_VIEWPORT = 0x0BA2;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_SCISSOR_BOX = 0x0C10;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_TEXTURE_BINDING_2D = 0x8069;
constexpr GLenum GL_ACTIVE_TEXTURE = 0x84E0;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_CURRENT_PROGRAM = 0x8B8D;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_ARRAY_BUFFER_BINDING = 0x8894;
constexpr GLenum GL_VERTEX_ARRAY_BINDING = 0x85B5;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05;
constexpr GLenum GL_VERSION = 0x1F02;
constexpr GLenum GL_RENDERER = 0x1F01;
constexpr GLenum GL_PIXEL_PACK_BUFFER = 0x88EB;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER = 0x88EC;
constexpr GLenum GL_UNPACK_ROW_LENGTH = 0x0CF2;
constexpr GLenum GL_UNPACK_SKIP_ROWS = 0x0CF3;
constexpr GLenum GL_UNPACK_SKIP_PIXELS = 0x0CF4;
constexpr GLenum GL_STENCIL_TEST = 0x0B90;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_CULL_FACE = 0x0B44;

static void *glProc(const char *name);

// the GL entry points the renderer needs, loaded once the context is current
struct Gl
{
#define GL_FN(ret, name, ...) ret(GLAPI_CALL *name)(__VA_ARGS__) = nullptr;
#include "glfuncs.inc"
#undef GL_FN
    bool load()
    {
        bool ok = true;
#define GL_FN(ret, name, ...)                                                         \
    name = reinterpret_cast<decltype(name)>(glProc("gl" #name));                        \
    if (!name) {                                                                      \
        std::fprintf(stderr, "spike: missing gl" #name "\n");                          \
        ok = false;                                                                   \
    }
#include "glfuncs.inc"
#undef GL_FN
        return ok;
    }
};

static Gl gl;

#if defined(_WIN32)
static void *glProc(const char *name)
{
    void *p = reinterpret_cast<void *>(wglGetProcAddress(name));
    // wglGetProcAddress returns small sentinels for GL 1.1 entry points
    auto v = reinterpret_cast<intptr_t>(p);
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1)
        p = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("opengl32.dll"), name));
    return p;
}
#elif defined(__APPLE__)
static void *glProc(const char *name)
{
    return dlsym(RTLD_DEFAULT, name);
}
#else
// Wayland and most X11 setups get an EGL context from Slint, some X11 setups GLX
static void *glProc(const char *name)
{
    using GetProc = void *(*)(const char *);
    using GetCurrent = void *(*)();
    static void *egl = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    static void *glx = dlopen("libGLX.so.0", RTLD_LAZY | RTLD_LOCAL);
    static auto eglGetProcAddress = egl ? reinterpret_cast<GetProc>(dlsym(egl, "eglGetProcAddress")) : nullptr;
    static auto eglGetCurrentContext = egl ? reinterpret_cast<GetCurrent>(dlsym(egl, "eglGetCurrentContext")) : nullptr;
    static auto glXGetProcAddressARB = glx ? reinterpret_cast<GetProc>(dlsym(glx, "glXGetProcAddressARB")) : nullptr;
    if (eglGetProcAddress && eglGetCurrentContext && eglGetCurrentContext())
        return eglGetProcAddress(name);
    if (glXGetProcAddressARB)
        return glXGetProcAddressARB(name);
    return nullptr;
}
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
// Slint asks for a blocking swap (interval 1). A Wayland window that gets no
// frame callbacks (other workspace, minimized) then blocks the main thread in
// eglSwapBuffers and the compositor reports it as not responding. Slint's own
// frame throttle paces redraws by timer, so the swap need not wait.
static void disableBlockingSwap()
{
    using GetCurrent = void *(*)();
    using SwapInterval = unsigned (*)(void *, int);
    void *egl = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!egl)
        return;
    auto getDisplay = reinterpret_cast<GetCurrent>(dlsym(egl, "eglGetCurrentDisplay"));
    auto swapInterval = reinterpret_cast<SwapInterval>(dlsym(egl, "eglSwapInterval"));
    if (getDisplay && swapInterval && getDisplay())
        std::fprintf(stderr, "spike: eglSwapInterval(0) %s\n", swapInterval(getDisplay(), 0) ? "ok" : "failed");
}
#else
static void disableBlockingSwap() { }
#endif

static mpv_handle *g_mpv = nullptr;

static void *mpvGlProc(void *, const char *name)
{
    return glProc(name);
}

struct Options
{
    std::string media;
    std::string ao;
    std::string hwdec = "auto-safe";
    bool check = false;
};

// GL state that mpv and the blit touch; Slint expects it unchanged afterwards
struct GlState
{
    GLint drawFbo = 0, readFbo = 0, viewport[4] = {}, scissor[4] = {}, program = 0;
    GLint arrayBuffer = 0, vao = 0, activeTexture = 0, textures[4] = {};
    GLint unpackAlign = 4, packAlign = 4;
    GLboolean scissorTest = 0, blend = 0;

    void save(bool hasVao)
    {
        gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        gl.GetIntegerv(GL_VIEWPORT, viewport);
        gl.GetIntegerv(GL_SCISSOR_BOX, scissor);
        gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
        gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        if (hasVao)
            gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        gl.GetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        for (int i = 0; i < 4; i++) {
            gl.ActiveTexture(GL_TEXTURE0 + i);
            gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
        }
        gl.GetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlign);
        gl.GetIntegerv(GL_PACK_ALIGNMENT, &packAlign);
        scissorTest = gl.IsEnabled(GL_SCISSOR_TEST);
        blend = gl.IsEnabled(GL_BLEND);
    }

    void restore(bool hasVao)
    {
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
        gl.Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        gl.Scissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        gl.UseProgram(program);
        gl.BindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        if (hasVao)
            gl.BindVertexArray(vao);
        for (int i = 0; i < 4; i++) {
            gl.ActiveTexture(GL_TEXTURE0 + i);
            gl.BindTexture(GL_TEXTURE_2D, textures[i]);
        }
        gl.ActiveTexture(activeTexture);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpackAlign);
        gl.PixelStorei(GL_PACK_ALIGNMENT, packAlign);
        scissorTest ? gl.Enable(GL_SCISSOR_TEST) : gl.Disable(GL_SCISSOR_TEST);
        blend ? gl.Enable(GL_BLEND) : gl.Disable(GL_BLEND);
    }
};

#ifdef SPIKE_EGL
// mpv's own GL context, sharing objects with Slint's: FemtoVG's GL state can
// then not leak into mpv's rendering (and the reverse)
struct OwnContext
{
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLContext ctx = EGL_NO_CONTEXT, slintCtx = EGL_NO_CONTEXT;
    EGLSurface slintDraw = EGL_NO_SURFACE, slintRead = EGL_NO_SURFACE;

    bool create()
    {
        dpy = eglGetCurrentDisplay();
        slintCtx = eglGetCurrentContext();
        EGLint cfgId = 0, clientVersion = 2;
        eglQueryContext(dpy, slintCtx, EGL_CONFIG_ID, &cfgId);
        eglQueryContext(dpy, slintCtx, EGL_CONTEXT_CLIENT_VERSION, &clientVersion);
        EGLint cfgAttr[] = { EGL_CONFIG_ID, cfgId, EGL_NONE };
        EGLConfig cfg;
        EGLint n = 0;
        eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n);
        EGLint attr[] = { EGL_CONTEXT_CLIENT_VERSION, clientVersion, EGL_NONE };
        ctx = n ? eglCreateContext(dpy, cfg, slintCtx, attr) : EGL_NO_CONTEXT;
        std::fprintf(stderr, "spike: own mpv context %s\n", ctx != EGL_NO_CONTEXT ? "created" : "failed");
        return ctx != EGL_NO_CONTEXT;
    }
    void enter()
    {
        slintDraw = eglGetCurrentSurface(EGL_DRAW);
        slintRead = eglGetCurrentSurface(EGL_READ);
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    }
    void leave() { eglMakeCurrent(dpy, slintDraw, slintRead, slintCtx); }
    void destroy()
    {
        if (ctx != EGL_NO_CONTEXT)
            eglDestroyContext(dpy, ctx);
        ctx = EGL_NO_CONTEXT;
    }
};
#endif

// Draws mpv into an FBO the size of the video rect, then blits it into the
// window's framebuffer at that rect. Runs on the Slint thread inside the
// rendering notifier, with Slint's GL context current.
class VideoRenderer
{
public:
    VideoRenderer(slint::ComponentWeakHandle<SpikeWindow> win, mpv_handle **mpv, bool check)
        : m_win(win), m_mpv(mpv), m_check(check)
    {
    }

    void operator()(slint::RenderingState state, slint::GraphicsAPI api)
    {
        switch (state) {
        case slint::RenderingState::RenderingSetup:
            if (api != slint::GraphicsAPI::NativeOpenGL) {
                std::fprintf(stderr, "spike: renderer is not OpenGL, no video\n");
                return;
            }
            m_glOk = gl.load();
            disableBlockingSwap();
#if !defined(_WIN32) && !defined(__APPLE__)
            if (auto w = m_win.lock())
                std::fprintf(stderr, "spike: windowing %s\n", (*w)->window().wayland_display() ? "wayland" : "x11");
#endif
            if (m_glOk) {
                std::fprintf(stderr, "spike: GL %s on %s\n",
                             reinterpret_cast<const char *>(gl.GetString(GL_VERSION)),
                             reinterpret_cast<const char *>(gl.GetString(GL_RENDERER)));
                const char *v = reinterpret_cast<const char *>(gl.GetString(GL_VERSION));
                m_hasVao = v && (std::strstr(v, "OpenGL ES 2") == nullptr);
            }
            break;
        case slint::RenderingState::BeforeRendering:
            render();
            break;
        case slint::RenderingState::AfterRendering:
            if (m_check && m_readBack)
                readBack();
            break;
        case slint::RenderingState::RenderingTeardown:
            teardown();
            break;
        }
    }

    void requestReadBack() { m_readBack = true; }
    // runs once the render context exists (the file is loaded then)
    std::function<void()> m_onContext;

private:
    struct Rect
    {
        int x, y, w, h, fbH;
    };

    // the video rect in framebuffer pixels, y counted from the bottom
    Rect videoRect(const SpikeWindow &w) const
    {
        float scale = w.window().scale_factor();
        auto size = w.window().size();
        int x = int(w.get_video_x() * scale);
        int y = int(w.get_video_y() * scale);
        int vw = int(w.get_video_w() * scale);
        int vh = int(w.get_video_h() * scale);
        return { x, int(size.height) - y - vh, vw, vh, int(size.height) };
    }

    bool ensureContext(const SpikeWindow &w)
    {
        if (m_ctx)
            return true;
        if (!*m_mpv)
            return false;
        mpv_opengl_init_params init { mpvGlProc, nullptr };
        int advanced = std::getenv("SPIKE_NOADV") ? 0 : 1;
        mpv_render_param params[4] = {
            { MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL) },
            { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init },
            { MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };
#if !defined(_WIN32) && !defined(__APPLE__)
        // VAAPI interop wants the Wayland display
        if (auto *display = w.window().wayland_display())
            params[3] = { MPV_RENDER_PARAM_WL_DISPLAY, display };
#else
        (void)w;
#endif
#ifdef SPIKE_EGL
        // FemtoVG's GL state breaks mpv's rendering of hardware frames (broken
        // rows with nvdec and vaapi on NVIDIA), resetting single states is not
        // enough: mpv gets its own context unless SPIKE_SHAREDCTX is set
        m_own = !std::getenv("SPIKE_SHAREDCTX") && m_ownCtx.create();
        if (m_own)
            m_ownCtx.enter();
#endif
        int created = mpv_render_context_create(&m_ctx, *m_mpv, params);
#ifdef SPIKE_EGL
        if (m_own)
            m_ownCtx.leave();
#endif
        if (created < 0) {
            std::fprintf(stderr, "spike: mpv_render_context_create failed\n");
            m_ctx = nullptr;
            return false;
        }
        // mpv calls this from its own threads: hop to the Slint thread
        mpv_render_context_set_update_callback(m_ctx, [](void *data) {
            auto *weak = static_cast<slint::ComponentWeakHandle<SpikeWindow> *>(data);
            slint::invoke_from_event_loop([weak = *weak] {
                if (auto w = weak.lock())
                    (*w)->window().request_redraw();
            });
        }, &m_win);
        if (std::getenv("SPIKE_VIDNO")) {
            // video output only once a render context exists, like MpvVideoItem
            mpv_set_property_string(*m_mpv, "vid", "auto");
        } else if (m_onContext) {
            m_onContext();
        }
        return true;
    }

    void ensureFbo(int w, int h)
    {
        if (m_fbo && w == m_fboW && h == m_fboH)
            return;
        if (!m_fbo) {
            gl.GenFramebuffers(1, &m_fbo);
            gl.GenTextures(1, &m_tex);
        }
        gl.BindTexture(GL_TEXTURE_2D, m_tex);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "spike: video FBO incomplete\n");
        m_fboW = w;
        m_fboH = h;
    }

    void render()
    {
        if (!m_glOk)
            return;
        auto win = m_win.lock();
        if (!win || !ensureContext(**win))
            return;
        Rect r = videoRect(**win);
        if (r.w <= 0 || r.h <= 0)
            return;

        GlState state;
        state.save(m_hasVao);
#ifdef SPIKE_EGL
        if (m_own) {
            m_ownCtx.enter();
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
            // the texture is read in Slint's context next: finish it here
            gl.Finish();
            m_ownCtx.leave();
            // FBOs are not shared: Slint's context reads the texture through its own
            if (!m_blitFbo)
                gl.GenFramebuffers(1, &m_blitFbo);
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER, m_blitFbo);
            gl.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
            gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, state.drawFbo);
            gl.Disable(GL_SCISSOR_TEST);
            gl.BlitFramebuffer(0, 0, r.w, r.h, r.x, r.y + r.h, r.x + r.w, r.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            state.restore(m_hasVao);
            mpv_render_context_report_swap(m_ctx);
            return;
        }
#endif
        ensureFbo(r.w, r.h);
        if (std::getenv("SPIKE_RESET")) {
            // a clean slate for mpv: FemtoVG's state stays bound otherwise
            gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            gl.BindBuffer(GL_ARRAY_BUFFER, 0);
            if (m_hasVao)
                gl.BindVertexArray(0);
            gl.UseProgram(0);
            gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            gl.PixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            gl.PixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
            if (std::getenv("SPIKE_RESET_SAMPLERS")) {
                for (int i = 0; i < 16; i++) {
                    gl.ActiveTexture(GL_TEXTURE0 + i);
                    gl.BindTexture(GL_TEXTURE_2D, 0);
                    gl.BindSampler(i, 0);
                }
            }
            gl.ActiveTexture(GL_TEXTURE0);
            gl.Disable(GL_BLEND);
            gl.Disable(GL_SCISSOR_TEST);
            gl.Disable(GL_STENCIL_TEST);
            gl.Disable(GL_DEPTH_TEST);
            gl.Disable(GL_CULL_FACE);
            gl.ColorMask(1, 1, 1, 1);
        }
        mpv_render_context_update(m_ctx);
        mpv_opengl_fbo fbo { GLint(m_fbo), r.w, r.h, 0 };
        int flip = 0;
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
            { MPV_RENDER_PARAM_FLIP_Y, &flip },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };
        mpv_render_context_render(m_ctx, params);

        if (std::getenv("SPIKE_FINISH"))
            gl.Finish();
        gl.Disable(GL_SCISSOR_TEST);
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, state.drawFbo);
        // mpv renders top-down into the FBO: swap the target rows to flip it
        gl.BlitFramebuffer(0, 0, r.w, r.h, r.x, r.y + r.h, r.x + r.w, r.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        state.restore(m_hasVao);
        mpv_render_context_report_swap(m_ctx);

        if (!m_firstFrame) {
            m_firstFrame = true;
            std::fprintf(stderr, "spike: first video frame drawn (%dx%d at %d,%d)\n", r.w, r.h, r.x, r.y);
        }
    }

    // center of the video, and a point inside the translucent overlay bar
    void readBack()
    {
        m_readBack = false;
        auto win = m_win.lock();
        if (!win)
            return;
        Rect r = videoRect(**win);
        float scale = (*win)->window().scale_factor();
        GLubyte center[4] = {}, overlay[4] = {}, sidebar[4] = {};
        gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl.ReadPixels(r.x + r.w / 2, r.y + r.h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
        gl.ReadPixels(r.x + r.w - int(40 * scale), r.y + int(30 * scale), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, overlay);
        gl.ReadPixels(int(100 * scale), r.fbH - int(300 * scale), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sidebar);
        // whole frame as PPM (bottom-up rows) for a look by eye
        auto size = (*win)->window().size();
        std::vector<GLubyte> frame(size_t(size.width) * size.height * 4);
        gl.ReadPixels(0, 0, size.width, size.height, GL_RGBA, GL_UNSIGNED_BYTE, frame.data());
        if (FILE *f = std::fopen("spike-frame.ppm", "wb")) {
            std::fprintf(f, "P6\n%u %u\n255\n", size.width, size.height);
            for (int y = int(size.height) - 1; y >= 0; y--)
                for (unsigned x = 0; x < size.width; x++)
                    std::fwrite(&frame[(size_t(y) * size.width + x) * 4], 1, 3, f);
            std::fclose(f);
        }
        gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
        std::printf("check: video center rgb %d %d %d\n", center[0], center[1], center[2]);
        std::printf("check: overlay bar rgb %d %d %d\n", overlay[0], overlay[1], overlay[2]);
        std::printf("check: sidebar rgb %d %d %d\n", sidebar[0], sidebar[1], sidebar[2]);
        std::fflush(stdout);
        slint::quit_event_loop();
    }

    void teardown()
    {
#ifdef SPIKE_EGL
        if (m_own) {
            if (m_blitFbo)
                gl.DeleteFramebuffers(1, &m_blitFbo);
            m_blitFbo = 0;
            m_ownCtx.enter();
        }
#endif
        if (m_ctx)
            mpv_render_context_free(m_ctx);
        m_ctx = nullptr;
        if (m_fbo) {
            gl.DeleteFramebuffers(1, &m_fbo);
            gl.DeleteTextures(1, &m_tex);
        }
        m_fbo = m_tex = 0;
#ifdef SPIKE_EGL
        if (m_own) {
            m_ownCtx.leave();
            m_ownCtx.destroy();
            m_own = false;
        }
#endif
    }

    slint::ComponentWeakHandle<SpikeWindow> m_win;
    mpv_handle **m_mpv;
    mpv_render_context *m_ctx = nullptr;
    GLuint m_fbo = 0, m_tex = 0, m_blitFbo = 0;
    int m_fboW = 0, m_fboH = 0;
    bool m_own = false;
#ifdef SPIKE_EGL
    OwnContext m_ownCtx;
#endif
    bool m_glOk = false, m_hasVao = false, m_check = false;
    bool m_readBack = false, m_firstFrame = false;
};

static Options parseArgs(int argc, char **argv)
{
    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--check")
            o.check = true;
        else if (a.rfind("--hwdec=", 0) == 0)
            o.hwdec = a.substr(8);
        else if (a.rfind("--ao=", 0) == 0)
            o.ao = a.substr(5);
        else
            o.media = a;
    }
    return o;
}

// drains mpv events on the Slint thread and mirrors a few properties
static void processMpvEvents(mpv_handle *mpv, const slint::ComponentHandle<SpikeWindow> &ui)
{
    for (;;) {
        mpv_event *ev = mpv_wait_event(mpv, 0);
        if (ev->event_id == MPV_EVENT_NONE)
            break;
        if (ev->event_id != MPV_EVENT_PROPERTY_CHANGE)
            continue;
        auto *prop = static_cast<mpv_event_property *>(ev->data);
        if (!prop->data)
            continue;
        if (std::strcmp(prop->name, "hwdec-current") == 0 && prop->format == MPV_FORMAT_STRING)
            std::fprintf(stderr, "spike: hwdec-current %s\n", *static_cast<char **>(prop->data));
        else if (std::strcmp(prop->name, "media-title") == 0 && prop->format == MPV_FORMAT_STRING)
            ui->set_media_title(*static_cast<char **>(prop->data));
        else if (std::strcmp(prop->name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG)
            ui->set_paused(*static_cast<int *>(prop->data) != 0);
        else if (std::strcmp(prop->name, "percent-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE)
            ui->set_position(float(*static_cast<double *>(prop->data) / 100.0));
    }
}

int main(int argc, char **argv)
{
    auto t0 = std::chrono::steady_clock::now();
    Options opts = parseArgs(argc, argv);

    // destroyed before mpv: that runs RenderingTeardown with the GL context
    // current, which frees the render context (hide() alone does not on Windows)
    std::optional<slint::ComponentHandle<SpikeWindow>> uiHolder(SpikeWindow::create());
    auto &ui = *uiHolder;
    auto tray = SpikeTray::create();
    mpv_handle *mpv = nullptr;
    VideoRenderer *renderer = nullptr;

    // the Qt thread owns QCoreApplication and its event loop; Slint keeps the
    // main thread because winit needs it on macOS
    std::atomic<QCoreApplication *> qtApp = nullptr;
    std::function<void()> onQtReady;
    std::thread qtThread([&qtApp, &onQtReady, argv, weak = slint::ComponentWeakHandle(ui)] {
        if (std::getenv("SPIKE_NOQT")) {
            slint::invoke_from_event_loop([&onQtReady] { onQtReady(); });
            return;
        }
        int qargc = 1;
        QCoreApplication app(qargc, argv);
        qtApp = &app;
        slint::invoke_from_event_loop([&onQtReady] { onQtReady(); });
        QTimer ticks;
        int count = 0;
        QObject::connect(&ticks, &QTimer::timeout, [&count, weak] {
            int n = ++count;
            slint::invoke_from_event_loop([weak, n] {
                if (auto w = weak.lock())
                    (*w)->set_qt_ticks(n);
            });
        });
        ticks.start(1000);
        app.exec();
        qtApp = nullptr;
    });

    auto startMpv = [&] {
        // QCoreApplication sets the locale from the environment; mpv needs C numbers
        std::setlocale(LC_NUMERIC, "C");
        mpv = mpv_create();
        mpv_set_option_string(mpv, "vo", "libmpv");
        mpv_set_option_string(mpv, "hwdec", opts.hwdec.c_str());
        if (!std::getenv("SPIKE_NOLOOP"))
            mpv_set_option_string(mpv, "loop-file", "inf");
        if (std::getenv("SPIKE_VIDNO"))
            mpv_set_option_string(mpv, "vid", "no");
        mpv_set_option_string(mpv, "terminal", "yes");
        mpv_set_option_string(mpv, "msg-level", "all=warn");
        if (!opts.ao.empty())
            mpv_set_option_string(mpv, "ao", opts.ao.c_str());
        // SPIKE_MPV="key=value key=value": extra options for experiments
        if (const char *extra = std::getenv("SPIKE_MPV")) {
            std::string all = extra;
            size_t pos = 0;
            while (pos < all.size()) {
                size_t end = all.find(' ', pos);
                std::string kv = all.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                if (size_t eq = kv.find('='); eq != std::string::npos)
                    mpv_set_option_string(mpv, kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str());
                if (end == std::string::npos)
                    break;
                pos = end + 1;
            }
        }
        if (mpv_initialize(mpv) < 0) {
            std::fprintf(stderr, "spike: mpv_initialize failed\n");
            return;
        }
        mpv_observe_property(mpv, 0, "media-title", MPV_FORMAT_STRING);
        mpv_observe_property(mpv, 0, "pause", MPV_FORMAT_FLAG);
        mpv_observe_property(mpv, 0, "percent-pos", MPV_FORMAT_DOUBLE);
        mpv_observe_property(mpv, 0, "hwdec-current", MPV_FORMAT_STRING);
        mpv_set_wakeup_callback(mpv, [](void *data) {
            auto weak = *static_cast<slint::ComponentWeakHandle<SpikeWindow> *>(data);
            slint::invoke_from_event_loop([weak] {
                if (auto w = weak.lock(); w && g_mpv)
                    processMpvEvents(g_mpv, *w);
            });
        }, new slint::ComponentWeakHandle(ui));
        g_mpv = mpv;
        auto load = [&] {
            if (opts.media.empty())
                return;
            const char *cmd[] = { "loadfile", opts.media.c_str(), nullptr };
            mpv_command_async(mpv, 0, cmd);
        };
        if (std::getenv("SPIKE_VIDNO"))
            load();
        else
            renderer->m_onContext = load;
        ui->window().request_redraw();
    };
    ui->on_ping_qt([&qtApp, weak = slint::ComponentWeakHandle(ui)] {
        QCoreApplication *app = qtApp;
        if (!app)
            return;
        QMetaObject::invokeMethod(app, [weak] {
            auto tid = std::hash<std::thread::id>()(std::this_thread::get_id()) & 0xffff;
            std::fprintf(stderr, "spike: ping handled on Qt thread %04zx\n", tid);
            slint::invoke_from_event_loop([weak] {
                if (auto w = weak.lock())
                    (*w)->set_media_title("Qt thread answered");
            });
        }, Qt::QueuedConnection);
    });

    auto togglePause = [&mpv] {
        if (!mpv)
            return;
        const char *cmd[] = { "cycle", "pause", nullptr };
        mpv_command_async(mpv, 0, cmd);
    };
    ui->on_toggle_pause(togglePause);
    tray->on_toggle_pause(togglePause);
    tray->on_quit([] { slint::quit_event_loop(); });
    ui->on_seek([&mpv](float v) {
        if (!mpv)
            return;
        std::string pct = std::to_string(v * 100.0);
        const char *cmd[] = { "seek", pct.c_str(), "absolute-percent", nullptr };
        mpv_command_async(mpv, 0, cmd);
    });
    ui->window().on_close_requested([] {
        slint::quit_event_loop();
        return slint::CloseRequestResponse::HideWindow;
    });

    VideoRenderer video(ui, &mpv, opts.check);
    renderer = &video;
    if (auto err = ui->window().set_rendering_notifier(std::ref(video))) {
        std::fprintf(stderr, "spike: no GL renderer (set SLINT_BACKEND=winit-femtovg)\n");
    }

    // mpv starts once the Qt thread has set its locale
    onQtReady = [&] {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "spike: Qt thread up after %lld ms\n", (long long)ms);
        startMpv();
        if (opts.check)
            ui->invoke_ping_qt();
    };
    slint::Timer checkTimer;
    if (opts.check) {
        checkTimer.start(slint::TimerMode::SingleShot, std::chrono::seconds(4), [&] {
            // the decoded frame as mpv has it, without the GL render path
            if (mpv) {
                const char *shot[] = { "screenshot-to-file", "spike-video.png", "video", nullptr };
                mpv_command_async(mpv, 0, shot);
            }
            renderer->requestReadBack();
            ui->window().request_redraw();
        });
    }

    ui->show();
    slint::run_event_loop(slint::EventLoopMode::RunUntilQuit);

    if (QCoreApplication *app = qtApp)
        QMetaObject::invokeMethod(app, &QCoreApplication::quit, Qt::QueuedConnection);
    qtThread.join();
    ui->hide();
    uiHolder.reset();
    if (mpv) {
        g_mpv = nullptr;
        mpv_terminate_destroy(mpv);
    }
    std::fprintf(stderr, "spike: clean exit\n");
    return 0;
}
