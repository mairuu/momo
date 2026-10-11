# 12 · Textures

> **Goal:** the bat becomes a picture: a 16×16 sprite with ears, fangs and two yellow eyes, loaded from a PNG file and flapping between two frames. Above it, READY blinks in momo's own 8×8 font, the way MegaMan 2 starts a stage. The renderer draws rectangles, sprites and text the same way, in a handful of draw calls a frame.

## The problem

Chapter 11's bat is five rectangles: a body, two wings and two one-pixel eyes. That's as much detail as rectangles can give. A real sprite is a picture, every pixel its own color. Drawn with `draw_rect`, the 16×16 bat this chapter loads would be 84 one-pixel rectangles, one for each pixel that isn't see-through, and every one of their colors would be a line in `game.cpp`.

Text is worse. The five letters of READY light up 144 pixels of an 8×8 font. A screen of text, 32 letters by 30 lines, lights up about 20,000, five times the 4,096 rectangles a frame can hold (chapter 10).

A **texture** is a picture on the GPU. Instead of one color, a rectangle can show part of a texture, and a 16×16 sprite or an 8×8 letter is one rectangle again. [Plan §4.5](plan.md#45-rendering) promised `renderer::draw_sprite(tex, src_rect, pos, layer, flags)`, and the outline gives this chapter two first textures: pictures loaded from PNG files with stb_image, and an 8×8 font written in code.

Chapter 10 also left a warning. Each sprite picks a texture, and changing something between draw calls is what costs. Two uniforms before each rectangle made a frame six times slower. A texture is one more thing to change. So the questions are: how does each rectangle pick its texture without giving up chapter 10's few draw calls? Where do plain rectangles fit? Who owns a texture, when the game's code can reload and its state can start over? And where does the font come from?

## Design

### A rectangle shows part of a texture

Each rectangle gets a **source**: a `Rect` in the texture's own pixels, the part of the picture it shows. The bat's PNG holds two frames side by side, 32×16 pixels in all, so the wings-up frame is `{ 0, 0, 16, 16 }` and the wings-down frame `{ 16, 0, 16, 16 }`. A picture made of many small ones like this is a **sprite sheet**.

The vertex shader already works out each corner of the rectangle on the screen (chapter 10). It works out the same corner of the source too, and turns it into **texture coordinates**, which run from 0 to 1 across the whole texture, by dividing by the texture's size:

```
texel = source.xy + corner * source.zw        corner (1, 1) of frame 1: (32, 16)
uv    = texel / texture size                  (32, 16) / (32, 16) = (1, 1)
```

The fragment shader then reads the texture at `uv`, LearnOpenGL's `texture(sampler, uv)`, and multiplies it by the rectangle's color.

A sprite is drawn at its source's size, so every screen pixel lands on exactly one texture pixel. Sprites never get bigger or smaller: the one place momo scales is the copy into the window (chapter 11). The plan's signature has `flags` for flipping a sprite. They'll come when the game needs them, when the player faces left (chapter 29). The bat looks the same from both sides.

### Rectangles of one color

`draw_rect` still draws rectangles of one color: the floor, the pillar, the shadow, and later chapter 13's text backdrop and chapter 19's hitboxes. With textures in the shader, where do they fit?

| Option | The catch |
|---|---|
| A flag in each rectangle: "no texture", which the fragment shader checks before reading one | A branch in the shader, a field in every instance, and something still has to be bound while it draws |
| **A texture of one white pixel. A rectangle of one color shows it, multiplied by its color.** | Every pixel of a plain rectangle reads a texture, to multiply by 1 |

momo takes the white pixel. White is 1.0 in every channel, so white times the rectangle's color is the color, and one shader draws everything. Hazel does the same, with a white texture it binds when a quad has none.

ZII settles the white pixel's number. It's the renderer's first texture, number 0, so a zeroed `Texture` draws plain rectangles. A texture that fails to load comes back as number 0 too, and its sprites draw as white rectangles: easy to spot, never a crash.

Fonts work the same way from the other side. The font's texture is white wherever a letter has a pixel and see-through everywhere else, so multiplying by the text's color colors the letters, and blending (chapter 10) leaves everything between them as it was.

### Which way is up

LearnOpenGL loads its pictures with `stbi_set_flip_vertically_on_load(true)`. stb_image hands over rows top first, the way the file stores them, and LearnOpenGL's quad puts texture coordinate (0, 0) at its bottom left corner, so without the flip its pictures came out upside down.

OpenGL itself has no up. It keeps a texture's rows in the order it's given them, and texture coordinate `y = 0` is the first row it was given. Which corner of the rectangle shows that row is up to the vertex shader. momo's puts `corner = (0, 0)` at the rectangle's top left (chapter 10), and the same `corner` picks the source's top left. So the picture's first row, its top, lands at the rectangle's top, and nothing needs flipping. Sources count from the top left, `y` down, the way `Rect` counts on the screen and the way every image editor counts a sprite sheet.

### One draw call per run

A frame's rectangles are still one buffer, sorted into layer order. Now each rectangle comes with a texture, and a draw call reads only the textures bound when it starts. While this chapter was being written, a throwaway version of momo drew this chapter's scene plus a grid of 3,840 small rectangles behind it, 3,880 a frame, in three different ways. A **run** here is a stretch of rectangles in a row, in drawing order, that use the same texture, drawn by one draw call. The release build ran under X11 with vsync off (chapter 11's `vblank_mode=0`), 8 seconds at a time, twice each:

| The grid's 3,840 rectangles | Draw calls | Frames a second | Each frame took |
|---|---|---|---|
| All plain rectangles, the white pixel like the floor, so they join the floor's run | 4 | 5,900 | 0.17 ms |
| The same, but one draw call per rectangle | 3,880 | 2,300 | 0.43 ms |
| Every other one cut from `bat.png` instead, so nearly every rectangle starts a new run | 3,784 | 500 | 2.0 ms |

The second row is chapter 10's lesson again: 3,876 more draw calls cost about a quarter of a millisecond. The third row is new: changing the texture before each draw call cost 1.6 ms more, about 0.4 µs per change. Changing a texture costs more than the draw call it comes with.

Ways to give each rectangle its texture:

| Option | The catch |
|---|---|
| One draw call per rectangle, binding its texture first | The third row's cost, for every frame |
| **One draw call per run, in drawing order, all from the one buffer** | A frame that switches textures back and forth inside a layer pays the third row's cost |
| **Texture slots**, Hazel's way: bind up to 32 textures at once, and let each rectangle's slot number pick one in the fragment shader. One draw call. | The shader language leaves it undefined (below), and a frame with more textures than slots still has to split |
| An **atlas**: pack every picture into one big texture | Something has to pack them, which is a job for chapter 30's bake tool, and the font is made at run time |
| An **array texture**: a stack of pictures in one texture, picked by a number | Every picture in the stack must be the same size |

The slots needed a closer look, since they draw everything in one call. A second throwaway version bound the three textures to units 0, 1 and 2, gave each rectangle its slot, and read `sprite_textures[slot]` in the fragment shader. GLSL 4.60 says an array of samplers may only be indexed by a value that's **dynamically uniform**: the same for every pixel the GPU works on together. AMD's GPU runs pixels in groups of 64, and a group can hold pixels of several rectangles, with different slots. That's undefined behavior.

On your Radeon, the result matched momo's draw calls in every one of the screen's 61,440 pixels. Mesa's shader compiler, asked to print the machine code (`AMD_DEBUG=ps,asm`), showed why: it turned the lookup into a loop. Take the slot of the first pixel still waiting (`v_readfirstlane_b32`), read that texture for every pixel with the same slot, set them aside, and go round again while any are left. On the same grid, the slots drew 5,900 frames a second against the runs' 515.

So slots are correct on Mesa, and fast. But nothing in GLSL promises the loop. A driver that takes the language at its word could read one texture for a whole group of pixels, and draw some of them from another rectangle's texture, with no error from the compiler or the debug callback. Your GPU has 32 slots, and OpenGL promises only 16, so a frame with more textures than that splits into draw calls anyway.

momo takes the runs. A frame of this game changes textures a handful of times: tiles from one tileset behind, sprites from a few sheets, letters from the font. This chapter's frame has 4 runs. The third row is a real cost, but one the game controls: what costs is interleaving textures inside a layer, and the game decides that. An atlas from chapter 30's bake tool would merge runs with no change to the renderer. If a profile ever shows texture changes costing time, slots can come back as a measured change.

The batch splits the runs after sorting, and a draw call reads its stretch of the one buffer by starting at its first rectangle's instance: OpenGL's **base instance**.

### A texture is a number

`GameState` must hold numbers, never pointers into the engine or into game code ([plan §3.2](plan.md#32-rules-for-game-code-required-by-hot-reload)). So a `Texture` is a number: an index into the renderer's table of textures, inside a struct of its own, so that passing a layer where a texture goes is a compile error. The game keeps the bat's texture in `GameState` like any other number.

How long does a texture live? The game loads its textures in `init`, which runs again whenever the engine starts the game over, after a reload changes `GameState`'s layout (chapter 06).

| Option | The catch |
|---|---|
| The game's textures last as long as the permanent arena: the engine deletes them whenever it starts the game over, and `init` loads them again | A restart reloads every file. The numbers would come out the same only if `init` loaded them in the same order. |
| **Each file is loaded once, for the whole run. Asking for the same path again returns the same texture.** | A texture can't be unloaded. A game that loads more than `MAX_TEXTURES`, 64, hits an `ASSERT`. |

momo takes the second. A game that starts over gets back the numbers it had, and loads nothing. Chapter 33 makes assets reload when their files change, and a table that remembers each texture's path is what it needs: it can replace a texture's pixels under the same number, and the game never knows. An NES-sized game has a handful of pictures, far from 64.

The renderer keeps its own copy of each path. The game's `"assets/bat.png"` is a string literal, which lives in `game.so`, and the next reload unloads it. That's chapter 06's rule about string literals, from the engine's side this time.

### Loading a PNG

stb_image is Sean Barrett's image loader: one header, public domain, and the library [plan §7](plan.md#7-build-dependencies-repo) lists. momo copies version 2.30 into `third_party/stb/` and compiles it once, as C, with two options:

- **PNG only** (`STBI_ONLY_PNG`). Every image editor saves PNG, it keeps every pixel exactly, and it has alpha. [Plan §6](plan.md#6-content) says runtime code loads only simple formats, which chapter 30's bake tool will produce. Until then, the game's pictures are PNGs, and stb_image decodes them when the game starts.
- **No `fopen`** (`STBI_NO_STDIO`). Reading a file is the platform's job ([plan §4.4](plan.md#44-platform-and-input)), so `platform::read_file` reads it through SDL, and stb_image decodes the bytes it's handed.

Where does the memory come from? The file's bytes are needed until stb_image has decoded them, and the pixels until OpenGL has copied them: both for less than one function call. The engine's arena is never reset, so it would keep every file it ever read. The frame arena is the game's. So both come from `malloc`, the C library's: SDL mallocs the file's bytes and stb_image the pixels, and `load_texture` gives both back before it returns. AddressSanitizer watches `malloc`'s memory, which it can't do inside an arena (chapter 04).

Paths are relative to the executable's folder, where `game.so` is, so a build runs from anywhere (chapter 02). The pictures live in `assets/`, next to `LICENSES.md`, and `make` copies each one into the build folder: `assets/bat.png` becomes `build/debug/assets/bat.png`. Chapter 30's bake tool will write its own formats there instead.

A file that's missing or isn't a PNG is an expected failure ([plan §3.3](plan.md#33-errors-and-strings)): the log says why, and `load_texture` returns texture 0.

### The font, in code

The font is the engine's: chapter 13's debug overlay is engine code and draws text, and the game draws text with it too, READY now, and a title later. [Plan §9](plan.md#9-out-of-scope) allows one font, 8×8 bitmaps.

Should the font be a PNG? The outline says code, and code wins. It needs no file, so it's there before any file loads and still works when the game's files don't, which is when you most want the debug overlay. It's also small: 95 glyphs, the printable ASCII chars from space to `~`, at 8 bytes each. Uppercase, lowercase, digits and punctuation.

The glyphs are `font8x8_basic`, by Daniel Hepper, in the public domain. They come from the IBM PC's VGA fonts. Each glyph is 8 bytes, its rows from top to bottom, and each bit of a row is a pixel. Bit 0, the lowest, is the leftmost pixel, so each row of the picture is its byte in binary, written backwards:

```
        binary      the row
0x0C    0000 1100   ..##....
0x1E    0001 1110   .####...
0x33    0011 0011   ##..##..
0x33    0011 0011   ##..##..
0x3F    0011 1111   ######..
0x33    0011 0011   ##..##..
0x33    0011 0011   ##..##..
0x00    0000 0000   ........        'A'
```

Most glyphs leave the right column and the bottom row empty, so letters drawn side by side don't touch.

When the renderer starts, it draws all 95 glyphs into one picture, 16 to a row in ASCII order, 128×48 pixels: white wherever a bit is set, see-through everywhere else. That picture becomes texture number 1. `draw_text` draws one sprite per char, cut from it. A char the font doesn't have, such as a newline or a byte of UTF-8, draws as `?`.

Turning glyphs into pixels and finding a char's place in the picture is a module of its own, `font::`, which never calls OpenGL, for the batch's and the screen fit's reason: its tests read what it makes, with no GPU.

## The code

### stb_image: `third_party/stb/`

Three files from the stb repository: `stb_image.h`, its `LICENSE`, and a `README.md` that gives the version, the commit it came from, and the file's SHA-256, so you can check that it's the released file. The fourth is momo's:

`third_party/stb/stb_image.c:13`

```c
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO

#pragma GCC diagnostic ignored "-Wunused-function"

#include "stb_image.h"
```

stb_image is a **header-only** library. Every file that includes `stb_image.h` gets its declarations, and the one file that defines `STB_IMAGE_IMPLEMENTATION` first gets its code too. This is that file. With only PNG, two helper functions the other formats use are never called, and `-Wall` warns about unused static functions. The `#pragma` turns that warning off in this file only, since the code isn't momo's to change. The comment above it says so.

### The Makefile

stb_image is compiled the way glad is (chapter 08): as C, with `-fPIC`, nothing exported, and the build config's flags, so the debug build runs it under the sanitizers. `STB_INCLUDE` gives the engine `<stb_image.h>`, and `compile_commands.json` lists `stb_image.c` too. Both vendored libraries now go into `libmomo.so` through one variable, `VENDOR_OBJ`.

`Makefile:105`

```make
ASSETS     := $(wildcard assets/*.png)
ASSETS_OUT := $(ASSETS:%=$(OUT)/%)
```

Every PNG in `assets/`, and where its copy goes in the build folder. `all` depends on the copies, and one rule makes them, at `Makefile:221`: `cp $< $@`, after making the folder.

### Reading a file: `engine/platform.cpp`

`engine/platform.cpp:199`

```cpp
void* read_file(const char* path, i64* size)
{
    size_t file_size = 0;
    void* data = SDL_LoadFile(path, &file_size);
    if (!data) {
        LOG_ERROR("%s", SDL_GetError());
        return nullptr;
    }
    *size = (i64)file_size;
    return data;
}

void free_file(void* data)
{
    SDL_free(data);
}
```

Without its comment. `SDL_LoadFile` asks the file for its size, mallocs that much plus a byte, reads it all, and puts a 0 in the extra byte. SDL's error message names the file and says why it couldn't read it. The header (`engine/platform.h:89`) says the memory isn't an arena's, and to give it back as soon as you're done.

### Textures for the game: `engine/renderer.h`

`engine/renderer.h:61`

```cpp
struct Texture {
    i32 index;
};

constexpr i32 MAX_TEXTURES = 64;

constexpr i32 GLYPH_SIZE = 8;
```

Without their comments, which say what Design said: a texture is a number, the zero texture is the white pixel, and a failed load is the zero texture too. `GLYPH_SIZE` is here because the game lays out text with it.

`engine/renderer.h:80`

```cpp
MOMO_API Texture load_texture(const char* path);

MOMO_API void draw_rect(Rect rect, Color color, i32 layer);
MOMO_API void draw_sprite(Texture texture, Rect source, i32 x, i32 y, i32 layer);
MOMO_API void draw_text(Str text, i32 x, i32 y, Color color, i32 layer);
```

The game's four renderer functions, without their comments. `draw_sprite` takes a position, not a rectangle: the sprite is the size of its source. `draw_text` takes a `Str` (chapter 05), so a literal goes in as `STR("READY")`, and text built with `str::fmt` in the frame arena works the same way.

### Runs: `engine/batch.h`

`engine/batch.h:14`

```cpp
struct RectInstance {
    Rect rect;
    Rect source;
    Color color;
};

struct Batch {
    RectInstance* rects; // room for capacity of them, from an arena
    i32* layers;         // each rectangle's layer, 0 to MAX_LAYERS - 1
    Texture* textures;   // each rectangle's texture
    i32 count;
    i32 capacity;
};

struct DrawCall {
    Texture texture;
    i32 first; // the first one's place in drawing order
    i32 count;
};
```

`RectInstance` grows from 20 bytes to 36 with the source. The texture goes in an array of its own, like the layer, since the GPU never reads either: the renderer binds the texture before the draw call. `batch::add` now takes a whole `RectInstance`, a texture and a layer. Passing the instance's three fields one by one would put two `Rect`s side by side in the argument list, easy to swap, and a designated initializer names each one instead. `sort` moves each rectangle's texture along with it, into a second output array (`engine/batch.cpp:34`).

`engine/batch.cpp:61`

```cpp
i32 draw_calls(const Texture* textures, i32 count, DrawCall* calls)
{
    i32 call_count = 0;
    for (i32 i = 0; i < count; i++) {
        bool same = call_count > 0 && calls[call_count - 1].texture.index == textures[i].index;
        if (same) {
            calls[call_count - 1].count++;
        } else {
            calls[call_count] = { .texture = textures[i], .first = i, .count = 1 };
            call_count++;
        }
    }
    return call_count;
}
```

Without its comment. One pass over the sorted textures: a rectangle joins the last draw call if it has the same texture, and starts a new one if not. The first rectangle always starts one, since there's no last draw call yet.

### The font: `engine/font.cpp`

`engine/font.cpp:21`

```cpp
constexpr u8 GLYPHS[][GLYPH_SIZE] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // space
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00 }, // !
    ...
    { 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // ~
};
static_assert(ARRAY_COUNT(GLYPHS) == LAST_CHAR - FIRST_CHAR + 1, "one glyph for each char");
```

95 lines of glyphs, with the comment above them giving the diagram from Design and where the glyphs come from. The `static_assert` checks while compiling that there's one glyph for each char from `' '` to `'~'`. One glyph's comment says `backslash` rather than showing it: a `//` comment that ends in `\` carries on into the next line, which would comment out the next glyph.

`engine/font.cpp:129`

```cpp
Rect glyph(char c)
{
    i32 code = (u8)c;
    if (code < FIRST_CHAR || code > LAST_CHAR)
        code = '?';
    return cell(code - FIRST_CHAR);
}
```

Without its comment. `char` is signed on x86, so a byte past 127, like the bytes UTF-8 writes an `é` with, would be a negative number. As a `u8` it's 128 to 255, past `'~'`, and comes out as `?` like any other char the font doesn't have. clang-tidy flags turning a plain `char` straight into an `i32` (`bugprone-signed-char-misuse`), which is how this line got its cast. `cell` (`engine/font.cpp:121`) turns a glyph's number into its place: `index % 16` across, `index / 16` down, 8 pixels each.

`fill_pixels` (`engine/font.cpp:140`) first makes every pixel see-through, including the one cell after `~` that no glyph uses, then sets a white pixel for every bit that's set: `(GLYPHS[i][row] >> column) & 1`.

### The shaders: `engine/renderer.cpp`

`engine/renderer.cpp:37`

```glsl
layout(location = 0) uniform vec2 screen_size; // 256, 240
layout(binding = 0) uniform sampler2D sprite_texture;

layout(location = 0) in ivec4 rect;   // x, y, w, h, in screen pixels
layout(location = 1) in ivec4 source; // x, y, w, h, in the texture's pixels
layout(location = 2) in vec4 color;   // red, green, blue, alpha, 0 to 1

out vec2 uv;
out vec4 corner_color;

void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec2 pixel = rect.xy + corner * rect.zw;

    vec2 clip = pixel / screen_size * 2.0 - 1.0;
    gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0);

    vec2 texel = source.xy + corner * source.zw;
    uv = texel / vec2(textureSize(sprite_texture, 0));
    corner_color = color;
}
```

The vertex shader, without its comments. New are `source`, a second `ivec4` in the instance, the sampler, and the last three lines of `main`. `layout(binding = 0)` ties the sampler to texture unit 0 in the shader itself, the way `layout(location = ...)` ties the inputs, so no `glUniform1i` is needed to say which unit it reads. `textureSize` asks the bound texture for its size, so the shader never needs it as a uniform.

The fragment shader (`engine/renderer.cpp:80`) is LearnOpenGL's with a multiplication:

```glsl
pixel_color = texture(sprite_texture, uv) * corner_color;
```

### Making a texture

`engine/renderer.cpp:263`

```cpp
static Texture make_texture(i32 width, i32 height, const void* rgba, Str path)
{
    ASSERT(texture_count < MAX_TEXTURES);

    u32 gl_texture = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &gl_texture);
    glTextureStorage2D(gl_texture, 1, GL_RGBA8, width, height);
    glTextureSubImage2D(gl_texture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    glTextureParameteri(gl_texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(gl_texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(gl_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(gl_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    textures[texture_count] = { .gl_texture = gl_texture, .path = path };
    texture_count++;
    return { .index = texture_count - 1 };
}
```

Without its comments. The first three calls are chapter 11's screen texture plus its pixels: `glTextureSubImage2D` copies `width × height` pixels of four bytes into level 0, from the top left. OpenGL copies them while the call runs, so the caller can free them right after.

The filters are LearnOpenGL's, in DSA. `NEAREST` makes each screen pixel take the color of the one texture pixel it lands in. `MAG_FILTER` is for drawing a texture at its own size or bigger, which is all momo does. `MIN_FILTER`, for drawing smaller, is set too, so there's no default to look up. Its default reads **mipmaps**, smaller copies of the texture for drawing it small, and a texture made with `glTexImage2D` that has none, and keeps that default, reads as black. LearnOpenGL never hits that, since it calls `glGenerateMipmap`. `glTextureStorage2D`'s textures don't have the problem: one made with one level has exactly one, and it's complete. Exercise 1 shows the difference. `CLAMP_TO_EDGE` means that past the texture's edge it repeats its edge pixels instead of wrapping around to the far side.

`textures` is the table (`engine/renderer.cpp:134`): each entry is OpenGL's number for the texture, and the path it was loaded from.

### Loading one

`engine/renderer.cpp:291`

```cpp
Texture load_texture(const char* path)
{
    Str wanted = { .data = path, .len = (i32)strlen(path) };
    for (i32 i = 0; i < texture_count; i++) {
        if (str::eq(textures[i].path, wanted))
            return { .index = i };
    }

    Str full_path = str::fmt(arena, "%s%s", platform::executable_folder(), path);
    i64 size = 0;
    void* file = platform::read_file(full_path.data, &size);
    if (!file)
        return {}; // already logged

    i32 width = 0;
    i32 height = 0;
    u8* pixels = stbi_load_from_memory((const u8*)file, (i32)size, &width, &height, nullptr, 4);
    platform::free_file(file);
    if (!pixels) {
        LOG_ERROR("can't load %s: %s", path, stbi_failure_reason());
        return {};
    }

    Texture texture = make_texture(width, height, pixels, str::fmt(arena, "%s", path));
    stbi_image_free(pixels);
    LOG_INFO("loaded %s, %dx%d, as texture %d", path, width, height, texture.index);
    return texture;
}
```

Without its comments. First the table: a path loaded before returns its number. The two textures the renderer made itself have empty paths, which no file's path matches. Then the full path, the file's bytes, and stb_image. The last argument, 4, asks for four bytes per pixel, red, green, blue and alpha, whatever the file holds. The `nullptr` is where stb_image would say what the file held, which momo doesn't need. `stbi_failure_reason` is stb_image's one-line reason for its last failure. Last, the texture, with the game's path copied into the engine's arena, which the renderer now keeps from `init` (`engine/renderer.cpp:119`).

### The renderer's own two

`engine/renderer.cpp:437`

```cpp
    constexpr Color WHITE = rgb(0xffffff);
    Texture white = make_texture(1, 1, &WHITE, {});

    Color font_pixels[FONT_WIDTH * FONT_HEIGHT];
    font::fill_pixels(font_pixels);
    Texture font_texture = make_texture(FONT_WIDTH, FONT_HEIGHT, font_pixels, {});

    ASSERT(white.index == WHITE_TEXTURE.index && font_texture.index == FONT_TEXTURE.index);
```

At the end of `init`. The white pixel is one `Color`, four bytes, the format `make_texture` takes. The font's picture is 24 KB, small enough for the stack, since OpenGL copies it. `WHITE_TEXTURE` and `FONT_TEXTURE` (`engine/renderer.cpp:104`) are the numbers the renderer uses for them, and the `ASSERT` checks that they came out that way. `shutdown` deletes every texture in the table.

`init` also pushes two more arrays from the engine's arena: the sorted textures, and the draw calls, room for one per rectangle. The vertex array gets `source` like `rect`, four ints read as ints (`engine/renderer.cpp:407`).

### Drawing

`engine/renderer.cpp:490`

```cpp
void draw_sprite(Texture texture, Rect source, i32 x, i32 y, i32 layer)
{
    ASSERT(in_frame);
    ASSERT(texture.index >= 0 && texture.index < texture_count);
    RectInstance instance = {
        .rect = { .x = x, .y = y, .w = source.w, .h = source.h },
        .source = source,
        .color = rgb(0xffffff), // the texture's own colors
    };
    batch::add(&batch, instance, texture, layer);
}
```

The second `ASSERT` catches a texture number the renderer never gave out. `draw_rect` (`engine/renderer.cpp:477`) is the same with the white texture, its source `{ 0, 0, 1, 1 }` and its color. `draw_text` (`engine/renderer.cpp:502`) adds one rectangle per char, 8 pixels apart, each with its glyph's source from `font::glyph`, the text's color and the font's texture.

`engine/renderer.cpp:539`

```cpp
    for (i32 i = 0; i < call_count; i++) {
        DrawCall call = calls[i];
        glBindTextureUnit(SPRITE_TEXTURE_UNIT, textures[call.texture.index].gl_texture);
        glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, 0, 4, call.count, (u32)call.first);
    }
```

The end of `draw_batch`, where chapter 10's one `glDrawArraysInstanced` was. Before it, `draw_batch` sorts, splits the runs, uploads the whole sorted frame in one `glNamedBufferSubData` as before, and binds the framebuffer, program and vertex array. Then one draw call per run. `glBindTextureUnit` is DSA's way to bind a texture to a unit, without `glActiveTexture`. `BaseInstance` is where the run starts in the buffer: instance 0 of this draw call reads `RectInstance` number `first`.

### The game: `game/game.cpp`

`game/game.cpp:18`

```cpp
    Texture bat_texture; // bat.png, a number the renderer gave out (chapter 12)
```

`GameState`'s new field. It grows from 12 bytes to 16, which the engine notices by itself, so `GAME_STATE_VERSION` stays 1. `init` fills it in: `renderer::load_texture("assets/bat.png")` (`game/game.cpp:50`).

`game/game.cpp:91`

```cpp
    i32 frame = (state->tick / 8) % 2;
    Rect source = { .x = frame * BAT_SIZE, .y = 0, .w = BAT_SIZE, .h = BAT_SIZE };
    renderer::draw_sprite(state->bat_texture, source, x, BAT_Y, LAYER_ENTITIES);

    Str ready = STR("READY");
    if ((state->tick / 16) % 2 == 0)
        renderer::draw_text(ready, (SCREEN_WIDTH - ready.len * GLYPH_SIZE) / 2, 64, TEXT_COLOR,
                            LAYER_HUD);
```

The body, wings and eyes are gone, and so are their colors. The flap is the same 8 ticks, but now picks a frame of the sprite sheet instead of moving two rectangles. READY is 40 pixels wide, centered, on for 16 ticks and off for 16. It stays until chapter 28's game flow gives it a stage to start. The shadow is still a `draw_rect`.

### The bat: `assets/bat.png`

32×16 pixels, two frames, in chapter 11's colors: the purple body, darker wings, yellow eyes, and two white fangs. The wings are up in the first frame and down in the second:

```
................   ................
W..............W   ................
WW...B....B...WW   .....B....B.....
WWW..BB..BB..WWW   .....BB..BB.....
WWWW.BBBBBB.WWWW   .....BBBBBB.....
.WWWBBEBBEBBWWW.   ....BBEBBEBB....
..WWBBBBBBBBWW..   ...WBBBBBBBBW...
...WBBBBBBBBW...   ..WWBBBBBBBBWW..
....BBBBBBBB....   .WWWBBBBBBBBWWW.
.....BBBBBB.....   WWWW.BBBBBB.WWWW
......F..F......   WWW...F..F...WWW
................   WW............WW
................   W..............W
```

B is the body, W the wings, E the eyes and F the fangs. The dots are see-through, alpha 0, and the bottom three rows of each frame are empty. A short throwaway script wrote the PNG from this picture, since no image editor was at hand. Open it in any editor to change it. `assets/LICENSES.md` lists it, as every asset will be: drawn for momo, under momo's license, which [plan §7](plan.md#7-build-dependencies-repo) leaves for when the book is published.

### The tests

`tests/font_test.cpp` is new: eight tests of where glyphs are and what the picture holds. Space is at the top left, `/` ends the first row, `A` is on the third row, `~` is last, a newline and a byte of UTF-8 come out as `?`, the top row of `A` is `..##....`, every pixel is white or see-through, and the cell after `~` is empty even when the memory started out holding something else.

`tests/batch_test.cpp` changes with the batch, and gets five more tests, among them:

`tests/batch_test.cpp:217`

```cpp
static void a_texture_used_again_later_starts_another_draw_call()
{
    // The draw calls keep drawing order, so a texture that comes back after
    // another one needs a draw call of its own again.
    Texture textures[] = { WHITE, WHITE, BAT, WHITE };
    DrawCall calls[CAPACITY] = {};
    CHECK_EQ(batch::draw_calls(textures, ARRAY_COUNT(textures), calls), 3);
    ...
}
```

The `...` checks each of the three draw calls: white for the first two rectangles, the bat for one, white again for one. Then `the_frame_from_chapter_12` draws this chapter's frame in `game.cpp`'s order, the floor's 32 tiles, the pillar, the shadow, the bat and READY, and checks that it comes out as 4 draw calls: the floor and the shadow, the bat, the pillar, the letters.

## Run it

### The bat and READY

```
$ make
...
cp assets/bat.png build/debug/assets/bat.png
$ ./build/debug/momo
engine/momo.cpp:82: reserved 64 MB at 0x7bf99a400000
engine/momo.cpp:206: loaded /home/mairuu/repos/momo/build/debug/game.so
engine/platform.cpp:86: SDL 3.4.18, video driver wayland, window 716x860, 1432x1720 pixels, display scale 2, 120 Hz
engine/renderer.cpp:346: OpenGL 4.6 (Core Profile) Mesa 26.2.4-arch1.1, AMD Radeon 880M Graphics (radeonsi, strix1, ACO, DRM 3.64, 7.2.9-arch1-1)
game/game.cpp:47: init
engine/renderer.cpp:326: loaded assets/bat.png, 32x16, as texture 2
engine/renderer.cpp:555: the screen at 5x: 1280x1200 pixels, in a window of 1432x1720
```

Texture 2: the white pixel and the font are 0 and 1. The bat flaps across the screen as before, now with ears and fangs, and READY blinks above the pillar, white, twice a second.

### Look closer

`grim` took a picture of the window, as in chapter 11, and a script read the color at the center of every screen pixel. The window had settled at 1420×1712 pixels, so the screen starts at window pixel (70, 256), at 5x. READY's `R` starts at screen pixel (108, 64), and its 8×8 pixels came out as:

```
######..      0x3F
.##..##.      0x66
.##..##.      0x66
.#####..      0x3E
.##.##..      0x36
.##..##.      0x66
###..##.      0x67
........      0x00
```

The glyph's bytes are on the right, and each row is its byte written backwards, as Design said. Every one of those screen pixels is a 5×5 square of one color. The bat was at screen x = 235, wings down, and all 84 of its opaque pixels matched the second frame of `bat.png` exactly.

### Draw calls

Mesa counts draw calls itself (chapter 10):

```
$ GALLIUM_HUD=stdout,draw-calls ./build/release/momo
draw calls: 5
draw calls: 5
...
```

That's an average over half a second. With READY showing all the time, the count is 6, and with it hidden, 5. Chapter 11's frame, with its one draw call, counts 3, so Mesa counts 2 of its own in every frame. That leaves this chapter's 4 runs with READY and 3 without, as `the_frame_from_chapter_12` says.

### A missing file

Take the bat's copy out of the build folder, `mv build/debug/assets/bat.png /tmp`, and run it:

```
game/game.cpp:47: init
engine/platform.cpp:206: error: Couldn't open /home/mairuu/repos/momo/build/debug/assets/bat.png: No such file or directory
engine/renderer.cpp:555: the screen at 5x: 1280x1200 pixels, in a window of 1432x1720
```

momo keeps running. The bat is a white 16×16 square, flying back and forth: texture 0, its source clamped to the one white pixel. Now put a file there that isn't a picture, `echo "not a picture" > build/debug/assets/bat.png`:

```
engine/renderer.cpp:317: error: can't load assets/bat.png: unknown image type
```

stb_image's reason is short, and the log line around it says which file. To get the bat back, delete the broken copy and run `make`, which copies `assets/bat.png` again. `make` alone wouldn't: it copies a picture only when the copy is missing or older than the original, and the broken one is newer.

### Starting over

With momo running, add a field to `GameState`, such as `i32 lives;` after `bat_texture`, and `make`:

```
engine/momo.cpp:189: warning: GameState changed from version 1, 16 bytes, to version 1, 20 bytes: starting the game over
game/game.cpp:48: init
```

The game started over, and `init` asked for `assets/bat.png` again. No "loaded" line: the renderer handed back texture 2 without reading the file. Take the field out again.

### Memory

```
engine/momo.cpp:88: engine    arena: at most 393326 bytes used of 8 MB
```

Chapter 11 used 180,268 bytes. A rectangle grew from 20 bytes to 36, and there's one more array of textures and one of draw calls: 4,096 rectangles each in the batch (36 + 4 + 4 bytes), sorted (36 + 4) and as draw calls (12) is 393,216 bytes. The rest is paths: `game.so`'s, as before, and the bat's twice, as the game wrote it and in full. The textures themselves live in the driver's memory, like chapter 11's screen.

### The tests

```
$ make test
...
batch_test
  ...
  sort_copies_the_source_and_color_with_the_rect ... ok
  sort_moves_each_texture_with_its_rect ... ok
  ...
  no_rects_need_no_draw_calls ... ok
  one_texture_is_one_draw_call ... ok
  a_texture_used_again_later_starts_another_draw_call ... ok
  the_frame_from_chapter_12 ... ok
  14 passed
font_test
  space_is_at_the_top_left ... ok
  a_row_holds_16_chars ... ok
  a_is_on_the_third_row ... ok
  tilde_is_the_last_char ... ok
  chars_the_font_lacks_draw_as_a_question_mark ... ok
  the_picture_has_the_top_of_a_where_glyph_says ... ok
  every_pixel_is_white_or_see_through ... ok
  the_cell_after_tilde_is_empty ... ok
  8 passed
...
```

## Recap

```
momo/
  third_party/stb/
    stb_image.h     new: stb_image 2.30, as released
    stb_image.c     new: its code, compiled once, PNG only, no fopen
    LICENSE         new
    README.md       new: the version, the commit and the checksum
  assets/
    bat.png         new: the bat, two frames of 16x16
    LICENSES.md     new: every asset, where it came from, and its license
  engine/
    font.h          new: the font's layout, 16 glyphs to a row
    font.cpp        new: 95 glyphs from font8x8; glyph, fill_pixels
    platform.h/cpp  + read_file and free_file, through SDL
    renderer.h      + Texture, MAX_TEXTURES, GLYPH_SIZE; load_texture, draw_sprite, draw_text
    renderer.cpp    + the texture table, the white pixel and the font; shaders read a
                      texture; one draw call per run of the same texture
    batch.h/cpp     + each rectangle's source and texture; DrawCall, draw_calls
    momo.h          comments: init loads textures; render draws sprites and text too
  game/
    game.cpp        the bat is a sprite from bat.png; READY blinks
  tests/
    font_test.cpp   new: eight tests of the font
    batch_test.cpp  + sources, textures, and five tests of draw calls
  Makefile          + stb_image; copies assets/*.png into the build folder
```

- A rectangle shows its **source**, a part of a texture, in the texture's own pixels. A sprite is drawn at its source's size.
- Rectangles of one color show a texture of one white pixel, number 0, so one shader draws everything, and a zeroed `Texture` draws plain rectangles.
- Texture rows go to OpenGL top first, as stb_image gives them, and the vertex shader puts the source's top at the rectangle's top. Nothing is flipped.
- The frame is one buffer, sorted by layer, drawn with one draw call per run of rectangles that share a texture. Changing textures costs about 0.4 µs each, so what costs is switching back and forth inside a layer.
- Texture slots drew correctly on your Radeon, in one draw call, because Mesa turns the lookup into a loop. GLSL doesn't promise that, so momo doesn't rely on it.
- A `Texture` is a number in the renderer's table. Each file is loaded once, for the whole run, so a game that starts over gets back the numbers it had.
- PNGs are decoded by stb_image, from bytes `platform::read_file` reads. Both live in `malloc`'s memory for one function call. A file that can't be loaded logs why and draws white.
- The 8×8 font is 95 glyphs in code, made into texture 1 when the renderer starts. `draw_text` draws a sprite per char.

The plan records textures, runs and the font in [§4.5](plan.md#45-rendering), the memory for loading in [§4.2](plan.md#42-memory), reading files in [§4.4](plan.md#44-platform-and-input), PNGs in [§6](plan.md#6-content), and stb_image in [§7](plan.md#7-build-dependencies-repo). The outline now says this chapter also gave the game `draw_text` and its first picture.

Chapter 13 puts the font to work in the engine's debug overlay, with frames and ticks per second, and adds `--capture`, which saves the screen to a file.

## Exercises

Undo each change before the next exercise: `git checkout -- engine game assets` puts the code and the bat back.

1. **Black boxes.** In `make_texture`, replace `glTextureStorage2D` and `glTextureSubImage2D` with LearnOpenGL's two calls: `glBindTexture(GL_TEXTURE_2D, gl_texture);` and `glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);`. Delete the `MIN_FILTER` line, so the default, `GL_NEAREST_MIPMAP_LINEAR`, stays. What do the bat and READY look like? Does the debug callback say anything? Why do the floor and the pillar still draw in color, though they read a texture too? (How many mipmap levels does a 1×1 texture need?) Then put `MIN_FILTER` back and run again.
2. **Linear.** Change both filters in `make_texture` to `GL_LINEAR`, the smoothing filter chapter 11's compositor used. Predict what happens to the bat's edges and to READY, then run it and look closely. Explain what you see, using where each screen pixel's center lands in the texture. When would `GL_LINEAR` make a difference? Think of a sprite drawn at x = 10.5, or at twice its size.
3. **Upside down.** Call `stbi_set_flip_vertically_on_load(1);` at the start of `load_texture`. What happens to the bat, and to its flapping? What happens to READY, and why?
4. **Count the runs.** Before you run each change, work out how many draw calls the frame needs, then check with `GALLIUM_HUD=stdout,draw-calls` (remember Mesa's 2): draw READY on `LAYER_BACKGROUND`; draw the shadow after the bat instead of before it; draw a second bat on `LAYER_FOREGROUND`, in front of the pillar.
5. **The whole font.** Make `render` draw all 95 chars, 16 to a line, using `draw_text` with a `Str` that points into a `char` array on the stack. Find the glyphs you'd want to change for a game, and look at how `g`, `j`, `p`, `q` and `y` use the bottom row.
6. **A third frame.** Give the bat a third frame, its wings level, in `assets/bat.png` (make it 48×16), and flap through all three. While momo runs, `make` copies the new PNG. Does the running bat change? What would the engine have to do to notice? That's chapter 33.
