// batch.h: the rectangles a frame draws, and the order they're drawn in.
// Chapter 10 explains it.
#pragma once

#include "base.h"
#include "arena.h"
#include "renderer.h"

// One rectangle the way the GPU reads it: one instance of the four corners
// the vertex shader draws.
struct RectInstance {
    Rect rect;
    Color color;
};

// Every rectangle drawn so far this frame, in the order the game drew them.
// A batch never calls OpenGL: the renderer sorts it and hands the result to
// the GPU, and the tests read the result themselves.
struct Batch {
    RectInstance* rects; // room for capacity of them, from an arena
    i32* layers;         // each rectangle's layer, 0 to MAX_LAYERS - 1
    i32 count;
    i32 capacity;
};

namespace batch {

// Pushes room for capacity rectangles from arena.
void init(Batch* batch, Arena* arena, i32 capacity);

// Empties the batch for the next frame.
void clear(Batch* batch);

// Adds one rectangle. A full batch is an ASSERT.
void add(Batch* batch, Rect rect, Color color, i32 layer);

// Writes the batch's rectangles to out in drawing order: by layer, back to
// front, and inside a layer in the order they were added. out has room for
// batch.count rectangles.
void sort(const Batch& batch, RectInstance* out);

} // namespace batch
