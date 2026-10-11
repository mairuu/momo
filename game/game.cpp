// game.cpp: the game. For now it's a bat flying back and forth across the
// screen, and a player who walks where the pad says and changes power.
#include "momo.h"

// Bump this by hand whenever GameState changes in a way that keeps its size:
// fields swapped, a type changed, a field that now means something else. A
// change of size the engine notices by itself.
constexpr i32 GAME_STATE_VERSION = 1;

// Everything the game remembers from one tick to the next. The engine keeps
// it in its own memory, so it survives a reload of this code. Game state holds
// numbers, enums and indices only, never pointers into game.so (chapter 06).
struct GameState {
    i32 tick;            // ticks run since init, which counts game time (chapter 09)
    i32 bat_x;           // pixels from the left edge of the screen
    i32 bat_dir;         // 1 flying right, -1 flying left
    Texture bat_texture; // bat.png, a number the renderer gave out (chapter 12)
    i32 player_x;        // pixels from the left edge of the screen
    i32 power;           // the player's power, 0 to POWER_COUNT - 1, which shows as its color
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

constexpr i32 TILE_SIZE = 16;
constexpr i32 BAT_SPEED = 1; // pixels per tick
constexpr i32 BAT_Y = 136;   // the top of the bat, in pixels from the top of the screen
constexpr i32 BAT_SIZE = 16; // each frame in bat.png is 16x16
constexpr i32 FLOOR_Y = SCREEN_HEIGHT - 2 * TILE_SIZE; // two rows of tiles

// MegaMan walks 1.375 pixels a tick, which a whole number of pixels can't
// hold. Chapter 15 brings fractions of a pixel.
constexpr i32 PLAYER_SPEED = 1; // pixels per tick
constexpr i32 PLAYER_START_X = 48;
constexpr i32 PLAYER_WIDTH = 16;
constexpr i32 PLAYER_HEIGHT = 24;

constexpr Color FLOOR_COLORS[] = { renderer::rgb(0x7c4a1e), renderer::rgb(0x5c3612) };
constexpr Color PILLAR_COLOR = renderer::rgb(0x8a8a8a);
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
    // The engine hands over a zeroed GameState, so the bat starts at x = 0.
    state->bat_dir = 1;
    state->bat_texture = renderer::load_texture("assets/bat.png");
    state->player_x = PLAYER_START_X;
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

    // Walk while the d-pad is held left or right. Both at once cancel out.
    if (input::down(BUTTON_LEFT))
        state->player_x -= PLAYER_SPEED;
    if (input::down(BUTTON_RIGHT))
        state->player_x += PLAYER_SPEED;
    state->player_x = clamp(state->player_x, 0, SCREEN_WIDTH - PLAYER_WIDTH);

    // One power along for each press, however long the button stays down.
    if (input::pressed(NEXT_POWER_BUTTON))
        state->power = (state->power + 1) % POWER_COUNT;
    if (input::pressed(PREVIOUS_POWER_BUTTON))
        state->power = (state->power + POWER_COUNT - 1) % POWER_COUNT;

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

    // The bat's shadow on the floor, then the bat. bat.png holds two frames
    // side by side, wings up and wings down, and the bat flaps from one to
    // the other every 8 ticks.
    i32 x = state->bat_x;
    renderer::draw_rect({ .x = x + 2, .y = FLOOR_Y, .w = 12, .h = 3 }, SHADOW_COLOR,
                        LAYER_ENTITIES);
    i32 frame = (state->tick / 8) % 2;
    Rect source = { .x = frame * BAT_SIZE, .y = 0, .w = BAT_SIZE, .h = BAT_SIZE };
    renderer::draw_sprite(state->bat_texture, source, x, BAT_Y, LAYER_ENTITIES);

    // The player, standing on the floor, in its power's color. It walks
    // behind the pillar, which is in the foreground.
    Rect player = {
        .x = state->player_x, .y = FLOOR_Y - PLAYER_HEIGHT, .w = PLAYER_WIDTH, .h = PLAYER_HEIGHT
    };
    renderer::draw_rect(player, POWER_COLORS[state->power], LAYER_ENTITIES);

    // READY, centered and blinking, as MegaMan 2 says it when a stage
    // starts: on for 16 ticks, off for 16.
    Str ready = STR("READY");
    if ((state->tick / 16) % 2 == 0)
        renderer::draw_text(ready, (SCREEN_WIDTH - ready.len * GLYPH_SIZE) / 2, 64, TEXT_COLOR,
                            LAYER_HUD);
}

void shutdown(GameState* state)
{
    LOG_INFO("shutdown, with the bat at x = %d, the player at x = %d, power %d", state->bat_x,
             state->player_x, state->power);
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
