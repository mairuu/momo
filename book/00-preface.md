# 00 · Preface

> **Goal:** know what we're building and how this book works, and finish with a machine that can compile our dialect of C++20 against SDL3.

## What we're building

Two things, one on top of the other.

**momo** is a small game engine. It's a core library that owns the window, the clock, the renderer, input, audio and memory, and knows nothing about any particular game.

**The game** is a 2D action platformer loosely inspired by MegaMan 2, set in an isekai/fantasy world. You run, jump, shoot and climb ladders through a stage, room by room, and beat the boss at the end. In Part II you take the boss's power and use it on the next boss.

The game is there to keep the engine honest: nothing goes into momo unless the game needs it. Engine projects without a game tend to grow features nobody uses and never finish. This one has a finish line. Part I ends when one stage and its boss are playable from start to finish.

Why NES style? Constraints make finished games. A 256×240 screen, 16-pixel tiles and a fixed 60 updates per second keep every system small enough to understand completely. They also let us borrow MegaMan's feel directly, because its movement was defined in exactly those units.

## Who this book is for

You know C and C++, you've worked through LearnOpenGL's "Getting Started", and game loops, entities and how an engine is put together are new to you. You learn mostly by reading, so this book is written to be read. Every chapter still ends with code you build and run. When you want to write a piece yourself, say so, and that chapter becomes a spec for you to implement.

## The shape of momo

```
 host (executable)          libmomo.so (the engine)           game.so (the game)
 ┌────────────────┐  links  ┌──────────────────────────┐ links ┌──────────────────┐
 │ main() {       │────────▶│ momo::run()              │◀──────│ init / update /  │
 │   momo::run()  │         │   loop, platform, render,│ dlopen│ render / shutdown│
 │ }              │         │   input, audio, arenas   │──────▶│ (reloaded on     │
 └────────────────┘         └──────────────────────────┘       │  every rebuild)  │
                                                               └──────────────────┘
```

Chapter 02 explains each box properly. For now, one idea shapes everything else: **the game is a separate shared library that the engine reloads while it runs.** Edit the jump code, rebuild, and the running game picks up the change mid-jump. We set that up in chapter 06, and it's the main reason the C++ in this book looks the way it does (chapter 01).

## How to read this book

### Chapters and tags

Every chapter is one commit, tagged `chNN`:

```sh
git tag                 # list the chapters
git checkout ch05       # the code exactly as chapter 5 left it
git diff ch04 ch05      # everything chapter 5 changed
git checkout main       # back to the latest
```

Code in a chapter is shown as excerpts of the part that matters, with `file:line` so you can open the whole file. The diff is the complete record.

### What's in a chapter

- **Goal**: what you'll see at the end
- **Problem**: why the engine needs this now
- **Design**: the options, which one we pick, and why
- **Code**: the key excerpts
- **Run it**: exact commands, and what to look for
- **Recap**: what changed in the engine's structure
- **Exercises**: optional

Chapters are short, one idea each, about 15–25 minutes. Every one ends with something you can run.

### Mistakes stay in

The book is append-only. When a later chapter finds that an earlier design was wrong, it doesn't quietly rewrite history. It shows what broke and fixes it in the open, because seeing a design fail is often the most useful thing an engine book can show you. Only typos and plain bugs get corrected in place, and the chapter you're currently reading can still change until the next one starts.

### Your turn

When you want to implement something yourself, say so. That chapter arrives as a spec, tagged `chNN-spec`, with the interface, the expected behavior, stub functions and failing tests. You make the tests pass, I review your code, and it becomes the book's code.

### The plan

Every design decision and the reason for it lives in [plan.md](plan.md). When a chapter says "we decided X", that's where X was decided. When a decision changes, the plan says which chapter changed it and why.

## The road ahead

**Part I**, from nothing to one stage and its boss:

- **A. Foundations** (01–06): the C++ dialect, three binaries, a test runner, memory arenas, strings, and hot reload, all in the terminal
- **B. Window and clock** (07–09): an SDL3 window, OpenGL 4.6, and a fixed 60 Hz loop
- **C. Drawing** (10–13): a sprite batcher, the 256×240 virtual screen, textures, debug text
- **D. Moving things** (14–22): input, fixed-point math, entities, tilemaps, collision, running, jumping, ladders, and record-and-loop for tuning them while the game runs
- **E. A stage** (23–28): rooms, camera, spawning, shooting, enemies, health, game flow
- **F. Real content** (29–35): sprites and animation, the asset baker, palette sprites, LDtk levels, sound and music
- **G. The boss** (36–37): the boss gate and the fight

**Part II** turns one stage into a small game: stage select, a second stage and boss, and the power-swap loop that makes MegaMan MegaMan.

The full outline is in [plan.md §8](plan.md#8-outline).

## What we won't build

Networking, saving, 3D, an editor, web or mobile builds, and a scripting language, except for one optional chapter that compares Lua with C++. The full list is in [plan.md §9](plan.md#9-out-of-scope). Leaving things out is how a project like this gets finished.

## Setup

You need GCC with C++20 support, GNU Make, gdb, git, pkg-config and SDL3. On Arch:

```sh
sudo pacman -S --needed gcc make gdb git pkgconf sdl3
```

Third-party code like glad and the stb libraries is copied into the repo when a chapter first needs it, so there's nothing else to install. LDtk, the level editor, comes in chapter 32.

## Run it

Check that the toolchain can build the dialect we'll use and link SDL3. From the repo root:

```sh
mkdir -p build/ch00
cat > build/ch00/check.cpp <<'EOF'
#include <SDL3/SDL.h>
#include <stdio.h>

int main()
{
    int v = SDL_GetVersion();
    printf("C++ %ld, SDL %d.%d.%d: ready\n", __cplusplus,
           SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
    return 0;
}
EOF
g++ -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti build/ch00/check.cpp \
    $(pkg-config --cflags --libs sdl3) -o build/ch00/check
./build/ch00/check
```

You should see something like:

```
C++ 202002, SDL 3.4.18: ready
```

`202002` means C++20. Your SDL version may differ, and anything from 3.2 up works. There should be no warnings. `build/` is ignored by git, so this leaves the repo clean.

Look at the compile flags: `-fno-exceptions -fno-rtti`. That's the first sign of the dialect, and chapter 01 explains why it's there.

## Recap

There's no engine yet. The repo holds only the book:

```
momo/
  .gitignore     build/ and tool folders
  book/
    00-preface.md
    plan.md
```

The engine's module map starts in chapter 02.

## Exercises

1. Skim [plan.md §3](plan.md#3-language-orthodox-c20-built-with-gcc), the dialect rules. Note any rule that surprises you. Chapter 01 should explain it, and if it doesn't, ask.
2. Run `git log --oneline` and `git tag`. Each shows exactly one entry. Every chapter adds one more to each.
