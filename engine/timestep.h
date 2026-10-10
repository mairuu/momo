// timestep.h: the fixed timestep, which works out how many ticks of game time
// each frame runs. Chapter 09 explains it.
#pragma once

#include "base.h"

// A tick is one update of the game, 1/60 of a second of game time. A frame is
// one picture on the screen, as often as the display shows them. The game
// runs 60 ticks a second at any frame rate.
//
// 1/60 s isn't a whole number of nanoseconds, so a tick is 2/3 ns short of it.
// That adds up to one extra tick every 4.8 days.
constexpr i32 TICKS_PER_SECOND = 60;
constexpr i64 TICK_NS = 1'000'000'000 / TICKS_PER_SECOND; // 16,666,666

// The most ticks one frame runs: 100 ms of game time. Frames as slow as 10 a
// second keep the game at full speed: half the 20 a second SDL draws on
// Wayland while the compositor isn't drawing the window, as while it opens
// (chapter 09). A longer frame was a stall, such as a breakpoint, and the
// time over the cap is dropped instead of caught up.
constexpr i32 MAX_TICKS_PER_FRAME = 6;

// All times are nanoseconds, from whatever clock the caller reads. Only
// differences between them matter.
struct Timestep {
    i64 last_ns;    // when the last frame started
    i64 owed_ns;    // time that has passed and no tick has covered yet
    i64 dropped_ns; // time the last frame dropped, because of the cap
};

namespace timestep {

// Starts counting time from now_ns.
void init(Timestep* step, i64 now_ns);

// Called at the start of every frame with the time now. Returns how many
// ticks the frame runs: one for every whole tick of time since the last
// frame, at most MAX_TICKS_PER_FRAME. What's left of a tick is owed to the
// next frame.
i32 advance(Timestep* step, i64 now_ns);

} // namespace timestep
