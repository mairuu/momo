# 05 · Strings and Logging

> **Goal:** give momo text it can build and pass around, and one way to report what happens. Build strings in the frame arena without ever freeing them, cut a path into pieces without copying it, crash the game while its output goes to a file and still keep every line, and jump from any log line to the code that printed it.

## The problem

### Printing

Everything momo prints so far is a `printf` with a name typed in by hand, `"momo: "` or `"game: "`, and the errors are `fprintf(stderr, ...)`. That has three problems, and the first one appears as soon as the output goes anywhere but the terminal. Here's chapter 04's out-of-memory experiment again, on chapter 04's code, with the output saved to a file as you would to keep a record of a run:

```
$ ./build/debug/momo > run.txt
engine/arena.cpp:29: ASSERT(start + size <= arena->size) failed
zsh: illegal hardware instruction  ./build/debug/momo > run.txt
$ cat run.txt
$
```

`run.txt` is empty. The five lines that said what the game was doing before it crashed are gone. stdout is **buffered**: `printf` collects text in memory and writes it out in big pieces. To a terminal, it writes at the end of every line, so you never notice. To a file or a pipe, it waits until it has a few KB, and when the program crashes, whatever is still waiting is lost. Only the `ASSERT` line made it, to the terminal and not the file, because `ASSERT` writes to stderr, which isn't buffered.

The other two problems are smaller:

- **Which code printed a line?** `momo:` or `game:` is typed into every message, and it doesn't say which line of code printed it. Finding that code means searching for the text.
- **`(long long)` everywhere.** Every `i64` that gets printed needs a cast to match `%lld` on every platform ([chapter 03](03-test-runner.md)), as in `(long long)arena.peak`.

### Text

The engine builds exactly one string so far, the path to `game.so`:

```cpp
char exe[PATH_MAX] = {};
if (readlink("/proc/self/exe", exe, sizeof(exe) - 1) < 0) { ... }
*strrchr(exe, '/') = 0; // ".../build/debug/momo" becomes ".../build/debug"

char path[PATH_MAX + 16] = {}; // room for the folder plus "/game.so"
snprintf(path, sizeof(path), "%s/game.so", exe);
```

Those are C strings: a pointer to chars that end at the first 0. That shows in every line:

- The length isn't stored anywhere. `strlen` finds it by walking to the 0.
- Cutting a piece out means either writing a 0 into the original, which is what the `strrchr` line does, or copying the piece somewhere else.
- Every buffer's size is a guess, like `PATH_MAX + 16`. Guess too small, and `snprintf` quietly cuts the text short.
- `readlink` returns the path's length and doesn't write a 0. The code throws the length away and leaves room for a 0 instead.

momo is about to build a lot more text. The debug overlay redraws its text every frame (chapter 13), the HUD prints the lives counter, and chapter 17 reads tilemaps from text files and cuts them into lines. All of that wants strings that know their length, can be cut without copying, and come from arenas.

## Design

### A string is a pointer and a length

```cpp
struct Str {
    const char* data;
    i32 len;
};
```

| | C string | `Str` |
|---|---|---|
| Its length | `strlen` walks to the 0 | `len` |
| A piece of it | Copy it, or write a 0 into the original | A new `Str` pointing into the old one |
| Needs a 0 at the end | Always | No |
| Owns its memory | Depends on who you ask | Never |

The last row is from the plan ([§3.3](plan.md#33-errors-and-strings)). A `Str` is a view of chars that live somewhere else, usually an arena, and it's good for as long as they are, the same rule as any pointer into an arena (chapter 04). A `Str` built in the frame arena is gone at the next frame.

`len` is an `i32`: a count of chars, and counts are `i32` ([§3.4](plan.md#34-names-and-types)). It also suits `printf`. `%.*s` prints at most a given number of chars, so the string doesn't need a 0 at the end. It takes two arguments, an `int` for the number of chars and then the pointer, and an `i32` is an `int`:

```cpp
LOG_INFO("loaded %.*s", path.len, path.data);
```

`data` is `const`, since a `Str` is for reading. All zeros is a valid `Str`, the empty string (ZII).

### Where strings come from

For now, three places:

- **Literals:** `STR("game.so")`, a `Str` whose length is worked out while compiling.
- **Formatting:** `str::fmt(arena, "frame %d", frame)` formats like `printf` into memory pushed from `arena`.
- **Pieces of other strings:** `str::slice(s, start, end)` points into `s`.

C functions like `dlopen` and `fopen` want a C string, with a 0 at the end. A slice doesn't have one. A literal does, and `str::fmt` always writes one after the text. To pass a slice to a C function, format it: `str::fmt(arena, "%.*s", s.len, s.data)`.

### Formatting: `str::fmt`

Formatting is `printf`'s job, and momo won't write its own `printf`. Parsing format strings isn't a learning goal, and GCC already checks `printf` formats against their arguments. `str::fmt` hands the work to `vsnprintf`, the version of `snprintf` that takes its arguments as a `va_list`, the way a function with `...` passes its arguments on.

The question is how much memory to push before formatting:

| Option | The catch |
|---|---|
| A fixed-size buffer | A guess again. Too small cuts the text. |
| Format into the arena's free space, then push what was used | One pass, but `str::fmt` would have to work around the arena's insides |
| **Measure, push, then format** | Formats twice |

Given no buffer, `vsnprintf` writes nothing and returns the length the text would have. `str::fmt` measures, pushes exactly that many bytes plus one for the 0, then formats into them. Formatting a short string twice costs next to nothing.

The arena you pass decides how long the string lasts: the frame arena for text used during one frame, like the overlay or a path about to be opened, and the stage arena for text that has to last a stage.

The plan named it `str_fmt`. The namespace rule ([§3.4](plan.md#34-names-and-types)) makes it `str::fmt`, the same way the arena has `arena::push`. It's marked with `__attribute__((format(printf, 2, 3)))`, so GCC checks every call the way it checks `printf`: argument 2 is the format, and the values start at argument 3. [Chapter 01](01-dialect.md) promised that, and Run it shows what it catches.

### Literals: `STR`

```cpp
#define STR(literal) (Str{ .data = "" literal, .len = (i32)sizeof("" literal) - 1 })
```

A string literal is an array of chars, and `sizeof` counts them, plus the 0, while compiling. So `STR("game.so")` is `{ "game.so", 7 }`, with no `strlen` at run time.

It's a macro because a function would get a pointer, and `sizeof` a pointer is 8. The `""` in front is a guard. C++ joins string literals that are written next to each other, so `"" "game.so"` is just `"game.so"`, but `"" name`, with a variable, is a syntax error. Without the guard, `STR(name)` would compile and give a length of 7, the size of a pointer minus one (exercise 2). A template taking a reference to an array would also work, but it would accept a `char` buffer too, whose size isn't the length of the text in it.

A `STR` points into the binary that compiled it, so a `STR` in game code points into `game.so`. Use it during a frame, but never store it in game state, which outlives a reload ([§3.2](plan.md#32-rules-for-game-code-required-by-hot-reload)).

### Only the functions momo needs

There are three more: `slice`, `find_last` and `eq`. `load_game` needs the first two, and the tests need `eq`. The rest arrive when something needs them. Chapter 17, for one, splits text into lines.

- `slice(s, start, end)` doesn't include `end`, as in Python, so its length is `end - start`. A slice that reaches outside `s` is a bug, so it's an `ASSERT`.
- `find_last(s, c)` returns `-1` when `c` isn't there. That's why indices are signed (§3.4).
- `eq(a, b)` compares the chars, not the pointers. `a == b` on two `Str`s doesn't compile: C++ doesn't define `==` for structs, and operator overloading is only for math types ([§3.1](plan.md#31-rules)).

### `%lld`, again

Chapter 03 cast every `i64` to `long long` so that `%lld` was right on both Linux and Windows, where `int64_t` is `long` on one and `long long` on the other. Now every log line with a size in it has the same problem, so it's worth fixing at the root:

| Option | Looks like | The catch |
|---|---|---|
| Keep casting | `"%lld", (long long)arena.peak` | Forget one and it's wrong only on Windows. GCC on Linux can't tell, because there `%ld` is right for an `int64_t`. |
| The `<inttypes.h>` macros | `"%" PRId64, arena.peak` | Right everywhere, and hard to read: every format string is cut into pieces |
| **Make `i64` a `long long`** | `"%lld", arena.peak` | On Linux, `i64` stops being the same type as `int64_t`. That only matters for pointers to them. |

momo now defines `i64` as `long long` and `u64` as `unsigned long long`. Both are 64 bits everywhere momo could run, which a `static_assert` checks. `%lld` matches an `i64` on every platform, and GCC on Linux now warns about a `%ld`, which Run it shows. The cost: an `int64_t*` no longer converts to an `i64*`, and no function momo calls so far takes one. `i32` never had this problem, because `int32_t` is `int` everywhere and `%d` is right.

### The log

**It writes to stderr.**

| | stdout | stderr |
|---|---|---|
| Meant for | What a program produces | Messages about the program |
| Buffering | By line to a terminal, in blocks of a few KB to a file or a pipe | None |
| When the program crashes | Whatever is in the buffer is lost | Nothing is lost |

A game's output is its window. Everything it prints is about the program, so it all goes to stderr. Since `ASSERT` also writes to stderr, the log and an `ASSERT` come out in the order they happened. Writing without a buffer costs a few small writes per line, which is nothing at the rate a game logs. To keep a log, redirect stderr: `2> run.txt`.

**A line looks like a compiler message**: `file:line: message`, with `warning: ` or `error: ` before the message when it's one. `ASSERT`, failed tests and GCC all print this format already. The path tells you which binary logged the line (`engine/` or `game/`), and editors that read compiler output can jump to it. Run it does that from Neovim.

**The log is three macros** around one function:

```cpp
#define LOG_INFO(...) logger::print(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
```

`__FILE__` and `__LINE__` are replaced with the file and line where they're written. Written inside a log function, they'd always name the log function. Written in a macro, they're replaced where the macro is used, which is why `ASSERT` is a macro too ([chapter 01](01-dialect.md)). `...` and `__VA_ARGS__` let the macro take any number of arguments and pass them all on, the format included.

**Three levels**, plus `ASSERT` for comparison:

| | When | What happens next |
|---|---|---|
| `LOG_INFO` | Something worth knowing happened | The program carries on |
| `LOG_WARN` | Something odd happened, and the program can handle it | The program carries on. Chapter 06 warns when the game's state layout has changed and the game starts over. |
| `LOG_ERROR` | Something failed, and it isn't a bug: a missing file, a shader that won't compile | The function returns `false` or null ([§3.3](plan.md#33-errors-and-strings)) |
| `ASSERT` | A bug | The program stops in gdb |

**The namespace is `logger`, not `log`.** The outline called it `log::info`, but `log` is the C library's natural logarithm, and a namespace can't have the same name as a function. GCC knows `log` as a built-in function and warns about `namespace log` on its own. Once anything includes `<math.h>`, which momo's math will, it's an error:

```
ns.cpp:2:11: error: ‘namespace log { }’ redeclared as different kind of entity
    2 | namespace log {
      |           ^~~
...
/usr/include/bits/mathcalls.h:126:1: note: previous declaration ‘double log(double)’
```

**What it doesn't do yet:** timestamps, frame numbers, log files, colors. Each is easy to add when a chapter needs it. If chapter 13's overlay shows recent log lines, one rule applies: the log must copy the text it keeps, never keep the pointers it was given. A file name or a format string from game code points into `game.so`, which hot reload unloads (§3.2).

## The code

### `i64` and `u64`: `engine/base.h`

`engine/base.h:14`

```cpp
// i64 and u64 are long long rather than int64_t, which is long on Linux and
// long long on Windows. printf's %lld and %llu then match them on every
// platform, and GCC's format check catches a %ld that would only be right on
// Linux (chapter 05).
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = long long;
// ...u8 to u32 as before...
using u64 = unsigned long long;
```

Plus `static_assert(sizeof(i64) == 8 && sizeof(u64) == 8, ...)`. Every file includes `base.h`, so Make rebuilds everything, and the names in the binaries change with the types: `nm` now shows `arena::push_bytes(Arena*, long long, long long)`, where chapter 04 showed `long`.

### Strings: `engine/str.h`

`engine/str.h:15`

```cpp
struct Str {
    const char* data;
    i32 len;
};

#define STR(literal) (Str{ .data = "" literal, .len = (i32)sizeof("" literal) - 1 })

namespace str {

MOMO_API Str fmt(Arena* arena, const char* format, ...)
    __attribute__((format(printf, 2, 3)));
MOMO_API Str slice(Str s, i32 start, i32 end);
MOMO_API i32 find_last(Str s, char c);
MOMO_API bool eq(Str a, Str b);

} // namespace str
```

`str.h` includes `arena.h`, because `fmt` takes an `Arena*`. The header's comments say what each function does, and the Design section above says why.

### Formatting: `engine/str.cpp`

`engine/str.cpp:9`

```cpp
Str fmt(Arena* arena, const char* format, ...)
{
    va_list args;
    va_start(args, format);

    va_list measure_args;
    va_copy(measure_args, args);
    i32 len = vsnprintf(nullptr, 0, format, measure_args);
    va_end(measure_args);
    ASSERT(len >= 0); // only a broken format fails

    char* data = arena::push_array<char>(arena, len + 1);
    vsnprintf(data, len + 1, format, args);
    va_end(args);

    return { .data = data, .len = len };
}
```

A function with `...` reads its extra arguments through a `va_list`, from `<stdarg.h>`:

| | What it does |
|---|---|
| `va_start(args, format)` | Points `args` at the first argument after `format` |
| `va_copy(copy, args)` | Makes a second `va_list` at the same place |
| `va_end(args)` | Finishes with one. Every `va_start` and `va_copy` needs one. |

`vsnprintf` reads the arguments out of the `va_list` it's given, and after that the C standard says the `va_list` can't be used again. That's why the measuring gets a copy and the formatting gets the original. Get this wrong and the result is undefined behavior that the sanitizers don't see, which exercise 3 tries.

`vsnprintf`'s size counts the 0, so the second call gets `len + 1`. With only `len`, it would drop the last char to make room for the 0. `push_array<char>` pushes with an alignment of 1, so strings from the same arena sit back to back with no padding between them.

`engine/str.cpp:46`

```cpp
bool eq(Str a, Str b)
{
    if (a.len != b.len)
        return false;
    if (a.len == 0)
        return true;
    return memcmp(a.data, b.data, a.len) == 0;
}
```

The middle check isn't only a shortcut. An all-zero `Str` has a null `data`, and `memcmp` must never be given a null pointer, even with a length of 0. glibc's header marks its arguments as never null, and UBSan stops the program when one is (exercise 1).

`slice` (`engine/str.cpp:31`) checks its range and returns `{ s.data + start, end - start }`. `find_last` (`engine/str.cpp:37`) walks backwards from the end.

### The log: `engine/logger.h` and `engine/logger.cpp`

`engine/logger.h:15`

```cpp
#define LOG_INFO(...)  logger::print(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  logger::print(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) logger::print(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

enum LogLevel : u8 {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
};

namespace logger {

MOMO_API void print(LogLevel level, const char* file, i32 line, const char* format, ...)
    __attribute__((format(printf, 4, 5)));

} // namespace logger
```

The enum's values are `LOG_LEVEL_*`, prefixed with the type's name as §3.4 says, which also keeps them apart from the macros. `print` is marked as a `printf` function too: its format is argument 4.

`engine/logger.cpp:11`

```cpp
static const char* const LEVEL_LABELS[] = {
    "",          // LOG_LEVEL_INFO
    "warning: ", // LOG_LEVEL_WARN
    "error: ",   // LOG_LEVEL_ERROR
};

void print(LogLevel level, const char* file, i32 line, const char* format, ...)
{
    ASSERT(level < ARRAY_COUNT(LEVEL_LABELS));

    fprintf(stderr, "%s:%d: %s", file, line, LEVEL_LABELS[level]);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}
```

The label comes from a table indexed by the enum, which is what plain enums are for ([§3.4](plan.md#34-names-and-types)). The `ASSERT` catches a level with no label. The line is written in three pieces, prefix, message and newline, and the log adds the newline, so messages don't end with `\n`.

### The engine: `engine/momo.cpp`

`load_game` builds its path with `Str`:

`engine/momo.cpp:84`

```cpp
    char exe_buffer[PATH_MAX] = {};
    i32 exe_len = (i32)readlink("/proc/self/exe", exe_buffer, sizeof(exe_buffer));
    if (exe_len < 0) {
        LOG_ERROR("can't find the executable's path");
        return false;
    }
    Str exe = { .data = exe_buffer, .len = exe_len };

    // ".../build/debug/momo" -> ".../build/debug" -> ".../build/debug/game.so"
    Str folder = str::slice(exe, 0, str::find_last(exe, '/'));
    Str path = str::fmt(&engine.frame, "%.*s/game.so", folder.len, folder.data);

    engine.game_lib = dlopen(path.data, RTLD_NOW);
```

`readlink` returns a length and no 0, and that's exactly a `Str`, so the length is kept this time. `folder` is a slice: the same chars as `exe`, with a shorter length, and nothing written into the buffer. `find_last` always finds a `/`, because `/proc/self/exe` is an absolute path. `path` is pushed from the frame arena, since it's only needed until `dlopen` returns and the log line is written. The first frame's reset frees it. `str::fmt` wrote a 0 after it, so `dlopen` can take `path.data`.

Every other `printf` and `fprintf` is now a `LOG_INFO` or a `LOG_ERROR`, without the `momo:` and without the casts:

```cpp
printf("momo: reserved %lld MB at %p\n", (long long)(size / MB), memory); // chapter 04
LOG_INFO("reserved %lld MB at %p", size / MB, memory);                    // now
```

`print_peak` is now `log_peak`. `engine/momo.h` includes `logger.h` and `str.h`, so the game gets both by including `momo.h`.

### The game: `game/game.cpp`

`game/game.cpp:12`

```cpp
void update()
{
    i32 frame = momo::frame_index();
    Arena* frame_arena = momo::frame_arena();

    Str text = str::fmt(frame_arena, "frame %d: %d bats", frame, 50 * frame);

    LOG_INFO("\"%.*s\" is %d chars at %p, frame arena has %lld bytes used",
             text.len, text.data, text.len, (void*)text.data, frame_arena->used);
    if (frame == 2)
        momo::quit();
}
```

Each frame builds a string of a different length in the frame arena and never frees it, like the debug overlay will. `init` and `shutdown` log too.

### The tests: `tests/str_test.cpp`

Ten tests, plus two checks GCC does while compiling:

`tests/str_test.cpp:7`

```cpp
static_assert(STR("game.so").len == 7);
static_assert(STR("").len == 0);
```

The `fmt` tests give their arena a buffer on the stack, like chapter 04's tests:

`tests/str_test.cpp:48`

```cpp
static void fmt_prints_a_str_with_precision_star()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    Str folder = str::slice(STR("build/debug/momo"), 0, 11);
    Str path = str::fmt(&a, "%.*s/game.so", folder.len, folder.data);
    CHECK(str::eq(path, STR("build/debug/game.so")));
}
```

That's what `load_game` does, on a path the test can predict. The others check that an all-zero `Str` equals `STR("")`, that `fmt` pushes exactly `len + 1` bytes and packs strings back to back, that `%lld` takes an `i64`, that slices point into the original and can be empty, that `find_last` returns `-1` and looks only inside its slice, and that `eq` compares chars, not addresses.

The log has no test. It writes to stderr, which a test can't easily read back, so Run it checks it instead.

`CHECK_EQ` in `tests/test.h` loses its two `(long long)` casts.

### The Makefile

Nothing changed. The new files match `engine/*.cpp` and `tests/*_test.cpp`.

## Run it

### The game

```
$ make
$ ./build/debug/momo
engine/momo.cpp:59: reserved 56 MB at 0x7b79cf400000
engine/momo.cpp:113: loaded /home/mairuu/repos/momo/build/debug/game.so
game/game.cpp:9: init
game/game.cpp:22: "frame 0: 0 bats" is 15 chars at 0x7b79d2400000, frame arena has 16 bytes used
game/game.cpp:22: "frame 1: 50 bats" is 16 chars at 0x7b79d2400000, frame arena has 17 bytes used
game/game.cpp:22: "frame 2: 100 bats" is 17 chars at 0x7b79d2400000, frame arena has 18 bytes used
game/game.cpp:30: shutdown
engine/momo.cpp:135: quit after 3 frames
engine/momo.cpp:65: permanent arena: at most 0 bytes used of 16 MB
engine/momo.cpp:65: stage     arena: at most 0 bytes used of 32 MB
engine/momo.cpp:65: frame     arena: at most 44 bytes used of 8 MB
```

- Every line starts with the code that printed it, and the folder says which binary: `engine/` is `libmomo.so`, `game/` is `game.so`.
- Each frame's string is at the start of the frame arena, 48 MB into the block, as in chapter 04.
- The arena holds the text plus its 0: 15 chars and 16 bytes, 16 and 17, 17 and 18.
- The frame arena's peak is 44 bytes, more than any frame used. That's `path`: 43 chars and a 0, pushed by `load_game` before the first frame.

### A crash, with the output in a file

The problem section lost five lines. Try the same with the log. Add an `ASSERT` that fails on frame 2, right after the first line of `game::update` (`game/game.cpp:14`):

```cpp
    i32 frame = momo::frame_index();
    ASSERT(frame != 2);
```

Then run the game with stderr going to a file:

```
$ make
$ ./build/debug/momo 2> run.txt
zsh: illegal hardware instruction  ./build/debug/momo 2> run.txt
$ cat run.txt
engine/momo.cpp:59: reserved 56 MB at 0x7bfb29200000
engine/momo.cpp:113: loaded /home/mairuu/repos/momo/build/debug/game.so
game/game.cpp:9: init
game/game.cpp:23: "frame 0: 0 bats" is 15 chars at 0x7bfb2c200000, frame arena has 16 bytes used
game/game.cpp:23: "frame 1: 50 bats" is 16 chars at 0x7bfb2c200000, frame arena has 17 bytes used
game/game.cpp:15: ASSERT(frame != 2) failed
```

Every line is there, with the `ASSERT` last, in the order it all happened. (`game.cpp:22` became `:23`, because the `ASSERT` pushed it down a line.)

Now open the file in Neovim as a list of places to jump to:

```
:cfile run.txt
:copen
```

`:cfile` reads a file of compiler messages into the quickfix list and jumps to the first one. Every log line counts as one, because it's in the same format. `:copen` shows the list; press Enter on a line to jump to the code that logged it, or use `:cnext` and `:cprev`.

Take the `ASSERT` out again with `git checkout game/game.cpp`, and run `make`.

### An expected failure

Hide `game.so` and run the game:

```
$ mv build/debug/game.so build/debug/game.so.off
$ ./build/debug/momo
engine/momo.cpp:59: reserved 56 MB at 0x7b076a600000
engine/momo.cpp:101: error: /home/mairuu/repos/momo/build/debug/game.so: cannot open shared object file: No such file or directory
$ echo $?
1
$ mv build/debug/game.so.off build/debug/game.so
```

A missing file isn't a bug, so it's a `LOG_ERROR`, `load_game` returns `false`, and `run` returns 1. The message after `error:` is `dlerror()`'s own.

### What the format check catches

At `game/game.cpp:22`, make two mistakes: print `text` itself with `%s`, and the arena's `used` with `%ld`:

```cpp
    LOG_INFO("\"%s\" is %d chars at %p, frame arena has %ld bytes used",
             text, text.len, (void*)text.data, frame_arena->used);
```

```
$ make
game/game.cpp:22:14: warning: format ‘%s’ expects argument of type ‘char*’, but argument 5 has type ‘Str’ [-Wformat=]
...
game/game.cpp:22:14: warning: format ‘%ld’ expects argument of type ‘long int’, but argument 8 has type ‘i64’ {aka ‘long long int’} [-Wformat=]
...
game/game.cpp:22:59: note: format string is defined here
   22 |     LOG_INFO("\"%s\" is %d chars at %p, frame arena has %ld bytes used",
      |                                                         ~~^
      |                                                           |
      |                                                           long int
      |                                                         %lld
```

The first warning is the mistake every `Str` invites: a `Str` isn't a `char*`, so it needs `%.*s` and two arguments. The second is the `i64` change at work. On Linux a `long` and a `long long` are the same size, and GCC still says no, so `%ld` can't sneak in here and break on Windows. GCC even suggests the fix. Put the line back with `git checkout game/game.cpp`, and run `make`.

### The tests

```
$ make test
...
str_test
  zero_str_is_the_empty_string ... ok
  fmt_formats_into_the_arena ... ok
  fmt_packs_strings_back_to_back ... ok
  fmt_prints_a_str_with_precision_star ... ok
  fmt_formats_64_bit_numbers_with_lld ... ok
  slice_points_into_the_original ... ok
  slice_can_take_nothing_or_everything ... ok
  find_last_finds_the_last_one ... ok
  find_last_looks_only_inside_the_str ... ok
  eq_compares_chars_not_pointers ... ok
  10 passed
```

`arena_test` and `base_test` pass as before. As in chapter 04, the test program got only the engine code it uses:

```
$ nm -C build/debug/tests/str_test | grep -E ' T (arena|str|logger|momo)::'
000000000000a5b9 T str::eq(Str, Str)
0000000000009d46 T str::fmt(Arena*, char const*, ...)
000000000000a0bb T str::slice(Str, int, int)
000000000000a355 T str::find_last(Str, char)
0000000000009490 T arena::push_bytes(Arena*, long long, long long)
000000000000903e T arena::init(Arena*, void*, long long)
0000000000009c5a T arena::reset(Arena*)
```

`str.o` and the `arena.o` it pushes from came out of `libmomo.a`. `logger.o` and `momo.o` stayed behind. And there's the `long long`.

### Look inside: a slice in gdb

gdb knows a `Str` only as a struct with a `char*` in it, and it prints a `char*` the C way, up to the next 0. Stop in `load_game` and look at `folder`. `start` runs the program to the top of `main`, by which time `libmomo.so` is loaded, so gdb can set a breakpoint in it without asking:

```
$ gdb -q ./build/debug/momo
(gdb) start
Temporary breakpoint 1 at 0x117d: file host/main.cpp, line 7.
Starting program: /home/mairuu/repos/momo/build/debug/momo
...
Temporary breakpoint 1, main () at host/main.cpp:7
7	    return momo::run();
(gdb) break engine/momo.cpp:99
Breakpoint 2 at 0x7ffff7f8bbd2: file engine/momo.cpp, line 99.
(gdb) continue
Continuing.
engine/momo.cpp:59: reserved 56 MB at 0x7bfff0000000

Breakpoint 2, momo::load_game () at engine/momo.cpp:99
99	    engine.game_lib = dlopen(path.data, RTLD_NOW);
(gdb) print exe
$1 = {data = 0x7bfff4bf00c0 "/home/mairuu/repos/momo/build/debug/momo", len = 40}
(gdb) print folder
$2 = {data = 0x7bfff4bf00c0 "/home/mairuu/repos/momo/build/debug/momo", len = 35}
(gdb) print *folder.data@folder.len
$3 = "/home/mairuu/repos/momo/build/debug"
(gdb) print path
$4 = {data = 0x7bfff3000000 "/home/mairuu/repos/momo/build/debug/game.so", len = 43}
(gdb) print path.data[path.len]
$5 = 0 '\000'
(gdb) quit
```

`folder` and `exe` have the same `data`, so gdb prints the same text for both, past the end of `folder`. Only `len` differs. `*p@n` is gdb for "the n things starting at `*p`", and `*folder.data@folder.len` shows exactly the slice. `path` is in the frame arena, at the address the game's strings will use too, with the 0 that `str::fmt` wrote after it.

Quit at the breakpoint, as above. In the debug build, letting the program run to the end inside gdb makes LeakSanitizer, which checks for leaks at exit, fail with "LeakSanitizer does not work under ptrace".

## Recap

```
momo/
  engine/
    base.h          + i64 and u64 are long long
    str.h           Str, STR, str::fmt / slice / find_last / eq
    str.cpp         → libmomo.so (and libmomo.a)
    logger.h        LOG_INFO / LOG_WARN / LOG_ERROR, LogLevel, logger::print
    logger.cpp      → libmomo.so (and libmomo.a)
    momo.h          + includes logger.h and str.h
    momo.cpp        + logs instead of printing; builds game.so's path with Str
  game/
    game.cpp        + builds a string in the frame arena every frame, and logs
  tests/
    str_test.cpp    → build/<config>/tests/str_test
    test.h          + CHECK_EQ without casts
```

- A `Str` is a pointer and an `i32` length. It doesn't own its memory, and it's good for as long as that memory is. All zeros is the empty string.
- `str::fmt` measures, pushes `len + 1` bytes from the arena you give it, and formats. Its result ends with a 0, so C functions can take it. A slice doesn't.
- Print a `Str` with `"%.*s", s.len, s.data`.
- `STR("...")` measures a literal while compiling, and refuses anything that isn't one.
- `i64` is `long long`, so `%lld` is right on every platform, and GCC checks the format of every `str::fmt` and `LOG_*` call.
- The log writes `file:line: message` lines to stderr: unbuffered, in order with `ASSERT`, and readable by editors as a jump list.
- `LOG_ERROR` is for expected failures, and `ASSERT` is for bugs.

The plan records the string details in [§3.3](plan.md#33-errors-and-strings), along with the log, and the `i64` change in [§3.4](plan.md#34-names-and-types).

Chapter 06 makes `game.so` reload while the game runs, gives the game its `GameState`, and logs the first `LOG_WARN`.

## Exercises

Undo each change before the next exercise: `git checkout -- engine game tests` puts those back.

1. **`memcmp` and null.** Delete the `if (a.len == 0) return true;` in `str::eq` (`engine/str.cpp:52`) and run `make test`. Which test stops, and what does UBSan say? `str::slice(Str{}, 0, 0)` computes `nullptr + 0` and doesn't stop anything. Why is that allowed when `memcmp` isn't?
2. **The guard in `STR`.** In `game::update`, add `const char* name = "bats";` and `Str s = STR(name);`. What does GCC say? Now delete the `""` from both places in the macro (`engine/str.h:23`), build again, and log `s.len`. Why that number?
3. **One `va_list`, read twice.** In `str::fmt`, delete the `va_copy` and its `va_end`, and pass `args` to both `vsnprintf` calls. Run `make test`, then the game. Which tests fail, and do the same ones fail if you change a test and run again? Why does no sanitizer report anything?
4. **Colors.** GCC prints `warning:` in magenta and `error:` in red when stderr is a terminal. Do the same in `logger::print`. `isatty(STDERR_FILENO)` from `<unistd.h>` says whether it's a terminal, `"\x1b[35m"` starts magenta, `"\x1b[31m"` red, and `"\x1b[0m"` goes back to normal. Check that `2> run.txt` still gets no color codes.
5. **`str::find`.** Write `str::find(Str s, char c)`, which returns the index of the first `c` or `-1`, with tests: an empty `Str`, a `c` that isn't there, and a slice whose next char in memory is `c`.
