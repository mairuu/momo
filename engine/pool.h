// pool.h: a fixed number of slots for things that come and go, and handles
// that know when the thing they refer to is gone. Chapter 16 explains them.
#pragma once

#include "base.h"

// Which item in a Pool: the slot it's in, and the slot's generation when it
// was added. Every add into a slot starts a new generation, so a handle kept
// after its item was removed finds nothing, even once another item has taken
// the slot. Generations start at 1, so a zeroed handle finds nothing either:
// it's the handle to no item (ZII).
struct Handle {
    i32 index;      // the slot
    i32 generation; // the slot's generation when the item was added
};

// Up to N items of type T, each in a slot of its own. An item stays in its
// slot until it's removed, and adding never moves anything, so a pointer to
// an item is good until the item is removed. A zeroed Pool is an empty one
// (ZII), so a Pool can sit in GameState with nothing to set up.
//
// The functions are templates in this header, like arena::push, so the game
// compiles its own copy, and none of them is MOMO_API.
template <typename T, i32 N>
struct Pool {
    static_assert(N > 0, "a pool needs at least one slot");
    T items[N];
    i32 generations[N]; // how many items each slot has held: 0 if none yet
    bool used[N];       // whether each slot holds an item now
    i32 count;          // how many slots hold an item
};

namespace pool {

// Copies item into the first free slot, and returns its handle. ASSERTs if
// every slot is taken: N is too small for the game.
template <typename T, i32 N>
Handle add(Pool<T, N>* pool, const T& item)
{
    ASSERT(pool->count < N);
    // count < N, so a free slot is there to be found.
    i32 i = 0;
    while (pool->used[i])
        i++;
    pool->items[i] = item;
    pool->used[i] = true;
    pool->generations[i]++; // an i32 lasts for 2 billion adds into one slot
    pool->count++;
    return { .index = i, .generation = pool->generations[i] };
}

// Whether the item handle refers to is still in the pool: false for a zeroed
// handle, and for an item that was removed, whatever has taken its slot since.
template <typename T, i32 N>
bool contains(const Pool<T, N>& pool, Handle handle)
{
    ASSERT(handle.index >= 0 && handle.index < N);
    return pool.used[handle.index] && pool.generations[handle.index] == handle.generation;
}

// The item handle refers to, or null if it's no longer in the pool.
template <typename T, i32 N>
T* get(Pool<T, N>* pool, Handle handle)
{
    return contains(*pool, handle) ? &pool->items[handle.index] : nullptr;
}

// The same, for reading only: without the &, get(state->entities, handle)
// returns a const pointer, which is all render's const GameState can give.
template <typename T, i32 N>
const T* get(const Pool<T, N>& pool, Handle handle)
{
    return contains(pool, handle) ? &pool.items[handle.index] : nullptr;
}

// Removes the item handle refers to, which must still be in the pool:
// removing an item twice is a bug. The slot is zeroed, so a pointer kept past
// this reads zeros, and the slot's next item starts the next generation.
template <typename T, i32 N>
void remove(Pool<T, N>* pool, Handle handle)
{
    ASSERT(contains(*pool, handle));
    pool->items[handle.index] = {};
    pool->used[handle.index] = false;
    pool->count--;
}

// The handle of the item in slot index, or a zeroed handle if the slot is
// empty. Going through the slots in order visits every item:
//
//     for (i32 i = 0; i < MAX_ENTITIES; i++) {
//         Handle handle = pool::handle_at(state->entities, i);
//         Entity* e = pool::get(&state->entities, handle);
//         if (!e)
//             continue; // an empty slot
//         ...
//     }
//
// An item can be added or removed along the way, since nothing moves. One
// added into a later slot is visited in the same loop, and one added into an
// earlier slot isn't.
template <typename T, i32 N>
Handle handle_at(const Pool<T, N>& pool, i32 index)
{
    ASSERT(index >= 0 && index < N);
    if (!pool.used[index])
        return {};
    return { .index = index, .generation = pool.generations[index] };
}

} // namespace pool
