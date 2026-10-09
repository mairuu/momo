# 04 · Arenas

> **Goal:** give momo its memory: one block from the OS, split into three arenas that each last a different length of time. Watch the game take memory every frame without ever freeing any, run an arena out of memory on purpose, and see that reserving 56 MB costs almost no RAM.

## The problem

A game needs memory for things it only finds out about while it runs: a stage's tilemap when the stage loads, an enemy when it scrolls into view, the list of sprites to draw this frame, the text of the debug overlay. The usual C++ answer is `new` and `delete` (in C, `malloc` and `free`): ask for memory when you need it, and give each piece back when you're done with it.

That gives every allocation a lifetime of its own, and each one is something to get right. Free a piece too early, and some other code reads memory that now belongs to something else, and crashes much later somewhere unrelated. Forget to free it, and it leaks. Free it twice, and the heap is corrupted. Modern C++ answers this with RAII and smart pointers, which free things at the end of a scope, and the dialect rules those out because they hide when things happen ([chapter 01](01-dialect.md)).

momo has two more reasons to want something else, and both arrive soon:

- **Hot reload (chapter 06).** Game state has to survive while `game.so` is swapped out, so it must live in memory the engine owns. When the game's state layout changes, the engine zeros all of it and starts the game again. That's easy if game memory is a few blocks the engine knows about, and impossible if it's a thousand `malloc`ed pieces scattered across the heap.
- **Record and loop (chapter 21).** Snapshotting the game means copying all of its memory. Again, a few blocks are easy.

Look at when things actually stop being needed in a game, and most memory falls into one of three groups: things that last the whole run, things that last one stage, and things that last one frame. A bat doesn't need its own lifetime. It goes when the stage goes.

## Design

### An arena

An arena is a block of memory plus an offset, `used`, that says how much of it has been handed out:

```
base                            base + used                     base + size
│◀─────────── handed out ───────────▶│◀──────────── free ────────────▶│
│ tilemap       │ bat │ bat │ slime  │                                │
```

To allocate, return `base + used` and move `used` forward by the size you asked for. This is called **pushing**, because it works like pushing onto a stack. To free, set `used` back to 0. That frees everything in the arena at once, and it's the only way to free anything. There's no way to free a single piece, and that's the point: there's nothing per piece to keep track of.

| | `malloc` / `free` | An arena |
|---|---|---|
| Allocating | Searches its lists of free blocks for one that fits | Adds to `used` |
| Freeing | One `free` per allocation, each at the right moment | One `reset` for everything with the same lifetime |
| Forgetting to free | A leak | Nothing to forget |
| Using a pointer after it's freed | Reads memory that now belongs to something else | The same: the pointer now points at whatever gets pushed there next |
| Running out | Rare; `malloc` returns null | An `ASSERT`, at a limit you chose |

The fourth row matters. Arenas don't make stale pointers impossible. They make the rule simple: a pointer into an arena is good until that arena is reset. So never store a pointer into a shorter-lived arena inside something that lives longer. A pointer into the frame arena kept in game state points at garbage on the next frame.

### Three arenas

| Arena | Reset | What will go in it | Size |
|---|---|---|---|
| permanent | Never | Game state (chapter 06) | 16 MB |
| stage | By the game, when a stage loads | The tilemap, entities, the stage's music | 32 MB |
| frame | By the engine, before every update | Lists of sprites to draw, debug text, temporary strings (chapter 05) | 8 MB |

The sizes are fixed ([plan §4.2](plan.md#42-memory)). The game can't grow past them, and running out is an `ASSERT`. When that happens, it tells you something real: either the game is doing something it shouldn't, or the limit was too low and you change a constant. These sizes are generous for an NES-style game (the NES itself had 2 KB of RAM), and they cost almost nothing until they're used, which the next section explains. The debug overlay in chapter 13 will show how much of each arena is in use. Until then, the engine prints each arena's peak when it quits.

The engine resets the frame arena because it runs the frames. It doesn't know what a stage is, because game flow belongs to the game ([plan §5](plan.md#5-game)), so the game resets the stage arena itself.

### Where the memory comes from: `mmap`

All three arenas share one block, which the engine asks the OS for at startup with one call to `mmap`, and gives back at the end with `munmap`.

`mmap` gives a process memory in whole **pages**, 4096 bytes each on x86-64. With `MAP_ANONYMOUS` the memory isn't backed by a file; it's plain memory. With `MAP_PRIVATE` it belongs to this process alone. Two properties matter here:

- **The pages come zeroed.** The kernel never hands one process memory that still holds another process's data.
- **A page gets physical memory the first time it's touched.** Until then, the block is only a range of addresses. A 56 MB block the game barely uses costs almost no RAM, and Run it measures exactly that.

Why not `malloc(56 * MB)`? For a request that big, glibc's `malloc` calls `mmap` itself, so it would work. Calling `mmap` directly says what we mean, and a Windows port replaces that one call with `VirtualAlloc`. Why one block and not three? One call, one `munmap`, and in chapter 21, one copy to snapshot the game.

### Zero means default

ZII ([chapter 01](01-dialect.md)) says every struct is valid when it's all zeros, and anything that hands out memory hands out zeros. Fresh `mmap` pages are zeros. But the frame arena reuses the same memory every frame, and after a reset it still holds whatever the last frame left there. There are two places to zero it:

- In `reset`, zero everything that was used.
- In `push`, zero each piece as it's handed out.

momo zeros in `push`. Then "a push returns zeros" is true however the memory was used before, whether it was reset or it's a test's buffer full of garbage, and `reset` stays one line. The cost is a `memset` over memory you're about to use anyway.

### Alignment

CPUs expect a value at an address that's a multiple of its size: an `i32` at a multiple of 4, a `u64` or a pointer at a multiple of 8. x86-64 handles most misaligned values anyway, sometimes more slowly, some other CPUs refuse, and C++ calls it undefined behavior, which UBSan stops on (exercise 3). `malloc` always returns memory aligned for any type. An arena hands out pieces back to back, so it has to do that itself. `push` takes an alignment and skips forward to the next address that's a multiple of it. The bytes it skips are **padding**. `alignof(T)` gives the alignment a type needs:

```
push<u8>, then push<u64>:

offset   0    1    2    3    4    5    6    7    8 ............ 15
        u8   ───────────── padding ─────────────  u64
```

### Running out

A full arena is an `ASSERT`, not a null pointer. If `push` could return null, every push would need a check, and a game has nothing sensible to do when memory runs out halfway through a frame. A full arena means a number is wrong, and you find out the first time it happens, in gdb, at the push that didn't fit.

### What arenas hide from ASan

AddressSanitizer knows about memory from `malloc` and on the stack, and keeps a poisoned gap around each allocation so it notices a write past the end. To ASan, an arena is one big block. Writing past the end of something you pushed lands in the next piece, or in the arena's free space, and ASan sees nothing wrong (exercise 4). That's the price of any custom allocator. momo's containers (`Array` and `Pool`, chapter 16) check their indices with `ASSERT`, which catches most of these bugs. ASan also has functions an allocator can call to tell it which bytes are in use, if momo ever needs them.

### How the game gets at the arenas

The engine owns the arenas and hands out pointers to them: `momo::frame_arena()`, in the same style as `momo::frame_index()`. Engine state lives in the engine, and nothing gets passed around ([plan §4.1](plan.md#41-three-binaries-and-hot-reload)). Two changes come with that.

**The boundary grows.** The arena code is a module of its own, `engine/arena.h` and `engine/arena.cpp`, and the game calls into it. In chapter 02, `momo.h` was the whole boundary. Now `momo.h` includes `arena.h`, so the boundary is `momo.h` plus the module headers it includes: every function marked `MOMO_API`. `MOMO_API` moves into `base.h`, since each module header needs it, and they can't include `momo.h`, which includes them.

**Typed pushes are templates.** `arena::push<Entity>(a)` returns one zeroed `Entity*`, and `arena::push_array<i32>(a, 100)` returns 100 zeroed `i32`s. The dialect only allows templates for containers and math ([plan §3.1](plan.md#31-rules)). These two only fill in `sizeof(T)` and `alignof(T)` and cast the result, so the plan now allows them too. The C way is a macro, `PUSH_STRUCT(a, Entity)`, which does the same with worse error messages and no namespace.

Can the game keep a pointer to something it pushed? Not yet. Game code has no globals ([plan §3.2](plan.md#32-rules-for-game-code-required-by-hot-reload)), and the engine doesn't pass it any state, so it has nowhere to put one. Chapter 06 gives the game its `GameState`, which lives in the permanent arena. Until then the frame arena is the useful one: memory the game uses during one update.

## The code

### The arena: `engine/arena.h`

`engine/arena.h:14`

```cpp
struct Arena {
    u8* base; // the start of the block
    i64 size; // the block's size in bytes
    i64 used; // bytes handed out since the last reset, padding included
    i64 peak; // the most `used` has ever been
};
```

Four numbers, and all zeros is a valid (empty) arena. The arena doesn't own its block. Whoever calls `arena::init` passes the block in and keeps it alive, which lets the engine split one `mmap` block three ways, and lets a test use a buffer on the stack.

`engine/arena.h:21`

```cpp
namespace arena {

MOMO_API void init(Arena* arena, void* memory, i64 size);
MOMO_API void* push_bytes(Arena* arena, i64 size, i64 align);
MOMO_API void reset(Arena* arena);

template <typename T>
T* push(Arena* arena)
{
    return (T*)push_bytes(arena, sizeof(T), alignof(T));
}

template <typename T>
T* push_array(Arena* arena, i32 count)
{
    ASSERT(count >= 0);
    return (T*)push_bytes(arena, (i64)sizeof(T) * count, alignof(T));
}

} // namespace arena
```

`push_bytes` does the work, and the two templates call it with the size and alignment of `T`. Templates have to be in the header so the compiler can write them out for each `T`. When the game calls `push_array<i32>`, that code ends up in `game.so`, and it calls `push_bytes` in `libmomo.so`. `push_array` multiplies in `i64`, so a large count can't overflow an `i32`.

### Pushing: `engine/arena.cpp`

`engine/arena.cpp:15`

```cpp
void* push_bytes(Arena* arena, i64 size, i64 align)
{
    ASSERT(arena->base); // arena::init hasn't been called
    ASSERT(size >= 0);
    ASSERT(align > 0 && (align & (align - 1)) == 0); // a power of two

    u64 address = (u64)(arena->base + arena->used);
    i64 misalignment = (i64)(address & (u64)(align - 1));
    i64 padding = misalignment == 0 ? 0 : align - misalignment;
    i64 start = arena->used + padding;

    ASSERT(start + size <= arena->size); // the arena is full

    arena->used = start + size;
    arena->peak = max(arena->peak, arena->used);

    u8* result = arena->base + start;
    memset(result, 0, size);
    return result;
}
```

Three `ASSERT`s, the alignment, one more `ASSERT`, and the push itself.

The third `ASSERT` checks that `align` is a power of two. A power of two has exactly one bit set (8 is `1000` in binary), and subtracting 1 turns that bit off and every bit below it on (7 is `0111`), so `align & (align - 1)` is 0 only for a power of two.

The alignment works on the address, not the offset. `(u64)(...)` turns the pointer into the number it holds. For a power of two, the remainder after dividing by `align` is just the number's lowest bits, and `& (align - 1)` keeps exactly those bits. Take the test `push_aligns_to_the_type`: the block starts at a multiple of 16, one `u8` has been pushed, and now it pushes a `u64`, so `align` is 8:

| | Value |
|---|---|
| `address` | the block's address + 1, so its lowest three bits are `001` |
| `misalignment` | `address & 7` = 1 |
| `padding` | 8 − 1 = 7 |
| `start` | 1 + 7 = 8 |

Why the address and not just `used`? If the block itself doesn't start at a multiple of `align`, a multiple-of-8 offset still isn't a multiple-of-8 address. `mmap` hands out page-aligned blocks, so momo's arenas would be fine either way, but working on the address makes `push_bytes` correct for any block (test `push_aligns_the_address_not_the_offset`).

Then the arena-full `ASSERT`. `used` moves to the end of the new piece, `peak` remembers the highest `used` has been (the `max` from chapter 03), and `memset` zeros the piece before it's returned.

`reset` (`engine/arena.cpp:39`) is one line: `arena->used = 0;`. `peak` survives resets on purpose, since the most a frame ever used is the number worth knowing.

### The engine's arenas: `engine/momo.cpp`

`engine/momo.cpp:15`

```cpp
struct Engine {
    // ...game_lib, game, frame_index and quit_requested, as before...

    u8* memory; // one block from mmap, split into the three arenas
    i64 memory_size;
    Arena permanent;
    Arena stage;
    Arena frame;
};

static Engine engine;

constexpr i64 PERMANENT_SIZE = 16 * MB;
constexpr i64 STAGE_SIZE = 32 * MB;
constexpr i64 FRAME_SIZE = 8 * MB;
```

The arenas live in the engine's state like everything else. `MB` is new in `base.h`, next to `KB`: plain `i64` constants, so `16 * MB` reads as what it is.

`engine/momo.cpp:41`

```cpp
static bool init_memory()
{
    i64 size = PERMANENT_SIZE + STAGE_SIZE + FRAME_SIZE;
    void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        fprintf(stderr, "momo: can't reserve %lld MB of memory\n", (long long)(size / MB));
        return false;
    }
    engine.memory = (u8*)memory;
    engine.memory_size = size;

    u8* next = engine.memory;
    arena::init(&engine.permanent, next, PERMANENT_SIZE);
    next += PERMANENT_SIZE;
    arena::init(&engine.stage, next, STAGE_SIZE);
    next += STAGE_SIZE;
    arena::init(&engine.frame, next, FRAME_SIZE);

    printf("momo: reserved %lld MB at %p\n", (long long)(size / MB), memory);
    return true;
}
```

`mmap`'s arguments, in order:

| Argument | Meaning |
|---|---|
| `nullptr` | Let the kernel pick the address |
| `size` | How many bytes. The kernel rounds up to whole pages, and 56 MB is already whole pages. |
| `PROT_READ \| PROT_WRITE` | The pages can be read and written, but not run as code |
| `MAP_PRIVATE \| MAP_ANONYMOUS` | Plain memory that belongs to this process, as above |
| `-1, 0` | The file and the offset into it. An anonymous mapping has no file. |

On failure, `mmap` returns `MAP_FAILED`, not null. Failing to get 56 MB of address space is an expected failure in the sense of [plan §3.3](plan.md#33-errors-and-strings) (the system is out of memory, which isn't a momo bug), so it prints why and returns `false`, and `run` returns 1.

The block is cut into three pieces in order: permanent first, then stage, then frame, which starts 48 MB in. `shutdown_memory` (`engine/momo.cpp:70`) prints each arena's peak and gives the block back with `munmap`.

The loop resets the frame arena at the start of every frame:

`engine/momo.cpp:116`

```cpp
i32 run()
{
    if (!init_memory())
        return 1;
    if (!load_game())
        return 1;

    engine.game.init();
    while (!engine.quit_requested) {
        arena::reset(&engine.frame); // last frame's memory is free again
        engine.game.update();
        engine.frame_index++;
    }
    engine.game.shutdown();

    printf("momo: quit after %d frames\n", engine.frame_index);
    dlclose(engine.game_lib);
    shutdown_memory();
    return 0;
}
```

Memory comes first, because the game may push from an arena in `init`. Resetting at the start of a frame, not the end, means whatever the game pushed during a frame stays valid for everything else that runs in that frame. Later chapters add more steps to the frame, and they'll all come after this reset.

The accessors at `engine/momo.cpp:150` each return a pointer to one of the arenas, `&engine.frame` and so on, and `engine/momo.h:24` declares them with `MOMO_API`.

### The game: `game/game.cpp`

`game/game.cpp:12`

```cpp
void update()
{
    i32 frame = momo::frame_index();

    // Memory that lasts until the end of this frame. The engine resets the
    // frame arena before every update, so the game never frees anything.
    i32 count = 100 * (frame + 1);
    i32* numbers = arena::push_array<i32>(momo::frame_arena(), count);
    for (i32 i = 0; i < count; i++)
        numbers[i] = i;

    printf("game: update, frame %d: %d numbers at %p, frame arena has %lld bytes used\n",
           frame, count, (void*)numbers, (long long)momo::frame_arena()->used);
    if (frame == 2)
        momo::quit();
}
```

Each frame takes a different amount of memory, fills it, and never gives it back. The numbers stand in for real work, like a list of sprites to draw.

### The tests: `tests/arena_test.cpp`

Eight tests. Each one gives its arena a small block on the stack:

`tests/arena_test.cpp:25`

```cpp
static void push_returns_zeroed_memory()
{
    alignas(16) u8 memory[64];
    memset(memory, 0xAB, sizeof(memory)); // not zero, like a reused block

    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    u8* bytes = (u8*)arena::push_bytes(&a, 64, 1);
    for (i32 i = 0; i < 64; i++)
        CHECK_EQ(bytes[i], 0);
}
```

`alignas(16)` puts the buffer at an address that's a multiple of 16, so the alignment tests can predict where every push lands. The others check that pushes come out in order, that `reset` reuses the same memory and `push` zeros it again, both kinds of alignment, `push_array`'s size, filling an arena exactly, and that `peak` survives a reset.

There's no test that a full arena `ASSERT`s. An `ASSERT` stops the whole test program, so the runner can't check for one and carry on. Run it shows it in the game instead.

### The Makefile

Nothing changed. `engine/arena.cpp` matches `engine/*.cpp`, so it's compiled into `libmomo.so` and into `libmomo.a`. `tests/arena_test.cpp` matches `tests/*_test.cpp`, so it becomes a test program. Both land in `compile_commands.json` too.

## Run it

### The game

```
$ make
$ ./build/debug/momo
momo: reserved 56 MB at 0x7b9da0a00000
momo: loaded /home/mairuu/repos/momo/build/debug/game.so
game: init
game: update, frame 0: 100 numbers at 0x7b9da3a00000, frame arena has 400 bytes used
game: update, frame 1: 200 numbers at 0x7b9da3a00000, frame arena has 800 bytes used
game: update, frame 2: 300 numbers at 0x7b9da3a00000, frame arena has 1200 bytes used
game: shutdown
momo: quit after 3 frames
momo: permanent arena: at most 0 bytes used of 16 MB
momo: stage     arena: at most 0 bytes used of 32 MB
momo: frame     arena: at most 1200 bytes used of 8 MB
```

Your addresses will differ, but the pattern won't:

- The frame arena starts 48 MB into the block: `0x7b9da0a00000` + `0x3000000` is `0x7b9da3a00000`.
- Every frame gets the same address, because each frame starts from an empty frame arena.
- `used` counts only the current frame: 400, 800, 1200 bytes, never the total.
- The peak is the biggest frame, 1200 bytes. Nothing pushed to the permanent or stage arena yet.

The release build (`make release`, `./build/release/momo`) prints the same, at different addresses.

### The tests

```
$ make test
...
arena_test
  push_hands_out_memory_in_order ... ok
  push_returns_zeroed_memory ... ok
  reset_reuses_the_memory_and_push_zeroes_it_again ... ok
  push_aligns_to_the_type ... ok
  push_aligns_the_address_not_the_offset ... ok
  push_array_takes_count_times_the_size ... ok
  push_can_fill_the_arena_exactly ... ok
  peak_remembers_the_most_ever_used ... ok
  8 passed
base_test
  ...
  5 passed
```

Chapter 03 promised that a test program would get only the engine code it uses. Here's the arena test:

```
$ nm -C build/debug/tests/arena_test | grep -E ' T (arena|momo)::'
00000000000080a2 T arena::push_bytes(Arena*, long, long)
0000000000007c50 T arena::init(Arena*, void*, long)
000000000000886c T arena::reset(Arena*)
```

`arena.o` came out of `libmomo.a`, and `momo.o`, with its `mmap` and `dlopen`, stayed behind.

### Run out of memory

Make the game greedy. At `game/game.cpp:18`, change `100` to `1000000`, so frame 0 takes 4 MB, frame 1 takes 8 MB, and frame 2 asks for 12 MB of an 8 MB arena:

```
$ make
$ ./build/debug/momo
momo: reserved 56 MB at 0x7b3da3400000
momo: loaded /home/mairuu/repos/momo/build/debug/game.so
game: init
game: update, frame 0: 1000000 numbers at 0x7b3da6400000, frame arena has 4000000 bytes used
game: update, frame 1: 2000000 numbers at 0x7b3da6400000, frame arena has 8000000 bytes used
engine/arena.cpp:29: ASSERT(start + size <= arena->size) failed
zsh: illegal hardware instruction  ./build/debug/momo
```

Frame 1 used 8,000,000 bytes, which fits in 8 MB (8,388,608). Frame 2 didn't fit. The `ASSERT` names a line in the arena, which every push shares, so ask gdb who was pushing:

```
$ gdb -q ./build/debug/momo
(gdb) run
...
engine/arena.cpp:29: ASSERT(start + size <= arena->size) failed

Program received signal SIGILL, Illegal instruction.
0x00007ffff7f8cca0 in arena::push_bytes (arena=0x7ffff7f939f8 <momo::engine+120>, size=12000000, align=4) at engine/arena.cpp:29
29	    ASSERT(start + size <= arena->size); // the arena is full
(gdb) bt
#0  0x00007ffff7f8cca0 in arena::push_bytes (arena=0x7ffff7f939f8 <momo::engine+120>, size=12000000, align=4) at engine/arena.cpp:29
#1  0x00007ffff7f185fe in arena::push_array<int> (arena=0x7ffff7f939f8 <momo::engine+120>, count=3000000) at engine/arena.h:47
#2  0x00007ffff7f182bc in game::update () at game/game.cpp:19
#3  0x00007ffff7f8da65 in momo::run () at engine/momo.cpp:129
#4  0x0000555555555182 in main () at host/main.cpp:7
(gdb) print *arena
$1 = {base = 0x7bfff3000000 "", size = 8388608, used = 0, peak = 8000000}
(gdb) print arena == momo::frame_arena()
$2 = true
(gdb) quit
```

The backtrace runs through all three binaries again: `main` in `momo`, `momo::run` and `arena::push_bytes` in `libmomo.so`, and `game::update` and the `push_array<int>` the compiler wrote for it in `game.so`. gdb even names the arena: `<momo::engine+120>` means "120 bytes into the variable `momo::engine`", which is where `engine.frame` is. `print *arena` shows the whole picture at once: an 8 MB arena, empty because this frame had only just started, with a peak of 8,000,000 from frame 1.

Put the `100` back (`git checkout game/game.cpp`) and run `make`.

### Look inside: how much RAM is 56 MB?

The engine reserved 56 MB. How much of it is in RAM? `pmap -x` lists a running process's mappings, with each one's size and how much of it is resident (`RSS`, the part in physical memory). The game quits after three frames, so stop it in gdb first. Use the release build, which has far fewer mappings than the sanitizer build:

```
$ gdb -q ./build/release/momo
(gdb) break game::shutdown
Function "game::shutdown" not defined.
Make breakpoint pending on future shared library load? (y or [n]) y
Breakpoint 1 (game::shutdown) pending.
(gdb) run
momo: reserved 56 MB at 0x7ffff4000000
momo: loaded /home/mairuu/repos/momo/build/release/game.so
game: init
game: update, frame 0: 100 numbers at 0x7ffff7000000, frame arena has 400 bytes used
game: update, frame 1: 200 numbers at 0x7ffff7000000, frame arena has 800 bytes used
game: update, frame 2: 300 numbers at 0x7ffff7000000, frame arena has 1200 bytes used

Breakpoint 1, game::shutdown () at game/game.cpp:31
31	    printf("game: shutdown\n");
(gdb) shell pmap -x $(pidof momo) | grep -E 'Address|7ffff4000000'
Address           Kbytes     RSS   Dirty Mode  Mapping
00007ffff4000000   57344    2048    2048 rw---   [ anon ]
(gdb) quit
```

`shell` runs a command from inside gdb, and `$(pidof momo)` finds the process ID. Search for the address the engine printed (gdb turns off address randomization, so under gdb it's usually the same from run to run). Out of 57,344 KB, 2,048 KB are resident.

The game touched 1200 bytes, so why 2 MB and not one 4 KB page? Linux has **transparent huge pages**: for a big mapping it can use 2 MB pages instead of 4 KB ones, which makes the CPU's address translation cheaper. On this machine it's set to `always` (`cat /sys/kernel/mm/transparent_hugepage/enabled` shows the setting), so the first touch brought in a whole 2 MB page. With it off, you'd see 4 KB. Either way, the other 54 MB cost nothing. Exercise 1 makes the game touch more.

## Recap

```
momo/
  engine/
    base.h          + KB, MB, and MOMO_API (moved from momo.h)
    arena.h         Arena, arena::init / push_bytes / reset, push<T>, push_array<T>
    arena.cpp       → libmomo.so (and libmomo.a)
    momo.h          + includes arena.h; permanent_arena, stage_arena, frame_arena
    momo.cpp        + one mmap block, three arenas, frame reset, peaks at quit
  game/
    game.cpp        + uses the frame arena
  tests/
    arena_test.cpp  → build/<config>/tests/arena_test
```

- An arena is a block and an offset. Pushing moves the offset forward, and resetting frees everything at once.
- momo has three arenas, one per lifetime: permanent, stage and frame, with fixed sizes of 16, 32 and 8 MB.
- They share one `mmap` block. Its pages are zero and cost no RAM until they're touched.
- `push` aligns the address, zeros what it returns, and `ASSERT`s if the arena is full.
- A pointer into an arena is good until that arena is reset. Never keep one in something that lives longer.
- ASan can't see inside an arena. Bounds checks in containers have to cover that.
- The boundary is now `momo.h` plus the module headers it includes, and `MOMO_API` lives in `base.h`.

The plan records the arena details in [§4.2](plan.md#42-memory), the bigger boundary in [§4.1](plan.md#41-three-binaries-and-hot-reload), and the typed-push templates in [§3.1](plan.md#31-rules).

Chapter 05 builds momo's strings, which are allocated in these arenas, and its logging.

## Exercises

Undo each change before the next exercise: `git checkout -- engine game tests` puts those back.

1. **Touch the stage arena.** In `game::init`, add `arena::push_bytes(momo::stage_arena(), 20 * MB, 16);`, then repeat the `pmap` session. What's resident now, and what in `push_bytes` touched all those pages, given that the game never wrote to them?
2. **A pointer that outlives its arena.** Write a test that pushes an `i32` from an arena, sets it to 42, keeps the pointer, resets the arena, and pushes a different `i32` set to 7. What does the old pointer read? Would ASan catch this?
3. **Forget alignment.** In `engine/arena.cpp:26`, change the padding to `i64 padding = 0;` (GCC now warns that `misalignment` is unused). Run `make test`: which tests fail, and what do they print? Then add `arena::push<u8>(momo::frame_arena());` before the `push_array` in `game::update` and run the game. What stops it?
4. **ASan's blind spot.** Write a test that pushes `push_array<i32>(&a, 4)` from a 64-byte arena and writes `numbers[4] = 1`, one past the end of what it pushed. Does anything notice? Now push 16 `i32`s, which fill the whole 64-byte block, and write `numbers[16] = 1`. What's different?
5. **Shrink the frame arena.** Set `FRAME_SIZE` to `1 * KB` (`engine/momo.cpp:34`) and run the game. Which frame fails, and why that one?
