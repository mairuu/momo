// tilemap_test.cpp: tests for engine/tilemap.h.
#include "test.h"

#include "tilemap.h"

#include <string.h>

// The tests' tiles, as a game would have them: '.' is tile 0, '#' tile 1 and
// '|' tile 2. The maps that tilemap::parse refuses log why, so their errors
// show up in the output.
constexpr Str CHARS = STR(".#|");

static void a_zeroed_tilemap_has_no_tiles()
{
    Tilemap map = {};
    CHECK_EQ(map.columns, 0);
    CHECK_EQ(map.rows, 0);
    CHECK_EQ(tilemap::at(map, 0, 0), 0);
}

static void each_char_becomes_its_place_in_chars()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    Tilemap map = tilemap::parse(&a, STR(".#|\n"), CHARS, "room.txt");
    CHECK_EQ(map.columns, 3);
    CHECK_EQ(map.rows, 1);
    CHECK_EQ(tilemap::at(map, 0, 0), 0);
    CHECK_EQ(tilemap::at(map, 1, 0), 1);
    CHECK_EQ(tilemap::at(map, 2, 0), 2);
}

static void rows_count_down_from_the_top()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    // A wall on the left, and a floor.
    Tilemap map = tilemap::parse(&a, STR("#..\n#..\n###\n"), CHARS, "room.txt");
    CHECK_EQ(map.columns, 3);
    CHECK_EQ(map.rows, 3);
    CHECK_EQ(tilemap::at(map, 0, 0), 1);
    CHECK_EQ(tilemap::at(map, 1, 0), 0);
    CHECK_EQ(tilemap::at(map, 1, 2), 1);
    CHECK_EQ(tilemap::at(map, 2, 1), 0);
}

static void the_tiles_go_in_the_arena_one_byte_each()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    Tilemap map = tilemap::parse(&a, STR("#..\n###\n"), CHARS, "room.txt");
    CHECK(map.tiles == memory);
    CHECK_EQ(a.used, 6);
}

static void the_text_isnt_needed_afterwards()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    // The text is in a buffer of its own, which is wiped once it's parsed.
    char text[] = "#.\n.#\n";
    Tilemap map = tilemap::parse(&a, { .data = text, .len = (i32)strlen(text) }, CHARS, "room.txt");
    memset(text, 0, sizeof(text));
    CHECK_EQ(tilemap::at(map, 0, 0), 1);
    CHECK_EQ(tilemap::at(map, 1, 0), 0);
    CHECK_EQ(tilemap::at(map, 1, 1), 1);
}

static void the_last_newline_is_optional()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    Tilemap with = tilemap::parse(&a, STR("#.\n.#\n"), CHARS, "room.txt");
    Tilemap without = tilemap::parse(&a, STR("#.\n.#"), CHARS, "room.txt");
    CHECK_EQ(with.rows, 2);
    CHECK_EQ(without.rows, 2);
    CHECK_EQ(memcmp(with.tiles, without.tiles, 4), 0);
}

static void outside_the_map_is_tile_0()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    Tilemap map = tilemap::parse(&a, STR("##\n##\n"), CHARS, "room.txt");
    CHECK_EQ(tilemap::at(map, -1, 0), 0);
    CHECK_EQ(tilemap::at(map, 2, 0), 0);
    CHECK_EQ(tilemap::at(map, 0, -1), 0);
    CHECK_EQ(tilemap::at(map, 0, 2), 0);
    CHECK_EQ(tilemap::at(map, 1, 1), 1); // the last tile inside
}

static void a_line_of_another_width_pushes_nothing()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    // The second line is one tile short.
    Tilemap map = tilemap::parse(&a, STR("###\n##\n###\n"), CHARS, "room.txt");
    CHECK_EQ(map.columns, 0);
    CHECK_EQ(map.rows, 0);
    CHECK(map.tiles == nullptr);
    CHECK_EQ(a.used, 0);
}

static void a_char_that_isnt_a_tile_pushes_nothing()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    // An 'x' in the last line, and a '\r' from a Windows line ending.
    Tilemap typo = tilemap::parse(&a, STR("###\n#x#\n"), CHARS, "room.txt");
    Tilemap windows = tilemap::parse(&a, STR("###\r\n###\r\n"), CHARS, "room.txt");
    CHECK_EQ(typo.rows, 0);
    CHECK_EQ(windows.rows, 0);
    CHECK_EQ(a.used, 0);
}

static void no_text_is_no_map()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    // A file that couldn't be read is an empty Str, and so is an empty file.
    CHECK_EQ(tilemap::parse(&a, Str{}, CHARS, "room.txt").rows, 0);
    CHECK_EQ(tilemap::parse(&a, STR("\n\n"), CHARS, "room.txt").rows, 0);
    CHECK_EQ(a.used, 0);
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(a_zeroed_tilemap_has_no_tiles);
    RUN(each_char_becomes_its_place_in_chars);
    RUN(rows_count_down_from_the_top);
    RUN(the_tiles_go_in_the_arena_one_byte_each);
    RUN(the_text_isnt_needed_afterwards);
    RUN(the_last_newline_is_optional);
    RUN(outside_the_map_is_tile_0);
    RUN(a_line_of_another_width_pushes_nothing);
    RUN(a_char_that_isnt_a_tile_pushes_nothing);
    RUN(no_text_is_no_map);
    return test::report();
}
