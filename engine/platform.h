// platform.h: everything the engine needs from the operating system: a
// window and its events, memory, files and libraries. Chapter 07 explains it.
#pragma once

#include "base.h"

// The rest of the engine reaches the OS only through these functions, and
// only platform.cpp includes SDL or an OS header. A port to another OS, or
// from SDL to something else, rewrites platform.cpp and nothing else.
//
// Nothing here is MOMO_API. The game never calls the OS: it asks the engine.
//
// A function that fails logs why and returns false or null. Paths are C
// strings, with a 0 at the end, because every OS call wants one. A Str made
// by str::fmt has one, so pass its .data.
namespace platform {

// ---- The window -------------------------------------------------------------

// Starts SDL and opens a window width by height in size.
bool init(const char* title, i32 width, i32 height);

// Closes the window and stops SDL.
void shutdown();

// Handles every event that arrived since the last call. Returns false once
// momo should quit: the window was closed, or Ctrl+C was pressed in the
// terminal.
bool handle_events();

// Shows the frame in the window. Until chapter 08 draws with OpenGL, the
// frame is one solid color.
void present();

// ---- Memory, files and libraries --------------------------------------------

// These work before init and after shutdown.

// size bytes of zeroed memory, at an address that's a multiple of the page
// size (4 KB). The OS only gives a page real memory when it's first touched.
void* reserve_memory(i64 size);

// Gives back memory from reserve_memory. size must be the size reserved.
void release_memory(void* memory, i64 size);

// The folder the executable is in, ending with a '/'. The text belongs to
// SDL, so copy what you need to keep.
const char* executable_folder();

// When the file at path was last modified, in nanoseconds since 1970, or 0
// if there's no file there.
i64 file_time(const char* path);

// Loads the shared library at path. Returns a handle to it, or null.
void* open_library(const char* path);

// The address of the function called name in a library open_library opened,
// or null.
void* find_function(void* library, const char* name);

// Unloads a library. Every address in it is gone afterwards.
void close_library(void* library);

// Waits for about this many milliseconds.
void sleep_ms(i32 milliseconds);

} // namespace platform
