// collision_test.cpp: tests for engine/collision.h.
#include "test.h"

#include "collision.h"

// The tests' tiles: '.' is tile 0, '#' tile 1 and '|' tile 2, as in the game.
// Only '#' is solid, so '|' shows that the game decides which tiles stop a
// box, not the engine.
constexpr Str CHARS = STR(".#|");

static bool wall_is_solid(u8 tile)
{
    return tile == 1;
}

// The tests' maps. Each test reads its own over the last one's.
alignas(16) static u8 memory[256];

// The tiles of map that stop a box, for tiles tile_size pixels wide.
static SolidTiles solid_tiles(Str map, i32 tile_size)
{
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    return { .map = tilemap::parse(&a, map, CHARS, "test map"),
             .tile_size = tile_size,
             .is_solid = wall_is_solid };
}

// An Fx2 at whole pixels x, y.
static Fx2 at(i32 x, i32 y)
{
    return { .x = fx::from_int(x), .y = fx::from_int(y) };
}

// ---- overlaps ---------------------------------------------------------------

static void a_rect_overlaps_a_wall_by_one_pixel()
{
    // A wall at x 32 to 47.
    SolidTiles solids = solid_tiles(STR("..#.\n"), 16);
    CHECK(!collision::overlaps(solids, { .x = 16, .y = 0, .w = 16, .h = 16 }));
    CHECK(collision::overlaps(solids, { .x = 17, .y = 0, .w = 16, .h = 16 }));
    CHECK(collision::overlaps(solids, { .x = 47, .y = 0, .w = 1, .h = 1 }));
    CHECK(!collision::overlaps(solids, { .x = 48, .y = 0, .w = 16, .h = 16 }));
}

static void the_game_decides_which_tiles_are_solid()
{
    // '|' is tile 2, which wall_is_solid says isn't solid.
    SolidTiles solids = solid_tiles(STR("||||\n"), 16);
    CHECK(!collision::overlaps(solids, { .x = 0, .y = 0, .w = 64, .h = 16 }));
}

static void outside_the_map_nothing_is_solid()
{
    SolidTiles solids = solid_tiles(STR("#\n"), 16);
    CHECK(!collision::overlaps(solids, { .x = -100, .y = -100, .w = 50, .h = 50 }));
    CHECK(!collision::overlaps(solids, { .x = 16, .y = 0, .w = 16, .h = 16 }));
    CHECK(!collision::overlaps(solids, { .x = 0, .y = 16, .w = 16, .h = 16 }));
}

static void left_of_the_map_rounds_down()
{
    // Pixel -1 is in column -1, outside the map, not in column 0. Rounding
    // toward zero, -1 / 16 would be 0, the wall.
    SolidTiles solids = solid_tiles(STR("#\n"), 16);
    CHECK(!collision::overlaps(solids, { .x = -16, .y = 0, .w = 16, .h = 16 }));
    CHECK(!collision::overlaps(solids, { .x = 0, .y = -16, .w = 16, .h = 16 }));
    CHECK(collision::overlaps(solids, { .x = -15, .y = 0, .w = 16, .h = 16 }));
    CHECK(collision::overlaps(solids, { .x = 0, .y = -15, .w = 16, .h = 16 }));
}

static void a_zeroed_map_is_all_empty()
{
    SolidTiles solids = { .map = {}, .tile_size = 16, .is_solid = wall_is_solid };
    CHECK(!collision::overlaps(solids, { .x = 0, .y = 0, .w = 16, .h = 16 }));
}

static void tiles_can_be_any_size()
{
    // Tiles 10 pixels wide, which isn't a power of two: the wall is at x 20
    // to 29 and y 0 to 9.
    SolidTiles solids = solid_tiles(STR("..#\n"), 10);
    CHECK(!collision::overlaps(solids, { .x = 10, .y = 0, .w = 10, .h = 10 }));
    CHECK(collision::overlaps(solids, { .x = 11, .y = 0, .w = 10, .h = 10 }));
    CHECK(!collision::overlaps(solids, { .x = 30, .y = 0, .w = 10, .h = 10 }));
    CHECK(collision::overlaps(solids, { .x = 29, .y = 9, .w = 1, .h = 1 }));
    CHECK(!collision::overlaps(solids, { .x = 29, .y = 10, .w = 1, .h = 1 }));
}

// ---- move_x and move_y ------------------------------------------------------

static void a_free_move_keeps_the_fraction()
{
    SolidTiles solids = solid_tiles(STR("....\n"), 16);
    Fx2 pos = at(0, 0);
    CHECK(!collision::move_x(solids, &pos, 16, 16, fx::from_double(1.375)));
    CHECK_EQ(pos.x.raw, fx::from_double(1.375).raw);
    CHECK(!collision::move_x(solids, &pos, 16, 16, fx::from_double(1.375)));
    CHECK_EQ(pos.x.raw, fx::from_double(2.75).raw);
    CHECK(!collision::move_y(solids, &pos, 16, 16, fx::from_double(0.5)));
    CHECK_EQ(pos.y.raw, fx::from_double(0.5).raw);
}

static void a_move_of_zero_goes_nowhere()
{
    SolidTiles solids = solid_tiles(STR("#\n"), 16);
    Fx2 pos = { .x = fx::from_double(16.5), .y = {} };
    CHECK(!collision::move_x(solids, &pos, 16, 16, {}));
    CHECK(!collision::move_y(solids, &pos, 16, 16, {}));
    CHECK_EQ(pos.x.raw, fx::from_double(16.5).raw);
}

static void stops_flush_against_a_wall_on_the_right()
{
    // A wall at x 48 to 63. A box 16 wide, flush against it, is at x 32.
    SolidTiles solids = solid_tiles(STR("...#\n"), 16);
    Fx2 pos = at(0, 0);
    CHECK(collision::move_x(solids, &pos, 16, 16, fx::from_int(40)));
    CHECK_EQ(pos.x.raw, fx::from_int(32).raw);
    // Pushing on, it stays.
    CHECK(collision::move_x(solids, &pos, 16, 16, fx::from_double(1.375)));
    CHECK_EQ(pos.x.raw, fx::from_int(32).raw);
}

static void stops_flush_against_a_wall_on_the_left()
{
    // A wall at x 0 to 15.
    SolidTiles solids = solid_tiles(STR("#...\n"), 16);
    Fx2 pos = at(40, 0);
    CHECK(collision::move_x(solids, &pos, 16, 16, fx::from_int(-40)));
    CHECK_EQ(pos.x.raw, fx::from_int(16).raw);
}

static void stops_flush_on_the_floor_and_under_the_ceiling()
{
    // A ceiling at y 0 to 15 and a floor at y 48 to 63.
    SolidTiles solids = solid_tiles(STR("#\n.\n.\n#\n"), 16);
    Fx2 pos = at(0, 20);
    CHECK(collision::move_y(solids, &pos, 16, 16, fx::from_int(40)));
    CHECK_EQ(pos.y.raw, fx::from_int(32).raw);
    CHECK(collision::move_y(solids, &pos, 16, 16, fx::from_int(-40)));
    CHECK_EQ(pos.y.raw, fx::from_int(16).raw);
}

static void a_hit_drops_the_fraction()
{
    // From 30.5, 3 more would be drawn at 33, which overlaps the wall at 48.
    // The box stops at 32, the last pixel that was free, and its half pixel
    // is gone: the stop is the wall's position, not the box's.
    SolidTiles solids = solid_tiles(STR("...#\n"), 16);
    Fx2 pos = { .x = fx::from_double(30.5), .y = {} };
    CHECK(collision::move_x(solids, &pos, 16, 16, fx::from_int(3)));
    CHECK_EQ(pos.x.raw, fx::from_int(32).raw);
}

static void a_move_within_a_pixel_checks_nothing()
{
    // A box standing on a floor at y 48, pulled down 0.25, 0.5 and 0.75, as
    // gravity pulls a player who stands still (chapter 20). The first two
    // moves leave the box drawn at 32, so no tile is checked, and they aren't
    // hits, though the floor is right under the box. The third would draw it
    // at 33, in the floor.
    SolidTiles solids = solid_tiles(STR(".\n.\n.\n#\n"), 16);
    Fx2 pos = at(0, 32);
    CHECK(!collision::move_y(solids, &pos, 16, 16, fx::from_double(0.25)));
    CHECK_EQ(pos.y.raw, fx::from_double(32.25).raw);
    CHECK(!collision::move_y(solids, &pos, 16, 16, fx::from_double(0.5)));
    CHECK_EQ(pos.y.raw, fx::from_double(32.75).raw);
    CHECK(collision::move_y(solids, &pos, 16, 16, fx::from_double(0.75)));
    CHECK_EQ(pos.y.raw, fx::from_int(32).raw);
}

static void never_overlaps_at_walking_speed()
{
    // MegaMan's 1.375 pixels a tick, into a wall, for 40 ticks.
    SolidTiles solids = solid_tiles(STR("...#\n"), 16);
    Fx2 pos = at(0, 0);
    for (i32 tick = 0; tick < 40; tick++) {
        collision::move_x(solids, &pos, 16, 16, fx::from_double(1.375));
        CHECK(!collision::overlaps(solids, { .x = fx::floor(pos.x), .y = 0, .w = 16, .h = 16 }));
    }
    CHECK_EQ(pos.x.raw, fx::from_int(32).raw);
}

static void cant_go_through_a_wall_however_fast()
{
    // 200 pixels in one move is more than 12 tiles, and a wall one tile thick
    // is in the way. Checking only where the box ends up would put it past
    // the wall.
    SolidTiles solids = solid_tiles(STR("....#.........\n"), 16);
    Fx2 pos = at(0, 0);
    CHECK(collision::move_x(solids, &pos, 16, 16, fx::from_int(200)));
    CHECK_EQ(pos.x.raw, fx::from_int(48).raw);
}

static void stops_flush_left_of_the_map()
{
    // The box comes from outside the map, where tiles are empty, and stops
    // at -16, its right edge at -1. Rounding pixels to tiles toward zero
    // would stop it a pixel early, at -17.
    SolidTiles solids = solid_tiles(STR("#\n"), 16);
    Fx2 pos = at(-40, 0);
    CHECK(collision::move_x(solids, &pos, 16, 16, fx::from_int(40)));
    CHECK_EQ(pos.x.raw, fx::from_int(-16).raw);
}

static void stops_flush_with_small_tiles()
{
    // Tiles 8 pixels wide: a wall at x 24 to 31, and a box 8 wide.
    SolidTiles solids = solid_tiles(STR("...#\n"), 8);
    Fx2 pos = at(0, 0);
    CHECK(collision::move_x(solids, &pos, 8, 8, fx::from_int(20)));
    CHECK_EQ(pos.x.raw, fx::from_int(16).raw);
}

static void a_hit_across_doesnt_stop_the_box_going_down()
{
    // The box moves right and down into a corner. move_x stops it at the
    // wall, and move_y still takes it down to the floor.
    SolidTiles solids = solid_tiles(STR("...#\n...#\n...#\n####\n"), 16);
    Fx2 pos = at(0, 0);
    CHECK(collision::move_x(solids, &pos, 16, 16, fx::from_int(40)));
    CHECK(collision::move_y(solids, &pos, 16, 16, fx::from_int(40)));
    CHECK_EQ(pos.x.raw, fx::from_int(32).raw);
    CHECK_EQ(pos.y.raw, fx::from_int(32).raw);
}

static void slides_along_the_floor()
{
    // A box standing on the floor touches it without overlapping it, so it
    // moves across freely while move_y stops it going down.
    SolidTiles solids = solid_tiles(STR("....\n####\n"), 16);
    Fx2 pos = at(0, 0);
    CHECK(!collision::move_x(solids, &pos, 16, 16, fx::from_int(20)));
    CHECK(collision::move_y(solids, &pos, 16, 16, fx::from_int(5)));
    CHECK_EQ(pos.x.raw, fx::from_int(20).raw);
    CHECK_EQ(pos.y.raw, fx::from_int(0).raw);
}

static void a_box_inside_a_wall_stays_put()
{
    // Every step from inside the wall overlaps it, so the first is blocked.
    SolidTiles solids = solid_tiles(STR("###\n"), 16);
    Fx2 pos = { .x = fx::from_double(16.5), .y = {} };
    CHECK(collision::move_x(solids, &pos, 16, 16, fx::from_int(5)));
    CHECK_EQ(pos.x.raw, fx::from_int(16).raw);
    CHECK(collision::move_y(solids, &pos, 16, 16, fx::from_int(-5)));
    CHECK_EQ(pos.y.raw, fx::from_int(0).raw);
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(a_rect_overlaps_a_wall_by_one_pixel);
    RUN(the_game_decides_which_tiles_are_solid);
    RUN(outside_the_map_nothing_is_solid);
    RUN(left_of_the_map_rounds_down);
    RUN(a_zeroed_map_is_all_empty);
    RUN(tiles_can_be_any_size);
    RUN(a_free_move_keeps_the_fraction);
    RUN(a_move_of_zero_goes_nowhere);
    RUN(stops_flush_against_a_wall_on_the_right);
    RUN(stops_flush_against_a_wall_on_the_left);
    RUN(stops_flush_on_the_floor_and_under_the_ceiling);
    RUN(a_hit_drops_the_fraction);
    RUN(a_move_within_a_pixel_checks_nothing);
    RUN(never_overlaps_at_walking_speed);
    RUN(cant_go_through_a_wall_however_fast);
    RUN(stops_flush_left_of_the_map);
    RUN(stops_flush_with_small_tiles);
    RUN(a_hit_across_doesnt_stop_the_box_going_down);
    RUN(slides_along_the_floor);
    RUN(a_box_inside_a_wall_stays_put);
    return test::report();
}
