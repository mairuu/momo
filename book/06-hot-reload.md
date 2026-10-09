# 06 · Hot Reload

> **Goal:** change the game while it runs. Rebuild `game.so` in one terminal, and the game running in another picks up the new code with its bat still where it was. Then change the shape of the game's state and watch the engine start the game over by itself, break one of the hot-reload rules on purpose, and watch in gdb as one `game.so` replaces another at the same address.

## The problem

So far, changing the game means quitting it, running `make` and starting it again, and the game starts from the beginning every time. That's fine while it runs for three frames. It won't be when you're tuning a jump in the middle of a stage, or the boss's third attack: every change would mean playing back to where you were.

Chapter 02 split momo up so this could change. The game is a shared library, `game.so`, which the engine opens itself with `dlopen`, so the engine can also close it and open a new one. Three things are missing:

- **Noticing.** The engine has to find out that `game.so` was rebuilt.
- **Swapping.** It has to close the old `game.so`, open the new one, and get the new addresses of `init`, `update` and `shutdown`.
- **Keeping.** The game has to carry on from where it was, and so far it has nothing to carry on with. It has no state at all: chapter 02 had it ask the engine for the frame number, because a counter of its own would have been a global in `game.so`, and chapter 04 found it had nowhere to keep a pointer.

One more thing: the game quits after three frames, which is too soon to rebuild anything. It has to keep running.

## Design

### What a reload throws away

When the loader opens `game.so`, it maps three kinds of memory from the file:

| | What's in it | Can be |
|---|---|---|
| Code | The compiled functions | Read and run |
| Constants | String literals, `constexpr` tables, other read-only data | Read |
| Globals | Global and `static` variables | Read and written |

`dlclose` unmaps all three. `dlopen` maps fresh copies from the new file: new code, new constants, and every global back at its starting value. The rest of the process isn't touched: the engine's code and its state, and the arenas.

```
                 before the reload              after the reload
  libmomo.so     engine code and state          the same
  game.so        code, constants, globals       new code, new constants, fresh globals
  the arenas     the game's state               the same
```

So the game's state has to live in the arenas, and nothing in it may point into `game.so`. Those are the six rules for game code ([plan §3.2](plan.md#32-rules-for-game-code-required-by-hot-reload)) that [chapter 01](01-dialect.md) put off until now. Each one is the picture above applied to one thing:

| Rule | What a reload does if you break it |
|---|---|
| All game state lives in memory the engine owns | Anything else was in the old `game.so`, or somewhere the engine doesn't know about |
| No globals and no `static` locals | They're back at their starting values: the new `game.so` brought fresh copies |
| No string-literal pointers in game state | The text was one of the old `game.so`'s constants |
| Store indices into `constexpr` tables, not pointers | The table was one of the old `game.so`'s constants too |
| No function pointers in game state | The function was the old `game.so`'s code |
| No `virtual` | An object with virtual functions holds a hidden pointer to its class's table of functions, the vtable, which was one of the old `game.so`'s constants |

`constexpr` constants themselves are fine to use. `BAT_SPEED` below is part of the code, and getting its new value after a reload is the whole point. Tuning numbers live in code because hot reload makes editing them as quick as editing a data file ([plan §5](plan.md#5-game)).

A pointer into the old `game.so` doesn't reliably crash, and that's why these are rules you follow from the start rather than bugs you wait for. The new `game.so` is often loaded at exactly the address the old one had, so a stale pointer points into the new library: at the same thing, if your edit didn't move it, and at something else if it did. Run it breaks a rule and shows both.

### Where the state lives: `GameState`

The game defines one struct, `GameState`, for everything it remembers from one frame to the next. The engine keeps it in the permanent arena and passes it to every call:

```cpp
struct GameApi {
    i32 state_version;
    i64 state_size;
    void (*init)(GameState* state);
    void (*update)(GameState* state);
    void (*shutdown)(GameState* state);
};
```

So the engine allocates something it can't see inside. `momo.h` only declares `struct GameState;`, without fields. C++ calls that an **incomplete type**: code can pass pointers to it around, but can't read its fields or take its `sizeof`. The game's own `.cpp` file defines it. The engine can't come to depend on what the game keeps, and the game can change it freely. The one thing the engine needs, the size, comes in the API struct: `state_size` is `sizeof(GameState)`, worked out where the type is complete, in `game.so`.

The engine pushes `state_size` bytes from the permanent arena, so the game starts from all zeros (ZII), and `init` sets only what shouldn't be zero.

### Noticing a new `game.so`

| Option | The catch |
|---|---|
| A key that reloads | Easy to forget, and there's no window or keyboard until chapters 07 and 14 |
| `inotify`, where the kernel reports changes to a file | Linux-only, and a file descriptor and a queue of events to read, all for one file |
| **Check `game.so`'s modification time every frame** | One system call per frame |

momo checks the time, the same test make uses to decide what to rebuild (chapter 02). `stat` returns it. A call took about 300 nanoseconds on this machine, which is nothing next to a 16 ms frame. Handmade Hero checks its game DLL's write time every frame the same way.

### A file that's still being written

The linker doesn't produce `game.so` all at once. It creates the file and then writes it from front to back. That takes long enough that the engine, checking every 16 ms, caught the file half-written on ten rebuilds out of ten (exercise 3):

```
engine/momo.cpp:144: error: /home/mairuu/repos/momo/build/debug/game.so: file too short
engine/momo.cpp:208: reloaded /home/mairuu/repos/momo/build/debug/game.so
```

That time `dlopen` found a file too short to even hold its header and refused it, and the engine loaded the finished file a few frames later, when its time changed again. A file that's further along could have a valid header and code that isn't there yet.

The fix is two lines in the Makefile: link to `game.so.tmp`, then `mv game.so.tmp game.so`. Within one file system, `mv` is a **rename**. It changes which file the name `game.so` refers to, in one step, so whoever opens `game.so` gets the old file or the finished new one, never half of one. (Handmade Hero gets the same effect differently: its build script holds a lock file while it builds, and the game waits for the lock to go away.)

What happens to the old file? It loses its name, but the running engine still has it mapped, and a file's contents last until nothing uses them. So the old code keeps running, safely, until the engine closes it. GNU ld is careful in the same way: it deletes an existing output file before writing a new one, instead of writing over it. That's why rebuilding `libmomo.so` under a running momo doesn't corrupt the engine that's running.

### Swapping: close, then open

The careful order would be to open the new `game.so` first, and close the old one only if that worked. `dlopen` won't do it. It remembers the paths it has opened, and opening the same path again hands back the library it already has (exercise 1). The way around it is to copy `game.so` to a new name before every load, `game_1.so`, `game_2.so` and so on, and clean the copies up. Handmade Hero copies, because Windows won't let the compiler overwrite a DLL that's loaded. momo closes first, then opens.

If the new library fails to load, there's no game for a while. The engine logs why, skips `update` until `game.so` changes again, and leaves the `GameState` in the arena alone. Once a fixed build loads, the game carries on where it stopped.

### When `GameState` changes

A reload hands new code the old bytes. If `GameState` gained a field, lost one, or had two fields swap places, the new code reads those bytes with a different meaning.

| Option | The catch |
|---|---|
| Never change `GameState` while the game runs | Then you restart for every new field anyway |
| Convert the old state to the new layout | Code for every change. Real games do this for save files. |
| **Notice the change and start the game over** | You lose your place, but only when the layout changes |

The engine notices with the two numbers in `GameApi` ([plan §4.1](plan.md#41-three-binaries-and-hot-reload)):

- `state_size` is `sizeof(GameState)`. It changes by itself when you add or remove a field.
- `state_version` is the game's `GAME_STATE_VERSION`, which you bump by hand for a change that keeps the size, such as two `i32` fields swapping places.

If either differs from what the current state was made with, the engine logs a warning, momo's first `LOG_WARN` as chapter 05 promised. Then it resets the game's memory, pushes a new `GameState` and calls `init` again. It doesn't call the old `shutdown`: that code is already gone, and there's nothing to give back, since the arenas are reset.

If you forget to bump the version, the engine can't tell. Run it shows what that looks like.

### The engine's own memory

Starting over resets the stage arena as well as the permanent one, because whatever the old `GameState` pointed to in the stage arena belongs to a game that's gone. That breaks a promise from chapter 04, where the permanent arena lasted the whole run. Meanwhile the engine has something that really does last the whole run: the path to `game.so`, which it checks every frame. If that path were in the permanent arena, a layout change would wipe it.

So the arenas now split by owner as well as by lifetime. Permanent and stage hold the game's memory and nothing else. The engine gets a fourth arena of its own, which the game can't reach and nothing resets:

```
0           16 MB                        48 MB         56 MB         64 MB
├─permanent──┼───────────stage───────────┼────frame────┼───engine────┤
└────────── the game's memory ───────────┘
```

The game's memory comes first, in one piece, which chapter 21 will need: recording and looping the game copies its memory, and copying the engine's memory along with it would roll the engine back too. The frame arena sits between them. Both sides use it for memory that lasts one frame, and nothing in it lasts long enough to matter to a reload. The plan's memory section records the change ([§4.2](plan.md#42-memory)).

### What can't be reloaded

Only `game.so` reloads. `libmomo.so` was loaded before `main`, and it stays until the program exits. If you change the engine and rebuild, the running momo keeps its old engine, and a new `game.so` that calls a new engine function fails to load, which Run it shows. `momo.h` is the same problem from the other side: both binaries were compiled against it, and if `GameApi` changed shape, the running engine would read the new game's struct the old way. Changed anything outside `game/`? Restart momo.

### Keeping the game running

Two stopgaps keep the game running until there's a window:

- **The loop sleeps** 16 ms after each frame, which makes about 60 frames a second. Chapter 09 replaces the sleep with a real clock and a fixed timestep.
- **Ctrl+C stops it.** Ctrl+C sends the process the signal `SIGINT`, which normally ends it on the spot: no `shutdown`, no arena report. momo installs a handler that asks the loop to stop instead. SDL does the same thing, so chapter 07 hands the job over to it.

## The code

### The boundary: `engine/momo.h`

`engine/momo.h:45`

```cpp
struct GameState;

struct GameApi {
    i32 state_version;
    i64 state_size;

    void (*init)(GameState* state);
    void (*update)(GameState* state);
    void (*shutdown)(GameState* state);
};
```

(The file has comments on each.) `GameApi` gains the two layout numbers, and the three functions take the state. A struct of two numbers and three function pointers still fits the one-export boundary from chapter 02: the engine calls `momo_game_api` again after every reload and gets everything at once.

The comment on the arenas (`engine/momo.h:26`) now says the permanent arena lasts the whole run unless a reload changes `GameState`'s layout.

### Four arenas: `engine/momo.cpp`

`engine/momo.cpp:17`

```cpp
struct Engine {
    Str game_path;  // the full path to game.so, in the engine's own arena
    void* game_lib; // the handle dlopen returned for game.so, or null
    i64 game_time;  // game.so's modification time when it was last loaded
    GameApi game;

    GameState* state;
    i32 state_version;
    i64 state_size;

    i32 frame_index;
    bool quit_requested;

    u8* memory; // one block from mmap, split into the four arenas
    i64 memory_size;
    Arena permanent; // the game's three arenas
    Arena stage;
    Arena frame;
    Arena arena; // the engine's own, which the game can't reach
};
```

`state_version` and `state_size` are what the current `GameState` was made with. They're kept apart from `game`, which a reload replaces, so the engine has something to compare the new numbers with even after a failed load.

`init_memory` (`engine/momo.cpp:54`) reserves `ENGINE_SIZE`, another 8 MB, and sets up `engine.arena` after the frame arena, so the block is now 64 MB. `shutdown_memory` reports its peak too.

### Where `game.so` is, and when it changed

`find_game` (`engine/momo.cpp:99`) is chapter 05's path code, moved out of `load_game` so it runs once. The path now goes in the engine's arena instead of the frame arena, since the engine checks it every frame for the whole run:

```cpp
    engine.game_path = str::fmt(&engine.arena, "%.*s/game.so", folder.len, folder.data);
```

`engine/momo.cpp:121`

```cpp
static i64 file_time(Str path)
{
    struct stat info = {};
    if (stat(path.data, &info) != 0)
        return 0;
    return (i64)info.st_mtim.tv_sec * 1000000000 + info.st_mtim.tv_nsec;
}
```

`stat` fills in a struct about the file, and `st_mtim` is its modification time, in seconds plus nanoseconds. `file_time` folds the two into one `i64` of nanoseconds, so comparing two times is one `==`. A missing file gives 0, and ZII makes 0 a natural "no time".

C has a function called `stat` and a struct called `stat`. C can tell them apart, because a struct's name only counts after the word `struct`. In C++, `stat` alone means the function, so declaring the struct needs `struct stat`.

### Loading and unloading

`engine/momo.cpp:131`

```cpp
static bool load_game()
{
    engine.game_time = file_time(engine.game_path);

    engine.game_lib = dlopen(engine.game_path.data, RTLD_NOW);
    if (!engine.game_lib) {
        LOG_ERROR("%s", dlerror());
        return false;
    }
    // ...dlsym and get_game_api(), as in chapter 05...
}
```

Two details in the first line. The time is read *before* `dlopen`, so if the file changes again between the two, the next frame sees a newer time and loads it again; read after, the engine could record the newer time with the older code loaded, and never notice. And it's recorded even if the load fails, so a broken `game.so` is tried once, not 60 times a second.

`engine/momo.cpp:163`

```cpp
static void unload_game()
{
    dlclose(engine.game_lib);
    engine.game_lib = nullptr;
    engine.game = {};
}
```

Zeroing `engine.game` afterwards means no stale function pointer into the closed library is left lying around in the engine either.

### Starting the game

`engine/momo.cpp:173`

```cpp
static void start_game()
{
    arena::reset(&engine.permanent);
    arena::reset(&engine.stage);

    engine.state = (GameState*)arena::push_bytes(&engine.permanent, engine.game.state_size,
                                                 alignof(max_align_t));
    engine.state_version = engine.game.state_version;
    engine.state_size = engine.game.state_size;
    engine.game.init(engine.state);
}
```

This runs when momo starts and again after every layout change. The resets don't zero anything themselves, but `push` zeroes what it returns (chapter 04), so the new `GameState`, and everything the game pushes after it, starts at zero. The state always lands at the start of the permanent arena.

The engine can't ask for `alignof(GameState)`, an incomplete type. `max_align_t`, from `<stddef.h>`, is a type as aligned as any ordinary type gets, 16 bytes on x86-64. That's the same promise `malloc` makes.

### Reloading

`engine/momo.cpp:190`

```cpp
static void reload_game_if_changed()
{
    i64 time = file_time(engine.game_path);
    if (time == 0 || time == engine.game_time)
        return;

    if (engine.game_lib)
        unload_game();
    if (!load_game())
        return; // already logged: the game waits, its state untouched, for the next build

    if (engine.game.state_version == engine.state_version &&
        engine.game.state_size == engine.state_size) {
        LOG_INFO("reloaded %.*s", engine.game_path.len, engine.game_path.data);
        return;
    }

    LOG_WARN("GameState changed from version %d, %lld bytes, to version %d, %lld bytes: "
             "starting the game over",
             engine.state_version, engine.state_size, engine.game.state_version,
             engine.game.state_size);
    start_game();
}
```

This is the whole of hot reload, top to bottom:

1. **Notice.** Same time, nothing to do. A missing file (in the middle of `make clean`, say) isn't a change either: the old code is still mapped and keeps running (exercise 5).
2. **Swap.** Close, then open. `if (engine.game_lib)` skips the close when the last load failed and there's nothing open.
3. **Keep.** Same layout: carry on with the state as it is. A different layout: warn and start over.

### The loop

`engine/momo.cpp:225`

```cpp
static volatile sig_atomic_t interrupted;

static void on_interrupt(int)
{
    interrupted = 1;
}
```

A signal handler can run between any two instructions of the program, even in the middle of a `printf`, so it should do as little as possible. This one sets a flag and returns. `sig_atomic_t` is the integer type C promises a handler can write in one piece, and `volatile` tells the compiler the variable can change behind its back, so the loop reads it fresh each time instead of keeping an old copy in a register. It's a file-scope variable, like `engine`, which game code couldn't have, but the engine never reloads.

`engine/momo.cpp:244`

```cpp
    signal(SIGINT, on_interrupt);

    start_game();
    while (!engine.quit_requested && !interrupted) {
        reload_game_if_changed();
        arena::reset(&engine.frame); // last frame's memory is free again
        if (engine.game_lib)
            engine.game.update(engine.state);
        engine.frame_index++;

        usleep(16 * 1000);
    }
    if (interrupted)
        LOG_INFO("interrupted");

    if (engine.game_lib) {
        engine.game.shutdown(engine.state);
        unload_game();
    }
```

The reload check comes first in the frame, so a reload always happens between two updates, never in the middle of one. `update` and `shutdown` are skipped while no game is loaded. `usleep` takes microseconds: 16,000 of them is 16 ms. With the loop's own work, that comes to about 60 frames a second.

### The game: `game/game.cpp`

`game/game.cpp:9`

```cpp
constexpr i32 GAME_STATE_VERSION = 1;

struct GameState {
    i32 bat_x;   // pixels from the left edge of the screen
    i32 bat_dir; // 1 flying right, -1 flying left
};
```

The game's whole state is one bat: where it is, and which way it's flying. It's defined at the top of `game.cpp`, outside `namespace game`, because `momo.h` declared it outside any namespace, and the two have to name the same type.

`game/game.cpp:21`

```cpp
constexpr i32 SCREEN_WIDTH = 256;
constexpr i32 TILE_SIZE = 16;
constexpr i32 BAT_SPEED = 1; // pixels per frame

void init(GameState* state)
{
    LOG_INFO("init");
    // The engine hands over a zeroed GameState, so the bat starts at x = 0.
    state->bat_dir = 1;
}

void update(GameState* state)
{
    // Fly, and turn around at either edge of the screen.
    state->bat_x += BAT_SPEED * state->bat_dir;
    i32 right_edge = SCREEN_WIDTH - TILE_SIZE;
    if (state->bat_x <= 0 || state->bat_x >= right_edge) {
        state->bat_x = clamp(state->bat_x, 0, right_edge);
        state->bat_dir = -state->bat_dir;
    }
    // ...
```

The bat is a 16-pixel tile on the 256-pixel screen from the plan ([§4.5](plan.md#45-rendering)), so its `x` runs from 0 to 240, and `clamp` from chapter 03 keeps it there when it turns. Speeds are pixels per frame, the way MegaMan's were.

The rest of `update` (`game/game.cpp:42`) draws the screen twice a second, every 30th frame, as a row of 16 tiles in the log: `|.......B........|`. It builds the row in a `char` array on the stack, which is gone when `update` returns, so nothing in it can outlive a reload.

`game/game.cpp:63`

```cpp
extern "C" MOMO_API GameApi momo_game_api()
{
    return {
        .state_version = GAME_STATE_VERSION,
        .state_size = sizeof(GameState),
        .init = game::init,
        .update = game::update,
        .shutdown = game::shutdown,
    };
}
```

`sizeof(GameState)` is worked out here, where `GameState` is complete. The engine calls this after every load, so every `game.so` reports its own layout.

### The Makefile

`Makefile:94`

```make
$(OUT)/game.so: $(GAME_OBJ) $(OUT)/libmomo.so Makefile
	$(CXX) $(LIB_LDFLAGS) $(GAME_OBJ) $(LINK_MOMO) -o $@.tmp
	mv $@.tmp $@
```

`$@.tmp` is `build/debug/game.so.tmp`. If the link fails, `mv` never runs, so the running game keeps the last good `game.so`, and the next build replaces whatever the linker left in `game.so.tmp`. Only `game.so` gets this treatment, since it's the only file momo reloads.

## Run it

### Hot reload

You need two terminals in the repo root. In the first, build and start the game:

```
$ make
$ ./build/debug/momo
engine/momo.cpp:77: reserved 64 MB at 0x7bf8e1800000
engine/momo.cpp:242: loaded /home/mairuu/repos/momo/build/debug/game.so
game/game.cpp:27: init
game/game.cpp:51: frame    0  |B...............|  x =   1
game/game.cpp:51: frame   30  |.B..............|  x =  31
game/game.cpp:51: frame   60  |...B............|  x =  61
game/game.cpp:51: frame   90  |.....B..........|  x =  91
```

Twice a second, the bat moves 30 pixels, about two tiles. Leave it running. In the second terminal, make the bat twice as fast: change `BAT_SPEED` to 2 in `game/game.cpp:23`, and rebuild.

```
$ make
g++ -std=c++20 ... -c game/game.cpp -o build/debug/obj/game/game.o
g++ -fsanitize=address,undefined -shared ... -o build/debug/game.so.tmp
mv build/debug/game.so.tmp build/debug/game.so
```

Only the game is rebuilt: one compile, one link to the temporary name, one rename. In the first terminal:

```
game/game.cpp:51: frame  120  |.......B........|  x = 121
engine/momo.cpp:208: reloaded /home/mairuu/repos/momo/build/debug/game.so
game/game.cpp:51: frame  150  |.........B......|  x = 157
game/game.cpp:51: frame  180  |.............B..|  x = 217
game/game.cpp:51: frame  210  |............B...|  x = 204
game/game.cpp:51: frame  240  |.........B......|  x = 144
```

After the reload the bat covers 60 pixels a line, two a frame, and turns at the right edge. (From 120 to 150 it's a mix of both speeds: the reload came at frame 144.) Nothing started over. There's no `init` line, the frame count carries on, and the bat carries on from where it was, at the new speed. That's the new code running on the old state.

### Change `GameState`

Keep the game running. Count the bat's turns: add a field to `GameState`,

```cpp
    i32 bat_turns;
```

add `state->bat_turns++;` after `state->bat_dir = -state->bat_dir;`, and change the log line at the end of `update` to print it:

```cpp
    LOG_INFO("frame %4d  |%.*s|  x = %3d  turns = %d", frame, ARRAY_COUNT(row), row,
             state->bat_x, state->bat_turns);
```

`make` again:

```
game/game.cpp:51: frame  360  |......B.........|  x =  96
engine/momo.cpp:212: warning: GameState changed from version 1, 8 bytes, to version 1, 12 bytes: starting the game over
game/game.cpp:28: init
game/game.cpp:53: frame  390  |..B.............|  x =  36  turns = 0
game/game.cpp:53: frame  420  |......B.........|  x =  96  turns = 0
game/game.cpp:53: frame  450  |.........B......|  x = 156  turns = 0
game/game.cpp:53: frame  480  |.............B..|  x = 216  turns = 0
game/game.cpp:53: frame  510  |............B...|  x = 204  turns = 1
```

The size went from 8 bytes to 12, so the engine warned and started over: `init` ran, and the bat starts from the left edge again. The frame count didn't restart, because the engine keeps it. The warning reads the way a compiler's does, so the editor jump list from chapter 05 picks it up too.

### Same size, different layout

Still running. Swap the first two fields of `GameState`, so `bat_dir` comes before `bat_x`, and `make`. The size doesn't change, and the version doesn't either:

```
game/game.cpp:53: frame  690  |.........B......|  x = 156  turns = 2
engine/momo.cpp:208: reloaded /home/mairuu/repos/momo/build/debug/game.so
game/game.cpp:53: frame  720  |B...............|  x =   0  turns = 28
game/game.cpp:53: frame  750  |B...............|  x =   0  turns = 58
game/game.cpp:53: frame  780  |B...............|  x =   0  turns = 88
```

The engine saw nothing wrong, and reloaded. But the new code reads the first four bytes as `bat_dir`, and those bytes held `x`, about 158. So the bat flies 316 pixels a frame, more than the width of the screen, hits a wall on every frame and turns around: 30 turns in every 30 frames. At least it's obvious. A swapped pair of flags or two similar counters could go wrong quietly.

Tell the engine: set `GAME_STATE_VERSION` to 2, and `make`:

```
engine/momo.cpp:212: warning: GameState changed from version 1, 12 bytes, to version 2, 12 bytes: starting the game over
game/game.cpp:28: init
game/game.cpp:53: frame  870  |.B..............|  x =  24  turns = 0
```

Now press Ctrl+C in the first terminal:

```
game/game.cpp:53: frame  990  |.............B..|  x = 216  turns = 1
^Cengine/momo.cpp:259: interrupted
game/game.cpp:59: shutdown, with the bat at x = 212
engine/momo.cpp:266: quit after 993 frames
engine/momo.cpp:83: permanent arena: at most 12 bytes used of 16 MB
engine/momo.cpp:83: stage     arena: at most 0 bytes used of 32 MB
engine/momo.cpp:83: frame     arena: at most 0 bytes used of 8 MB
engine/momo.cpp:83: engine    arena: at most 44 bytes used of 8 MB
```

The terminal printed `^C`, and the handler turned it into a normal quit: `shutdown` ran, and the arenas reported. The permanent arena peaked at 12 bytes, one `GameState`. The engine arena holds the 43-character path and its 0. The frame arena is unused now that the path has moved out of it.

Put the game back with `git checkout game/game.cpp`.

### Break a rule

Give the bat a name, kept the way the rules say not to: a pointer to a string literal in `GameState`. Start the game again, and add a field after `bat_dir`,

```cpp
    const char* name;
```

set it in `init` after `state->bat_dir = 1;`,

```cpp
    state->name = "bat";
```

and log it instead of `x =`:

```cpp
    LOG_INFO("frame %4d  |%.*s|  %s at x = %3d", frame, ARRAY_COUNT(row), row, state->name,
             state->bat_x);
```

`make`. The size changed, so the game starts over and `init` stores the pointer. Then change `BAT_SPEED` to 2 and `make` again. So far, so good:

```
engine/momo.cpp:212: warning: GameState changed from version 1, 8 bytes, to version 1, 16 bytes: starting the game over
game/game.cpp:28: init
game/game.cpp:53: frame   90  |B...............|  bat at x =   4
...
game/game.cpp:53: frame  180  |.....B..........|  bat at x =  94
engine/momo.cpp:208: reloaded /home/mairuu/repos/momo/build/debug/game.so
game/game.cpp:53: frame  210  |........B.......|  bat at x = 143
game/game.cpp:53: frame  240  |............B...|  bat at x = 203
```

The name survived a reload. `name` points into the old `game.so`, which is gone, but the new `game.so` was loaded at the same address, with `"bat"` in the same place, so the old address still holds `"bat"`. Now make an edit that has nothing to do with the name: change the message in `init` to `LOG_INFO("init: the bat starts at the left edge");`, and `make`:

```
engine/momo.cpp:208: reloaded /home/mairuu/repos/momo/build/debug/game.so
game/game.cpp:53: =================================================================
==14972==ERROR: AddressSanitizer: global-buffer-overflow on address 0x7f267e6ec200 at pc 0x7f267e8d0192 bp 0x7ffe8d494e00 sp 0x7ffe8d494578
READ of size 1 at 0x7f267e6ec200 thread T0
    #0 0x7f267e8d0191  (/usr/lib/libasan.so.8+0xd0191) (BuildId: b8a4241051a1621937fdc46e867ba7ecb56d96ea)
    #1 0x7f267e8f1663 in vfprintf (/usr/lib/libasan.so.8+0xf1663) (BuildId: b8a4241051a1621937fdc46e867ba7ecb56d96ea)
    #2 0x7f267ef9c4c5 in logger::print(LogLevel, char const*, int, char const*, ...) engine/logger.cpp:27
    #3 0x7f267e6ebc39 in game::update(GameState*) game/game.cpp:53
    #4 0x7f267ef9d8f6 in momo::run() engine/momo.cpp:251
    #5 0x55b1056a8181 in main host/main.cpp:7
    ...

0x7f267e6ec200 is located 26 bytes after global variable '*.LC1' defined in 'game/game.cpp' (0x7f267e6ec1c0) of size 38
  '*.LC1' is ascii string 'init: the bat starts at the left edge'
0x7f267e6ec200 is located 32 bytes before global variable '*.LC2' defined in 'game/game.cpp' (0x7f267e6ec220) of size 4
  '*.LC2' is ascii string 'bat'
...
SUMMARY: AddressSanitizer: global-buffer-overflow engine/logger.cpp:27 in logger::print(LogLevel, char const*, int, char const*, ...)
==14972==ABORTING
```

The longer message pushed the string literals after it further along, so the old address of `"bat"` is now in the middle of the new library's constants. ASan keeps poisoned padding around every global it knows of, and string literals count, so it can say exactly where the read landed: 26 bytes past the new `init` message, and 32 bytes before the new `"bat"`. `*.LC1` and `*.LC2` are the compiler's own names for the two literals. What ASan can't know is that the pointer was meant for the old library. To it, this is an ordinary overflow.

The release build has no ASan. The same steps there, with `make release` and `./build/release/momo`, end differently:

```
engine/momo.cpp:208: reloaded /home/mairuu/repos/momo/build/release/game.so
game/game.cpp:53: frame  300  |.........B......|  s at the left edge at x = 156
game/game.cpp:53: frame  330  |......B.........|  s at the left edge at x =  96
```

No crash. The old address is now partway into the new `init` message, so the bat's name reads from there to the end of it: `s at the left edge`. A rule broken at one edit fails at another, unrelated one. Keep `"bat"` in game code, where it's a constant, and store the bat's kind as an enum. That's what the game will do from chapter 16 on.

Put the game back with `git checkout game/game.cpp`, and `make` and `make release`.

### A build that doesn't load

Start the game. Then change the engine as well as the game: add a function to `engine/momo.h`, after `frame_index`,

```cpp
// How many frames the engine runs per second.
MOMO_API i32 frames_per_second();
```

define it in `engine/momo.cpp`, above `quit`,

```cpp
i32 frames_per_second()
{
    return 60;
}
```

and use it in `game::update`: `if (frame % (momo::frames_per_second() / 2) != 0)`. `make` rebuilds the engine, the host and the game, since all three include `momo.h`. The game:

```
game/game.cpp:51: frame  150  |.........B......|  x = 151
engine/momo.cpp:144: error: /home/mairuu/repos/momo/build/debug/game.so: undefined symbol: _ZN4momo17frames_per_secondEv
```

The new `game.so` calls `momo::frames_per_second`, and the running engine is the old `libmomo.so`, which doesn't have it. The new `libmomo.so` is on disk, but the loader sees that a library by that name is already loaded and uses it. `RTLD_NOW` makes `dlopen` fail right away, with the mangled name of what's missing.

The game is paused now. No more lines appear, because `update` isn't being called, and the bat's state sits untouched in the arena. Take the call back out of `game::update` (`if (frame % 30 != 0)` again) and `make`:

```
engine/momo.cpp:208: reloaded /home/mairuu/repos/momo/build/debug/game.so
game/game.cpp:51: frame  330  |...........B....|  x = 176
game/game.cpp:51: frame  360  |............B...|  x = 206
```

The bat carries on from where it stopped, about 25 pixels on from frame 150's `x = 151`, though 180 frames went by. To get the new engine, quit and start momo again. Then put everything back with `git checkout engine game`, and `make`.

### Look inside: one `game.so` for another

Watch a reload happen in gdb. Stop at the line in `reload_game_if_changed` that runs only when `game.so` has changed (`engine/momo.cpp:201`). As in chapter 05, `start` first, so `libmomo.so` is loaded and the breakpoint can be set without a question:

```
$ gdb -q ./build/debug/momo
(gdb) start
...
Temporary breakpoint 1, main () at host/main.cpp:7
7	    return momo::run();
(gdb) break engine/momo.cpp:201
Breakpoint 2 at 0x7ffff7f8a733: file engine/momo.cpp, line 201.
(gdb) continue
Continuing.
engine/momo.cpp:77: reserved 64 MB at 0x7bffef800000
engine/momo.cpp:242: loaded /home/mairuu/repos/momo/build/debug/game.so
game/game.cpp:27: init
game/game.cpp:51: frame    0  |B...............|  x =   1
...
game/game.cpp:51: frame  270  |.............B..|  x = 209
```

In the other terminal, change `BAT_SPEED` and `make`. gdb stops:

```
Breakpoint 2, momo::reload_game_if_changed () at engine/momo.cpp:201
201	    if (engine.game_lib)
(gdb) info sharedlibrary game
From                To                  Syms Read   Shared Object Library
0x00007ffff76d9000  0x00007ffff76e0000  Yes         /home/mairuu/repos/momo/build/debug/game.so
(gdb) shell grep game.so /proc/$(pidof momo)/maps
7ffff76d9000-7ffff76db000 r--p 00000000 103:06 22939620                  /home/mairuu/repos/momo/build/debug/game.so (deleted)
7ffff76db000-7ffff76dc000 r-xp 00002000 103:06 22939620                  /home/mairuu/repos/momo/build/debug/game.so (deleted)
7ffff76dc000-7ffff76dd000 r--p 00003000 103:06 22939620                  /home/mairuu/repos/momo/build/debug/game.so (deleted)
7ffff76dd000-7ffff76de000 r--p 00003000 103:06 22939620                  /home/mairuu/repos/momo/build/debug/game.so (deleted)
7ffff76de000-7ffff76e0000 rw-p 00004000 103:06 22939620                  /home/mairuu/repos/momo/build/debug/game.so (deleted)
(gdb) print *engine.state
$1 = {bat_x = 181, bat_dir = -1}
```

`/proc/<pid>/maps` lists every range of memory the process has mapped. Each line is an address range, its permissions (`r` read, `w` write, `x` execute, `p` private), the offset into the file, the file's device and inode number, and the file. The five ranges hold the three kinds of memory from the Design section: `r-xp` is the code and `rw-p` the globals. The three `r--p` ranges are read-only: the constants, the file's headers, and tables the loader filled in and then locked. `(deleted)` means the file has lost its name: `mv` gave the name `game.so` to the new file. The running code is still the old file, inode 22939620.

`print *engine.state` works even though the engine only declares `GameState`: gdb found the definition in `game.so`'s debug information. Step over the close, then the open:

```
(gdb) next
202	        unload_game();
(gdb) next
203	    if (!load_game())
(gdb) info sharedlibrary game
No shared libraries matched.
(gdb) next
206	    if (engine.game.state_version == engine.state_version &&
(gdb) info sharedlibrary game
From                To                  Syms Read   Shared Object Library
0x00007ffff76d9000  0x00007ffff76e0000  Yes         /home/mairuu/repos/momo/build/debug/game.so
(gdb) shell grep game.so /proc/$(pidof momo)/maps
7ffff76d9000-7ffff76db000 r--p 00000000 103:06 22939371                  /home/mairuu/repos/momo/build/debug/game.so
7ffff76db000-7ffff76dc000 r-xp 00002000 103:06 22939371                  /home/mairuu/repos/momo/build/debug/game.so
7ffff76dc000-7ffff76dd000 r--p 00003000 103:06 22939371                  /home/mairuu/repos/momo/build/debug/game.so
7ffff76dd000-7ffff76de000 r--p 00003000 103:06 22939371                  /home/mairuu/repos/momo/build/debug/game.so
7ffff76de000-7ffff76e0000 rw-p 00004000 103:06 22939371                  /home/mairuu/repos/momo/build/debug/game.so
(gdb) print *engine.state
$2 = {bat_x = 181, bat_dir = -1}
(gdb) quit
```

Once `unload_game` had run (the second `next`), no `game.so` was loaded at all. After `load_game` (the third), there's one again, from a different file (inode 22939371, with a name), at exactly the same addresses. That's the reuse that let the bat's name survive its first reload. The state didn't move or change. gdb asks whether to kill the program; answer `y`. Quit at a breakpoint, as in chapter 05, so LeakSanitizer doesn't complain about running under gdb.

### The tests

```
$ make test
arena_test
  push_hands_out_memory_in_order ... ok
  ...
  8 passed
base_test
  ...
  5 passed
str_test
  ...
  10 passed
```

All three test programs pass as before. Hot reload has no test of its own: it needs a running engine, two builds and a file system, so Run it checks it instead.

## Recap

```
momo/
  Makefile          + links game.so under a temporary name and renames it into place
  engine/
    momo.h          + GameState (declared only); GameApi with state_version and state_size,
                      and functions that take a GameState*
    momo.cpp        + reloads game.so when its time changes; GameState at the start of the
                      permanent arena; a fourth arena for the engine; runs until Ctrl+C
  game/
    game.cpp        + GameState and GAME_STATE_VERSION; a bat flying back and forth
```

- A reload replaces `game.so`'s code, constants and globals. Only memory the engine owns survives, so the game keeps everything in a `GameState`, and nothing in it points into `game.so`.
- A stale pointer into the old `game.so` may still work, because the new one often lands at the same address. It stops working at some unrelated edit.
- The engine checks `game.so`'s modification time every frame, closes the old library, then opens the new one. A failed load leaves the game paused with its state untouched.
- The Makefile renames the finished `game.so` into place, so the engine never sees half a file.
- `state_size` catches most layout changes by itself. `GAME_STATE_VERSION` is for the ones that keep the size. Either one changing starts the game over, with a warning.
- Permanent and stage are the game's memory only. The engine has its own arena.
- Only the game reloads. A change to the engine or `momo.h` needs a restart.

The plan records the reload details in [§4.1](plan.md#41-three-binaries-and-hot-reload) and the fourth arena in [§4.2](plan.md#42-memory).

That's the end of Part A, the foundations. Chapter 07 gives momo a window, through SDL3 behind momo's `platform::` module.

## Exercises

Undo each change before the next exercise: `git checkout -- Makefile engine game` puts those back.

1. **Open first, close later.** In `reload_game_if_changed`, keep the old handle in a local, call `load_game` first, and `dlclose` the old handle only after the state check. Log both handles in the `reloaded` line. Run the game, change `BAT_SPEED`, and `make`. What do the handles say, and what speed does the bat fly at?
2. **A `static` counter.** In `game::update`, count the lines it has logged with a `static i32 lines = 0;` and print it in the log line. Reload a few times. What happens to `lines`, and why not to `x`? Then move the counter into `GameState`.
3. **No rename.** In the Makefile, link `game.so` straight to `$@` and delete the `mv` line. With the game running, `touch game/game.cpp` and `make`, several times. What does the engine log, and why does it recover by itself?
4. **A function pointer in `GameState`.** Move the block that moves the bat and turns it around out of `update`, into a function `void fly(GameState* state)` above `init`. Add `void (*fly)(GameState* state);` to `GameState`, set `state->fly = fly;` in `init`, and call `state->fly(state);` in `update`. Run the debug build and change `BAT_SPEED`: what happens? Do the same in the release build, then add `LOG_INFO("fly");` at the top of `fly` and reload again. Which function does the old pointer call, and why does the game seem to recover afterwards?
5. **Delete `game.so`.** With the game running, `rm build/debug/game.so`. Does the game stop? Why can it keep running code from a file that isn't there? Then `make`.
