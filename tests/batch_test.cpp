// batch_test.cpp: tests for engine/batch.h.
#include "test.h"

#include "batch.h"

// The batch never calls OpenGL, so the tests sort rectangles and read the
// result, with no window and no GPU. Each rectangle's x tells the tests
// which one it is. The arena's memory is on the stack, as in arena_test.

constexpr i32 CAPACITY = 16;

// The batch sees only layer numbers. These are the game's names for them
// (game/game.cpp), so the tests read like the game's code.
enum Layer : u8 {
    LAYER_BACKGROUND,
    LAYER_ENTITIES,
    LAYER_FOREGROUND,
    LAYER_HUD,
};

static Rect rect_at(i32 x)
{
    return { .x = x, .y = 0, .w = 16, .h = 16 };
}

static void an_empty_batch_sorts_to_nothing()
{
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    RectInstance out[CAPACITY] = {};
    batch::sort(b, out);
    CHECK_EQ(b.count, 0);
    CHECK_EQ(out[0].rect.w, 0); // untouched
}

static void layers_come_out_back_to_front()
{
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    // Added front to back, the opposite of drawing order.
    batch::add(&b, rect_at(3), {}, LAYER_HUD);
    batch::add(&b, rect_at(2), {}, LAYER_FOREGROUND);
    batch::add(&b, rect_at(1), {}, LAYER_ENTITIES);
    batch::add(&b, rect_at(0), {}, LAYER_BACKGROUND);

    RectInstance out[CAPACITY] = {};
    batch::sort(b, out);
    for (i32 i = 0; i < 4; i++)
        CHECK_EQ(out[i].rect.x, i);
}

static void every_layer_up_to_max_layers_is_drawn_in_order()
{
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    // All of them, the game's four and the four it doesn't use, last first.
    for (i32 layer = MAX_LAYERS - 1; layer >= 0; layer--)
        batch::add(&b, rect_at(layer), {}, layer);

    RectInstance out[CAPACITY] = {};
    batch::sort(b, out);
    for (i32 i = 0; i < MAX_LAYERS; i++)
        CHECK_EQ(out[i].rect.x, i);
}

static void empty_layers_in_between_are_skipped()
{
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    batch::add(&b, rect_at(1), {}, MAX_LAYERS - 1);
    batch::add(&b, rect_at(0), {}, 0);

    RectInstance out[CAPACITY] = {};
    batch::sort(b, out);
    CHECK_EQ(out[0].rect.x, 0);
    CHECK_EQ(out[1].rect.x, 1);
}

static void a_layer_keeps_the_order_its_rects_were_added_in()
{
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    // Two layers, taking turns.
    batch::add(&b, rect_at(10), {}, LAYER_ENTITIES);
    batch::add(&b, rect_at(0), {}, LAYER_BACKGROUND);
    batch::add(&b, rect_at(11), {}, LAYER_ENTITIES);
    batch::add(&b, rect_at(1), {}, LAYER_BACKGROUND);
    batch::add(&b, rect_at(12), {}, LAYER_ENTITIES);

    RectInstance out[CAPACITY] = {};
    batch::sort(b, out);
    CHECK_EQ(out[0].rect.x, 0);
    CHECK_EQ(out[1].rect.x, 1);
    CHECK_EQ(out[2].rect.x, 10);
    CHECK_EQ(out[3].rect.x, 11);
    CHECK_EQ(out[4].rect.x, 12);
}

static void the_frame_from_the_chapter()
{
    // Chapter 10's walk through the counting sort: a pillar, a floor tile,
    // the bat, another floor tile, a health bar, the bat's wing.
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    batch::add(&b, rect_at(4), {}, LAYER_FOREGROUND); // pillar
    batch::add(&b, rect_at(0), {}, LAYER_BACKGROUND); // floor
    batch::add(&b, rect_at(2), {}, LAYER_ENTITIES);   // bat
    batch::add(&b, rect_at(1), {}, LAYER_BACKGROUND); // floor
    batch::add(&b, rect_at(5), {}, LAYER_HUD);        // health bar
    batch::add(&b, rect_at(3), {}, LAYER_ENTITIES);   // wing

    RectInstance out[CAPACITY] = {};
    batch::sort(b, out);
    for (i32 i = 0; i < 6; i++)
        CHECK_EQ(out[i].rect.x, i);
}

static void sort_copies_the_color_with_the_rect()
{
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    batch::add(&b, rect_at(1), renderer::rgb(0x112233), LAYER_HUD);
    batch::add(&b, rect_at(0), renderer::rgb(0x445566), LAYER_BACKGROUND);

    RectInstance out[CAPACITY] = {};
    batch::sort(b, out);
    CHECK_EQ(out[0].color.r, 0x44);
    CHECK_EQ(out[0].color.b, 0x66);
    CHECK_EQ(out[1].color.g, 0x22);
    CHECK_EQ(out[1].color.a, 255);
}

static void clear_empties_the_batch()
{
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    batch::add(&b, rect_at(7), {}, LAYER_ENTITIES);
    batch::clear(&b);
    CHECK_EQ(b.count, 0);

    batch::add(&b, rect_at(8), {}, LAYER_ENTITIES);
    RectInstance out[CAPACITY] = {};
    batch::sort(b, out);
    CHECK_EQ(b.count, 1);
    CHECK_EQ(out[0].rect.x, 8);
}

static void a_full_batch_holds_capacity_rects()
{
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    // One more add would be an ASSERT, which stops the program, so this
    // test fills the batch and stops there. Two rectangles go on each of
    // the 8 layers.
    for (i32 i = 0; i < CAPACITY; i++)
        batch::add(&b, rect_at(CAPACITY - 1 - i), {}, i % MAX_LAYERS);
    CHECK_EQ(b.count, CAPACITY);

    // Layer by layer: the 0th and 8th rectangles added went on layer 0, and
    // their xs were 15 and 7.
    RectInstance out[CAPACITY] = {};
    batch::sort(b, out);
    CHECK_EQ(out[0].rect.x, 15);
    CHECK_EQ(out[1].rect.x, 7);
    CHECK_EQ(out[2].rect.x, 14);
    CHECK_EQ(out[CAPACITY - 1].rect.x, 0);
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(an_empty_batch_sorts_to_nothing);
    RUN(layers_come_out_back_to_front);
    RUN(every_layer_up_to_max_layers_is_drawn_in_order);
    RUN(empty_layers_in_between_are_skipped);
    RUN(a_layer_keeps_the_order_its_rects_were_added_in);
    RUN(the_frame_from_the_chapter);
    RUN(sort_copies_the_color_with_the_rect);
    RUN(clear_empties_the_batch);
    RUN(a_full_batch_holds_capacity_rects);
    return test::report();
}
