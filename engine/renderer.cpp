// renderer.cpp: momo's drawing, on OpenGL 4.6. Chapter 08 explains how it
// starts OpenGL, and chapter 10 how it draws.
#include "renderer.h"
#include "batch.h"
#include "logger.h"
#include "platform.h"

// glad declares every OpenGL 4.6 function and constant. Each function is a
// pointer that gladLoadGL fills in, under a macro with the function's name:
// glClear is really glad_glClear.
#include <glad/gl.h>

#include <stddef.h>

namespace renderer {

// The color behind everything, as red, green, blue and alpha from 0 to 1.
constexpr f32 CLEAR_COLOR[] = { 0.06f, 0.22f, 0.25f, 1.0f }; // dark teal

// ---- Shaders ----------------------------------------------------------------

// Each rectangle is one instance: the GPU runs the vertex shader for four
// corners per rectangle, and each corner reads that rectangle's Rect and
// Color from rect_buffer. The numbers in layout(location = ...) are the
// ones the vertex array below uses.
constexpr const char* VERTEX_SHADER = R"(
#version 460 core

layout(location = 0) uniform vec2 screen_size; // 256, 240

layout(location = 0) in ivec4 rect;  // x, y, w, h, in screen pixels
layout(location = 1) in vec4 color;  // red, green, blue, alpha, 0 to 1

out vec4 corner_color;

void main()
{
    // gl_VertexID counts the corners, 0 to 3, which the triangle strip
    // joins into two triangles: 0 1 2, then 1 2 3.
    //
    //   0 ---- 1      corner = (0, 0), (1, 0), (0, 1), (1, 1)
    //   |    / |
    //   |  /   |
    //   2 ---- 3
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec2 pixel = rect.xy + corner * rect.zw;

    // OpenGL draws whatever is between -1 and 1, with y pointing up. The
    // screen runs from 0 to its size, with y pointing down.
    vec2 clip = pixel / screen_size * 2.0 - 1.0;
    gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0);
    corner_color = color;
}
)";

constexpr const char* FRAGMENT_SHADER = R"(
#version 460 core

in vec4 corner_color;

out vec4 pixel_color;

void main()
{
    pixel_color = corner_color;
}
)";

// The vertex shader's locations, which its layout(location = ...) lines give.
constexpr i32 SCREEN_SIZE_UNIFORM = 0;
constexpr u32 RECT_ATTRIB = 0;
constexpr u32 COLOR_ATTRIB = 1;

// ---- The renderer's state ---------------------------------------------------

// The engine never reloads, so these can be file-scope variables, like
// platform.cpp's window.
static u32 program;          // the two shaders, linked together
static u32 rect_buffer;      // the frame's rectangles, on the GPU
static u32 vertex_array;     // how the vertex shader reads rect_buffer
static Batch batch;          // the frame's rectangles, as the game drew them
static RectInstance* sorted; // the same rectangles, in drawing order
static bool in_frame;        // between begin_frame and end_frame

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
    // to it. A shader that doesn't compile is an expected failure, not a
    // bug, and compile_shader reports it, so init turns off the shader
    // compiler's messages.
    bool api_error = source == GL_DEBUG_SOURCE_API && type == GL_DEBUG_TYPE_ERROR;
    ASSERT(!api_error);
}

// ---- Compiling shaders ------------------------------------------------------

// The compiler's and the linker's messages end with a newline, and the log
// adds one of its own. This is the length of the text without it.
static i32 without_last_newline(const char* text, i32 length)
{
    if (length > 0 && text[length - 1] == '\n')
        return length - 1;
    return length;
}

// Compiles one shader. If it doesn't compile, logs the compiler's messages
// and returns 0.
static u32 compile_shader(GLenum stage, const char* stage_name, const char* source)
{
    u32 shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    i32 compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char messages[1024] = {};
        i32 length = 0;
        glGetShaderInfoLog(shader, sizeof(messages), &length, messages);
        LOG_ERROR("the %s shader doesn't compile:\n%.*s", stage_name,
                  without_last_newline(messages, length), messages);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Compiles both shaders and links them into a program. Returns 0 if either
// step fails, after logging why.
static u32 make_program(const char* vertex_source, const char* fragment_source)
{
    u32 vertex = compile_shader(GL_VERTEX_SHADER, "vertex", vertex_source);
    u32 fragment = compile_shader(GL_FRAGMENT_SHADER, "fragment", fragment_source);
    if (!vertex || !fragment) {
        glDeleteShader(vertex); // deleting 0 does nothing
        glDeleteShader(fragment);
        return 0;
    }

    u32 linked_program = glCreateProgram();
    glAttachShader(linked_program, vertex);
    glAttachShader(linked_program, fragment);
    glLinkProgram(linked_program);
    // The program keeps what it needs from the shaders, which can go.
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    i32 linked = 0;
    glGetProgramiv(linked_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char messages[1024] = {};
        i32 length = 0;
        glGetProgramInfoLog(linked_program, sizeof(messages), &length, messages);
        LOG_ERROR("the shaders don't link:\n%.*s", without_last_newline(messages, length),
                  messages);
        glDeleteProgram(linked_program);
        return 0;
    }
    return linked_program;
}

// ---- The renderer -----------------------------------------------------------

bool init(Arena* arena)
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

    // A shader that doesn't compile arrives here as an error message too,
    // from the shader compiler. compile_shader asks for the same text
    // itself, which works on every driver, debug context or not, so the
    // compiler's messages are turned off rather than logged twice.
    glDebugMessageControl(GL_DEBUG_SOURCE_SHADER_COMPILER, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                          GL_FALSE);

    program = make_program(VERTEX_SHADER, FRAGMENT_SHADER);
    if (!program)
        return false;
    // DSA again: glProgramUniform names the program, so it needn't be bound.
    glProgramUniform2f(program, SCREEN_SIZE_UNIFORM, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Room for a frame's rectangles three times over: as the game draws
    // them, in drawing order, and on the GPU. The GPU's buffer keeps its
    // size for good (Storage), and the CPU can rewrite it every frame
    // (DYNAMIC_STORAGE).
    batch::init(&batch, arena, MAX_RECTS);
    sorted = arena::push_array<RectInstance>(arena, MAX_RECTS);
    glCreateBuffers(1, &rect_buffer);
    glNamedBufferStorage(rect_buffer, MAX_RECTS * sizeof(RectInstance), nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    // The vertex array says where the vertex shader's inputs come from.
    // Binding point 0 reads rect_buffer, one RectInstance after another,
    // and moves to the next one once per instance (divisor 1) instead of
    // once per corner. rect is four ints, read as ints (IFormat). color is
    // four bytes, turned into floats from 0 to 1 (normalized: GL_TRUE).
    glCreateVertexArrays(1, &vertex_array);
    glVertexArrayVertexBuffer(vertex_array, 0, rect_buffer, 0, sizeof(RectInstance));
    glVertexArrayBindingDivisor(vertex_array, 0, 1);

    glEnableVertexArrayAttrib(vertex_array, RECT_ATTRIB);
    glVertexArrayAttribIFormat(vertex_array, RECT_ATTRIB, 4, GL_INT, offsetof(RectInstance, rect));
    glVertexArrayAttribBinding(vertex_array, RECT_ATTRIB, 0);

    glEnableVertexArrayAttrib(vertex_array, COLOR_ATTRIB);
    glVertexArrayAttribFormat(vertex_array, COLOR_ATTRIB, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                              offsetof(RectInstance, color));
    glVertexArrayAttribBinding(vertex_array, COLOR_ATTRIB, 0);

    // Alpha blending: a pixel ends up as color * alpha + what was there
    // * (1 - alpha).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

void shutdown()
{
    glDeleteVertexArrays(1, &vertex_array);
    glDeleteBuffers(1, &rect_buffer);
    glDeleteProgram(program);
    vertex_array = 0;
    rect_buffer = 0;
    program = 0;
}

void begin_frame()
{
    ASSERT(!in_frame);
    in_frame = true;
    batch::clear(&batch);

    // Direct state access (DSA): the call names the framebuffer it clears,
    // and takes the color, instead of clearing whatever is bound with
    // whatever glClearColor last set. Framebuffer 0 is the window's.
    glClearNamedFramebufferfv(0, GL_COLOR, 0, CLEAR_COLOR);
}

void draw_rect(Rect rect, Color color, i32 layer)
{
    // begin_frame empties the batch, so a rectangle drawn before it, from
    // the game's update say, would vanish without a trace.
    ASSERT(in_frame);
    batch::add(&batch, rect, color, layer);
}

void end_frame()
{
    ASSERT(in_frame);
    in_frame = false;
    if (batch.count == 0)
        return;

    batch::sort(batch, sorted);
    glNamedBufferSubData(rect_buffer, 0, batch.count * (i64)sizeof(RectInstance), sorted);

    // Drawing has no DSA version: it uses whatever program and vertex array
    // are bound. They're bound right here, so the draw never depends on
    // what some other code left bound.
    glUseProgram(program);
    glBindVertexArray(vertex_array);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, batch.count);
}

} // namespace renderer
