// str_test.cpp: tests for engine/str.h.
#include "test.h"

#include "str.h"

// STR's length is worked out while compiling, so the compiler can check it.
static_assert(STR("game.so").len == 7);
static_assert(STR("").len == 0);

static void zero_str_is_the_empty_string()
{
    Str empty = {};
    CHECK_EQ(empty.len, 0);
    CHECK(str::eq(empty, STR("")));
    CHECK(str::eq(empty, empty));
    CHECK(!str::eq(empty, STR("a")));
}

static void fmt_formats_into_the_arena()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    Str s = str::fmt(&a, "%d bats", 12);
    CHECK(str::eq(s, STR("12 bats")));
    CHECK((u8*)s.data == memory);
    CHECK_EQ(s.data[s.len], 0); // the 0 after the text
    CHECK_EQ(a.used, s.len + 1);
}

static void fmt_packs_strings_back_to_back()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    // chars need no alignment, so there's no padding between strings.
    Str first = str::fmt(&a, "%s", "stage");
    Str second = str::fmt(&a, "%s", "");
    Str third = str::fmt(&a, "%d", 1);
    CHECK(second.data == first.data + first.len + 1);
    CHECK(third.data == second.data + 1);
    CHECK_EQ(second.len, 0);
    CHECK_EQ(a.used, 6 + 1 + 2);
}

static void fmt_prints_a_str_with_precision_star()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    // The precision, .*, is how many chars of the string to print, so a Str
    // that isn't followed by a 0 prints correctly. load_game does this.
    Str folder = str::slice(STR("build/debug/momo"), 0, 11);
    Str path = str::fmt(&a, "%.*s/game.so", folder.len, folder.data);
    CHECK(str::eq(path, STR("build/debug/game.so")));
}

static void fmt_formats_64_bit_numbers_with_lld()
{
    alignas(16) u8 memory[64] = {};
    Arena a = {};
    arena::init(&a, memory, sizeof(memory));

    // An i64 is a long long, so %lld takes it with no cast.
    i64 big = (i64)1 << 40;
    Str s = str::fmt(&a, "%lld bytes", big);
    CHECK(str::eq(s, STR("1099511627776 bytes")));
}

static void slice_points_into_the_original()
{
    Str path = STR("build/debug/momo");
    Str folder = str::slice(path, 6, 11);
    CHECK(str::eq(folder, STR("debug")));
    CHECK(folder.data == path.data + 6);
}

static void slice_can_take_nothing_or_everything()
{
    Str path = STR("build/debug/momo");
    CHECK_EQ(str::slice(path, 3, 3).len, 0);
    CHECK_EQ(str::slice(path, path.len, path.len).len, 0);
    CHECK(str::eq(str::slice(path, 0, path.len), path));
    CHECK_EQ(str::slice(Str{}, 0, 0).len, 0);
}

static void find_last_finds_the_last_one()
{
    CHECK_EQ(str::find_last(STR("build/debug/momo"), '/'), 11);
    CHECK_EQ(str::find_last(STR("/"), '/'), 0);
    CHECK_EQ(str::find_last(STR("momo"), '/'), -1);
    CHECK_EQ(str::find_last(Str{}, '/'), -1);
}

static void find_last_looks_only_inside_the_str()
{
    // "build/debug" is followed by "/momo" in memory, but the slice ends
    // before it, so the last '/' it holds is the one after "build".
    Str folder = str::slice(STR("build/debug/momo"), 0, 11);
    CHECK_EQ(str::find_last(folder, '/'), 5);
}

static void eq_compares_chars_not_pointers()
{
    // Two arrays, so the same chars at two different addresses.
    char a[] = "bats";
    char b[] = "bats";
    CHECK(str::eq(Str{ .data = a, .len = 4 }, Str{ .data = b, .len = 4 }));
    CHECK(!str::eq(STR("bat"), STR("bats")));
    CHECK(!str::eq(STR("bats"), STR("bots")));
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(zero_str_is_the_empty_string);
    RUN(fmt_formats_into_the_arena);
    RUN(fmt_packs_strings_back_to_back);
    RUN(fmt_prints_a_str_with_precision_star);
    RUN(fmt_formats_64_bit_numbers_with_lld);
    RUN(slice_points_into_the_original);
    RUN(slice_can_take_nothing_or_everything);
    RUN(find_last_finds_the_last_one);
    RUN(find_last_looks_only_inside_the_str);
    RUN(eq_compares_chars_not_pointers);
    return test::report();
}
