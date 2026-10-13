// game.cpp: the game. For now it's one room, read from assets/room.txt, with
// two nests that let out bats, one at a time each, and a player who runs,
// jumps, falls, stops at the room's tiles and changes power. Each of them is
// an entity.
#include "momo.h"

// Bump this by hand whenever GameState changes in a way that keeps its size:
// fields swapped, a type changed, a field that now means something else. A
// change of size the engine notices by itself.
constexpr i32 GAME_STATE_VERSION = 1;

// What an entity is. update and render each switch on it, with a case for
// every kind (chapter 16). Zero is no kind at all, so an entity added without
// one stops the game the first time it's updated.
//
// GameState keeps each entity's kind as a number, which a reload doesn't
// change. Add new kinds at the end, or bump GAME_STATE_VERSION: a kind added
// in the middle would change what the numbers after it mean.
enum EntityKind : u8 {
    ENTITY_NONE,
    ENTITY_PLAYER,
    ENTITY_NEST, // lets out bats
    ENTITY_BAT,
};

// What each tile of the room is (chapter 17). The engine keeps tiles as
// numbers, and these are this game's names for them. TILE_CHARS has the char
// that stands for each one in a map file, in the same order: tile n is the
// char at n.
//
// Tiles are numbers in the stage arena, like kinds in GameState, so add new
// ones at the end, of both.
enum Tile : u8 {
    TILE_EMPTY,  // '.': nothing, the background shows
    TILE_WALL,   // '#'
    TILE_PILLAR, // '|': stone, in front of the entities
    TILE_COUNT,
};
constexpr Str TILE_CHARS = STR(".#|");
static_assert(TILE_CHARS.len == TILE_COUNT, "a char for every tile");

// Anything in the game that moves or acts. Every kind has every field, and
// leaves the ones it doesn't use at zero.
//
// Positions and speeds are fixed-point, in pixels and pixels per tick
// (chapter 15). render rounds positions down to whole pixels, and nothing
// else does.
struct Entity {
    EntityKind kind;
    Fx2 pos;    // its top left corner, from the screen's top left
    Fx2 vel;    // pixels per tick. A nest's is the velocity it gives its bats.
    i32 timer;  // ticks: a bat's since it left its nest, a nest's while it waits
    Handle bat; // a nest's bat: the last one it let out, which may be gone
};

// The most entities there can be at once. One more is an ASSERT.
constexpr i32 MAX_ENTITIES = 32;

// Everything the game remembers from one tick to the next. The engine keeps
// it in its own memory, so it survives a reload of this code. Game state holds
// numbers, enums, indices and handles, and pointers into the game's arenas,
// never pointers into game.so (chapter 06).
struct GameState {
    i32 tick;                            // ticks run since init: game time (chapter 09)
    Pool<Entity, MAX_ENTITIES> entities; // everything that moves or acts (chapter 16)
    Handle player;                       // the player's entity
    i32 power;                           // the player's power, 0 to POWER_COUNT - 1: its color
    Texture bat_texture;                 // bat.png, a number the renderer gave out (chapter 12)
    Tilemap room;                        // the room's tiles, in the stage arena (chapter 17)
};

namespace game {

// What the renderer's layers mean in this game, back to front. The engine
// only draws them in order (chapter 10).
enum Layer : u8 {
    LAYER_BACKGROUND, // tiles behind everything
    LAYER_ENTITIES,   // the player, enemies, shots
    LAYER_FOREGROUND, // tiles in front of entities
    LAYER_HUD,        // health bars, lives
    LAYER_COUNT,
};
static_assert(LAYER_COUNT <= MAX_LAYERS, "the renderer draws at most MAX_LAYERS layers");

// What the pad's buttons do in this game. The engine names them for where
// they are on the pad, and the game decides what they mean (chapter 14). Left
// and right on the d-pad walk, and up and down do nothing yet.
constexpr Button JUMP_BUTTON = BUTTON_A;
constexpr Button PREVIOUS_POWER_BUTTON = BUTTON_L;
constexpr Button NEXT_POWER_BUTTON = BUTTON_R;

constexpr i32 TILE_SIZE = 16; // pixels: a screen is 16 tiles by 15

// Where the bats' shadows fall: the top of room.txt's floor, two rows of
// tiles high. It doesn't read the room, so a floor moved in room.txt leaves
// the shadows falling here.
constexpr i32 FLOOR_Y = SCREEN_HEIGHT - 2 * TILE_SIZE;

// How MegaMan moves in MegaMan 1 and 2, in pixels per tick, and in 256ths of
// a pixel where there's a fraction. y grows downward, so a rise is a minus
// speed, and gravity is a plus.
constexpr Fx PLAYER_SPEED = fx::from_double(1.375);      // walking: 1 and 96/256
constexpr Fx PLAYER_AIR_SPEED = fx::from_double(1.3125); // across, in the air: 1 and 80/256
constexpr Fx GRAVITY = fx::from_double(0.25);            // more down, every tick: 64/256
constexpr Fx JUMP_SPEED = fx::from_double(4.87109375);   // up, as a jump starts: 4 and 223/256
// Letting go of the jump button while rising faster than JUMP_CUT_ABOVE slows
// the rise to JUMP_CUT_SPEED at once, so a short press is a low jump.
constexpr Fx JUMP_CUT_ABOVE = fx::from_double(2.12109375); // 2 and 31/256
constexpr Fx JUMP_CUT_SPEED = fx::from_int(1);
// The fastest fall. This is MegaMan 3 and 4's: 1 and 2's is 12, which only a
// fall of more than a screen reaches.
constexpr Fx MAX_FALL_SPEED = fx::from_int(7);

// The player starts up by the ceiling, in a gap in the room's top rows, and
// falls to the floor.
constexpr i32 PLAYER_START_X = 48;
constexpr i32 PLAYER_START_Y = 16;
constexpr i32 PLAYER_WIDTH = 16;
constexpr i32 PLAYER_HEIGHT = 24;

constexpr i32 NEST_SIZE = 16;
// How long a nest waits, once its bat is gone, before it lets out the next.
constexpr i32 NEST_WAIT_TICKS = 60;
constexpr i32 BAT_SIZE = 16; // each frame in bat.png is 16x16

constexpr Color WALL_COLORS[] = { renderer::rgb(0x7c4a1e), renderer::rgb(0x5c3612) };
constexpr Color PILLAR_COLOR = renderer::rgb(0x8a8a8a);
constexpr Color NEST_RIM_COLOR = renderer::rgb(0x5a5a5a);
constexpr Color NEST_HOLE_COLOR = renderer::rgb(0x141414);
constexpr Color SHADOW_COLOR = { .r = 0, .g = 0, .b = 0, .a = 96 }; // black, partly see-through
constexpr Color TEXT_COLOR = renderer::rgb(0xffffff);

// The debug boxes' colors (chapter 19), which nothing else in the room uses:
// red for the tiles that stop the player, and green for the player's box.
constexpr Color SOLID_BOX_COLOR = renderer::rgb(0xff004d);
constexpr Color PLAYER_BOX_COLOR = renderer::rgb(0x00e436);

// The player's color for each power, from the NES's palette: blue, as
// MegaMan starts, then orange, green and purple.
constexpr Color POWER_COLORS[] = {
    renderer::rgb(0x0058f8),
    renderer::rgb(0xe45c10),
    renderer::rgb(0x00a800),
    renderer::rgb(0x9878f8),
};
constexpr i32 POWER_COUNT = ARRAY_COUNT(POWER_COLORS);

void init(GameState* state)
{
    LOG_INFO("init");
    state->bat_texture = renderer::load_texture("assets/bat.png");

    // The room's text is needed only until its tiles are read out of it, so
    // it goes in the frame arena. The tiles last as long as the stage. The
    // stage arena is empty here: the engine resets it whenever it starts the
    // game over (chapter 06).
    const char* room_path = "assets/room.txt";
    Str room_text = momo::read_file(momo::frame_arena(), room_path);
    state->room = tilemap::parse(momo::stage_arena(), room_text, TILE_CHARS, room_path);

    // The engine hands over a zeroed GameState, so the pool starts empty, and
    // the player goes into slot 0.
    state->player = pool::add(
        &state->entities,
        { .kind = ENTITY_PLAYER,
          .pos = { .x = fx::from_int(PLAYER_START_X), .y = fx::from_int(PLAYER_START_Y) } });

    // A nest on each side of the screen. The left one's bats fly right, and
    // the right one's fly left, faster.
    pool::add(&state->entities,
              { .kind = ENTITY_NEST,
                .pos = { .x = fx::from_int(8), .y = fx::from_int(104) },
                .vel = { .x = fx::from_double(1.5) } });
    pool::add(&state->entities,
              { .kind = ENTITY_NEST,
                .pos = { .x = fx::from_int(232), .y = fx::from_int(144) },
                .vel = { .x = fx::from_int(-2) } });
}

// ---- Update -----------------------------------------------------------------

// Whether a tile stops the player (chapter 18). A switch with no default, as
// in draw_tile, so a tile added to enum Tile and left out here is a warning
// (-Wswitch), and every new tile decides.
bool tile_is_solid(u8 tile)
{
    switch ((Tile)tile) {
    case TILE_EMPTY:
        return false;
    case TILE_WALL:
        return true;
    case TILE_PILLAR:
        return false; // it's in the foreground, and the player walks behind it
    case TILE_COUNT:
        break;
    }
    ASSERT(!"not a tile");
    return false;
}

// The tiles that stop the player: the room's, with tile_is_solid to say which
// ones. update_player moves the player through them, and draw_boxes shows
// them, so what it shows is what collision checks. tile_is_solid is code in
// game.so, so a SolidTiles is made again wherever it's needed, and never kept
// in GameState.
SolidTiles room_solids(const GameState* state)
{
    return { .map = state->room, .tile_size = TILE_SIZE, .is_solid = tile_is_solid };
}

// Whether the player stands on a tile that stops it: whether the row of
// pixels just under its feet overlaps one. It's worked out again every tick,
// from where the player is drawn, and never kept.
bool on_ground(const SolidTiles& solids, const Entity* player)
{
    Rect under_feet = { .x = fx::floor(player->pos.x),
                        .y = fx::floor(player->pos.y) + PLAYER_HEIGHT,
                        .w = PLAYER_WIDTH,
                        .h = 1 };
    return collision::overlaps(solids, under_feet);
}

// Walks the way the d-pad is held, jumps, falls, stops at the room's walls,
// floors and ceilings, and changes power.
void update_player(GameState* state, Entity* player)
{
    SolidTiles solids = room_solids(state);
    bool standing = on_ground(solids, player);

    // Across: a little slower in the air than on the ground, and both
    // directions at once cancel out.
    Fx speed = standing ? PLAYER_SPEED : PLAYER_AIR_SPEED;
    player->vel.x = {};
    if (input::down(BUTTON_LEFT))
        player->vel.x -= speed;
    if (input::down(BUTTON_RIGHT))
        player->vel.x += speed;

    // Down: vel.y is kept from one tick to the next, and gravity adds to it
    // every tick, up to the fastest fall. It pulls a standing player too,
    // into the floor, which stops it.
    player->vel.y = min(player->vel.y + GRAVITY, MAX_FALL_SPEED);

    // A jump starts only from the ground, on the tick the button goes down.
    // It replaces that tick's gravity, so the jump's first tick rises the
    // whole JUMP_SPEED. Letting go while still rising fast cuts the jump
    // short, so a button pressed and let go on one tick is the lowest jump.
    if (standing && input::pressed(JUMP_BUTTON))
        player->vel.y = -JUMP_SPEED;
    if (input::released(JUMP_BUTTON) && player->vel.y < -JUMP_CUT_ABOVE)
        player->vel.y = -JUMP_CUT_SPEED;

    // Across first, then down, each on its own, so a wall that stops one
    // doesn't stop the other. A floor that stops the player lands it, and a
    // ceiling bumps its head: either way, it stops moving down or up, and
    // gravity starts again from 0.
    collision::move_x(solids, &player->pos, PLAYER_WIDTH, PLAYER_HEIGHT, player->vel.x);
    if (collision::move_y(solids, &player->pos, PLAYER_WIDTH, PLAYER_HEIGHT, player->vel.y))
        player->vel.y = {};

    // The room's rows 3 to 12 are open at both sides, and outside the map
    // is empty tiles, so the screen's edges stop the player there.
    player->pos.x =
        clamp(player->pos.x, fx::from_int(0), fx::from_int(SCREEN_WIDTH - PLAYER_WIDTH));

    // One power along for each press, however long the button stays down.
    if (input::pressed(NEXT_POWER_BUTTON))
        state->power = (state->power + 1) % POWER_COUNT;
    if (input::pressed(PREVIOUS_POWER_BUTTON))
        state->power = (state->power + POWER_COUNT - 1) % POWER_COUNT;
}

// A nest lets out one bat at a time. While its bat is out, it waits, and
// NEST_WAIT_TICKS after the bat has gone, it lets out the next. A new nest's
// handle is zeroed, which finds nothing, so its first bat comes out
// NEST_WAIT_TICKS after init.
void update_nest(GameState* state, Entity* nest)
{
    if (pool::contains(state->entities, nest->bat))
        return; // its bat is still out

    nest->timer++;
    if (nest->timer < NEST_WAIT_TICKS)
        return;
    nest->timer = 0;
    // Adding moves no entity, so nest still points at this nest.
    nest->bat =
        pool::add(&state->entities, { .kind = ENTITY_BAT, .pos = nest->pos, .vel = nest->vel });
    LOG_INFO("tick %d: the nest at x = %d lets out a bat into slot %d, generation %d", state->tick,
             fx::floor(nest->pos.x), nest->bat.index, nest->bat.generation);
}

// A bat flies straight on, and once it's wholly off the screen, it's gone.
// self is its own handle, which removing it needs.
void update_bat(GameState* state, Handle self, Entity* bat)
{
    bat->pos += bat->vel;
    bat->timer++;
    if (bat->pos.x <= fx::from_int(-BAT_SIZE) || bat->pos.x >= fx::from_int(SCREEN_WIDTH)) {
        LOG_INFO("tick %d: the bat in slot %d flew off the screen", state->tick, self.index);
        pool::remove(&state->entities, self);
    }
}

// One tick: 1/60 s of game time.
void update(GameState* state)
{
    // Every entity in turn, in the order of their slots. A nest's new bat
    // goes into a later slot than the nest's, so it moves on the tick it
    // comes out.
    for (i32 i = 0; i < MAX_ENTITIES; i++) {
        Handle handle = pool::handle_at(state->entities, i);
        Entity* e = pool::get(&state->entities, handle);
        if (!e)
            continue; // an empty slot
        switch (e->kind) {
        case ENTITY_NONE:
            ASSERT(!"an entity with no kind");
            break;
        case ENTITY_PLAYER:
            update_player(state, e);
            break;
        case ENTITY_NEST:
            update_nest(state, e);
            break;
        case ENTITY_BAT:
            update_bat(state, handle, e);
            break;
        }
    }

    state->tick++;
}

// ---- Render -----------------------------------------------------------------

// One of the room's tiles, in its place on the screen. Walls are a
// checkerboard of two browns.
void draw_tile(Tile tile, i32 column, i32 row)
{
    Rect rect = { .x = column * TILE_SIZE, .y = row * TILE_SIZE, .w = TILE_SIZE, .h = TILE_SIZE };
    switch (tile) {
    case TILE_EMPTY:
        break;
    case TILE_WALL:
        renderer::draw_rect(rect, WALL_COLORS[(row + column) % 2], LAYER_BACKGROUND);
        break;
    case TILE_PILLAR:
        renderer::draw_rect(rect, PILLAR_COLOR, LAYER_FOREGROUND);
        break;
    case TILE_COUNT:
        ASSERT(!"TILE_COUNT isn't a tile");
        break;
    }
}

// The player's box, in the whole pixels it's drawn at: the box collision
// moves through the room, from fx::floor of its position.
Rect player_box(const Entity* player)
{
    return { .x = fx::floor(player->pos.x),
             .y = fx::floor(player->pos.y),
             .w = PLAYER_WIDTH,
             .h = PLAYER_HEIGHT };
}

// The player, in its power's color. It walks behind the pillar, which is in
// the foreground.
void draw_player(const GameState* state, const Entity* player)
{
    renderer::draw_rect(player_box(player), POWER_COLORS[state->power], LAYER_ENTITIES);
}

// A dark hole with a stone rim, behind the bats that come out of it.
void draw_nest(const Entity* nest)
{
    i32 x = fx::floor(nest->pos.x);
    i32 y = fx::floor(nest->pos.y);
    Rect rim = { .x = x - 2, .y = y - 2, .w = NEST_SIZE + 4, .h = NEST_SIZE + 4 };
    renderer::draw_rect(rim, NEST_RIM_COLOR, LAYER_BACKGROUND);
    Rect hole = { .x = x, .y = y, .w = NEST_SIZE, .h = NEST_SIZE };
    renderer::draw_rect(hole, NEST_HOLE_COLOR, LAYER_BACKGROUND);
}

// The bat's shadow on the floor, then the bat. bat.png holds two frames side
// by side, wings up and wings down, and the bat flaps from one to the other
// every 8 ticks, counted from when it left its nest.
void draw_bat(const GameState* state, const Entity* bat)
{
    i32 x = fx::floor(bat->pos.x);
    i32 y = fx::floor(bat->pos.y);
    renderer::draw_rect({ .x = x + 2, .y = FLOOR_Y, .w = 12, .h = 3 }, SHADOW_COLOR,
                        LAYER_ENTITIES);
    i32 frame = (bat->timer / 8) % 2;
    Rect source = { .x = frame * BAT_SIZE, .y = 0, .w = BAT_SIZE, .h = BAT_SIZE };
    renderer::draw_sprite(state->bat_texture, source, x, y, LAYER_ENTITIES);
}

// The debug boxes (chapter 19): what collision sees, over the picture. A box
// for each tile that stops the player, from the same SolidTiles update_player
// moves it through, so a tile the player walks through, such as the pillar,
// gets none. Then the player's own box, on top.
void draw_boxes(const GameState* state)
{
    SolidTiles solids = room_solids(state);
    for (i32 row = 0; row < solids.map.rows; row++) {
        for (i32 column = 0; column < solids.map.columns; column++) {
            if (!solids.is_solid(tilemap::at(solids.map, column, row)))
                continue;
            Rect tile = { .x = column * solids.tile_size,
                          .y = row * solids.tile_size,
                          .w = solids.tile_size,
                          .h = solids.tile_size };
            renderer::debug_box(tile, SOLID_BOX_COLOR);
        }
    }
    const Entity* player = pool::get(state->entities, state->player);
    renderer::debug_box(player_box(player), PLAYER_BOX_COLOR);
}

// Once per frame: the whole picture, drawn again from the state.
void render(const GameState* state)
{
    // The room, row by row. A room that failed to load is a zeroed
    // Tilemap, with no rows.
    const Tilemap& room = state->room;
    for (i32 row = 0; row < room.rows; row++) {
        for (i32 column = 0; column < room.columns; column++)
            draw_tile((Tile)tilemap::at(room, column, row), column, row);
    }

    // Every entity, in the order of their slots, as in update. state is
    // const, so this is the get that returns const pointers.
    for (i32 i = 0; i < MAX_ENTITIES; i++) {
        const Entity* e = pool::get(state->entities, pool::handle_at(state->entities, i));
        if (!e)
            continue; // an empty slot
        switch (e->kind) {
        case ENTITY_NONE:
            ASSERT(!"an entity with no kind");
            break;
        case ENTITY_PLAYER:
            draw_player(state, e);
            break;
        case ENTITY_NEST:
            draw_nest(e);
            break;
        case ENTITY_BAT:
            draw_bat(state, e);
            break;
        }
    }

    // READY, centered and blinking, as MegaMan 2 says it when a stage
    // starts: on for 16 ticks, off for 16.
    Str ready = STR("READY");
    if ((state->tick / 16) % 2 == 0)
        renderer::draw_text(ready, (SCREEN_WIDTH - ready.len * GLYPH_SIZE) / 2, 64, TEXT_COLOR,
                            LAYER_HUD);

    // The game draws its boxes every frame, and the engine decides whether
    // they're shown: F1, or --boxes.
    draw_boxes(state);
}

void shutdown(GameState* state)
{
    const Entity* player = pool::get(state->entities, state->player);
    LOG_INFO("shutdown, with %d entities, the player at x = %g, y = %g, power %d",
             state->entities.count, fx::to_double(player->pos.x), fx::to_double(player->pos.y),
             state->power);
}

} // namespace game

// The engine calls this after every load of game.so, and everything it
// calls in the game comes from the struct it returns.
extern "C" MOMO_API GameApi momo_game_api()
{
    return {
        .state_version = GAME_STATE_VERSION,
        .state_size = sizeof(GameState),
        .init = game::init,
        .update = game::update,
        .render = game::render,
        .shutdown = game::shutdown,
    };
}
