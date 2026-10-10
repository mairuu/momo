// game.cpp: the game. For now it's one bat flying back and forth across the
// screen, which is enough to watch hot reload change the code and keep the
// state.
#include "momo.h"

// Bump this by hand whenever GameState changes in a way that keeps its size:
// fields swapped, a type changed, a field that now means something else. A
// change of size the engine notices by itself.
constexpr i32 GAME_STATE_VERSION = 1;

// Everything the game remembers from one tick to the next. The engine keeps
// it in its own memory, so it survives a reload of this code. Game state holds
// numbers, enums and indices only, never pointers into game.so (chapter 06).
struct GameState {
    i32 tick;    // ticks run since init, which counts game time (chapter 09)
    i32 bat_x;   // pixels from the left edge of the screen
    i32 bat_dir; // 1 flying right, -1 flying left
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

constexpr i32 TILE_SIZE = 16;
constexpr i32 BAT_SPEED = 1; // pixels per tick
constexpr i32 BAT_Y = 136;   // the top of the bat, in pixels from the top of the screen
constexpr i32 FLOOR_Y = SCREEN_HEIGHT - 2 * TILE_SIZE; // two rows of tiles

constexpr Color FLOOR_COLORS[] = { renderer::rgb(0x7c4a1e), renderer::rgb(0x5c3612) };
constexpr Color PILLAR_COLOR = renderer::rgb(0x8a8a8a);
constexpr Color BAT_COLOR = renderer::rgb(0x8e44ad);
constexpr Color WING_COLOR = renderer::rgb(0x5b2c6f);
constexpr Color SHADOW_COLOR = { .r = 0, .g = 0, .b = 0, .a = 96 }; // black, partly see-through

void init(GameState* state)
{
    LOG_INFO("init");
    // The engine hands over a zeroed GameState, so the bat starts at x = 0.
    state->bat_dir = 1;
}

// One tick: 1/60 s of game time.
void update(GameState* state)
{
    // Fly, and turn around at either edge of the screen.
    state->bat_x += BAT_SPEED * state->bat_dir;
    i32 right_edge = SCREEN_WIDTH - TILE_SIZE;
    if (state->bat_x <= 0 || state->bat_x >= right_edge) {
        state->bat_x = clamp(state->bat_x, 0, right_edge);
        state->bat_dir = -state->bat_dir;
    }

    state->tick++;
}

// Once per frame: the whole picture, drawn again from the state.
void render(const GameState* state)
{
    // The floor: two rows of tiles in a checkerboard.
    for (i32 row = 0; row < 2; row++) {
        for (i32 column = 0; column < SCREEN_WIDTH / TILE_SIZE; column++) {
            Rect tile = { .x = column * TILE_SIZE,
                          .y = FLOOR_Y + row * TILE_SIZE,
                          .w = TILE_SIZE,
                          .h = TILE_SIZE };
            renderer::draw_rect(tile, FLOOR_COLORS[(row + column) % 2], LAYER_BACKGROUND);
        }
    }

    // A stone pillar in the middle of the screen, in front of the bat.
    Rect pillar = { .x = 120, .y = 96, .w = TILE_SIZE, .h = FLOOR_Y - 96 };
    renderer::draw_rect(pillar, PILLAR_COLOR, LAYER_FOREGROUND);

    // The bat's shadow on the floor, then the bat: a body, and two wings
    // that flap up and down every 8 ticks.
    i32 x = state->bat_x;
    renderer::draw_rect({ .x = x + 2, .y = FLOOR_Y, .w = 12, .h = 3 }, SHADOW_COLOR,
                        LAYER_ENTITIES);
    i32 wing_y = (state->tick / 8) % 2 == 0 ? BAT_Y + 1 : BAT_Y + 7;
    renderer::draw_rect({ .x = x, .y = wing_y, .w = 6, .h = 5 }, WING_COLOR, LAYER_ENTITIES);
    renderer::draw_rect({ .x = x + 10, .y = wing_y, .w = 6, .h = 5 }, WING_COLOR, LAYER_ENTITIES);
    renderer::draw_rect({ .x = x + 4, .y = BAT_Y + 3, .w = 8, .h = 9 }, BAT_COLOR, LAYER_ENTITIES);
}

void shutdown(GameState* state)
{
    LOG_INFO("shutdown, with the bat at x = %d", state->bat_x);
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
