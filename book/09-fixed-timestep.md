# 09 · Fixed Timestep

> **Goal:** the game runs at exactly 60 updates a second, whatever the display does. On the laptop's 120 Hz panel momo draws 120 frames a second, with vsync off it draws 29,000, and either way the bat ends up at the same pixel. If momo stalls, under a breakpoint or with Ctrl+Z, the game skips the lost time instead of racing to catch up, and says so in the log.

## The problem

Since chapter 06, the loop has run one `update` per frame and then slept for 16 ms. The game's speed is whatever that adds up to, and it adds up to the wrong thing:

- **It's a little fast.** A frame is the sleep plus the frame's own work. Chapter 08 measured 16.4 ms, which is 61 frames a second, not 60. In a 10-second run, the old loop drew 602 frames. The bat flies 61 pixels a second when the game says 60.
- **It depends on how long a frame takes.** The debug build, with its sanitizers, does more work per frame than the release build. A slower machine does more. Chapter 10 draws sprites and chapter 34 mixes audio. Every bit of work added to a frame slows the game down.
- **It depends on the display.** Chapter 08's exercise 3 turned vsync on and took the sleep out. The bat flew twice as fast, because your laptop's display shows 120 pictures a second. On the 60 Hz monitor next to it, the same build would fly at the right speed.

The plan wants the opposite ([plan §4.3](plan.md#43-the-frame)). The game runs at a fixed 60 updates a second, with speeds in pixels per update, so MegaMan's movement numbers carry over unchanged and the same inputs always give the same result. Chapter 21's record and replay depends on that.

So momo needs two rates, kept apart: how often it draws, and how fast game time passes.

## Design

### Ticks and frames

Two words, used strictly from here on:

- A **tick** is one update of the game: `game.update`, which moves the game exactly 1/60 s forward. There are always 60 ticks a second.
- A **frame** is one pass through the loop, ending with one picture on the screen. There are as many frames a second as the display shows, or as the computer can draw.

On the NES they were the same thing. The console drew one picture per update, to a TV that showed 60 pictures a second (in North America), so MegaMan's speeds are "pixels per frame". In momo those are pixels per **tick**. The game's `BAT_SPEED` comment now says so.

Each frame does some things once, and runs some number of ticks:

| Once per frame | Once per tick |
|---|---|
| Handle SDL's events | `game.update` |
| Check whether `game.so` changed | Read the virtual pad's presses (chapter 14) |
| Reset the frame arena | |
| Draw and present (`game.render` from chapter 10) | |

### Four ways to run a loop

| Option | The catch |
|---|---|
| Update, then sleep a fixed time (momo until now) | The speed depends on how long a frame takes and how exactly the sleep wakes up |
| One update per vsync, as the NES did | The speed depends on the display: double on 120 Hz, as chapter 08's exercise 3 showed |
| A **variable timestep**: each update gets the time since the last one, `dt`, and multiplies every speed by it | The game's results depend on the frame rate (below) |
| **A fixed timestep:** every update moves the game exactly 1/60 s, and each frame runs as many updates as the time since the last frame calls for | A frame runs zero, one or several ticks, and the leftover time has to be carried to the next frame |

You've used the variable timestep. LearnOpenGL's camera chapter computes `deltaTime` every frame and moves the camera by `2.5f * deltaTime`. For a camera it's fine. For a game it breaks one of the plan's goals: the result depends on the frame rate.

Take a jump. Each update takes gravity off the upward speed, then moves up by the speed, both scaled by `dt`: the usual two lines. Say the jump starts at 5 pixels per tick upward, with gravity of 0.25 pixels per tick per tick. The exact peak is 50 pixels. Step it with `dt` at different frame rates, and each rate lands somewhere else:

| Frames a second | `dt`, in ticks | Peak height |
|---|---|---|
| 30 | 2 | 45 px |
| 60 | 1 | 47.5 px |
| 120 | 0.5 | 48.75 px |

A ledge three tiles up is 48 pixels. On a 120 Hz display you can reach it. At 60 frames a second you can't. Moving in steps of `dt` always misses the exact curve, and smaller steps miss it by less. Add floats rounding differently at every step, and two runs with the same inputs drift apart, so chapter 21 couldn't replay a recording.

A fixed timestep makes every step the same size, 1/60 s, so the jump peaks at 47.5 pixels on every machine, every time. The game's `update` doesn't even take a `dt`: there's nothing to multiply by, since every tick is the same length. momo takes the last option.

### The accumulator

Each frame, the engine reads the clock and adds the time since the last frame to a running total of time the game is **owed**. Then it runs one tick for every whole tick of time in the total, and keeps the remainder, less than one tick, for the next frame:

```
owed += now - last          time since the last frame
due   = owed / TICK         whole ticks of it
run due ticks
owed -= due * TICK          what's left is less than one tick
```

That running total is usually called the **accumulator**. momo's field is called `owed_ns`, after what it holds. Here's a run of frames, with a tick of 16.7 ms:

| Frame | Since the last frame | Owed | Ticks run | Still owed |
|---|---|---|---|---|
| 1 | 8.3 ms | 8.3 ms | 0 | 8.3 ms |
| 2 | 8.3 ms | 16.7 ms | 1 | 0 |
| 3 | 8.3 ms | 8.3 ms | 0 | 8.3 ms |
| 4 | 16.7 ms: a refresh was missed | 25 ms | 1 | 8.3 ms |
| 5 | 8.3 ms | 16.7 ms | 1 | 0 |
| 6 | 50 ms: the window was opening | 50 ms | 3 | 0 |
| 7 | 3000 ms: Ctrl+Z | 3000 ms | 6 | 0, and 2900 ms dropped |

Frames 1–3 are the 120 Hz display: a tick every other frame. Frame 4 came late, and the time still adds up, so the game doesn't lose it. Frame 6 owes three ticks and runs all three. Frame 7 is the cap, two sections down.

> **"Fix Your Timestep!"** The accumulator comes from Glenn Fiedler's 2004 article of that name, the usual reference on game loops. It goes one step further than momo: it draws a blend of the last two ticks, weighted by how far the leftover time reaches into the next one, so motion looks smooth even when ticks and frames don't line up. That's the smoothing [plan §4.3](plan.md#43-the-frame) leaves out for now. Hazel takes the variable timestep instead: every layer's `OnUpdate` gets a `Timestep`, which is Hazel's name for `dt`. momo's `Timestep` is something else, the accumulator's state.

### The clock

SDL's clock is `SDL_GetTicksNS`: nanoseconds since SDL first read the clock, as an unsigned 64-bit number. On Linux it calls `clock_gettime(CLOCK_MONOTONIC_RAW)`, a clock that only counts up. Setting the system's time, or NTP nudging it, doesn't move it. `platform::clock_ns` returns it as an `i64`. Careful with the name: SDL's "ticks" are just its word for time since start, and have nothing to do with momo's ticks.

Time stays an integer count of nanoseconds. An `i64` of them lasts 292 years. Seconds in a float would be the obvious alternative, but an `f32` has 24 bits of precision: an hour into a run, two times less than a quarter of a millisecond apart look the same, and a day in, 8 ms apart, half a tick. An `f64` would do, but integers are exact, and they're what SDL hands out.

A tick doesn't quite fit, though: 1/60 s is 16,666,666.67 ns. momo's `TICK_NS` is `1'000'000'000 / 60`, which integer division makes 16,666,666, 2/3 ns short. The game runs fast by one tick every 25 million ticks: one extra tick every 4.8 days.

### Time as an argument

The arithmetic goes in a module of its own, `timestep::`, and it never reads the clock. The engine reads `platform::clock_ns()` and passes the time in:

```cpp
i32 ticks = timestep::advance(&engine.timestep, platform::clock_ns());
```

That's chapter 04's trick again. An arena doesn't own its memory, so a test can hand it a buffer on the stack. The timestep doesn't own its clock, so a test can hand it made-up times: a whole second of 120 Hz frames is 120 calls with times 8.3 ms apart, and it runs in microseconds. The test links only `timestep.o` from the engine, with no SDL (chapter 03). Code that reads the clock itself can only be tested by waiting.

### The cap

When momo stalls, the time keeps passing. Stop it at a breakpoint for five seconds, and the next frame owes 300 ticks. Running them all would make the game race through five seconds in one frame: you'd see the bat jump to where it would have been. Worse is a game whose ticks take longer to run than the 1/60 s each one stands for. A frame that runs three of them then takes longer than three ticks, so the next frame owes four, takes longer still, and the game never catches up. That's called the **spiral of death**.

So a frame runs at most `MAX_TICKS_PER_FRAME` ticks, and the time beyond the cap is **dropped**. It isn't owed to the next frame, which would only run the burst a few ticks at a time. The game slows down rather than skipping ahead, and after a stall it carries on from where it stopped. Chapter 03 promised `min` for this:

```cpp
i32 ticks = (i32)min(due, (i64)MAX_TICKS_PER_FRAME);
```

How big should the cap be? Big enough that no normal frame ever hits it, and the slowest normal frame turned out to be SDL's. With vsync on Wayland, SDL waits for the compositor's frame callback: the compositor's signal that now is a good time to draw the next picture. A compositor sends none to a window it isn't drawing, so SDL gives up after 1/20 s, so that a hidden window doesn't hang its program. That happens on every run: while the window opens, 13 frames in a row take exactly 50 ms each. Each one owes three ticks. A cap of 3 would barely cover that, so momo's is 6, 100 ms. Frames twice as slow as the slowest SDL makes on purpose still keep the game at full speed, and a stall moves it forward by at most 100 ms.

Dropped time is logged with `LOG_WARN`. A stall should never pass without a trace, since the game slowed down for it.

### Vsync

Chapter 08 turned vsync off so that the sleep alone set the pace. With a fixed timestep the game's speed no longer depends on the frame rate, so the question is only how often to draw:

| Option | The catch |
|---|---|
| Vsync off, and draw as fast as possible | Measured: 291,779 frames in 10 s, and 114% of a CPU core (Mesa's driver thread takes another core) to draw pictures nobody sees |
| Vsync off, and sleep until the next tick is due | Frames at 60 a second, but not lined up with the display's refreshes, so a picture sometimes stays up one refresh too long or too short |
| **Vsync on: each swap waits until the display is ready for a new picture** | The frame rate is the display's. If vsync can't be turned on, momo falls back to the first option. |

momo takes vsync. The display sets the pace, the CPU waits instead of spinning (2% of a core, measured), and on a display whose rate is a multiple of 60, every tick stays on screen for the same number of refreshes:

| Display | Ticks per frame | Each tick is on screen for |
|---|---|---|
| 60 Hz (the external monitor) | 1 | 1 refresh |
| 120 Hz (the laptop) | 1, 0, 1, 0, ... | 2 refreshes |
| 144 Hz | 1 or 0, unevenly: 2.4 frames per tick | 2 or 3 refreshes |

The 144 Hz row is where motion would stutter, and where Fiedler's blending would help. Neither of your displays has that problem, and chapter 10 will be the first chance to see it on screen.

On Wayland, SDL doesn't use the driver's vsync at all. It sets the driver's swap interval to 0 and waits for the frame callback itself, with the 1/20 s limit above. Until it's asked to, SDL doesn't wait at all, which is why chapter 08 found vsync off by default on Wayland. A Wayland compositor shows only finished pictures, so nothing ever tears. Under X11, the driver's own vsync waits (GLX).

### The game counts its own ticks

`momo::frame_index()` is gone. The game used it to log twice a second, and in a game that runs 120 frames a second it would log four times a second. The game could have a `tick_index()` instead, but it doesn't need the engine for that: `update` runs once per tick, so the game counts its calls itself, in `GameState`.

That's a rule the plan now records. **The game's logic depends on nothing but `GameState` and its inputs.** Chapter 21 replays a recording by copying the game's memory back to where it was and feeding it the same inputs. A counter in the engine wouldn't be copied back, so a replay that read it would go differently from the original. The same reasoning gave random numbers a generator in `GameState` ([plan §4.10](plan.md#410-debug-tools-and-tests)). The engine gives the game no clock, no frame count and no `dt`.

## The code

### The timestep: `engine/timestep.h`

`engine/timestep.h:13`

```cpp
constexpr i32 TICKS_PER_SECOND = 60;
constexpr i64 TICK_NS = 1'000'000'000 / TICKS_PER_SECOND; // 16,666,666

constexpr i32 MAX_TICKS_PER_FRAME = 6;
```

With the comments from Design above them. The `'` in `1'000'000'000` is C++14's digit separator, which the compiler ignores.

`engine/timestep.h:25`

```cpp
struct Timestep {
    i64 last_ns;    // when the last frame started
    i64 owed_ns;    // time that has passed and no tick has covered yet
    i64 dropped_ns; // time the last frame dropped, because of the cap
};

namespace timestep {

void init(Timestep* step, i64 now_ns);
i32 advance(Timestep* step, i64 now_ns);

} // namespace timestep
```

Times are `i64` nanoseconds from whatever clock the caller reads. `dropped_ns` is for the engine's warning: `advance` returns the ticks to run, and the struct holds the rest of the answer.

A zeroed `Timestep` is valid (ZII), but its `last_ns` of 0 means "when SDL's clock started". The loop starts later, once the window and OpenGL are up, so with a zeroed timestep the first frame would owe all the time that took. `init` says when counting starts.

### `advance`: `engine/timestep.cpp`

`engine/timestep.cpp:11`

```cpp
i32 advance(Timestep* step, i64 now_ns)
{
    ASSERT(now_ns >= step->last_ns); // the clock never runs backwards
    step->owed_ns += now_ns - step->last_ns;
    step->last_ns = now_ns;

    i64 due = step->owed_ns / TICK_NS;
    i32 ticks = (i32)min(due, (i64)MAX_TICKS_PER_FRAME);
    step->owed_ns -= due * TICK_NS;
    step->dropped_ns = (due - ticks) * TICK_NS;
    return ticks;
}
```

Design's four lines of pseudocode, plus the cap. All the due ticks come off `owed_ns`, but only `ticks` of them are returned: the rest is dropped. `due` stays an `i64`, since it counts every tick of a stall, however long. The `(i64)` cast is for `min`, whose two arguments must have the same type (chapter 03's exercise 4). After the `min`, the result fits an `i32` easily, so the cast to `i32` loses nothing.

`init` (`engine/timestep.cpp:6`) is one line, `*step = {.last_ns = now_ns};`, which also zeroes the other two fields.

### The clock: `engine/platform.h`

`engine/platform.h:50`

```cpp
// The time now, in nanoseconds since SDL first read its clock, which in momo
// is during init. The clock never goes backwards, even when the system's
// time is set, so the difference between two readings is the time that
// passed between them. Use it between init and shutdown only: after
// shutdown, SDL's clock starts again from 0.
i64 clock_ns();
```

It replaces `sleep_ms`, which nothing calls anymore. The last sentence was found by testing: SDL's clock counts from the first time it's read, and `SDL_Quit` resets it, so a reading after `shutdown` would come out smaller than one before. That's why `clock_ns` has a section of its own in the header, instead of joining the memory and file functions, which work before `init` and after `shutdown`.

`engine/platform.cpp:125`

```cpp
i64 clock_ns()
{
    return (i64)SDL_GetTicksNS();
}
```

Without its comment, which names `CLOCK_MONOTONIC_RAW`.

### Vsync: `engine/platform.cpp`

`engine/platform.cpp:60`

```cpp
    if (!SDL_GL_SetSwapInterval(1))
        LOG_WARN("no vsync: %s", SDL_GetError());

    f32 refresh_rate = 0;
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    if (mode)
        refresh_rate = mode->refresh_rate;
```

The swap interval goes from 0 to 1. A failure is only a warning: momo still works without vsync, just as fast as it can. The display's refresh rate joins the log line that names SDL's version, so the frame count at the end of a run can be checked against it. On Wayland, SDL can't know which display a window is on until the compositor shows it, so it reports the one with the focus, which is where Hyprland opens new windows.

### The loop: `engine/momo.cpp`

`engine/momo.cpp:215`

```cpp
    start_game();
    engine.start_ns = platform::clock_ns();
    timestep::init(&engine.timestep, engine.start_ns);
    while (!engine.quit_requested) {
        if (!platform::handle_events())
            break; // the window was closed, or Ctrl+C
        reload_game_if_changed();
        arena::reset(&engine.frame); // last frame's memory is free again

        i32 ticks = timestep::advance(&engine.timestep, platform::clock_ns());
        if (engine.timestep.dropped_ns > 0)
            LOG_WARN("skipping %lld ms of game time: a frame runs at most %d ticks",
                     engine.timestep.dropped_ns / 1'000'000, MAX_TICKS_PER_FRAME);
        if (engine.game_lib) {
            for (i32 i = 0; i < ticks; i++)
                engine.game.update(engine.state);
            engine.tick_count += ticks;
        }

        renderer::begin_frame();
        platform::present(); // with vsync, waits for the display
        engine.frame_count++;
    }
    // SDL's clock starts over after platform::shutdown, so read it now.
    f64 seconds = (f64)(platform::clock_ns() - engine.start_ns) / 1e9;
```

The sleep is gone, and `update` sits inside a loop that runs `ticks` times. Everything else in the frame runs once, in the same order as before. With vsync, `present` is where the loop waits. The time is read right after events and the reload check, so the gap between two readings is one whole pass through the loop. The clock starts after `start_game`, so a slow `init` (textures, from chapter 12) doesn't count against the first frame.

When no game is loaded, after a reload that failed, the ticks are still worked out but not run. The game waits, and when it's back it carries on from where it stopped: the ticks it missed are never run.

The engine keeps `frame_count` and `tick_count` (`engine/momo.cpp:28`) only for the last log line, `quit after 9.95 s: 1115 frames, 596 ticks`. The two counts side by side show the whole chapter in one line. `frame_index()` is gone, from `momo.cpp` and from `momo.h`.

### The boundary: `engine/momo.h`

`engine/momo.h:57`

```cpp
    // Runs one tick: moves the game 1/60 s forward. The engine calls it 60
    // times a second, however many frames a second the display shows
    // (chapter 09), so it takes no time step: every tick is the same length.
    void (*update)(GameState* state);
```

Only the comment is new. The type is the same, and the game's `update` needed no change except to stop calling `frame_index`. The frame arena's comment now says the engine resets it once per frame, before the frame's ticks, so memory a tick pushes lasts until the frame is drawn.

### The game: `game/game.cpp`

`game/game.cpp:14`

```cpp
struct GameState {
    i32 tick;    // ticks run since init, which counts game time (chapter 09)
    i32 bat_x;   // pixels from the left edge of the screen
    i32 bat_dir; // 1 flying right, -1 flying left
};
```

`game/game.cpp:46`

```cpp
    if (state->tick % 30 == 0) {
        char row[SCREEN_WIDTH / TILE_SIZE] = {};
        // ...the same row of tiles as before...
        LOG_INFO("tick %4d  |%.*s|  x = %3d", state->tick, ARRAY_COUNT(row), row, state->bat_x);
    }

    state->tick++;
```

The log moved inside an `if`, so the counter can go up at the end of `update` on every tick, in one place. `GameState` grew by 4 bytes, and a new size alone tells the engine the layout changed (chapter 06), so `GAME_STATE_VERSION` stays at 1. `BAT_SPEED` is "pixels per tick" now.

### The tests: `tests/timestep_test.cpp`

`tests/timestep_test.cpp:37`

```cpp
static void a_120_hz_display_runs_a_tick_every_other_frame()
{
    Timestep step = {};
    timestep::init(&step, START_NS);
    i64 now = START_NS;
    for (i32 frame = 0; frame < 120; frame++) {
        now += TICK_NS / 2;
        CHECK_EQ(timestep::advance(&step, now), frame % 2);
    }
}
```

A second of the laptop's frames, in made-up time. `TICK_NS` is even, so half a tick is exact, and the frames alternate 0, 1, 0, 1. `START_NS` is 5 s, as if SDL had been running for a while, so no test depends on the clock starting at 0. The file has eight tests: no time, one tick, 60 Hz, 120 Hz, the 20 Hz window, 1,000 frames a second (which still makes exactly 60 ticks), a long frame hitting the cap, and the frame after it running normally.

### The Makefile

Unchanged. `engine/timestep.cpp` joins `libmomo.so` and `libmomo.a` through the `engine/*.cpp` wildcard, and `tests/timestep_test.cpp` becomes a test program through `tests/*_test.cpp`.

## Run it

### 60 ticks a second

zsh's `time` prints how long a command ran and how much CPU it used:

```
$ make
...
$ time ./build/debug/momo
engine/momo.cpp:79: reserved 64 MB at 0x7b2325e00000
engine/momo.cpp:205: loaded /home/mairuu/repos/momo/build/debug/game.so
engine/platform.cpp:71: SDL 3.4.18, video driver wayland, window 768x720, display 120 Hz
engine/renderer.cpp:84: OpenGL 4.6 (Core Profile) Mesa 26.2.4-arch1.1, AMD Radeon 880M Graphics (radeonsi, strix1, ACO, DRM 3.64, 7.2.8-arch1-2)
game/game.cpp:28: init
game/game.cpp:51: tick    0  |B...............|  x =   1
game/game.cpp:51: tick   30  |.B..............|  x =  31
game/game.cpp:51: tick   60  |...B............|  x =  61
...
game/game.cpp:51: tick  540  |...B............|  x =  61
game/game.cpp:51: tick  570  |.....B..........|  x =  91
```

The log lines come twice a second, as before. Press Ctrl+C after about ten seconds:

```
^Cengine/platform.cpp:96: quit requested
game/game.cpp:59: shutdown, with the bat at x = 116
engine/momo.cpp:250: quit after 9.95 s: 1115 frames, 596 ticks
...
-----------------------------------------------------
Suppressions used:
  count      bytes template
      1        256 libSDL3.so
-----------------------------------------------------

./build/debug/momo  0.13s user 0.09s system 2% cpu 10.035 total
```

596 ticks in 9.95 s is 60 a second. (9.95 × 60 is 597: the last tick or so of time was still owed when momo quit.) 1115 frames is 112 a second, not 120. Counting how long every frame took (exercise 4 does it) shows where the rest went:

| Frame took | Frames | Ticks each |
|---|---|---|
| 50 ms | 13 | 3 |
| 16 ms | 14 | 1 |
| 8 ms | about 1085 | 1 or 0, alternating |

That's from another ten-second run, of 1116 frames. The 13 slow frames come right at the start, in the 0.65 s it takes the window to open. Hyprland sends no frame callbacks until the window is on screen, so SDL waits its full 1/20 s each time. That's the frame from Design's cap section, and each one runs its three ticks. The game didn't lose a tick, and no warning appeared. The 16 ms frames each missed one refresh, and ran the one tick due. With vsync, momo used 2% of a CPU: it spent the rest of the time waiting for the display.

### Without vsync

In `platform::init`, change the swap interval to 0: `SDL_GL_SetSwapInterval(0)` (`engine/platform.cpp:60`). `make`, run it the same way for ten seconds, and the end changes:

```
game/game.cpp:59: shutdown, with the bat at x = 116
engine/momo.cpp:250: quit after 9.94 s: 291779 frames, 596 ticks
...
./build/debug/momo  5.43s user 6.16s system 114% cpu 10.144 total
```

29,000 frames a second, and the same 596 ticks. The bat ends at x = 116, the same pixel as with vsync, because the game ran the same ticks. Meanwhile momo used more than a whole CPU core: its own thread, plus Mesa's, to draw pictures the display mostly threw away. Put the 1 back.

### A stall

Run momo, and after a few seconds press Ctrl+Z in the terminal. The shell stops momo: the bat freezes, and the window stops redrawing. Wait three seconds, then type `fg` to continue it:

```
$ ./build/debug/momo
...
game/game.cpp:51: tick  150  |.........B......|  x = 151
game/game.cpp:51: tick  180  |...........B....|  x = 181
^Z
zsh: suspended  ./build/debug/momo
$ fg
[1]  + continued  ./build/debug/momo
engine/momo.cpp:229: warning: skipping 2916 ms of game time: a frame runs at most 6 ticks
game/game.cpp:51: tick  210  |.............B..|  x = 211
game/game.cpp:51: tick  240  |..............B.|  x = 239
```

The first frame after `fg` found 3 seconds owed. It ran 6 ticks, 100 ms of them, and dropped the other 2916 ms. The bat carried on from x = 181 to 211 at its usual pace: no leap across the screen. Without the cap, that frame would have run 180 ticks at once, and the bat would have bounced off the right edge and come halfway back between two pictures. Exercise 3 tries it.

A breakpoint in gdb does the same: the program stops, the clock doesn't, and the first frame after `continue` logs the skip.

### Look inside: where the frame waits

With vsync on, momo used 2% of a CPU. The rest of the time it was waiting, and gdb can show where. Run it in gdb and press Ctrl+C after a few seconds. gdb stops momo before SDL sees the signal, so momo doesn't quit:

```
$ gdb -q ./build/debug/momo
(gdb) run
...
game/game.cpp:28: init
^C
Thread 1 "momo" received signal SIGINT, Interrupt.
0x00007ffff68a08f2 in ?? () from /usr/lib/libc.so.6
(gdb) backtrace
#0  0x00007ffff68a08f2 in ?? () from /usr/lib/libc.so.6
#1  0x00007ffff69119d7 in ppoll () from /usr/lib/libc.so.6
#2  0x00007ffff79007e8 in __interceptor_ppoll () from /usr/lib/libasan.so.8
#3  0x00007ffff65e6b24 in ?? () from /usr/lib/libSDL3.so.0
#4  0x00007ffff65d02b0 in ?? () from /usr/lib/libSDL3.so.0
#5  0x00007ffff7f4acab in platform::present () at engine/platform.cpp:112
#6  0x00007ffff7f4a4fa in momo::run () at engine/momo.cpp:238
#7  0x0000555555555182 in main () at host/main.cpp:7
(gdb) quit
```

Read from the bottom: `run` called `present`, which called `SDL_GL_SwapWindow`, and SDL is inside `ppoll`, the system call that sleeps until a file has something to read. The file is the connection to the Wayland compositor, and what SDL is waiting for is the frame callback. (`__interceptor_ppoll` is AddressSanitizer looking at the call on its way through.) Interrupt it a few more times, and nearly every stop is here. It's a cheap way to measure where a program spends its time, sometimes called the poor man's profiler. Answer `y` to quit.

### The tests

```
$ make test
...
timestep_test
  no_time_runs_no_ticks ... ok
  one_tick_of_time_runs_one_tick ... ok
  a_60_hz_display_runs_one_tick_a_frame ... ok
  a_120_hz_display_runs_a_tick_every_other_frame ... ok
  a_20_hz_display_runs_three_ticks_a_frame ... ok
  a_thousand_frames_a_second_still_run_60_ticks ... ok
  a_long_frame_runs_only_the_cap ... ok
  dropped_time_is_not_owed_to_the_next_frame ... ok
  8 passed
```

A second of 120 Hz frames, a second of a thousand, and a one-second stall, all in no time. `ldd build/debug/tests/timestep_test | grep SDL` prints nothing: the test never touched SDL, since `timestep.o` doesn't call it.

## Recap

```
momo/
  engine/
    timestep.h      new: ticks and frames, TICK_NS, the cap, Timestep
    timestep.cpp    new: init, advance
    platform.h      + clock_ns; sleep_ms is gone
    platform.cpp    + vsync on, the display's refresh rate in the log, clock_ns
    momo.h          - frame_index; update is one tick
    momo.cpp        + the loop runs 0…6 ticks a frame, logs dropped time,
                      and counts frames and ticks for the last log line
  game/
    game.cpp        + counts its own ticks in GameState
  tests/
    timestep_test.cpp  new: eight tests with made-up times
```

- A tick is one 60 Hz update of the game. A frame is one picture. On the NES they were the same, so MegaMan's "per frame" speeds are momo's per tick.
- A variable timestep gives different results at different frame rates: the same jump reaches 45, 47.5 or 48.75 pixels. A fixed timestep gives the same result every time.
- Each frame adds the time since the last one to `owed_ns`, runs a tick for every whole tick of it, and keeps the rest.
- A frame runs at most 6 ticks. Time over the cap is dropped and logged, so a stall slows the game instead of making it race.
- Time is integer nanoseconds, passed into `timestep::advance` instead of read by it, so tests make up their own times.
- Vsync is on. The display sets the frame rate, the CPU waits in the swap, and on Wayland SDL waits for the compositor itself, giving up after 50 ms.
- The game counts its own ticks. Its logic depends only on `GameState` and its inputs, which chapter 21's replays need.

The plan records ticks and frames, the accumulator, the cap and the game's own tick count in [§4.3](plan.md#43-the-frame), and vsync in [§4.5](plan.md#45-rendering).

Chapter 10 draws the bat as a rectangle, many rectangles in one draw call, and gives the game its `render`: once per frame, after the ticks.

## Exercises

Undo each change before the next exercise: `git checkout -- engine game` puts the code back.

1. **The other monitor.** Focus a workspace on the external monitor, start momo there, and quit after about ten seconds. What does the log say about the display, and how many frames and ticks does the last line count? Where does the bat end up, compared with a ten-second run on the laptop's panel?
2. **The NES cap.** Set `MAX_TICKS_PER_FRAME` to 1, as if each frame could run only one update, the NES way. Which tests fail, and why? Run momo: how many warnings appear at the start, and what do they say? What would the game do for the whole run on a 30 Hz display?
3. **No cap.** Set `MAX_TICKS_PER_FRAME` to 100000. Repeat the stall from Run it. Where is the bat after `fg`? Watch the log as well as the window. Why is a burst of ticks worse in a game where the player can die?
4. **Count the frames.** Give the `Engine` struct two `i32` arrays: one counting frames by how long they took in whole milliseconds (64 entries), and one by how many ticks they ran (8 entries). Fill them in `run`, keeping each index in range with `min`, and log every count that isn't 0 before the `quit after` line. Do your numbers match the table in Run it? Which frames ran 3 ticks? Then change the swap interval to 0 and run again.
5. **X11.** Run `SDL_VIDEO_DRIVER=x11 ./build/debug/momo`. What does the first warning say? Under X11, SDL leaves the waiting to the driver, with no 1/20 s limit of its own. What do you think the first swap was waiting for? Is dropping that time the right thing for a game that has only just started?
