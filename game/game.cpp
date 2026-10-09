// game.cpp: the game. For now it only shows the engine calling the game, the
// game calling the engine, and the game building text in the frame arena.
#include "momo.h"

namespace game {

void init()
{
    LOG_INFO("init");
}

void update()
{
    i32 frame = momo::frame_index();
    Arena* frame_arena = momo::frame_arena();

    // Text that lasts until the end of this frame, the way the debug
    // overlay's will (chapter 13). str::fmt pushes exactly the bytes it
    // needs from the frame arena, so nothing has to free them.
    Str text = str::fmt(frame_arena, "frame %d: %d bats", frame, 50 * frame);

    LOG_INFO("\"%.*s\" is %d chars at %p, frame arena has %lld bytes used",
             text.len, text.data, text.len, (void*)text.data, frame_arena->used);
    if (frame == 2)
        momo::quit();
}

void shutdown()
{
    LOG_INFO("shutdown");
}

} // namespace game

// The engine calls this right after loading game.so, and everything else it
// calls in the game comes from the struct it returns.
extern "C" MOMO_API GameApi momo_game_api()
{
    return { .init = game::init, .update = game::update, .shutdown = game::shutdown };
}
