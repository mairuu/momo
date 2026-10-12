// tilemap.h: grids of tiles, read from text. Chapter 17 explains them.
#pragma once

#include "base.h"
#include "arena.h"
#include "str.h"

// A grid of tiles, columns wide and rows high. Each tile is a number, and the
// game decides what each number means: the engine only keeps them. Tiles go
// row by row, top row first, the way the text they came from reads.
//
// A zeroed Tilemap has no tiles at all, and every tile asked for is 0.
struct Tilemap {
    i32 columns;
    i32 rows;
    // columns * rows tile numbers, in memory pushed from an arena: row 0's
    // tiles, then row 1's. Nothing changes a tile once it's read, so a
    // render that's handed a const GameState can't change one either.
    const u8* tiles;
};

namespace tilemap {

// Reads a map written as text: one line for each row of tiles, and one char
// for each tile. The chars that may appear are chars, and tile n is the char
// at chars.data[n], so with chars ".#", '.' is tile 0 and '#' is tile 1.
//
// Every line must be as long as the first. If one isn't, or a char isn't in
// chars, or there are no tiles at all, logs where and returns a zeroed
// Tilemap, with nothing pushed. name, such as the file the text came from, is
// only for those messages. Otherwise the tiles go in memory pushed from
// arena, and the text isn't needed afterwards.
MOMO_API Tilemap parse(Arena* arena, Str text, Str chars, const char* name);

// The tile at column, row, counted from the top left. Outside the map, it's
// tile 0, as if the map had empty tiles all around it, when tile 0 means
// empty.
MOMO_API u8 at(const Tilemap& map, i32 column, i32 row);

} // namespace tilemap
