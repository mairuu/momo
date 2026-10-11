# 15 · Fixed-Point

> **Goal:** the player walks at MegaMan's speed, 1.375 pixels a tick: 11 pixels every 8 ticks, a step of 2 on three of them and a step of 1 on the rest. Every position and speed in the game becomes a fixed-point number, `Fx`, an `i32` that counts 256ths of a pixel, and the game turns positions into whole pixels in one place, where it draws them. Change the walking speed to a quarter of a pixel while momo runs, and the player creeps one pixel every four ticks. The bat flies exactly as it did: a capture from this chapter is the same file, byte for byte, as one from chapter 14.

## The problem

Every position in momo is a whole number of pixels, so every speed is too. The player walks 1 pixel a tick, and the next speed up is 2. MegaMan walks 1.375 pixels a tick in MegaMan 1 and 2, [according to TASVideos](https://tasvideos.org/GameResources/NES/Rockman), where speed-runners record such numbers, and 1.3125 in the air. Chapter 20's gravity changes the speed by a fraction of a pixel every tick. None of those fit in an `i32` of pixels.

Earlier chapters left this one three promises. Chapter 09 made every tick the same length, so that the game gives the same answers on every run, and chapter 21's replays depend on that. Chapter 10 made the renderer take whole pixels, and said that the game's positions would round to whole pixels once, where the game draws them. Chapter 03 showed UBSan stopping an `i32` that overflowed, and said that this chapter's multiply is the kind of code where such an overflow hides.

[Plan §4.6](plan.md#46-numbers) decided the type: `Fx`, an `i32` counting 256ths of a pixel, and `Fx2`, two of them. This chapter works out what that means in code: how the game writes `1.375`, how an `Fx` becomes a pixel, and how two of them multiply.

## Design

### Why not floats

Floats are the usual answer. LearnOpenGL moves its camera with them, and so do most engines.

Floats are binary fractions, so they hold 1.375 exactly: it's 11/8. They can't hold 0.1, which goes on forever in binary, as 1/3 does in decimal. A throwaway program added 0.1 to a position 600 times, once a tick for ten seconds, and floored each sum to the pixel it would be drawn at:

| Type | Ticks drawn one pixel off | The first |
|---|---|---|
| `f32` | 30 of 600 | tick 30: the sum is 2.9999993, drawn at 2 instead of 3 |
| `f64` | 15 of 600 | tick 10: ten 0.1s add up to 0.9999999999999999, drawn at 0 instead of 1 |

Each add rounds, by an amount that depends on how big the sum already is, so the error grows and shrinks as the position moves. After 160 ticks the `f32` should be exactly on a tile's edge, at 16, and it's at 16.0000248, a hair past it.

A float's answer can also depend on how the code was compiled. The same function, `a * b + c` with 0.1, 0.1 and -0.01, compiled by GCC 16 with `-O2`, returned 9.31e-10. Add `-march=x86-64-v3`, which lets GCC use newer instructions, and it returned 5.22e-10: GCC fused the multiply and the add into one FMA instruction, which rounds once instead of twice. momo builds for plain x86-64, but chapter 21's replays need the game to give the same answers every time, and an integer's answer doesn't depend on flags.

| Option | The catch |
|---|---|
| `f32` or `f64` | Numbers like 0.1 are approximations, and every add rounds again. The answer can change with compiler flags, and `==` is risky. |
| Whole pixels, with a pattern of steps for each speed: 1, 1, 2, 1, 1, 2, 1, 2 for 1.375 | Every speed needs its own table, and a speed that changes every tick, like a fall, can't have one |
| **Fixed point: an `i32` that counts 256ths of a pixel** | Only 256ths. A multiply needs a shift and 64 bits. |

momo takes the last, as the plan decided. An `Fx` can hold only whole numbers of 256ths, so every add and subtract is exact: integer arithmetic, the same on every run, in every build, on every CPU. `==` works, and the tests compare with `CHECK_EQ`.

`Fx` can't hold 0.1 either. It rounds to 26/256, 0.1015625, and 600 ticks of it come to 60.9375, further from 60 than either float. But the error is in the constant, once, and you can see it: the number is 26. Every tick adds exactly 26, so where a thing is after any number of ticks is known exactly. Floats would hold MegaMan's numbers exactly too, if you were careful to write only 256ths. With `Fx`, that's the only thing you can write.

The NES had no floats at all. NES games, MegaMan among them, kept each position as whole pixels plus one byte of fraction, 256ths of a pixel, which speed-runners call **subpixels**. MegaMan's 1.375 is 1 pixel and 96 subpixels.

### 24.8

A fixed-point `i32` splits its 32 bits between whole pixels and fraction. More bits of fraction make finer steps, and fewer whole pixels:

| Bits of fraction | Smallest step | Positions up to |
|---|---|---|
| 4 | 1/16 pixel | ±134 million pixels |
| **8: momo, as the NES** | **1/256 pixel** | **±8.4 million pixels** |
| 16: Doom's `fixed_t` | 1/65,536 pixel | ±32,768 pixels |

8 bits of fraction is a **24.8** number: 24 bits of whole pixels and 8 of fraction. 8.4 million pixels is 32,768 screens side by side. MegaMan's numbers are 256ths, so they copy over exactly, and nothing in the game asks for finer steps.

### A type of its own

The plain way is another name for `i32`: `using Fx = i32;`. Then this compiles:

```cpp
state->player_pos.x += 1; // meant one pixel, adds 1/256
```

The number is right, the unit is wrong, and nothing complains. momo makes `Fx` a struct holding the `i32`, called `raw`, so the line above is a compile error (Run it shows it). [Plan §3.1](plan.md#31-rules) allows operator overloading for math types, so `Fx` gets `+`, `-`, `*`, `+=`, `-=` and the six comparisons. Each is one line on `raw`.

The struct costs nothing in the release build. A struct holding one `i32` is passed in a register like an `i32`, and with optimization on, the compiler copies each operator into the code that calls it, so adding two `Fx` is one add instruction (Run it looks). The operators are `constexpr` functions in a header with no `.cpp`, like `base.h`'s `min` and `max`, so the game compiles its own copy, and none of them is `MOMO_API`. That also means `min`, `max` and `clamp` work on `Fx` as they are: they're templates that need only `<` and `>`.

`+=` and `-=` return nothing, so `a = b += c` doesn't compile. Nothing in momo needs it, and a change hidden in the middle of a longer line is what plan §3's test, reading one line by itself, is there to catch.

### Writing numbers

Whole numbers are easy: `fx::from_int(48)` is 48 × 256. Fractions are harder to write:

| Option | The catch |
|---|---|
| The raw number, `{ .raw = 352 }`, or in hex, `0x160`, as NES disassemblies write numbers | You have to divide by 256 in your head to read it |
| A ratio, `fx::from_ratio(11, 8)` | Exact, but 11/8 doesn't read as 1.375 |
| **A decimal, `fx::from_double(1.375)`, which only the compiler may call** | A `double` in the game's source, though never in the running game |

momo takes the decimal. `from_double` is **`consteval`**, a C++20 keyword: every call to it is worked out while compiling, and a call the compiler can't work out, because its argument isn't known until the game runs, doesn't compile. So the constant is a plain number in `game.so`, 352, and the game never computes with a `double` while it runs.

A decimal that isn't a whole number of 256ths, like 0.1, rounds to the nearest one. Refusing it instead would make you write 0.1015625 to mean "about a tenth". Rounding moves the speed by at most 1/512 of a pixel, the same every tick, and the tests show what 0.1 becomes.

Both `from_int` and `from_double` `ASSERT` that the number fits. An `ASSERT` that fails while the compiler works out a constant is a compile error, which names the condition that failed.

### Back to pixels

Chapter 10 said positions round to whole pixels once, where the game draws them. Which way they round matters below zero:

| Option | 1.75 | -0.25 | The catch |
|---|---|---|---|
| Divide `raw` by 256, as C++ divides: toward zero | 1 | 0 | Everything from -0.99 to 0.99 is drawn at 0, so pixel 0 is two pixels wide |
| Round to the nearest | 2 | 0 | An extra add before the shift, for no gain here |
| **Floor: the whole number at or below** | **1** | **-1** | None: every pixel is one pixel wide |

momo floors, with `fx::floor`. It's `raw >> 8`: shifting right by 8 drops the fraction's bits. For a negative number, C++20 defines the shift as rounding down, so -0.25, which is -64, shifts to -1. Floor is also what the NES did, when it drew a thing at its byte of whole pixels.

Nothing in the game goes below zero yet. Chapter 23's camera will draw every position relative to the camera, and an enemy partly off the left edge of the screen is at a negative x. Drawn with division, it would stay at pixel 0 twice as long as at any other pixel.

At 1.375 a tick, the player's position and the pixel it's drawn at go like this, starting at x = 48:

| Tick | x | `raw` | Drawn at | Moved |
|---|---|---|---|---|
| 0 | 48 | 12,288 | 48 | |
| 1 | 49.375 | 12,640 | 49 | 1 |
| 2 | 50.75 | 12,992 | 50 | 1 |
| 3 | 52.125 | 13,344 | 52 | 2 |
| 4 | 53.5 | 13,696 | 53 | 1 |
| 5 | 54.875 | 14,048 | 54 | 1 |
| 6 | 56.25 | 14,400 | 56 | 2 |
| 7 | 57.625 | 14,752 | 57 | 1 |
| 8 | 59 | 15,104 | 59 | 2 |

After 8 ticks the fraction is back to 0, and the pattern starts again. That's the table the second option in "Why not floats" would have needed by hand, worked out by the arithmetic.

### Multiplying

Adding two `Fx` adds their `raw` numbers. Multiplying them doesn't work the same way. Each `raw` is 256 times its value, so the product of two is 256 × 256 times too big:

```
1.5 × 2.5:  384 × 640 = 245,760    which is 3.75 × 65,536
            245,760 >> 8 = 960     which is 3.75 × 256
```

Shifting right by 8 takes one 256 back out. The shift rounds down, like `floor`.

The product before the shift is the problem. 1000 × 1000 is 256,000 × 256,000 in `raw` numbers, about 2^36, which doesn't fit in an `i32`. The answer, 1,000,000, does. So the product is worked out in an `i64`, and an `ASSERT` checks that the answer fits back into an `i32`. Without it, C++20 would quietly wrap the `i64` into an `i32`: converting an integer that doesn't fit isn't undefined behavior, so UBSan wouldn't notice. Run it breaks the multiply on purpose, to show what chapter 03 warned about.

Nothing in the game multiplies two `Fx` yet. Speeds add to positions, and chapter 20's gravity adds to speeds. A shot aimed at the player will need a multiply, and it's the one operation where fixed point needs thought, so it's here, with its tests. Division waits until something needs it: exercise 1 writes it.

### `Fx2`

An `Fx2` is a point or a step on the screen: an `x` and a `y`, both `Fx`. It gets `+`, `-`, `+=`, `-=`, `==` and `!=`, so a position moves by its velocity in one line, `pos += vel`. There's no `Fx2` times a number yet, for the same reason as division.

### The game

Every position and speed in the game becomes `Fx`. `GameState` holds where the bat and the player are as `Fx2`s, and the bat's velocity as an `Fx2` too, in place of `bat_dir`. The bat turns around by negating its velocity. `render` floors each position into the pixels it draws at, and nothing else in the game turns an `Fx` into pixels.

The player walks at 1.375 pixels a tick, `fx::from_double(1.375)`. The bat keeps its speed of 1 pixel a tick, `fx::from_int(1)`. That makes it a check on the change: the bat should be in exactly the same place on every tick as in chapter 14, and every capture should be the same file.

## The code

### Fixed-point numbers: `engine/fx.h`

`engine/fx.h:27`

```cpp
struct Fx {
    i32 raw; // the number times 256
};

// A point, or a step, on the screen: x to the right and y down, in pixels.
struct Fx2 {
    Fx x;
    Fx y;
};
```

A zeroed `Fx` is 0.0, and a zeroed `Fx2` is the point (0, 0) (ZII). `FX_FRACTION_BITS` is 8, and `FX_ONE`, the `raw` number for 1.0, is 256 (`engine/fx.h:17`). `FX_MIN_INT` and `FX_MAX_INT` are the whole numbers an `Fx` holds, from -8,388,608 to 8,388,607.

`engine/fx.h:50`

```cpp
consteval Fx from_double(f64 x)
{
    ASSERT(x >= FX_MIN_INT && x <= FX_MAX_INT);
    f64 scaled = x * FX_ONE;
    return { .raw = (i32)(scaled < 0 ? scaled - 0.5 : scaled + 0.5) };
}
```

Without its comment. A cast from `double` to `i32` drops the fraction, toward zero, so adding a half first, or taking one away below zero, rounds to the nearest 256th. `from_int` (`engine/fx.h:40`) is the same with `x * FX_ONE`, and `constexpr` instead of `consteval`, since a whole number from a running game is fine: `fx::from_int(SCREEN_WIDTH - PLAYER_WIDTH)`.

`engine/fx.h:61`

```cpp
constexpr i32 floor(Fx x)
{
    return x.raw >> FX_FRACTION_BITS;
}
```

Its comment says which way it rounds, and why not `/`. Next to it, `fx::to_double` (`engine/fx.h:67`) turns an `Fx` into a `double` for printing. A `double` holds every `Fx` exactly, and it's the one way out of `Fx` that isn't whole pixels. Nothing turns a `double` back into an `Fx` while the game runs, since `from_double` is `consteval`.

`engine/fx.h:95`

```cpp
constexpr Fx operator*(Fx a, Fx b)
{
    i64 product = ((i64)a.raw * b.raw) >> FX_FRACTION_BITS;
    ASSERT(product >= INT32_MIN && product <= INT32_MAX); // the answer must fit an Fx
    return { .raw = (i32)product };
}
```

Without its comment. `(i64)a.raw` widens one side before the multiply, and C++ then multiplies in `i64`. Writing `(i64)(a.raw * b.raw)` instead would multiply in `i32` first, and widen the overflow.

`engine/fx.h:103`

```cpp
constexpr void operator+=(Fx& a, Fx b)
{
    a.raw += b.raw;
}
```

The first function in momo with a non-`const` reference for a parameter: an operator can't take a pointer, and `p += v` shows at the call that `p` changes, which is why plan §3.1 asks for pointers. The other operators are the same shape: `+`, `-` and negation return a new `Fx`, and the comparisons compare `raw`. `Fx2`'s operators (`engine/fx.h:145`) do the same to `x` and `y`.

`momo.h` includes `fx.h`, so the game gets it with the rest of the engine.

### The game: `game/game.cpp`

`game/game.cpp:17`

```cpp
struct GameState {
    i32 tick;            // ticks run since init, which counts game time (chapter 09)
    Fx2 bat_pos;         // the bat's top left corner, from the screen's top left
    Fx2 bat_vel;         // pixels per tick: BAT_SPEED to the right or to the left
    Texture bat_texture; // bat.png, a number the renderer gave out (chapter 12)
    Fx2 player_pos;      // the player's top left corner
    i32 power;           // the player's power, 0 to POWER_COUNT - 1, which shows as its color
};
```

`bat_x`, `bat_dir` and `player_x` are gone. The comment above the struct says positions and speeds are fixed-point, and that `render` alone turns them into pixels.

`game/game.cpp:51`

```cpp
// MegaMan's walking speed in MegaMan 1 and 2: 1 pixel and 96/256 a tick.
constexpr Fx PLAYER_SPEED = fx::from_double(1.375); // pixels per tick
```

`BAT_SPEED` is `fx::from_int(1)`. `init` (`game/game.cpp:72`) sets the bat's `y` to `BAT_Y` and its velocity to `BAT_SPEED` to the right, and stands the player on the floor at x = 48.

`game/game.cpp:86`

```cpp
    // Fly, and turn around at either edge of the screen.
    state->bat_pos += state->bat_vel;
    Fx left_edge = fx::from_int(0);
    Fx right_edge = fx::from_int(SCREEN_WIDTH - TILE_SIZE);
    if (state->bat_pos.x <= left_edge || state->bat_pos.x >= right_edge) {
        state->bat_pos.x = clamp(state->bat_pos.x, left_edge, right_edge);
        state->bat_vel.x = -state->bat_vel.x;
    }
```

The same steps as before, on `Fx`: move, and at either edge, stop at the edge and turn around. `clamp` is `base.h`'s, working on `Fx` through its `<` and `>`. The player's walk (`game/game.cpp:95`) is chapter 14's, with `player_pos.x` and `PLAYER_SPEED` in place of `player_x` and 1, and `clamp` on `Fx`.

`game/game.cpp:134`

```cpp
    i32 x = fx::floor(state->bat_pos.x);
    i32 y = fx::floor(state->bat_pos.y);
```

In `render`, where the bat is drawn. The player's `Rect` floors its `x` and `y` the same way. The shutdown log prints both `x` positions with `fx::to_double` and `%g`.

### The tests: `tests/fx_test.cpp`

Thirteen tests. Each compares `raw` numbers, or the pixels `floor` gives, with `CHECK_EQ`: an `Fx` is exact, so the tests can be too. Design's table is one of them:

`tests/fx_test.cpp:74`

```cpp
static void walking_1_375_a_tick_moves_11_pixels_every_8_ticks()
{
    // The player's walk (game.cpp), from x = 48: the pixel it's drawn at
    // moves 1, 1, 2, 1, 1, 2, 1, 2, then the same again.
    constexpr i32 DRAWN_AT[] = { 49, 50, 52, 53, 54, 56, 57, 59, 60, 61, 63, 64, 65, 67, 68, 70 };
    Fx x = fx::from_int(48);
    for (i32 tick = 0; tick < ARRAY_COUNT(DRAWN_AT); tick++) {
        x += fx::from_double(1.375);
        CHECK_EQ(fx::floor(x), DRAWN_AT[tick]);
    }
    CHECK(x == fx::from_int(70)); // 48 + 16 * 1.375, with nothing left over
}
```

The others check that whole numbers and 256ths convert exactly, that 0.1 rounds to 26/256, that `floor` rounds down below zero, that 600 adds of a tenth come to exactly 600 × 26, that multiplying keeps the fraction and rounds down, that 1000 × 1000 works, that `clamp` and the comparisons work on `Fx`, and that `Fx2` works on both coordinates.

Every `from_double` in the tests is worked out by the compiler, as the game's are. Everything else runs the way the game runs it: in the debug build, as a function call, under the sanitizers.

## Run it

### Walking

```
$ make
$ ./build/debug/momo
```

Hold the right arrow. The player walks faster than in chapter 14, and moves a little unevenly, if you look closely: a step of 2 pixels on three ticks of every 8. Quit:

```
game/game.cpp:160: shutdown, with the bat at x = 128, the player at x = 130.5, power 0
```

That run came from a small program sending key events under X11, as in chapter 14: the right arrow held for 1,000 ms, which is 60 ticks. 48 + 60 × 1.375 = 130.5, drawn at 130. Chapter 14's 60 ticks walked to 108.

### Slower than a pixel

Run momo again, and walk right for a second. With momo still running, change `PLAYER_SPEED` to a quarter of a pixel:

```cpp
constexpr Fx PLAYER_SPEED = fx::from_double(0.25); // pixels per tick
```

and run `make` in another terminal. The log shows the reload:

```
engine/momo.cpp:310: reloaded /home/mairuu/repos/momo/build/debug/game.so
```

and nothing more: `GameState` didn't change, so the player stays where you left it. Hold right again. It creeps, one pixel every 4 ticks, 15 pixels a second, a speed chapter 14's whole pixels couldn't have. The same keys, a second before the change and a second after:

```
game/game.cpp:160: shutdown, with the bat at x = 131, the player at x = 145.5, power 0
```

130.5 from the first second, and 60 × 0.25 = 15 from the second. Put 1.375 back.

### The bat didn't move

A capture from this chapter's build and one from chapter 14's are the same file. `git worktree` checks chapter 14 out into a folder of its own, next to your working copy:

```
$ ./build/debug/momo --capture 520 /tmp/15.png
$ git worktree add /tmp/momo-ch14 ch14
$ make -C /tmp/momo-ch14
$ /tmp/momo-ch14/build/debug/momo --capture 520 /tmp/14.png
$ cmp /tmp/14.png /tmp/15.png
$ git worktree remove /tmp/momo-ch14
```

`cmp` prints nothing: the files are identical. By tick 520 the bat has turned around at the right edge, at tick 240, and the left, at tick 480. Captures at ticks 0, 72, 170 and 600 matched too. The bat's arithmetic changed from `i32` pixels to `Fx`, and not one pixel of the picture did.

### What the compiler made of it

```
$ make release
$ objdump -d --no-show-raw-insn -C build/release/obj/game/game.o | grep '\$0x160'
 328:	subl   $0x160,0x18(%rbx)
 340:	add    $0x160,%eax
```

`objdump -d` disassembles the release build's `game.o` into machine instructions, and `-C` writes C++ names the way the source does. Those two lines are the walk, left and right: 0x160 is 352, which is 1.375 × 256, written into the instructions by the compiler. `0x18(%rbx)` is `player_pos.x`, 24 bytes into `GameState`. No `double`, no call to `from_double` or to `operator+=`, nothing but integer adds. The bat's `pos += vel`, both coordinates, became one `paddd`, an instruction that adds two pairs of `i32` at once. The debug build, at `-O0`, calls each operator as a function, with the sanitizers' checks inside.

### Break it: an `int` in an `Fx`

In `update`, change the walk right to add 1:

```cpp
        state->player_pos.x += 1;
```

```
$ make
game/game.cpp: In function ‘void game::update(GameState*)’:
game/game.cpp:99:29: error: no match for ‘operator+=’ (operand types are ‘Fx’ and ‘int’)
   99 |         state->player_pos.x += 1;
      |         ~~~~~~~~~~~~~~~~~~~~^~~~
  • there are 2 candidates
...
    • candidate 2: ‘constexpr void operator+=(Fx&, Fx)’
      engine/fx.h:103:16:
...
      • no known conversion for argument 2 from ‘int’ to ‘Fx’
```

With `using Fx = i32`, this would have compiled, and the player would have walked 1/256 of a pixel a tick: a pixel every four seconds. `fx::from_int(1)` is what the line meant. Put `PLAYER_SPEED` back.

### Break it: multiply in 32 bits

In `operator*` (`engine/fx.h:97`), take out the `(i64)`:

```cpp
    i64 product = (a.raw * b.raw) >> FX_FRACTION_BITS;
```

```
$ make test
...
fx_test
  ...
  multiplying_big_numbers_works_in_64_bits ... engine/fx.h:97:26: runtime error: signed integer overflow: 256000 * 256000 cannot be represented in type 'int'
...
failed: build/debug/tests/fx_test
make: *** [Makefile:120: test] Error 1
```

UBSan stops the test at the multiply, as chapter 03 showed it would. The release build has no UBSan. Build its tests and run this one:

```
$ make release
$ make CONFIG=release build/release/tests/fx_test
$ ./build/release/tests/fx_test big
fx_test
  multiplying_big_numbers_works_in_64_bits ... FAIL
tests/fx_test.cpp:109: CHECK_EQ((thousand * thousand).raw, fx::from_int(1'000'000).raw) failed: 4341760 != 256000000
  0 passed, 1 failed
```

1000 × 1000 = 16,960. The product wrapped around, the shift and the `ASSERT` saw a number that fit, and only the test knew the answer was wrong. Without a test of big numbers, nothing would have. Put the `(i64)` back.

### Break it: divide instead of floor

In `fx::floor`, divide instead of shifting:

```cpp
    return x.raw / FX_ONE;
```

```
$ make test
...
fx_test
  ...
  floor_rounds_down_below_zero_too ... FAIL
tests/fx_test.cpp:51: CHECK_EQ(fx::floor(fx::from_double(-0.25)), -1) failed: 0 != -1
...
```

Every other test passes, and so would the game: nothing in it is below zero. Put the shift back.

### Memory

```
engine/momo.cpp:104: permanent arena: at most 36 bytes used of 16 MB
```

`GameState` grew from 24 bytes to 36: three `Fx2`s of 8 bytes in place of `bat_x`, `bat_dir` and `player_x`.

### The tests

```
$ make test
...
fx_test
  zero_is_zero ... ok
  whole_numbers_count_256ths ... ok
  decimals_that_are_256ths_are_exact ... ok
  other_decimals_round_to_the_nearest_256th ... ok
  floor_drops_the_fraction ... ok
  floor_rounds_down_below_zero_too ... ok
  adding_and_subtracting_are_exact ... ok
  walking_1_375_a_tick_moves_11_pixels_every_8_ticks ... ok
  multiplying_keeps_8_bits_of_fraction ... ok
  multiplying_rounds_down ... ok
  multiplying_big_numbers_works_in_64_bits ... ok
  comparisons_and_clamp_work_on_fx ... ok
  fx2_works_on_both_coordinates ... ok
  13 passed
...
```

## Recap

```
momo/
  engine/
    fx.h            new: Fx and Fx2, fixed-point numbers with 8 bits of fraction; from_int,
                      from_double for constants, floor to pixels, and their operators
    momo.h          includes fx.h
  game/
    game.cpp        positions and speeds are Fx; the player walks 1.375 pixels a tick;
                      render floors positions to pixels
  tests/
    fx_test.cpp     new: thirteen tests of Fx and Fx2
```

- An **`Fx`** is an `i32` counting 256ths of a pixel: 24 bits of whole pixels and 8 of fraction, the NES's subpixels. Adding and subtracting are exact, the same in every build.
- Floats measured one pixel off on 30 of 600 ticks, at 0.1 a tick, and gave another answer with another `-march`. An `Fx` rounds once, in the constant, where you can see it.
- `Fx` is a **struct**, so `x += 1` is a compile error instead of 1/256 of a pixel. Its operators are `constexpr` in a header, and in the release build they cost what an `i32`'s do.
- Constants are decimals, **`fx::from_double(1.375)`**, which only the compiler can call. The running game never computes with a `double`.
- Positions become pixels with **`fx::floor`**, a shift that rounds down below zero too, in `render` alone.
- A **multiply** works in 64 bits and `ASSERT`s that the answer fits. In 32 bits, UBSan caught it in the debug build, and the release build said 1000 × 1000 = 16,960.

The plan records how `Fx` is written, converted and multiplied in [§4.6](plan.md#46-numbers).

Before chapter 16, Appendix A drafts `book/design.md`, the game's design, for you to decide its theme, names and tone ([plan §2](plan.md#2-the-book)). Chapter 16 then turns the bat and the player into entities.

## Exercises

Undo each change before the next exercise: `git checkout -- engine game tests` puts the code back.

1. **Division.** Write `operator/(Fx a, Fx b)`: shift `a.raw` left by 8 in an `i64` first, so the answer keeps its fraction, then divide by `b.raw`. Add a test: 3.75 / 1.5 should be 2.5. Which way does it round -1.0 / 3.0, and does that match `floor`? What should dividing by zero do?
2. **A tenth of a pixel.** Set `PLAYER_SPEED` to `fx::from_double(0.1)`. Work out where the player is after walking right from x = 48 for ten seconds, 600 ticks. Then hold right for about ten seconds and compare with the log. Which other 256ths come close to a tenth, and where would each be after 600 ticks?
3. **Division instead of floor, in the game.** Make `fx::floor` divide again, set `PLAYER_SPEED` to `fx::from_double(0.0625)`, a pixel every 16 ticks, and change the left edge of the player's `clamp` to `fx::from_int(-16)`. First work out how many ticks the player spends at pixels 1, 0 and -1 while walking left. Then walk left across x = 0 and watch the player's left edge: where does it pause? Put the shift back and walk again.
4. **16.16.** Change `FX_FRACTION_BITS` to 16, as Doom's `fixed_t`. Which tests fail, and why do they fail but the game doesn't? What are `FX_MIN_INT` and `FX_MAX_INT` now, and would they be enough for a stage of 30 screens in a row?
5. **Rounding to the nearest.** Draw positions at `(x.raw + 128) >> 8` instead of `floor`. Work out the player's steps for the first 8 ticks at 1.375. Is it still 11 pixels every 8 ticks? What's the step pattern now?
6. **`consteval`.** In `update`, write `state->player_pos.x += fx::from_double(state->tick * 0.001);`. What does GCC say? Change `consteval` to `constexpr` in `fx.h`, and build again. What did `consteval` protect, and what does the game now do on every tick?
