# 10 · Sprite Batch

> **Goal:** the bat leaves the log and appears in the window: a purple rectangle with flapping wings, flying over a checkered floor and behind a stone pillar, with a see-through shadow under it. The game draws its whole picture again every frame, the engine puts the rectangles in layer order, and the GPU draws all of them with one call.

`git diff ch09 ch10` also shows the formatting and lint setup that came between the two chapters (the `maint:` commit, which `make format` and `make lint` come from). It put spaces inside braced initializers, as `.clang-format` writes them: `timestep::init` is now `*step = { .last_ns = now_ns };`, and chapter 09 quotes it the way it was.

## The problem

So far the game can only log. Twice a second it printed a row of dots with a `B` in it, and the window showed one dark teal color. Chapter 09 left a promise: the game gets a `render` function, called once per frame after the ticks. This chapter keeps it, and gives `render` something to call.

What the game needs to draw, for now, is rectangles of one color: the bat, the floor, a pillar. Chapter 12 turns them into sprites with textures. Either way, a frame is a lot of rectangles. A screen of tiles is 240 of them, and chapter 13's debug text is one per letter.

You've drawn rectangles before. LearnOpenGL draws each object with its own draw call: set the shader's uniforms for that object (its transform, its color), then call `glDrawArrays`. Here's what that costs. While this chapter was being written, momo drew the chapter's 37 rectangles plus a grid of 3,840 small ones, 3,877 a frame, three different ways. The release build ran with vsync off for 8 seconds each time, so the frame rate shows the cost of drawing:

| How each frame drew the 3,877 rectangles | Draw calls | Frames a second | Each frame took |
|---|---|---|---|
| LearnOpenGL's way: for each one, set its position and color as uniforms, then draw it | 3,877 | 640 | 1.6 ms |
| One draw per rectangle, with nothing changed between draws | 3,877 | 3,650 | 0.27 ms |
| **One draw for all of them** | 1 | 9,150 | 0.11 ms |

At 120 Hz a frame has 8.3 ms, and the first row spends a fifth of it before the game has done anything else. The middle row shows where that time goes. The draw call itself is cheap on Mesa, about 40 ns each here. Changing something between draws is what costs: two uniforms per rectangle made the frame six times slower. Chapter 12 adds another change, since each sprite picks a texture.

For today's 37 rectangles, any of the three would do. But the plan already chose how momo draws ([plan §4.5](plan.md#45-rendering)): *immediate-mode batched sprites, drawn in as few calls as possible*, ordered by layer, then call order. This chapter works out what that means.

## Design

### Immediate mode

There are two ways a game can tell a renderer what to draw:

| Option | The catch |
|---|---|
| **Retained mode:** the game creates a sprite object once, keeps a handle to it, and moves it when the game changes. The renderer keeps a list of every object and draws the list each frame. | Two copies of the world, the game's and the renderer's, that must agree. The game's handles live in `GameState`, so the renderer's objects must survive reloads and resets, and something has to delete them. |
| **Immediate mode:** every frame, the game draws everything again: "a rectangle here, this color, this layer". The renderer forgets it all once the frame is on screen. | The game repeats itself 120 times a second, even when nothing moved |

momo takes immediate mode, as the plan says. The renderer keeps nothing between frames, so nothing in it can point at old game code after a reload (chapter 06), and nothing in `GameState` points into the renderer. The picture is a function of the state: `render` reads `GameState` and draws it, and that's all. The repetition is cheap: the game calls `draw_rect` a few dozen times a frame, and each call copies 20 bytes.

Dear ImGui, which Part II may add, works the same way: a button exists only on the frames where your code calls `ImGui::Button`. Casey Muratori, of chapter 07's Handmade Hero, named the idea for user interfaces, "immediate-mode GUI".

> **Not OpenGL's immediate mode.** LearnOpenGL's introduction uses the same words for something else: old OpenGL's `glBegin` / `glVertex` / `glEnd`, one function call per vertex, which the core profile removed (chapter 08's exercise 1). That's about how vertices reach the GPU. momo's immediate mode is about who remembers the picture between frames: nobody does. Its vertices reach the GPU in one buffer.

### Drawing only in `render`

The game's functions now run at two different rates:

- `update` runs once per **tick**: 60 times a second, but zero, one or several times in a frame (chapter 09).
- `render` runs once per **frame**, after that frame's ticks.

Drawing belongs only in `render`. If `update` drew, a frame that ran two ticks would draw the bat twice, and a frame that ran none, every other frame on your 120 Hz panel, would draw no bat at all. So the renderer **opens** a frame in `begin_frame` and closes it in `end_frame`, and `draw_rect` `ASSERT`s that a frame is open. The engine opens it right before `render`, after the ticks, so a `draw_rect` from `update` stops momo (Run it tries this).

The other direction matters just as much. `render` gets a `const GameState*`:

```cpp
void (*render)(const GameState* state);
```

The compiler won't let `render` change the state. That keeps chapter 09's rule: the game's results depend only on `GameState` and its inputs, not on how many frames the display shows. A `render` that counted frames in the state, or moved something "just a little", would make the game play differently at 60 Hz and 120 Hz.

On the laptop's panel, every other frame runs no tick, so `render` draws the same picture twice. That second picture is where chapter 09's Fiedler would draw the bat halfway between two ticks, which [plan §4.3](plan.md#43-the-frame) leaves for later.

### Screen pixels

The game thinks in a 256×240 screen, the NES's: 16 by 15 tiles of 16 pixels. The game's old `SCREEN_WIDTH` was a stand-in for it. Now the engine owns the screen's size, in `renderer.h`, and every position the game passes is a whole pixel on that screen, `x` from the left and `y` from the top.

OpenGL doesn't know about pixels. A vertex shader's output position is in **clip space**: whatever lies between -1 and 1 on both axes is drawn, stretched over the whole window, with `y` pointing **up**. So the vertex shader turns a screen pixel into clip space:

```
clip = pixel / screen_size * 2 - 1          x: 0 → -1, 128 → 0, 256 → 1
gl_Position = (clip.x, -clip.y, 0, 1)       y flipped, so 0 is the top
```

LearnOpenGL does this with a projection matrix: `glm::ortho(0, 256, 240, 0)` builds a 4×4 matrix that does the same scale and offset, and the vertex shader multiplies by it. That's the general tool, ready for zooming, rotating and 3D. momo's screen does none of those, so the shader gets two numbers, the screen's size, as a uniform. [Plan §4.6](plan.md#46-numbers) said rendering would use float `V2`s and matrices. It changes here: the game passes `i32` pixels, and floats exist only on the GPU.

Why integers? Pixel art is drawn at whole pixels, and an `i32` says so in the type. The window is 768×720, exactly three times the screen, so every screen pixel covers 3×3 of the window's, and every edge lands on a window pixel's edge. Nothing is ever drawn at a third of a pixel, so nothing blurs. Chapter 15's fixed-point positions will round to whole pixels once, when the game draws. Chapter 11 handles windows that aren't an exact multiple.

### One draw call: instancing

How do thousands of rectangles become one draw call? The GPU has to be handed all of them at once, in one buffer:

| Option | The catch |
|---|---|
| A vertex buffer with the 4 corners of every rectangle, and an index buffer that joins them into triangles (Hazel's `Renderer2D` works this way) | The CPU works out all 4 corners of every rectangle and writes each one with its color, so every color is stored four times |
| **Instancing:** the buffer holds one entry per rectangle, and the GPU draws a four-corner shape once for each entry, an **instance** | The vertex shader works out its own corners, and the vertex array needs a setting LearnOpenGL's first chapters didn't use |

momo takes instancing. Each rectangle is one `RectInstance`: its `Rect` (four `i32`s) and its `Color` (four bytes), 20 bytes. Drawing them all is one call:

```cpp
glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, batch.count);
```

"Draw 4 vertices as a triangle strip, `batch.count` times." The GPU runs the vertex shader 4 × count times. Two things tell each run which corner of which rectangle it is:

- **`gl_VertexID`**, 0 to 3, the corner. A triangle strip joins 4 vertices into two triangles, 0-1-2 and 1-2-3, which is a rectangle when the corners go top left, top right, bottom left, bottom right. Bit 0 of the ID picks left or right, bit 1 top or bottom: `corner = vec2(gl_VertexID & 1, gl_VertexID >> 1)`.
- **The instance's attributes.** In LearnOpenGL, an attribute moves to the next element of its buffer for every vertex. With a **divisor** of 1, it moves once per instance instead, so all four corners of a rectangle read the same `rect` and `color`.

The corner is then `rect.xy + corner * rect.zw`: the top left, plus 0 or 1 times the width and height. The CPU never computes a corner.

### Layers: numbers in the engine, names in the game

Rectangles that overlap are drawn back to front, the **painter's algorithm**: whatever is drawn last covers what was there. [Plan §4.5](plan.md#45-rendering) orders them by **layer number**, then by the order the game drew them in. Who decides what each layer is for?

| Option | The catch |
|---|---|
| The engine defines the layers by name: background, entities, foreground, HUD | Those are this game's ideas, built into the engine. A game that wants a parallax layer behind the background has to change the engine. |
| **The engine draws layers 0 to `MAX_LAYERS - 1` in order, and the game names them with an enum of its own** | `draw_rect` takes a number, so passing the wrong one isn't a compile error. A layer past the last is still an `ASSERT`. |

The first draft of this chapter took the first option. You pointed out that the engine needs only the count, and you were right: what a layer *means* is the game's decision, the same split as everything else between `libmomo.so` and `game.so` ([plan §1](plan.md#1-what-momo-is)). Nothing in the engine reads the names. The sort only needs to know how many layers there can be, and that's `MAX_LAYERS`, 8. The plan had already said "layer number". Since layers live for one frame and are never stored, a reload that renumbers them changes nothing either.

This game names four of the eight, in `game.cpp`:

| The game's layer | Number | For |
|---|---|---|
| `LAYER_BACKGROUND` | 0 | tiles behind everything |
| `LAYER_ENTITIES` | 1 | the player, enemies, shots |
| `LAYER_FOREGROUND` | 2 | tiles in front of entities, such as this chapter's pillar |
| `LAYER_HUD` | 3 | health bars, lives |

A plain enum turns into its number with no cast, so `draw_rect(pillar, PILLAR_COLOR, LAYER_FOREGROUND)` reads the same as if the engine had defined the name. A `static_assert` in `game.cpp` checks, while compiling, that the game's layers fit in the engine's eight.

Chapter 13's debug overlay is engine code, and has to end up on top of whatever the game draws. It can draw after the game's rectangles, or the engine can keep one layer above the game's for itself. Chapter 13 decides. Either way, the engine still needn't know what the game's layers mean.

### Putting them in order: a counting sort

The game draws in whatever order its code runs, so the engine has to put the rectangles in layer order before the one draw call:

| Option | The catch |
|---|---|
| A depth buffer: give each layer a depth, and let the GPU keep the nearest pixel | A depth buffer only hides pixels. A see-through rectangle, like the bat's shadow, must be drawn after what's behind it, since it blends with it. Chapter 08 also asked for no depth buffer. |
| One list per layer, drawn in layer order | Each layer needs a fixed size of its own, and a frame that's mostly tiles runs out of room in the background while the other layers sit empty. |
| `qsort` by layer | It compares rectangles, and isn't **stable**: two rectangles in the same layer can come out in either order, and if they overlap, the picture changes from frame to frame. |
| **A counting sort** | Needs a second array to write the result into |

With only eight possible layers, momo doesn't need to compare anything. It counts. Take a frame that draws six rectangles in this order:

| Drawn | What | Layer |
|---|---|---|
| 1st | the pillar | foreground |
| 2nd | a floor tile | background |
| 3rd | the bat | entities |
| 4th | another floor tile | background |
| 5th | a health bar | HUD |
| 6th | the bat's wing | entities |

1. **Count** each layer's rectangles: background 2, entities 2, foreground 1, HUD 1.
2. **Add up** where each layer starts in the result: background at 0, entities after background's 2 at 2, foreground at 4, HUD at 5. The four layers the game doesn't use, 4 to 7, are empty, and all start at 6, the end.
3. **Place** each rectangle, in the order they were drawn, at its layer's next free place, and move that place on by one: the pillar goes to 4, the first tile to 0, the bat to 2, the second tile to 1, the health bar to 5, the wing to 3.

The result is tile, tile, bat, wing, pillar, health bar. Layers are in order, and inside each layer the rectangles kept the order they were drawn in, so the sort is stable. It's three passes over the rectangles, none over pairs of them, and one step per layer, used or not. The tests check this exact frame.

A frame has room for `MAX_RECTS`, 4,096 rectangles: 17 screens of tiles. Hazel's renderer, when its buffer is full, draws what it has and starts a new batch. momo can't: its rectangles aren't in drawing order until the frame ends, so drawing early would put the first ones under everything else. A full batch is an `ASSERT`, like a full arena ([plan §4.2](plan.md#42-memory)): the limit is fixed, and hitting it says something about the game.

### The batch doesn't call OpenGL

The sort goes in a module of its own, `batch::`, which collects rectangles, sorts them, and never calls OpenGL. The renderer owns one batch and hands the sorted result to the GPU. That's chapter 09's trick once more: the timestep doesn't read the clock, so tests hand it times, and the batch doesn't touch the GPU, so tests read its result. The test program links `batch.o` and nothing with OpenGL or SDL in it.

The batch's arrays, and the sorted copy, come from the engine's arena when the renderer starts: 4,096 rectangles twice over, plus a layer number for each. They're `ASSERT`-checked like chapter 04 promised for containers, since AddressSanitizer can't see past the end of an array in an arena.

### Colors

A `Color` is four bytes: red, green, blue and alpha, 0 to 255, the way the GPU likes to read them. The vertex array turns each byte into a float from 0 to 1 as the shader reads it (**normalized**).

Alpha is how much of what's behind a pixel the color covers. Blending is on, with the usual formula:

```
result = color * alpha + what was there * (1 - alpha)
```

An alpha of 255 is opaque. The bat's shadow is black at 96, which darkens the floor tile under it to about 60% of its color, light tiles and dark tiles alike. NES games had no see-through pixels, but chapter 13's debug text wants a dark backdrop and chapter 19's hitboxes want to show what's under them, so blending comes now, while it's two lines.

ZII has something to say here: a zeroed `Color` has alpha 0, so it draws nothing. That's a valid color, and it's also an easy mistake, since `{ .r = 255 }` is an invisible red. So `renderer.h` has a helper for opaque colors, written in hex the way color pickers write them: `renderer::rgb(0x8e44ad)`. The see-through ones spell out their alpha.

### Shaders, and when they don't compile

The two shaders are short, and they're the engine's, not the game's, so they live in `renderer.cpp` as C++ raw string literals: `R"(...)"` holds text across lines without any escaping. Loading them from files would make them hot-reloadable, which is chapter 33's subject, assets that reload when they change.

Chapter 08 left a problem here. A shader that doesn't compile is an expected failure ([plan §3.3](plan.md#33-errors-and-strings)): the renderer must log why and return `false`. The portable way is to ask OpenGL after compiling, `glGetShaderiv(..., GL_COMPILE_STATUS, ...)`, and read the compiler's messages with `glGetShaderInfoLog`. That works on every driver, debug context or not. But Mesa also sends the same text to the debug callback, so every error was logged twice:

```
engine/renderer.cpp:120: error: OpenGL error: 0:10(16): error: `corner_colour' undeclared
engine/renderer.cpp:154: error: the fragment shader doesn't compile:
0:10(16): error: `corner_colour' undeclared

```

(That's from a test run while this chapter was being written, with `corner_color` misspelled.) The callback's copy doesn't say which shader it was. So `renderer::init` turns off the messages whose source is the shader compiler, the same way chapter 08 turned off notifications, and `compile_shader` logs the only copy. The compiler's text also ended with a newline, hence the empty line above, so `compile_shader` cuts it off before logging.

## The code

### The screen and the game's drawing: `engine/renderer.h`

`engine/renderer.h:17`

```cpp
constexpr i32 SCREEN_WIDTH = 256;
constexpr i32 SCREEN_HEIGHT = 240;

struct Rect {
    i32 x;
    i32 y;
    i32 w;
    i32 h;
};

struct Color {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

constexpr i32 MAX_LAYERS = 8;

constexpr i32 MAX_RECTS = 4096;
```

Without their comments, which say what Design said. `MAX_LAYERS`'s comment also says that the engine gives the numbers no meaning, and the game names them.

`engine/renderer.h:55`

```cpp
MOMO_API void draw_rect(Rect rect, Color color, i32 layer);

constexpr Color rgb(u32 hex)
{
    return { .r = (u8)(hex >> 16), .g = (u8)(hex >> 8), .b = (u8)hex, .a = 255 };
}
```

`draw_rect` is the renderer's first `MOMO_API` function: the first drawing call the game can make. The layer is an `i32`, like every index in momo ([plan §3.4](plan.md#34-names-and-types)). `rgb` is `constexpr`, so the game's colors are worked out while compiling, and it needs no `MOMO_API`: it's compiled into whoever calls it, from the header. Each `(u8)` cast keeps the low 8 bits, one byte of `0xRRGGBB`.

The engine's half of the header follows: `init(Arena*)`, `shutdown`, `begin_frame` and `end_frame`, none of them `MOMO_API`. `momo.h` now includes `renderer.h`, so the game sees their declarations too, but calling one fails to link, like calling SDL did in chapter 07.

### The batch: `engine/batch.h`

`engine/batch.h:11`

```cpp
struct RectInstance {
    Rect rect;
    Color color;
};

struct Batch {
    RectInstance* rects; // room for capacity of them, from an arena
    i32* layers;         // each rectangle's layer, 0 to MAX_LAYERS - 1
    i32 count;
    i32 capacity;
};
```

`RectInstance` is what the GPU reads: 16 bytes of `Rect` and 4 of `Color`, with no padding. The layer is kept in its own array, since the GPU never needs it. `batch::init` pushes both arrays from an arena, `clear` sets `count` to 0, and `add` checks `count` against `capacity`, and that the layer is from 0 to `MAX_LAYERS - 1`, before storing.

### The counting sort: `engine/batch.cpp`

`engine/batch.cpp:31`

```cpp
void sort(const Batch& batch, RectInstance* out)
{
    i32 next[MAX_LAYERS] = {};
    for (i32 i = 0; i < batch.count; i++)
        next[batch.layers[i]]++;

    i32 start = 0;
    for (i32 layer = 0; layer < MAX_LAYERS; layer++) {
        i32 count = next[layer];
        next[layer] = start;
        start += count;
    }

    for (i32 i = 0; i < batch.count; i++) {
        i32 layer = batch.layers[i];
        out[next[layer]] = batch.rects[i];
        next[layer]++;
    }
}
```

Design's three steps, with one array, `next`, doing two jobs. After the first loop it holds each layer's count. The second loop turns counts into starting places: for the six-rectangle frame, `{ 2, 2, 1, 1, 0, 0, 0, 0 }` becomes `{ 0, 2, 4, 5, 6, 6, 6, 6 }`. In the third loop it's each layer's next free place, moving on as the layer fills. The batch comes in as a `const` reference, since `sort` only reads it ([plan §3.1](plan.md#31-rules)).

### The shaders: `engine/renderer.cpp`

`engine/renderer.cpp:26`

```glsl
#version 460 core

layout(location = 0) uniform vec2 screen_size; // 256, 240

layout(location = 0) in ivec4 rect;  // x, y, w, h, in screen pixels
layout(location = 1) in vec4 color;  // red, green, blue, alpha, 0 to 1

out vec4 corner_color;

void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec2 pixel = rect.xy + corner * rect.zw;

    vec2 clip = pixel / screen_size * 2.0 - 1.0;
    gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0);
    corner_color = color;
}
```

The vertex shader, inside `VERTEX_SHADER`'s raw string, without its comments (one of them draws the four corners). `layout(location = ...)` fixes each input's number in the shader itself, so the C++ side can use the number instead of asking OpenGL for it by name, as LearnOpenGL did with `glGetUniformLocation`. Uniforms and inputs are numbered separately, so both can start at 0. `rect` is an `ivec4`, four ints, since that's what the buffer holds. The fragment shader (`engine/renderer.cpp:56`) only passes `corner_color` on, so every pixel of a rectangle gets its color.

### Making the program

`engine/renderer.cpp:153`

```cpp
static u32 compile_shader(GLenum stage, const char* stage_name, const char* source)
{
    u32 shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    i32 compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char messages[1024] = {};
        i32 length = 0;
        glGetShaderInfoLog(shader, sizeof(messages), &length, messages);
        LOG_ERROR("the %s shader doesn't compile:\n%.*s", stage_name,
                  without_last_newline(messages, length), messages);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
```

The steps LearnOpenGL taught, with momo's log. The `\n` after the colon puts the compiler's first message at the start of a line, where its `0:10(16)` (source string 0, line 10, column 16) is easy to read. `without_last_newline` (`engine/renderer.cpp:144`) returns the length without the compiler's final newline, and `%.*s` prints that many characters. `make_program` (`engine/renderer.cpp:175`) compiles both shaders, links them, checks `GL_LINK_STATUS` the same way, and deletes the shaders, which the linked program no longer needs.

`engine/renderer.cpp:247`

```cpp
    glDebugMessageControl(GL_DEBUG_SOURCE_SHADER_COMPILER, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                          GL_FALSE);

    program = make_program(VERTEX_SHADER, FRAGMENT_SHADER);
    if (!program)
        return false;
    glProgramUniform2f(program, SCREEN_SIZE_UNIFORM, SCREEN_WIDTH, SCREEN_HEIGHT);
```

In `renderer::init`, after chapter 08's debug setup. The first call turns off every message from the shader compiler: that source, any type, any severity. `glProgramUniform2f` is DSA for uniforms: it names the program, where LearnOpenGL's `glUniform2f` sets a uniform of whichever program `glUseProgram` last bound. The screen's size never changes, so it's set once. The callback's comment now says why it never sees a compile error.

### Buffers and the vertex array

`engine/renderer.cpp:260`

```cpp
    batch::init(&batch, arena, MAX_RECTS);
    sorted = arena::push_array<RectInstance>(arena, MAX_RECTS);
    glCreateBuffers(1, &rect_buffer);
    glNamedBufferStorage(rect_buffer, MAX_RECTS * sizeof(RectInstance), nullptr,
                         GL_DYNAMIC_STORAGE_BIT);
```

`batch`, `sorted` and `rect_buffer` are file-scope variables (`engine/renderer.cpp:78`), like `platform.cpp`'s window: the engine never reloads. `glNamedBufferStorage` gives the buffer a size it keeps for good, 80 KB, with no data yet. LearnOpenGL's `glBufferData` can give a buffer a new size on every call, and storage that can't change is the simpler promise to make to the driver. `GL_DYNAMIC_STORAGE_BIT` allows `glNamedBufferSubData` to rewrite the contents, which `end_frame` does every frame. Without it, that call would be an error.

`engine/renderer.cpp:271`

```cpp
    glCreateVertexArrays(1, &vertex_array);
    glVertexArrayVertexBuffer(vertex_array, 0, rect_buffer, 0, sizeof(RectInstance));
    glVertexArrayBindingDivisor(vertex_array, 0, 1);

    glEnableVertexArrayAttrib(vertex_array, RECT_ATTRIB);
    glVertexArrayAttribIFormat(vertex_array, RECT_ATTRIB, 4, GL_INT, offsetof(RectInstance, rect));
    glVertexArrayAttribBinding(vertex_array, RECT_ATTRIB, 0);

    glEnableVertexArrayAttrib(vertex_array, COLOR_ATTRIB);
    glVertexArrayAttribFormat(vertex_array, COLOR_ATTRIB, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                              offsetof(RectInstance, color));
    glVertexArrayAttribBinding(vertex_array, COLOR_ATTRIB, 0);
```

The vertex array, in DSA. LearnOpenGL's `glVertexAttribPointer` says everything about an attribute in one call, about whatever buffer is bound to `GL_ARRAY_BUFFER`. DSA splits that into the buffer and the attribute's format, joined by a numbered **binding point**:

| LearnOpenGL | DSA | What it says |
|---|---|---|
| `glGenVertexArrays`, then bind | `glCreateVertexArrays` | make a vertex array |
| `glBindBuffer(GL_ARRAY_BUFFER, ...)` | `glVertexArrayVertexBuffer` | binding point 0 reads `rect_buffer`, from byte 0, 20 bytes per element |
| `glVertexAttribDivisor` | `glVertexArrayBindingDivisor` | binding point 0 moves on once per instance |
| `glVertexAttribPointer` | `glVertexArrayAttribFormat` / `IFormat` | what one attribute looks like, and where it is in the element |
| (implied by the bind) | `glVertexArrayAttribBinding` | which binding point the attribute reads |
| `glEnableVertexAttribArray` | `glEnableVertexArrayAttrib` | turn the attribute on |

`IFormat` keeps `rect`'s ints as ints, for the shader's `ivec4`. Plain `Format` with `GL_TRUE` turns `color`'s bytes into floats from 0 to 1. `offsetof` (from `<stddef.h>`) is where a field starts in its struct: 0 for `rect`, 16 for `color`. `RECT_ATTRIB` and `COLOR_ATTRIB` (`engine/renderer.cpp:71`) are the shader's `layout(location = ...)` numbers, kept next to the shaders.

The last lines of `init` turn on blending, `glEnable(GL_BLEND)` and `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`, which is Design's formula.

### A frame

`engine/renderer.cpp:301`

```cpp
void begin_frame()
{
    ASSERT(!in_frame);
    in_frame = true;
    batch::clear(&batch);
    glClearNamedFramebufferfv(0, GL_COLOR, 0, CLEAR_COLOR);
}

void draw_rect(Rect rect, Color color, i32 layer)
{
    ASSERT(in_frame);
    batch::add(&batch, rect, color, layer);
}

void end_frame()
{
    ASSERT(in_frame);
    in_frame = false;
    if (batch.count == 0)
        return;

    batch::sort(batch, sorted);
    glNamedBufferSubData(rect_buffer, 0, batch.count * (i64)sizeof(RectInstance), sorted);

    glUseProgram(program);
    glBindVertexArray(vertex_array);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, batch.count);
}
```

Without their comments. `begin_frame` keeps chapter 08's clear. `draw_rect` only adds to the batch: nothing reaches OpenGL until the frame ends. `end_frame` sorts, copies the sorted rectangles into the GPU's buffer, and draws. Only `batch.count` rectangles are copied, 740 bytes today, not the buffer's whole 80 KB. `sizeof` is unsigned, and the `(i64)` keeps the byte count a signed `i64`, as [plan §3.4](plan.md#34-names-and-types) wants sizes.

Chapter 08 said drawing still uses whatever is bound. There's no DSA version of a draw call: it draws with the bound program and vertex array. So `end_frame` binds both right before drawing. The draw never depends on what some other function left bound, and the binds sit next to the call that uses them.

`shutdown` (`engine/renderer.cpp:291`) deletes the vertex array, the buffer and the program, as chapter 08 promised once there were OpenGL objects. It runs before `platform::shutdown`, while the context still exists.

### The boundary: `engine/momo.h`

`engine/momo.h:63`

```cpp
    // Draws the game, with renderer::draw_rect. The engine calls it once
    // per frame, after the frame's ticks, which may be none: then it draws
    // the same picture again. The state is const, so drawing can't change
    // the game, and the game plays the same however many frames there are
    // (chapter 10).
    void (*render)(const GameState* state);
```

`GameApi` gets its fourth function, between `update` and `shutdown`, and `momo.h` includes `renderer.h`. Changing `momo.h` means restarting momo, as always (chapter 06): a running engine and a new `game.so` would disagree about the struct.

### The loop: `engine/momo.cpp`

`engine/momo.cpp:235`

```cpp
        // The game draws its picture of where the ticks left it.
        renderer::begin_frame();
        if (engine.game_lib)
            engine.game.render(engine.state);
        renderer::end_frame();
        platform::present(); // with vsync, waits for the display
```

After the ticks, as chapter 09 promised. When no game is loaded, after a failed reload, the frame is just the clear color. `renderer::init` now takes `&engine.arena`, the engine's own arena (chapter 06), and `renderer::shutdown` runs just before `platform::shutdown`. The window's size is now `3 * SCREEN_WIDTH` by `3 * SCREEN_HEIGHT`, from `renderer.h`.

### The game: `game/game.cpp`

`game/game.cpp:24`

```cpp
enum Layer : u8 {
    LAYER_BACKGROUND, // tiles behind everything
    LAYER_ENTITIES,   // the player, enemies, shots
    LAYER_FOREGROUND, // tiles in front of entities
    LAYER_HUD,        // health bars, lives
    LAYER_COUNT,
};
static_assert(LAYER_COUNT <= MAX_LAYERS, "the renderer draws at most MAX_LAYERS layers");
```

The game's names for the renderer's layers, inside `namespace game`. It's a plain enum on a `u8`, with prefixed values, as [plan §3.4](plan.md#34-names-and-types) wants. `LAYER_COUNT` comes last, so it's the number of layers the game uses, and the `static_assert` stops the build if that ever passes `MAX_LAYERS`. An enum is a type, not a variable, so [plan §3.2](plan.md#32-rules-for-game-code-required-by-hot-reload)'s ban on globals doesn't touch it.

`game/game.cpp:66`

```cpp
void render(const GameState* state)
{
    // The floor: two rows of tiles in a checkerboard.
    for (i32 row = 0; row < 2; row++) {
        for (i32 column = 0; column < SCREEN_WIDTH / TILE_SIZE; column++) {
            Rect tile = { .x = column * TILE_SIZE,
                          .y = FLOOR_Y + row * TILE_SIZE,
                          .w = TILE_SIZE,
                          .h = TILE_SIZE };
            renderer::draw_rect(tile, FLOOR_COLORS[(row + column) % 2], LAYER_BACKGROUND);
        }
    }

    // A stone pillar in the middle of the screen, in front of the bat.
    Rect pillar = { .x = 120, .y = 96, .w = TILE_SIZE, .h = FLOOR_Y - 96 };
    renderer::draw_rect(pillar, PILLAR_COLOR, LAYER_FOREGROUND);

    i32 x = state->bat_x;
    renderer::draw_rect({ .x = x + 2, .y = FLOOR_Y, .w = 12, .h = 3 }, SHADOW_COLOR,
                        LAYER_ENTITIES);
    i32 wing_y = (state->tick / 8) % 2 == 0 ? BAT_Y + 1 : BAT_Y + 7;
    renderer::draw_rect({ .x = x, .y = wing_y, .w = 6, .h = 5 }, WING_COLOR, LAYER_ENTITIES);
    renderer::draw_rect({ .x = x + 10, .y = wing_y, .w = 6, .h = 5 }, WING_COLOR, LAYER_ENTITIES);
    renderer::draw_rect({ .x = x + 4, .y = BAT_Y + 3, .w = 8, .h = 9 }, BAT_COLOR, LAYER_ENTITIES);
}
```

37 rectangles: 32 floor tiles, the pillar, and the bat's shadow, two wings and body. The pillar is drawn before the bat in the code, but its layer puts it in front. Inside the entities layer, call order decides: the shadow lands on the floor tiles under it, and the body, drawn last, covers the inner edges of the wings.

The wings move every 8 ticks, so they flap in game time, read from `state->tick`. At 60 Hz or 120 Hz they flap at the same speed. The colors are `constexpr` (`game/game.cpp:38`): `rgb` for the opaque ones, and `{ .r = 0, .g = 0, .b = 0, .a = 96 }` for the shadow.

`update` lost its row of dots: the window shows the real thing now. It moves the bat and counts the tick, nothing else. The game's own `SCREEN_WIDTH` is gone in favor of the engine's. `GameState` didn't change, so `GAME_STATE_VERSION` stays 1.

### The tests: `tests/batch_test.cpp`

`tests/batch_test.cpp:119`

```cpp
static void the_frame_from_the_chapter()
{
    // ...
    batch::add(&b, rect_at(4), {}, LAYER_FOREGROUND); // pillar
    batch::add(&b, rect_at(0), {}, LAYER_BACKGROUND); // floor
    batch::add(&b, rect_at(2), {}, LAYER_ENTITIES);   // bat
    batch::add(&b, rect_at(1), {}, LAYER_BACKGROUND); // floor
    batch::add(&b, rect_at(5), {}, LAYER_HUD);        // health bar
    batch::add(&b, rect_at(3), {}, LAYER_ENTITIES);   // wing

    RectInstance out[CAPACITY] = {};
    batch::sort(b, out);
    for (i32 i = 0; i < 6; i++)
        CHECK_EQ(out[i].rect.x, i);
}
```

Design's six rectangles. Each one's `x` is where it should end up, so the check is one loop. The test file names the layers with an enum of its own, the same as the game's: the batch sees only numbers, so any code that draws can name them its own way. The batch's arrays come from an arena on the stack, as in `arena_test`. The other eight tests cover an empty batch, four layers added front to back, all eight layers, empty layers between two used ones, two layers taking turns, colors traveling with their rectangles, `clear`, and a batch filled to its capacity, two rectangles on each layer. Overfilling it would be an `ASSERT`, which stops the test program, so no test does.

### The Makefile

Unchanged. `engine/batch.cpp` joins the engine through the `engine/*.cpp` wildcard, and `tests/batch_test.cpp` becomes a test program through `tests/*_test.cpp`.

## Run it

### A picture

```
$ make
...
$ time ./build/debug/momo
engine/momo.cpp:79: reserved 64 MB at 0x7be937400000
engine/momo.cpp:203: loaded /home/mairuu/repos/momo/build/debug/game.so
engine/platform.cpp:71: SDL 3.4.18, video driver wayland, window 768x720, display 120 Hz
engine/renderer.cpp:221: OpenGL 4.6 (Core Profile) Mesa 26.2.4-arch1.1, AMD Radeon 880M Graphics (radeonsi, strix1, ACO, DRM 3.64, 7.2.9-arch1-1)
game/game.cpp:46: init
```

The window shows the scene: the dark teal sky, a checkered brown floor along the bottom, a gray pillar in the middle, and the purple bat flying from the left edge, flapping, with its shadow sliding along the floor. When it reaches the pillar, it slides behind it, and comes out the other side. The log stays quiet, with no more rows of dots. After about ten seconds, press Ctrl+C:

```
^Cengine/platform.cpp:96: quit requested
game/game.cpp:96: shutdown, with the bat at x = 113
engine/momo.cpp:253: quit after 9.89 s: 1188 frames, 593 ticks
engine/momo.cpp:85: permanent arena: at most 12 bytes used of 16 MB
engine/momo.cpp:85: stage     arena: at most 0 bytes used of 32 MB
engine/momo.cpp:85: frame     arena: at most 0 bytes used of 8 MB
engine/momo.cpp:85: engine    arena: at most 180268 bytes used of 8 MB
...
./build/debug/momo  0.28s user 0.21s system 4% cpu 10.086 total
```

The engine's arena grew from 44 bytes, `game.so`'s path, to 180,268. The rest is the renderer's: 4,096 `RectInstance`s of 20 bytes for the batch (81,920), an `i32` layer for each (16,384), and 81,920 more for the sorted copy. The GPU's buffer is another 80 KB, in memory the driver manages. momo now uses about 5% of a CPU (4 to 6% over several runs), in the debug and the release build alike, where chapter 09's frame, a clear and nothing else, used 2%.

If the window opens on the external monitor, its edges look soft. That monitor's scale is 1.67, so the compositor stretches the 768×720 picture by a fraction, and screen pixels come out uneven. On the laptop's panel, at scale 2, every screen pixel is exactly 6×6 of the panel's. Chapter 11 deals with display scales.

### One draw call

Mesa can report what it's doing. Its driver has a built-in overlay, the Gallium HUD, which normally draws graphs on top of the window. Asked for `stdout`, it prints its numbers to the terminal, twice a second, each one the average per frame:

```
$ GALLIUM_HUD=stdout,draw-calls ./build/debug/momo
...
game/game.cpp:46: init
draw calls: 1
draw calls: 1
draw calls: 1
```

37 rectangles, one draw call, counted by the driver itself. `GALLIUM_HUD=help` lists everything it can count. The HUD slows momo down while it's on, so don't measure frame rates with it. Exercise 1 uses it to count the draw calls of the last two rows of the Problem's table.

### Layers

In `render`, move the pillar to the background layer:

```cpp
    renderer::draw_rect(pillar, PILLAR_COLOR, LAYER_BACKGROUND);
```

`make`, run it, and wait for the bat to reach the middle. Now it flies in front of the pillar. Both are drawn the same way, and only the layer decides. Put `LAYER_FOREGROUND` back.

### Hot reload you can see

Run momo, and while the bat flies, change `BAT_COLOR` (`game/game.cpp:40`) to red, `renderer::rgb(0xe74c3c)`, and run `make` in another terminal:

```
game/game.cpp:46: init
engine/momo.cpp:182: reloaded /home/mairuu/repos/momo/build/debug/game.so
```

The bat's body turns red where it is, mid-flight, and keeps flying. Chapter 06 could only show this in the log. The wings keep their purple, since only `BAT_COLOR` changed. Change it back.

### Drawing from `update`

At the end of `update`, just before `state->tick++;`, add a rectangle:

```cpp
    renderer::draw_rect({ .x = 0, .y = 0, .w = 8, .h = 8 }, BAT_COLOR, LAYER_HUD);
```

It compiles, and links: `draw_rect` is `MOMO_API`. Run it:

```
$ ./build/debug/momo
...
game/game.cpp:46: init
engine/renderer.cpp:317: ASSERT(in_frame) failed
zsh: illegal hardware instruction  ./build/debug/momo
```

The first tick ran before any frame was open. Without the `ASSERT`, `begin_frame` would have emptied the batch right after, and the square would never have appeared, with no message at all. Take the line out.

### A shader that doesn't compile

In `FRAGMENT_SHADER` (`engine/renderer.cpp:56`), misspell the input: `pixel_color = corner_colour;`. `make`, and run:

```
$ ./build/debug/momo; echo $?
...
engine/renderer.cpp:221: OpenGL 4.6 (Core Profile) Mesa 26.2.4-arch1.1, AMD Radeon 880M Graphics (radeonsi, strix1, ACO, DRM 3.64, 7.2.9-arch1-1)
engine/renderer.cpp:165: error: the fragment shader doesn't compile:
0:10(16): error: `corner_colour' undeclared
-----------------------------------------------------
Suppressions used:
  count      bytes template
    221      44128 libSDL3.so
-----------------------------------------------------

1
```

Once, this time, with the shader's name, and no empty line. `init` returned `false`, and momo exited with 1 before the game started. Line 10 of the shader is counted from the raw string's first line, the empty one right after `R"(`.

LeakSanitizer's table counts 221 blocks this time, not 1. momo returned from `run` without shutting SDL down, so LeakSanitizer counted what SDL and the driver had allocated for the window. Every one of those blocks was allocated through SDL, so the suppression covers them, and the OS takes all of it back when the process ends. Take the `u` out again.

### Look inside: what the GPU reads

Stop momo just before the rectangles go to the GPU. As in chapter 07, `start` first, so the engine is loaded before you set a breakpoint in it:

```
$ gdb -q ./build/debug/momo
(gdb) start
...
(gdb) break engine/renderer.cpp:329
Breakpoint 2 at 0x7ffff77b2b0c: file engine/renderer.cpp, line 329.
(gdb) continue
...
Thread 1 "momo" hit Breakpoint 2, renderer::end_frame ()
    at engine/renderer.cpp:329
329	    glNamedBufferSubData(rect_buffer, 0, batch.count * (i64)sizeof(RectInstance), sorted);
(gdb) print batch.count
$1 = 37
(gdb) print sorted[0]
$2 = {rect = {x = 0, y = 208, w = 16, h = 16}, color = {r = 124 '|', 
    g = 74 'J', b = 30 '\036', a = 255 '\377'}}
```

The sort has run, on the first frame. The first rectangle is the first floor tile, at the left edge of the floor's top row. gdb prints each `u8` as a character too, since `uint8_t` is an `unsigned char`. After the 32 tiles comes the entities layer:

```
(gdb) print sorted[32]
$3 = {rect = {x = 2, y = 208, w = 12, h = 3}, color = {r = 0 '\000', 
    g = 0 '\000', b = 0 '\000', a = 96 '`'}}
(gdb) print sorted[36]
$4 = {rect = {x = 120, y = 96, w = 16, h = 112}, color = {r = 138 '\212', 
    g = 138 '\212', b = 138 '\212', a = 255 '\377'}}
```

At 32, the shadow, the first entity drawn, near the left edge with the bat. The pillar was drawn second in the code, but it's last, at 36. Here are the 20 bytes the GPU reads for the first tile, as five 4-byte words:

```
(gdb) x/5xw &sorted[0]
0x7bfff2c1802c:	0x00000000	0x000000d0	0x00000010	0x00000010
0x7bfff2c1803c:	0xff1e4a7c
(gdb) print/x sorted[0].color
$5 = {r = 0x7c, g = 0x4a, b = 0x1e, a = 0xff}
(gdb) quit
```

`x` is 0, `y` is 0xd0 = 208, `w` and `h` are 0x10 = 16, and then the color, `0x7c4a1e`, the first of `FLOOR_COLORS`. Its four bytes in memory are `7c 4a 1e ff`, red first. Read as one little-endian word, the way `x/xw` reads, they come out backwards: `0xff1e4a7c`. The vertex array told the GPU exactly this layout: four ints at offset 0, four bytes at offset 16, 20 bytes per rectangle. Answer `y` to quit.

### The tests

```
$ make test
...
batch_test
  an_empty_batch_sorts_to_nothing ... ok
  layers_come_out_back_to_front ... ok
  every_layer_up_to_max_layers_is_drawn_in_order ... ok
  empty_layers_in_between_are_skipped ... ok
  a_layer_keeps_the_order_its_rects_were_added_in ... ok
  the_frame_from_the_chapter ... ok
  sort_copies_the_color_with_the_rect ... ok
  clear_empties_the_batch ... ok
  a_full_batch_holds_capacity_rects ... ok
  9 passed
...
```

`ldd build/debug/tests/batch_test | grep -E 'SDL|GL'` prints nothing: the drawing order is tested with no window and no GPU.

## Recap

```
momo/
  engine/
    renderer.h      + the screen's size, Rect, Color, MAX_LAYERS, MAX_RECTS; draw_rect
                      (MOMO_API) and rgb for the game; init takes an arena; shutdown, end_frame
    renderer.cpp    + the shaders and their errors, the buffer and vertex array,
                      blending, one instanced draw per frame; the shader compiler's
                      debug messages are off
    batch.h         new: RectInstance, Batch
    batch.cpp       new: init, clear, add, and the counting sort
    momo.h          + GameApi.render, which gets a const GameState*; includes renderer.h
    momo.cpp        + begin_frame, the game's render, end_frame; the renderer's
                      memory comes from the engine's arena
  game/
    game.cpp        + names four layers; render: the floor, the pillar, the bat and
                      its shadow; the log's row of dots is gone
  tests/
    batch_test.cpp  new: nine tests of drawing order
```

- Immediate mode: the game draws everything again every frame, and the renderer forgets it all once it's drawn. Nothing in either points into the other.
- The game draws only in `render`, once per frame, from a `const GameState*`. `draw_rect` outside a frame is an `ASSERT`.
- Positions are whole pixels on the 256×240 screen, `i32`s with `y` pointing down. The vertex shader maps them to clip space with the screen's size, and no matrix.
- Each rectangle is one instance, 20 bytes. The vertex shader makes its four corners from `gl_VertexID`, and a divisor of 1 makes the attributes move once per instance.
- The engine draws up to 8 layers, numbered back to front. The game gives them names and meanings, with an enum of its own.
- A counting sort puts the frame's rectangles in layer order, keeping call order inside each layer, and the frame draws with one call. 3,877 rectangles took 0.11 ms that way, against 1.6 ms LearnOpenGL's way.
- The batch never calls OpenGL, so its tests need no GPU.
- Colors are RGBA bytes with blending on. A shader that won't compile is logged once, by momo, with the shader's name.

The plan records `render` in [§4.1](plan.md#41-three-binaries-and-hot-reload), the batch, colors and shaders in [§4.5](plan.md#45-rendering), and the change to whole pixels and no matrix in [§4.6](plan.md#46-numbers). The outline now says this chapter also gave the game its `render` and the first shaders.

Chapter 11 makes the window resizable, and fits the 256×240 screen into it at a whole-number scale, with black bars, so screen pixels stay square and sharp at any window size and display scale.

## Exercises

Undo each change before the next exercise: `git checkout -- engine game` puts the code back.

1. **Measure it yourself.** At the start of `render`, draw a grid of 64×60 rectangles, 3×3 pixels each, 4 pixels apart, on `LAYER_BACKGROUND`. In `platform::init`, set the swap interval to 0. Run `make release` and `./build/release/momo` for ten seconds, and note the frame count. Then replace the draw in `end_frame` with one draw per rectangle: `glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, 0, 4, 1, i)` in a loop, which draws one instance starting at rectangle `i`. How many frames now? What does `GALLIUM_HUD=stdout,draw-calls` say for each? Do your numbers match the last two rows of the Problem's table?
2. **Full.** Make the grid in exercise 1 larger, so the frame draws more than `MAX_RECTS`. Where does momo stop, and what does the log say? Why can't `end_frame` simply draw what it has when the batch fills, and start again empty?
3. **A HUD.** At the start of `render`, draw a health bar in the top left corner on `LAYER_HUD`: a dark, see-through backdrop (alpha 160), then 10 small red segments on top of it. Why is it on top of everything, even though `render` draws it first? Then stretch the backdrop into a strip across the whole screen at the bat's height (`y = 132`, `h = 20`). What happens to the bat, and to the pillar, where the strip covers them?
4. **See-through pillar.** Give the pillar an alpha of 128. What do you see when the bat is behind it? Now draw the pillar on `LAYER_ENTITIES`, before the bat in the code, and still see-through. Which way looks right, and why does a depth buffer not help with this?
5. **The game calls the engine's half.** In `render`, call `renderer::begin_frame();`. What does `make` say, and at which step: compiling `game.cpp`, or linking `game.so`? Which line of the Makefile makes that happen ([plan §7](plan.md#7-build-dependencies-repo))?
6. **Half a pixel.** Set the window to 640×600 in `momo.cpp`, 2.5 times the screen, so a screen pixel should cover 2.5 window pixels. The GPU fills only whole pixels. Look closely at the bat while it flies, and at the floor. What stays even, and what doesn't? Why do the floor's tiles come out fine (16 × 2.5)? That's chapter 11's problem.
7. **More layers.** Add `LAYER_SKY` before `LAYER_BACKGROUND` in the game's enum, and draw a band of a lighter color across the top of the screen on it. Which engine files did you have to change? Now add layers until `LAYER_COUNT` is 9. What stops the build, and where? Take them out again, and pass `MAX_LAYERS` itself, 8, to one `draw_rect`. What stops momo this time, and why can't the compiler catch it?
