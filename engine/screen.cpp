// screen.cpp: where the screen goes in the window. Chapter 11 explains it.
#include "screen.h"

namespace screen {

ScreenFit fit(i32 window_width, i32 window_height)
{
    // How many whole screens fit across, and how many down. The smaller of
    // the two fits both ways. Integer division rounds down, which is what
    // makes the scale whole.
    i32 scale = min(window_width / SCREEN_WIDTH, window_height / SCREEN_HEIGHT);
    scale = max(scale, 1);

    i32 width = SCREEN_WIDTH * scale;
    i32 height = SCREEN_HEIGHT * scale;

    // Centered: half the leftover on each side. When the leftover is odd,
    // the extra pixel goes to the right or the bottom.
    return {
        .scale = scale,
        .rect = { .x = (window_width - width) / 2,
                  .y = (window_height - height) / 2,
                  .w = width,
                  .h = height },
    };
}

} // namespace screen
