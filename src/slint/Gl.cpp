#include "Gl.h"

#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace gl {

#if defined(_WIN32)
void *procAddress(const char *name)
{
    void *p = reinterpret_cast<void *>(wglGetProcAddress(name));
    // wglGetProcAddress returns small sentinels for the GL 1.1 entry points
    const auto v = reinterpret_cast<intptr_t>(p);
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1)
        p = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("opengl32.dll"), name));
    return p;
}
#elif defined(__APPLE__)
void *procAddress(const char *name)
{
    return dlsym(RTLD_DEFAULT, name);
}
#else
// Wayland and most X11 setups get an EGL context from Slint, some X11 setups GLX
void *procAddress(const char *name)
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

bool load(Functions &f)
{
    bool ok = true;
#define GL_FN(ret, name, ...)                                                   \
    f.name = reinterpret_cast<decltype(f.name)>(procAddress("gl" #name));       \
    if (!f.name) {                                                              \
        std::fprintf(stderr, "quince: missing gl" #name "\n");                  \
        ok = false;                                                             \
    }
#include "GlFunctions.inc"
#undef GL_FN
    return ok;
}

} // namespace gl
