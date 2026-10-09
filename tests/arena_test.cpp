// arena_test.cpp: tests for engine/arena.h.
#include "test.h"

#include "arena.h"

#include <string.h>

// Each test gives its arena a small block of its own, on the stack. alignas
// puts the block at an address that's a multiple of 16, so the tests know
// how the alignment works out.

static void push_hands_out_memory_in_order()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    void* first = arena::push_bytes(&a, 10, 1);
    void* second = arena::push_bytes(&a, 6, 1);
    CHECK(first == memory);
    CHECK(second == memory + 10);
    CHECK_EQ(a.used, 16);
}

static void push_returns_zeroed_memory()
{
    alignas(16) u8 memory[64];
    memset(memory, 0xAB, sizeof(memory)); // not zero, like a reused block

    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    u8* bytes = (u8*)arena::push_bytes(&a, 64, 1);
    for (i32 i = 0; i < 64; i++)
        CHECK_EQ(bytes[i], 0);
}

static void reset_reuses_the_memory_and_push_zeroes_it_again()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    i32* first = arena::push_array<i32>(&a, 4);
    for (i32 i = 0; i < 4; i++)
        first[i] = 7;

    arena::reset(&a);
    CHECK_EQ(a.used, 0);

    i32* second = arena::push_array<i32>(&a, 4);
    CHECK(second == first);
    for (i32 i = 0; i < 4; i++)
        CHECK_EQ(second[i], 0);
}

static void push_aligns_to_the_type()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    arena::push<u8>(&a);           // 1 byte at offset 0
    u64* x = arena::push<u64>(&a); // 7 bytes of padding, then 8 bytes
    CHECK((u8*)x == memory + 8);
    CHECK_EQ(a.used, 16);
}

static void push_aligns_the_address_not_the_offset()
{
    // The arena's block starts 1 byte past a multiple of 16, so offset 0 is
    // not a multiple of 4, but offset 3 is.
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory + 1, sizeof(memory) - 1);

    u32* x = arena::push<u32>(&a);
    CHECK_EQ((u64)x % 4, 0);
    CHECK((u8*)x == memory + 4);
    CHECK_EQ(a.used, 3 + 4);
}

static void push_array_takes_count_times_the_size()
{
    struct Pair {
        i32 a;
        i32 b;
    };
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    arena::push_array<Pair>(&a, 3);
    CHECK_EQ(a.used, 3 * sizeof(Pair));
    arena::push_array<Pair>(&a, 0);
    CHECK_EQ(a.used, 3 * sizeof(Pair));
}

static void push_can_fill_the_arena_exactly()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    arena::push_bytes(&a, 60, 1);
    arena::push_bytes(&a, 4, 4);
    CHECK_EQ(a.used, a.size);
}

static void peak_remembers_the_most_ever_used()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    arena::push_bytes(&a, 40, 1);
    arena::reset(&a);
    arena::push_bytes(&a, 10, 1);
    CHECK_EQ(a.used, 10);
    CHECK_EQ(a.peak, 40);
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(push_hands_out_memory_in_order);
    RUN(push_returns_zeroed_memory);
    RUN(reset_reuses_the_memory_and_push_zeroes_it_again);
    RUN(push_aligns_to_the_type);
    RUN(push_aligns_the_address_not_the_offset);
    RUN(push_array_takes_count_times_the_size);
    RUN(push_can_fill_the_arena_exactly);
    RUN(peak_remembers_the_most_ever_used);
    return test::report();
}
