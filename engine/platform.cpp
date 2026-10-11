// platform.cpp: the engine's one connection to the operating system, mostly
// through SDL3. Chapter 07 explains it, and the chapters after it each add
// what they need.
#include "platform.h"
#include "logger.h"

#include <SDL3/SDL.h>
#include <sys/mman.h>

namespace platform {

// The engine never reloads, so these can be file-scope variables, like the
// engine's own state in momo.cpp.
static SDL_Window* window;
static SDL_GLContext gl_context; // OpenGL's state for the window (chapter 08)

// ---- The window -------------------------------------------------------------

bool init(const char* title, i32 width, i32 height)
{
    // Starting SDL's video starts its events too, and SDL then handles Ctrl+C
    // (the signal SIGINT) and kill's SIGTERM itself: either one becomes a
    // quit event, which handle_events sees.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("can't start SDL: %s", SDL_GetError());
        return false;
    }

    // What kind of OpenGL context to make, which has to be said before the
    // window opens: version 4.6, the core profile, which leaves out
    // everything older versions deprecated, and a debug context, which
    // explains its errors (chapter 08). momo draws back to front, in layer
    // order, so it needs no depth buffer.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    // Resizable: the renderer fits its picture to any size (chapter 11).
    // HIGH_PIXEL_DENSITY asks for every pixel the window covers. Without it,
    // a display with a scale of 2 gets a window with a quarter of its
    // pixels, which the compositor stretches, and blurs at every edge.
    SDL_WindowFlags flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window = SDL_CreateWindow(title, width, height, flags);
    if (!window) {
        LOG_ERROR("can't open a window: %s", SDL_GetError());
        return false;
    }

    // Making the context also makes it current: the one OpenGL calls on this
    // thread go to. momo has one window, one context and one thread, so it
    // stays current until shutdown.
    gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        LOG_ERROR("can't make an OpenGL 4.6 context: %s", SDL_GetError());
        return false;
    }

    // Vsync: each swap waits until the display is ready for a new picture,
    // so momo draws as many frames a second as the display shows, and waits
    // the rest of the time. The game's speed doesn't depend on it: the loop
    // runs 60 ticks a second at any frame rate (chapter 09). Without vsync,
    // momo still works, but draws as many frames as it can.
    if (!SDL_GL_SetSwapInterval(1))
        LOG_WARN("no vsync: %s", SDL_GetError());

    // The display the window opens on, for the log. On Wayland, SDL can only
    // guess until the window is shown: the one with the focus.
    f32 refresh_rate = 0;
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    if (mode)
        refresh_rate = mode->refresh_rate;

    // The window's size as the window manager gave it, which a tiling one
    // decides for itself, then in pixels. The display scale is how many
    // pixels the display shows for each point (chapter 11).
    i32 window_width = 0;
    i32 window_height = 0;
    i32 pixel_width = 0;
    i32 pixel_height = 0;
    SDL_GetWindowSize(window, &window_width, &window_height);
    SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);

    i32 version = SDL_GetVersion();
    LOG_INFO("SDL %d.%d.%d, video driver %s, window %dx%d, %dx%d pixels, display scale %.3g, "
             "%.0f Hz",
             SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
             SDL_VERSIONNUM_MICRO(version), SDL_GetCurrentVideoDriver(), window_width,
             window_height, pixel_width, pixel_height, SDL_GetWindowDisplayScale(window),
             refresh_rate);
    return true;
}

void shutdown()
{
    SDL_GL_DestroyContext(gl_context);
    gl_context = nullptr;
    SDL_DestroyWindow(window);
    window = nullptr;
    SDL_Quit();
}

Events handle_events()
{
    // SDL collects events in a queue as they happen, and SDL_PollEvent takes
    // them out one at a time until the queue is empty. Closing the window
    // and Ctrl+C both end up as SDL_EVENT_QUIT. Every event not handled here
    // is ignored for now. That includes resizing: SDL keeps the window's size
    // up to date as it handles the events, and the renderer asks for it
    // every frame (chapter 11).
    Events events = {};
    SDL_Event event = {};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            LOG_INFO("quit requested");
            events.quit = true;
            return events; // the rest of the queue doesn't matter now
        }

        // A key's scancode is its place on the keyboard, whatever the layout
        // prints on it: SDL_SCANCODE_GRAVE is the key left of 1, ` on a US
        // keyboard. A key held down sends key-down events again and again,
        // marked repeat, and only the first one is a press.
        bool key_down = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
        if (key_down && event.key.scancode == SDL_SCANCODE_GRAVE)
            events.backquote_pressed = true;
    }
    return events;
}

void window_size_in_pixels(i32* width, i32* height)
{
    // SDL fills in 0s if it fails, which only happens without a window. A
    // failure isn't logged, since this runs every frame.
    SDL_GetWindowSizeInPixels(window, width, height);
}

void present()
{
    // OpenGL draws into a back buffer, a picture the screen doesn't show,
    // while the window shows the front one. Swapping hands the finished back
    // buffer to the display server and gives OpenGL a free one for the next
    // frame, so you never see a frame half drawn.
    //
    // A failure isn't logged: it would repeat every frame, and the window
    // would only keep its last picture.
    SDL_GL_SwapWindow(window);
}

GlFunction gl_function(const char* name)
{
    // SDL asks the driver, through EGL on Wayland or GLX on X11. Unlike the
    // other functions here, a missing function isn't logged: glad asks for
    // every function in OpenGL 4.6 and checks the version itself.
    return SDL_GL_GetProcAddress(name);
}

// ---- The clock --------------------------------------------------------------

i64 clock_ns()
{
    // On Linux, SDL calls clock_gettime with CLOCK_MONOTONIC_RAW: a clock
    // that only counts up (monotonic), straight from the hardware (raw), so
    // nothing adjusts it, not even the small corrections NTP makes to keep
    // the system's time right.
    return (i64)SDL_GetTicksNS();
}

// ---- Memory, files and libraries --------------------------------------------

void* reserve_memory(i64 size)
{
    // SDL has nothing like this, so it's the one place momo calls the OS
    // directly. mmap hands out whole pages of zeros. It's POSIX; a Windows
    // port would call VirtualAlloc.
    void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        LOG_ERROR("can't reserve %lld MB of memory", size / MB);
        return nullptr;
    }
    return memory;
}

void release_memory(void* memory, i64 size)
{
    munmap(memory, size);
}

const char* executable_folder()
{
    // On Linux, SDL reads the link /proc/self/exe, as chapter 02 did.
    const char* folder = SDL_GetBasePath();
    if (!folder)
        LOG_ERROR("can't find the executable's folder: %s", SDL_GetError());
    return folder;
}

i64 file_time(const char* path)
{
    // On Linux, SDL calls stat, as chapter 06 did. SDL_Time counts
    // nanoseconds since 1970.
    SDL_PathInfo info = {};
    if (!SDL_GetPathInfo(path, &info))
        return 0;
    return info.modify_time;
}

void* read_file(const char* path, i64* size)
{
    // SDL asks the file for its size, mallocs that much plus one byte, reads
    // it all in, and puts a 0 in the extra byte.
    size_t file_size = 0;
    void* data = SDL_LoadFile(path, &file_size);
    if (!data) {
        LOG_ERROR("%s", SDL_GetError());
        return nullptr;
    }
    *size = (i64)file_size;
    return data;
}

void free_file(void* data)
{
    SDL_free(data);
}

bool write_png(const char* path, i32 width, i32 height, const u8* rgb)
{
    // A surface is SDL's picture in memory. SDL_CreateSurfaceFrom wraps
    // pixels that are already there instead of copying them: RGB24 is 3
    // bytes a pixel, red first, and each row is width * 3 bytes after the
    // last. SDL takes the pixels as non-const, since a surface can be drawn
    // on, but saving only reads them.
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGB24, (void*)rgb, width * 3);
    if (!surface) {
        LOG_ERROR("%s", SDL_GetError());
        return false;
    }

    // SDL_SavePNG is new in SDL 3.4: it compresses the pixels and writes
    // the file. Its error says which file and why, like SDL_LoadFile's.
    bool saved = SDL_SavePNG(surface, path);
    if (!saved)
        LOG_ERROR("%s", SDL_GetError());
    SDL_DestroySurface(surface); // the surface only, not the pixels it wrapped
    return saved;
}

void* open_library(const char* path)
{
    // On Linux, SDL calls dlopen with RTLD_NOW, so every engine function the
    // library calls is looked up here, and a missing one fails now, not in
    // the middle of a frame (chapter 02).
    void* library = SDL_LoadObject(path);
    if (!library)
        LOG_ERROR("%s", SDL_GetError());
    return library;
}

void* find_function(void* library, const char* name)
{
    void* function = (void*)SDL_LoadFunction((SDL_SharedObject*)library, name);
    if (!function)
        LOG_ERROR("%s", SDL_GetError());
    return function;
}

void close_library(void* library)
{
    SDL_UnloadObject((SDL_SharedObject*)library);
}

} // namespace platform
