// momo.h: the boundary between momo's three binaries. The host and game.so
// call the engine functions declared here and in the module headers it
// includes (every function marked MOMO_API), and the engine calls the game
// through GameApi.
#pragma once

#include "base.h"
#include "arena.h"
#include "logger.h"
#include "renderer.h"
#include "str.h"

// ---- The engine (libmomo.so) ------------------------------------------------

namespace momo {

// Reads the command line main() was given (chapter 13), loads game.so,
// opens the window, and runs the game until it calls quit(), the window is
// closed, or you press Ctrl+C, reloading game.so whenever it changes.
// Returns the exit code for main(): 0, or 1 if anything failed.
MOMO_API i32 run(i32 argc, char** argv);

// Stops the loop once the current frame is done.
MOMO_API void quit();

// The game's three arenas, which last for different lengths of time:
//
//   permanent  the whole run, unless a reload changes GameState's layout:
//              then the engine resets it and the stage arena, and the game
//              starts over
//   stage      until the game resets it, which it does when a stage loads
//   frame      one frame: the engine resets it as every frame starts,
//              before the frame's ticks
MOMO_API Arena* permanent_arena();
MOMO_API Arena* stage_arena();
MOMO_API Arena* frame_arena();

} // namespace momo

// ---- The game (game.so) -----------------------------------------------------

// Everything the game remembers from one tick to the next. The game defines
// it, and the engine only ever passes a pointer to it, so the engine doesn't
// need to know what's inside. The engine keeps it in the permanent arena,
// where it survives a reload unless its layout changes (see GameApi).
struct GameState;

// What game.so hands the engine: the size and version of its GameState, and
// the functions the engine calls. After every reload the engine asks again.
struct GameApi {
    // GAME_STATE_VERSION, which the game bumps by hand, and sizeof(GameState).
    // If either one changes in a reload, the old state no longer fits the new
    // code, and the engine starts the game over with a new one.
    i32 state_version;
    i64 state_size;

    // Starts the game on a zeroed GameState, and loads what it needs, such
    // as its textures (chapter 12). The engine calls it once the window and
    // OpenGL are up, and again whenever it starts the game over.
    void (*init)(GameState* state);

    // Runs one tick: moves the game 1/60 s forward. The engine calls it 60
    // times a second, however many frames a second the display shows
    // (chapter 09), so it takes no time step: every tick is the same length.
    void (*update)(GameState* state);

    // Draws the game, with the renderer's draw_rect, draw_sprite and
    // draw_text. The engine calls it once per frame, after the frame's
    // ticks, which may be none: then it draws the same picture again. The
    // state is const, so drawing can't change the game, and the game plays
    // the same however many frames there are (chapter 10).
    void (*render)(const GameState* state);

    void (*shutdown)(GameState* state);
};

// The one function game.so exports. The engine finds it by name with dlsym.
// extern "C" turns off C++ name mangling, so the symbol's name is exactly
// "momo_game_api" and not "_Z13momo_game_apiv".
extern "C" MOMO_API GameApi momo_game_api();

// The type of momo_game_api: a function that takes nothing and returns a
// GameApi. The engine casts the address dlsym returns to GameApiFn*.
using GameApiFn = GameApi();
