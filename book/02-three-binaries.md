# 02 · Three Binaries

> **Goal:** build momo's three binaries with one `make`: the `momo` executable, the engine `libmomo.so`, and the game `game.so`. Run them and watch the engine load the game and call it, and the game call back into the engine. Your editor learns how momo is compiled, too.

## The problem

Chapter 06 gives momo hot reload: you rebuild the game while it runs, and the running program swaps in the new code. That decides how momo is split into files, so it has to be settled before there's any engine code to split.

An operating system loads code one file at a time. A program's executable is loaded when it starts and stays until it exits. The only code a running program can load later, and unload again, is a **shared library**: a `.so` on Linux, a `.dll` on Windows. So the game has to be a shared library of its own, `game.so`.

The engine is everything that stays put: the window, the renderer, input, memory and the loop. It has to keep running, with all its state, while `game.so` is swapped out underneath it. And the game has to be able to call it, the way it will call `renderer::draw_sprite` or `input::pressed` in later chapters.

So how many binaries are there, and which one links to which? This chapter answers that and builds them. Chapters 00 and 01 compiled single files by hand. From here on, a Makefile does it.

## Shared libraries in a minute

A program can use a library in one of two ways:

- **Statically linked**: the linker copies the library's code into your binary at build time.
- **Dynamically linked**: your binary records only the library's name and which of its functions you call. When the program starts, before `main`, the *dynamic loader* (`ld.so` on Linux) finds each library, maps it into memory, and fills in the address of every function you call. `printf` reaches you this way. It lives in `libc.so.6`, not in your program.

A program can also load a shared library itself, at any moment while it runs, with three functions from `<dlfcn.h>`:

| Function | What it does |
|---|---|
| `dlopen(path, flags)` | Loads the library and returns a handle to it |
| `dlsym(handle, "name")` | Looks up a symbol (a function or variable) by name and returns its address |
| `dlclose(handle)` | Unloads the library |

Load, look up, unload. Hot reload is those three steps, repeated every time the game is rebuilt.

## Design

### Who links to whom

There were four ways to split momo up:

| Layout | How the game calls the engine | The catch |
|---|---|---|
| One executable | Normal function calls | Nothing can be reloaded |
| The engine is the executable, and hands `game.so` a struct of function pointers | `engine->draw_sprite(...)` | Every engine function the game uses needs a slot in that struct, filled in by hand. Handmade Hero works this way. |
| The engine is a static library, linked into both the executable and `game.so` | Normal function calls | Two copies of the engine, each with its own state. The game would draw with a renderer that was never given a window. |
| **`momo` + `libmomo.so` + `game.so`** | Normal function calls: `momo::frame_index()` | One more file, and the loader has to be told where `libmomo.so` is |

momo uses the last one ([plan §4.1](plan.md#41-three-binaries-and-hot-reload)). The engine is an ordinary shared library. The executable is a few lines that call into it, and the game links against it like it would any other library:

```
   momo (executable)        libmomo.so (the engine)          game.so (the game)
  ┌─────────────────┐      ┌──────────────────────┐        ┌───────────────────┐
  │ main()          │─(1)─▶│ momo::run()          │──(2)──▶│ momo_game_api()   │
  │   momo::run()   │      │ momo::frame_index()  │        │ game::init()      │
  │                 │      │ momo::quit()         │◀──(3)──│ game::update()    │
  └─────────────────┘      └──────────────────────┘        │ game::shutdown()  │
                                                           └───────────────────┘
```

The three arrows are three different connections:

1. `momo` links `libmomo.so` the ordinary way. The loader loads the engine before `main` runs.
2. The engine loads `game.so` with `dlopen` when `momo::run` starts. Because the engine loaded it, the engine can unload it and load a new one (chapter 06).
3. `game.so` also links `libmomo.so` the ordinary way. When the engine opens `game.so`, the loader sees that `libmomo.so` is already loaded and connects the game to that same copy. There's one engine, with one set of state, and both the host and the game use it.

Why does the engine `dlopen` the game, instead of `momo` linking `game.so` the ordinary way? A library linked the ordinary way is loaded before `main` and stays until the program exits. Only a library the program opened itself can be swapped.

### What crosses between binaries

Three details make this layout work. The code below covers each one:

- **Which functions other binaries can see.** By default, a shared library on Linux exports every function in it. momo turns that off and marks the few it means to export.
- **How the engine finds the game's code by name.** `dlsym` takes a string, and C++ changes function names in the binary, so the game's one export is declared `extern "C"` to keep its name plain.
- **How `momo` finds `libmomo.so`** when it starts, without installing anything.

### A change to the plan

The plan's outline put `dlopen` and the game's API struct in chapter 06. But a `game.so` that nothing loads would prove nothing, so this chapter loads it, once, at startup. Chapter 06 keeps the part that's actually hot reload: noticing the file changed, swapping it, and keeping the game's state across the swap. The outline in [plan §8](plan.md#8-outline) and the change log now say so.

> **Hazel did this too, once.** Early in TheCherno's Game Engine series, Hazel was a DLL, with an `HZ_API` macro doing the job `MOMO_API` does here. It later became a static library linked into the app, partly because exporting C++ classes across a DLL boundary kept causing trouble, especially classes with standard library members. momo's boundary is a few plain functions and one plain struct, so that trouble doesn't come up.

## The code

### The host: `host/main.cpp`

`host/main.cpp:5`

```cpp
int main()
{
    return momo::run();
}
```

That's the whole executable. It exists because a shared library can't be started on its own: the OS only starts executables. The host is the smallest executable that can start the engine. Everything else, including the loop, lives in `libmomo.so`, where the game can call it.

### The boundary: `engine/momo.h`

Everything that crosses between the three binaries is declared in this one header. The host, the engine and the game all include it.

**Exports.** `engine/momo.h:14`

```cpp
#define MOMO_API __attribute__((visibility("default")))

namespace momo {

MOMO_API i32 run();
MOMO_API i32 frame_index();
MOMO_API void quit();

} // namespace momo
```

Both shared libraries are compiled with `-fvisibility=hidden`, which flips Linux's default: a function is private to its library unless it's marked. `visibility("default")` marks one as public again. So the engine exports exactly the functions in this header that have `MOMO_API` in front. Everything else in `momo.cpp`, like `load_game` below, can't be called from another binary, even by accident. Real engines make this choice on purpose, because every exported function is a promise that game code might come to depend on.

Windows spells this differently (`__declspec(dllexport)` while building the library, `__declspec(dllimport)` everywhere else), so a Windows port only has to change this one definition.

**The game's entry point.** `engine/momo.h:35`

```cpp
struct GameApi {
    void (*init)();
    void (*update)();
    void (*shutdown)();
};

extern "C" MOMO_API GameApi momo_game_api();

using GameApiFn = GameApi();
```

The engine calls three functions in the game, and finds all three through one exported function that returns them in a struct. That's the one-function boundary from [plan §4.1](plan.md#41-three-binaries-and-hot-reload). It takes one `dlsym` lookup instead of one per function, and in chapter 06, after a reload, one call to `momo_game_api` hands the engine every new address at once. `render` joins the struct when there's something to draw.

Doesn't a struct of function pointers break the rule against storing function pointers ([plan §3.2](plan.md#32-rules-for-game-code-required-by-hot-reload))? That rule is about game state, which outlives the code it points into. `GameApi` lives in the engine, and the engine fetches it again after every reload.

`extern "C"` is about **name mangling**. C++ lets two functions share a name when they're in different namespaces or take different parameters, so the compiler encodes the namespace and the parameter types into the name it writes into the binary. `momo::frame_index()` is stored as `_ZN4momo11frame_indexEv`:

| Part | Means |
|---|---|
| `_Z` | This is a mangled C++ name |
| `N` … `E` | A name inside a namespace |
| `4momo` | A 4-letter name: `momo` |
| `11frame_index` | An 11-letter name: `frame_index` |
| `v` | No parameters (`void`) |

You never see mangling when the linker connects ordinary calls, because the compiler mangles the name the same way on both sides. `dlsym` is different: you hand it a string. `extern "C"` tells the compiler to name the function the way C does, which means leaving the name alone. The symbol is exactly `momo_game_api`, and the engine can ask for it by that name. Every other function stays mangled, which is fine, because ordinary linking connects those.

The last line names a function type: `GameApiFn` is "a function that takes nothing and returns a `GameApi`". `dlsym` returns a `void*`, and the engine casts it to `GameApiFn*` before calling it.

### Loading the game: `engine/momo.cpp`

`engine/momo.cpp:13`

```cpp
struct Engine {
    void* game_lib; // the handle dlopen returned for game.so
    GameApi game;
    i32 frame_index;
    bool quit_requested;
};

static Engine engine;
```

The engine keeps its state in a `static` variable. Game code isn't allowed to do that (chapter 06 shows why), but the engine never reloads, so its file-scope state is safe ([plan §4.1](plan.md#41-three-binaries-and-hot-reload)). Static variables start out zeroed, so ZII holds: no game loaded, frame 0, not quitting.

`load_game` finds `game.so`, opens it, and asks it for its API. First, the path:

`engine/momo.cpp:29`

```cpp
    char exe[PATH_MAX] = {};
    if (readlink("/proc/self/exe", exe, sizeof(exe) - 1) < 0) {
        fprintf(stderr, "momo: can't find the executable's path\n");
        return false;
    }
    *strrchr(exe, '/') = 0; // ".../build/debug/momo" becomes ".../build/debug"

    char path[PATH_MAX + 16] = {}; // room for the folder plus "/game.so"
    snprintf(path, sizeof(path), "%s/game.so", exe);
```

`game.so` sits next to the executable: in `build/debug/` for the debug build, `build/release/` for the release build. On Linux, `/proc/self/exe` is a symbolic link to the running executable, so reading it gives the executable's full path, whichever folder you started it from. Cutting it at the last `/` leaves the folder. This and `dlopen` are momo's first Linux-only code. Chapter 07 moves code like this behind `platform::`.

Then the three steps from the start of the chapter:

`engine/momo.cpp:42`

```cpp
    engine.game_lib = dlopen(path, RTLD_NOW);
    if (!engine.game_lib) {
        fprintf(stderr, "momo: %s\n", dlerror());
        return false;
    }

    auto* get_game_api = (GameApiFn*)dlsym(engine.game_lib, "momo_game_api");
    if (!get_game_api) {
        fprintf(stderr, "momo: %s\n", dlerror());
        dlclose(engine.game_lib);
        return false;
    }

    engine.game = get_game_api();
```

`RTLD_NOW` tells the loader to connect every engine function `game.so` calls right away. The alternative, `RTLD_LAZY`, connects each one the first time it's called, so a missing function would stop the program in the middle of a frame instead of here. Both failures are expected ones in the sense of [plan §3.3](plan.md#33-errors-and-strings): you forgot to build the game, or built a broken one. So they print why (`dlerror` returns the loader's message) and return `false`. They don't `ASSERT`, because they don't mean momo has a bug.

The loop:

`engine/momo.cpp:60`

```cpp
i32 run()
{
    if (!load_game())
        return 1;

    // Chapter 09 turns this into a real game loop with a fixed timestep.
    engine.game.init();
    while (!engine.quit_requested) {
        engine.game.update();
        engine.frame_index++;
    }
    engine.game.shutdown();

    printf("momo: quit after %d frames\n", engine.frame_index);
    dlclose(engine.game_lib);
    return 0;
}
```

This is the shape every later chapter keeps: the engine owns the loop and calls the game. There's no clock yet, so the frames run as fast as the CPU allows, and the game decides when to stop by calling `momo::quit()`.

### The game: `game/game.cpp`

`game/game.cpp:12`

```cpp
void update()
{
    printf("game: update, frame %d\n", momo::frame_index());
    if (momo::frame_index() == 2)
        momo::quit();
}
```

`game/game.cpp:28`

```cpp
extern "C" MOMO_API GameApi momo_game_api()
{
    return { .init = game::init, .update = game::update, .shutdown = game::shutdown };
}
```

Both directions fit in a few lines. The engine calls `game::update` through the struct, and `game::update` calls `momo::frame_index` and `momo::quit` in the engine.

Why does the game ask the engine for the frame number instead of counting frames itself? Its counter would have to be a global in `game.so`, and globals in game code reset on every reload. Chapter 06 gives the game memory that survives a reload.

### The Makefile

The Makefile is about 120 lines, a third of them comments. Here it is in order.

**Two builds.** `Makefile:10`

```make
CONFIG ?= debug
OUT    := build/$(CONFIG)

CXX := g++

# For every file in every build
CXXFLAGS := -std=c++20 -Wall -Wextra -Wno-missing-field-initializers \
            -fno-exceptions -fno-rtti -Iengine

ifeq ($(CONFIG),debug)
    CXXFLAGS += -O0 -g3 -fsanitize=address,undefined
    LDFLAGS  += -fsanitize=address,undefined
else ifeq ($(CONFIG),release)
    CXXFLAGS += -O2 -g
else
    $(error CONFIG must be debug or release, not "$(CONFIG)")
endif
```

`CONFIG` picks the build, and everything it produces goes under `build/$(CONFIG)/`, so both builds can exist side by side. `?=` makes `debug` only a default. A variable set on make's command line wins over the Makefile, so `make CONFIG=release` builds the other one, and that's all the `release` rule (`Makefile:60`) does: it runs a second make with that setting.

The flags are the ones chapter 01 typed by hand ([plan §7](plan.md#7-build-dependencies-repo)), plus `-Iengine`, so `#include "momo.h"` works from `game/` and `host/` too. The sanitizers appear in both `CXXFLAGS` and `LDFLAGS` because they come in two halves: compiling adds the checks to your code, and linking adds the runtime library that reports what the checks find.

**Flags for shared libraries.** `Makefile:32`

```make
LIB_CXXFLAGS := $(CXXFLAGS) -fPIC -fvisibility=hidden
LIB_LDFLAGS := $(LDFLAGS) -shared -Wl,--no-undefined
LINK_MOMO := -L$(OUT) -lmomo -Wl,-rpath,'$$ORIGIN'
```

(The file has a comment above each line.)

| Flag | Why |
|---|---|
| `-fPIC` | Position-independent code. A shared library doesn't know where in memory it'll be loaded: it's different every run, and different again on every reload of `game.so`. Position-independent code works at any address. It finds its own data relative to wherever it landed, and other libraries' functions through a table the loader fills in. Without it, the linker refuses to build a shared library (exercise 4). |
| `-fvisibility=hidden` | Nothing is exported unless it's marked `MOMO_API` |
| `-shared` | Link a shared library instead of an executable |
| `-Wl,--no-undefined` | `-Wl,` passes what follows to the linker. Normally a shared library may call functions that nothing provides yet, in case some other library provides them at load time. This flag turns that into a link error, so calling an engine function that isn't exported fails in the build, not at `dlopen` (exercise 1). |
| `-L$(OUT) -lmomo` | Link against `libmomo.so`: `-lmomo` means "look for `libmomo.so`", and `-L` adds `build/debug/` to the folders to look in |
| `-Wl,-rpath,'$$ORIGIN'` | Tells the loader where to look for `libmomo.so` at run time: `$ORIGIN`, which the loader reads as "the folder this binary is in". `$$` is how Make writes a single `$`, and the quotes stop the shell from expanding `$ORIGIN` itself. |

`-L` only helps the linker at build time. At run time the loader does its own search: the folders in `LD_LIBRARY_PATH`, then the folders the binary lists in its RUNPATH, then system folders like `/usr/lib`. Without the rpath, `momo` would build fine and then fail to start (exercise 3).

**Files.** `Makefile:48`

```make
ENGINE_SRC := $(wildcard engine/*.cpp)
GAME_SRC   := $(wildcard game/*.cpp)
HOST_SRC   := $(wildcard host/*.cpp)

ENGINE_OBJ := $(ENGINE_SRC:%.cpp=$(OUT)/obj/%.o)
```

Every `.cpp` in `engine/` belongs to the engine, and the same goes for `game/` and `host/`. A new file joins its binary without any change to the Makefile. The last line rewrites each name: `engine/momo.cpp` becomes `build/debug/obj/engine/momo.o`.

**Linking.** `Makefile:73`

```make
$(OUT)/libmomo.so: $(ENGINE_OBJ) Makefile
	$(CXX) $(LIB_LDFLAGS) $(ENGINE_OBJ) -o $@

$(OUT)/game.so: $(GAME_OBJ) $(OUT)/libmomo.so Makefile
	$(CXX) $(LIB_LDFLAGS) $(GAME_OBJ) $(LINK_MOMO) -o $@

$(OUT)/momo: $(HOST_OBJ) $(OUT)/libmomo.so Makefile
	$(CXX) $(LDFLAGS) $(HOST_OBJ) $(LINK_MOMO) -o $@
```

Each rule is a target, a colon, its prerequisites, and below them the command that builds the target. `$@` is the target. `game.so` and `momo` list `libmomo.so` as a prerequisite because they link against it, so make always builds the engine first, even in a parallel build (`make -j`). Every rule also lists the `Makefile`, so changing a flag rebuilds everything that uses it.

**Compiling.** `Makefile:90`

```make
$(OUT)/obj/engine/%.o: engine/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(LIB_CXXFLAGS) -MMD -MP -c $< -o $@

# ...the same rule for game/, and for host/ with plain CXXFLAGS...

-include $(ENGINE_OBJ:.o=.d) $(GAME_OBJ:.o=.d) $(HOST_OBJ:.o=.d)
```

This is a pattern rule. `%` matches any name, so this one rule compiles every engine file. `$<` is the first prerequisite, the `.cpp`, and `-c` means compile without linking. `mkdir -p $(dir $@)` creates the object's folder first, and the `@` in front stops make from printing that command. Game files get the same flags as the engine. Host files get plain `CXXFLAGS`, because the executable isn't a shared library and exports nothing.

A `.cpp` file isn't the only thing its object depends on. `game.o` has to be rebuilt when `momo.h` changes too. `-MMD` makes the compiler write down every header it read, in a `.d` file next to the object, and that file is itself a small makefile:

```make
build/debug/obj/game/game.o: game/game.cpp engine/momo.h engine/base.h
engine/momo.h:
engine/base.h:
```

The `-include` line reads all the `.d` files. The `-` in front means "don't complain if they don't exist yet", as on the first build. The two empty rules come from `-MP`: if you delete a header, make treats it as a target with nothing to do, instead of stopping with "No rule to make target".

**compile_commands.json.** `Makefile:112`

```make
compdb_entry = echo '{ "directory": "$(CURDIR)", "file": "$(1)", "command": "$(CXX) $(2) -c $(1)" }';

compile_commands.json: Makefile $(ENGINE_SRC) $(GAME_SRC) $(HOST_SRC)
	@{ echo '['; \
	   { $(foreach f,$(ENGINE_SRC) $(GAME_SRC),$(call compdb_entry,$(f),$(LIB_CXXFLAGS))) \
	     $(foreach f,$(HOST_SRC),$(call compdb_entry,$(f),$(CXXFLAGS))) } | sed '$$!s/$$/,/'; \
	   echo ']'; } > $@
```

Editors don't run make. A language server like clangd compiles each file on its own as you type, so it needs the same flags the build uses. Without `-Iengine` it can't find `momo.h`, and every line that uses `i32` turns red. `compile_commands.json` is the standard fix: a JSON list of every source file and the command that compiles it. Tools like `bear` record one by watching a real build. momo's Makefile writes it directly, from the same variables the build uses, so the two can't disagree.

`compdb_entry` is a Make function. `$(call compdb_entry,game/game.cpp,$(LIB_CXXFLAGS))` expands to a shell `echo` that prints one entry, and the two `foreach` lines produce one entry per source file. JSON doesn't allow a comma after the last entry, so `sed` adds one to every line except the last (`$!` means "not on the last line"). The file is part of `all`, so it's rewritten, quietly, whenever the Makefile or a source file changes. It holds absolute paths, so git ignores it.

## Run it

### Build

From the repo root:

```sh
make
```

Your terminal shows every flag. Here they're shortened to `...`:

```
g++ -std=c++20 ... -O0 -g3 -fsanitize=address,undefined -MMD -MP -c host/main.cpp -o build/debug/obj/host/main.o
g++ -std=c++20 ... -fPIC -fvisibility=hidden -MMD -MP -c engine/momo.cpp -o build/debug/obj/engine/momo.o
g++ -fsanitize=address,undefined -shared -Wl,--no-undefined build/debug/obj/engine/momo.o -o build/debug/libmomo.so
g++ -fsanitize=address,undefined build/debug/obj/host/main.o -Lbuild/debug -lmomo -Wl,-rpath,'$ORIGIN' -o build/debug/momo
g++ -std=c++20 ... -fPIC -fvisibility=hidden -MMD -MP -c game/game.cpp -o build/debug/obj/game/game.o
g++ -fsanitize=address,undefined -shared -Wl,--no-undefined build/debug/obj/game/game.o -Lbuild/debug -lmomo -Wl,-rpath,'$ORIGIN' -o build/debug/game.so
```

Three compiles and three links, with no warnings. The order follows the prerequisites: `libmomo.so` is linked before anything that links against it.

### Run

```
$ ./build/debug/momo
momo: loaded /home/mairuu/repos/momo/build/debug/game.so
game: init
game: update, frame 0
game: update, frame 1
game: update, frame 2
game: shutdown
momo: quit after 3 frames
```

Between them, these lines cross every boundary: `main` into the engine, the engine into the game through `GameApi`, and the game back into the engine for the frame number and to quit. It works from any folder (try `cd build && ./debug/momo`), thanks to `$ORIGIN` and `/proc/self/exe`. And it ran under both sanitizers without a report.

### The release build

```sh
make release
./build/release/momo
```

make prints `make CONFIG=release`, then the same six commands with `-O2 -g` and no sanitizers, and `build/release/momo` prints the same lines as the debug build.

### Look inside

`readelf -d` prints a binary's dynamic section, which is what the loader reads before `main`. Use the release build here, because the debug build also needs the sanitizer libraries and the list gets long:

```
$ readelf -d build/release/momo | grep -E 'NEEDED|RUNPATH'
 0x0000000000000001 (NEEDED)             Shared library: [libmomo.so]
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN]
```

`libmomo.so` is recorded by name only, and `$ORIGIN` says where to look for it. The next three come with every link `g++` does, even though momo doesn't use them yet. `readelf -d build/release/game.so` shows the same lines, because the game depends on the engine too.

`nm -D` lists a binary's dynamic symbols, the ones other binaries can see. `--defined-only` keeps the ones it provides, and `-C` demangles their names:

```
$ nm -DC --defined-only build/release/libmomo.so
00000000000013e0 T momo::frame_index()
0000000000001370 T momo::run()
00000000000013f0 T momo::quit()
$ nm -D --defined-only build/release/game.so
00000000000011a0 T momo_game_api
```

Only the `MOMO_API` functions are there. `load_game`, `engine` and `game::update` are all hidden. (`T` means the symbol is code, which lives in a section called "text".) Now look at what the game needs from other libraries, without demangling:

```
$ nm -D --undefined-only build/release/game.so | grep momo
                 U _ZN4momo11frame_indexEv
                 U _ZN4momo4quitEv
```

`U` means undefined: a name the loader has to find in some other library. These two are mangled, and `momo_game_api` above isn't. That's `extern "C"` at work.

### Three binaries, one backtrace

gdb treats all three binaries as one program, because at run time they are one. Start it on the debug build:

```sh
gdb -q ./build/debug/momo
```

Set a breakpoint in the game before running:

```
(gdb) break game::update
Function "game::update" not defined.
Make breakpoint pending on future shared library load? (y or [n]) y
Breakpoint 1 (game::update) pending.
```

gdb can't find `game::update` yet, because nothing has loaded `game.so`. That happens later, inside `momo::run`. Answering `y` makes the breakpoint *pending*: gdb sets it as soon as a library with that function loads. (If gdb asks about debuginfod first, answer `n`, as in chapter 01.)

```
(gdb) run
momo: loaded /home/mairuu/repos/momo/build/debug/game.so
game: init

Breakpoint 1, game::update () at game/game.cpp:14
14	    printf("game: update, frame %d\n", momo::frame_index());
(gdb) bt
#0  game::update () at game/game.cpp:14
#1  0x00007ffff7f90922 in momo::run () at engine/momo.cpp:68
#2  0x0000555555555162 in main () at host/main.cpp:7
```

That's one chain of calls through three binaries: `main` in the executable, `momo::run` in `libmomo.so`, and `game::update` in `game.so`. The addresses show it: the executable is loaded low, around `0x5555…`, and shared libraries high, around `0x7fff…`. Your addresses will differ.

```
(gdb) info sharedlibrary momo
From                To                  Syms Read   Shared Object Library
0x00007ffff7f8e000  0x00007ffff7f94000  Yes         /home/mairuu/repos/momo/build/debug/libmomo.so
0x00007ffff76e7000  0x00007ffff76ec000  Yes         /home/mairuu/repos/momo/build/debug/game.so
(gdb) print momo::frame_index()
$1 = 0
(gdb) continue
Continuing.
game: update, frame 0

Breakpoint 1, game::update () at game/game.cpp:14
14	    printf("game: update, frame %d\n", momo::frame_index());
(gdb) print momo::frame_index()
$2 = 1
(gdb) quit
```

gdb asks whether to kill the running program; answer `y`. The new commands:

| Command | What it does |
|---|---|
| `break fn` (or `b`) | Stops when `fn` is called. If `fn`'s library isn't loaded yet, gdb offers to make the breakpoint pending. |
| `info sharedlibrary [text]` | Lists the loaded shared libraries, or only those whose path contains `text` |
| `print fn()` | Calls a function inside the stopped program and prints what it returned |
| `continue` (or `c`) | Runs until the next breakpoint |

### Rebuild only what changed

```
$ make
make: Nothing to be done for 'all'.
$ touch game/game.cpp
$ make
g++ -std=c++20 ... -c game/game.cpp -o build/debug/obj/game/game.o
g++ -fsanitize=address,undefined -shared ... -o build/debug/game.so
```

make decides by modification time: a target is rebuilt when any of its prerequisites is newer than it. `touch` makes a file newer without changing it. Changing the game rebuilds only the game, and that's what chapter 06 relies on: rebuilding `game.so` is what triggers a reload, and the engine, which didn't change, keeps running.

Now `touch engine/base.h` and run `make` again. Everything is rebuilt, because every `.cpp` includes `base.h` through `momo.h`, and the `.d` files told make so.

### Your editor

The build also wrote `compile_commands.json` at the repo root:

```
$ head -2 compile_commands.json
[
{ "directory": "/home/mairuu/repos/momo", "file": "engine/momo.cpp", "command": "g++ -std=c++20 ... -fPIC -fvisibility=hidden -c engine/momo.cpp" },
```

clangd looks for this file in the folder of the file you open and in every folder above it, so the copy at the root covers all of momo. Headers aren't listed; clangd borrows the command of a `.cpp` file in the same folder. If your editor was already open, restart its language server (`:LspRestart` in Neovim). Then open `game/game.cpp`: there are no red lines, and hovering over `momo::frame_index` shows its comment from `engine/momo.h`. clangd keeps its index in `.cache/` at the repo root, which git ignores too.

clangd is built on Clang, not GCC, but it reads `g++`'s flags fine. If the two ever disagree about a warning, the build is the judge.

## Recap

momo now has its shape: three binaries and one header between them.

```
momo/
  Makefile                 make, make release, make clean
  compile_commands.json    written by every build, for editors (git ignores it)
  host/
    main.cpp               → momo         main() calls momo::run()
  engine/
    base.h                                types, ASSERT, ARRAY_COUNT
    momo.h                                the boundary: MOMO_API, run, frame_index, quit, GameApi
    momo.cpp               → libmomo.so   loads game.so and runs the loop
  game/
    game.cpp               → game.so      init, update, shutdown; exports momo_game_api
  build/debug/, build/release/
    momo, libmomo.so, game.so, obj/
```

- The game is a shared library, because only shared libraries can be loaded and unloaded while a program runs.
- The engine is a shared library too, so the host and the game use one copy of it.
- `momo` and `game.so` link `libmomo.so` the ordinary way. The engine loads `game.so` itself, with `dlopen`.
- `-fvisibility=hidden` plus `MOMO_API`: a library exports only what its header marks.
- `extern "C"` gives the game's one export a plain name that `dlsym` can find.
- `$ORIGIN` lets each binary find `libmomo.so` in its own folder.
- `-MMD -MP` and the Makefile prerequisite make sure a change rebuilds exactly what it affects.

The plan records this chapter's decisions in [§4.1](plan.md#41-three-binaries-and-hot-reload) and [§7](plan.md#7-build-dependencies-repo), and the outline notes that loading `game.so` moved here from chapter 06.

Chapter 03 adds `make test` and a test runner in about 50 lines.

## Exercises

Each one edits a file and runs `make`. Since every rule depends on the Makefile, a changed flag rebuilds everything that uses it. Undo each change before the next exercise: `git checkout -- Makefile engine game` puts those files back.

1. **Hide an engine function.** Delete `MOMO_API` in front of `frame_index` (`engine/momo.h:25`) and run `make`. Read the linker's error. Then also delete `-Wl,--no-undefined` from `Makefile:36`, run `make` and `./build/debug/momo`. Where does it fail now? Decode the name in the message with `c++filt`.
2. **Mangle the entry point.** Delete `extern "C"` from both `engine/momo.h:44` and `game/game.cpp:28`, then build and run. Find the export's new name with `nm -D --defined-only build/debug/game.so`. Then put back only the one in `momo.h`, and build and run again. Why does it work?
3. **Lose the engine.** Delete `-Wl,-rpath,'$$ORIGIN'` from `Makefile:42`, then build and run. Now run `LD_LIBRARY_PATH=build/debug ./build/debug/momo`. Why is a path built into the binary better than that variable?
4. **Drop `-fPIC`.** Delete it from `Makefile:32` and run `make`. What does the linker tell you to do?
5. **Export everything.** Delete `-fvisibility=hidden` from `Makefile:32`, run `make release`, and then `nm -DC --defined-only build/release/game.so`. What does the game export now that nobody calls by name?
