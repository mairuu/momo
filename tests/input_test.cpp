// input_test.cpp: tests for engine/input.h.
#include "test.h"

#include "input.h"

// Each test plays the engine's part. It records buttons going down and up in
// a Pad with set_held, as platform::handle_events does, and runs ticks the
// way momo's loop does. Frames don't appear at all: a frame that runs no
// ticks is two set_held calls with no tick between them, and a frame that
// runs three is three ticks with no set_held between them.

constexpr Buttons A = input::bit(BUTTON_A);
constexpr Buttons B = input::bit(BUTTON_B);

// What one tick saw of a button.
struct Seen {
    bool down;
    bool pressed;
    bool released;
};

// Runs a tick, the way the loop does, and returns what it saw of button.
static Seen tick(Pad* pad, Button button)
{
    input::begin_tick(input::take_tick(pad));
    Seen seen = {
        .down = input::down(button),
        .pressed = input::pressed(button),
        .released = input::released(button),
    };
    input::end_tick();
    return seen;
}

static void nothing_held_is_nothing_pressed()
{
    Pad pad = {};
    Seen seen = tick(&pad, BUTTON_A);
    CHECK(!seen.down);
    CHECK(!seen.pressed);
    CHECK(!seen.released);
}

static void a_held_button_is_pressed_on_its_first_tick_only()
{
    Pad pad = {};
    input::set_held(&pad, A);
    Seen first = tick(&pad, BUTTON_A);
    CHECK(first.down);
    CHECK(first.pressed);
    for (i32 i = 0; i < 10; i++) {
        Seen later = tick(&pad, BUTTON_A);
        CHECK(later.down);
        CHECK(!later.pressed);
    }
}

static void a_release_is_released_on_one_tick()
{
    Pad pad = {};
    input::set_held(&pad, A);
    tick(&pad, BUTTON_A);
    input::set_held(&pad, 0);
    Seen first = tick(&pad, BUTTON_A);
    CHECK(!first.down);
    CHECK(first.released);
    CHECK(!tick(&pad, BUTTON_A).released);
}

static void a_press_in_a_frame_without_ticks_waits_for_the_next_tick()
{
    // At 120 Hz, every other frame runs no tick. The key goes down in one of
    // those frames, and an autorepeat sets it held again in the next, with
    // the tick: the tick still sees the press.
    Pad pad = {};
    input::set_held(&pad, A);
    input::set_held(&pad, A);
    Seen seen = tick(&pad, BUTTON_A);
    CHECK(seen.down);
    CHECK(seen.pressed);
}

static void a_frame_with_three_ticks_hands_the_press_to_the_first()
{
    // While the window opens on Wayland, a frame runs three ticks
    // (chapter 09). A press must not count three times.
    Pad pad = {};
    input::set_held(&pad, A);
    CHECK(tick(&pad, BUTTON_A).pressed);
    CHECK(!tick(&pad, BUTTON_A).pressed);
    CHECK(!tick(&pad, BUTTON_A).pressed);
}

static void a_tap_between_two_ticks_is_pressed_and_released_but_not_down()
{
    Pad pad = {};
    input::set_held(&pad, A);
    input::set_held(&pad, 0);
    Seen seen = tick(&pad, BUTTON_A);
    CHECK(seen.pressed);
    CHECK(seen.released);
    CHECK(!seen.down);
}

static void letting_go_and_pressing_again_between_ticks_is_both()
{
    Pad pad = {};
    input::set_held(&pad, A);
    tick(&pad, BUTTON_A);
    input::set_held(&pad, 0);
    input::set_held(&pad, A);
    Seen seen = tick(&pad, BUTTON_A);
    CHECK(seen.down);
    CHECK(seen.pressed);
    CHECK(seen.released);
}

static void each_button_is_its_own()
{
    // A is held across the tick, and B goes down next to it.
    Pad pad = {};
    input::set_held(&pad, A);
    tick(&pad, BUTTON_A);
    input::set_held(&pad, A | B);
    CHECK(!tick(&pad, BUTTON_A).pressed);
    input::set_held(&pad, A);
    Seen seen = tick(&pad, BUTTON_B);
    CHECK(!seen.down);
    CHECK(seen.released);
}

static void a_stick_in_the_middle_is_no_direction()
{
    CHECK_EQ(input::stick_directions(0, 0), 0);
    CHECK_EQ(input::stick_directions(16384, -16384), 0); // exactly halfway isn't more than halfway
}

static void a_stick_pushed_all_the_way_is_one_direction()
{
    CHECK_EQ(input::stick_directions(-32768, 0), input::bit(BUTTON_LEFT));
    CHECK_EQ(input::stick_directions(32767, 0), input::bit(BUTTON_RIGHT));
    CHECK_EQ(input::stick_directions(0, -32768), input::bit(BUTTON_UP));
    CHECK_EQ(input::stick_directions(0, 32767), input::bit(BUTTON_DOWN));
}

static void a_stick_pushed_to_a_corner_is_two_directions()
{
    // All the way, at 45 degrees: 0.707 of the way along each axis.
    CHECK_EQ(input::stick_directions(23170, -23170),
             input::bit(BUTTON_RIGHT) | input::bit(BUTTON_UP));
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(nothing_held_is_nothing_pressed);
    RUN(a_held_button_is_pressed_on_its_first_tick_only);
    RUN(a_release_is_released_on_one_tick);
    RUN(a_press_in_a_frame_without_ticks_waits_for_the_next_tick);
    RUN(a_frame_with_three_ticks_hands_the_press_to_the_first);
    RUN(a_tap_between_two_ticks_is_pressed_and_released_but_not_down);
    RUN(letting_go_and_pressing_again_between_ticks_is_both);
    RUN(each_button_is_its_own);
    RUN(a_stick_in_the_middle_is_no_direction);
    RUN(a_stick_pushed_all_the_way_is_one_direction);
    RUN(a_stick_pushed_to_a_corner_is_two_directions);
    return test::report();
}
