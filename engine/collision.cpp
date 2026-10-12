// collision.cpp: moving boxes through a tilemap, one pixel at a time.
// Chapter 18 explains it.
#include "collision.h"

namespace collision {

// a / b, rounded down: the tile that pixel a is in, for tiles b pixels wide.
// C++'s / rounds toward zero instead, so -1 / 16 is 0, which would put the
// pixel just left of the map in its first column. Rounded down, it's -1, the
// column outside the map. b must be more than 0.
static i32 floor_div(i32 a, i32 b)
{
    i32 q = a / b;
    if (a % b != 0 && a < 0)
        q--;
    return q;
}

bool overlaps(const SolidTiles& solids, Rect rect)
{
    ASSERT(solids.tile_size > 0);
    ASSERT(rect.w > 0 && rect.h > 0);

    // The tiles under the rectangle's first and last pixels, across and down.
    i32 first_column = floor_div(rect.x, solids.tile_size);
    i32 last_column = floor_div(rect.x + rect.w - 1, solids.tile_size);
    i32 first_row = floor_div(rect.y, solids.tile_size);
    i32 last_row = floor_div(rect.y + rect.h - 1, solids.tile_size);
    for (i32 row = first_row; row <= last_row; row++) {
        for (i32 column = first_column; column <= last_column; column++) {
            if (solids.is_solid(tilemap::at(solids.map, column, row)))
                return true;
        }
    }
    return false;
}

bool move_x(const SolidTiles& solids, Fx2* pos, i32 w, i32 h, Fx dx)
{
    i32 x = fx::floor(pos->x); // where the box is drawn now
    i32 y = fx::floor(pos->y);
    i32 to = fx::floor(pos->x + dx); // and where it would be drawn after
    i32 step = to > x ? 1 : -1;
    while (x != to) {
        if (overlaps(solids, { .x = x + step, .y = y, .w = w, .h = h })) {
            pos->x = fx::from_int(x);
            return true;
        }
        x += step;
    }
    pos->x += dx;
    return false;
}

bool move_y(const SolidTiles& solids, Fx2* pos, i32 w, i32 h, Fx dy)
{
    i32 x = fx::floor(pos->x);
    i32 y = fx::floor(pos->y);
    i32 to = fx::floor(pos->y + dy);
    i32 step = to > y ? 1 : -1;
    while (y != to) {
        if (overlaps(solids, { .x = x, .y = y + step, .w = w, .h = h })) {
            pos->y = fx::from_int(y);
            return true;
        }
        y += step;
    }
    pos->y += dy;
    return false;
}

} // namespace collision
