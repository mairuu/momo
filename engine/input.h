// input.h: the virtual pad the game reads its buttons from, and how presses
// reach the game's ticks. Chapter 14 explains it.
#pragma once

#include "base.h"

// The game never sees a key or a gamepad, only one pad: the NES pad's eight
// buttons, and two shoulder buttons. The keyboard and every gamepad plugged
// in drive the same pad (platform.cpp has the tables).
//
// Buttons are named for where they are on the pad, not for what they do.
// What A does is the game's decision, as it was Capcom's and not Nintendo's.
//
// The first eight come in the order the NES pad sent them to the console,
// one bit each.
enum Button : u8 {
    BUTTON_A,
    BUTTON_B,
    BUTTON_SELECT,
    BUTTON_START,
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,
    BUTTON_RIGHT,
    BUTTON_L, // the shoulder buttons, which the NES pad didn't have
    BUTTON_R,
    BUTTON_COUNT,
};

// A set of buttons, one bit per button: bit 0 is A, bit 1 is B, and so on.
using Buttons = u16;
static_assert(BUTTON_COUNT <= 16, "Buttons needs a bit for every button");

// The pad as a tick sees it. Between two ticks, the engine keeps a Pad of
// its own, which collects what went down and up for the next tick to take.
struct Pad {
    Buttons down;     // held when the tick runs
    Buttons pressed;  // went down since the last tick
    Buttons released; // went up since the last tick
};

namespace input {

// ---- For the game -----------------------------------------------------------

// These work only during a tick, in the game's update. A button that render
// should show goes into GameState first: render draws the game's state and
// nothing else (chapter 10).

// Whether button is held down.
MOMO_API bool down(Button button);

// Whether button went down since the last tick: true on exactly one tick per
// press, however long the button is held, and even for a tap that ended
// before the tick ran.
MOMO_API bool pressed(Button button);

// Whether button went up since the last tick: true on exactly one tick per
// release.
MOMO_API bool released(Button button);

// ---- For the engine ---------------------------------------------------------

// button's bit in a Buttons.
constexpr Buttons bit(Button button)
{
    return (Buttons)(1 << button);
}

// Records in pad that the buttons held are now the ones in held. Buttons that
// weren't down before are pressed, and buttons that were and aren't are
// released. The platform calls it every time a key or a gamepad button goes
// down or up, so a button can go down and up again before a tick sees it.
void set_held(Pad* pad, Buttons held);

// What the next tick sees: everything pad has collected since the last tick.
// pad keeps the buttons that are down, and collects the next tick's presses
// and releases from nothing.
Pad take_tick(Pad* pad);

// From begin_tick to end_tick, down, pressed and released answer from pad.
void begin_tick(Pad pad);
void end_tick();

// The d-pad's directions a gamepad's stick is pushed in. x and y go from
// -32768 (left, up) to 32767 (right, down), as SDL reports them, and a
// direction counts once the stick is pushed more than halfway along it.
Buttons stick_directions(i32 x, i32 y);

} // namespace input
