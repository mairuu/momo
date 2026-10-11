# The Plan

This file records every design decision behind momo and why it was made. Chapters link here when they rely on a decision.

Each entry is a decision and its reason, in a sentence or two. Details such as struct fields, function signatures, file paths and exact sizes belong in the code and in the chapter that introduced them.

The book is append-only, and so is this plan. When a later chapter changes a decision, the original stays here, marked **changed in chNN**, and [changelog.md](changelog.md) says why.

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
| *Added in ch13:* The book's pictures are `--capture` files in `book/images/`, at the screen's 256×240, and each chapter gives the command that made each one | They're the game's own pixels, the same on every display, and you can make each one again and compare it with `cmp`. |

**Every chapter has these parts:** Goal (what you'll see at the end) → Problem (why the engine needs this now) → Design (the options, the pick, and why) → Code (key excerpts with `file:line`, not whole files) → Run it (exact commands, what to look for) → Recap (updated module map) → Exercises (optional).

**Before each commit, Claude checks:** both builds compile cleanly with `-Wall -Wextra`, `make test` passes, `make lint` passes (*added after ch09*), the game runs under the sanitizers, and from chapter 13 on, the `--capture` screenshots show what the chapter claims. Those screenshots go into the chapter.

**Chapters you implement:** Claude commits the spec, stubs and failing tests as `chNN-spec`. You implement, Claude reviews, and `chNN` is tagged on your code. Claude adjusts only what later chapters depend on.

**The game design** lives in `book/design.md`. Claude drafts it before chapter 16, and you decide the theme, names and tone. Claude keeps it inside the Part I and Part II scope. **Changed in ch16:** it comes after chapter 16, as a request of its own, before the chapters that need names: chapter 26's enemies at the latest.

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
| *Added in ch01:* structs are created with `= {}` or a designated initializer, `{ .kind = ENTITY_BAT, .hp = 1 }`. Fields left out are zero. | A local struct with no initializer holds garbage. A designated initializer names only what isn't zero, which is ZII written down. |
| *Added in ch01:* C-style casts, `(i32)x` | Short, and the resulting type is on the line. |
| *Added in ch03:* `min`, `max` and `clamp` are templates, with one `T` for every argument | A macro evaluates an argument twice, and a function per type multiplies names. With one `T`, mixing types is a compile error instead of a silent conversion. |
| *Added in ch04:* Typed arena pushes (`arena::push<T>`) are templates too. They only fill in the size and alignment and cast the result. | A C macro would do the same with worse error messages and no namespace. |

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

*Added after ch09:* `make lint` catches globals in `game/` (§7). No clang-tidy check catches the other rules, so they're kept by reading the code.

### 3.3 Errors and strings

| Decision | Why |
|---|---|
| Bugs (a broken invariant, a bad index) hit `ASSERT`, which stops in gdb | A bug should stop the game where it happens. |
| Expected failures (a missing file, a shader that won't compile) return `bool` or null and log the reason | They're normal events, not bugs. No `Result<T>`, no error-code hierarchy. |
| `Str` = pointer plus length, not owning its memory. Strings are built in arenas with `str_fmt(arena, ...)`. **Changed in ch05** (row below). | No hidden allocation, and the lifetime is the arena's. |
| Game state stores enums and IDs, never strings | Avoids the dangling string-literal problem from §3.2. |
| *Added in ch01:* `ASSERT` is on in every build. It prints `file:line` and traps. | gdb stops inside the function that failed, not deep in libc as with `assert()`, and editors can jump to the line. |
| *Changed in ch05:* Strings are built with `str::fmt(arena, ...)`, which ends the text with a 0. A `Str` itself doesn't promise one, and its length is an `i32`. | The name follows §3.4. The 0 lets C functions take formatted text, which a slice can't give them. `%.*s` prints a `Str` and wants an `int` length. |
| *Added in ch05:* Logging is three macros, `LOG_INFO`, `LOG_WARN` and `LOG_ERROR`, that write `file:line: message` lines to stderr. The function behind them is in `logger::`, since `log` is the C library's logarithm. | Each line names the code that logged it, in the format editors already jump to. stderr isn't buffered, so a crash loses nothing, and the log stays in order with `ASSERT`. |

### 3.4 Names and types

- Types are `PascalCase`. Functions and variables are `snake_case`. Macros and enum values are `UPPER_SNAKE`.
- One lowercase namespace per module (`renderer::`, `input::`), with no nesting.
- Plain `enum`s with a fixed underlying type and prefixed values: `enum EntityKind : u8 { ENTITY_PLAYER, ENTITY_BAT };`. They index tables without casts.
- Base types are `i8`…`i64`, `u8`…`u64`, `f32`, `f64`. **Counts and indices are signed**, so `i - 1` on an empty list gives `-1`, which an assert catches, instead of 4 billion.
- Each module is a `.h` + `.cpp` pair with `#pragma once`.
- *Added in ch01:* counts and indices are `i32`, and byte sizes are `i64`. Unsigned types are only for bits, flags and formats defined outside momo (files, pixels, GPU data).
- *Added in ch01:* constants are `constexpr` and `UPPER_SNAKE`, like macros and enum values.
- *Added in ch05:* `i64` and `u64` are `long long` and `unsigned long long`, not `int64_t` and `uint64_t`, so `%lld` and `%llu` match them on every platform without casts.

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
| *Added in ch02:* `momo.h` is the whole boundary between the binaries. Both shared libraries build with `-fvisibility=hidden`, and the game's one export is `extern "C"`. **Changed in ch04** (next row). | Everything one binary can call in another is in one header, and nothing unmarked leaks out. `extern "C"` keeps the export's name unmangled for `dlsym`. |
| *Changed in ch04:* The boundary is `momo.h` plus the module headers it includes: every function marked `MOMO_API`. The macro lives in `base.h`. | Each module keeps its own header (§3.4), and the game needs some of them. A module header can't include `momo.h`, which includes it. |
| *Added in ch02:* The binaries find each other in the executable's folder, and `game.so` loads with `RTLD_NOW` | A build folder runs from anywhere, with no install step. A missing engine function fails when the game loads, not in the middle of a frame. |
| *Added in ch06:* Each frame starts by checking `game.so`'s modification time. To reload, the engine closes the old library, then opens the new one. | One `stat` costs well under a microsecond. `dlopen` hands back the library it already has for a path it has opened before. |
| *Added in ch06:* If the new `game.so` fails to load, the engine logs why, stops calling the game, and tries again when the file next changes | A broken build doesn't stop momo or touch the game's state, so the fixed build carries on where the game stopped. |
| *Added in ch06:* `game.so` is linked under a temporary name and renamed into place | The linker writes the file over many milliseconds, and without the rename the engine saw it half-written on every rebuild. A rename swaps in the whole file at once. |
| *Added in ch06:* The engine declares `GameState` but never defines it | It passes the game's state along without being able to depend on what's inside. |
| *Added in ch06:* Only `game.so` reloads. A change to the engine or to `momo.h` needs a restart. | The running engine can't be swapped, and both sides must agree on `momo.h`. |
| *Added in ch10:* The game draws only in `render`, which runs once per frame after the ticks and gets a `const GameState*`. Drawing outside it is an `ASSERT`. | A frame runs any number of ticks, so drawing in `update` would draw some frames twice and others not at all. A `const` state means drawing can't change the game, so it plays the same at any frame rate (§4.3). |
| *Added in ch13:* The host passes its command line to `momo::run`, and the engine reads it before anything else. An option it doesn't know stops momo with the usage line. | The engine owns `main()`, and the host stays one line. A mistyped option should fail, not be ignored. |

### 4.2 Memory

| Decision | Why |
|---|---|
| One `mmap` block, split into three arenas: **permanent** (whole run, survives reload), **stage** (reset when a stage loads), **frame** (reset every frame). **Changed in ch06** (last row). | Grouping memory by lifetime means nothing gets freed one object at a time. |
| Sizes are fixed. Running out is an `ASSERT`. The debug overlay shows peak usage. | Hitting a limit tells you something real about the game. |
| Memory comes zeroed | ZII (§3.1). Untouched `mmap` pages are zero and don't use physical memory. |
| *Added in ch04:* An arena doesn't own its block. A push aligns the address (not the offset), zeroes what it returns, and `ASSERT`s when full. | Zeroing in push keeps "push returns zeros" true after a reset, and reset stays one line. Aligning the address works for any block, including a test's stack buffer. |
| *Added in ch04:* Each arena is sized generously for an NES-style game | Untouched pages are nearly free. One block is one `mmap` call and, in chapter 21, one copy. |
| *Added in ch04:* The engine resets the frame arena at the start of every frame. The game resets the stage arena itself, and reaches all three arenas through engine functions. | Memory pushed during a frame stays valid for the rest of that frame. Only the game knows when a stage loads (§5). Engine state stays in the engine (§4.1). |
| *Added in ch04:* Until the debug overlay (chapter 13), the engine prints each arena's peak when it quits. **Changed in ch13** (last row). | The sizes can be checked against real use. |
| *Added in ch04:* ASan can't see inside an arena. Containers check their indices with `ASSERT` instead. | ASan only tracks `malloc`, stack and global memory. Its manual poisoning interface is the fallback if this ever bites. |
| *Changed in ch06:* A fourth arena belongs to the engine, and the game can't reach it. Permanent and stage hold only the game's memory, side by side at the start of the block, and the engine resets both when `GameState`'s layout changes. | The reset would wipe anything the engine kept in the permanent arena. Chapter 21's snapshots should copy all of the game's memory and none of the engine's, in one piece. |
| *Added in ch12:* A file's bytes and its decoded pixels come from `malloc`, SDL's and stb_image's, and are given back before the function that loaded them returns | They're needed only until OpenGL has copied the pixels. The engine's arena is never reset, and the frame arena is the game's. AddressSanitizer checks `malloc`'s memory. |
| *Changed in ch13:* The overlay shows the peaks of the game's three arenas while it runs, and the engine still prints all four when it quits. The overlay's text is formatted in an arena on the stack. | The log stays after the window is gone, and the chapters quote it. The engine's own arena isn't the game's to watch. The frame arena is the game's, and its peak is one of the overlay's lines. |

### 4.3 The frame

`handle platform events → update input → 0…N fixed 60 Hz ticks (game.update) → game.render → debug overlay → present`

| Decision | Why |
|---|---|
| One fixed sequence in a single engine function, with no layer stack | momo has two parts, the game and the debug overlay. A layer stack is for many pieces plugged in and out. The book explains Hazel's layer stack in a sidebar. |
| Fixed 60 Hz game updates, with speeds in pixels or subpixels per frame | MegaMan-style movement numbers carry over directly, and the simulation is deterministic. |
| Ticks per frame are capped | Resuming from a gdb breakpoint doesn't make the game fast-forward. |
| No smoothing between ticks for now | It can come in a later chapter if a high-refresh display stutters. |
| *Added in ch09:* A **tick** is one 60 Hz update of the game, and a **frame** is one picture. Speeds are per tick. | The NES drew one picture per update, so MegaMan's "per frame" numbers are per tick in momo. How many frames there are depends on the display. |
| *Added in ch09:* Each frame runs one tick for every whole 1/60 s since the last frame, and owes the rest to the next. Time is integer nanoseconds from SDL's monotonic clock, passed to the timestep's arithmetic instead of read by it. | Integers are exact, and an `i64` of nanoseconds lasts 292 years. Tests hand the timestep made-up times, so a second of frames takes no time to test and needs no SDL. |
| *Added in ch09:* A frame runs at most 6 ticks (100 ms). Time over the cap is dropped, not owed, and logged. | On Wayland, SDL stops waiting for the compositor after 50 ms, so a window that's opening or hidden draws 20 frames a second, and those must keep full speed. A slower frame is a stall, and catching up would make the game race. |
| *Added in ch09:* The game counts its own ticks in `GameState`. The engine gives it no clock, no frame count and no time step. | The game's logic must depend only on `GameState` and its inputs. Chapter 21's replays rewind only the game's memory, so anything else would make a replay go differently. |
| *Added in ch13:* The overlay counts frames and ticks over a second of game time: the frame that runs the second's 60th tick ends it, or 2 s of real time if the game runs fewer. A frame's **work** is its time before the swap. | A second of real time ends between ticks, and read 61 ticks in 5 of 29 seconds with every frame on time. With vsync, every frame lasts as long as the display's refresh, so only the work says what a frame cost. |

### 4.4 Platform and input

| Decision | Why |
|---|---|
| SDL3 (system package) behind momo's `platform::` module. Game code never includes SDL. | Window, input, gamepad, audio and timing in one library, with portability for free. Writing these from scratch isn't one of the learning goals. |
| Input is a **polled virtual NES pad**: d-pad, Jump, Shoot, Start, Select, previous/next power. Keyboard and gamepad map onto it. The game calls `input::down` / `pressed` / `released`. **Changed in ch14** (below). | The game never sees real keys. Polling suits a fixed-tick game. |
| Presses are **latched to ticks** | A tap between two ticks still counts, and "pressed" is true on exactly one tick even when a frame runs zero ticks. |
| The engine handles non-gameplay events itself (lost focus pauses, resize, gamepad plugged in) | Hazel's event system mostly serves the editor and UI, which are out of scope. |
| *Added in ch07:* `platform::` is the only engine module that includes SDL or an OS header. It uses SDL wherever SDL has the function (window, events, paths, file times, libraries, sleeping), and calls the OS only where it doesn't (reserving memory). None of it is `MOMO_API`. | The rest of the engine reads in momo's types and logs failures the momo way, and porting, or replacing SDL, rewrites one file. On Linux, SDL makes the same calls momo made itself before. |
| *Added in ch07:* Closing the window, Ctrl+C and `kill` all become SDL's quit event, the loop's one way out. momo's own `SIGINT` handler is gone. | SDL installs the signal handlers itself, and a window adds a second way to quit. |
| *Added in ch11:* The window is resizable, and resizing needs no event: the renderer asks SDL for the window's size in pixels every frame. The game never learns the window's size. | SDL keeps the size current while it handles events, so asking can't fall out of step. It also catches a move to a display with another scale, which changes the pixels but not the points. |
| *Added in ch12:* `platform::read_file` reads a whole file through SDL | Reading files is the platform's job, like their times. stb_image decodes bytes it's handed and never opens a file. |
| *Added in ch13:* The engine has keys of its own, which the game never sees. `platform::handle_events` reports that a key went down, by its place on the keyboard, and the engine decides what it does: `` ` ``, left of 1, shows and hides the overlay. | A debug key isn't a button on the game's pad (chapter 14). The platform reports the key, and its meaning is the engine's. |
| *Changed in ch14:* The pad's buttons are named for where they are: the NES pad's d-pad, A, B, Select and Start, plus L and R. The game names what each one does. | What a button does is the game's decision (§1), as a layer's is (§4.5). A button can't be a bare number like a layer, since the engine ties each one to keys and gamepad buttons. |
| *Added in ch14:* Between ticks, the engine collects the pad's changes as bits, what went down and what went up, after every event. Each tick takes them, and the next tick's collection starts from none. The game can read the pad only during a tick. | Measured with 40 taps: comparing each frame with the last saw 20 at 120 Hz and 122 with 50 ms frames. Latched, both saw 40. Outside a tick, `pressed` would repeat the last tick's answer. A tick's pad, 6 bytes, is all the input a tick has, and what chapter 21 records. |
| *Added in ch14:* The tables from keys, by scancode, and SDL's gamepad buttons to the pad's buttons are in `platform.cpp`. The keyboard and every gamepad drive one pad: a button is down while anything holds it. A gamepad's left stick is the d-pad once it's pushed more than halfway. | Only `platform::` knows SDL's names. One person plays, with whatever is in their hands. Keeping what each device holds lets an unplugged gamepad let go of its buttons. |
| *Added in ch14:* A capture takes no input | The same command must save the same file (§4.10). |

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
| *Added in ch08:* `platform::` makes the OpenGL context and looks up OpenGL's functions. Only `renderer.cpp` includes glad and calls OpenGL. | Each module keeps to one library, so porting either one rewrites one file. |
| *Added in ch08:* The debug callback logs each message at its severity, and `ASSERT`s on errors from the API. Notifications are off, messages are synchronous, and both builds ask for a debug context. | A wrong call is a bug in momo (§3.3). A shader that won't compile arrives as an error too, but from the shader compiler, and it's an expected failure. Mesa queues calls, so without synchronous messages an error is reported far from the call that caused it. |
| *Added in ch08:* vsync is off until chapter 09. **Changed in ch09** (next row). | Left to the video driver, it was off on Wayland and on under X11. The loop's sleep sets the pace until chapter 09 picks a clock. |
| *Changed in ch09:* vsync is on, and nothing sleeps. Without vsync, momo logs a warning and draws as many frames as it can. | The fixed timestep keeps the game's speed whatever the frame rate, so the display can set it. On a display that's a multiple of 60 Hz, every tick stays up for the same number of refreshes. The CPU waits in the swap: 2% of a core, against 114% with vsync off. |
| *Added in ch10:* Each rectangle is one **instance**: 20 bytes of position, size and color, from which the vertex shader makes four corners. A frame's rectangles go to the GPU in one buffer and are drawn by one call. | Measured with 3,877 rectangles a frame: 0.11 ms in one call, 0.27 ms in one call each, 1.6 ms setting uniforms before each call. Four vertices per rectangle would repeat each rectangle's data four times. |
| *Added in ch10:* The engine draws up to 8 layers, numbered back to front, and gives the numbers no meaning. The game names them, with an enum of its own. | What a layer is for is the game's decision (§1), and the engine's sort needs only the count. A wrong number is an `ASSERT` instead of a compile error. |
| *Added in ch10:* When the frame ends, a counting sort puts its rectangles in layer order, keeping call order inside each layer. A frame holds at most 4,096 rectangles, and more is an `ASSERT`. The batch and its sort are a module that never calls OpenGL. | With eight layers, counting beats comparing, and the sort is stable. A batch that drew whenever it filled up would break layer order, and a depth buffer can't order see-through pixels. Tests check the order with no GPU. |
| *Added in ch10:* Colors are four bytes, RGBA, with alpha blending on | Shadows, text backdrops and hitbox overlays need see-through rectangles. A zeroed color draws nothing, which is ZII. |
| *Added in ch10:* Shaders are strings in `renderer.cpp`. momo checks whether each one compiled and logs the compiler's messages itself, and turns off the shader compiler's debug messages. | Compile status works on every driver, debug context or not. Mesa also sent each failure to the debug callback, so it was logged twice. |
| *Added in ch11:* The window asks for every pixel it covers (SDL's high pixel density), and the scale is chosen from its size in pixels, not points | Otherwise the compositor stretched momo's picture to the display's scale with linear filtering, which blurred every edge even at exactly 2: a one-pixel eye became a 6×6 smudge. |
| *Added in ch11:* The game draws on a 256×240 texture of its own, through a framebuffer. Each frame, one blit copies it into the window with nearest filtering, at the largest whole-number scale that fits (at least 1), centered, over black. | The screen stays 256×240 whatever the window does, so neither the game nor the shaders change, and chapter 13's capture can read the screen at its own size. A blit needs no shader. Drawing into the window instead would need the viewport and the scissor set from its size every frame. |
| *Added in ch11:* The screen's size stays the engine's | Like `MAX_LAYERS`, it's a number the engine's machinery is built on: the texture, the shader, the fit and the overlay. Moving it into `GameApi` would need code to remake the screen on a reload, for a choice this game makes once (§1). |
| *Added in ch12:* Each rectangle shows its **source**, a part of a texture in the texture's own pixels, at the source's size. A rectangle of one color shows texture 0, one white pixel, in its color. Rows go to OpenGL top first, and nothing is flipped. | One shader and one instance format draw rectangles, sprites and text, and a zeroed `Texture` draws plain rectangles (ZII). The vertex shader puts the source's top at the rectangle's top, so sources count from the top left, like the screen and every image editor. |
| *Added in ch12:* The frame stays one buffer, sorted by layer, and is drawn with one draw call per **run**: rectangles in a row, in drawing order, that share a texture | Measured with 3,880 rectangles: 0.17 ms in 4 draw calls, 2.0 ms when the texture changed before nearly every one. This game's frames change textures a few times. **Texture slots** drew correctly in one call on Mesa, but GLSL leaves indexing an array of samplers by a per-rectangle number undefined. |
| *Added in ch12:* A `Texture` is an index into the renderer's table, at most 64. Each file is loaded once, for the whole run, and asking again returns the same texture. A file that can't be loaded logs why and returns texture 0. | `GameState` holds numbers (§3.2). A game that starts over gets back the numbers it had without reading a file, and chapter 33 can reload a file under the same number. A missing picture shows as a white rectangle. |
| *Added in ch12:* `draw_sprite` has no flags yet | Flipping comes when the player faces left (chapter 29). The bat looks the same both ways. |
| *Added in ch12:* The 8×8 font is the engine's: the 95 printable ASCII glyphs, in code (`font8x8`, public domain), made into a texture when the renderer starts. `draw_text` draws one sprite per char, and any other char as `?`. | It needs no file, so chapter 13's overlay works even when the game's files don't. The game draws text with it too. |
| *Added in ch13:* The debug overlay draws on the screen, after the game's `render`, into a batch of its own that holds 1,024 rectangles. The renderer sorts both batches into one array, the game's first, and draws it in runs as before. | It ends up on top whatever layers the game uses, it can't push the game past its 4,096 rectangles, and the counts it shows are the game's alone. Drawing after the copy into the window would need a second set of coordinates. |
| *Added in ch13:* `read_screen` reads the screen's red, green and blue, top row first | OpenGL's rows start at the bottom, and blending leaves the screen's alpha below 255 under see-through rectangles: 195 under the shadow. |

### 4.6 Numbers

| Decision | Why |
|---|---|
| Simulation uses **fixed-point** `Fx` / `Fx2`: `i32` counting 1/256ths of a pixel (24.8) | Exact and deterministic, which makes record-and-replay possible. 1/256 matches NES subpixels, so MegaMan-style numbers (walking ≈ 1.375 px per frame) translate directly. No float jitter at tile edges. |
| Rendering uses float `V2` and matrices. **Changed in ch10** (next row). | That's what the GPU wants. Positions round to whole pixels at draw time. |
| *Changed in ch10:* The renderer takes whole pixels as `i32`, on the 256×240 screen with y pointing down. Floats exist only on the GPU, and the vertex shader maps pixels to clip space with the screen's size instead of a matrix. | Pixel art is drawn at whole pixels, so the type says so, and the rounding happens once, where the game turns positions into pixels. A screen with no zoom or rotation needs two numbers, not a 4×4 matrix. |
| *Added in ch15:* `Fx` is a struct holding the `i32`, with `+`, `-`, `*` and the comparisons. They're `constexpr`, in `fx.h`, which has no `.cpp`. | Adding a whole number to an `Fx` is a compile error instead of 1/256 of a pixel. The game compiles the operators into its own code, and in the release build an `Fx` add is one add instruction. |
| *Added in ch15:* The game writes fractions as decimals, `fx::from_double(1.375)`, which is `consteval` and rounds to the nearest 256th | The source shows the numbers it means, and the running game never computes with a `double`. A number that isn't a 256th is rounded once, in the constant, not at every add: at 0.1 a tick, an `f32` was drawn a pixel off on 30 of 600 ticks. |
| *Added in ch15:* An `Fx` becomes whole pixels with `fx::floor`, which rounds toward minus infinity, and only where the game draws | Dividing by 256 rounds toward zero, which would draw everything from -0.99 to 0.99 at pixel 0. Floor is a shift, and what the NES drew. |
| *Added in ch15:* Multiplying two `Fx` works in an `i64`, and `ASSERT`s that the answer fits | In an `i32`, 1000 × 1000 overflowed: UBSan stopped the debug build, and the release build said 16,960. |

### 4.7 Entities

| Decision | Why |
|---|---|
| One `Entity` struct with a `kind` enum, in a fixed `Pool<T>` with generational handles | Enough for a few dozen entities. The handles catch references to entities that no longer exist. |
| ECS comes later, in Part II: build a small sparse-set ECS by hand, port the game or part of it, and compare honestly | You learn ECS best after running into the problem it solves. At this size, plain structs may come out ahead, and the book will say so if they do. |
| No EnTT | It's heavy on templates and breaks §3.1. |
| *Added in ch16:* A `Pool<T, N>` keeps its items in arrays inside itself, so a zeroed pool is an empty one, and `GameState` holds it like any other field. Its functions are templates in `pool.h`. | Nothing to set up and no pointer to keep. The pool's size is part of `GameState`'s, so changing the capacity starts the game over instead of leaving an array that's too small. |
| *Added in ch16:* A **handle** is a slot's index and the slot's generation when the item was added. Each add into a slot counts its generation up, from 1, so a zeroed handle finds nothing. | Measured with the game's two nests: with an index alone, the left nest took the right nest's bat for its own, four seconds in, and let out its next bat two seconds late. Without generation 0 meaning none, every zeroed handle found the player in slot 0. |
| *Added in ch16:* `add` takes the first free slot, found by scanning. `remove` `ASSERT`s that its handle still finds the item, and zeroes the slot. Nothing ever moves. | 32 flags are half a cache line, and the lowest free slot keeps the log easy to follow. Removing an item twice is a bug. A pointer kept past the remove reads `ENTITY_NONE`. Entities can be added and removed in the middle of the loop that visits them. |
| *Added in ch16:* An entity's kind 0 is `ENTITY_NONE`. `update` and `render` each `switch` on the kind, with no `default`, and `ENTITY_NONE`'s `case` is an `ASSERT`. New kinds go at the end of the enum. | An entity added without a kind stops the game instead of becoming a second player. `-Wswitch` names a kind with no `case`. A kind added in the middle turned the bats into another kind on a reload, and `GameState`'s size couldn't show it. |

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
| *Added in ch03:* Each test file is its own program. Its tests are `static` functions that its `main` runs one by one. | No self-registering tests, which would run code before `main` (§3.1). A test left out of `main` is an unused function, which `-Wall` warns about. A crash stops one program, not every test. |
| *Added in ch03:* A failed check ends its test, not the program. Each test's name is printed before it runs. | One run reports every failing test, and if a test crashes, its name is already on screen. |
| *Added in ch03:* Test programs link the engine as a static archive, built from the same object files as `libmomo.so` | A test gets only the engine code it uses, so the arena test won't need SDL, and it can call functions that aren't `MOMO_API`. |
| *Added in ch03:* `make test` runs every test program on the debug build, then fails if any of them failed | Every failure shows up in one run, and the sanitizers catch memory bugs and UB, not only wrong answers. |
| *Added in ch07:* The debug build's executable tells LeakSanitizer to skip leaks whose call stack passes through `libSDL3.so`, and LeakSanitizer lists what it skipped at every exit | The graphics driver keeps memory that only its own globals point to, so it looks leaked once `SDL_Quit` unloads the driver. momo's own memory comes from arenas. The list keeps the suppression in sight. |
| *Added in ch13:* The debug overlay shows frames and ticks a second, each frame's work, the game's rectangles and draw calls in the last frame, and the peaks of its arenas. It's hidden until `` ` `` is pressed, or `--overlay` is given. | Each line answers a question earlier chapters needed an outside tool or a throwaway patch for. |
| *Added in ch13:* `--capture TICK FILE` runs the game to that tick, at its usual speed, saves the 256×240 screen as a PNG file, and quits. Failing to save makes momo exit with 1. | The game depends only on its ticks (§4.3), so the same command saves the same file on every run, on any display. Seconds or frames would land on a different tick each time. |
| *Added in ch14:* The overlay's last line names every button on the pad, white while it's held | It says whether a key reaches the pad at all, before the game's code is suspected. |

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
| *Added in ch12:* Until chapter 30's bake tool, pictures are PNG files in `assets/`, which `make` copies next to the executable, and which stb_image decodes when the game loads them. Paths are relative to the executable's folder. `assets/LICENSES.md` lists every asset. | Every image editor saves PNG, without losing a pixel. A build folder runs from anywhere (§4.1). |

## 7. Build, dependencies, repo

| Decision | Why |
|---|---|
| GNU Make. `make` builds the debug build (`-O0 -g3`, AddressSanitizer + UndefinedBehaviorSanitizer, asserts on). `make release` uses `-O2 -g` with asserts still on. Both support hot reload. No static shipping build. | Separate compilation and linking stay visible. The sanitizers catch exactly the bugs C-style code invites. |
| `libmomo.so` is built with `-fPIC` and an export macro | Required by §4.1. |
| *Added in ch01:* warnings are `-Wall -Wextra -Wno-missing-field-initializers` | In C++, `-Wextra` warns when a designated initializer leaves fields out, and leaving them out is how ZII code is written (§3.1). |
| *Added in ch02:* Each build config has its own folder under `build/`. Make tracks header dependencies, and every rule also depends on the Makefile. | Both builds exist side by side. Editing a header or a flag rebuilds exactly what it affects. |
| *Added in ch02:* Shared libraries link with `-Wl,--no-undefined` | Calling an engine function that isn't exported fails at link time instead of when `game.so` loads. |
| *Added in ch02:* Every build writes `compile_commands.json` at the repo root, from the build's own flags | clangd sees each file with the build's exact flags. It holds absolute paths, so it's generated and gitignored. |
| *Added in ch03:* The debug build adds `-fno-sanitize-recover=all` | UBSan otherwise prints a report and keeps going, so a test with undefined behavior could still pass. Bugs stop the program (§3.3), in the tests and in the game. |
| *Added in ch03:* Test programs are built and listed in `compile_commands.json` like the host | They're executables too. |
| SDL3 from the system through `pkg-config` | Already installed (3.4). |
| *Added in ch07:* Only the engine gets SDL's compile and link flags | The host and the game never call SDL. With `--no-undefined`, a game that tries fails to link. |
| Copied into `third_party/` with pinned versions and licenses: glad, stb_image, stb_image_write, stb_vorbis, a JSON library, and Dear ImGui later. **Changed in ch13** (stb_image_write, below). | Libraries handle file formats, GL loading and debug UI. We write everything that's a learning goal. |
| *Added in ch08:* glad is generated once, by a pinned glad version with `--reproducible`, and its output is committed with a README that records the command. Vendored C code is compiled as C, and the flags that pick a build config apply to both languages. | Building momo needs no Python, and the same command always writes the same files. |
| *Added in ch12:* stb_image 2.30 is copied as released, with its commit and checksum in a README, and compiled once, in momo's `stb_image.c`, for PNG only and without `fopen` | Pinned like glad. PNG is all momo loads, and reading files is the platform's job (§4.4). |
| *Changed in ch13:* PNG files are written by SDL 3.4's `SDL_SavePNG`, through `platform::write_png`, and stb_image_write isn't vendored. stb_image still reads them. | `platform::` uses SDL wherever SDL has the function (§4.4). Only the compressed bytes depend on SDL's version, never the pixels. `SDL_LoadPNG` returns each file in its own format, and chapter 30's bake tool needs a PNG reader without SDL. |
| *Added after ch09:* `.clang-format` describes the style the code already had: WebKit style, 100 columns. `make format` applies it. `.editorconfig` gives other editors the indents and line endings. | Neovim formats with clang-format, and its default style would have rewritten nearly every line. |
| *Added after ch09:* `make lint` fails on unformatted code and on any clang-tidy finding. The checks are picked by hand: the analyzer, `bugprone`, §3.4's naming rules, and no globals in `game/` (§3.2). An exception gets a `NOLINT` comment that says why. | Lint helps only while it stays at zero findings. `cppcoreguidelines` and `modernize` are left out because they flag C-style casts, raw pointers and the lack of RAII, which §3.1 chose on purpose. |
| Git stays local for now. Code and book licenses get decided when publishing. | Nothing depends on that choice yet. |

```
momo/
  Makefile
  book/          chapters (NN-slug.md), plan.md, outline.md, changelog.md, design.md
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

The chapter-by-chapter outline is in [outline.md](outline.md).

## 9. Out of scope

Networking · saving or passwords · threads of our own · 3D · an in-engine editor · localization, and fonts beyond the 8×8 bitmap font · web or mobile · a static shipping build · testing on Windows or macOS during Parts I and II (the code stays portable, but only Linux gets tested).

Anything here can move into scope later through a chapter. If it does, [changelog.md](changelog.md) records it.
