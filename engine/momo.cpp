// momo.cpp: the engine's entry point. It reserves memory, loads game.so and
// runs the loop.
#include "momo.h"

#include <dlfcn.h>
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

namespace momo {

// The engine never reloads, so its state can live in a file-scope variable.
// Like every static, it starts out zeroed.
struct Engine {
    void* game_lib; // the handle dlopen returned for game.so
    GameApi game;
    i32 frame_index;
    bool quit_requested;

    u8* memory; // one block from mmap, split into the three arenas
    i64 memory_size;
    Arena permanent;
    Arena stage;
    Arena frame;
};

static Engine engine;

// Fixed sizes, so a game that needs more memory than this stops at an
// ASSERT instead of quietly growing.
constexpr i64 PERMANENT_SIZE = 16 * MB;
constexpr i64 STAGE_SIZE = 32 * MB;
constexpr i64 FRAME_SIZE = 8 * MB;

// Reserves one block for all three arenas. mmap hands out whole pages of
// zeros, and Linux only gives a page physical memory when it's first
// touched, so reserving more than the game uses costs almost nothing.
// mmap is POSIX; Windows has VirtualAlloc instead. Chapter 07 moves this
// behind platform::.
static bool init_memory()
{
    i64 size = PERMANENT_SIZE + STAGE_SIZE + FRAME_SIZE;
    void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        fprintf(stderr, "momo: can't reserve %lld MB of memory\n", (long long)(size / MB));
        return false;
    }
    engine.memory = (u8*)memory;
    engine.memory_size = size;

    u8* next = engine.memory;
    arena::init(&engine.permanent, next, PERMANENT_SIZE);
    next += PERMANENT_SIZE;
    arena::init(&engine.stage, next, STAGE_SIZE);
    next += STAGE_SIZE;
    arena::init(&engine.frame, next, FRAME_SIZE);

    printf("momo: reserved %lld MB at %p\n", (long long)(size / MB), memory);
    return true;
}

static void print_peak(const char* name, const Arena& arena)
{
    printf("momo: %-9s arena: at most %lld bytes used of %lld MB\n", name,
           (long long)arena.peak, (long long)(arena.size / MB));
}

static void shutdown_memory()
{
    print_peak("permanent", engine.permanent);
    print_peak("stage", engine.stage);
    print_peak("frame", engine.frame);
    munmap(engine.memory, engine.memory_size);
}

// Loads game.so from the executable's folder and fills in engine.game.
// dlopen and /proc are Linux-only. Chapter 07 moves them behind platform::.
static bool load_game()
{
    // /proc/self/exe is a link to the running executable, always as an
    // absolute path. readlink doesn't add a terminating zero, which is why
    // the buffer starts zeroed and readlink gets one byte less than its size.
    char exe[PATH_MAX] = {};
    if (readlink("/proc/self/exe", exe, sizeof(exe) - 1) < 0) {
        fprintf(stderr, "momo: can't find the executable's path\n");
        return false;
    }
    *strrchr(exe, '/') = 0; // ".../build/debug/momo" becomes ".../build/debug"

    char path[PATH_MAX + 16] = {}; // room for the folder plus "/game.so"
    snprintf(path, sizeof(path), "%s/game.so", exe);

    // RTLD_NOW looks up every engine function game.so calls right here, so
    // a missing one fails now, with a message, instead of in the middle of
    // a frame.
    engine.game_lib = dlopen(path, RTLD_NOW);
    if (!engine.game_lib) {
        fprintf(stderr, "momo: %s\n", dlerror());
        return false;
    }

    auto* get_game_api = (GameApiFn*)dlsym(engine.game_lib, "momo_game_api");
    if (!get_game_api) {
        fprintf(stderr, "momo: %s\n", dlerror());
        dlclose(engine.game_lib);
        return false;
    }

    engine.game = get_game_api();
    printf("momo: loaded %s\n", path);
    return true;
}

i32 run()
{
    // On a failure here, the process exits right away, and the OS takes
    // back its memory and closes its libraries.
    if (!init_memory())
        return 1;
    if (!load_game())
        return 1;

    // Chapter 09 turns this into a real game loop with a fixed timestep.
    engine.game.init();
    while (!engine.quit_requested) {
        arena::reset(&engine.frame); // last frame's memory is free again
        engine.game.update();
        engine.frame_index++;
    }
    engine.game.shutdown();

    printf("momo: quit after %d frames\n", engine.frame_index);
    dlclose(engine.game_lib);
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
