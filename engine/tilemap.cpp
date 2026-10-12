// tilemap.cpp: grids of tiles, read from text. Chapter 17 explains them.
#include "tilemap.h"

#include "logger.h"

namespace tilemap {

Tilemap parse(Arena* arena, Str text, Str chars, const char* name)
{
    // A tile is a u8, so there can be at most 256 kinds.
    ASSERT(chars.len >= 1 && chars.len <= 256);

    // First, measure the map and check every line, so that a map with a
    // mistake pushes nothing. Messages count lines and columns from 1, as
    // editors do. Rows and columns count from 0.
    i32 columns = 0;
    i32 rows = 0;
    Str rest = text;
    while (rest.len > 0) {
        Str line = str::next_line(&rest);
        if (rows == 0)
            columns = line.len;
        if (line.len != columns) {
            LOG_ERROR("%s, line %d: %d tiles wide, but line 1 is %d", name, rows + 1, line.len,
                      columns);
            return {};
        }
        for (i32 column = 0; column < columns; column++) {
            char c = line.data[column];
            if (str::find(chars, c) != -1)
                continue;
            // A char that doesn't print, such as the '\r' of a Windows line
            // ending, would garble the message, so it's shown by its number.
            if (c >= ' ' && c <= '~')
                LOG_ERROR("%s, line %d, column %d: '%c' isn't one of the tiles \"%.*s\"", name,
                          rows + 1, column + 1, c, chars.len, chars.data);
            else
                LOG_ERROR("%s, line %d, column %d: char %d isn't one of the tiles \"%.*s\"", name,
                          rows + 1, column + 1, (u8)c, chars.len, chars.data);
            return {};
        }
        rows++;
    }
    if (columns == 0) {
        LOG_ERROR("%s has no tiles", name);
        return {};
    }

    // Then read it again, and turn each char into its tile: its place in
    // chars.
    u8* tiles = arena::push_array<u8>(arena, columns * rows);
    rest = text;
    for (i32 row = 0; row < rows; row++) {
        Str line = str::next_line(&rest);
        for (i32 column = 0; column < columns; column++)
            tiles[row * columns + column] = (u8)str::find(chars, line.data[column]);
    }
    return { .columns = columns, .rows = rows, .tiles = tiles };
}

u8 at(const Tilemap& map, i32 column, i32 row)
{
    if (column < 0 || column >= map.columns || row < 0 || row >= map.rows)
        return 0;
    return map.tiles[row * map.columns + column];
}

} // namespace tilemap
