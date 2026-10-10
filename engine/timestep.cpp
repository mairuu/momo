// timestep.cpp: the fixed timestep. Chapter 09 explains it.
#include "timestep.h"

namespace timestep {

void init(Timestep* step, i64 now_ns)
{
    *step = { .last_ns = now_ns };
}

i32 advance(Timestep* step, i64 now_ns)
{
    ASSERT(now_ns >= step->last_ns); // the clock never runs backwards
    step->owed_ns += now_ns - step->last_ns;
    step->last_ns = now_ns;

    // Every whole tick of time owed is due. The frame runs them, up to the
    // cap, and the ones over the cap are dropped, not owed to the next
    // frame, or it would run a burst of ticks too.
    i64 due = step->owed_ns / TICK_NS;
    i32 ticks = (i32)min(due, (i64)MAX_TICKS_PER_FRAME);
    step->owed_ns -= due * TICK_NS;
    step->dropped_ns = (due - ticks) * TICK_NS;
    return ticks;
}

} // namespace timestep
