// momo.cpp: the engine's entry point. It loads game.so and runs the loop.
#include "momo.h"

#include <dlfcn.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

namespace momo {

// The engine never reloads, so its state can live in a file-scope variable.
// Like every static, it starts out zeroed.
struct Engine {
    void* game_lib; // the handle dlopen returned for game.so
    GameApi game;
    i32 frame_index;
    bool quit_requested;
};

static Engine engine;

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
    if (!load_game())
        return 1;

    // Chapter 09 turns this into a real game loop with a fixed timestep.
    engine.game.init();
    while (!engine.quit_requested) {
        engine.game.update();
        engine.frame_index++;
    }
    engine.game.shutdown();

    printf("momo: quit after %d frames\n", engine.frame_index);
    dlclose(engine.game_lib);
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

} // namespace momo
