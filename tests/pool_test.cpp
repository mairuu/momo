// pool_test.cpp: tests for engine/pool.h.
#include "test.h"

#include "pool.h"

// The tests keep Items in a pool of four slots, small enough to fill.
struct Item {
    i32 id;
};

using Pool4 = Pool<Item, 4>;

static void a_zeroed_pool_is_empty()
{
    Pool4 pool = {};
    CHECK_EQ(pool.count, 0);
    for (i32 i = 0; i < 4; i++) {
        Handle handle = pool::handle_at(pool, i);
        CHECK_EQ(handle.index, 0);
        CHECK_EQ(handle.generation, 0);
    }
    CHECK(!pool::contains(pool, Handle{}));
}

static void add_returns_a_handle_that_gets_the_item()
{
    Pool4 pool = {};
    Handle a = pool::add(&pool, { .id = 10 });
    Handle b = pool::add(&pool, { .id = 20 });
    CHECK_EQ(pool.count, 2);
    CHECK(pool::get(&pool, a) != nullptr);
    CHECK_EQ(pool::get(&pool, a)->id, 10);
    CHECK_EQ(pool::get(&pool, b)->id, 20);

    // The const get finds the same items, for reading.
    const Pool4& readonly = pool;
    CHECK(pool::get(readonly, b) == pool::get(&pool, b));
}

static void the_first_items_are_in_slots_0_1_2_generation_1()
{
    Pool4 pool = {};
    for (i32 i = 0; i < 3; i++) {
        Handle handle = pool::add(&pool, { .id = i });
        CHECK_EQ(handle.index, i);
        CHECK_EQ(handle.generation, 1);
    }
}

static void a_zeroed_handle_finds_nothing()
{
    // Slot 0 holds an item, generation 1. A zeroed handle is slot 0,
    // generation 0, which no item ever has.
    Pool4 pool = {};
    pool::add(&pool, { .id = 10 });
    Handle none = {};
    CHECK(!pool::contains(pool, none));
    CHECK(pool::get(&pool, none) == nullptr);
}

static void a_removed_item_is_gone()
{
    Pool4 pool = {};
    Handle a = pool::add(&pool, { .id = 10 });
    Handle b = pool::add(&pool, { .id = 20 });
    pool::remove(&pool, a);
    CHECK_EQ(pool.count, 1);
    CHECK(!pool::contains(pool, a));
    CHECK(pool::get(&pool, a) == nullptr);
    CHECK_EQ(pool::get(&pool, b)->id, 20);
}

static void a_handle_finds_nothing_once_another_item_takes_its_slot()
{
    // The game's two nests (game.cpp): one nest's bat is removed, and the
    // other nest's next bat goes into its slot. The first nest's handle must
    // not find the second nest's bat.
    Pool4 pool = {};
    Handle first_bat = pool::add(&pool, { .id = 1 });
    pool::remove(&pool, first_bat);
    Handle second_bat = pool::add(&pool, { .id = 2 });

    CHECK_EQ(second_bat.index, first_bat.index); // the same slot
    CHECK_EQ(second_bat.generation, 2);          // the slot's second item
    CHECK(pool::get(&pool, first_bat) == nullptr);
    CHECK_EQ(pool::get(&pool, second_bat)->id, 2);
}

static void add_takes_the_first_free_slot()
{
    Pool4 pool = {};
    Handle handles[4] = {};
    for (i32 i = 0; i < 4; i++)
        handles[i] = pool::add(&pool, { .id = i });
    CHECK_EQ(pool.count, 4); // full: one more add would ASSERT

    pool::remove(&pool, handles[2]);
    pool::remove(&pool, handles[1]);
    CHECK_EQ(pool::add(&pool, { .id = 5 }).index, 1);
    CHECK_EQ(pool::add(&pool, { .id = 6 }).index, 2);
    CHECK_EQ(pool.count, 4);
}

static void remove_zeroes_the_slot()
{
    // A pointer kept past remove still points at the slot, which reads
    // zeros: a zeroed Entity's kind is ENTITY_NONE in game.cpp.
    Pool4 pool = {};
    Handle a = pool::add(&pool, { .id = 10 });
    Item* kept = pool::get(&pool, a);
    pool::remove(&pool, a);
    CHECK_EQ(kept->id, 0);
}

static void pointers_stay_put_while_the_pool_fills()
{
    // Adding never moves an item, unlike std::vector's push_back, which
    // may move every item into a bigger allocation.
    Pool4 pool = {};
    Handle a = pool::add(&pool, { .id = 10 });
    Item* item = pool::get(&pool, a);
    for (i32 i = 0; i < 3; i++)
        pool::add(&pool, { .id = 20 + i });
    CHECK(pool::get(&pool, a) == item);
    CHECK_EQ(item->id, 10);
}

static void handle_at_visits_every_item_in_slot_order()
{
    Pool4 pool = {};
    Handle handles[4] = {};
    for (i32 i = 0; i < 4; i++)
        handles[i] = pool::add(&pool, { .id = 10 * i });
    pool::remove(&pool, handles[1]);

    i32 ids[4] = {};
    i32 visited = 0;
    for (i32 i = 0; i < 4; i++) {
        Item* item = pool::get(&pool, pool::handle_at(pool, i));
        if (!item)
            continue; // an empty slot
        ids[visited] = item->id;
        visited++;
    }
    CHECK_EQ(visited, 3);
    CHECK_EQ(ids[0], 0);
    CHECK_EQ(ids[1], 20);
    CHECK_EQ(ids[2], 30);
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(a_zeroed_pool_is_empty);
    RUN(add_returns_a_handle_that_gets_the_item);
    RUN(the_first_items_are_in_slots_0_1_2_generation_1);
    RUN(a_zeroed_handle_finds_nothing);
    RUN(a_removed_item_is_gone);
    RUN(a_handle_finds_nothing_once_another_item_takes_its_slot);
    RUN(add_takes_the_first_free_slot);
    RUN(remove_zeroes_the_slot);
    RUN(pointers_stay_put_while_the_pool_fills);
    RUN(handle_at_visits_every_item_in_slot_order);
    return test::report();
}
