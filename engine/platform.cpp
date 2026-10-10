// platform.cpp: the engine's one connection to the operating system, mostly
// through SDL3. Chapter 07 explains it.
#include "platform.h"
#include "logger.h"

#include <SDL3/SDL.h>
#include <sys/mman.h>

namespace platform {

// The engine never reloads, so this can be a file-scope variable, like the
// engine's own state in momo.cpp.
static SDL_Window* window;

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

    // Not resizable yet. Chapter 11 scales the picture to fit any size.
    window = SDL_CreateWindow(title, width, height, 0);
    if (!window) {
        LOG_ERROR("can't open a window: %s", SDL_GetError());
        return false;
    }

    i32 version = SDL_GetVersion();
    LOG_INFO("SDL %d.%d.%d, video driver %s, window %dx%d", SDL_VERSIONNUM_MAJOR(version),
             SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version),
             SDL_GetCurrentVideoDriver(), width, height);
    return true;
}

void shutdown()
{
    SDL_DestroyWindow(window);
    window = nullptr;
    SDL_Quit();
}

bool handle_events()
{
    // SDL collects events in a queue as they happen, and SDL_PollEvent takes
    // them out one at a time until the queue is empty. Closing the window
    // and Ctrl+C both end up as SDL_EVENT_QUIT. Every other event is
    // ignored for now.
    SDL_Event event = {};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            LOG_INFO("quit requested");
            return false; // the rest of the queue doesn't matter now
        }
    }
    return true;
}

void present()
{
    // A Wayland compositor shows a window only once the window has given it
    // a picture, so without this there'd be nothing to see. The surface is
    // the window's picture in memory. SDL makes a new one when the window
    // changes size, so it's asked for again every frame.
    //
    // A failure here isn't logged: it would repeat 60 times a second, and
    // the window would only keep its last picture. Chapter 08 replaces all
    // of this with OpenGL.
    SDL_Surface* surface = SDL_GetWindowSurface(window);
    if (!surface)
        return;
    SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGB(surface, 0x18, 0x20, 0x48));
    SDL_UpdateWindowSurface(window);
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

void sleep_ms(i32 milliseconds)
{
    SDL_Delay((u32)milliseconds);
}

} // namespace platform
