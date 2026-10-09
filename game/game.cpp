// game.cpp: the game. For now it only shows the engine calling the game, the
// game calling the engine, and the game using the frame arena.
#include "momo.h"

namespace game {

void init()
{
    printf("game: init\n");
}

void update()
{
    i32 frame = momo::frame_index();

    // Memory that lasts until the end of this frame. The engine resets the
    // frame arena before every update, so the game never frees anything.
    i32 count = 100 * (frame + 1);
    i32* numbers = arena::push_array<i32>(momo::frame_arena(), count);
    for (i32 i = 0; i < count; i++)
        numbers[i] = i;

    printf("game: update, frame %d: %d numbers at %p, frame arena has %lld bytes used\n",
           frame, count, (void*)numbers, (long long)momo::frame_arena()->used);
    if (frame == 2)
        momo::quit();
}

void shutdown()
{
    printf("game: shutdown\n");
}

} // namespace game

// The engine calls this right after loading game.so, and everything else it
// calls in the game comes from the struct it returns.
extern "C" MOMO_API GameApi momo_game_api()
{
    return { .init = game::init, .update = game::update, .shutdown = game::shutdown };
}
