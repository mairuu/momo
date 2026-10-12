// renderer.h: everything momo draws goes through here, on OpenGL 4.6.
// Chapter 08 explains how it starts, chapter 10 how it draws, chapter 11 how
// the picture fits the window, chapter 12 textures and text, chapter 13 the
// overlay and reading the screen back, and chapter 19 debug boxes.
#pragma once

#include "base.h"
#include "arena.h"
#include "str.h"

// Only renderer.cpp calls OpenGL. A port to another graphics API, such as
// SDL_GPU for macOS, rewrites renderer.cpp and nothing else.

// ---- The screen -------------------------------------------------------------

// The game draws on a screen 256 pixels wide and 240 high, the NES's: 16 by
// 15 tiles of 16 pixels. x counts from the left edge and y from the top, so
// y grows downward. The screen is a picture of its own, always this size,
// which the renderer scales up by a whole number to fit the window, with
// black bars around it (chapter 11). The game never sees the window's size.
constexpr i32 SCREEN_WIDTH = 256;
constexpr i32 SCREEN_HEIGHT = 240;

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

// The most rectangles one frame can draw, sprites and letters included. More
// is an ASSERT.
constexpr i32 MAX_RECTS = 4096;

// The most boxes debug_box can draw in a frame, apart from MAX_RECTS. More is
// an ASSERT, whether the boxes are shown or not.
constexpr i32 MAX_DEBUG_BOXES = 512;

// ---- Textures ---------------------------------------------------------------

// A picture on the GPU that sprites are cut from, as a number the renderer
// gives out: an index into its table of textures. A texture lasts for the
// whole run, so the game can keep one in GameState, like any other number.
//
// The zero texture is one white pixel, which is what draw_rect draws with.
// A texture that fails to load is the zero texture too, so its sprites come
// out as white rectangles.
struct Texture {
    i32 index;
};

// The most textures momo can hold, the two the renderer makes for itself
// included. More is an ASSERT.
constexpr i32 MAX_TEXTURES = 64;

// draw_text's chars are 8x8 pixels each.
constexpr i32 GLYPH_SIZE = 8;

// ---- Stats ------------------------------------------------------------------

// What the game drew in a frame: its rectangles, sprites and letters
// included, and the draw calls that drew them. Its debug boxes and the
// overlay's own rectangles aren't counted.
struct RenderStats {
    i32 rects;
    i32 draw_calls;
};

namespace renderer {

// ---- For the game -----------------------------------------------------------

// Loads the PNG file at path, relative to the executable's folder, such as
// "assets/bat.png", and returns its texture. Each file is loaded once:
// asking for the same path again returns the same texture. If the file can't
// be loaded, logs why and returns the zero texture.
MOMO_API Texture load_texture(const char* path);

// The drawing functions work only inside the game's render, and draw on a
// layer, this frame only: the renderer forgets everything once the frame is
// drawn, so the game draws its whole picture again every frame.

// Draws a rectangle of one color.
MOMO_API void draw_rect(Rect rect, Color color, i32 layer);

// Draws the part of texture inside source, which is in the texture's own
// pixels, counted from its top left corner. The sprite's top left corner goes
// at x, y on the screen, and it's the same size as source.
MOMO_API void draw_sprite(Texture texture, Rect source, i32 x, i32 y, i32 layer);

// Draws text in momo's 8x8 font, one char after another to the right of x, y,
// in the given color. The font has the printable ASCII chars, ' ' to '~', and
// draws any other char as '?'.
MOMO_API void draw_text(Str text, i32 x, i32 y, Color color, i32 layer);

// Draws a debug box: rect's outline, 1 pixel thick and inside it, in color,
// over every layer the game draws and under the debug overlay. It shows what
// the game's code sees, such as the boxes collision checks (chapter 19).
//
// The engine shows the boxes only while F1 or --boxes asks it to, and the
// game never knows whether they're shown: debug_box does the same work
// either way, so the game can't play any differently with them on. It works
// only inside the game's render, like the rest, and its boxes aren't counted
// in RenderStats.
MOMO_API void debug_box(Rect rect, Color color);

// An opaque color, written as hex the way color pickers write it:
// rgb(0x8e44ad) is a purple.
constexpr Color rgb(u32 hex)
{
    return { .r = (u8)(hex >> 16), .g = (u8)(hex >> 8), .b = (u8)hex, .a = 255 };
}

// ---- For the engine ---------------------------------------------------------

// Loads OpenGL's functions, turns on its debug messages, and makes the
// shaders, buffers and textures the renderer draws with. Its own memory
// comes from arena, for the rest of the run. Needs the window from
// platform::init.
bool init(Arena* arena);

// Deletes what init made in OpenGL, and every texture. Call it before
// platform::shutdown, while there's still a context.
void shutdown();

// Starts a frame: clears the screen to one color and empties the batches.
// What's drawn next goes in the game's batch.
void begin_frame();

// Called after the game's render: what's drawn from here until end_frame
// goes in the overlay's batch instead, which is drawn after the game's and
// its debug boxes, over all of them. Its room is its own, apart from the
// game's MAX_RECTS.
void begin_overlay();

// Draws the frame on the screen: the game's batch, then its debug boxes if
// boxes_shown, then the overlay's batch, with one draw call per run of
// rectangles that share a texture. Then copies the screen into the window at
// the largest whole number scale that fits, with black around it.
void end_frame(bool boxes_shown);

// What the game drew in the last frame that ended.
RenderStats last_frame_stats();

// Copies the screen as the last end_frame left it into rgb, which has room
// for SCREEN_WIDTH * SCREEN_HEIGHT pixels of 3 bytes, red, green and blue,
// top row first. It waits for the GPU to finish drawing the frame.
void read_screen(u8* rgb);

} // namespace renderer
