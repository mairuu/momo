# Outline

The chapters planned for the book. The decisions behind them are in [plan.md](plan.md).

Like the plan, the outline is append-only. When a chapter changes what a planned chapter covers, its entry says so, and [changelog.md](changelog.md) says why.

## Part I: from nothing to one stage and its boss

**A. Foundations (terminal only)**
- 00 Preface
- 01 The Dialect: the Orthodox rules, `base.h` (types, `ASSERT`, macros)
- 02 Three Binaries: host, `libmomo.so`, `game.so`, and the Makefile. *Changed in ch02:* also loads `game.so` once with `dlopen` through the game API struct, and generates `compile_commands.json`.
- 03 A Test Runner: `make test` in about 50 lines. *Changed in ch03:* about 90 lines, one program per test file, and `min`, `max` and `clamp` join `base.h` as the first code under test.
- 04 Arenas: permanent, stage and frame on `mmap`, zeroed, with overflow asserts
- 05 Strings and Logging: `Str`, `str_fmt`, `log::info`. *Changed in ch05:* `str::fmt`, and the log is the macros `LOG_INFO`, `LOG_WARN` and `LOG_ERROR`, since `log` can't be a namespace. `i64` also became `long long`.
- 06 Hot Reload: `dlopen`, the game API struct, engine-owned memory, the version check. *Changed in ch02:* `dlopen` and the API struct arrive in chapter 02, so this chapter is about reloading: noticing a new `game.so`, swapping it, and keeping game state across the swap. *Changed in ch06:* also gives the engine an arena of its own, and the loop runs about 60 times a second until Ctrl+C, until chapters 07 and 09 replace both.

**B. Window and clock**
- 07 Platform Layer: an SDL3 window behind `platform::`
- 08 OpenGL 4.6: glad, DSA, the debug callback, a clear color
- 09 Fixed Timestep: the accumulator, the tick cap, ticks vs frames

**C. Drawing**
- 10 Sprite Batch: colored rectangles, one draw call, layers
- 11 Virtual Screen: 256×240, whole-number scaling, black bars
- 12 Textures: stb_image; an 8×8 font in code as the first texture
- 13 Debug Text and Capture: the overlay, `--capture`

**D. Moving things**
- 14 Input: the virtual pad, keyboard and gamepad, latching to ticks
- 15 Fixed-Point: `Fx`, `Fx2`, tests
- *Appendix A: `design.md` is drafted here*
- 16 Entities: `Entity`, `kind`, `Pool<T>`, generational handles, `switch` dispatch
- 17 Text Tilemaps
- 18 Collision: pixel-by-pixel stepping (Actor/Solid), with tests
- 19 Debug Drawing: hitboxes, collision tiles
- 20 Run and Jump: variable-height jump, gravity, maximum fall speed
- 21 Pause, Step, Record, Loop
- 22 Ladders and Spikes: tile types, climbing, death and respawn

**E. A stage**
- 23 Rooms and Camera: bounds, pixel snapping, screen-scroll transitions
- 24 Spawning: spawn on scroll-in, despawn on scroll-out
- 25 Shooting: at most 3 shots, hitboxes vs hurtboxes, damage, invincibility frames, knockback
- 26 Enemies: three behaviors, random numbers in `GameState`
- 27 Health and HUD: health bar, lives, checkpoint
- 28 Game Flow: title → stage → game over or victory

**F. Real content**
- 29 Sprites and Animation: CC0 art, animation tables in code
- 30 The Bake Tool
- 31 Palette Sprites: index textures, palette rows, hit flash
- 32 LDtk Import
- 33 Asset Hot Reload
- 34 The Mixer: SDL3 stream, voices, WAV
- 35 Music: streaming OGG

**G. The boss**
- 36 Boss Gate: shutters, transition, health bar fill
- 37 The Boss: attack pattern as a state machine, victory
- 38 Step Tables: *only if chapter 37 turns out painful*

## Part II: the vertical slice (a sketch, refined when we get there)

Title screen and stage select · a second stage · a second boss · taking powers and switching between them, with a palette per power · the weakness table · the ECS chapter (build it, port to it, compare) · Dear ImGui, if it proves necessary · *optional:* Lua vs C++, the same boss written twice.

## Possible bonus chapters

An SDL_GPU renderer backend (macOS) · a Windows port · an NES-style synth.
