// input.cpp: the virtual pad. Chapter 14 explains it.
#include "input.h"

namespace input {

// The engine never reloads, so these can be file-scope variables.
static Pad tick_pad; // what the tick that's running sees
static bool in_tick; // between begin_tick and end_tick

// More than halfway, on SDL's axes, which go to 32767 one way and -32768 the
// other.
constexpr i32 STICK_HALFWAY = 16384;

static bool has(Buttons buttons, Button button)
{
    // Outside a tick, pressed could only repeat what the last tick saw.
    // render draws GameState and nothing else, so it reads no buttons.
    ASSERT(in_tick);
    ASSERT(button < BUTTON_COUNT);
    return (buttons & bit(button)) != 0;
}

bool down(Button button)
{
    return has(tick_pad.down, button);
}

bool pressed(Button button)
{
    return has(tick_pad.pressed, button);
}

bool released(Button button)
{
    return has(tick_pad.released, button);
}

void set_held(Pad* pad, Buttons held)
{
    // Bits are added to pressed and released, never taken away, until the
    // next tick takes them: a press stays a press however many frames
    // go by before a tick runs.
    pad->pressed |= held & ~pad->down;
    pad->released |= pad->down & ~held;
    pad->down = held;
}

Pad take_tick(Pad* pad)
{
    Pad tick = *pad;
    pad->pressed = 0;
    pad->released = 0;
    return tick;
}

void begin_tick(Pad pad)
{
    ASSERT(!in_tick);
    tick_pad = pad;
    in_tick = true;
}

void end_tick()
{
    ASSERT(in_tick);
    in_tick = false;
}

Buttons stick_directions(i32 x, i32 y)
{
    Buttons held = 0;
    if (x < -STICK_HALFWAY)
        held |= bit(BUTTON_LEFT);
    if (x > STICK_HALFWAY)
        held |= bit(BUTTON_RIGHT);
    if (y < -STICK_HALFWAY)
        held |= bit(BUTTON_UP);
    if (y > STICK_HALFWAY)
        held |= bit(BUTTON_DOWN);
    return held;
}

} // namespace input
