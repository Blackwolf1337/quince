#pragma once

// The few OpenGL (ES) entry points the video renderer needs, loaded from the
// context Slint makes current. No GL headers: MinGW ships only GL 1.1 and
// macOS deprecates them. GLAPI_CALL is __stdcall on Windows (matters for
// 32-bit builds only).

#if defined(_WIN32)
#define GLAPI_CALL __stdcall
#else
#define GLAPI_CALL
#endif

namespace gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLbitfield = unsigned int;
using GLboolean = unsigned char;
using GLubyte = unsigned char;

constexpr GLenum TEXTURE_2D = 0x0DE1;
constexpr GLenum RGBA = 0x1908;
constexpr GLenum UNSIGNED_BYTE = 0x1401;
constexpr GLenum TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum LINEAR = 0x2601;
constexpr GLenum NEAREST = 0x2600;
constexpr GLenum FRAMEBUFFER = 0x8D40;
constexpr GLenum READ_FRAMEBUFFER = 0x8CA8;
constexpr GLenum DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum DRAW_FRAMEBUFFER_BINDING = 0x8CA6;
constexpr GLenum READ_FRAMEBUFFER_BINDING = 0x8CAA;
constexpr GLenum COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum SCISSOR_TEST = 0x0C11;
constexpr GLenum TEXTURE_BINDING_2D = 0x8069;
constexpr GLenum ACTIVE_TEXTURE = 0x84E0;
constexpr GLenum TEXTURE0 = 0x84C0;
constexpr GLenum VERSION = 0x1F02;

struct Functions
{
#define GL_FN(ret, name, ...) ret(GLAPI_CALL *name)(__VA_ARGS__) = nullptr;
#include "GlFunctions.inc"
#undef GL_FN
};

// the entry points of the current context; false if one is missing
bool load(Functions &f);
// an entry point of the current context (also for mpv's render API)
void *procAddress(const char *name);

} // namespace gl
