// game.cpp: the game. For now it only shows the engine calling the game and
// the game calling the engine.
#include "momo.h"

namespace game {

void init()
{
    printf("game: init\n");
}

void update()
{
    printf("game: update, frame %d\n", momo::frame_index());
    if (momo::frame_index() == 2)
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
