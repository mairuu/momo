// momo.cpp: the engine's entry point. It reserves memory, loads game.so,
// opens the window, runs the loop, and reloads game.so whenever it changes.
#include "momo.h"
#include "platform.h"
#include "renderer.h"

#include <stddef.h>

namespace momo {

// The engine never reloads, so its state can live in a file-scope variable.
// Like every static, it starts out zeroed.
struct Engine {
    Str game_path;  // the full path to game.so, in the engine's own arena
    void* game_lib; // the handle platform::open_library returned, or null
    i64 game_time;  // game.so's modification time when it was last loaded
    GameApi game;

    // The game's state, at the start of the permanent arena, and the
    // version and size it was made with.
    GameState* state;
    i32 state_version;
    i64 state_size;

    i32 frame_index;
    bool quit_requested;

    u8* memory; // one block from the OS, split into the four arenas
    i64 memory_size;
    Arena permanent; // the game's three arenas
    Arena stage;
    Arena frame;
    Arena arena; // the engine's own, which the game can't reach
};

static Engine engine;

// Fixed sizes, so a game that needs more memory than this stops at an
// ASSERT instead of quietly growing.
constexpr i64 PERMANENT_SIZE = 16 * MB;
constexpr i64 STAGE_SIZE = 32 * MB;
constexpr i64 FRAME_SIZE = 8 * MB;
constexpr i64 ENGINE_SIZE = 8 * MB;

// The window: three times the 256x240 screen the game will draw, which
// chapter 11 scales to fit whatever size the window is.
constexpr i32 WINDOW_WIDTH = 3 * 256;
constexpr i32 WINDOW_HEIGHT = 3 * 240;

// Reserves one block for all four arenas. The OS hands out zeroed pages,
// and only gives a page physical memory when it's first touched, so
// reserving more than the game uses costs almost nothing.
static bool init_memory()
{
    i64 size = PERMANENT_SIZE + STAGE_SIZE + FRAME_SIZE + ENGINE_SIZE;
    void* memory = platform::reserve_memory(size);
    if (!memory)
        return false; // already logged
    engine.memory = (u8*)memory;
    engine.memory_size = size;

    // The game's memory, permanent and stage, comes first and in one piece,
    // so chapter 21 can copy all of it, and none of the engine's, at once.
    u8* next = engine.memory;
    arena::init(&engine.permanent, next, PERMANENT_SIZE);
    next += PERMANENT_SIZE;
    arena::init(&engine.stage, next, STAGE_SIZE);
    next += STAGE_SIZE;
    arena::init(&engine.frame, next, FRAME_SIZE);
    next += FRAME_SIZE;
    arena::init(&engine.arena, next, ENGINE_SIZE);

    LOG_INFO("reserved %lld MB at %p", size / MB, memory);
    return true;
}

static void log_peak(const char* name, const Arena& arena)
{
    LOG_INFO("%-9s arena: at most %lld bytes used of %lld MB", name, arena.peak,
             arena.size / MB);
}

static void shutdown_memory()
{
    log_peak("permanent", engine.permanent);
    log_peak("stage", engine.stage);
    log_peak("frame", engine.frame);
    log_peak("engine", engine.arena);
    platform::release_memory(engine.memory, engine.memory_size);
}

// Works out where game.so is: in the executable's folder. The path goes in
// the engine's arena, since the engine needs it for the whole run.
static bool find_game()
{
    // ".../build/debug/" -> ".../build/debug/game.so"
    const char* folder = platform::executable_folder();
    if (!folder)
        return false; // already logged
    engine.game_path = str::fmt(&engine.arena, "%sgame.so", folder);
    return true;
}

// Opens game.so and fills in engine.game.
static bool load_game()
{
    // The time is read before opening, so if the file changes again in
    // between, the next check sees a newer time and loads it once more.
    // It's recorded even if loading fails, so a broken game.so is tried
    // once, not on every frame. str::fmt put a 0 after the path, so the
    // platform functions can take it.
    engine.game_time = platform::file_time(engine.game_path.data);

    engine.game_lib = platform::open_library(engine.game_path.data);
    if (!engine.game_lib)
        return false; // already logged, like every platform:: failure

    auto* get_game_api =
        (GameApiFn*)platform::find_function(engine.game_lib, "momo_game_api");
    if (!get_game_api) {
        platform::close_library(engine.game_lib);
        engine.game_lib = nullptr;
        return false;
    }

    engine.game = get_game_api();
    return true;
}

// Closes game.so. Its code, its constants and its globals are gone after
// this, and a pointer into them points at whatever is loaded there next,
// which is often the new game.so.
static void unload_game()
{
    platform::close_library(engine.game_lib);
    engine.game_lib = nullptr;
    engine.game = {};
}

// Starts the game from nothing: resets the game's memory, puts a new
// GameState at the start of the permanent arena, and calls the game's init.
// Everything the game pushes after this comes out zeroed again.
static void start_game()
{
    arena::reset(&engine.permanent);
    arena::reset(&engine.stage);

    // The engine can't see alignof(GameState). max_align_t is the most any
    // ordinary type needs, which is what malloc promises too.
    engine.state = (GameState*)arena::push_bytes(&engine.permanent, engine.game.state_size,
                                                 alignof(max_align_t));
    engine.state_version = engine.game.state_version;
    engine.state_size = engine.game.state_size;
    engine.game.init(engine.state);
}

// Called once per frame: if game.so has changed since it was loaded, swaps
// in the new one. The game's state stays where it is, and the new code
// carries on with it, unless its layout changed.
static void reload_game_if_changed()
{
    // No file (in the middle of make clean, say) isn't a change: the old
    // code is still in memory and keeps running.
    i64 time = platform::file_time(engine.game_path.data);
    if (time == 0 || time == engine.game_time)
        return;

    // The old game.so has to be closed first. dlopen, under open_library,
    // finds libraries it has already loaded by their path, so opening the
    // same path again would only hand back the old one.
    if (engine.game_lib)
        unload_game();
    if (!load_game())
        return; // already logged: the game waits, its state untouched, for the next build

    if (engine.game.state_version == engine.state_version &&
        engine.game.state_size == engine.state_size) {
        LOG_INFO("reloaded %.*s", engine.game_path.len, engine.game_path.data);
        return;
    }

    LOG_WARN("GameState changed from version %d, %lld bytes, to version %d, %lld bytes: "
             "starting the game over",
             engine.state_version, engine.state_size, engine.game.state_version,
             engine.game.state_size);
    start_game();
}

i32 run()
{
    // On a failure here, the process exits right away, and the OS takes
    // back its memory, closes its libraries and its window.
    if (!init_memory())
        return 1;
    if (!find_game())
        return 1;
    if (!load_game())
        return 1;
    LOG_INFO("loaded %.*s", engine.game_path.len, engine.game_path.data);

    // The window and OpenGL start before the game does. From chapter 12 on,
    // the game's init loads textures, and OpenGL needs a window before it
    // can take them.
    if (!platform::init("momo", WINDOW_WIDTH, WINDOW_HEIGHT))
        return 1;
    if (!renderer::init())
        return 1;

    start_game();
    while (!engine.quit_requested) {
        if (!platform::handle_events())
            break; // the window was closed, or Ctrl+C
        reload_game_if_changed();
        arena::reset(&engine.frame); // last frame's memory is free again
        if (engine.game_lib)
            engine.game.update(engine.state);
        renderer::begin_frame();
        platform::present();
        engine.frame_index++;

        // About 60 frames a second. Chapter 09 replaces this with a real
        // clock.
        platform::sleep_ms(16);
    }

    if (engine.game_lib) {
        engine.game.shutdown(engine.state);
        unload_game();
    }
    platform::shutdown();

    LOG_INFO("quit after %d frames", engine.frame_index);
    shutdown_memory();
    return 0;
}

i32 frame_index()
{
    return engine.frame_index;
}

void quit()
{
    engine.quit_requested = true;
}

Arena* permanent_arena()
{
    return &engine.permanent;
}

Arena* stage_arena()
{
    return &engine.stage;
}

Arena* frame_arena()
{
    return &engine.frame;
}

} // namespace momo
