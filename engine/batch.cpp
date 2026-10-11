// batch.cpp: the rectangles a frame draws. Chapter 10 explains it, and
// chapter 12 adds textures.
#include "batch.h"

namespace batch {

void init(Batch* batch, Arena* arena, i32 capacity)
{
    *batch = {
        .rects = arena::push_array<RectInstance>(arena, capacity),
        .layers = arena::push_array<i32>(arena, capacity),
        .textures = arena::push_array<Texture>(arena, capacity),
        .capacity = capacity,
    };
}

void clear(Batch* batch)
{
    batch->count = 0;
}

void add(Batch* batch, RectInstance rect, Texture texture, i32 layer)
{
    // The arrays live in an arena, where AddressSanitizer can't see past
    // their ends, so the index is checked here (chapter 04).
    ASSERT(batch->count < batch->capacity);
    ASSERT(layer >= 0 && layer < MAX_LAYERS);
    batch->rects[batch->count] = rect;
    batch->layers[batch->count] = layer;
    batch->textures[batch->count] = texture;
    batch->count++;
}

void sort(const Batch& batch, RectInstance* out, Texture* out_textures)
{
    // A counting sort. There are only MAX_LAYERS layers, so instead of
    // comparing rectangles, count how many each layer has. Then each layer's
    // rectangles start in out right after all of the layers before it.
    i32 next[MAX_LAYERS] = {};
    for (i32 i = 0; i < batch.count; i++)
        next[batch.layers[i]]++;

    i32 start = 0;
    for (i32 layer = 0; layer < MAX_LAYERS; layer++) {
        i32 count = next[layer];
        next[layer] = start;
        start += count;
    }

    // next[layer] is now where that layer's next rectangle goes. Going
    // through the rectangles in the order they were added, and moving each
    // layer's place along as it fills, keeps that order inside each layer.
    for (i32 i = 0; i < batch.count; i++) {
        i32 layer = batch.layers[i];
        out[next[layer]] = batch.rects[i];
        out_textures[next[layer]] = batch.textures[i];
        next[layer]++;
    }
}

i32 draw_calls(const Texture* textures, i32 count, DrawCall* calls)
{
    // Every rectangle joins the last draw call, unless its texture is a
    // different one: then it starts a new draw call.
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

} // namespace batch
