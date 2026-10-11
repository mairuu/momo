// stats_test.cpp: tests for engine/stats.h.
#include "test.h"

#include "stats.h"
#include "timestep.h"

// Like the timestep's tests, these make up the times. The clock starts at
// 5 s, as if SDL had been running for a while.
constexpr i64 START_NS = 5'000'000'000;

static void nothing_is_known_before_a_second_ends()
{
    // 59 ticks at 120 frames a second: not quite a second of game time.
    FrameStats stats = {};
    stats::init(&stats, START_NS);
    for (i32 frame = 0; frame < 118; frame++)
        stats::count_frame(&stats, START_NS + (frame + 1) * (TICK_NS / 2), frame % 2, 0);
    CHECK_EQ(stats.frames_per_second, 0);
    CHECK_EQ(stats.ticks_per_second, 0);
}

static void a_120_hz_second_is_120_frames_and_60_ticks()
{
    // A tick every other frame, as the timestep runs them at 120 Hz. The
    // frame that runs the 60th tick, the 120th, ends the second.
    FrameStats stats = {};
    stats::init(&stats, START_NS);
    for (i32 frame = 0; frame < 120; frame++)
        stats::count_frame(&stats, START_NS + (frame + 1) * (TICK_NS / 2), frame % 2, 0);
    CHECK_EQ(stats.frames_per_second, 120);
    CHECK_EQ(stats.ticks_per_second, 60);
    CHECK_EQ(stats.frames, 0); // the next second has started, with nothing in it yet
}

static void a_20_hz_second_is_20_frames_and_60_ticks()
{
    // Three ticks a frame, as on Wayland while the window opens (chapter 09).
    FrameStats stats = {};
    stats::init(&stats, START_NS);
    for (i32 frame = 0; frame < 20; frame++)
        stats::count_frame(&stats, START_NS + (frame + 1) * (3 * TICK_NS), 3, 0);
    CHECK_EQ(stats.frames_per_second, 20);
    CHECK_EQ(stats.ticks_per_second, 60);
}

static void a_game_that_drops_time_shows_fewer_ticks()
{
    // Frames of 200 ms that run the most ticks a frame may: 6, where 12 were
    // due. 60 ticks take 2 s.
    FrameStats stats = {};
    stats::init(&stats, START_NS);
    for (i32 frame = 0; frame < 10; frame++)
        stats::count_frame(&stats, START_NS + (i64)(frame + 1) * 200'000'000, MAX_TICKS_PER_FRAME,
                           0);
    CHECK_EQ(stats.frames_per_second, 5);
    CHECK_EQ(stats.ticks_per_second, 30);
}

static void a_game_without_ticks_ends_its_second_after_2_s()
{
    // A game that failed to load runs no ticks, but the frames still count.
    // 240 frames end 80 ns short of 2 s, so the 241st ends the second.
    FrameStats stats = {};
    stats::init(&stats, START_NS);
    for (i32 frame = 0; frame < 241; frame++)
        stats::count_frame(&stats, START_NS + (frame + 1) * (TICK_NS / 2), 0, 0);
    CHECK_EQ(stats.frames_per_second, 120);
    CHECK_EQ(stats.ticks_per_second, 0);
}

static void work_is_the_average_over_the_second()
{
    // At 60 Hz, half the frames work 1 ms and half 3 ms.
    FrameStats stats = {};
    stats::init(&stats, START_NS);
    for (i32 frame = 0; frame < 60; frame++) {
        i64 work_ns = frame % 2 == 0 ? 1'000'000 : 3'000'000;
        stats::count_frame(&stats, START_NS + (frame + 1) * TICK_NS, 1, work_ns);
    }
    CHECK_EQ(stats.work_ns_per_frame, 2'000'000);
}

static void each_second_starts_counting_again()
{
    // A second at 120 Hz, then one at 60 Hz: the numbers are the second one's.
    FrameStats stats = {};
    stats::init(&stats, START_NS);
    i64 now = START_NS;
    for (i32 frame = 0; frame < 120; frame++) {
        now += TICK_NS / 2;
        stats::count_frame(&stats, now, frame % 2, 0);
    }
    for (i32 frame = 0; frame < 60; frame++) {
        now += TICK_NS;
        stats::count_frame(&stats, now, 1, 0);
    }
    CHECK_EQ(stats.frames_per_second, 60);
    CHECK_EQ(stats.ticks_per_second, 60);
}

// Checks that format_size writes exactly expected.
static bool formats_as(i64 bytes, Str expected)
{
    u8 memory[64];
    Arena arena = {};
    arena::init(&arena, memory, sizeof(memory));
    return str::eq(stats::format_size(&arena, bytes), expected);
}

static void sizes_under_a_kb_are_bytes()
{
    CHECK(formats_as(0, STR("0 B")));
    CHECK(formats_as(16, STR("16 B")));
    CHECK(formats_as(1023, STR("1023 B")));
}

static void bigger_sizes_are_kb_or_mb_with_one_decimal()
{
    CHECK(formats_as(KB, STR("1.0 KB")));
    CHECK(formats_as(491'630, STR("480.1 KB")));
    CHECK(formats_as(MB, STR("1.0 MB")));
    CHECK(formats_as(16 * MB, STR("16.0 MB")));
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(nothing_is_known_before_a_second_ends);
    RUN(a_120_hz_second_is_120_frames_and_60_ticks);
    RUN(a_20_hz_second_is_20_frames_and_60_ticks);
    RUN(a_game_that_drops_time_shows_fewer_ticks);
    RUN(a_game_without_ticks_ends_its_second_after_2_s);
    RUN(work_is_the_average_over_the_second);
    RUN(each_second_starts_counting_again);
    RUN(sizes_under_a_kb_are_bytes);
    RUN(bigger_sizes_are_kb_or_mb_with_one_decimal);
    return test::report();
}
