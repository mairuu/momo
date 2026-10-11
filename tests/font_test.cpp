// font_test.cpp: tests for engine/font.h.
#include "test.h"

#include "font.h"

// The font never calls OpenGL: glyph is arithmetic, and fill_pixels writes
// into memory the test hands it. A glyph's place is checked against
// font.h's layout, 16 to a row in ASCII order, 8 pixels apart.

static void space_is_at_the_top_left()
{
    Rect r = font::glyph(' ');
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(r.w, GLYPH_SIZE);
    CHECK_EQ(r.h, GLYPH_SIZE);
}

static void a_row_holds_16_chars()
{
    // '/' is char 47, 15 after ' ' (32): the last of the first row.
    CHECK_EQ(font::glyph('/').x, 120); // 15 * 8
    CHECK_EQ(font::glyph('/').y, 0);
    // '0' starts the second row.
    CHECK_EQ(font::glyph('0').x, 0);
    CHECK_EQ(font::glyph('0').y, 8);
}

static void a_is_on_the_third_row()
{
    // 'A' is char 65, 33 after ' ': row 2, column 1.
    CHECK_EQ(font::glyph('A').x, 8);
    CHECK_EQ(font::glyph('A').y, 16);
}

static void tilde_is_the_last_char()
{
    // '~' is char 126, 94 after ' ': row 5, column 14, the last row of the
    // 48-pixel picture.
    CHECK_EQ(font::glyph('~').x, 112); // 14 * 8
    CHECK_EQ(font::glyph('~').y, 40);  // 5 * 8
}

static void chars_the_font_lacks_draw_as_a_question_mark()
{
    Rect question = font::glyph('?');
    // A newline, DEL, and the first byte of a UTF-8 'é', which is negative
    // in a char on x86.
    const char lacking[] = { '\n', 127, (char)0xc3 };
    for (char c : lacking) {
        CHECK_EQ(font::glyph(c).x, question.x);
        CHECK_EQ(font::glyph(c).y, question.y);
    }
}

// The color of the pixel at x, y of the font's picture.
static Color pixel_at(const Color* pixels, i32 x, i32 y)
{
    return pixels[y * FONT_WIDTH + x];
}

static void the_picture_has_the_top_of_a_where_glyph_says()
{
    // The 24 KB picture would be a big stack frame, so it's static.
    static Color pixels[FONT_WIDTH * FONT_HEIGHT];
    font::fill_pixels(pixels);

    // 'A''s top row is 0x0C, ..##....: pixels 2 and 3 of its 8.
    Rect a = font::glyph('A');
    CHECK_EQ(pixel_at(pixels, a.x + 1, a.y).a, 0);
    CHECK_EQ(pixel_at(pixels, a.x + 2, a.y).a, 255);
    CHECK_EQ(pixel_at(pixels, a.x + 3, a.y).a, 255);
    CHECK_EQ(pixel_at(pixels, a.x + 4, a.y).a, 0);
    // Its bottom row is empty.
    for (i32 column = 0; column < GLYPH_SIZE; column++)
        CHECK_EQ(pixel_at(pixels, a.x + column, a.y + GLYPH_SIZE - 1).a, 0);
}

static void every_pixel_is_white_or_see_through()
{
    static Color pixels[FONT_WIDTH * FONT_HEIGHT];
    font::fill_pixels(pixels);

    i32 white = 0;
    for (i32 i = 0; i < FONT_WIDTH * FONT_HEIGHT; i++) {
        Color c = pixels[i];
        bool is_white = c.r == 255 && c.g == 255 && c.b == 255 && c.a == 255;
        bool is_clear = c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0;
        CHECK(is_white || is_clear);
        if (is_white)
            white++;
    }
    // Some of each: a font of nothing but see-through pixels would pass the
    // loop too.
    CHECK(white > 0);
    CHECK(white < FONT_WIDTH * FONT_HEIGHT);
}

static void the_cell_after_tilde_is_empty()
{
    // 95 chars leave the last of the 96 cells unused, and it starts out
    // with whatever the memory held.
    static Color pixels[FONT_WIDTH * FONT_HEIGHT];
    for (Color& c : pixels)
        c = renderer::rgb(0x123456);
    font::fill_pixels(pixels);

    for (i32 y = FONT_HEIGHT - GLYPH_SIZE; y < FONT_HEIGHT; y++) {
        for (i32 x = FONT_WIDTH - GLYPH_SIZE; x < FONT_WIDTH; x++)
            CHECK_EQ(pixel_at(pixels, x, y).a, 0);
    }
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(space_is_at_the_top_left);
    RUN(a_row_holds_16_chars);
    RUN(a_is_on_the_third_row);
    RUN(tilde_is_the_last_char);
    RUN(chars_the_font_lacks_draw_as_a_question_mark);
    RUN(the_picture_has_the_top_of_a_where_glyph_says);
    RUN(every_pixel_is_white_or_see_through);
    RUN(the_cell_after_tilde_is_empty);
    return test::report();
}
