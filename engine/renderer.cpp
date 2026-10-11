// renderer.cpp: momo's drawing, on OpenGL 4.6. Chapter 08 explains how it
// starts OpenGL, chapter 10 how it draws, chapter 11 how the screen fits the
// window, chapter 12 textures and text, and chapter 13 the overlay and
// reading the screen back.
#include "renderer.h"
#include "batch.h"
#include "font.h"
#include "logger.h"
#include "platform.h"
#include "screen.h"

// glad declares every OpenGL 4.6 function and constant. Each function is a
// pointer that gladLoadGL fills in, under a macro with the function's name:
// glClear is really glad_glClear.
#include <glad/gl.h>

// stb_image decodes PNG files into pixels (chapter 12). Its code is compiled
// once, in third_party/stb/stb_image.c, and this is only its declarations.
#include <stb_image.h>

#include <stddef.h>
#include <string.h>

namespace renderer {

// The color behind everything on the screen, as red, green, blue and alpha
// from 0 to 1, and the color of the bars around the screen.
constexpr f32 CLEAR_COLOR[] = { 0.06f, 0.22f, 0.25f, 1.0f }; // dark teal
constexpr f32 BAR_COLOR[] = { 0.0f, 0.0f, 0.0f, 1.0f };      // black

// ---- Shaders ----------------------------------------------------------------

// Each rectangle is one instance: the GPU runs the vertex shader for four
// corners per rectangle, and each corner reads that rectangle's RectInstance
// from rect_buffer. The numbers in layout(location = ...) are the ones the
// vertex array below uses. sprite_texture is whichever texture is bound to
// texture unit 0 when the draw call runs.
constexpr const char* VERTEX_SHADER = R"(
#version 460 core

layout(location = 0) uniform vec2 screen_size; // 256, 240
layout(binding = 0) uniform sampler2D sprite_texture;

layout(location = 0) in ivec4 rect;   // x, y, w, h, in screen pixels
layout(location = 1) in ivec4 source; // x, y, w, h, in the texture's pixels
layout(location = 2) in vec4 color;   // red, green, blue, alpha, 0 to 1

out vec2 uv;
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

    // The same corner of the source rectangle, in the texture's pixels, then
    // as texture coordinates, which run from 0 to 1 across the texture.
    // Both start at the texture's first row, the top of the picture, so a
    // sprite's top lands at the rectangle's top.
    vec2 texel = source.xy + corner * source.zw;
    uv = texel / vec2(textureSize(sprite_texture, 0));
    corner_color = color;
}
)";

// Each pixel is the texture's color times the rectangle's: a white texture
// pixel comes out in the rectangle's color, and a white rectangle shows the
// texture as it is.
constexpr const char* FRAGMENT_SHADER = R"(
#version 460 core

layout(binding = 0) uniform sampler2D sprite_texture;

in vec2 uv;
in vec4 corner_color;

out vec4 pixel_color;

void main()
{
    pixel_color = texture(sprite_texture, uv) * corner_color;
}
)";

// The shaders' locations and binding, which their layout(...) lines give.
constexpr i32 SCREEN_SIZE_UNIFORM = 0;
constexpr u32 RECT_ATTRIB = 0;
constexpr u32 SOURCE_ATTRIB = 1;
constexpr u32 COLOR_ATTRIB = 2;
constexpr u32 SPRITE_TEXTURE_UNIT = 0;

// The two textures the renderer makes for itself, always the first two.
constexpr Texture WHITE_TEXTURE = { .index = 0 }; // one white pixel, for draw_rect
constexpr Texture FONT_TEXTURE = { .index = 1 };  // momo's 8x8 font, for draw_text

// The most rectangles the overlay can draw in a frame, apart from the game's
// MAX_RECTS: a screen's worth of letters, 32 by 30, plus their backdrop, fits
// with room to spare.
constexpr i32 MAX_OVERLAY_RECTS = 1024;

// Room for a frame's rectangles: the game's and the overlay's.
constexpr i32 MAX_FRAME_RECTS = MAX_RECTS + MAX_OVERLAY_RECTS;

// ---- The renderer's state ---------------------------------------------------

// A texture the renderer has made: OpenGL's number for it, and the file it
// came from, as the game named it. The path is empty for the two textures
// the renderer makes itself.
struct LoadedTexture {
    u32 gl_texture;
    Str path;
};

// The engine never reloads, so these can be file-scope variables, like
// platform.cpp's window.
static Arena* arena;             // the engine's, for the rest of the run
static u32 program;              // the two shaders, linked together
static u32 rect_buffer;          // the frame's rectangles, on the GPU
static u32 vertex_array;         // how the vertex shader reads rect_buffer
static Batch game_batch;         // the frame's rectangles, as the game drew them
static Batch overlay_batch;      // and the overlay's, drawn after the game's
static Batch* drawing_into;      // the batch draw_rect and the rest add to
static RectInstance* sorted;     // all of the frame's rectangles, in drawing order
static Texture* sorted_textures; // and each one's texture
static DrawCall* calls;          // the draw calls that draw them
static RenderStats stats;        // what the game drew in the last frame
static bool in_frame;            // between begin_frame and end_frame
static u32 screen_texture;       // the 256x240 screen, which the game draws on
static u32 screen_framebuffer;   // what lets OpenGL draw into screen_texture
static i32 screen_scale;         // the scale last logged, 0 before the first frame

// Every texture there is, the renderer's own two first. A Texture is an
// index into this.
static LoadedTexture textures[MAX_TEXTURES];
static i32 texture_count;

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

// ---- Textures ---------------------------------------------------------------

// Makes a texture of width x height pixels from rgba, four bytes per pixel,
// top row first, and adds it to the table.
static Texture make_texture(i32 width, i32 height, const void* rgba, Str path)
{
    ASSERT(texture_count < MAX_TEXTURES);

    // The same steps as the screen's texture (chapter 11), plus the pixels.
    // OpenGL keeps rows in the order it's given them: the first row, the
    // picture's top, is where texture coordinates have y = 0.
    u32 gl_texture = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &gl_texture);
    glTextureStorage2D(gl_texture, 1, GL_RGBA8, width, height);
    glTextureSubImage2D(gl_texture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    // NEAREST: each screen pixel takes the color of the one texture pixel
    // it lands in, never a mix of it and its neighbors. MAG_FILTER is for
    // drawing at the texture's own size or bigger, which is all momo does.
    // MIN_FILTER, for drawing smaller, is set too, so no default is left to
    // look up. CLAMP_TO_EDGE: past the edge, the edge's own pixels, never
    // the far side's.
    glTextureParameteri(gl_texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(gl_texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(gl_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(gl_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    textures[texture_count] = { .gl_texture = gl_texture, .path = path };
    texture_count++;
    return { .index = texture_count - 1 };
}

Texture load_texture(const char* path)
{
    // A file is loaded once. The game asks again whenever it starts over,
    // which a change to GameState's layout makes it do (chapter 06).
    Str wanted = { .data = path, .len = (i32)strlen(path) };
    for (i32 i = 0; i < texture_count; i++) {
        if (str::eq(textures[i].path, wanted))
            return { .index = i };
    }

    // Paths are relative to the executable's folder, where game.so is.
    Str full_path = str::fmt(arena, "%s%s", platform::executable_folder(), path);
    i64 size = 0;
    void* file = platform::read_file(full_path.data, &size);
    if (!file)
        return {}; // already logged

    // stb_image decodes the PNG into rows of pixels, top row first, four
    // bytes each: the 4 asks for red, green, blue and alpha, whatever the
    // file holds, and the null says momo doesn't need to know what that was.
    // Its memory comes from malloc.
    i32 width = 0;
    i32 height = 0;
    u8* pixels = stbi_load_from_memory((const u8*)file, (i32)size, &width, &height, nullptr, 4);
    platform::free_file(file);
    if (!pixels) {
        LOG_ERROR("can't load %s: %s", path, stbi_failure_reason());
        return {};
    }

    // The path is copied: the game's text lives in game.so, which a reload
    // unloads (chapter 06). The copy is in the engine's arena, like the
    // texture, for the rest of the run.
    Texture texture = make_texture(width, height, pixels, str::fmt(arena, "%s", path));
    stbi_image_free(pixels);
    LOG_INFO("loaded %s, %dx%d, as texture %d", path, width, height, texture.index);
    return texture;
}

// ---- The renderer -----------------------------------------------------------

bool init(Arena* engine_arena)
{
    arena = engine_arena;

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

    // Room for a frame's rectangles three times over: as the game and the
    // overlay draw them, in drawing order, and on the GPU, plus the draw
    // calls, at most one per rectangle. The GPU's buffer keeps its size for
    // good (Storage), and the CPU can rewrite it every frame
    // (DYNAMIC_STORAGE).
    batch::init(&game_batch, arena, MAX_RECTS);
    batch::init(&overlay_batch, arena, MAX_OVERLAY_RECTS);
    sorted = arena::push_array<RectInstance>(arena, MAX_FRAME_RECTS);
    sorted_textures = arena::push_array<Texture>(arena, MAX_FRAME_RECTS);
    calls = arena::push_array<DrawCall>(arena, MAX_FRAME_RECTS);
    glCreateBuffers(1, &rect_buffer);
    glNamedBufferStorage(rect_buffer, MAX_FRAME_RECTS * sizeof(RectInstance), nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    // The vertex array says where the vertex shader's inputs come from.
    // Binding point 0 reads rect_buffer, one RectInstance after another,
    // and moves to the next one once per instance (divisor 1) instead of
    // once per corner. rect and source are four ints each, read as ints
    // (IFormat). color is four bytes, turned into floats from 0 to 1
    // (normalized: GL_TRUE).
    glCreateVertexArrays(1, &vertex_array);
    glVertexArrayVertexBuffer(vertex_array, 0, rect_buffer, 0, sizeof(RectInstance));
    glVertexArrayBindingDivisor(vertex_array, 0, 1);

    glEnableVertexArrayAttrib(vertex_array, RECT_ATTRIB);
    glVertexArrayAttribIFormat(vertex_array, RECT_ATTRIB, 4, GL_INT, offsetof(RectInstance, rect));
    glVertexArrayAttribBinding(vertex_array, RECT_ATTRIB, 0);

    glEnableVertexArrayAttrib(vertex_array, SOURCE_ATTRIB);
    glVertexArrayAttribIFormat(vertex_array, SOURCE_ATTRIB, 4, GL_INT,
                               offsetof(RectInstance, source));
    glVertexArrayAttribBinding(vertex_array, SOURCE_ATTRIB, 0);

    glEnableVertexArrayAttrib(vertex_array, COLOR_ATTRIB);
    glVertexArrayAttribFormat(vertex_array, COLOR_ATTRIB, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                              offsetof(RectInstance, color));
    glVertexArrayAttribBinding(vertex_array, COLOR_ATTRIB, 0);

    // Alpha blending: a pixel ends up as color * alpha + what was there
    // * (1 - alpha).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // The screen: a texture of 256x240 RGBA pixels, with one level (no
    // mipmaps), and a framebuffer that makes it something OpenGL can draw
    // into instead of the window. RGBA8 is a format every OpenGL 4.6 driver
    // must be able to draw into, so if the framebuffer isn't complete, momo
    // made it wrongly.
    glCreateTextures(GL_TEXTURE_2D, 1, &screen_texture);
    glTextureStorage2D(screen_texture, 1, GL_RGBA8, SCREEN_WIDTH, SCREEN_HEIGHT);
    glCreateFramebuffers(1, &screen_framebuffer);
    glNamedFramebufferTexture(screen_framebuffer, GL_COLOR_ATTACHMENT0, screen_texture, 0);
    ASSERT(glCheckNamedFramebufferStatus(screen_framebuffer, GL_FRAMEBUFFER) ==
           GL_FRAMEBUFFER_COMPLETE);

    // The renderer's own two textures, which take indices 0 and 1. The font
    // is made from the glyphs in font.cpp, in a picture of 128x48 pixels
    // (24 KB), which can go on the stack: OpenGL copies it.
    constexpr Color WHITE = rgb(0xffffff);
    Texture white = make_texture(1, 1, &WHITE, {});

    Color font_pixels[FONT_WIDTH * FONT_HEIGHT];
    font::fill_pixels(font_pixels);
    Texture font_texture = make_texture(FONT_WIDTH, FONT_HEIGHT, font_pixels, {});

    ASSERT(white.index == WHITE_TEXTURE.index && font_texture.index == FONT_TEXTURE.index);
    return true;
}

void shutdown()
{
    for (i32 i = 0; i < texture_count; i++)
        glDeleteTextures(1, &textures[i].gl_texture);
    texture_count = 0;
    glDeleteFramebuffers(1, &screen_framebuffer);
    glDeleteTextures(1, &screen_texture);
    glDeleteVertexArrays(1, &vertex_array);
    glDeleteBuffers(1, &rect_buffer);
    glDeleteProgram(program);
    screen_framebuffer = 0;
    screen_texture = 0;
    vertex_array = 0;
    rect_buffer = 0;
    program = 0;
}

void begin_frame()
{
    ASSERT(!in_frame);
    in_frame = true;
    batch::clear(&game_batch);
    batch::clear(&overlay_batch);
    drawing_into = &game_batch;

    // Direct state access (DSA): the call names the framebuffer it clears,
    // and takes the color, instead of clearing whatever is bound with
    // whatever glClearColor last set.
    glClearNamedFramebufferfv(screen_framebuffer, GL_COLOR, 0, CLEAR_COLOR);
}

void draw_rect(Rect rect, Color color, i32 layer)
{
    // begin_frame empties the batch, so a rectangle drawn before it, from
    // the game's update say, would vanish without a trace.
    ASSERT(in_frame);
    RectInstance instance = {
        .rect = rect,
        .source = { .x = 0, .y = 0, .w = 1, .h = 1 }, // all of the white pixel
        .color = color,
    };
    batch::add(drawing_into, instance, WHITE_TEXTURE, layer);
}

void draw_sprite(Texture texture, Rect source, i32 x, i32 y, i32 layer)
{
    ASSERT(in_frame);
    ASSERT(texture.index >= 0 && texture.index < texture_count);
    RectInstance instance = {
        .rect = { .x = x, .y = y, .w = source.w, .h = source.h },
        .source = source,
        .color = rgb(0xffffff), // the texture's own colors
    };
    batch::add(drawing_into, instance, texture, layer);
}

void draw_text(Str text, i32 x, i32 y, Color color, i32 layer)
{
    // One sprite per char, cut from the font's texture, which is white, so
    // each one comes out in color.
    ASSERT(in_frame);
    for (i32 i = 0; i < text.len; i++) {
        RectInstance instance = {
            .rect = { .x = x + i * GLYPH_SIZE, .y = y, .w = GLYPH_SIZE, .h = GLYPH_SIZE },
            .source = font::glyph(text.data[i]),
            .color = color,
        };
        batch::add(drawing_into, instance, FONT_TEXTURE, layer);
    }
}

void begin_overlay()
{
    ASSERT(in_frame && drawing_into == &game_batch);
    drawing_into = &overlay_batch;
}

// Draws the frame's rectangles on the screen: one draw call for each run of
// rectangles in a row, in drawing order, that use the same texture.
static void draw_batches()
{
    // Both batches go into one array in drawing order, the overlay's after
    // the game's, so it comes out on top of every layer the game drew. The
    // game's draw calls are counted for the overlay before the overlay's
    // rectangles join them.
    i32 game_count = game_batch.count;
    i32 count = game_count + overlay_batch.count;
    batch::sort(game_batch, sorted, sorted_textures);
    batch::sort(overlay_batch, sorted + game_count, sorted_textures + game_count);
    stats = {
        .rects = game_count,
        .draw_calls = batch::draw_calls(sorted_textures, game_count, calls),
    };
    if (count == 0)
        return;

    i32 call_count = batch::draw_calls(sorted_textures, count, calls);
    glNamedBufferSubData(rect_buffer, 0, count * (i64)sizeof(RectInstance), sorted);

    // Drawing has no DSA version: it draws into whatever framebuffer is
    // bound, with whatever program and vertex array are bound. They're
    // bound right here, so the draw never depends on what some other code
    // left bound. The viewport is where in the framebuffer clip space's -1
    // to 1 lands, and it's set here too: it starts out as the window's size,
    // and a framebuffer doesn't bring its own.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, screen_framebuffer);
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    glUseProgram(program);
    glBindVertexArray(vertex_array);

    // Every draw call reads the same buffer. BaseInstance says where its
    // rectangles start: instance 0 of the call reads RectInstance number
    // first. glBindTextureUnit is DSA's way to bind a texture to a unit.
    for (i32 i = 0; i < call_count; i++) {
        DrawCall call = calls[i];
        glBindTextureUnit(SPRITE_TEXTURE_UNIT, textures[call.texture.index].gl_texture);
        glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, 0, 4, call.count, (u32)call.first);
    }
}

// Copies the screen into the window, scaled up by a whole number, and fills
// the rest of the window with black.
static void present_screen()
{
    i32 window_width = 0;
    i32 window_height = 0;
    platform::window_size_in_pixels(&window_width, &window_height);
    ScreenFit fit = screen::fit(window_width, window_height);
    if (fit.scale != screen_scale) {
        LOG_INFO("the screen at %dx: %dx%d pixels, in a window of %dx%d", fit.scale, fit.rect.w,
                 fit.rect.h, window_width, window_height);
        screen_scale = fit.scale;
    }

    // Framebuffer 0 is the window's. A blit copies a rectangle of pixels
    // from one framebuffer to another, scaling it to the size of the
    // rectangle it lands in. GL_NEAREST gives each window pixel the color of
    // the screen pixel its center falls in, so every screen pixel comes out
    // as a square of exactly scale x scale pixels, all of one color.
    //
    // OpenGL counts a framebuffer's rows from the bottom, so the bottom
    // edge is however far the screen's bottom is from the window's.
    i32 bottom = window_height - (fit.rect.y + fit.rect.h);
    glClearNamedFramebufferfv(0, GL_COLOR, 0, BAR_COLOR);
    glBlitNamedFramebuffer(screen_framebuffer, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, fit.rect.x,
                           bottom, fit.rect.x + fit.rect.w, bottom + fit.rect.h,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void end_frame()
{
    ASSERT(in_frame);
    in_frame = false;
    draw_batches();
    present_screen();
}

RenderStats last_frame_stats()
{
    return stats;
}

void read_screen(u8* rgb)
{
    ASSERT(!in_frame); // the screen holds a whole frame only after end_frame

    // Three bytes a pixel, leaving out alpha. The screen shows as opaque,
    // but its alpha isn't 255 everywhere: blending (chapter 10) mixes alpha
    // like the colors, so a see-through rectangle lowers it, and a PNG with
    // that alpha would be see-through there too. A row of 256 pixels is 768
    // bytes, a multiple of 4: OpenGL starts each row it hands back at a
    // multiple of 4 bytes, unless told otherwise, so there are no gaps.
    //
    // OpenGL draws the frame on the GPU after the calls that asked for it
    // return. To hand back the pixels, it first waits for that to finish.
    constexpr i32 ROW_SIZE = SCREEN_WIDTH * 3;
    glGetTextureImage(screen_texture, 0, GL_RGB, GL_UNSIGNED_BYTE, SCREEN_HEIGHT * ROW_SIZE, rgb);

    // OpenGL's rows start at the bottom: the vertex shader turns y around
    // (chapter 10), so the screen's top row is OpenGL's last. Swapping rows
    // from the outside in puts the top first.
    for (i32 y = 0; y < SCREEN_HEIGHT / 2; y++) {
        u8* top = rgb + (i64)y * ROW_SIZE;
        u8* bottom = rgb + (i64)(SCREEN_HEIGHT - 1 - y) * ROW_SIZE;
        u8 row[ROW_SIZE];
        memcpy(row, top, ROW_SIZE);
        memcpy(top, bottom, ROW_SIZE);
        memcpy(bottom, row, ROW_SIZE);
    }
}

} // namespace renderer
