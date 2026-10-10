// timestep_test.cpp: tests for engine/timestep.h.
#include "test.h"

#include "timestep.h"

// The timestep never reads a clock: each test makes up the times, so a test
// of a whole second of frames runs in no time at all. The clock starts at 5 s,
// as if SDL had been running for a while.
constexpr i64 START_NS = 5'000'000'000;

static void no_time_runs_no_ticks()
{
    Timestep step = {};
    timestep::init(&step, START_NS);
    CHECK_EQ(timestep::advance(&step, START_NS), 0);
}

static void one_tick_of_time_runs_one_tick()
{
    Timestep step = {};
    timestep::init(&step, START_NS);
    CHECK_EQ(timestep::advance(&step, START_NS + TICK_NS), 1);
    CHECK_EQ(step.owed_ns, 0);
}

static void a_60_hz_display_runs_one_tick_a_frame()
{
    Timestep step = {};
    timestep::init(&step, START_NS);
    i64 now = START_NS;
    for (i32 frame = 0; frame < 60; frame++) {
        now += TICK_NS;
        CHECK_EQ(timestep::advance(&step, now), 1);
    }
}

static void a_120_hz_display_runs_a_tick_every_other_frame()
{
    // Half a tick of time a frame: the first frame owes the second the half
    // it couldn't run, and the second runs one whole tick.
    Timestep step = {};
    timestep::init(&step, START_NS);
    i64 now = START_NS;
    for (i32 frame = 0; frame < 120; frame++) {
        now += TICK_NS / 2;
        CHECK_EQ(timestep::advance(&step, now), frame % 2);
    }
}

static void a_20_hz_display_runs_three_ticks_a_frame()
{
    // About what SDL draws for a window you can't see on Wayland.
    Timestep step = {};
    timestep::init(&step, START_NS);
    CHECK_EQ(timestep::advance(&step, START_NS + 3 * TICK_NS), 3);
    CHECK_EQ(timestep::advance(&step, START_NS + 6 * TICK_NS), 3);
}

static void a_thousand_frames_a_second_still_run_60_ticks()
{
    // No vsync: frames as fast as the computer can draw them.
    Timestep step = {};
    timestep::init(&step, START_NS);
    i32 ticks = 0;
    for (i32 frame = 1; frame <= 1000; frame++) {
        i32 frame_ticks = timestep::advance(&step, START_NS + frame * 1'000'000);
        CHECK(frame_ticks <= 1);
        ticks += frame_ticks;
    }
    CHECK_EQ(ticks, 60);
}

static void a_long_frame_runs_only_the_cap()
{
    // A second with no frames, as after a breakpoint: 60 ticks are due, the
    // frame runs 6 and drops the other 54. What's left of a tick, 40 ns,
    // is still owed.
    Timestep step = {};
    timestep::init(&step, START_NS);
    CHECK_EQ(timestep::advance(&step, START_NS + 1'000'000'000), MAX_TICKS_PER_FRAME);
    CHECK_EQ(step.dropped_ns, (60 - MAX_TICKS_PER_FRAME) * TICK_NS);
    CHECK_EQ(step.owed_ns, 1'000'000'000 - 60 * TICK_NS);
}

static void dropped_time_is_not_owed_to_the_next_frame()
{
    // After the long frame, the game carries on at its usual pace, with no
    // burst of ticks to catch up.
    Timestep step = {};
    timestep::init(&step, START_NS);
    i64 now = START_NS + 1'000'000'000;
    timestep::advance(&step, now);
    now += TICK_NS;
    CHECK_EQ(timestep::advance(&step, now), 1);
    CHECK_EQ(step.dropped_ns, 0);
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(no_time_runs_no_ticks);
    RUN(one_tick_of_time_runs_one_tick);
    RUN(a_60_hz_display_runs_one_tick_a_frame);
    RUN(a_120_hz_display_runs_a_tick_every_other_frame);
    RUN(a_20_hz_display_runs_three_ticks_a_frame);
    RUN(a_thousand_frames_a_second_still_run_60_ticks);
    RUN(a_long_frame_runs_only_the_cap);
    RUN(dropped_time_is_not_owed_to_the_next_frame);
    return test::report();
}
