// font.h: momo's 8x8 font, and the picture the renderer makes its texture
// from. Chapter 12 explains it.
#pragma once

#include "base.h"
#include "renderer.h"

// The font has the printable ASCII chars, ' ' to '~', 95 of them. Any other
// char draws as '?'.
constexpr i32 FIRST_CHAR = ' ';
constexpr i32 LAST_CHAR = '~';

// The font's picture holds the glyphs in ASCII order, 16 to a row: ' ' at the
// top left, '/' at the right end of the first row, '0' at the start of the
// second, and '~' on the sixth and last.
constexpr i32 FONT_COLUMNS = 16;
constexpr i32 FONT_ROWS = 6;
constexpr i32 FONT_WIDTH = FONT_COLUMNS * GLYPH_SIZE; // 128
constexpr i32 FONT_HEIGHT = FONT_ROWS * GLYPH_SIZE;   // 48

// Like the batch and the screen's fit, the font never calls OpenGL, so the
// tests can read what it makes.
namespace font {

// Where c's glyph is in the font's picture.
Rect glyph(char c);

// Fills pixels, which has room for FONT_WIDTH x FONT_HEIGHT colors, top row
// first, with the font's picture: white wherever a glyph has a pixel, and
// see-through everywhere else.
void fill_pixels(Color* pixels);

} // namespace font
