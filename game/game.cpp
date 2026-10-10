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

constexpr i32 SCREEN_WIDTH = 256;
constexpr i32 TILE_SIZE = 16;
constexpr i32 BAT_SPEED = 1; // pixels per tick

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

    // Twice a second of game time, draw the screen as one row of 16 tiles,
    // with the bat in the tile it's in.
    if (state->tick % 30 == 0) {
        char row[SCREEN_WIDTH / TILE_SIZE] = {};
        for (i32 i = 0; i < ARRAY_COUNT(row); i++)
            row[i] = '.';
        row[state->bat_x / TILE_SIZE] = 'B';
        LOG_INFO("tick %4d  |%.*s|  x = %3d", state->tick, ARRAY_COUNT(row), row, state->bat_x);
    }

    state->tick++;
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
        .shutdown = game::shutdown,
    };
}
