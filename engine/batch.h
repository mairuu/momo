// batch.h: the rectangles a frame draws, the order they're drawn in, and the
// draw calls that draw them. Chapter 10 explains it, chapter 12 adds
// textures, and chapter 19 outlines.
#pragma once

#include "base.h"
#include "arena.h"
#include "renderer.h"

// One rectangle the way the GPU reads it: one instance of the four corners
// the vertex shader draws. source is the part of its texture it shows, in
// the texture's pixels. A rectangle of one color shows all of the white
// pixel, { 0, 0, 1, 1 }, in its color.
struct RectInstance {
    Rect rect;
    Rect source;
    Color color;
};

// Every rectangle drawn so far this frame, in the order the game drew them.
// A batch never calls OpenGL: the renderer sorts it and hands the result to
// the GPU, and the tests read the result themselves.
struct Batch {
    RectInstance* rects; // room for capacity of them, from an arena
    i32* layers;         // each rectangle's layer, 0 to MAX_LAYERS - 1
    Texture* textures;   // each rectangle's texture
    i32 count;
    i32 capacity;
};

// Rectangles that come one after another in drawing order and share a
// texture: what one draw call draws.
struct DrawCall {
    Texture texture;
    i32 first; // the first one's place in drawing order
    i32 count;
};

namespace batch {

// Pushes room for capacity rectangles from arena.
void init(Batch* batch, Arena* arena, i32 capacity);

// Empties the batch for the next frame.
void clear(Batch* batch);

// Adds one rectangle. A full batch is an ASSERT.
void add(Batch* batch, RectInstance rect, Texture texture, i32 layer);

// Writes the batch's rectangles to out in drawing order: by layer, back to
// front, and inside a layer in the order they were added. Each one's texture
// goes to out_textures, in the same order. Both have room for batch.count.
void sort(const Batch& batch, RectInstance* out, Texture* out_textures);

// Splits count rectangles in drawing order, whose textures are textures, into
// draw calls: a new one wherever the texture changes. Writes them to calls,
// which has room for count, and returns how many there are.
i32 draw_calls(const Texture* textures, i32 count, DrawCall* calls);

// Writes the outline of rect to edges, which has room for 4: the rectangles
// along its four edges, 1 pixel thick and inside it. The top and bottom ones
// are as wide as rect, and the left and right ones fit between them, so no
// pixel is in two of them, unless rect is 1 pixel wide or high. Returns how
// many there are: 4, or 0 if rect has no pixels, 0 wide or high.
i32 outline(Rect rect, Rect* edges);

} // namespace batch
