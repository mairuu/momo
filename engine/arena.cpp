// arena.cpp: the arena allocator. Chapter 04 explains it.
#include "arena.h"

#include <string.h>

namespace arena {

void init(Arena* arena, void* memory, i64 size)
{
    ASSERT(memory);
    ASSERT(size > 0);
    *arena = { .base = (u8*)memory, .size = size };
}

void* push_bytes(Arena* arena, i64 size, i64 align)
{
    ASSERT(arena->base); // arena::init hasn't been called
    ASSERT(size >= 0);
    ASSERT(align > 0 && (align & (align - 1)) == 0); // a power of two

    // Skip forward to the next address that's a multiple of align. Because
    // align is a power of two, an address's remainder after dividing by
    // align is just its lowest bits, and & (align - 1) keeps exactly those.
    u64 address = (u64)(arena->base + arena->used);
    i64 misalignment = (i64)(address & (u64)(align - 1));
    i64 padding = misalignment == 0 ? 0 : align - misalignment;
    i64 start = arena->used + padding;

    ASSERT(start + size <= arena->size); // the arena is full

    arena->used = start + size;
    arena->peak = max(arena->peak, arena->used);

    u8* result = arena->base + start;
    memset(result, 0, size);
    return result;
}

void reset(Arena* arena)
{
    arena->used = 0;
}

} // namespace arena
