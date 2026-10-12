// str.h: momo's strings, a pointer and a length. Chapter 05 explains them,
// and chapter 17 adds find and next_line.
#pragma once

#include "base.h"
#include "arena.h"

// A piece of text: len chars starting at data. A Str doesn't own its memory,
// and data isn't always followed by a 0, so it can point into the middle of
// other text. All zeros is the empty string.
//
// A Str is only as good as the memory it points into: a Str built in the
// frame arena lasts until the end of the frame. Print one with "%.*s":
//
//     LOG_INFO("loaded %.*s", path.len, path.data);
struct Str {
    const char* data;
    i32 len;
};

// A Str for a string literal, with its length worked out while compiling:
// STR("game.so"). The "" in front only compiles if the argument is a string
// literal, so a char* can't slip in and get the length of a pointer.
#define STR(literal) (Str{ .data = "" literal, .len = (i32)sizeof("" literal) - 1 })

namespace str {

// Formats like printf into memory pushed from arena, and returns the text.
// It's followed by a 0, so result.data can go straight to a C function that
// wants a C string. GCC checks the format against the arguments.
MOMO_API Str fmt(Arena* arena, const char* format, ...) __attribute__((format(printf, 2, 3)));

// The chars from start up to, but not including, end. Nothing is copied: the
// result points into s.
MOMO_API Str slice(Str s, i32 start, i32 end);

// The index of the first c in s, or -1 if there isn't one.
MOMO_API i32 find(Str s, char c);

// The index of the last c in s, or -1 if there isn't one.
MOMO_API i32 find_last(Str s, char c);

// Cuts the first line off text and returns it, without its '\n'. text moves
// on to the start of the next line, and is empty once the last line is cut.
// Nothing is copied: the line points into text. A '\n' at the very end
// starts no new line, so "a\nb\n" and "a\nb" are both two lines, as an
// editor counts them:
//
//     Str rest = text;
//     while (rest.len > 0) {
//         Str line = str::next_line(&rest);
//         ...
//     }
MOMO_API Str next_line(Str* text);

// True if a and b hold the same chars.
MOMO_API bool eq(Str a, Str b);

} // namespace str
