// momo.cpp: the engine's entry point. It reserves memory, loads game.so,
// runs the loop, and reloads game.so whenever it changes.
#include "momo.h"

#include <dlfcn.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace momo {

// The engine never reloads, so its state can live in a file-scope variable.
// Like every static, it starts out zeroed.
struct Engine {
    Str game_path;  // the full path to game.so, in the engine's own arena
    void* game_lib; // the handle dlopen returned for game.so, or null
    i64 game_time;  // game.so's modification time when it was last loaded
    GameApi game;

    // The game's state, at the start of the permanent arena, and the
    // version and size it was made with.
    GameState* state;
    i32 state_version;
    i64 state_size;

    i32 frame_index;
    bool quit_requested;

    u8* memory; // one block from mmap, split into the four arenas
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

// Reserves one block for all four arenas. mmap hands out whole pages of
// zeros, and Linux only gives a page physical memory when it's first
// touched, so reserving more than the game uses costs almost nothing.
// mmap is POSIX; Windows has VirtualAlloc instead. Chapter 07 moves this
// behind platform::.
static bool init_memory()
{
    i64 size = PERMANENT_SIZE + STAGE_SIZE + FRAME_SIZE + ENGINE_SIZE;
    void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        LOG_ERROR("can't reserve %lld MB of memory", size / MB);
        return false;
    }
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
    munmap(engine.memory, engine.memory_size);
}

// Works out where game.so is: in the executable's folder. The path goes in
// the engine's arena, since the engine needs it for the whole run.
// /proc is Linux-only. Chapter 07 moves this behind platform::.
static bool find_game()
{
    // /proc/self/exe is a link to the running executable, always as an
    // absolute path. readlink returns the path's length and doesn't add a
    // 0 after it, so the path is a Str: the buffer and that length.
    char exe_buffer[PATH_MAX] = {};
    i32 exe_len = (i32)readlink("/proc/self/exe", exe_buffer, sizeof(exe_buffer));
    if (exe_len < 0) {
        LOG_ERROR("can't find the executable's path");
        return false;
    }
    Str exe = { .data = exe_buffer, .len = exe_len };

    // ".../build/debug/momo" -> ".../build/debug" -> ".../build/debug/game.so"
    Str folder = str::slice(exe, 0, str::find_last(exe, '/'));
    engine.game_path = str::fmt(&engine.arena, "%.*s/game.so", folder.len, folder.data);
    return true;
}

// When the file at path was last modified, in nanoseconds since 1970, or 0
// if there's no file there. C names both the function and its result
// "stat", so C++ needs the word struct to tell them apart.
static i64 file_time(Str path)
{
    struct stat info = {};
    if (stat(path.data, &info) != 0)
        return 0;
    return (i64)info.st_mtim.tv_sec * 1000000000 + info.st_mtim.tv_nsec;
}

// Opens game.so and fills in engine.game. dlopen is POSIX; Windows has
// LoadLibrary instead. Chapter 07 moves it behind platform::.
static bool load_game()
{
    // The time is read before opening, so if the file changes again in
    // between, the next check sees a newer time and loads it once more.
    // It's recorded even if loading fails, so a broken game.so is tried
    // once, not on every frame.
    engine.game_time = file_time(engine.game_path);

    // RTLD_NOW looks up every engine function game.so calls right here, so
    // a missing one fails now, with a message, instead of in the middle of
    // a frame. str::fmt put a 0 after the path, so dlopen can take it.
    engine.game_lib = dlopen(engine.game_path.data, RTLD_NOW);
    if (!engine.game_lib) {
        LOG_ERROR("%s", dlerror());
        return false;
    }

    auto* get_game_api = (GameApiFn*)dlsym(engine.game_lib, "momo_game_api");
    if (!get_game_api) {
        LOG_ERROR("%s", dlerror());
        dlclose(engine.game_lib);
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
    dlclose(engine.game_lib);
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
    i64 time = file_time(engine.game_path);
    if (time == 0 || time == engine.game_time)
        return;

    // The old game.so has to be closed first. dlopen finds libraries it has
    // already loaded by their path, so opening the same path again would
    // only hand back the old one.
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

// Ctrl+C sends momo the signal SIGINT, which ends a process on the spot
// unless it has a handler. With this one, Ctrl+C stops the loop instead, and
// the game shuts down as usual. A handler can run between any two
// instructions, so it only sets a flag: sig_atomic_t is the type C promises
// a handler can set safely, and volatile makes the loop read it fresh every
// time. Chapter 07 hands this job to SDL.
static volatile sig_atomic_t interrupted;

static void on_interrupt(int)
{
    interrupted = 1;
}

i32 run()
{
    // On a failure here, the process exits right away, and the OS takes
    // back its memory and closes its libraries.
    if (!init_memory())
        return 1;
    if (!find_game())
        return 1;
    if (!load_game())
        return 1;
    LOG_INFO("loaded %.*s", engine.game_path.len, engine.game_path.data);

    signal(SIGINT, on_interrupt);

    start_game();
    while (!engine.quit_requested && !interrupted) {
        reload_game_if_changed();
        arena::reset(&engine.frame); // last frame's memory is free again
        if (engine.game_lib)
            engine.game.update(engine.state);
        engine.frame_index++;

        // About 60 frames a second, so the game runs long enough to be
        // rebuilt. Chapter 09 replaces this with a real clock.
        usleep(16 * 1000);
    }
    if (interrupted)
        LOG_INFO("interrupted");

    if (engine.game_lib) {
        engine.game.shutdown(engine.state);
        unload_game();
    }

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
