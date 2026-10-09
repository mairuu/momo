// momo.h: the boundary between momo's three binaries. The host and game.so
// call the engine functions declared here and in the module headers it
// includes (every function marked MOMO_API), and the engine calls the game
// through GameApi.
#pragma once

#include "base.h"
#include "arena.h"

// ---- The engine (libmomo.so) ------------------------------------------------

namespace momo {

// Loads game.so and runs it until the game calls quit(). Returns the exit
// code for main().
MOMO_API i32 run();

// The frame being run, counting from 0.
MOMO_API i32 frame_index();

// Stops the loop once the current frame is done.
MOMO_API void quit();

// The engine's three arenas, which last for different lengths of time:
//
//   permanent  the whole run
//   stage      until the game resets it, which it does when a stage loads
//   frame      one frame: the engine resets it before every update
MOMO_API Arena* permanent_arena();
MOMO_API Arena* stage_arena();
MOMO_API Arena* frame_arena();

} // namespace momo

// ---- The game (game.so) -----------------------------------------------------

// The functions the engine calls in game.so.
struct GameApi {
    void (*init)();
    void (*update)();
    void (*shutdown)();
};

// The one function game.so exports. The engine finds it by name with dlsym.
// extern "C" turns off C++ name mangling, so the symbol's name is exactly
// "momo_game_api" and not "_Z13momo_game_apiv".
extern "C" MOMO_API GameApi momo_game_api();

// The type of momo_game_api: a function that takes nothing and returns a
// GameApi. The engine casts the address dlsym returns to GameApiFn*.
using GameApiFn = GameApi();
