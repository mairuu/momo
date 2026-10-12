// options_test.cpp: tests for engine/options.h.
#include "test.h"

#include "options.h"

// Each test passes a made-up command line, argv[0] first, as main gets it.
// The ones momo refuses log why, so their errors show up in the output.

static void no_arguments_is_a_plain_run()
{
    const char* argv[] = { "momo" };
    Options options = {};
    CHECK(options::parse(ARRAY_COUNT(argv), argv, &options));
    CHECK(!options.overlay);
    CHECK(!options.boxes);
    CHECK(options.capture_path == nullptr);
}

static void overlay_shows_the_overlay_from_the_start()
{
    const char* argv[] = { "momo", "--overlay" };
    Options options = {};
    CHECK(options::parse(ARRAY_COUNT(argv), argv, &options));
    CHECK(options.overlay);
    CHECK(!options.boxes);
}

static void boxes_shows_the_boxes_from_the_start()
{
    const char* argv[] = { "momo", "--boxes" };
    Options options = {};
    CHECK(options::parse(ARRAY_COUNT(argv), argv, &options));
    CHECK(options.boxes);
    CHECK(!options.overlay); // each one is shown on its own
}

static void capture_takes_a_tick_and_a_file()
{
    const char* argv[] = { "momo", "--capture", "120", "shot.png" };
    Options options = {};
    CHECK(options::parse(ARRAY_COUNT(argv), argv, &options));
    CHECK_EQ(options.capture_tick, 120);
    CHECK(options.capture_path == argv[3]); // the same text, not a copy
}

static void capture_at_tick_0_saves_the_first_picture()
{
    const char* argv[] = { "momo", "--capture", "0", "shot.png" };
    Options options = {};
    CHECK(options::parse(ARRAY_COUNT(argv), argv, &options));
    CHECK_EQ(options.capture_tick, 0);
}

static void arguments_come_in_any_order()
{
    const char* argv[] = { "momo", "--boxes", "--capture", "60", "shot.png", "--overlay" };
    Options options = {};
    CHECK(options::parse(ARRAY_COUNT(argv), argv, &options));
    CHECK(options.overlay);
    CHECK(options.boxes);
    CHECK_EQ(options.capture_tick, 60);
}

static void a_tick_is_only_digits()
{
    const char* ticks[] = { "-1", "1.5", "12x", "1e3", " 7", "" };
    for (const char* tick : ticks) {
        const char* argv[] = { "momo", "--capture", tick, "shot.png" };
        Options options = {};
        CHECK(!options::parse(ARRAY_COUNT(argv), argv, &options));
    }
}

static void a_tick_fits_in_an_i32()
{
    const char* biggest[] = { "momo", "--capture", "2147483647", "shot.png" };
    Options options = {};
    CHECK(options::parse(ARRAY_COUNT(biggest), biggest, &options));
    CHECK_EQ(options.capture_tick, 2147483647);

    const char* too_big[] = { "momo", "--capture", "2147483648", "shot.png" };
    CHECK(!options::parse(ARRAY_COUNT(too_big), too_big, &options));
}

static void capture_without_a_file_is_refused()
{
    const char* argv[] = { "momo", "--capture", "120" };
    Options options = {};
    CHECK(!options::parse(ARRAY_COUNT(argv), argv, &options));
}

static void an_unknown_argument_is_refused()
{
    const char* argv[] = { "momo", "--fullscreen" };
    Options options = {};
    CHECK(!options::parse(ARRAY_COUNT(argv), argv, &options));
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(no_arguments_is_a_plain_run);
    RUN(overlay_shows_the_overlay_from_the_start);
    RUN(boxes_shows_the_boxes_from_the_start);
    RUN(capture_takes_a_tick_and_a_file);
    RUN(capture_at_tick_0_saves_the_first_picture);
    RUN(arguments_come_in_any_order);
    RUN(a_tick_is_only_digits);
    RUN(a_tick_fits_in_an_i32);
    RUN(capture_without_a_file_is_refused);
    RUN(an_unknown_argument_is_refused);
    return test::report();
}
