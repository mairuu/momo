// collision.h: moving boxes through a tilemap, one pixel at a time, so they
// stop at its walls. Chapter 18 explains it.
#pragma once

#include "base.h"
#include "fx.h"
#include "tilemap.h"

// The tiles that stop a moving box. The engine keeps tiles as numbers, and
// only the game knows which numbers are walls, so the game says so with
// is_solid, which is true for every tile a box can't go into.
//
// is_solid is code in game.so, which a reload loads at a new address. So the
// game fills a SolidTiles in each update and lets it go at the end: kept in
// GameState, it would point at code that's gone after the next reload.
struct SolidTiles {
    // The map's tiles aren't copied, only the pointer to them, as with a Str.
    // Outside the map, every tile is tile 0 (chapter 17).
    Tilemap map;
    i32 tile_size; // pixels on each side of a tile; more than 0
    bool (*is_solid)(u8 tile);
};

namespace collision {

// Whether rect overlaps a solid tile. rect is in pixels, from the map's top
// left corner, and may be partly or wholly outside the map.
MOMO_API bool overlaps(const SolidTiles& solids, Rect rect);

// Moves a box, w pixels wide and h high with its top left corner at *pos, dx
// pixels to the right (left if dx is negative), and stops it at the first
// solid tile on the way.
//
// The box moves one whole pixel at a time, from the pixel it's drawn at,
// fx::floor(pos->x), to the one it would be drawn at after the move, and
// checks each pixel before it moves there. If none is blocked, dx is added
// to pos->x, fraction and all, and move_x returns false. If one is blocked,
// the box stops at the pixel before it, flush against the tile: pos->x
// becomes that whole pixel, its fraction dropped, and move_x returns true.
//
// Each step checks the whole box, so a box that starts out overlapping a
// solid tile is blocked on its first step, unless that step clears it, and
// stays where it is.
MOMO_API bool move_x(const SolidTiles& solids, Fx2* pos, i32 w, i32 h, Fx dx);

// The same, dy pixels down (up if dy is negative).
MOMO_API bool move_y(const SolidTiles& solids, Fx2* pos, i32 w, i32 h, Fx dy);

} // namespace collision
