// screen.h: where the 256x240 screen goes in a window of any size. Chapter 11
// explains it.
#pragma once

#include "base.h"
#include "renderer.h"

// The renderer draws the game's screen at its own size, SCREEN_WIDTH by
// SCREEN_HEIGHT, then copies it into the window scaled up by a whole number,
// so that every screen pixel becomes a square of the same number of window
// pixels. Whatever the window has left over around it stays black: the bars.
// Like the batch, this never calls OpenGL, so the tests can check it.

// The screen, fitted into a window: how many window pixels each screen pixel
// covers across and down, and where the scaled screen goes, in window pixels
// from the window's top left corner.
struct ScreenFit {
    i32 scale;
    Rect rect;
};

namespace screen {

// The largest whole number scale at which the screen fits in a window of
// window_width by window_height pixels, and the screen centered there. A
// window too small for even one screen pixel per window pixel still gets
// scale 1: the screen stays centered, and its edges are cut off.
ScreenFit fit(i32 window_width, i32 window_height);

} // namespace screen
