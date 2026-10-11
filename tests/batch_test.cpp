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

static RectInstance rect_at(i32 x)
{
    return { .rect = { .x = x, .y = 0, .w = 16, .h = 16 } };
}

// Textures are numbers to the batch too: it never looks inside one.
constexpr Texture WHITE = { .index = 0 };
constexpr Texture FONT = { .index = 1 };
constexpr Texture BAT = { .index = 2 };

static void an_empty_batch_sorts_to_nothing()
{
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    RectInstance out[CAPACITY] = {};
    Texture out_textures[CAPACITY] = {};
    batch::sort(b, out, out_textures);
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
    Texture out_textures[CAPACITY] = {};
    batch::sort(b, out, out_textures);
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
    Texture out_textures[CAPACITY] = {};
    batch::sort(b, out, out_textures);
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
    Texture out_textures[CAPACITY] = {};
    batch::sort(b, out, out_textures);
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
    Texture out_textures[CAPACITY] = {};
    batch::sort(b, out, out_textures);
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
    Texture out_textures[CAPACITY] = {};
    batch::sort(b, out, out_textures);
    for (i32 i = 0; i < 6; i++)
        CHECK_EQ(out[i].rect.x, i);
}

static void sort_copies_the_source_and_color_with_the_rect()
{
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    RectInstance hud = rect_at(1);
    hud.source = { .x = 16, .y = 0, .w = 16, .h = 16 };
    hud.color = renderer::rgb(0x112233);
    RectInstance background = rect_at(0);
    background.source = { .x = 0, .y = 0, .w = 1, .h = 1 };
    background.color = renderer::rgb(0x445566);
    batch::add(&b, hud, BAT, LAYER_HUD);
    batch::add(&b, background, WHITE, LAYER_BACKGROUND);

    RectInstance out[CAPACITY] = {};
    Texture out_textures[CAPACITY] = {};
    batch::sort(b, out, out_textures);
    CHECK_EQ(out[0].source.w, 1);
    CHECK_EQ(out[0].color.r, 0x44);
    CHECK_EQ(out[0].color.b, 0x66);
    CHECK_EQ(out[1].source.x, 16);
    CHECK_EQ(out[1].color.g, 0x22);
    CHECK_EQ(out[1].color.a, 255);
}

static void sort_moves_each_texture_with_its_rect()
{
    alignas(16) u8 memory[1024] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, CAPACITY);

    batch::add(&b, rect_at(2), FONT, LAYER_HUD);
    batch::add(&b, rect_at(1), BAT, LAYER_ENTITIES);
    batch::add(&b, rect_at(0), WHITE, LAYER_BACKGROUND);

    RectInstance out[CAPACITY] = {};
    Texture out_textures[CAPACITY] = {};
    batch::sort(b, out, out_textures);
    CHECK_EQ(out_textures[0].index, WHITE.index);
    CHECK_EQ(out_textures[1].index, BAT.index);
    CHECK_EQ(out_textures[2].index, FONT.index);
}

static void no_rects_need_no_draw_calls()
{
    DrawCall calls[CAPACITY] = {};
    CHECK_EQ(batch::draw_calls(nullptr, 0, calls), 0);
}

static void one_texture_is_one_draw_call()
{
    Texture textures[] = { BAT, BAT, BAT, BAT };
    DrawCall calls[CAPACITY] = {};
    CHECK_EQ(batch::draw_calls(textures, ARRAY_COUNT(textures), calls), 1);
    CHECK_EQ(calls[0].texture.index, BAT.index);
    CHECK_EQ(calls[0].first, 0);
    CHECK_EQ(calls[0].count, 4);
}

static void a_texture_used_again_later_starts_another_draw_call()
{
    // The draw calls keep drawing order, so a texture that comes back after
    // another one needs a draw call of its own again.
    Texture textures[] = { WHITE, WHITE, BAT, WHITE };
    DrawCall calls[CAPACITY] = {};
    CHECK_EQ(batch::draw_calls(textures, ARRAY_COUNT(textures), calls), 3);
    CHECK_EQ(calls[0].texture.index, WHITE.index);
    CHECK_EQ(calls[0].first, 0);
    CHECK_EQ(calls[0].count, 2);
    CHECK_EQ(calls[1].texture.index, BAT.index);
    CHECK_EQ(calls[1].first, 2);
    CHECK_EQ(calls[1].count, 1);
    CHECK_EQ(calls[2].texture.index, WHITE.index);
    CHECK_EQ(calls[2].first, 3);
    CHECK_EQ(calls[2].count, 1);
}

static void the_frame_from_chapter_12()
{
    // The game's frame, drawn in the order game.cpp draws it: the floor's 32
    // tiles, the pillar, the bat's shadow, the bat, and READY's 5 letters.
    // Sorted, the pillar moves after the bat, and the shadow joins the
    // floor's draw call: 4 draw calls for 40 rectangles.
    alignas(16) u8 memory[4096] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));
    Batch b = {};
    batch::init(&b, &a, 64);

    for (i32 i = 0; i < 32; i++)
        batch::add(&b, rect_at(i), WHITE, LAYER_BACKGROUND);
    batch::add(&b, rect_at(100), WHITE, LAYER_FOREGROUND); // pillar
    batch::add(&b, rect_at(101), WHITE, LAYER_ENTITIES);   // shadow
    batch::add(&b, rect_at(102), BAT, LAYER_ENTITIES);     // bat
    for (i32 i = 0; i < 5; i++)
        batch::add(&b, rect_at(200 + i), FONT, LAYER_HUD); // READY

    RectInstance out[64] = {};
    Texture out_textures[64] = {};
    batch::sort(b, out, out_textures);
    DrawCall calls[64] = {};
    i32 call_count = batch::draw_calls(out_textures, b.count, calls);

    CHECK_EQ(call_count, 4);
    CHECK_EQ(calls[0].texture.index, WHITE.index); // the floor and the shadow
    CHECK_EQ(calls[0].count, 33);
    CHECK_EQ(calls[1].texture.index, BAT.index);
    CHECK_EQ(calls[1].first, 33);
    CHECK_EQ(calls[2].texture.index, WHITE.index); // the pillar
    CHECK_EQ(out[calls[2].first].rect.x, 100);
    CHECK_EQ(calls[3].texture.index, FONT.index);
    CHECK_EQ(calls[3].first, 35);
    CHECK_EQ(calls[3].count, 5);
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
    Texture out_textures[CAPACITY] = {};
    batch::sort(b, out, out_textures);
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
    Texture out_textures[CAPACITY] = {};
    batch::sort(b, out, out_textures);
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
    RUN(sort_copies_the_source_and_color_with_the_rect);
    RUN(sort_moves_each_texture_with_its_rect);
    RUN(clear_empties_the_batch);
    RUN(a_full_batch_holds_capacity_rects);
    RUN(no_rects_need_no_draw_calls);
    RUN(one_texture_is_one_draw_call);
    RUN(a_texture_used_again_later_starts_another_draw_call);
    RUN(the_frame_from_chapter_12);
    return test::report();
}
