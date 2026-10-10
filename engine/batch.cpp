// batch.cpp: the rectangles a frame draws. Chapter 10 explains it.
#include "batch.h"

namespace batch {

void init(Batch* batch, Arena* arena, i32 capacity)
{
    *batch = {
        .rects = arena::push_array<RectInstance>(arena, capacity),
        .layers = arena::push_array<i32>(arena, capacity),
        .capacity = capacity,
    };
}

void clear(Batch* batch)
{
    batch->count = 0;
}

void add(Batch* batch, Rect rect, Color color, i32 layer)
{
    // The arrays live in an arena, where AddressSanitizer can't see past
    // their ends, so the index is checked here (chapter 04).
    ASSERT(batch->count < batch->capacity);
    ASSERT(layer >= 0 && layer < MAX_LAYERS);
    batch->rects[batch->count] = { .rect = rect, .color = color };
    batch->layers[batch->count] = layer;
    batch->count++;
}

void sort(const Batch& batch, RectInstance* out)
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
        next[layer]++;
    }
}

} // namespace batch
