// renderer.h: everything momo draws goes through here, on OpenGL 4.6.
// Chapter 08 explains it.
#pragma once

#include "base.h"

// Only renderer.cpp calls OpenGL. A port to another graphics API, such as
// SDL_GPU for macOS, rewrites renderer.cpp and nothing else.
//
// Nothing here is MOMO_API yet. The game gets drawing functions in chapter 10.
namespace renderer {

// Loads OpenGL's functions and turns on its debug messages. Needs the window
// from platform::init.
bool init();

// Starts a frame by clearing the whole window to one color.
void begin_frame();

} // namespace renderer
