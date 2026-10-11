// fx_test.cpp: tests for engine/fx.h.
#include "test.h"

#include "fx.h"

// An Fx's raw number is exact, so the tests compare raw numbers with
// CHECK_EQ: 1.0 is 256, 1.375 is 352, and 1/256, the smallest step, is 1.

static void zero_is_zero()
{
    Fx x = {};
    CHECK_EQ(x.raw, 0);
    CHECK(x == fx::from_int(0));
}

static void whole_numbers_count_256ths()
{
    CHECK_EQ(fx::from_int(1).raw, 256);
    CHECK_EQ(fx::from_int(3).raw, 768);
    CHECK_EQ(fx::from_int(-2).raw, -512);
    CHECK_EQ(fx::from_int(FX_MAX_INT).raw, 2147483392); // 8,388,607 * 256
    CHECK_EQ(fx::from_int(FX_MIN_INT).raw, INT32_MIN);
}

static void decimals_that_are_256ths_are_exact()
{
    CHECK_EQ(fx::from_double(1.375).raw, 352); // 1 and 96/256
    CHECK_EQ(fx::from_double(0.5).raw, 128);
    CHECK_EQ(fx::from_double(-1.375).raw, -352);
    CHECK_EQ(fx::from_double(0.00390625).raw, 1); // 1/256
}

static void other_decimals_round_to_the_nearest_256th()
{
    CHECK_EQ(fx::from_double(0.1).raw, 26); // 25.6 256ths
    CHECK_EQ(fx::from_double(-0.1).raw, -26);
    CHECK_EQ(fx::from_double(0.001).raw, 0); // 0.256 256ths
}

static void floor_drops_the_fraction()
{
    CHECK_EQ(fx::floor(fx::from_int(5)), 5);
    CHECK_EQ(fx::floor(fx::from_double(1.375)), 1);
    CHECK_EQ(fx::floor(fx::from_double(1.99609375)), 1); // 1 and 255/256
}

static void floor_rounds_down_below_zero_too()
{
    // Toward minus infinity, not toward zero: an Fx a quarter of a pixel
    // left of 0 is drawn at -1, not 0.
    CHECK_EQ(fx::floor(fx::from_double(-0.25)), -1);
    CHECK_EQ(fx::floor(fx::from_double(-0.00390625)), -1);
    CHECK_EQ(fx::floor(fx::from_int(-1)), -1);
    CHECK_EQ(fx::floor(fx::from_double(-1.375)), -2);
}

static void adding_and_subtracting_are_exact()
{
    // A tenth of a pixel isn't a 256th, but whatever it rounded to, every
    // add is exact: 600 adds are 600 times the raw number.
    Fx tenth = fx::from_double(0.1);
    Fx x = {};
    for (i32 i = 0; i < 600; i++)
        x += tenth;
    CHECK_EQ(x.raw, 15600); // 600 * 26
    for (i32 i = 0; i < 600; i++)
        x -= tenth;
    CHECK_EQ(x.raw, 0);
    CHECK_EQ((fx::from_double(2.5) + fx::from_double(0.75)).raw, fx::from_double(3.25).raw);
    CHECK_EQ((fx::from_double(2.5) - fx::from_double(3.75)).raw, fx::from_double(-1.25).raw);
    CHECK_EQ((-fx::from_double(1.375)).raw, -352);
}

static void walking_1_375_a_tick_moves_11_pixels_every_8_ticks()
{
    // The player's walk (game.cpp), from x = 48: the pixel it's drawn at
    // moves 1, 1, 2, 1, 1, 2, 1, 2, then the same again.
    constexpr i32 DRAWN_AT[] = { 49, 50, 52, 53, 54, 56, 57, 59, 60, 61, 63, 64, 65, 67, 68, 70 };
    Fx x = fx::from_int(48);
    for (i32 tick = 0; tick < ARRAY_COUNT(DRAWN_AT); tick++) {
        x += fx::from_double(1.375);
        CHECK_EQ(fx::floor(x), DRAWN_AT[tick]);
    }
    CHECK(x == fx::from_int(70)); // 48 + 16 * 1.375, with nothing left over
}

static void multiplying_keeps_8_bits_of_fraction()
{
    CHECK_EQ((fx::from_double(1.5) * fx::from_double(2.5)).raw, fx::from_double(3.75).raw);
    CHECK_EQ((fx::from_double(0.5) * fx::from_double(0.5)).raw, fx::from_double(0.25).raw);
    CHECK_EQ((fx::from_double(-1.5) * fx::from_double(2.5)).raw, fx::from_double(-3.75).raw);
    CHECK_EQ((fx::from_double(1.375) * fx::from_int(8)).raw, fx::from_int(11).raw);
}

static void multiplying_rounds_down()
{
    // Half of 1/256 is 1/512, which an Fx can't hold: it rounds down to 0,
    // and minus 1/512 rounds down to minus 1/256.
    Fx smallest = { .raw = 1 };
    CHECK_EQ((smallest * fx::from_double(0.5)).raw, 0);
    CHECK_EQ((-smallest * fx::from_double(0.5)).raw, -1);
}

static void multiplying_big_numbers_works_in_64_bits()
{
    // The raw numbers multiplied are 256,000 * 256,000, about 2^36, which
    // overflows an i32. The answer, 1,000,000, fits an Fx.
    Fx thousand = fx::from_int(1000);
    CHECK_EQ((thousand * thousand).raw, fx::from_int(1'000'000).raw);
    CHECK_EQ((thousand * -thousand).raw, fx::from_int(-1'000'000).raw);
}

static void comparisons_and_clamp_work_on_fx()
{
    Fx a = fx::from_double(1.5);
    Fx b = fx::from_int(2);
    CHECK(a < b && a <= b && b > a && b >= a && a != b);
    CHECK(a <= a && a >= a && a == a);
    CHECK(clamp(fx::from_int(300), fx::from_int(0), fx::from_int(240)) == fx::from_int(240));
    CHECK(clamp(fx::from_double(-0.5), fx::from_int(0), fx::from_int(240)) == fx::from_int(0));
    CHECK(min(a, b) == a);
}

static void fx2_works_on_both_coordinates()
{
    Fx2 pos = { .x = fx::from_int(10), .y = fx::from_int(20) };
    Fx2 vel = { .x = fx::from_double(1.375), .y = fx::from_double(-0.5) };
    pos += vel;
    CHECK_EQ(pos.x.raw, fx::from_double(11.375).raw);
    CHECK_EQ(pos.y.raw, fx::from_double(19.5).raw);
    pos -= vel;
    CHECK(pos == (Fx2{ .x = fx::from_int(10), .y = fx::from_int(20) }));
    CHECK(pos + vel - vel == pos);
    CHECK(pos != pos + vel);
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(zero_is_zero);
    RUN(whole_numbers_count_256ths);
    RUN(decimals_that_are_256ths_are_exact);
    RUN(other_decimals_round_to_the_nearest_256th);
    RUN(floor_drops_the_fraction);
    RUN(floor_rounds_down_below_zero_too);
    RUN(adding_and_subtracting_are_exact);
    RUN(walking_1_375_a_tick_moves_11_pixels_every_8_ticks);
    RUN(multiplying_keeps_8_bits_of_fraction);
    RUN(multiplying_rounds_down);
    RUN(multiplying_big_numbers_works_in_64_bits);
    RUN(comparisons_and_clamp_work_on_fx);
    RUN(fx2_works_on_both_coordinates);
    return test::report();
}
