# The Plan

This file records every design decision behind momo and why it was made. Chapters link here when they rely on a decision.

The book is append-only, and so is this plan. When a later chapter changes a decision, the original stays here, marked **changed in chNN**, and the change log at the bottom says why.

---

## 1. What momo is

| Decision | Why |
|---|---|
| A game engine built as a core library, plus a game built on it | The engine/game split is the main architecture lesson, the same shape as Hazel. |
| The game: a 2D action platformer loosely inspired by MegaMan 2, with an isekai/fantasy theme | A concrete game decides which engine features exist. A feature goes into the core only when the game needs it. |
| Built for one person: you | No accounts, onboarding, polish or support. Everything goes into learning the core. |
| Purpose: learning. Engine architecture first, graphics second. | When this conflicts with "ship it", learning wins. Tools (editor, asset pipeline) come last. |
| Linux now, written so it can be ported later | All OS-specific code goes behind one platform layer. Only Linux gets tested. |

## 2. The book

| Decision | Why |
|---|---|
| Claude writes the code and the chapters (about 95%). You take a piece whenever you ask. | You learn mostly by reading. Chapters you implement give you hands-on practice exactly where you want it. |
| Chapters are markdown in `book/NN-slug.md`, next to the code | The book and the code can't drift apart. |
| One commit per chapter, tagged `chNN` | `git checkout ch05` is exactly the code chapter 5 describes, and `git diff ch04 ch05` is everything it changed. The git history reads like the table of contents. |
| Append-only: later chapters fix earlier designs and explain why | Seeing a design break and get fixed is the most valuable architecture lesson. Rewriting old chapters would hide it, and the tags would no longer match what each chapter described. |
| The current chapter can still change until the next one starts. After that, only bug and typo fixes. | Keeps append-only from locking in a mistake you spotted while reading. |
| Small chapters, one idea each, about 15–25 minutes, always ending with something you can run | Reading always ends with seeing the code work. |
| One chapter per request | Your feedback shapes the next chapter. |

**Every chapter has these parts:** Goal (what you'll see at the end) → Problem (why the engine needs this now) → Design (the options, the pick, and why) → Code (key excerpts with `file:line`, not whole files) → Run it (exact commands, what to look for) → Recap (updated module map) → Exercises (optional).

**Before each commit, Claude checks:** both builds compile cleanly with `-Wall -Wextra`, `make test` passes, the game runs under the sanitizers, and from chapter 13 on, the `--capture` screenshots show what the chapter claims. Those screenshots go into the chapter.

**Chapters you implement:** Claude commits the spec, stubs and failing tests as `chNN-spec`. You implement, Claude reviews, and `chNN` is tagged on your code. Claude adjusts only what later chapters depend on.

**The game design** lives in `book/design.md`. Claude drafts it before chapter 16, and you decide the theme, names and tone. Claude keeps it inside the Part I and Part II scope.

## 3. Language: Orthodox C++20, built with GCC

The test for any feature: *can I tell what this line does by reading only this line?*

### 3.1 Rules

| Rule | Why |
|---|---|
| Plain `struct`s with public fields, and behavior in free functions grouped by namespace: `sprite_batch::draw(&batch, s, pos)` | A struct definition is only data, and a header is a list of functions. Functions that work on two things don't have to choose an owner. |
| No inheritance, no `virtual` | Dispatch is visible: a `switch` on a `kind` enum, or an explicit function-pointer table inside the engine. Hot reload also requires it (§3.2). |
| Constructors and destructors do no work. No RAII. Setup and teardown are explicit `init` / `shutdown` calls. | No code runs at a declaration or at the end of a scope without you seeing it. |
| **Zero means default (ZII).** Every struct is valid when all zeros. No default field values. Whatever creates a thing sets its values. | Arenas hand out zeroed memory. Resetting anything is zeroing it, and nothing quietly loses its defaults. |
| Templates only for containers and math (`Array<T>`, `Pool<T>`). No metaprogramming. | Typed containers are the biggest win over C. Anything beyond that stops being readable. |
| Operator overloading only for math types | `p += v` is clear. Overloaded `<<` or `()` is not. |
| `-fno-exceptions -fno-rtti`. No STL containers, no `<iostream>`. Use C headers and `<stdint.h>` types. | No hidden control flow, allocation or copying. |
| Memory from arenas and fixed pools. No `new` / `delete` in game code, no smart pointers. | Lifetimes are grouped and obvious. Hot reload also requires it (§3.2). |
| Pointers for anything a function modifies (`f(&x)`). Values or const references for read-only inputs. | You can see a mutation where the function is called. |
| No capturing lambdas. `auto` only where the type is obvious from the right-hand side. | Both hide types or state. |

### 3.2 Rules for game code, required by hot reload

When `game.so` reloads, its old code is unloaded but game state memory stays. Anything in that state that points into the old code is left dangling.

| Rule | What happens if you break it |
|---|---|
| No `virtual` | Objects keep vtable pointers into the unloaded `.so`, and the game crashes. |
| No function pointers stored in game state. Dispatch with a `switch` on a `kind` enum. | Same as above. Function-pointer tables are fine inside the engine, which never reloads. |
| No globals and no `static` locals | They quietly reset to their starting values on every reload. |
| No string-literal pointers stored in game state | The text lived in the old `.so`'s memory. |
| Store indices into `constexpr` tables, never pointers to them (`anim_id`, not `const Anim*`) | The tables live in the old `.so` too. |
| All game state lives in memory the engine owns | It's the only memory that survives a reload. |

### 3.3 Errors and strings

| Decision | Why |
|---|---|
| Bugs (a broken invariant, a bad index) hit `ASSERT`, which stops in gdb | A bug should stop the game where it happens. |
| Expected failures (a missing file, a shader that won't compile) return `bool` or null and log the reason | They're normal events, not bugs. No `Result<T>`, no error-code hierarchy. |
| `Str` = pointer plus length, not owning its memory. Strings are built in arenas with `str_fmt(arena, ...)`. | No hidden allocation, and the lifetime is the arena's. |
| Game state stores enums and IDs, never strings | Avoids the dangling string-literal problem from §3.2. |

### 3.4 Names and types

- Types are `PascalCase`. Functions and variables are `snake_case`. Macros and enum values are `UPPER_SNAKE`.
- One lowercase namespace per module (`renderer::`, `input::`), with no nesting.
- Plain `enum`s with a fixed underlying type and prefixed values: `enum EntityKind : u8 { ENTITY_PLAYER, ENTITY_BAT };`. They index tables without casts.
- Base types are `i8`…`i64`, `u8`…`u64`, `f32`, `f64`. **Counts and indices are signed**, so `i - 1` on an empty list gives `-1`, which an assert catches, instead of 4 billion.
- Each module is a `.h` + `.cpp` pair with `#pragma once`.

## 4. Engine

### 4.1 Three binaries and hot reload

```
 host (executable)          libmomo.so (the engine)           game.so (the game)
 ┌────────────────┐  links  ┌──────────────────────────┐ links ┌──────────────────┐
 │ main() {       │────────▶│ momo::run()              │◀──────│ init / update /  │
 │   momo::run()  │         │   loop, platform, render,│ dlopen│ render / shutdown│
 │ }              │         │   input, audio, arenas   │──────▶│ (reloaded on     │
 └────────────────┘         └──────────────────────────┘       │  every rebuild)  │
                                                               └──────────────────┘
```

| Decision | Why |
|---|---|
| The engine owns `main()` (through `host`) and the fixed-timestep loop | It's the core-plus-game structure, and the fixed timestep belongs in the engine. |
| **Hot reload from day one (chapter 06).** The engine loads `game.so` with `dlopen` and reloads it when the file changes. | You can change gameplay while the game runs. |
| The engine is a shared library, `libmomo.so`, and both `host` and `game.so` link it | One copy of the engine. If `game.so` statically linked the engine, the game would call a second renderer that was never initialized. The same setup works on Windows (`momo.dll` + `game.dll`). |
| Engine functions the game can call are marked with `MOMO_API` (visibility / export) | Deciding what a library exports is something real engines deal with. |
| The game exports one function that returns a struct of `init`, `update`, `render` and `shutdown`. The engine fetches it again after every reload. | That's the whole boundary. No inheritance, and nothing stale after a reload. |
| The game exports `GAME_STATE_VERSION` (bumped by hand) and `sizeof(GameState)`. If either changes, the engine zeros game memory, calls `init` again, and logs it. | Code-only edits keep running. A layout change costs a stage restart instead of a crash. |
| Engine module state lives inside each module (`renderer::draw_sprite(s, pos)`, with no context passed in) | There's only one window, GPU and audio device. The engine never reloads, so its internal state is safe. |
| Game state hangs off the `GameState*` the engine passes in | Rules in §3.2. |

### 4.2 Memory

| Decision | Why |
|---|---|
| One `mmap` block, split into three arenas: **permanent** (whole run, survives reload), **stage** (reset when a stage loads), **frame** (reset every frame) | Grouping memory by lifetime means nothing gets freed one object at a time. |
| Sizes are fixed. Running out is an `ASSERT`. The debug overlay shows peak usage. | Hitting a limit tells you something real about the game. |
| Memory comes zeroed | ZII (§3.1). Untouched `mmap` pages are zero and don't use physical memory. |

### 4.3 The frame

`handle platform events → update input → 0…N fixed 60 Hz ticks (game.update) → game.render → debug overlay → present`

| Decision | Why |
|---|---|
| One fixed sequence in a single engine function, with no layer stack | momo has two parts, the game and the debug overlay. A layer stack is for many pieces plugged in and out. The book explains Hazel's layer stack in a sidebar. |
| Fixed 60 Hz game updates, with speeds in pixels or subpixels per frame | MegaMan-style movement numbers carry over directly, and the simulation is deterministic. |
| Ticks per frame are capped | Resuming from a gdb breakpoint doesn't make the game fast-forward. |
| No smoothing between ticks for now | It can come in a later chapter if a high-refresh display stutters. |

### 4.4 Platform and input

| Decision | Why |
|---|---|
| SDL3 (system package) behind momo's `platform::` module. Game code never includes SDL. | Window, input, gamepad, audio and timing in one library, with portability for free. Writing these from scratch isn't one of the learning goals. |
| Input is a **polled virtual NES pad**: d-pad, Jump, Shoot, Start, Select, previous/next power. Keyboard and gamepad map onto it. The game calls `input::down` / `pressed` / `released`. | The game never sees real keys. Polling suits a fixed-tick game. |
| Presses are **latched to ticks** | A tap between two ticks still counts, and "pressed" is true on exactly one tick even when a frame runs zero ticks. |
| The engine handles non-gameplay events itself (lost focus pauses, resize, gamepad plugged in) | Hazel's event system mostly serves the editor and UI, which are out of scope. |

### 4.5 Rendering

| Decision | Why |
|---|---|
| OpenGL 4.6 core with direct state access (DSA), function loading with glad, `glDebugMessageCallback` | Builds on LearnOpenGL without the bind-then-edit step. Errors explain themselves. Your Radeon 890M / Mesa supports 4.6. |
| Everything goes through momo's renderer module | Porting to SDL_GPU later (for macOS) stays contained. |
| **Immediate-mode batched sprites:** `renderer::draw_sprite(tex, src_rect, pos, layer, flags)` every frame, drawn in as few calls as possible | The renderer keeps nothing that points into game code, which suits hot reload and ZII. |
| Draw order: layer number (background tiles, entities, foreground tiles, HUD), then call order | Simple and predictable. |
| **256×240 virtual screen** (16×15 tiles of 16 px), scaled up by whole numbers, with black bars | One screen is one room. Pixel art stays crisp. |
| Camera snapped to whole pixels | No shimmering. |
| **Palette sprites for characters:** single-channel index textures plus palette rows chosen per draw. Tiles stay RGBA. Plus a hit flash. | Weapon colors, damage flashes and boss variants are just different rows. It's the graphics lesson most tied to this game. |

### 4.6 Numbers

| Decision | Why |
|---|---|
| Simulation uses **fixed-point** `Fx` / `Fx2`: `i32` counting 1/256ths of a pixel (24.8) | Exact and deterministic, which makes record-and-replay possible. 1/256 matches NES subpixels, so MegaMan-style numbers (walking ≈ 1.375 px per frame) translate directly. No float jitter at tile edges. |
| Rendering uses float `V2` and matrices | That's what the GPU wants. Positions round to whole pixels at draw time. |

### 4.7 Entities

| Decision | Why |
|---|---|
| One `Entity` struct with a `kind` enum, in a fixed `Pool<T>` with generational handles | Enough for a few dozen entities. The handles catch references to entities that no longer exist. |
| ECS comes later, in Part II: build a small sparse-set ECS by hand, port the game or part of it, and compare honestly | You learn ECS best after running into the problem it solves. At this size, plain structs may come out ahead, and the book will say so if they do. |
| No EnTT | It's heavy on templates and breaks §3.1. |

### 4.8 Collision

| Decision | Why |
|---|---|
| **Actor/Solid, pixel by pixel:** movement adds to a fixed-point remainder, and the entity moves one whole pixel at a time, stopping at the first solid. Moving platforms are solids that carry actors. | Nothing ever overlaps a wall, so nothing needs pushing back out. Entities can't tunnel through walls, and edges are always correct. At MegaMan speeds the cost is negligible. |
| Tile types: solid, ladder, ladder top (you can stand on it), spikes, and optionally one-way | That's what the game needs. |
| Entities use AABB overlap on every pair, with separate hitboxes and hurtboxes | A few dozen entities don't need spatial partitioning. |

### 4.9 Audio

| Decision | Why |
|---|---|
| Our own mixer (voices for one-shot sound effects plus a music stream), mixed on the main thread and pushed into an SDL3 audio stream with a few frames buffered | Mixing is a learning goal. Running on the main thread means no audio thread and no locking. |
| WAV sound effects (baked to raw PCM), OGG music through stb_vorbis | Decoding file formats isn't a learning goal. |
| An NES-style synth is an optional bonus chapter | It's a great project, but a different subject. |

### 4.10 Debug tools and tests

| Decision | Why |
|---|---|
| Part I has: pause and frame step, **record and loop** (snapshot the arenas, record inputs, replay on a loop), hitbox / hurtbox / collision-tile overlays, debug text from our own bitmap font, and `--capture` to save screenshots | Record and loop plus hot reload lets you replay the same jump while editing its code. That depends on determinism (§4.6) and arena-only state (§4.2). |
| Random numbers come from a generator whose state is in `GameState`, never `rand()` | Replays stay deterministic. |
| Dear ImGui only in Part II, and only if it proves necessary | Hot reload already makes tuning numbers in code fast. |
| A tiny test runner we write ourselves (`make test`) for pure logic: math, fixed-point, containers, collision | Those bugs are much easier to find in a test than at 60 fps. Test frameworks are heavy on templates. |

## 5. Game

| Decision | Why |
|---|---|
| **Rooms** a whole number of screens wide or tall. The camera follows the player inside a room and stops at its edges. Moving into the next room freezes gameplay and scrolls exactly one screen. | That's how MegaMan feels. Each LDtk level is one room. |
| Enemies spawn when their spawn point scrolls into view and despawn when it scrolls off, so they come back if you return | Classic MegaMan behavior. |
| Game flow (title → stage select → stage → boss → victory or game over) is a `switch` on a mode enum in game state. The engine has no scene system. | Required by §3.2. |
| Tuning numbers (physics, enemy stats, hitboxes, animations, damage tables) are `constexpr` tables in code. Files only for what external tools produce. | With hot reload, editing code takes effect almost as fast as editing data, the compiler checks it, and there's no parser to write. |
| The boss is written as a `switch` state machine. If that's painful, an extra chapter adds step tables (`{JUMP_AT_PLAYER}, {FIRE_SPREAD, 3}, {WAIT, 60}`). | You run into the problem before the fix arrives. |
| **Gameplay stays in C++ with hot reload. No scripting language in the main book.** An optional Part II chapter embeds Lua 5.4, rewrites the same boss as a coroutine, and compares the two. | Hot reload already lets you change gameplay without restarting. A scripting runtime would break the arena snapshots, fixed-point determinism, the dialect rules and single-debugger debugging. Comparing the two teaches the trade-off honestly. |

## 6. Content

| Decision | Why |
|---|---|
| Levels: text maps (`#` = wall) in the first stage chapters, then **LDtk** | LDtk's world of levels maps directly onto MegaMan rooms. The switch teaches how to write an importer. |
| Art: colored rectangles until the mechanics work, then **CC0** packs only | No art blocks the engine. Using only CC0 means publishing never requires checking asset licenses. |
| Character art is converted to palette indices by the bake tool, which asserts if a sprite has more colors than the palette allows | Required by §4.5. |
| `tools/bake` converts source assets into momo's own formats at build time, run by Make | Runtime code only loads simple formats. Teaches the asset pipeline. |
| A JSON library is included, used only by the bake tool | JSON is a file format, and parsing it teaches nothing about engines. |
| **Asset hot reload:** the engine watches baked files and reloads any that change | Content works the same way code does: save, re-bake, see it. |

## 7. Build, dependencies, repo

| Decision | Why |
|---|---|
| GNU Make. `make` builds the debug build (`-O0 -g3`, AddressSanitizer + UndefinedBehaviorSanitizer, asserts on). `make release` uses `-O2 -g` with asserts still on. Both support hot reload. No static shipping build. | Separate compilation and linking stay visible. The sanitizers catch exactly the bugs C-style code invites. |
| `libmomo.so` is built with `-fPIC` and an export macro | Required by §4.1. |
| SDL3 from the system through `pkg-config` | Already installed (3.4). |
| Copied into `third_party/` with pinned versions and licenses: glad, stb_image, stb_image_write, stb_vorbis, a JSON library, and Dear ImGui later | Libraries handle file formats, GL loading and debug UI. We write everything that's a learning goal. |
| Git stays local for now. Code and book licenses get decided when publishing. | Nothing depends on that choice yet. |

```
momo/
  Makefile
  book/          chapters (NN-slug.md), design.md, plan.md
  host/          main.cpp, the executable: calls momo::run()
  engine/        libmomo.so
  game/          game.so
  tools/bake/    the asset baker
  tests/         test runner and tests
  third_party/   vendored libraries, each with its LICENSE
  assets/        source assets + LICENSES.md
  build/         gitignored: debug/, release/, baked/
```

Each folder is created in the chapter that first needs it.

## 8. Outline

### Part I: from nothing to one stage and its boss

**A. Foundations (terminal only)**
- 00 Preface
- 01 The Dialect: the Orthodox rules, `base.h` (types, `ASSERT`, macros)
- 02 Three Binaries: host, `libmomo.so`, `game.so`, and the Makefile
- 03 A Test Runner: `make test` in about 50 lines
- 04 Arenas: permanent, stage and frame on `mmap`, zeroed, with overflow asserts
- 05 Strings and Logging: `Str`, `str_fmt`, `log::info`
- 06 Hot Reload: `dlopen`, the game API struct, engine-owned memory, the version check

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

### Part II: the vertical slice (a sketch, refined when we get there)

Title screen and stage select · a second stage · a second boss · taking powers and switching between them, with a palette per power · the weakness table · the ECS chapter (build it, port to it, compare) · Dear ImGui, if it proves necessary · *optional:* Lua vs C++, the same boss written twice.

### Possible bonus chapters

An SDL_GPU renderer backend (macOS) · a Windows port · an NES-style synth.

## 9. Out of scope

Networking · saving or passwords · threads of our own · 3D · an in-engine editor · localization, and fonts beyond the 8×8 bitmap font · web or mobile · a static shipping build · testing on Windows or macOS during Parts I and II (the code stays portable, but only Linux gets tested).

Anything here can move into scope later through a chapter. If it does, it gets logged below.

## 10. Change log

| Chapter | Change | Why |
|---|---|---|
| ch00 | Plan written from the design session on 2026-10-08 | Starting point. |
