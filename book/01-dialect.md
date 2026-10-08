# 01 · The Dialect

> **Goal:** learn the small subset of C++ that momo is written in, write `engine/base.h`, the header every momo file starts from, and watch an `ASSERT` stop a program in gdb on the exact line that failed.

## The problem

Every line in this book will be written in one style of C++, so we have to pick that style before we write any code. C++ supports very different styles, and the one we pick decides how hard the code is to read.

Here's one way to update every enemy in a level:

```cpp
for (auto& enemy : level.enemies())
    enemy->update(dt);
```

It's short, and it hides a lot. What type is `enemy`? Does `enemies()` return the list, or a copy of it? Which `update` runs: `Enemy::update`, or an override in one of its subclasses? Can it throw an exception, and if it does, where does execution go next? Does anything here allocate memory? Every one of those questions has an answer, and every answer is in another file.

Here's the same loop in momo:

```cpp
for (i32 i = 0; i < level->entity_count; i++) {
    Entity* e = &level->entities[i];
    entity::update(e);
}
```

`entity::update` is a `switch` on `e->kind` that you can read from top to bottom. The loop is longer, and there's nothing in it you can't see.

momo's dialect comes from one test, applied to every C++ feature: **can I tell what this line does by reading only this line?** ([plan §3](plan.md#3-language-orthodox-c20-built-with-gcc))

The first version isn't wrong. Most large C++ codebases look like it, Hazel included. It hides details on purpose, so a big team can work on one part without knowing every other part. In a learning engine, those details are what you came to learn.

## Design

There were three styles to choose from:

| Style | Looks like | Gives you | Costs you |
|---|---|---|---|
| Modern C++ | classes, `virtual`, RAII, the STL, exceptions (Hazel, most tutorials) | The compiler writes cleanup and copying for you | Behavior hides in constructors, destructors, overloads, virtual calls and exceptions |
| C | `pos = v2_add(pos, v2_scale(vel, 2));` | Nothing is hidden | No typed containers (macros or `void*` instead), no math operators, long prefixed names |
| **Orthodox C++** | C, plus the few C++ features that don't hide anything | As visible as C, with typed containers and readable math | You write cleanup and containers yourself |

We use **Orthodox C++**. The name comes from a short [write-up](https://gist.github.com/bkaradzic/2e39896bc7d8c34e042b) by Branimir Karadžić, the author of the bgfx renderer. It describes C-like C++ without exceptions, RTTI, the STL, iostreams or metaprogramming. It also suggests waiting about five years before using a new C++ standard's features. C++20 is six years old now, and momo uses it because its designated initializers fit our zero-initialization rule exactly (below).

The rules fall into four groups. Each group removes one kind of question that you'd otherwise answer by reading some other file.

### 1. Data is only data

**Structs have public fields, and behavior lives in free functions**, grouped in a namespace named after the module. Here is a sprite batch both ways. The momo version is roughly what chapter 10 builds:

```cpp
// Modern C++
class SpriteBatch {
public:
    explicit SpriteBatch(int capacity);
    ~SpriteBatch();
    void draw(const Sprite& sprite, glm::vec2 pos);
private:
    std::vector<Vertex> vertices_;
};

// momo
struct SpriteBatch {
    Vertex* vertices;
    i32 count;
    i32 capacity;
};

namespace sprite_batch {
void init(SpriteBatch* batch, Arena* arena, i32 capacity);
void draw(SpriteBatch* batch, Sprite sprite, V2 pos);
}
```

The struct only tells you what the data is, and the namespace lists everything you can do with it. A function that works on two types doesn't have to pick one of them as its owner. Is a player hitting a wall `Player::collide(Tilemap&)` or `Tilemap::collide(Player&)`? In momo it's a plain function that takes both.

**Zero means default (ZII, "zero is initialization").** Every struct is valid when every byte in it is zero. Pick field names so that zero is the natural starting value:

```cpp
struct Entity {
    EntityKind kind;
    i32 x, y;
    i32 hp;
    bool facing_left;   // not facing_right: everything starts out facing right
};
```

Then create a struct by naming only the fields that aren't zero:

```cpp
Entity bat = { .kind = ENTITY_BAT, .x = 128, .y = 48, .hp = 1 };   // facing_left is false
Entity e = {};                                                     // every field zero
Entity oops;                                                       // garbage
```

Those are C++20 designated initializers. Fields you leave out are zero, and the fields you name must be in the same order as in the struct. The last line is a trap C++ inherited from C: a local struct without an initializer holds whatever was on the stack. In momo, every local struct starts with `= {}` or a designated initializer.

ZII rules out C++ default member values, like `i32 hp = 28;` inside the struct. They look harmless, but they're applied only when the compiler constructs the object. Most momo objects come from an arena (chapter 04): a block of zeroed memory that no constructor ever runs on. With a default value, an `Entity` would start with 28 hp on the stack and 0 hp in an arena. With ZII, both start at 0, and the code that creates an entity sets its hp where you can see it.

**Constructors and destructors do no work. No RAII.** Setup and teardown are explicit calls:

```cpp
// RAII: the window is destroyed at the closing brace, and no line says so
{
    Window window("momo", 1024, 960);
    run_game(&window);
}

// momo: every step is a line you can see and step through in gdb
platform::init("momo", 1024, 960);
run_game();
platform::shutdown();
```

RAII makes cleanup automatic, so you can't forget it. momo gets by without it for two reasons. First, almost nothing is freed one object at a time, because arenas free a whole group at once (chapter 04). Second, the few things that need real cleanup (the window, the GL context, the audio device) each have one `init` and one `shutdown`, called from one place in the engine. And if some memory never gets freed anyway, the debug build's leak checker lists it when the program exits.

### 2. Control flow is visible

**No exceptions.** With exceptions, any function call might jump out of the current function, and where it lands is decided at runtime. We compile with `-fno-exceptions`, which makes `throw` a compile error. Errors come in two kinds instead ([plan §3.3](plan.md#33-errors-and-strings)):

- A **bug**, such as a broken invariant or an index out of range, is an `ASSERT`. It stops the program right where the bug is. This chapter builds it.
- An **expected failure**, such as a missing file or a shader that doesn't compile, returns `false` or `nullptr` and logs why. The caller decides what to do next.

**No inheritance, no `virtual`.** A virtual call like `enemy->update()` could run any one of the overrides. To know which, you have to know the object's type at runtime. momo uses a `kind` enum and a `switch`:

```cpp
void update(Entity* e)
{
    switch (e->kind) {
    case ENTITY_PLAYER: player_update(e); break;
    case ENTITY_BAT:    bat_update(e);    break;
    case ENTITY_SLIME:  slime_update(e);  break;
    }
}
```

Every kind is in one list. If you add `ENTITY_SKELETON` and forget its `case`, `-Wall` warns you. Hot reload needs this as well: an object with virtual functions holds a hidden pointer into the code that built it, and hot reload unloads that code (chapter 06). RTTI goes too (`-fno-rtti`). Its only uses are `dynamic_cast` and `typeid`, and those only matter when there's inheritance.

**Operators are overloaded only for math.** On a vector type, `pos += vel` means exactly what it says. With `std::cout << x`, or an object you call like a function with `()`, you have to look up what the operator means this time.

**If a function changes something, you pass a pointer to it:**

```cpp
entity::update(&player);   // the & says player may change
entity::print(player);     // no &, so print only reads player
```

A C++ reference would let `update(player)` change `player` with nothing at the call to show it. So momo passes a pointer for anything that changes, and a value or a `const` reference for anything that's only read.

**No capturing lambdas, and `auto` only when the right-hand side names the type.** `auto* e = (Entity*)ptr;` is fine. `auto x = compute();` hides the type. A capturing lambda hides state: the variables it captured live inside an object whose type you can't even write down.

### 3. Memory is explicit

**No `new`/`delete` in game code, no smart pointers, no STL containers.** Here's the classic bug STL containers invite in a game:

```cpp
std::vector<Entity> entities = load_level();
Entity* player = &entities[0];
entities.push_back(bat);    // may move every entity into a new, bigger allocation
player->hp -= 1;            // player may now point at freed memory
```

`push_back` doesn't look like it frees memory, but sometimes it does. Copies hide as well: `std::vector<Entity> copy = entities;` allocates and copies every element. momo's containers (`Array<T>`, `Pool<T>`) get a fixed capacity when they're created, and when they're full they `ASSERT` instead of growing. Memory comes from arenas, grouped by how long it lives (chapter 04).

**No `<iostream>`.** We use `printf` and the rest of `<stdio.h>`. They compile faster, and GCC checks format strings against their arguments (`-Wformat`, part of `-Wall`). Chapter 05 gives our own `str_fmt` the same check.

### 4. Types say what they are

**Fixed-width types:** `i8` to `i64`, `u8` to `u64`, `f32` and `f64`. An `int` or a `long` can be a different size on another platform. An `i32` is always 32 bits.

**Numbers you do arithmetic on are signed.** Counts and indices are `i32`, and byte sizes are `i64`. Unsigned types are only for bits, flags and formats defined outside momo, like pixels, files and GPU data. Here's why:

```cpp
u32 hp = 2, damage = 3;
if (hp - damage <= 0)   // 2 - 3 wraps around to 4294967295, so this is false
    die();              // the player lives on with four billion hp
```

With `i32`, the test is `-1 <= 0`, which is true. Indices go wrong the same way: `count - 1` on an empty list is `-1`, which an `ASSERT(index >= 0)` catches and gdb shows as obviously wrong. As a `u32` it would be 4294967295.

**Plain enums with a fixed size and a prefix:**

```cpp
enum EntityKind : u8 { ENTITY_PLAYER, ENTITY_BAT, ENTITY_SLIME };

constexpr const char* KIND_NAMES[] = { "player", "bat", "slime" };

printf("%s\n", KIND_NAMES[e->kind]);   // a plain enum indexes a table, no cast
```

An `enum class` would need `KIND_NAMES[(int)e->kind]` every time. The `ENTITY_` prefix keeps the names from clashing, which is what `enum class` scoping is for, and `: u8` makes the enum one byte inside a struct.

**Templates only for containers and math.** `Array<Entity>` and `min(a, b)` are templates. Templates that compute types at compile time (`std::enable_if_t<...>` and friends) aren't allowed, because you can't tell what they do by reading them.

**C-style casts.** `(i32)x` is short, and the type you get is right there on the line.

**Names.** Types are `PascalCase`. Functions and variables are `snake_case`. Macros, enum values and `constexpr` constants are `UPPER_SNAKE`. Each module is a `.h`/`.cpp` pair with `#pragma once` and one namespace.

### What we keep from C++

The features that pass the test:

| Feature | What it gives us |
|---|---|
| Templates for containers | `Array<Entity>` instead of macros or `void*` |
| Operator overloading for math | `pos += vel * 2` instead of `v2_add(pos, v2_scale(vel, 2))` |
| Namespaces | `renderer::init`, and inside the module, plain `init` |
| `constexpr` | Constants and tables that the compiler checks, instead of `#define` |
| Enums with a fixed size | `enum EntityKind : u8` |
| Designated initializers (C++20) | ZII in code: name only the fields that aren't zero |
| `const` references | Pass a big struct without copying it when you only read it |
| `bool`, `nullptr`, `static_assert` | Small improvements over C, with nothing hidden |

### Rules for later

Code in `game.so` follows six more rules, because hot reload swaps out its code while its data stays in memory. One example: no global variables, because they quietly reset on every reload. You don't need these rules until chapter 06, which explains them when hot reload arrives. They're listed in [plan §3.2](plan.md#32-rules-for-game-code-required-by-hot-reload).

### What enforces the rules

The compiler enforces a few of them. `-fno-exceptions` and `-fno-rtti` turn `throw` and `typeid` into errors, and `-Wall` catches a `switch` that's missing a `case`. Everything else is a convention, and the only check is someone reading the code.

## The code: `engine/base.h`

Every file in momo includes `base.h`. It's small on purpose: the types, `ASSERT` and `ARRAY_COUNT`. Later chapters add to it only when they need something everywhere.

### Types

`engine/base.h:13`

```cpp
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
// ...u8 to u64 the same way...
using f32 = float;
using f64 = double;

static_assert(sizeof(f32) == 4 && sizeof(f64) == 8, "f32 and f64 must be 32 and 64 bits");
static_assert(sizeof(void*) == 8, "momo assumes a 64-bit platform");
```

`using` reads left to right: "`i32` is `int32_t`". The `static_assert`s write down assumptions the rest of the code makes. If a future port breaks one, the build stops on that line with that message.

### ASSERT

C already has an assert. Here's what it looks like in gdb when `assert(index < count)` fails:

```
Program received signal SIGABRT, Aborted.
(gdb) bt
#0  0x00007ffff689a11c in ?? () from /usr/lib/libc.so.6
#1  0x00007ffff683e5d0 in raise () from /usr/lib/libc.so.6
#2  0x00007ffff6825685 in abort () from /usr/lib/libc.so.6
#3  0x00007ffff6826704 in ?? () from /usr/lib/libc.so.6
#4  0x00007ffff68361f2 in __assert_fail () from /usr/lib/libc.so.6
#5  0x000055555555730e in entity::get (entities=..., count=3, index=3) at dialect.cpp:27
```

The program stopped five calls deep inside the C library, and you'd have to type `frame 5` before you could look at anything. `assert` also does nothing at all when `NDEBUG` is defined, which many release builds do.

Here's momo's version:

`engine/base.h:37`

```cpp
#define ASSERT(cond)                                                          \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "%s:%d: ASSERT(%s) failed\n",                     \
                    __FILE__, __LINE__, #cond);                               \
            __builtin_trap();                                                 \
        }                                                                     \
    } while (0)
```

Line by line:

- `do { ... } while (0)` makes the macro a single statement, so `if (x) ASSERT(y); else ...` still means what it looks like.
- `#cond` turns the condition into a string exactly as you wrote it, so `ASSERT(index < count)` prints `index < count`.
- `file:line:` is the format compiler errors use, so editors and terminals that can jump to a compiler error can jump to a failed assert too.
- `__builtin_trap()` compiles to one invalid instruction (`ud2` on x86-64). The CPU refuses to run it, the kernel sends the program `SIGILL`, and gdb stops right on that instruction. A macro is pasted in where you use it, so that instruction sits inside the function that failed, not in some helper.

`ASSERT` is on in every build, including `make release` ([plan §7](plan.md#7-build-dependencies-repo)). A check costs one comparison and one branch. A bug that gets past it in a release build costs far more.

### ARRAY_COUNT

`engine/base.h:50`

```cpp
#define ARRAY_COUNT(a) ((i32)(sizeof(a) / sizeof((a)[0])))
```

The classic C idiom: the size of the whole array divided by the size of one element, cast to `i32` because counts are signed. Its weak spot is a pointer. Pass a pointer instead of an array and `sizeof(a)` is the pointer's 8 bytes, so the result is nonsense. GCC catches exactly that mistake with `-Wsizeof-pointer-div`, which is part of `-Wall` (exercise 3).

## Run it

There are no momo binaries until chapter 02, so we test `base.h` with a throwaway program, the same way chapter 00 checked the toolchain. It's about 60 lines and uses most of the rules above. From the repo root:

```sh
mkdir -p build/ch01
cat > build/ch01/dialect.cpp <<'EOF'
// A throwaway program that exercises base.h. Chapter 02 replaces it with
// momo's real binaries.
#include "base.h"

#include <stdlib.h>

enum EntityKind : u8 {
    ENTITY_PLAYER,
    ENTITY_BAT,
    ENTITY_SLIME,
};

constexpr const char* KIND_NAMES[] = { "player", "bat", "slime" };

struct Entity {
    EntityKind kind;
    i32 x, y;
    i32 hp;
    bool facing_left; // zero means facing right, how everything starts
};

namespace entity {

Entity* get(Entity* entities, i32 count, i32 index)
{
    ASSERT(index >= 0);
    ASSERT(index < count);
    return &entities[index];
}

void update(Entity* e)
{
    switch (e->kind) {
    case ENTITY_PLAYER: e->x += 2; break;
    case ENTITY_BAT:    e->y += 1; break;
    case ENTITY_SLIME:  e->x -= 1; e->facing_left = true; break;
    }
}

void print(const Entity& e)
{
    printf("%-6s x=%3d y=%3d hp=%2d %s\n", KIND_NAMES[e.kind], e.x, e.y, e.hp,
           e.facing_left ? "facing left" : "facing right");
}

} // namespace entity

int main(int argc, char** argv)
{
    Entity entities[] = {
        { .kind = ENTITY_PLAYER, .x = 24,  .y = 192, .hp = 28 },
        { .kind = ENTITY_BAT,    .x = 128, .y = 48,  .hp = 1 },
        { .kind = ENTITY_SLIME,  .x = 200, .y = 192 },
    };

    i32 index = argc > 1 ? atoi(argv[1]) : 0;
    Entity* e = entity::get(entities, ARRAY_COUNT(entities), index);

    entity::print(*e);
    entity::update(e);
    entity::print(*e);
    return 0;
}
EOF
```

Compile it with the flags the debug build will use: no optimization, full debug info, and the address and undefined-behavior sanitizers. `-Iengine` lets `#include "base.h"` find the header.

```sh
g++ -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti -O0 -g3 \
    -fsanitize=address,undefined -Iengine \
    build/ch01/dialect.cpp -o build/ch01/dialect
```

### A warning that disagrees with ZII

The program compiles, but GCC prints four warnings like these:

```
build/ch01/dialect.cpp:54:5: warning: missing initializer for member ‘Entity::facing_left’ [-Wmissing-field-initializers]
build/ch01/dialect.cpp:54:5: warning: missing initializer for member ‘Entity::hp’ [-Wmissing-field-initializers]
```

`-Wextra` turns on `-Wmissing-field-initializers`. It's meant for positional initializers like `{ ENTITY_BAT, 128, 48 }`, where a missing field could be a mistake. GCC's manual says that in C, it doesn't warn about designated initializers, because leaving fields out of one is clearly on purpose. In C++, GCC warns anyway.

Leaving fields out is the whole point of ZII, so momo turns this one warning off with `-Wno-missing-field-initializers`. Not much is lost: a field left out of a positional initializer becomes zero, and ZII already says zero is valid. Chapter 02's Makefile will include the flag. For now, compile again with it:

```sh
g++ -std=c++20 -Wall -Wextra -Wno-missing-field-initializers -fno-exceptions -fno-rtti \
    -O0 -g3 -fsanitize=address,undefined -Iengine \
    build/ch01/dialect.cpp -o build/ch01/dialect
```

This time there's no output.

### Running it

The argument picks an entity by index. The program prints that entity, updates it once, and prints it again:

```
$ ./build/ch01/dialect
player x= 24 y=192 hp=28 facing right
player x= 26 y=192 hp=28 facing right
$ ./build/ch01/dialect 2
slime  x=200 y=192 hp= 0 facing right
slime  x=199 y=192 hp= 0 facing left
```

The slime shows ZII at work. Its initializer never mentioned `hp` or `facing_left`, so both started at zero, and zero is a valid value for each. The `switch` then changed only what the slime's `case` changes.

Now ask for an entity that doesn't exist:

```
$ ./build/ch01/dialect 3
build/ch01/dialect.cpp:27: ASSERT(index < count) failed
zsh: illegal hardware instruction  ./build/ch01/dialect 3
```

The first line is our message. The second is zsh reporting `SIGILL`, the signal from the trap instruction (bash says `Illegal instruction`). `./build/ch01/dialect -1` fails the other `ASSERT`, on line 26.

### Stopping in gdb

Run the same failing case under gdb. `--args` passes everything after the program name on to the program, and `-q` skips the banner:

```sh
gdb -q --args ./build/ch01/dialect 3
```

If gdb asks `Enable debuginfod for this session?`, answer `n`. It offers to download debug symbols for system libraries, and we don't need them. To stop the question for good, put `set debuginfod enabled off` in `~/.gdbinit`.

Type `run`. gdb may first print a line or two about `libthread_db`, which you can ignore:

```
(gdb) run
build/ch01/dialect.cpp:27: ASSERT(index < count) failed

Program received signal SIGILL, Illegal instruction.
0x0000555555557384 in entity::get (entities=0x7bfff45f0020, count=3, index=3) at build/ch01/dialect.cpp:27
27	    ASSERT(index < count);
```

gdb stopped on the line that failed, inside the function that failed, and the stop message already shows its arguments: `count=3, index=3`. The program is frozen at that moment, so you can look around:

```
(gdb) bt
#0  0x0000555555557384 in entity::get (entities=0x7bfff45f0020, count=3, index=3) at build/ch01/dialect.cpp:27
#1  0x0000555555558097 in main (argc=2, argv=0x7fffffffe438) at build/ch01/dialect.cpp:57
(gdb) print index
$1 = 3
(gdb) up
#1  0x0000555555558097 in main (argc=2, argv=0x7fffffffe438) at build/ch01/dialect.cpp:57
57	    Entity* e = entity::get(entities, ARRAY_COUNT(entities), index);
(gdb) print argv[1]
$2 = 0x7fffffffe816 "3"
(gdb) info locals
entities = {{kind = ENTITY_PLAYER, x = 24, y = 192, hp = 28, facing_left = false}, {kind = ENTITY_BAT, x = 128, y = 48, hp = 1, facing_left = false}, {kind = ENTITY_SLIME, x = 200, y = 192, hp = 0, facing_left = false}}
index = 3
e = 0x0
(gdb) quit
```

Your addresses will differ. Here's what each command did:

| Command | What it does |
|---|---|
| `run` | Starts the program, with the arguments given to `--args` |
| `bt` | Backtrace: the chain of calls that led here, innermost first. `#0` is where we stopped, and `#1` called it. |
| `print expr` (or `p`) | Evaluates any C++ expression in the current frame: a variable, `argv[1]`, `entities[2].hp`, `count - 1` |
| `up` / `down` | Moves to the calling frame and back, so `print` sees that function's variables |
| `info locals` | Prints every local variable in the current frame. `e` is still null because `get` never returned. |
| `quit` | Exits. gdb asks whether to kill the program; answer `y`. |

`up` and `print argv[1]` finish the investigation: the bad index came from the command line. That's the routine for every `ASSERT` in this book: read the message, run under gdb, then use `bt`, `print` and `up` until you know where the bad value came from. You can't `continue` past a failed `ASSERT`, because gdb would hand the signal to the program and it would exit. Fix the bug and run again.

## Recap

The dialect, short enough to keep in your head:

- The test: can I tell what this line does by reading only this line?
- Plain structs, free functions in a namespace, no member functions.
- All zeros is a valid value. Create structs with `= {}` or `{ .field = value }`.
- `init` / `shutdown` instead of constructors and destructors.
- `switch` on a `kind` enum instead of `virtual`.
- No exceptions: bugs `ASSERT`, expected failures return `false` or `nullptr`.
- Pointers for anything that changes.
- Signed numbers for counts, indices and arithmetic.
- From C++: templates for containers and math, operators for math, namespaces, `constexpr`.

The repo now has the engine's first file:

```
momo/
  book/
    00-preface.md
    01-dialect.md      this chapter
    plan.md            §3 and §7 gained the details this chapter settled
  engine/
    base.h             i8…f64, ASSERT, ARRAY_COUNT
```

There's no module yet. `base.h` is what every module will include, and chapter 02 builds the first ones: the host, `libmomo.so` and `game.so`.

The plan now records what this chapter decided that it didn't cover before: `= {}` and designated initializers, C-style casts, `i32` counts with `i64` sizes, `constexpr` constants in `UPPER_SNAKE`, how `ASSERT` stops, and the `-Wno-missing-field-initializers` flag.

## Exercises

All of them edit `build/ch01/dialect.cpp` and recompile with the second command from Run it.

1. **Let the compiler enforce a rule.** Add `throw 1;` before `return 0;` in `main` and compile. Then replace it with `printf("%s\n", typeid(*e).name());`. Read both errors.
2. **Forget a case.** Add `ENTITY_SKELETON` to `EntityKind` and compile. Read the warning, then add the `case`.
3. **Pass a pointer.** In `main`, add `Entity* list = entities;` and change the call to `entity::get(list, ARRAY_COUNT(list), index)`. Read the warning. Without it, what would `ARRAY_COUNT(list)` return? (`sizeof(Entity)` is 20.)
4. **Go unsigned.** Change `count` and `index` in `entity::get` to `u32` and compile. Which `ASSERT` does the compiler say is pointless, and why? Then run `./build/ch01/dialect -1`. Which `ASSERT` catches it now, and what is `index` in gdb?
5. **Debug an optimized build.** Compile with `-O2 -g` instead of `-O0 -g3` and the sanitizers. These are the release build's flags. Run the gdb session again. gdb still stops on line 27, but what does `print index` show? That answer is why the debug build uses `-O0`.
