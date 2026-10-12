// game.cpp: the game. For now it's one room, read from assets/room.txt, with
// two nests that let out bats, one at a time each, and a player who walks
// where the pad says and changes power. Each of them is an entity.
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
// they are on the pad, and the game decides what they mean (chapter 14). The
// d-pad's directions mean what they say.
constexpr Button PREVIOUS_POWER_BUTTON = BUTTON_L;
constexpr Button NEXT_POWER_BUTTON = BUTTON_R;

constexpr i32 TILE_SIZE = 16; // pixels: a screen is 16 tiles by 15

// Where the player stands and the bats' shadows fall: the top of room.txt's
// floor, two rows of tiles high. Nothing reads the room's tiles but render
// yet, so a floor moved in room.txt leaves the player standing here.
constexpr i32 FLOOR_Y = SCREEN_HEIGHT - 2 * TILE_SIZE;

// MegaMan's walking speed in MegaMan 1 and 2: 1 pixel and 96/256 a tick.
constexpr Fx PLAYER_SPEED = fx::from_double(1.375); // pixels per tick
constexpr i32 PLAYER_START_X = 48;
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
    state->player = pool::add(&state->entities,
                              { .kind = ENTITY_PLAYER,
                                .pos = { .x = fx::from_int(PLAYER_START_X),
                                         .y = fx::from_int(FLOOR_Y - PLAYER_HEIGHT) } });

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

// Walks while the d-pad is held left or right, and changes power.
void update_player(GameState* state, Entity* player)
{
    // Both directions at once cancel out.
    if (input::down(BUTTON_LEFT))
        player->pos.x -= PLAYER_SPEED;
    if (input::down(BUTTON_RIGHT))
        player->pos.x += PLAYER_SPEED;
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

// The player, standing on the floor, in its power's color. It walks behind
// the pillar, which is in the foreground.
void draw_player(const GameState* state, const Entity* player)
{
    Rect rect = { .x = fx::floor(player->pos.x),
                  .y = fx::floor(player->pos.y),
                  .w = PLAYER_WIDTH,
                  .h = PLAYER_HEIGHT };
    renderer::draw_rect(rect, POWER_COLORS[state->power], LAYER_ENTITIES);
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
}

void shutdown(GameState* state)
{
    const Entity* player = pool::get(state->entities, state->player);
    LOG_INFO("shutdown, with %d entities, the player at x = %g, power %d", state->entities.count,
             fx::to_double(player->pos.x), state->power);
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
