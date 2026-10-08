// base.h: the types and macros every file in momo starts from.
// Chapter 01 explains the dialect they belong to.
#pragma once

#include <stdint.h>
#include <stdio.h>

// ---- Types ----------------------------------------------------------------

// Counts, indices and anything you do arithmetic on are signed: i32 for counts
// and indices, i64 for byte sizes. Unsigned types are for bits, flags and
// formats defined outside momo (files, pixels, GPU data).
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using f32 = float;
using f64 = double;

static_assert(sizeof(f32) == 4 && sizeof(f64) == 8, "f32 and f64 must be 32 and 64 bits");
static_assert(sizeof(void*) == 8, "momo assumes a 64-bit platform");

// ---- ASSERT ---------------------------------------------------------------

// ASSERT(cond) states something that is true unless there's a bug. It is on
// in every build. When it fails, it prints "file:line: ..." in the same format
// as a compiler error, then executes a trap instruction, so gdb stops inside
// the function that failed with all its variables intact.
//
// __builtin_trap is GCC and Clang. A Windows port would use __debugbreak().
#define ASSERT(cond)                                                          \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "%s:%d: ASSERT(%s) failed\n",                     \
                    __FILE__, __LINE__, #cond);                               \
            __builtin_trap();                                                 \
        }                                                                     \
    } while (0)

// ---- Arrays ---------------------------------------------------------------

// The number of elements in a fixed-size array, as an i32. Passing a pointer
// by mistake is caught by -Wsizeof-pointer-div, which -Wall turns on.
#define ARRAY_COUNT(a) ((i32)(sizeof(a) / sizeof((a)[0])))
