// renderer.h: everything momo draws goes through here, on OpenGL 4.6.
// Chapter 08 explains how it starts, and chapter 10 how it draws.
#pragma once

#include "base.h"
#include "arena.h"

// Only renderer.cpp calls OpenGL. A port to another graphics API, such as
// SDL_GPU for macOS, rewrites renderer.cpp and nothing else.

// ---- The screen -------------------------------------------------------------

// The game draws on a screen 256 pixels wide and 240 high, the NES's: 16 by
// 15 tiles of 16 pixels. x counts from the left edge and y from the top, so
// y grows downward. The window is three times that size, so every screen
// pixel is 3x3 of the window's. Chapter 11 fits the screen to any window.
constexpr i32 SCREEN_WIDTH = 256;
constexpr i32 SCREEN_HEIGHT = 240;

// A rectangle on the screen, in whole pixels: its top left corner, and its
// size.
struct Rect {
    i32 x;
    i32 y;
    i32 w;
    i32 h;
};

// Red, green, blue and alpha, from 0 to 255. Alpha is how much the color
// covers what's behind it: 255 hides it, 0 leaves it as it was, so a zeroed
// Color draws nothing.
struct Color {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

// What gets drawn over what. Every rectangle goes on a layer, numbered from 0
// to MAX_LAYERS - 1, and layers are drawn in that order, back to front.
// Inside a layer, whatever was drawn first is drawn first. The engine gives
// the numbers no meaning: the game names them, with an enum of its own.
constexpr i32 MAX_LAYERS = 8;

// The most rectangles one frame can draw. More is an ASSERT.
constexpr i32 MAX_RECTS = 4096;

namespace renderer {

// ---- For the game -----------------------------------------------------------

// Draws a rectangle of one color on a layer, this frame only. Works only
// inside the game's render: the renderer forgets everything once the frame
// is drawn, so the game draws its whole picture again every frame.
MOMO_API void draw_rect(Rect rect, Color color, i32 layer);

// An opaque color, written as hex the way color pickers write it:
// rgb(0x8e44ad) is a purple.
constexpr Color rgb(u32 hex)
{
    return { .r = (u8)(hex >> 16), .g = (u8)(hex >> 8), .b = (u8)hex, .a = 255 };
}

// ---- For the engine ---------------------------------------------------------

// Loads OpenGL's functions, turns on its debug messages, and makes the
// shaders and buffers the renderer draws with. Its own memory comes from
// arena, for the rest of the run. Needs the window from platform::init.
bool init(Arena* arena);

// Deletes what init made in OpenGL. Call it before platform::shutdown, while
// there's still a context.
void shutdown();

// Starts a frame: clears the window to one color and empties the batch.
void begin_frame();

// Draws everything drawn since begin_frame, in one draw call.
void end_frame();

} // namespace renderer
