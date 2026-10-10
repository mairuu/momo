// platform.h: everything the engine needs from the operating system: a
// window, its events and its OpenGL context, memory, files, libraries and a
// clock. Chapter 07 explains it, chapter 08 adds OpenGL, and chapter 09 the
// clock and vsync.
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

// Starts SDL and opens a window width by height in size, with an OpenGL 4.6
// context ready to draw into it (chapter 08), and vsync on (chapter 09).
bool init(const char* title, i32 width, i32 height);

// Closes the OpenGL context and the window, and stops SDL.
void shutdown();

// Handles every event that arrived since the last call. Returns false once
// momo should quit: the window was closed, or Ctrl+C was pressed in the
// terminal.
bool handle_events();

// Shows the frame OpenGL drew in the window, and gives OpenGL another
// picture to draw the next frame into. With vsync, it first waits until the
// display is ready for a new picture.
void present();

// The type of an OpenGL function as SDL hands it out, before it's cast to
// its real type. It's the same as glad's GLADapiproc, so gl_function can be
// passed straight to gladLoadGL.
using GlFunction = void (*)();

// The address of the OpenGL function called name, such as "glClear", or
// null if the driver doesn't have it. Works only after init.
GlFunction gl_function(const char* name);

// ---- The clock --------------------------------------------------------------

// The time now, in nanoseconds since SDL first read its clock, which in momo
// is during init. The clock never goes backwards, even when the system's
// time is set, so the difference between two readings is the time that
// passed between them. Use it between init and shutdown only: after
// shutdown, SDL's clock starts again from 0.
i64 clock_ns();

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

} // namespace platform
