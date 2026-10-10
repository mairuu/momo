// renderer.cpp: momo's drawing, on OpenGL 4.6. Chapter 08 explains it.
#include "renderer.h"
#include "logger.h"
#include "platform.h"

// glad declares every OpenGL 4.6 function and constant. Each function is a
// pointer that gladLoadGL fills in, under a macro with the function's name:
// glClear is really glad_glClear.
#include <glad/gl.h>

namespace renderer {

// The color behind everything, as red, green, blue and alpha from 0 to 1.
constexpr f32 CLEAR_COLOR[] = { 0.06f, 0.22f, 0.25f, 1.0f }; // dark teal

// ---- Debug messages ---------------------------------------------------------

// What kind of problem a debug message reports, as a word for the log.
static const char* message_type_name(GLenum type)
{
    switch (type) {
    case GL_DEBUG_TYPE_ERROR:
        return "error";
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        return "deprecated";
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        return "undefined behavior";
    case GL_DEBUG_TYPE_PORTABILITY:
        return "portability";
    case GL_DEBUG_TYPE_PERFORMANCE:
        return "performance";
    default:
        return "other";
    }
}

// OpenGL calls this with every debug message, from inside the OpenGL call
// the message is about. The parameters are the ones glDebugMessageCallback
// asks for. momo doesn't use the commented-out ones, and leaving them unnamed
// keeps -Wextra from warning about them.
//
// GLAD_API_PTR is the calling convention OpenGL uses: nothing on Linux,
// __stdcall on 32-bit Windows.
static void GLAD_API_PTR on_debug_message(GLenum source, GLenum type, GLuint /* id */,
                                          GLenum severity, GLsizei length, const GLchar* message,
                                          const void* /* user_data */)
{
    const char* type_name = message_type_name(type);
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
        LOG_ERROR("OpenGL %s: %.*s", type_name, length, message);
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        LOG_WARN("OpenGL %s: %.*s", type_name, length, message);
        break;
    default:
        LOG_INFO("OpenGL %s: %.*s", type_name, length, message);
        break;
    }

    // An error in a call (from the API, as OpenGL calls itself) means momo
    // called OpenGL wrongly, which is a bug: stop here. The message arrives
    // during the call that caused it, so in gdb the backtrace leads straight
    // to it. A shader that doesn't compile is an error too, but from the
    // shader compiler, and it's an expected failure, not a bug.
    bool api_error = source == GL_DEBUG_SOURCE_API && type == GL_DEBUG_TYPE_ERROR;
    ASSERT(!api_error);
}

// ---- The renderer -----------------------------------------------------------

bool init()
{
    // momo links no OpenGL library. The functions belong to the driver,
    // which SDL loaded when it opened the window, so each one is looked up
    // by name. glad asks for every function in OpenGL 4.6 through
    // platform::gl_function, keeps the addresses, and returns the version of
    // the context, or 0 if it couldn't even find glGetString.
    i32 version = gladLoadGL(platform::gl_function);
    if (version == 0) {
        LOG_ERROR("can't load OpenGL's functions");
        return false;
    }
    LOG_INFO("OpenGL %s, %s", (const char*)glGetString(GL_VERSION),
             (const char*)glGetString(GL_RENDERER));

    // Only a debug context promises to send debug messages. platform::init
    // asked for one, but a driver is allowed to say no.
    i32 flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (!(flags & GL_CONTEXT_FLAG_DEBUG_BIT))
        LOG_WARN("not a debug context, so OpenGL may not explain its errors");

    // Synchronous: each message arrives during the call that caused it.
    // Otherwise a driver may queue calls and run them later, Mesa on a
    // thread of its own, and report an error from wherever the queue ran,
    // such as the swap in platform::present. Notifications, the driver
    // saying what it did with your data, are turned off: they're not
    // problems, and some drivers send one for every buffer.
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(on_debug_message, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr,
                          GL_FALSE);
    return true;
}

void begin_frame()
{
    // Direct state access (DSA): the call names the framebuffer it clears,
    // and takes the color, instead of clearing whatever is bound with
    // whatever glClearColor last set. Framebuffer 0 is the window's.
    glClearNamedFramebufferfv(0, GL_COLOR, 0, CLEAR_COLOR);
}

} // namespace renderer
