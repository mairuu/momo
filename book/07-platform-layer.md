# 07 · Platform Layer

> **Goal:** momo opens a window. Close it, or press Ctrl+C in the terminal, and the game quits cleanly either way, through the same SDL event. Under the window, every call momo makes to the operating system moves into one new module, `platform::`, the only code in momo that includes SDL. Along the way the debug build learns to ignore a leak that isn't momo's.

## The problem

Part A built the engine in a terminal. The loop runs, the bat flies across the log, and hot reload swaps the game under it. Part B gives momo a window, a GPU and a clock, and the window comes first, since the GPU needs somewhere to draw.

Opening a window is a conversation with the system's display server: Wayland or X11 on Linux, Win32 on Windows, Cocoa on macOS. Each one speaks its own protocol, and each sends back its own events: the window was closed, resized, covered, or got the keyboard. The plan hands all of that to SDL3 ([plan §4.4](plan.md#44-platform-and-input)). The question for this chapter is where SDL goes.

There's a second job waiting. momo already talks to the OS, and those calls are scattered through `engine/momo.cpp`, each with a comment promising that this chapter moves it:

| What | How (chapters 02–06) | Header |
|---|---|---|
| Reserve the arenas' memory | `mmap` | `<sys/mman.h>` |
| Find the executable's folder | `readlink("/proc/self/exe")` | `<unistd.h>`, `<limits.h>` |
| Check when `game.so` changed | `stat` | `<sys/stat.h>` |
| Load and unload `game.so` | `dlopen`, `dlsym`, `dlclose` | `<dlfcn.h>` |
| Run about 60 frames a second | `usleep` | `<unistd.h>` |
| Quit on Ctrl+C | `signal(SIGINT, ...)` | `<signal.h>` |

Six system headers, mixed in with the engine's own logic. Each row is something a Windows port would have to find and rewrite.

And a window brings a second way to quit. Ctrl+C in the terminal still works, but now you can also close the window, and the two should end the game the same way.

## Design

### Where SDL goes

| Option | The catch |
|---|---|
| Call SDL wherever the engine needs it | SDL's types and names spread through every module. Replacing SDL, or working around it on one OS, touches all of them. |
| **One module, `platform::`, calls SDL. The rest of the engine calls `platform::`.** | A layer of small functions, many of them one SDL call each |
| Write `platform::` straight on the OS (Wayland, Win32), with no SDL | Hundreds of lines per OS before the first window appears, and none of it is a learning goal |

momo takes the middle one. Wrapping a library that is itself a wrapper looks redundant, so here's what the layer buys:

- **It says what momo needs, in momo's terms.** SDL has over a thousand functions, and momo uses under twenty. `platform.h` lists them with momo's types (`i32`, `i64`, `bool`), and every failure is logged the momo way. Reading the header tells you everything the engine asks of the OS.
- **One file includes `<SDL3/SDL.h>`.** The rest of the engine compiles without SDL's headers, and the game can't even link against SDL (Run it shows this).
- **One file to change.** Porting to an OS where SDL falls short, or dropping SDL for something else, rewrites `platform.cpp` and nothing else.

> **Handmade Hero's platform layer.** Casey Muratori writes his straight against Win32, with no SDL: `win32_handmade.cpp` is the executable, owns the window and the loop, and calls into the game, which is a DLL it loads. That's chapter 02's design question again, answered differently. In momo the platform layer is one module inside the engine, and the engine is the library both the host and the game link to.

### What goes behind it

Everything in the table above moves into `engine/platform.cpp`. Where SDL has a function for the job, momo uses it. Where it doesn't, momo calls the OS directly:

| momo needs | Now | Underneath, on Linux |
|---|---|---|
| A window and its events | `SDL_Init`, `SDL_CreateWindow`, `SDL_PollEvent` | Wayland or X11 |
| Zeroed memory for the arenas | `mmap`, as before | — |
| The executable's folder | `SDL_GetBasePath` | `readlink("/proc/self/exe")` |
| A file's modification time | `SDL_GetPathInfo` | `stat` |
| Loading `game.so` | `SDL_LoadObject`, `SDL_LoadFunction`, `SDL_UnloadObject` | `dlopen`, `dlsym`, `dlclose` |
| Waiting | `SDL_Delay` | `nanosleep` |
| Quitting on Ctrl+C | SDL's own signal handler | `sigaction` |

On Linux, SDL makes the same calls momo made before, which Run it checks in gdb for `dlopen`. On Windows it would make different ones: `GetModuleFileName`, `LoadLibrary`, `GetProcAddress`. SDL has nothing like `mmap`, though (its `SDL_malloc` is `malloc`), so `reserve_memory` stays OS-specific: the one function a Windows port of `platform.cpp` would rewrite, with `VirtualAlloc`.

With SDL finding the folder, the engine no longer cuts a path at its last `/` with `str::find_last` and `str::slice` from chapter 05. Those stay in `str::`, tested, for later chapters.

None of `platform::` is `MOMO_API`. The game never calls the OS. It asks the engine, through modules like chapter 14's `input::`, which gets the keyboard from `platform::`.

Paths are passed as `const char*`, not `Str`. Every OS function wants a 0 after the text, and a `Str` doesn't promise one (chapter 05). A `Str` made by `str::fmt` has one, so the engine passes its `.data`, as chapter 06 did for `dlopen`.

### One way to quit

SDL already handles both ways to quit, and it turns each into the same event, `SDL_EVENT_QUIT`:

```
  close button ──▶ SDL_EVENT_WINDOW_CLOSE_REQUESTED ──▶ (last window) ──┐
  Ctrl+C ────────▶ SIGINT ──▶ SDL's handler sets a flag ────────────────┼──▶ SDL_EVENT_QUIT
  kill ──────────▶ SIGTERM ─▶ SDL's handler sets a flag ────────────────┘
```

When the last window asks to close, SDL follows the close request with a quit event. When SDL's video starts, it installs handlers for `SIGINT` and `SIGTERM` that do exactly what chapter 06's handler did: set a flag, because a handler can run in the middle of anything. The next `SDL_PollEvent` sees the flag and adds a quit event to the queue. So chapter 06's handler goes, and the loop's one way out is `platform::handle_events` returning `false`.

(SDL installs its handlers only where a signal still does its default thing, ending the process. A program that already handles `SIGINT` keeps its own handler.)

SDL queues events as the display server sends them, and `SDL_PollEvent` hands them out one at a time. The engine empties the queue at the start of every frame, the first step of the frame in [plan §4.3](plan.md#43-the-frame). It has to poll even when it ignores everything but quitting: the display server checks that each window still answers, and SDL answers only while it's being polled. A program that stops polling looks frozen, and desktops offer to kill a window that doesn't answer.

The plan lists other events for the engine to handle itself: a resize, a gamepad plugged in, the window losing focus. Resizing waits for chapter 11, which can scale the picture to any size, and gamepads for chapter 14. Pausing when the window loses focus waits for chapter 21, which builds pausing. Chapter 21 also has to weigh it against how you work: you edit the game's code in another window, and a game that paused whenever you did that couldn't keep playing while you change it.

### Something to look at

A Wayland compositor shows a window only once the window has handed it a picture. An SDL window that's been created and never drawn into doesn't appear at all. On this machine it wasn't even in Hyprland's list of windows. Chapter 08 draws with OpenGL. Until then, `platform::present` gives the window a picture the simplest way SDL has: the **window surface**, a picture of the window in ordinary memory. `present` fills it with one dark blue and tells SDL to put it on the screen.

That's a stopgap, like chapter 06's sleep. A window can't have both a surface and OpenGL, so chapter 08 replaces it.

The window opens at 768×720, three times the 256×240 screen the game will draw ([plan §4.5](plan.md#45-rendering)), so it's the same shape. It isn't resizable until chapter 11. Hyprland, which tiles resizable windows, floats a window that can't be resized, so on your desktop it opens floating at its own size.

### A leak that isn't momo's

The first run of the debug build with the window ended like this, when it quit:

```
==9772==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 256 byte(s) in 1 object(s) allocated from:
    #0 0x7f3624b2b161  (/usr/lib/libasan.so.8+0x12b161) (BuildId: b8a4241051a1621937fdc46e867ba7ecb56d96ea)
    #1 0x7b36181df3ce  (<unknown module>)
    #2 0x7b36181df8b7  (<unknown module>)
    #3 0x7f3623a9d79b  (/usr/lib/libc.so.6+0x9d79b) (BuildId: d320029ad1a23be2f4bce117fd321cbd23a2014b)
    #4 0x7f3623a9d818 in pthread_once (/usr/lib/libc.so.6+0x9d818) (BuildId: d320029ad1a23be2f4bce117fd321cbd23a2014b)
    #5 0x7b3618750673  (<unknown module>)
    ...
    #15 0x7f362312d8b9  (<unknown module>)
    #16 0x7f362373b427  (/usr/lib/libSDL3.so.0+0x13b427) (BuildId: 76a5ede7c37e79ea60185922de846c5465f18773)
    ...
    #23 0x7f36237656ff  (/usr/lib/libSDL3.so.0+0x1656ff) (BuildId: 76a5ede7c37e79ea60185922de846c5465f18773)
    #24 0x7f36252d6886 in platform::present() engine/platform.cpp:74
    #25 0x7f36252d62f0 in momo::run() engine/momo.cpp:213
    #26 0x5589841ee181 in main host/main.cpp:7
    ...

SUMMARY: AddressSanitizer: 256 byte(s) leaked in 1 allocation(s).
```

and with an exit code of 1, as if momo had a bug. When a program exits, LeakSanitizer looks through its memory for pointers to every block `malloc` handed out, and reports each block nothing points to anymore. Read from the bottom, this block was allocated on the first `present`, inside SDL, and inside code that LeakSanitizer can't name: `<unknown module>` means the code was no longer loaded when the report was written.

That code is the graphics driver, Mesa. On Wayland, SDL puts the window surface on the screen through the GPU, so even a solid color loads the driver. The driver allocates 256 bytes once (the `pthread_once` in the stack runs a function only the first time it's reached), and only the driver's own global variables point to them. When momo quits, `SDL_Quit` unloads the driver. Its globals are unmapped with it, the pointer to the block disappears, and the block looks leaked. To check: keep the driver loaded for the whole run, by preloading it, and the report goes away (exercise 4).

momo's own memory comes from the arenas, not `malloc`, so whatever LeakSanitizer finds was allocated by a library. Still, a debug build that fails on every quit trains you to ignore the failure:

| Option | The catch |
|---|---|
| Turn leak checking off | Nothing would catch game code that calls `malloc` and forgets to free, against the rules |
| Skip `SDL_Quit`, so the driver stays loaded | Hides the report by accident, and breaks the rule that setup and teardown are explicit calls |
| **Tell LeakSanitizer to skip leaks whose call stack passes through `libSDL3.so`** | It would also skip a real leak inside SDL. That's SDL's to fix, not momo's. |

The usual way to give LeakSanitizer a suppression list is a file, named in the environment variable `LSAN_OPTIONS` on every run. There's a second way that needs nothing at run time: if the program defines a function called `__lsan_default_suppressions`, LeakSanitizer calls it and reads the list it returns. It has to be in the executable. The sanitizer library has its own version of the function, which returns an empty list, and the loader finds a name in the executable before it looks in any library, so the executable's version replaces the library's. A version in `libmomo.so` would come after the sanitizer library's, and never be found. So the host gains a few lines, in the debug build only.

LeakSanitizer then lists what it skipped, every time momo quits. That's useful: a suppression you can't see is one you forget, and if the number of bytes ever grows, something new is leaking through SDL.

### The build

SDL3 is installed on the system ([plan §7](plan.md#7-build-dependencies-repo)), and `pkg-config` prints the flags to compile and link against it. Only the engine gets them. The host and the game never call SDL themselves, and `game.so` can't: it links only against `libmomo.so`, and `-Wl,--no-undefined` (chapter 02) turns a call to a library it doesn't link into an error.

The tests don't need SDL either, though `libmomo.a` now holds two object files that call it, `momo.o` and `platform.o`. Chapter 03 linked the tests against the archive so that each test gets only the object files it uses, and none of them uses those two.

## The code

### The interface: `engine/platform.h`

`engine/platform.h:16`

```cpp
namespace platform {

bool init(const char* title, i32 width, i32 height);
void shutdown();
bool handle_events();
void present();

void* reserve_memory(i64 size);
void release_memory(void* memory, i64 size);
const char* executable_folder();
i64 file_time(const char* path);
void* open_library(const char* path);
void* find_function(void* library, const char* name);
void close_library(void* library);
void sleep_ms(i32 milliseconds);

} // namespace platform
```

That's the whole list, without its comments, which say what each function returns and when. The file's opening comment states the rules from Design: only `platform.cpp` includes SDL or OS headers, nothing here is `MOMO_API`, failures are logged, and paths are C strings. The second group works before `init` and after `shutdown`, because the engine reserves its memory and loads `game.so` before it opens the window.

The library handle is a `void*`, as `dlopen`'s was. The header can't name SDL's type for it, `SDL_SharedObject*`, without including SDL.

### Starting SDL and the window

`engine/platform.cpp:13`

```cpp
static SDL_Window* window;
```

The module's one piece of state. The engine never reloads, so it can live in a file-scope variable, like `engine` in `momo.cpp` ([plan §4.1](plan.md#41-three-binaries-and-hot-reload)).

`engine/platform.cpp:17`

```cpp
bool init(const char* title, i32 width, i32 height)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("can't start SDL: %s", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow(title, width, height, 0);
    if (!window) {
        LOG_ERROR("can't open a window: %s", SDL_GetError());
        return false;
    }

    i32 version = SDL_GetVersion();
    LOG_INFO("SDL %d.%d.%d, video driver %s, window %dx%d", SDL_VERSIONNUM_MAJOR(version),
             SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version),
             SDL_GetCurrentVideoDriver(), width, height);
    return true;
}
```

In SDL3, functions that can fail return `bool` (`true` on success), and `SDL_GetError` says why the last one failed. That's momo's own convention for expected failures ([plan §3.3](plan.md#33-errors-and-strings)), so wrapping them is one `LOG_ERROR` each. Starting the video subsystem also starts SDL's events, and that's when SDL installs its `SIGINT` and `SIGTERM` handlers. The `0` is the window's flags: none, so it isn't resizable. The log line names the **video driver**, the part of SDL that talks to the display server. SDL picks one when it starts: `wayland` on your desktop.

`shutdown` (`engine/platform.cpp:41`) destroys the window and calls `SDL_Quit`, which also restores the signal handlers.

### Events

`engine/platform.cpp:48`

```cpp
bool handle_events()
{
    SDL_Event event = {};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            LOG_INFO("quit requested");
            return false; // the rest of the queue doesn't matter now
        }
    }
    return true;
}
```

`SDL_PollEvent` reads whatever the display server has sent, runs SDL's own bookkeeping, including the signal flag, and fills in `event` if the queue has one, returning `false` when it's empty. `SDL_Event` is a union: `type` says which event it is, and which of its other members to read. Window events, key presses and mouse motion all arrive here too, and fall through the loop unread. Chapter 14 reads the keys, and exercise 5 prints everything.

### A picture

`engine/platform.cpp:64`

```cpp
void present()
{
    SDL_Surface* surface = SDL_GetWindowSurface(window);
    if (!surface)
        return;
    SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGB(surface, 0x18, 0x20, 0x48));
    SDL_UpdateWindowSurface(window);
}
```

`SDL_GetWindowSurface` returns the window's picture in memory, and SDL makes a new one whenever the window changes size, so `present` asks for it every frame instead of keeping it. `SDL_MapSurfaceRGB` turns a color into the surface's own pixel format, `SDL_FillSurfaceRect` fills the whole surface with it (`nullptr` means no rectangle, so all of it), and `SDL_UpdateWindowSurface` puts it on the screen. A failure isn't logged, unlike everywhere else: it would repeat 60 times a second, and the window would just keep its last picture. The comment in the file says so, and that chapter 08 replaces all of it.

### Memory and libraries

`engine/platform.cpp:83`

```cpp
void* reserve_memory(i64 size)
{
    void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        LOG_ERROR("can't reserve %lld MB of memory", size / MB);
        return nullptr;
    }
    return memory;
}
```

Chapter 04's `mmap`, moved here unchanged. It and the `munmap` in `release_memory` are the only OS calls in momo that don't go through SDL, and the comment says so.

`engine/platform.cpp:120`

```cpp
void* open_library(const char* path)
{
    void* library = SDL_LoadObject(path);
    if (!library)
        LOG_ERROR("%s", SDL_GetError());
    return library;
}
```

The comment above `SDL_LoadObject` in the file records that, on Linux, SDL opens the library with `RTLD_NOW`, so chapter 02's rule still holds: a missing engine function fails here, when the game loads, not in the middle of a frame. Run it checks that in gdb, since momo's own code no longer says it. `find_function` and `close_library` wrap `SDL_LoadFunction` and `SDL_UnloadObject` the same way. `executable_folder` and `file_time` (`engine/platform.cpp:101`) are one SDL call each. `SDL_GetPathInfo` reports the modification time in nanoseconds since 1970, the same unit chapter 06 built from `stat` by hand.

### The engine: `engine/momo.cpp`

The six system headers are gone. `momo.cpp` includes `momo.h`, `platform.h` and `<stddef.h>`, for `max_align_t`, which is standard C on every OS.

`engine/momo.cpp:93`

```cpp
static bool find_game()
{
    // ".../build/debug/" -> ".../build/debug/game.so"
    const char* folder = platform::executable_folder();
    if (!folder)
        return false; // already logged
    engine.game_path = str::fmt(&engine.arena, "%sgame.so", folder);
    return true;
}
```

SDL's folder already ends with a `/`, so the format has none. The text belongs to SDL, so the engine copies what it keeps into its own arena, as before. `load_game` and `unload_game` (`engine/momo.cpp:104`) call `platform::file_time`, `open_library`, `find_function` and `close_library` where they called `stat`, `dlopen`, `dlsym` and `dlclose`, and lose their own `LOG_ERROR` lines, since the platform functions log. The steps and their order stayed the same: hot reload didn't change.

`engine/momo.cpp:188`

```cpp
i32 run()
{
    if (!init_memory())
        return 1;
    if (!find_game())
        return 1;
    if (!load_game())
        return 1;
    LOG_INFO("loaded %.*s", engine.game_path.len, engine.game_path.data);

    if (!platform::init("momo", WINDOW_WIDTH, WINDOW_HEIGHT))
        return 1;

    start_game();
    while (!engine.quit_requested) {
        if (!platform::handle_events())
            break; // the window was closed, or Ctrl+C
        reload_game_if_changed();
        arena::reset(&engine.frame); // last frame's memory is free again
        if (engine.game_lib)
            engine.game.update(engine.state);
        platform::present();
        engine.frame_index++;

        platform::sleep_ms(16);
    }

    if (engine.game_lib) {
        engine.game.shutdown(engine.state);
        unload_game();
    }
    platform::shutdown();
    // ...log the frame count and the arenas' peaks, as before...
```

The window opens after `game.so` loads, so a missing or broken game fails before any window flashes up. It opens before `start_game`, though: from chapter 12 on, the game's `init` loads textures, which needs OpenGL, which needs the window. `WINDOW_WIDTH` and `WINDOW_HEIGHT` (`engine/momo.cpp:46`) are `3 * 256` and `3 * 240`.

The frame now follows the plan's order, as far as it goes: events, the game's update, then `present`. The `interrupted` flag and its `interrupted` log line are gone. Quitting through SDL logs `quit requested`, from `handle_events`.

### The host: `host/main.cpp`

`host/main.cpp:10`

```cpp
#ifdef __SANITIZE_ADDRESS__

extern "C" const char* __lsan_default_suppressions()
{
    return "leak:libSDL3.so\n";
}

#endif
```

GCC defines `__SANITIZE_ADDRESS__` when it compiles with `-fsanitize=address`, so the release build doesn't get this. The list is one line per suppression. `leak:` followed by a name skips any leak with a function, a source file or a library by that name anywhere in its call stack, and `libSDL3.so` is a library. `extern "C"` keeps the name unmangled, so LeakSanitizer can find the function by it. The comment in the file repeats why the leak isn't momo's, and why the function has to be in the executable.

### The Makefile

`Makefile:40`

```make
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)
ENGINE_CXXFLAGS := $(LIB_CXXFLAGS) $(SDL_CFLAGS)
```

`$(shell ...)` runs a command when make reads the Makefile, and its output becomes the variable's value. Here `--libs` prints `-lSDL3`. `--cflags` prints nothing, because SDL's headers are in `/usr/include`, where the compiler already looks, but on another system it could print an `-I`, so the engine's compile rule uses `ENGINE_CXXFLAGS` anyway. (The empty value leaves two spaces before `-MMD` in the engine's compile lines.) The engine's link rule adds `$(SDL_LIBS)` after the object files, and `compile_commands.json` gives engine files `ENGINE_CXXFLAGS`, so clangd sees the same flags the build uses.

## Run it

### A window

```
$ make
$ ./build/debug/momo
engine/momo.cpp:72: reserved 64 MB at 0x7b2ff4400000
engine/momo.cpp:198: loaded /home/mairuu/repos/momo/build/debug/game.so
engine/platform.cpp:35: SDL 3.4.18, video driver wayland, window 768x720
game/game.cpp:27: init
game/game.cpp:51: frame    0  |B...............|  x =   1
game/game.cpp:51: frame   30  |.B..............|  x =  31
game/game.cpp:51: frame   60  |...B............|  x =  61
```

A dark blue window titled `momo` opens, floating, and the bat carries on in the terminal. The engine opened the window after loading the game and before the game's `init`.

Press Ctrl+C in the terminal:

```
game/game.cpp:51: frame  150  |.........B......|  x = 151
^Cengine/platform.cpp:57: quit requested
game/game.cpp:56: shutdown, with the bat at x = 151
engine/momo.cpp:228: quit after 151 frames
engine/momo.cpp:78: permanent arena: at most 8 bytes used of 16 MB
engine/momo.cpp:78: stage     arena: at most 0 bytes used of 32 MB
engine/momo.cpp:78: frame     arena: at most 0 bytes used of 8 MB
engine/momo.cpp:78: engine    arena: at most 44 bytes used of 8 MB
-----------------------------------------------------
Suppressions used:
  count      bytes template
      1        256 libSDL3.so
-----------------------------------------------------
```

The `SIGINT` became SDL's quit event, `handle_events` saw it, and the game shut down as in chapter 06. The table at the end is LeakSanitizer's: one leak skipped, the driver's 256 bytes, because of the `libSDL3.so` line. The exit code is 0 (`echo $?`).

Start it again, and close the window instead, the way you close any window: Super+Shift+Q on your desktop. No `^C` this time, and the same ending:

```
engine/platform.cpp:57: quit requested
game/game.cpp:56: shutdown, with the bat at x = 83
engine/momo.cpp:228: quit after 83 frames
...
```

### Hot reload still works

Start the game, change `BAT_SPEED` to 2 in `game/game.cpp`, and `make` in a second terminal, as in chapter 06:

```
game/game.cpp:51: frame  120  |.......B........|  x = 121
engine/momo.cpp:177: reloaded /home/mairuu/repos/momo/build/debug/game.so
game/game.cpp:51: frame  150  |..........B.....|  x = 164
game/game.cpp:51: frame  180  |..............B.|  x = 224
```

The reload now goes through `SDL_UnloadObject` and `SDL_LoadObject`, and nothing else changed. The window stays open through it. It belongs to the engine, which never reloads. Quit, and put the speed back with `git checkout game/game.cpp`.

### The game can't call SDL

Try to break the rule. Add `#include <SDL3/SDL.h>` under `#include "momo.h"` in `game/game.cpp`, and `LOG_INFO("%llu ms", (u64)SDL_GetTicks());` in `update`, after `i32 frame = momo::frame_index();`. Then `make`:

```
g++ -std=c++20 ... -c game/game.cpp -o build/debug/obj/game/game.o
g++ -fsanitize=address,undefined -shared -Wl,--no-undefined build/debug/obj/game/game.o -Lbuild/debug -lmomo -Wl,-rpath,'$ORIGIN' -o build/debug/game.so.tmp
/usr/bin/ld: build/debug/obj/game/game.o: in function `game::update(GameState*)':
/home/mairuu/repos/momo/game/game.cpp:47:(.text+0x568): undefined reference to `SDL_GetTicks'
collect2: error: ld returned 1 exit status
make: *** [Makefile:102: build/debug/game.so] Error 1
```

The compile works: SDL's headers are in `/usr/include`, where any file can include them. The link doesn't. `SDL_GetTicks` is in `libSDL3.so`, and `game.so` links only `libmomo.so`. The rule is enforced by the build, not just written down. Put the game back with `git checkout game/game.cpp`.

`readelf -d` lists the libraries a binary asks the loader for, its `NEEDED` entries:

```
$ readelf -d build/debug/game.so | grep -E 'momo|SDL'
 0x0000000000000001 (NEEDED)             Shared library: [libmomo.so]
$ readelf -d build/debug/libmomo.so | grep -E 'momo|SDL'
 0x0000000000000001 (NEEDED)             Shared library: [libSDL3.so.0]
```

The game needs the engine, and only the engine needs SDL. The test programs need neither: `ldd build/debug/tests/arena_test | grep SDL` prints nothing.

### No display

Over ssh, or anywhere without a display server, there's no window to open. Pretend by unsetting the two variables that tell programs where the display server is:

```
$ env -u WAYLAND_DISPLAY -u DISPLAY ./build/debug/momo
engine/momo.cpp:72: reserved 64 MB at 0x7b9563e00000
engine/momo.cpp:198: loaded /home/mairuu/repos/momo/build/debug/game.so
engine/platform.cpp:23: error: can't start SDL: No available video device
```

`SDL_Init` failed, the log says why, and momo exits with 1. The game was loaded but never started.

### Look inside: SDL calls `dlopen`

Check that SDL really loads `game.so` with `dlopen` and `RTLD_NOW`. As in chapter 06, `start` first. `dlopen` is in the C library, which isn't loaded until the program starts:

```
$ gdb -q ./build/debug/momo
(gdb) start
...
Temporary breakpoint 1, main () at host/main.cpp:7
7	    return momo::run();
(gdb) break dlopen
Breakpoint 2 at 0x7ffff689452c (2 locations)
(gdb) continue
Continuing.
engine/momo.cpp:72: reserved 64 MB at 0x7bffef400000

Breakpoint 2.2, 0x00007ffff7856fb9 in dlopen () from /usr/lib/libasan.so.8
(gdb) backtrace
#0  0x00007ffff7856fb9 in dlopen () from /usr/lib/libasan.so.8
#1  0x00007ffff657b4c9 in ?? () from /usr/lib/libSDL3.so.0
#2  0x00007ffff7f89bc9 in platform::open_library (
    path=0x7bfff2c00000 "/home/mairuu/repos/momo/build/debug/game.so")
    at engine/platform.cpp:125
#3  0x00007ffff7f88c19 in momo::load_game () at engine/momo.cpp:113
#4  0x00007ffff7f89229 in momo::run () at engine/momo.cpp:196
#5  0x0000555555555182 in main () at host/main.cpp:7
```

Read up from the bottom: `run` called `load_game`, which called `platform::open_library`, which called into SDL, which called `dlopen`. The `??` is SDL's code for `SDL_LoadObject`: the system's SDL has no debug information, and gdb can't name its internal functions. There were two `dlopen`s to break on, "2 locations": the C library's, and AddressSanitizer's, which stands in front of it to keep track of what's loaded. It's the one that stopped.

What did SDL pass? Without debug information, gdb doesn't know `dlopen`'s arguments by name, but on x86-64 Linux a function's first two arguments arrive in the registers `rdi` and `rsi`:

```
(gdb) print (char*)$rdi
$1 = 0x7bfff2c00000 "/home/mairuu/repos/momo/build/debug/game.so"
(gdb) print $rsi
$2 = 2
(gdb) quit
A debugging session is active.

	Inferior 1 [process 10453] will be killed.

Quit anyway? (y or n) y
```

The first is the path. The second is the flags, and `/usr/include/bits/dlfcn.h` defines `RTLD_NOW` as 2 and `RTLD_LOCAL` as 0, so SDL passed `RTLD_NOW | RTLD_LOCAL`. That's what chapter 02 asked for. `RTLD_LOCAL` is the default when neither it nor `RTLD_GLOBAL` is given. As in chapter 06, quit at the breakpoint, so LeakSanitizer doesn't complain about running under gdb.

### The tests

```
$ make test
arena_test
  ...
  8 passed
base_test
  ...
  5 passed
str_test
  ...
  10 passed
```

As before. The platform layer has no tests of its own: it's the part of momo that talks to things outside it, so Run it checks it.

## Recap

```
momo/
  Makefile          + SDL3's flags from pkg-config, for the engine only
  engine/
    platform.h      new: the window, events, memory, files and libraries, for the engine only
    platform.cpp    new: all of it on SDL3, except mmap
    momo.h          + run() also quits when the window is closed
    momo.cpp        + opens the window, handles events, presents each frame;
                      no system headers left; Ctrl+C handler gone
  host/
    main.cpp        + debug build: LeakSanitizer skips leaks that pass through SDL
```

- `platform::` is the engine's one connection to the OS. Only `platform.cpp` includes SDL or an OS header, it uses SDL wherever SDL has the function. Reserving memory, with `mmap`, is the one exception.
- Nothing in `platform::` is `MOMO_API`, and `game.so` doesn't link SDL, so a game that calls SDL fails to link.
- Closing the window, Ctrl+C and `kill` all become `SDL_EVENT_QUIT`, the loop's one way out. SDL's signal handler works like chapter 06's: it sets a flag.
- The engine polls events at the start of every frame, which also keeps the window answering the display server.
- A Wayland window appears only once it has a picture, so `present` fills the window surface with one color until chapter 08 draws with OpenGL.
- LeakSanitizer reported the graphics driver's memory as leaked, because `SDL_Quit` unloads the driver. The host's `__lsan_default_suppressions` skips leaks through `libSDL3.so`, and LeakSanitizer lists what it skipped.

The plan records the platform layer in [§4.4](plan.md#44-platform-and-input), the suppression in [§4.10](plan.md#410-debug-tools-and-tests), and the SDL flags in [§7](plan.md#7-build-dependencies-repo). The outline now says chapter 21 decides whether losing focus pauses the game.

Chapter 08 replaces the solid blue with OpenGL 4.6: an OpenGL window from SDL, functions loaded with glad, and a clear color from the GPU.

## Exercises

Undo each change before the next exercise: `git checkout -- engine game host` puts those back.

1. **No picture.** In `run`, comment out `platform::present();` and `make`. Is there a window? Does the bat still fly, and can you still quit with Ctrl+C? Then try it with `SDL_VIDEO_DRIVER=x11 ./build/debug/momo`, which runs momo as an X11 program through XWayland. Does that help, and why might it not?
2. **Another driver.** Put `present` back and run `SDL_VIDEO_DRIVER=x11 ./build/debug/momo`. What does the SDL line say? The window comes out half the size it did on Wayland. `hyprctl monitors` shows your display's `scale`: what might that have to do with it? (Chapter 11 comes back to this.) Does LeakSanitizer's table appear when you quit?
3. **Escape quits.** In `handle_events`, also return `false` for `SDL_EVENT_KEY_DOWN` when `event.key.key == SDLK_ESCAPE`. Why is it fine for the engine to read a key here, when the game must never see real keys ([plan §4.4](plan.md#44-platform-and-input))?
4. **Whose leak?** In `host/main.cpp`, make `__lsan_default_suppressions` return `""`, rebuild, run, and quit: you get the report from the Design section. While momo runs, find the driver with `grep gallium /proc/$(pidof momo)/maps`. Then run it with the driver preloaded, so it's loaded before `main` and never unloaded: `LD_PRELOAD="/usr/lib/libasan.so.8 <the driver's path>" ./build/debug/momo`. Why does the report go away? Why does `libasan.so.8` have to come first in the list?
5. **Every event.** In `handle_events`, log every event with `LOG_INFO("event 0x%x", event.type);`, and look the numbers up in `/usr/include/SDL3/SDL_events.h`. Which events arrive when the window opens? When you move the mouse over it, press a key, or switch to another workspace and back?
