// stats.h: the numbers the debug overlay shows, counted and turned into text
// without drawing anything, so the tests can check them. Chapter 13 explains
// them.
#pragma once

#include "base.h"
#include "arena.h"
#include "str.h"

// Frames, ticks, and the time the engine spends working on each frame,
// counted a second at a time. A number that changed every frame would
// flicker too fast to read, so the overlay shows the last whole second's,
// which change once a second.
//
// The second is one second of game time: it ends with the frame that runs
// its 60th tick. A second of real time would end between two ticks, and
// catch 60 or 61 of them depending on where its edges fell. A game that
// runs no ticks, such as one that failed to load, would never end one, so a
// second also ends once 2 s of real time have passed.
//
// Times are nanoseconds from any clock, as in the timestep (chapter 09): the
// engine passes platform::clock_ns, and the tests make them up.
struct FrameStats {
    // The second being counted: when it started, and what it has seen so far.
    i64 second_start_ns;
    i32 frames;
    i32 ticks;
    i64 work_ns;

    // The last whole second's numbers, all 0 until the first second ends.
    // Work is the time a frame took before it waited for the display: what
    // the engine and the game spent on it.
    i32 frames_per_second;
    i32 ticks_per_second;
    i64 work_ns_per_frame;
};

namespace stats {

// Starts counting the first second at now_ns.
void init(FrameStats* stats, i64 now_ns);

// Counts a frame that ended at now_ns, ran ticks ticks, and worked for
// work_ns of its time. If the frame ends the second, works out the second's
// numbers and starts the next one.
void count_frame(FrameStats* stats, i64 now_ns, i32 ticks, i64 work_ns);

// A size in bytes as text for the overlay, pushed from arena: "16 B",
// "384.1 KB", "16.0 MB".
Str format_size(Arena* arena, i64 bytes);

} // namespace stats
