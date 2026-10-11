// stats.cpp: the debug overlay's numbers. Chapter 13 explains them.
#include "stats.h"
#include "timestep.h"

namespace stats {

constexpr i64 SECOND_NS = 1'000'000'000;

// The longest a second can last in real time, for a game that runs too few
// ticks to end one.
constexpr i64 LONGEST_SECOND_NS = 2 * SECOND_NS;

// count things in elapsed_ns, as a number per second, rounded to the
// nearest whole number.
static i32 per_second(i64 count, i64 elapsed_ns)
{
    return (i32)((count * SECOND_NS + elapsed_ns / 2) / elapsed_ns);
}

void init(FrameStats* stats, i64 now_ns)
{
    *stats = { .second_start_ns = now_ns };
}

void count_frame(FrameStats* stats, i64 now_ns, i32 ticks, i64 work_ns)
{
    ASSERT(now_ns >= stats->second_start_ns); // the clock never runs backwards
    stats->frames++;
    stats->ticks += ticks;
    stats->work_ns += work_ns;

    // Usually 60 ticks take 1 s, and the rates come out as the counts. When
    // they don't, the rates say so: a game that drops time (chapter 09)
    // takes longer than 1 s to run 60 ticks, and shows fewer than 60 ticks
    // a second.
    i64 elapsed_ns = now_ns - stats->second_start_ns;
    if (stats->ticks < TICKS_PER_SECOND && elapsed_ns < LONGEST_SECOND_NS)
        return;

    // The next second starts now, with its counts at zero, and keeps the
    // numbers of the one that just ended.
    FrameStats next = {
        .second_start_ns = now_ns,
        .frames_per_second = per_second(stats->frames, elapsed_ns),
        .ticks_per_second = per_second(stats->ticks, elapsed_ns),
        .work_ns_per_frame = stats->work_ns / stats->frames,
    };
    *stats = next;
}

Str format_size(Arena* arena, i64 bytes)
{
    if (bytes < KB)
        return str::fmt(arena, "%lld B", bytes);
    if (bytes < MB)
        return str::fmt(arena, "%.1f KB", (f64)bytes / KB);
    return str::fmt(arena, "%.1f MB", (f64)bytes / MB);
}

} // namespace stats
