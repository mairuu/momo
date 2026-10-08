// base_test.cpp: tests for engine/base.h.
#include "test.h"

// ARRAY_COUNT is worked out while compiling, so the compiler can check it.
// Prefer static_assert for anything it can check; tests are for the rest.
constexpr i32 SEVEN_ZEROS[7] = {};
static_assert(ARRAY_COUNT(SEVEN_ZEROS) == 7);

static void min_and_max_pick_one_argument()
{
    CHECK_EQ(min(3, 5), 3);
    CHECK_EQ(min(5, 3), 3);
    CHECK_EQ(max(3, 5), 5);
    CHECK_EQ(max(5, 3), 5);
    CHECK_EQ(min(-4, 2), -4);
    CHECK_EQ(max(-4, -9), -4);
    CHECK_EQ(min(6, 6), 6);
}

static void min_and_max_keep_the_type()
{
    // Both arguments are i64, so the result is an i64 too. A min written
    // for i32 would turn 2^40 into 0.
    i64 big = (i64)1 << 40;
    CHECK_EQ(max(big, (i64)1), big);
    CHECK_EQ(min(-big, (i64)-1), -big);
}

static void min_and_max_work_on_floats()
{
    // CHECK_EQ is only for integers, so floats use CHECK. 1.5 and 2.5 are
    // exact in binary, so comparing them with == is safe.
    CHECK(min(2.5f, 1.5f) == 1.5f);
    CHECK(max(2.5f, 1.5f) == 2.5f);
}

static void clamp_leaves_values_inside_alone()
{
    CHECK_EQ(clamp(5, 0, 10), 5);
    CHECK_EQ(clamp(0, 0, 10), 0);
    CHECK_EQ(clamp(10, 0, 10), 10);
    CHECK_EQ(clamp(7, 7, 7), 7);
}

static void clamp_moves_values_outside_to_the_nearest_end()
{
    CHECK_EQ(clamp(-3, 0, 10), 0);
    CHECK_EQ(clamp(11, 0, 10), 10);
    CHECK_EQ(clamp(-100, -20, -10), -20);
    CHECK_EQ(clamp(1, 4, 4), 4);
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(min_and_max_pick_one_argument);
    RUN(min_and_max_keep_the_type);
    RUN(min_and_max_work_on_floats);
    RUN(clamp_leaves_values_inside_alone);
    RUN(clamp_moves_values_outside_to_the_nearest_end);
    return test::report();
}
