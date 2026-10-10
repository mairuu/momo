# 08 · OpenGL 4.6

> **Goal:** the window's color comes from the GPU. momo asks SDL for an OpenGL 4.6 context, loads OpenGL's functions with glad, and clears the window with one direct-state-access call. When momo calls OpenGL wrongly, OpenGL says what went wrong in words, and momo stops in gdb at the line that did it.

## The problem

Chapter 07's window shows one dark blue, from a stopgap: `platform::present` fills the **window surface**, a picture of the window in ordinary memory, and hands it to SDL. Chapter 10 needs to draw hundreds of sprites a frame, and that's the GPU's job, through OpenGL ([plan §4.5](plan.md#45-rendering)). A window can't have both a surface and OpenGL, so the surface goes.

Three things stand between momo and its first OpenGL call:

- **A context.** OpenGL draws into a window through a context: all of OpenGL's state for that window, plus the connection to the driver. Someone has to create it, with the right version.
- **The functions.** `glClear` isn't in any library momo links. Every OpenGL function is the driver's, and has to be looked up by name at run time.
- **Knowing when a call is wrong.** A wrong OpenGL call doesn't crash or fail. It quietly does nothing and sets an error flag, which nobody reads unless they ask.

You've done all of this before, in LearnOpenGL's first chapters, with GLFW and glad. This chapter follows the same steps with SDL instead of GLFW, and changes three things: a newer OpenGL, direct state access instead of binding, and a debug callback instead of the error flag.

| LearnOpenGL's "Hello Window" | momo |
|---|---|
| `glfwWindowHint(...)`: 3.3, core | `SDL_GL_SetAttribute(...)`: 4.6, core, debug |
| `glfwCreateWindow`, `glfwMakeContextCurrent` | `SDL_CreateWindow` with `SDL_WINDOW_OPENGL`, `SDL_GL_CreateContext` |
| `gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)` | `gladLoadGL(platform::gl_function)` |
| `glClearColor(...)`, then `glClear(GL_COLOR_BUFFER_BIT)` | `glClearNamedFramebufferfv(0, GL_COLOR, 0, color)` |
| `glfwSwapBuffers` | `SDL_GL_SwapWindow` |

## Design

### The context

A context holds everything OpenGL knows: its settings, the objects you've made (buffers, textures, shaders), and which window it draws into. OpenGL calls don't name a context. Each call goes to the context that is **current** on the calling thread, and SDL makes a new context current as it creates it. momo has one window, one context and one thread, so the context is made once and stays current until momo quits.

What kind of context it gets is decided before the window opens, through SDL's attributes:

- **Version 4.6**, the last version of OpenGL, from 2017. The plan picked it for direct state access (4.5) and debug output (4.3).
- **The core profile**, which leaves out everything older versions deprecated: `glBegin`/`glEnd`, the fixed-function lighting, the matrix stack. Its counterpart, the compatibility profile, keeps all of it. Exercise 1 calls one deprecated function under each profile.
- **A debug context**, which promises to explain its errors (below).
- **No depth buffer.** SDL asks for a 16-bit one unless told otherwise. momo draws in layer order, back to front ([plan §4.5](plan.md#45-rendering)), so it never needs one.

### Who owns what

Creating the context is SDL's job, and chapter 07 decided that only `platform.cpp` calls SDL. Drawing is OpenGL's job, and the plan wants all of it inside a renderer module, so a later port to another graphics API stays contained. So the work splits in two:

| Option | The catch |
|---|---|
| The renderer makes the context itself | It would call SDL, breaking chapter 07's one rule |
| `platform::` does everything, OpenGL calls included | From chapter 10 on, `platform.cpp` would be mostly a renderer, mixed in with events and files |
| **`platform::` makes the context and looks functions up. A new module, `renderer::`, makes every OpenGL call.** | `platform::init` always opens an OpenGL window. A port to SDL_GPU would change both files. |

momo takes the last one. Each module keeps to one library: only `platform.cpp` includes SDL, and only `renderer.cpp` includes OpenGL's header. Between them is one function, `platform::gl_function(name)`, which returns the address of the OpenGL function by that name.

### Loading the functions

`libmomo.so` links SDL and nothing else from the system. It doesn't link an OpenGL library at all. When SDL opens an OpenGL window, it loads the system's OpenGL libraries itself, and those load the driver: Mesa, on your machine. The functions are the driver's, and the only way to reach them is to ask by name, one at a time, for an address. In C++ that's a function pointer, which has to be cast to the function's real type before it's called.

OpenGL 4.6 has 657 functions. Writing a typedef, a pointer and a lookup for each is typing, not learning. A **loader** does it. The plan picked glad, the loader LearnOpenGL uses: a generator that reads Khronos's registry of every OpenGL function and constant (`gl.xml`) and writes C code for exactly the version you ask for:

- `include/glad/gl.h` declares every 4.6 constant, and a function pointer for every 4.6 function, under a macro with the function's name. `glClear` is really `glad_glClear`.
- `src/gl.c` defines the pointers, and `gladLoadGL`, which asks for every function and fills them in. It reads the context's version first and only asks for what that version has.

`gladLoadGL` needs one thing from outside: a function that turns a name into an address. That's `platform::gl_function`, which is SDL's `SDL_GL_GetProcAddress`. glad expects it to return a `void (*)()`, a pointer to a function that takes nothing and returns nothing. That type stands for "some function", like `void*` stands for "some data", and glad casts each address to its real type as it stores it. `platform.h` names that type `GlFunction`, so the renderer can hand `platform::gl_function` straight to glad.

glad's pointers are global variables, inside `libmomo.so`. Plan §3.2's ban on globals is a rule for `game.so`, which reloads. The engine never does, so its globals are safe. They're hidden too: glad is compiled with `-fvisibility=hidden`, like the rest of the engine, so the game can't call OpenGL even if it includes glad's header. It draws through the renderer, from chapter 10 on.

### Generating glad, once

glad 2 is a Python program (`pip install glad2`). LearnOpenGL used glad 1, through its website. It needs to run only when momo wants a different version of OpenGL, which may be never:

| Option | The catch |
|---|---|
| Run glad in the Makefile on every clean build | Building momo would need Python and glad installed |
| **Run it once, by hand, and commit what it writes** | Changing what it generates means running it again |

momo commits the output to `third_party/glad/`, with glad's license and a README that records the exact command and glad's version, 2.0.8. The command asks for OpenGL 4.6 core and no extensions, since everything momo uses is core in 4.6. `--reproducible` makes glad use the copy of `gl.xml` it was released with, instead of downloading the newest, so the same command always writes the same files.

glad's code is C, the first C code in momo, so it's compiled as C, by `gcc` with its own flags. It compiles cleanly with `-Wall -Wextra`, so it gets them too.

### Direct state access

LearnOpenGL makes a vertex buffer like this:

```cpp
unsigned int vbo;
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
```

`glBufferData` fills a buffer, but which one? The line doesn't say. It fills whichever buffer was last bound to `GL_ARRAY_BUFFER`, by this code or by any other code that ran before it. The bind is a hidden global, and every call after it depends on it. That's the opposite of momo's test for a line of code, "can I tell what this line does by reading only this line?" ([plan §3](plan.md#3-language-orthodox-c20-built-with-gcc)), and the source of a classic OpenGL bug: a function binds something, and quietly breaks the next function that assumed its own binding was still there.

OpenGL 4.5 added **direct state access** (DSA): versions of those functions that take the object as an argument.

```cpp
u32 vbo = 0;
glCreateBuffers(1, &vbo);
glNamedBufferData(vbo, sizeof(vertices), vertices, GL_STATIC_DRAW);
```

Nothing is bound, and each line says what it changes, the way `f(&x)` does in momo's own code. `glCreateBuffers` also makes the buffer right away, where `glGenBuffers` only reserves a number, and the buffer appears at its first bind.

This chapter draws nothing yet. Its one drawing call is a clear, and that has a DSA version too:

```cpp
// Bind, then edit: glClearColor draws nothing, it only sets the color
// that the next glClear uses, on whichever framebuffer is bound.
glClearColor(0.06f, 0.22f, 0.25f, 1.0f);
glClear(GL_COLOR_BUFFER_BIT);

// DSA: which framebuffer, which buffer in it, and the color, in one call.
glClearNamedFramebufferfv(0, GL_COLOR, 0, CLEAR_COLOR);
```

Framebuffer 0 is the window's own. `GL_COLOR` and the `0` after it pick its first color buffer, and the color is four floats: red, green, blue and alpha, from 0 to 1. DSA doesn't get rid of binding completely: drawing still uses the bound shader and vertex array, which chapter 10 deals with.

### When OpenGL says no

The old way to find a mistake is `glGetError`. A wrong call sets an error flag and returns as if nothing happened, and `glGetError` reads the flag and clears it. You'd call it after every OpenGL call, usually through a macro, and it hands back a number such as `GL_INVALID_ENUM`, without saying which call or why.

OpenGL 4.3 added **debug output**: you give OpenGL a callback, and the driver calls it with a message in words. That's the plan's pick. What momo does with a message depends on what it says:

| Message | momo |
|---|---|
| An error from the API: a call was wrong, and did nothing | Logs it with `LOG_ERROR`, then `ASSERT`s |
| Anything else: deprecated, undefined behavior, performance, portability, and errors from elsewhere | Logs it at a level that matches its severity: high is `LOG_ERROR`, medium is `LOG_WARN`, low is `LOG_INFO` |
| Severity `NOTIFICATION`: the driver saying what it did, such as where it put a buffer | Turned off. Some drivers send one for every buffer. |

An error in a call is momo calling OpenGL wrongly. That's a bug, and bugs stop momo where they happen ([plan §3.3](plan.md#33-errors-and-strings)). A logged line alone would scroll away at 60 frames a second.

A message also says where it came from, and that turned out to matter. While this chapter was being written, the callback stopped at every message of type `GL_DEBUG_TYPE_ERROR`, until a test compiled a fragment shader with a missing semicolon:

```
engine/renderer.cpp:51: error: OpenGL error: 0:3(29): error: syntax error, unexpected '}', expecting ',' or ';'
```

Mesa reports a shader that won't compile through the callback too, as an error. Plan §3.3 uses exactly that as its example of an expected failure: chapter 10's renderer will check whether each shader compiled, log why not, and carry on, and an `ASSERT` would stop it first. The message's source was `GL_DEBUG_SOURCE_SHADER_COMPILER`, though, where a wrong call's is `GL_DEBUG_SOURCE_API`. So the `ASSERT` checks both: only an error whose source is the API stops momo. The shader compiler's message is still logged.

"Where they happen" needs one more setting. A driver is allowed to run OpenGL calls after they return, and Mesa does: on your machine it queues them and runs them on a thread of its own. A wrong call is then only noticed when its turn in the queue comes, and the callback runs from wherever the queue happened to be emptied, far from the line that made the mistake. **Synchronous** debug output (`GL_DEBUG_OUTPUT_SYNCHRONOUS`) asks for every message to arrive during the call that caused it. Run it shows the difference in gdb.

A non-debug context is allowed to send no messages at all, which is why `platform::init` asks for a debug context, and why the renderer checks that it got one. A debug context can cost a little speed, since the driver checks more. momo asks for one in both builds, just as `ASSERT` is on in both ([plan §7](plan.md#7-build-dependencies-repo)). It makes a handful of OpenGL calls a frame, so the checking can't add up to much.

### Swapping, and no vsync yet

OpenGL draws into a **back buffer**, a picture the window doesn't show yet. `platform::present` now calls `SDL_GL_SwapWindow`, which hands the finished back buffer to the display server to show and gives OpenGL a free buffer for the next frame. You never see a frame half drawn.

Swapping can also wait for the display to finish showing a picture (**vsync**), which the swap interval controls: 0 doesn't wait, 1 waits. Left alone, the interval is whatever the video driver starts with. On this machine it was 0 on Wayland and 1 under X11. momo sets 0, so that the loop's 16 ms sleep alone keeps setting the pace on either one, as in chapter 07. Measured on Wayland, frames take 16.4 ms with the interval at 0 or 1: at this pace, waiting for the display costs nothing. It starts to matter once the sleep is gone, which exercise 3 tries. Chapter 09 replaces the sleep with a clock, so chapter 09 decides vsync.

### The color

`begin_frame` clears to a dark teal, so you can tell the GPU's picture from chapter 07's blue at a glance. (LearnOpenGL's first window is a similar color.) For now that's the whole frame: clear, then swap.

## The code

### glad: `third_party/glad/`

```
third_party/glad/
  include/glad/gl.h         every OpenGL 4.6 constant and function pointer
  include/KHR/khrplatform.h Khronos's sized types, which gl.h uses
  src/gl.c                  the pointers, and gladLoadGL
  README.md                 glad's version, the command, the licenses
  LICENSE                   glad's license file
```

Generated, not written, so the book doesn't quote it. The README has the command:

```
python -m glad --reproducible --api gl:core=4.6 --extensions '' --out-path third_party/glad c
```

The first lines of `gl.h` record the same command, and that the generated code is public domain, apart from the parts that come from Khronos's specification, which are under Apache 2.0. `LICENSE` is the license file from the glad package, which covers glad itself (MIT) and the specifications.

### The context: `engine/platform.cpp`

`engine/platform.cpp:14`

```cpp
static SDL_GLContext gl_context; // OpenGL's state for the window (chapter 08)
```

The module's second piece of state, next to `window`.

`engine/platform.cpp:33`

```cpp
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    window = SDL_CreateWindow(title, width, height, SDL_WINDOW_OPENGL);
    // ...log and return false if it failed, as before...

    gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        LOG_ERROR("can't make an OpenGL 4.6 context: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetSwapInterval(0);
```

In `init`, between `SDL_Init` and the log line. The attributes come first, since SDL picks the window's pixel format from them when it opens the window. `SDL_WINDOW_OPENGL` replaces chapter 07's `0`: a window OpenGL can draw into. `SDL_GL_CreateContext` makes the context and makes it current. If the driver can't make a 4.6 core context, this is where momo finds out, and the log says why. The attributes aren't checked one by one: they only record what to ask for, and the context's creation is where asking fails. The swap interval comes last because it belongs to the current context. The comments in the file say why each value was picked, as Design did.

`engine/platform.cpp:68`

```cpp
void shutdown()
{
    SDL_GL_DestroyContext(gl_context);
    gl_context = nullptr;
    SDL_DestroyWindow(window);
    window = nullptr;
    SDL_Quit();
}
```

The context goes before the window it draws into.

`engine/platform.cpp:93`

```cpp
void present()
{
    SDL_GL_SwapWindow(window);
}

GlFunction gl_function(const char* name)
{
    return SDL_GL_GetProcAddress(name);
}
```

Without their comments. Chapter 07's surface code is gone, replaced by one swap, still not logged when it fails, for the same reason: it would repeat 60 times a second. `gl_function` doesn't log a missing function either. glad asks for every function the context's version has and handles the gaps itself.

### The interface: `engine/platform.h`

`engine/platform.h:40`

```cpp
using GlFunction = void (*)();

GlFunction gl_function(const char* name);
```

`GlFunction` is the same type as SDL's `SDL_FunctionPointer` and glad's `GLADapiproc`, so neither side needs a cast. Chapter 07's `find_function` returns a `void*` instead, which momo casts to the function's type itself (`GameApiFn*`). Both work: POSIX requires that a data pointer can be cast to a function pointer, for `dlsym`'s sake, and GCC allows it. Here, matching glad's type means passing `platform::gl_function` with no wrapper.

### The renderer: `engine/renderer.h`

`engine/renderer.h:11`

```cpp
namespace renderer {

bool init();
void begin_frame();

} // namespace renderer
```

Two functions so far, neither `MOMO_API`: the game doesn't draw yet. `renderer.h` doesn't include glad, so nothing that includes it sees OpenGL's names. The file's comment says only `renderer.cpp` calls OpenGL.

### Loading OpenGL: `engine/renderer.cpp`

`engine/renderer.cpp:72`

```cpp
bool init()
{
    i32 version = gladLoadGL(platform::gl_function);
    if (version == 0) {
        LOG_ERROR("can't load OpenGL's functions");
        return false;
    }
    LOG_INFO("OpenGL %s, %s", (const char*)glGetString(GL_VERSION),
             (const char*)glGetString(GL_RENDERER));

    i32 flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (!(flags & GL_CONTEXT_FLAG_DEBUG_BIT))
        LOG_WARN("not a debug context, so OpenGL may not explain its errors");

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(on_debug_message, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr,
                          GL_FALSE);
    return true;
}
```

`gladLoadGL` returns the context's version as one number (`40006` for 4.6), or 0 when it couldn't even find `glGetString`, the first function it asks for. `glGetString` returns `const GLubyte*`, unsigned bytes, so the casts make it the `const char*` that `%s` wants. The log line names the version and the GPU, and the debug check turns a silent failure into a warning: without a debug context, the messages that follow might never come.

The last four lines set up Design's table. `GL_DEBUG_OUTPUT` is on by default in a debug context, so enabling it only says so. `glDebugMessageControl` turns messages off at the source, so they never reach the callback. Its arguments say: any source, any type, severity notification, all IDs (a count of 0 and no list), off.

### The callback

`engine/renderer.cpp:44`

```cpp
static void GLAD_API_PTR on_debug_message(GLenum source, GLenum type, GLuint /* id */,
                                          GLenum severity, GLsizei length, const GLchar* message,
                                          const void* /* user_data */)
{
    const char* type_name = message_type_name(type);
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
        LOG_ERROR("OpenGL %s: %.*s", type_name, length, message);
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        LOG_WARN("OpenGL %s: %.*s", type_name, length, message);
        break;
    default:
        LOG_INFO("OpenGL %s: %.*s", type_name, length, message);
        break;
    }

    bool api_error = source == GL_DEBUG_SOURCE_API && type == GL_DEBUG_TYPE_ERROR;
    ASSERT(!api_error);
}
```

The parameters are the ones `glDebugMessageCallback` requires. The two momo doesn't use have no names, only comments, since `-Wextra` warns about a named parameter that's never read. `GLAD_API_PTR` is OpenGL's calling convention, which is nothing on Linux and `__stdcall` on 32-bit Windows. `message_type_name` (`engine/renderer.cpp:19`) is a `switch` that turns the type into a word for the log. The `bool` gives the condition a name, so the `ASSERT` reads well when it fails: `ASSERT(!api_error) failed`.

### Clearing

`engine/renderer.cpp:108`

```cpp
void begin_frame()
{
    glClearNamedFramebufferfv(0, GL_COLOR, 0, CLEAR_COLOR);
}
```

`CLEAR_COLOR` (`engine/renderer.cpp:14`) is `{0.06f, 0.22f, 0.25f, 1.0f}`, a `constexpr` array of four `f32`s, which is the `const GLfloat*` the call wants. The name says the frame starts here: from chapter 10 on, drawing happens after it.

### The engine: `engine/momo.cpp`

`engine/momo.cpp:204`

```cpp
    if (!platform::init("momo", WINDOW_WIDTH, WINDOW_HEIGHT))
        return 1;
    if (!renderer::init())
        return 1;

    start_game();
    while (!engine.quit_requested) {
        // ...events, reload, frame arena, as before...
        if (engine.game_lib)
            engine.game.update(engine.state);
        renderer::begin_frame();
        platform::present();
```

The renderer starts right after the window, and before the game, which from chapter 12 on loads textures in its `init`. Each frame clears and then swaps. The plan's frame ([§4.3](plan.md#43-the-frame)) puts the game's drawing between those two, once there's a way to draw.

### The Makefile

`Makefile:15`

```make
CC  := gcc
...
# For every C file. Only vendored libraries are written in C (chapter 08).
CFLAGS := -std=c11 -Wall -Wextra
...
ifeq ($(CONFIG),debug)
    CONFIG_FLAGS := -O0 -g3 -fsanitize=address,undefined -fno-sanitize-recover=all
    LDFLAGS      += -fsanitize=address,undefined
else ifeq ($(CONFIG),release)
    CONFIG_FLAGS := -O2 -g
...
CXXFLAGS += $(CONFIG_FLAGS)
CFLAGS   += $(CONFIG_FLAGS)
```

C gets its own compiler and flags: `-std=c11` and the warnings, but none of C++'s `-fno-exceptions` and friends. The flags that pick a build config, the optimization and the sanitizers, moved into `CONFIG_FLAGS`, which both languages add. Before, they went straight into `CXXFLAGS`.

`Makefile:52`

```make
GLAD_INCLUDE := -Ithird_party/glad/include
GLAD_CFLAGS  := $(CFLAGS) -fPIC -fvisibility=hidden $(GLAD_INCLUDE)

ENGINE_CXXFLAGS := $(LIB_CXXFLAGS) $(SDL_CFLAGS) $(GLAD_INCLUDE)
```

glad goes into `libmomo.so`, so it's compiled like the engine: `-fPIC`, and nothing exported. The engine gets glad's include folder, so `renderer.cpp` can write `#include <glad/gl.h>`. The game and the host don't.

`Makefile:81`

```make
GLAD_SRC := third_party/glad/src/gl.c
GLAD_OBJ := $(GLAD_SRC:%.c=$(OUT)/obj/%.o)
```

Listed by name: every library in `third_party/` will have its own layout, so a wildcard can't find them. `$(GLAD_OBJ)` joins `$(ENGINE_OBJ)` in the rules for `libmomo.so` and `libmomo.a`. The tests still don't pull in glad: none of them calls OpenGL. Its compile rule (`Makefile:167`) is the C version of the others, and its `.d` file joins the `-include` list.

`Makefile:182`

```make
compdb_entry = echo '{ "directory": "$(CURDIR)", "file": "$(1)", "command": "$(2) -c $(1)" }';
```

Every entry in `compile_commands.json` used to start with `$(CXX)`. Now the compiler is part of the second argument, so `gl.c`'s entry says `gcc` with `$(GLAD_CFLAGS)`, and clangd reads it as C.

No `-lGL` appears anywhere. SDL loads the OpenGL libraries itself when it opens the window, and glad finds every function through it.

## Run it

### The GPU's color

```
$ make
...
gcc -std=c11 -Wall -Wextra -O0 -g3 -fsanitize=address,undefined -fno-sanitize-recover=all -fPIC -fvisibility=hidden -Ithird_party/glad/include -MMD -MP -c third_party/glad/src/gl.c -o build/debug/obj/third_party/glad/src/gl.o
...
$ ./build/debug/momo
engine/momo.cpp:73: reserved 64 MB at 0x7b0045000000
engine/momo.cpp:199: loaded /home/mairuu/repos/momo/build/debug/game.so
engine/platform.cpp:62: SDL 3.4.18, video driver wayland, window 768x720
engine/renderer.cpp:84: OpenGL 4.6 (Core Profile) Mesa 26.2.4-arch1.1, AMD Radeon 880M Graphics (radeonsi, strix1, ACO, DRM 3.64, 7.2.8-arch1-2)
game/game.cpp:27: init
game/game.cpp:51: frame    0  |B...............|  x =   1
game/game.cpp:51: frame   30  |.B..............|  x =  31
```

The window opens dark teal. The new log line is the context describing itself: OpenGL 4.6, core profile, from Mesa 26.2.4, on an AMD Radeon 880M, driven by radeonsi, Mesa's driver for AMD GPUs. (The plan says 890M. The driver knows better, and the 880M is the same chip family, `strix1`, with the same OpenGL 4.6.) There's no warning after it, so the context is a debug context, and no messages from the driver: momo made no mistakes.

Press Ctrl+C:

```
game/game.cpp:51: frame  150  |.........B......|  x = 151
^Cengine/platform.cpp:86: quit requested
game/game.cpp:56: shutdown, with the bat at x = 160
engine/momo.cpp:232: quit after 160 frames
...
-----------------------------------------------------
Suppressions used:
  count      bytes template
      1        256 libSDL3.so
-----------------------------------------------------
```

The same ending as chapter 07, down to LeakSanitizer's table: one skipped leak of 256 bytes, the driver's. Chapter 07 worried that OpenGL calls might leak through Mesa with no SDL in the call stack, where the suppression wouldn't catch them. They don't, so the list stays as it is.

### A mistake, explained

Make the mistake LearnOpenGL habits invite. In `begin_frame`, pass `glClear`'s bit to the DSA function, which wants `GL_COLOR`:

```cpp
    glClearNamedFramebufferfv(0, GL_COLOR_BUFFER_BIT, 0, CLEAR_COLOR);
```

It compiles without a warning. Both are plain numbers, `GLenum`s. Run it:

```
$ make
$ ./build/debug/momo
...
game/game.cpp:27: init
game/game.cpp:51: frame    0  |B...............|  x =   1
engine/renderer.cpp:51: error: OpenGL error: GL_INVALID_ENUM in glClearBufferfv(buffer=GL_LIGHT0)
engine/renderer.cpp:67: ASSERT(!api_error) failed
zsh: illegal hardware instruction  ./build/debug/momo
$ echo $?
132
```

On the first frame, Mesa explains, the callback logs it, and the `ASSERT` stops momo. The exit code is 128 plus 4, the number of `SIGILL`, the signal `ASSERT`'s trap instruction raises (chapter 01). Without the callback, the clear would have quietly done nothing, the window would have shown whatever the buffer held, and nothing would have said why.

The message has two surprises. Mesa reports the call under the name of the older, non-DSA function that does the same job, `glClearBufferfv`. And it calls the bad value `GL_LIGHT0`. `GL_COLOR_BUFFER_BIT` is 0x4000, and so is `GL_LIGHT0`, the first light of OpenGL 1.0's fixed-function lighting (`grep 0x4000 /usr/include/GL/gl.h`). Mesa had only the number, and printed a name it knows for it. OpenGL's constants are just numbers, so the compiler can't tell them apart, and it's up to the driver to notice the wrong one.

### In gdb

Find the line without reading any code:

```
$ gdb -q ./build/debug/momo
(gdb) run
...
engine/renderer.cpp:51: error: OpenGL error: GL_INVALID_ENUM in glClearBufferfv(buffer=GL_LIGHT0)
engine/renderer.cpp:67: ASSERT(!api_error) failed

Thread 1 "momo" received signal SIGILL, Illegal instruction.
0x00007ffff7f4e082 in renderer::on_debug_message (source=33350, type=33356,
    severity=37190, length=52,
    message=0x7fffffffcdc0 "GL_INVALID_ENUM in glClearBufferfv(buffer=GL_LIGHT0)") at engine/renderer.cpp:67
67	    ASSERT(!api_error);
(gdb) backtrace
#0  0x00007ffff7f4e082 in renderer::on_debug_message (source=33350, type=33356,
    severity=37190, length=52,
    message=0x7fffffffcdc0 "GL_INVALID_ENUM in glClearBufferfv(buffer=GL_LIGHT0)") at engine/renderer.cpp:67
#1  0x00007bffead266e2 in ?? () from /usr/lib/libgallium-26.2.4-arch1.1.so
#2  0x00007bffeae37ff5 in ?? () from /usr/lib/libgallium-26.2.4-arch1.1.so
#3  0x00007ffff7f4e47b in renderer::begin_frame () at engine/renderer.cpp:113
#4  0x00007ffff7f4d3c7 in momo::run () at engine/momo.cpp:217
#5  0x0000555555555182 in main () at host/main.cpp:7
(gdb) frame 3
#3  0x00007ffff7f4e47b in renderer::begin_frame () at engine/renderer.cpp:113
113	    glClearNamedFramebufferfv(0, GL_COLOR_BUFFER_BIT, 0, CLEAR_COLOR);
```

Read from the bottom: `begin_frame` called into Mesa (`libgallium` is Mesa's driver library, and it has no debug information, hence the `??`), and Mesa called the callback before returning. `frame 3` shows the bad line. `source`, `type` and `severity` are plain numbers to gdb: 33350 is 0x8246, `GL_DEBUG_SOURCE_API`, 33356 is 0x824C, `GL_DEBUG_TYPE_ERROR`, and 37190 is 0x9146, `GL_DEBUG_SEVERITY_HIGH`. `print/x type` prints one in hex. Type `quit` and answer `y`.

### Why synchronous

Now see what `GL_DEBUG_OUTPUT_SYNCHRONOUS` is for. Keep the mistake, comment out `glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);` in `renderer::init` (`engine/renderer.cpp:101`), `make`, and run gdb again:

```
(gdb) backtrace
#0  0x00007ffff7f4e082 in renderer::on_debug_message (source=33350, type=33356,
    severity=37190, length=52,
    message=0x7fffffffcac0 "GL_INVALID_ENUM in glClearBufferfv(buffer=GL_LIGHT0)") at engine/renderer.cpp:67
#1  0x00007bffead266e2 in ?? () from /usr/lib/libgallium-26.2.4-arch1.1.so
#2  0x00007bffeae37ff5 in ?? () from /usr/lib/libgallium-26.2.4-arch1.1.so
#3  0x00007bffeac6b6b8 in ?? () from /usr/lib/libgallium-26.2.4-arch1.1.so
#4  0x00007bffeab03ed1 in ?? () from /usr/lib/libgallium-26.2.4-arch1.1.so
#5  0x00007bffeab0466e in ?? () from /usr/lib/libgallium-26.2.4-arch1.1.so
#6  0x00007bffeaabec86 in dri_flush ()
   from /usr/lib/libgallium-26.2.4-arch1.1.so
#7  0x00007ffff5f32381 in ?? () from /usr/lib/../lib/libEGL_mesa.so.0
#8  0x00007ffff5f27cda in ?? () from /usr/lib/../lib/libEGL_mesa.so.0
#9  0x00007ffff5f1bfe5 in ?? () from /usr/lib/../lib/libEGL_mesa.so.0
#10 0x00007ffff65d0173 in ?? () from /usr/lib/libSDL3.so.0
#11 0x00007ffff7f4da2c in platform::present () at engine/platform.cpp:102
#12 0x00007ffff7f4d3cc in momo::run () at engine/momo.cpp:218
#13 0x0000555555555182 in main () at host/main.cpp:7
```

The same message, from the wrong place: `platform::present`, one line after the mistake. `begin_frame` returned as if all was well. Mesa had queued the call instead of running it, and it only ran when the swap made Mesa flush its queue (`dri_flush`). The queue is meant to be run by a thread of Mesa's own, which `info threads` lists among the driver's others:

```
(gdb) info threads
  Id   Target Id                                           Frame
* 1    Thread 0x7ffff7f197c0 (LWP 16198) "momo"            0x00007ffff7f4e082 in renderer::on_debug_message (...)
  ...
  9    Thread 0x7bffd647d6c0 (LWP 16208) "momo:gl0"        0x00007ffff68a08f2 in ?? () from /usr/lib/libc.so.6
  ...
```

momo has one thread, but the libraries under it have started nine more, most of them Mesa's. `momo:gl0` is the one that runs queued OpenGL calls, and exercise 2 turns it off. Here, one line separates the mistake from the report. In chapter 10's renderer, it would be a whole frame of calls. Put the line back, and `begin_frame` too.

### Look inside: where `glClear` goes

glad's function pointers point somewhere, and it isn't straight into Mesa. Stop at `begin_frame` and look at one. As in chapter 07, `start` first, so the engine is loaded before you set a breakpoint in it:

```
$ gdb -q ./build/debug/momo
(gdb) start
...
(gdb) break renderer::begin_frame
Breakpoint 2 at 0x7ffff7f4e432: file engine/renderer.cpp, line 113.
(gdb) continue
...
Thread 1 "momo" hit Breakpoint 2, renderer::begin_frame ()
    at engine/renderer.cpp:113
113	    glClearNamedFramebufferfv(0, GL_COLOR, 0, CLEAR_COLOR);
(gdb) print glClearNamedFramebufferfv
$1 = (PFNGLCLEARNAMEDFRAMEBUFFERFVPROC) 0x7ffff5d8da60
```

gdb can print the name even though it's a macro: the debug build's `-g3` keeps macros in the debug information. Which library holds that address?

```
(gdb) info sharedlibrary GLdispatch
From                To                  Syms Read   Shared Object Library
0x00007ffff5d87000  0x00007ffff5e00000  Yes (*)     /usr/lib/libGLdispatch.so.0
(*): Shared library is missing debugging information.
```

Not Mesa's `libgallium`, but `libGLdispatch`, part of **libglvnd**, the "vendor-neutral dispatch" that lets several OpenGL drivers (Mesa, NVIDIA's) be installed side by side. Here are its first four instructions:

```
(gdb) x/4i glClearNamedFramebufferfv
   0x7ffff5d8da60:	endbr64
   0x7ffff5d8da64:	mov    0x6949d(%rip),%rax        # 0x7ffff5df6f08
   0x7ffff5d8da6b:	mov    %fs:(%rax),%r11
   0x7ffff5d8da6f:	jmp    *0x698(%r11)
```

That's the whole function. `%fs` is the register that points at the current thread's own variables, so the third line reads a pointer that belongs to this thread: the table of functions for the context current on it. The last line jumps through one entry of that table. This is what "current context" means, down at the machine. Step through the four, and you land in the driver:

```
(gdb) break *glClearNamedFramebufferfv
Breakpoint 3 at 0x7ffff5d8da60
(gdb) continue
Continuing.

Thread 1 "momo" hit Breakpoint 3, 0x00007ffff5d8da60 in ?? ()
   from /usr/lib/libGLdispatch.so.0
(gdb) stepi 4
0x00007bffeb3108c0 in ?? () from /usr/lib/libgallium-26.2.4-arch1.1.so
(gdb) quit
```

Quit at the breakpoint, as in chapter 07, so LeakSanitizer doesn't complain about running under gdb.

### Nothing leaks out

```
$ nm -D --defined-only build/debug/libmomo.so | grep -c glad
0
$ readelf -d build/debug/libmomo.so | grep -E 'GL|SDL'
 0x0000000000000001 (NEEDED)             Shared library: [libSDL3.so.0]
```

None of glad's 657 pointers is exported, so `game.so` couldn't call OpenGL even if it tried. And `libmomo.so` still needs only SDL from the system: no OpenGL library.

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

As before. The renderer has no tests of its own, for the same reason as the platform layer: it talks to things outside momo, so Run it checks it.

## Recap

```
momo/
  Makefile          + C files: CC, CFLAGS, CONFIG_FLAGS shared by both languages;
                      glad compiled into libmomo.so; compile_commands.json names the compiler
  engine/
    platform.h      + gl_function and GlFunction
    platform.cpp    + an OpenGL 4.6 core debug context, no vsync; present swaps;
                      the window surface is gone
    renderer.h      new: init, begin_frame
    renderer.cpp    new: loads OpenGL with glad, the debug callback, the clear
    momo.cpp        + starts the renderer after the window; clears before every present
  third_party/
    glad/           new: the generated OpenGL 4.6 core loader, its README and LICENSE
```

- `platform::` makes the context through SDL, and `renderer::` makes every OpenGL call. One function, `platform::gl_function`, connects them.
- OpenGL's functions belong to the driver and are looked up by name. glad, generated once and committed, does it for all 657 of OpenGL 4.6's, and hides them inside `libmomo.so`.
- Direct state access names the object in every call, so a line of OpenGL says what it changes. The clear is `glClearNamedFramebufferfv` on framebuffer 0.
- The debug callback logs every message by severity, and `ASSERT`s on errors. Synchronous output keeps the report inside the call that caused it, which matters because Mesa queues calls.
- Vsync is off until chapter 09, so the sleep still sets the pace on Wayland and X11 alike.
- Under every OpenGL call is a four-instruction stub in libglvnd that jumps through the current thread's table into Mesa.

The plan records the split between `platform::` and `renderer::`, the debug callback and vsync in [§4.5](plan.md#45-rendering), and how vendored C code is generated and built in [§7](plan.md#7-build-dependencies-repo). The outline now says chapter 09 also decides vsync.

Chapter 09 gives the loop a real clock: a fixed 60 Hz timestep, however fast the frames come.

## Exercises

Undo each change before the next exercise: `git checkout -- engine` puts the engine back.

1. **Core and compatibility.** At the end of `renderer::init`, add `glEnable(GL_TEXTURE_2D);`, which tutorials from before shaders called before drawing anything textured. What does the callback say? Then change `SDL_GL_CONTEXT_PROFILE_CORE` to `SDL_GL_CONTEXT_PROFILE_COMPATIBILITY` in `platform::init`. What does the OpenGL log line say now, and why is the error gone? glad was generated for the core profile, so what would you have to change to call `glBegin`?
2. **Mesa's queue.** Comment out `glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);` again, keep the `GL_COLOR_BUFFER_BIT` mistake, and run gdb with Mesa's queue turned off: `mesa_glthread=false gdb -q ./build/debug/momo`. Where does the backtrace point now? Is `momo:gl0` in `info threads`? Why is synchronous output still the right setting, even though this variable exists?
3. **Vsync, no sleep.** Set the swap interval to 1 in `platform::init`, comment out `platform::sleep_ms(16);` in `run`, run for about five seconds, and quit. How many frames does the last line count? `hyprctl monitors` shows your display's refresh rate. How fast does the bat fly now, compared with before, and why is that a problem for a game whose speeds are in pixels per frame ([plan §4.3](plan.md#43-the-frame))? That's chapter 09's problem.
4. **Another driver.** Run `SDL_VIDEO_DRIVER=x11 ./build/debug/momo`. Which log lines change? Under X11, SDL makes the context through GLX instead of EGL. Repeat the "Look inside" session: does `glClearNamedFramebufferfv` still point into `libGLdispatch`?
5. **Messages that aren't errors.** After `glDebugMessageControl` in `renderer::init`, send OpenGL a message of your own: `glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 1, GL_DEBUG_SEVERITY_LOW, -1, "hello from momo");`. What does the log show, and at which level? Change the severity to `GL_DEBUG_SEVERITY_NOTIFICATION`. Why does it vanish? What does the `-1` mean?
