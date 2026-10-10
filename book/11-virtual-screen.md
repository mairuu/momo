# 11 · Virtual Screen

> **Goal:** the window can be any size. However you size it, the game's 256×240 screen fills as much of it as a whole-number scale allows, and black bars fill the rest. Every screen pixel is a square of window pixels, all one color, on any display scale. The bat gets two eyes of one pixel each, to prove it.

## The problem

Chapter 10's window is 768×720 and can't be resized, so Hyprland floats it at that size. The game draws on a 256×240 screen, and the window is exactly three times that. Two things break once the window can be any size.

The first is a window that isn't a whole multiple of the screen. Chapter 10's exercise 6 tried 640×600, 2.5 times the screen. The GPU fills only whole pixels, so a screen pixel can't be 2.5 window pixels wide: it's 2 or 3, depending on where it lands. A 16-pixel tile always comes out 40 window pixels wide, but the bat, which moves one screen pixel per tick, moves 2 window pixels on one tick and 3 on the next.

The second is a surprise: momo has never drawn at your display's own pixels. Your laptop's panel has a scale of 2. Hyprland shows each of the window's 768×720 pixels as 2×2 of the panel's, so the window covers 1536×1440 panel pixels. Chapter 10 said that made every screen pixel exactly 6×6 panel pixels. That's true of the geometry, but not of the colors. While this chapter was being written, `grim` took a picture of chapter 10's window, and a short script read the pixels on one row, across the pillar's left edge:

| Panel pixel | 717 | 718 | 719 | 720 | 721 | 722 |
|---|---|---|---|---|---|---|
| Color | `0f3840` | `0f3840` | `2e4d52` | `6b7577` | `8a8a8a` | `8a8a8a` |
| What it is | sky | sky | a mix | a mix | pillar | pillar |

The pillar starts at screen pixel 120, which is window pixel 360 and panel pixel 720. A sharp edge would go straight from the sky's `0f3840` to the pillar's `8a8a8a` there. Instead, two panel pixels are mixtures of the two, a quarter and three quarters of the way from sky to pillar. Hyprland stretches a window's picture with a smoothing filter, **linear filtering**, which mixes each pixel with its neighbors by how close they are, so every edge in the picture gets two pixels of blur. On the external monitor, at scale 1.67, the stretch isn't even a whole number, which is why chapter 10 looked soft there.

[Plan §4.5](plan.md#45-rendering) already says what to do: a *256×240 virtual screen, scaled up by whole numbers, with black bars*. This chapter works out how, and what a pixel is on a display with a scale.

## Design

### Points and pixels

A display's **scale** tells programs how big to draw. At scale 2, a program sizes everything in **points**, and the display shows each point as 2×2 of its pixels. Text is then as big on your 2880×1800 panel as on a 1440×900 one, only sharper. At scale 1.67, a point is 5/3 of a pixel each way.

SDL's window sizes are in whatever units the display server uses. On Wayland that's points: 768×720 points is 1536×1440 pixels on your panel. By default, SDL gives the window one pixel per point, 768×720 pixels. The compositor then stretches that picture over the 1536×1440 pixels the window covers, which is the blur above.

`SDL_WINDOW_HIGH_PIXEL_DENSITY` asks SDL for every pixel instead. The window is still sized in points, but OpenGL draws into all the pixels under it, and SDL reports two sizes: `SDL_GetWindowSize` in points, and `SDL_GetWindowSizeInPixels` in pixels. A small test program opened a resizable window both ways, and under X11 too, and printed what SDL said:

| | Window size | Size in pixels | Display scale |
|---|---|---|---|
| Wayland | 716×860 | 716×860 | 1 |
| Wayland, `HIGH_PIXEL_DENSITY` | 716×860 | 1432×1720 | 2 |
| X11, either way | 768×720 | 768×720 | 2 |

Hyprland tiles a resizable window, so both Wayland windows got 716×860 points, half the workspace, instead of the 768×720 they asked for. Without the flag, SDL even reports a scale of 1, and from the window's side that's true. With it, the window has four times the pixels.

X11 has no points. Its window sizes are pixels, and `HIGH_PIXEL_DENSITY` changes nothing. An X11 program finds out about a display's scale from a setting such as `Xft.dpi`, which your Hyprland config sets to 192, twice the standard 96. SDL reads it, hence the 2. Scaling up after that is the program's job. Your config also sets `force_zero_scaling`, so Hyprland shows XWayland windows pixel for pixel instead of stretching them. That's why chapter 07's exercise 2 found the X11 window half the size of the Wayland one: 768×720 pixels, where Wayland's was 768×720 points.

So momo asks for every pixel, and from then on works only in pixels. The scale is chosen from the window's size in pixels. Points matter only once, for the size the window asks for when it opens.

### Whole numbers, and black bars

A window has some number of pixels, and the screen has 256×240. How should one fill the other?

| Option | The catch |
|---|---|
| Stretch the screen over the whole window | Unless the window has the screen's shape, everything gets wider or taller than it should be |
| Keep the shape, at whatever scale fits, such as 2.5 | Screen pixels come out uneven, 2 and 3 window pixels wide in turn (chapter 10's exercise 6). Smoothing hides that by blurring every edge instead, which is what the compositor did. |
| **The largest whole-number scale that fits, centered, with black around it** | Part of the window stays black |

momo takes the plan's choice, the third. The scale is how many whole screens fit across, or down, whichever is fewer:

```
scale = min(window_width / 256, window_height / 240)     integer division, rounding down
```

Then the screen is `256 × scale` by `240 × scale`, centered, with the leftover split between the two sides. Some windows:

| Window, in pixels | Screens across | Down | Scale | The screen | Bars, each side |
|---|---|---|---|---|---|
| 768×720 | 3 | 3 | 3 | 768×720 | none |
| 1432×1720, a tile on your panel | 5.6 | 7.2 | 5 | 1280×1200 | 76 left and right, 260 above and below |
| 1920×1080, a common monitor | 7.5 | 4.5 | 4 | 1024×960 | 448 left and right, 60 above and below |
| 200×100 | 0.8 | 0.4 | 1 | 256×240 | none: the screen is cut off |

The last row is a window too small to show the screen even at scale 1. A scale of 0 would show nothing, so momo uses 1 and keeps the screen centered, with its edges cut off. Shrinking it below 1 would drop screen pixels here and there, and a one-pixel bullet could vanish.

Bars are what NES games on modern TVs look like, and the price of sharp pixels. On your 1432×1720 tile, the screen fills 62% of the window, and how much any window fills depends on how close its shape is to the screen's.

### A screen of its own

Where does the game draw now? Chapter 10 drew straight into the window, which was exactly three screens. Two options:

| Option | The catch |
|---|---|
| Keep drawing into the window, with the viewport set to where the scaled screen goes | Each frame sets the viewport from the window's size. A clear ignores the viewport, so clearing to the sky colors the bars too, unless the scissor test stops it. And there's no 256×240 picture anywhere: the screen is only a part of the window. |
| **Draw into a 256×240 picture of its own, then copy it into the window, scaled up** | One more texture and framebuffer, and a copy every frame |

momo takes the second. The screen becomes a real thing: a 256×240 texture, exactly that size whatever the window does. The game draws on it as before, and nothing in the game or the shaders changes. The vertex shader still maps pixels with a `screen_size` of 256×240, which is now the size of what it draws into. Chapter 13's `--capture` can then read the screen at its own size, 256×240, the same picture on every display.

A **framebuffer** is what OpenGL draws into: a set of images, the color one and, if you ask for them, depth and stencil. Framebuffer 0 is the window's, and came with the OpenGL context (chapter 08). You can make framebuffers of your own and **attach** a texture as the color image. Drawing then goes into the texture instead of the window. LearnOpenGL's "Framebuffers" chapter, in its Advanced OpenGL part, does this to add post-processing. It's the step after the "Getting Started" chapters you've read.

The copy is a **blit**. `glBlitNamedFramebuffer` copies a rectangle of pixels from one framebuffer into a rectangle of another, scaling it to fit. Asked for `GL_NEAREST`, it gives each window pixel the color of the screen pixel its center falls in. At a whole-number scale, every screen pixel becomes exactly `scale × scale` window pixels, all one color. LearnOpenGL draws a quad over the whole window with the texture on it instead, which needs a second pair of shaders. The blit is one call.

The attachment could also be a **renderbuffer**, an image OpenGL can draw into and copy from, but never read in a shader. A texture works for the copy just as well, you already know textures, and chapter 13 can read it back with one call.

### Asking for the size every frame

[Plan §4.4](plan.md#44-platform-and-input) says the engine handles resizing itself, and the game never hears of it. SDL reports a resize as an event. There are two ways to keep up with it:

| Option | The catch |
|---|---|
| Handle `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` in `platform::handle_events`, and keep the size where the renderer can read it | Another copy of the size, which must not miss an event |
| **Ask SDL for the size in pixels every frame** | A function call per frame |

SDL already keeps the window's size up to date as it handles the events, and `SDL_GetWindowSizeInPixels` reads two numbers it already has. So the renderer asks every frame, right before the copy. Nothing can fall out of step, and it covers the case that's easy to forget: moving the window to a display with another scale changes its pixels without changing its points.

The renderer logs only when the scale changes. Dragging a window's corner changes its size on every frame, and a line per frame would bury the log.

### Whose 256×240?

Chapter 10 put the screen's size in the engine, in `renderer.h`. It also settled, at your suggestion, that the engine doesn't name the game's layers. Should the game pick the screen's size too?

| Option | The catch |
|---|---|
| **The engine's: `SCREEN_WIDTH` and `SCREEN_HEIGHT` stay in `renderer.h`** | A game that wanted 320×180 would have to change the engine |
| The game's: `GameApi` carries the size, like `state_size`, and the engine makes the screen from it | The screen exists before the game's first tick, and a reload could change its size. The engine would need code to remake the screen or refuse the change, for a choice this game makes once. |

momo keeps it in the engine, for the reason `MAX_LAYERS` is there. It's a number the engine's machinery is built on: the screen texture, the vertex shader's `screen_size`, the fit, and chapter 13's overlay all use it. The layers' names were different: they mean something to the game and nothing to the engine. What goes on the screen is the game's business. The screen itself is the engine's. [Plan §1](plan.md#1-what-momo-is) says a feature goes into the core only when the game needs it, and this book is one 256×240 game. If a second game ever wants another size, moving it is a field in `GameApi`.

## The code

### Fitting the screen: `engine/screen.h`

`engine/screen.h:17`

```cpp
struct ScreenFit {
    i32 scale;
    Rect rect;
};

namespace screen {

ScreenFit fit(i32 window_width, i32 window_height);

} // namespace screen
```

Without their comments. `scale` is how many window pixels each screen pixel covers, across and down. `rect` is where the scaled screen goes, in window pixels from the window's top left corner, the same way `Rect` counts on the screen. It's a module of its own for the batch's reason: it never calls OpenGL, so the tests can call it. `renderer.cpp` includes glad and `platform.h`, and a test that linked it would need SDL.

`engine/screen.cpp:6`

```cpp
ScreenFit fit(i32 window_width, i32 window_height)
{
    i32 scale = min(window_width / SCREEN_WIDTH, window_height / SCREEN_HEIGHT);
    scale = max(scale, 1);

    i32 width = SCREEN_WIDTH * scale;
    i32 height = SCREEN_HEIGHT * scale;

    return {
        .scale = scale,
        .rect = { .x = (window_width - width) / 2,
                  .y = (window_height - height) / 2,
                  .w = width,
                  .h = height },
    };
}
```

Design's formula, with chapter 03's `min` and `max`. Integer division rounds down, which keeps the scale whole. When the leftover is odd, `/ 2` rounds down too, so the left bar is the narrower one and the extra pixel goes right. In a window narrower than the screen, `x` is negative: the screen starts left of the window's edge, and its edges are cut off evenly.

### Every pixel: `engine/platform.cpp`

`engine/platform.cpp:43`

```cpp
    SDL_WindowFlags flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window = SDL_CreateWindow(title, width, height, flags);
```

In `platform::init`, where chapter 08 passed `SDL_WINDOW_OPENGL` alone. The comment above says what each new flag is for. `RESIZABLE` also means Hyprland tiles the window now instead of floating it, like your terminal.

The log line at the end of `init` (`engine/platform.cpp:86`) now gives the window's size as the window manager gave it, the same size in pixels, and the display scale, from `SDL_GetWindowSize`, `SDL_GetWindowSizeInPixels` and `SDL_GetWindowDisplayScale`. `%.3g` prints a scale of 2 as `2` and 1.6666666 as `1.67`.

`engine/platform.cpp:122`

```cpp
void window_size_in_pixels(i32* width, i32* height)
{
    SDL_GetWindowSizeInPixels(window, width, height);
}
```

The renderer's one new need from the platform. It takes pointers, since it fills in two numbers ([plan §3.1](plan.md#31-rules)): `platform::window_size_in_pixels(&width, &height)` shows at the call that both change. `handle_events` didn't change. Its comment now says that resize events are ignored on purpose, and why.

### The screen: `engine/renderer.cpp`

`engine/renderer.cpp:301`

```cpp
    glCreateTextures(GL_TEXTURE_2D, 1, &screen_texture);
    glTextureStorage2D(screen_texture, 1, GL_RGBA8, SCREEN_WIDTH, SCREEN_HEIGHT);
    glCreateFramebuffers(1, &screen_framebuffer);
    glNamedFramebufferTexture(screen_framebuffer, GL_COLOR_ATTACHMENT0, screen_texture, 0);
    ASSERT(glCheckNamedFramebufferStatus(screen_framebuffer, GL_FRAMEBUFFER) ==
           GL_FRAMEBUFFER_COMPLETE);
```

At the end of `renderer::init`, in DSA once more. `glTextureStorage2D` is the texture's version of chapter 10's `glNamedBufferStorage`: a size and format the texture keeps for good, one level (no mipmaps, which only matter for textures drawn smaller than they are), 8 bits each of red, green, blue and alpha, and no pixels yet. LearnOpenGL's `glTexImage2D` can change all of that on every call. `glNamedFramebufferTexture` attaches level 0 of the texture as the framebuffer's first color image, which is where the fragment shader's one output, `pixel_color`, goes.

A framebuffer is **complete** when OpenGL can draw into it. Every OpenGL 4.6 driver must be able to draw into `GL_RGBA8`, so an incomplete framebuffer here means momo built it wrongly, a bug, and the check is an `ASSERT` ([plan §3.3](plan.md#33-errors-and-strings)). `screen_texture` and `screen_framebuffer` join the renderer's file-scope variables (`engine/renderer.cpp:88`), and `shutdown` deletes both.

`begin_frame` (`engine/renderer.cpp:324`) now clears `screen_framebuffer` to the sky's dark teal instead of framebuffer 0. The window isn't cleared until the copy.

`engine/renderer.cpp:356`

```cpp
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, screen_framebuffer);
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    glUseProgram(program);
    glBindVertexArray(vertex_array);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, batch.count);
```

Chapter 10's draw, moved out of `end_frame` into `draw_batch`, with two more lines. A draw call goes into whichever framebuffer is bound for drawing, so the screen's is bound right there, with the program and the vertex array.

The **viewport** is where the vertex shader's clip space lands: -1 to 1 becomes the viewport's rectangle, in the framebuffer's pixels. Chapter 10 never set it. OpenGL starts it at the window's size when the context is made, and that was exactly what chapter 10 needed. It's one setting for the whole context, though, not one per framebuffer, and now it would start at the window's 1432×1720 pixels, far bigger than the texture. Exercise 4 shows what that draws. So `draw_batch` sets it to the screen's size, every frame, next to the bind.

`engine/renderer.cpp:365`

```cpp
static void present_screen()
{
    i32 window_width = 0;
    i32 window_height = 0;
    platform::window_size_in_pixels(&window_width, &window_height);
    ScreenFit fit = screen::fit(window_width, window_height);
    if (fit.scale != screen_scale) {
        LOG_INFO("the screen at %dx: %dx%d pixels, in a window of %dx%d", fit.scale, fit.rect.w,
                 fit.rect.h, window_width, window_height);
        screen_scale = fit.scale;
    }

    i32 bottom = window_height - (fit.rect.y + fit.rect.h);
    glClearNamedFramebufferfv(0, GL_COLOR, 0, BAR_COLOR);
    glBlitNamedFramebuffer(screen_framebuffer, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, fit.rect.x,
                           bottom, fit.rect.x + fit.rect.w, bottom + fit.rect.h,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
}
```

Without its comments. The window's size, the fit, a log line if the scale changed, then the copy. `screen_scale` starts at 0, a scale `fit` never returns, so the first frame always logs.

The blit takes its source framebuffer, the screen's, and its destination, 0, the window's. Then come two rectangles as left, bottom, right, top: all of the screen, and where it goes in the window. `GL_COLOR_BUFFER_BIT` copies the color image, the only one either has, and `GL_NEAREST` picks the nearest pixel, as Design said.

OpenGL counts a framebuffer's rows from the bottom, the way clip space's `y` points up (chapter 10). `fit.rect.y` counts from the top, so `bottom` is the distance from the screen's bottom edge to the window's. The screen's rows need no flipping. Chapter 10's vertex shader already put the screen's top at clip space's top, the texture's highest row, and the blit copies the highest row to the highest row.

Clearing the window to black first leaves black wherever the blit doesn't write: the bars.

`engine/renderer.cpp:392`

```cpp
void end_frame()
{
    ASSERT(in_frame);
    in_frame = false;
    if (batch.count > 0)
        draw_batch();
    present_screen();
}
```

Chapter 10's `end_frame` returned early when the game had drawn nothing, after `begin_frame` had cleared the window. Now only `present_screen` touches the window, so an empty frame, such as when no game is loaded after a failed reload, still has to be copied. Otherwise the window would show whatever its back buffer held, a picture from some earlier frame.

### The window's size: `engine/momo.cpp`

Unchanged but for a comment. The window still asks for `3 * SCREEN_WIDTH` by `3 * SCREEN_HEIGHT`, 768×720, and the comment above it (`engine/momo.cpp:51`) says in which units: points on Wayland, which is 6 times the screen in pixels at your panel's scale; pixels on X11; and whatever a tiling window manager decides. The game never sees any of it.

### The bat's eyes: `game/game.cpp`

`game/game.cpp:94`

```cpp
    renderer::draw_rect({ .x = x + 5, .y = BAT_Y + 5, .w = 1, .h = 1 }, EYE_COLOR, LAYER_ENTITIES);
    renderer::draw_rect({ .x = x + 10, .y = BAT_Y + 5, .w = 1, .h = 1 }, EYE_COLOR, LAYER_ENTITIES);
```

The game's only change: two yellow pixels, `renderer::rgb(0xf4d03f)`, drawn after the body so they land on it, one pixel in from each of its sides. A one-pixel detail shows every kind of scaling problem at once: a stretch blurs it, a fractional scale makes one eye bigger than the other. `GameState` didn't change, so `GAME_STATE_VERSION` stays 1, and the eyes appear in a running momo after `make`.

### The tests: `tests/screen_test.cpp`

`tests/screen_test.cpp:39`

```cpp
static void a_window_that_fits_neither_way_gets_bars_all_round()
{
    // A 1920x1080 monitor: 7.5 screens across, 4.5 down.
    ScreenFit fit = screen::fit(1920, 1080);
    CHECK_EQ(fit.scale, 4);
    CHECK_EQ(fit.rect.x, 448); // (1920 - 1024) / 2
    CHECK_EQ(fit.rect.y, 60);  // (1080 - 960) / 2
    CHECK_EQ(fit.rect.w, 1024);
    CHECK_EQ(fit.rect.h, 960);
}
```

One of eight. The others are a window of exactly three screens, a wide one and a tall one, one pixel short of scale 2, an odd leftover pixel, a window smaller than the screen, and a window with no pixels at all, as a minimized one can be.

### The Makefile

Unchanged. `engine/screen.cpp` and `tests/screen_test.cpp` join through the wildcards, as chapter 10's files did.

## Run it

### A window like any other

```
$ make
...
$ ./build/debug/momo
engine/momo.cpp:82: reserved 64 MB at 0x7b1b25a00000
engine/momo.cpp:206: loaded /home/mairuu/repos/momo/build/debug/game.so
engine/platform.cpp:86: SDL 3.4.18, video driver wayland, window 716x860, 1432x1720 pixels, display scale 2, 120 Hz
engine/renderer.cpp:228: OpenGL 4.6 (Core Profile) Mesa 26.2.4-arch1.1, AMD Radeon 880M Graphics (radeonsi, strix1, ACO, DRM 3.64, 7.2.9-arch1-1)
game/game.cpp:47: init
engine/renderer.cpp:372: the screen at 5x: 1280x1200 pixels, in a window of 1432x1720
```

momo opens tiled, next to whatever else is on the workspace (Firefox, while this was written), at 716×860 points. That's 1432×1720 pixels, and the screen fits in it 5 times. The scene is as before, with black above and below it, and thin bars at the sides. Look for the bat's yellow eyes.

A moment later, Hyprland settled the window at 710×856 points, 1420×1712 pixels. The scale stayed 5, so the log didn't say so.

### Resize it

Open a second window on the same workspace, such as a terminal, and close it again. While this chapter was being written, one opened next to momo for about a second and a half:

```
engine/renderer.cpp:372: the screen at 3x: 768x720 pixels, in a window of 1420x848
engine/renderer.cpp:372: the screen at 5x: 1280x1200 pixels, in a window of 1420x1712
^Cengine/platform.cpp:115: quit requested
game/game.cpp:100: shutdown, with the bat at x = 128
engine/momo.cpp:256: quit after 5.89 s: 708 frames, 352 ticks
...
engine/momo.cpp:88: engine    arena: at most 180268 bytes used of 8 MB
```

The new window took half of momo's tile. 848 pixels is 3.5 screens high, so the scale fell to 3, and came back to 5 when the window closed. The game never noticed: the bat kept flying, and the engine's arena is the same 180,268 bytes as in chapter 10, since the screen's 240 KB texture lives in the driver's memory.

To try sizes freely, press Super+V to float momo, then hold Super and drag with the right mouse button to resize it, as your Hyprland config binds them. Between whole numbers, the bars grow and the screen stays the same size. It jumps only when another whole screen fits both ways, and each jump is one log line. Make the window smaller than 512×480 pixels (256×240 points on your panel) and the scale is 1. Smaller than 256×240 pixels, and the screen's edges are cut off.

### Look closer

With the window tiled, `grim` took a picture of momo, the same way as the Problem's, and the script read it. The window had settled at 1420×1712 pixels by then, so the screen starts 70 pixels from its left edge and 256 from its top. Across the pillar's left edge, on the same row of the screen as before:

| Window pixel | 668 | 669 | 670 | 671 |
|---|---|---|---|---|
| Color | `0f3840` | `0f3840` | `8a8a8a` | `8a8a8a` |

The pillar starts at screen pixel 120, which is window pixel 70 + 120 × 5 = 670, and there's nothing in between. The left bar ends just as exactly: pixel 69 is black, 70 is sky. The bat's body is 40×45 pixels, 8×9 screen pixels at 5x, and each eye is a 5×5 square of `f4d03f`:

```
     373    374    375    376    377    378    379    380
960  8e44ad 8e44ad 8e44ad 8e44ad 8e44ad 8e44ad 8e44ad 8e44ad
961  8e44ad 8e44ad f4d03f f4d03f f4d03f f4d03f f4d03f 8e44ad
962  8e44ad 8e44ad f4d03f f4d03f f4d03f f4d03f f4d03f 8e44ad
963  8e44ad 8e44ad f4d03f f4d03f f4d03f f4d03f f4d03f 8e44ad
964  8e44ad 8e44ad f4d03f f4d03f f4d03f f4d03f f4d03f 8e44ad
965  8e44ad 8e44ad f4d03f f4d03f f4d03f f4d03f f4d03f 8e44ad
966  8e44ad 8e44ad 8e44ad 8e44ad 8e44ad 8e44ad 8e44ad 8e44ad
```

Now take `| SDL_WINDOW_HIGH_PIXEL_DENSITY` out of the flags in `platform::init`, `make`, and run it again:

```
engine/platform.cpp:86: SDL 3.4.18, video driver wayland, window 716x860, 716x860 pixels, display scale 1, 120 Hz
...
engine/renderer.cpp:372: the screen at 2x: 512x480 pixels, in a window of 716x860
```

With a quarter of the pixels, only 2 screens fit, and the compositor doubles that. The screen comes out smaller than before, and blurry. Here's an eye from that run, as the panel showed it:

```
     437    438    439    440    441    442
939  944da6 a15e98 a76792 a76792 a15e98 944da6
940  a15e98 c7936f dbad5b dbad5b c7936f a15e98
941  a76792 dbad5b f4d03f f4d03f dbad5b a76792
942  a76792 dbad5b f4d03f f4d03f dbad5b a76792
943  a15e98 c7936f dbad5b dbad5b c7936f a15e98
944  944da6 a15e98 a76792 a76792 a15e98 944da6
```

One screen pixel at 2x is 2×2 window pixels, then 4×4 panel pixels. The compositor's smoothing turned it into a 6×6 blur, where only the middle 2×2 is the eye's real yellow, and even the bat's purple around it isn't `8e44ad` any more. Put the flag back.

### X11

Run it under X11, as in chapter 07's exercise 2:

```
$ SDL_VIDEO_DRIVER=x11 ./build/debug/momo
...
engine/platform.cpp:86: SDL 3.4.18, video driver x11, window 768x720, 768x720 pixels, display scale 2, 120 Hz
...
engine/renderer.cpp:372: the screen at 3x: 768x720 pixels, in a window of 768x720
engine/renderer.cpp:372: the screen at 5x: 1280x1200 pixels, in a window of 1420x1712
```

Here's the answer to that exercise. X11 sizes are pixels, so the window opened at 768×720 pixels, half the size of 768×720 points. SDL knew the display's scale was 2, from `Xft.dpi`, but X11 leaves it to the program to act on it. Then Hyprland tiled the window, at the same 1420×1712 pixels as the Wayland one, and the scale went to 5. At the size it asked for, 768×720 pixels, an X11 momo runs at scale 3, as the first line says: smaller than on Wayland, but just as sharp. Exercise 3 makes it open at the right size.

### What the copy costs

The CPU's work didn't change: three 10-second runs of each release build, back to back, each used 3% of a CPU. The copy is the GPU's work, and the frame rate with vsync off shows it. Mesa turns vsync off under X11 when you set `vblank_mode=0`. Under Wayland, SDL waits for the compositor itself (chapter 09), so this works only on X11:

```
$ SDL_VIDEO_DRIVER=x11 vblank_mode=0 ./build/release/momo
```

| 8 seconds each, release | Window | Frames a second | Each frame took |
|---|---|---|---|
| Chapter 10 | 768×720, floating | 17,150 | 58 µs |
| This chapter: clear the window, copy the screen | 1420×1712, tiled | 14,350 | 70 µs |

About 11 µs more a frame, for a window with four times the pixels. At 120 Hz a frame has 8,300 µs.

### The tests

```
$ make test
...
screen_test
  a_window_three_times_the_screen_is_filled ... ok
  a_wide_window_gets_bars_left_and_right ... ok
  a_tall_window_gets_bars_above_and_below ... ok
  a_window_that_fits_neither_way_gets_bars_all_round ... ok
  one_pixel_short_of_twice_is_once ... ok
  an_odd_leftover_pixel_goes_to_the_right ... ok
  a_window_smaller_than_the_screen_cuts_its_edges_off ... ok
  a_window_of_no_size_is_scale_1 ... ok
  8 passed
...
```

## Recap

```
momo/
  engine/
    screen.h        new: ScreenFit
    screen.cpp      new: fit, the largest whole-number scale and where the screen goes
    platform.h      + window_size_in_pixels; init's size is in points or pixels
    platform.cpp    + a resizable window with every pixel (HIGH_PIXEL_DENSITY); the log
                      gives the size in pixels and the display scale
    renderer.h      the screen is a picture of its own; end_frame copies it into the window
    renderer.cpp    + the screen's texture and framebuffer; the viewport; the copy at a
                      whole-number scale over black; a log line when the scale changes
    momo.cpp        a comment on what the window's size means
  game/
    game.cpp        + the bat's eyes, one pixel each
  tests/
    screen_test.cpp new: eight tests of the fit
```

- A display's scale says how many pixels show each point. Wayland sizes windows in points, and X11 in pixels.
- momo asks for every pixel the window covers, and works in pixels from then on. Without that, the compositor stretched momo's picture and blurred every edge, even at scale 2.
- The game draws on a 256×240 texture of its own, through a framebuffer. Each frame, a blit copies it into the window at the largest whole-number scale that fits, centered, over black.
- `GL_NEAREST` at a whole-number scale makes every screen pixel an exact square of one color.
- The viewport is one setting for the whole context, so `draw_batch` sets it to the screen's size before drawing into the screen.
- The renderer asks for the window's size in pixels every frame, so resizing needs no event, and the game never knows the window's size.
- The screen's size stays the engine's, like `MAX_LAYERS`: the engine's machinery is built on it.

The plan records the screen, the copy and the scale in [§4.5](plan.md#45-rendering), and reading the size every frame in [§4.4](plan.md#44-platform-and-input). The outline now says this chapter also made the window resizable and asked for every pixel.

Chapter 12 gives the renderer its first real textures, with stb_image, and an 8×8 font written in code as the first one. Each sprite then picks a texture, which chapter 10 called another change between draws.

## Exercises

Undo each change before the next exercise: `git checkout -- engine game` puts the code back.

1. **Stretch it.** In `present_screen`, right after the fit, replace where the screen goes with the whole window: `fit.rect = { .x = 0, .y = 0, .w = window_width, .h = window_height };`. Resize the window. What happens to the floor's tiles, and to the eyes? Now change `GL_NEAREST` to `GL_LINEAR`. Which is worse for pixel art? Then take the line out again, keeping `GL_LINEAR`, so the scale is a whole number once more. Are the edges sharp now? What does that say about the compositor's blur in the Problem, at a scale of exactly 2?
2. **The other monitor.** Every run in this chapter was on the laptop's panel. Focus the external monitor, at scale 1.67, and run momo there. What display scale does the log give, and what size in pixels for the window's points? Is it 5/3 of the points exactly? Take a picture of the bat with `grim -g "$(slurp)" bat.png` and zoom in. Are both eyes the same size?
3. **X11 at the right size.** After `SDL_CreateWindow` in `platform::init`, set the window's size to `width * SDL_GetWindowDisplayScale(window) / SDL_GetWindowPixelDensity(window)` by the same for the height, with `SDL_SetWindowSize`. Float the window under X11 (Super+V). How big is it now, and what scale does the log give? What does the same line do on Wayland, where the pixel density is 2?
4. **No viewport.** Delete the `glViewport` line in `draw_batch`. Before you run it, predict: clip space now lands on a rectangle the size of the window when it opened, such as 1432×1720 pixels, starting at the texture's bottom left corner, but the texture has only 256×240 of them. What part of the scene ends up on the screen, and how big?
5. **Fullscreen.** Add `SDL_WINDOW_FULLSCREEN` to the window's flags. Your panel is 2880×1800 pixels. Work out the scale and the bars before you run it, then check the log. How much of the panel does the screen fill?
6. **Too small.** Float momo and make the window as small as Hyprland allows. Add `SDL_SetWindowMinimumSize(window, width / 3, height / 3);` after `SDL_CreateWindow`, and try again. In which units is that minimum, and what's the smallest scale it allows on your panel? Does Hyprland honor it for a tiled window too?
