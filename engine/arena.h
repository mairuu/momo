// arena.h: memory handed out by moving an offset forward, and given back all
// at once. Chapter 04 explains it.
#pragma once

#include "base.h"

// An arena hands out pieces of one fixed block of memory, in order, and
// frees them all together when it's reset. Nothing is freed one piece at a
// time.
//
// Every push returns memory that's all zeros (ZII). Running out is an
// ASSERT: the sizes are fixed, and hitting one means the game needs more
// memory than it was given.
struct Arena {
    u8* base; // the start of the block
    i64 size; // the block's size in bytes
    i64 used; // bytes handed out since the last reset, padding included
    i64 peak; // the most `used` has ever been
};

namespace arena {

// Sets arena up to hand out the size bytes starting at memory. The arena
// doesn't own that memory: whoever passed it in keeps it alive.
MOMO_API void init(Arena* arena, void* memory, i64 size);

// Returns size bytes, all zero, at an address that's a multiple of align.
// align must be a power of two.
MOMO_API void* push_bytes(Arena* arena, i64 size, i64 align);

// Gives back everything pushed since init or the last reset. Pointers into
// the arena are still valid addresses, but the next push reuses them.
MOMO_API void reset(Arena* arena);

// One zeroed T: Entity* e = arena::push<Entity>(&arena);
template <typename T>
T* push(Arena* arena)
{
    return (T*)push_bytes(arena, sizeof(T), alignof(T));
}

// count zeroed Ts in a row: i32* xs = arena::push_array<i32>(&arena, 100);
template <typename T>
T* push_array(Arena* arena, i32 count)
{
    ASSERT(count >= 0);
    return (T*)push_bytes(arena, (i64)sizeof(T) * count, alignof(T));
}

} // namespace arena
