// screen_test.cpp: tests for engine/screen.h.
#include "test.h"

#include "screen.h"

// screen::fit is arithmetic on window sizes, so the tests make up windows,
// with no window and no GPU. The screen is 256x240.

static void a_window_three_times_the_screen_is_filled()
{
    ScreenFit fit = screen::fit(768, 720);
    CHECK_EQ(fit.scale, 3);
    CHECK_EQ(fit.rect.x, 0);
    CHECK_EQ(fit.rect.y, 0);
    CHECK_EQ(fit.rect.w, 768);
    CHECK_EQ(fit.rect.h, 720);
}

static void a_wide_window_gets_bars_left_and_right()
{
    // 1280 / 256 = 5 screens across, but 480 / 240 = only 2 down.
    ScreenFit fit = screen::fit(1280, 480);
    CHECK_EQ(fit.scale, 2);
    CHECK_EQ(fit.rect.x, 384); // (1280 - 512) / 2
    CHECK_EQ(fit.rect.y, 0);
    CHECK_EQ(fit.rect.w, 512);
    CHECK_EQ(fit.rect.h, 480);
}

static void a_tall_window_gets_bars_above_and_below()
{
    ScreenFit fit = screen::fit(512, 1000);
    CHECK_EQ(fit.scale, 2);
    CHECK_EQ(fit.rect.x, 0);
    CHECK_EQ(fit.rect.y, 260); // (1000 - 480) / 2
    CHECK_EQ(fit.rect.h, 480);
}

static void a_window_that_fits_neither_way_gets_bars_all_round()
{
    // A 1920x1080 monitor: 7.5 screens across, 4.5 down.
    ScreenFit fit = screen::fit(1920, 1080);
    CHECK_EQ(fit.scale, 4);
    CHECK_EQ(fit.rect.x, 448); // (1920 - 1024) / 2
    CHECK_EQ(fit.rect.y, 60);  // (1080 - 960) / 2
    CHECK_EQ(fit.rect.w, 1024);
    CHECK_EQ(fit.rect.h, 960);
}

static void one_pixel_short_of_twice_is_once()
{
    ScreenFit fit = screen::fit(511, 480);
    CHECK_EQ(fit.scale, 1);
    CHECK_EQ(fit.rect.x, 127); // 255 / 2, rounded down
    CHECK_EQ(fit.rect.y, 120);
}

static void an_odd_leftover_pixel_goes_to_the_right()
{
    // One pixel more than three screens across: 0 on the left, 1 on the
    // right.
    ScreenFit fit = screen::fit(769, 720);
    CHECK_EQ(fit.scale, 3);
    CHECK_EQ(fit.rect.x, 0);
    CHECK_EQ(fit.rect.w, 768);
}

static void a_window_smaller_than_the_screen_cuts_its_edges_off()
{
    // Scale 1, still centered, so the screen hangs over every edge.
    ScreenFit fit = screen::fit(200, 100);
    CHECK_EQ(fit.scale, 1);
    CHECK_EQ(fit.rect.x, -28); // (200 - 256) / 2
    CHECK_EQ(fit.rect.y, -70); // (100 - 240) / 2
    CHECK_EQ(fit.rect.w, 256);
    CHECK_EQ(fit.rect.h, 240);
}

static void a_window_of_no_size_is_scale_1()
{
    // A minimized window can have no pixels at all.
    ScreenFit fit = screen::fit(0, 0);
    CHECK_EQ(fit.scale, 1);
    CHECK_EQ(fit.rect.x, -128);
    CHECK_EQ(fit.rect.y, -120);
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(a_window_three_times_the_screen_is_filled);
    RUN(a_wide_window_gets_bars_left_and_right);
    RUN(a_tall_window_gets_bars_above_and_below);
    RUN(a_window_that_fits_neither_way_gets_bars_all_round);
    RUN(one_pixel_short_of_twice_is_once);
    RUN(an_odd_leftover_pixel_goes_to_the_right);
    RUN(a_window_smaller_than_the_screen_cuts_its_edges_off);
    RUN(a_window_of_no_size_is_scale_1);
    return test::report();
}
