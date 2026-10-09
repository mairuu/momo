// base.h: the types and macros every file in momo starts from.
// Chapter 01 explains the dialect they belong to.
#pragma once

#include <stdint.h>
#include <stdio.h>

// ---- Types ----------------------------------------------------------------

// Counts, indices and anything you do arithmetic on are signed: i32 for counts
// and indices, i64 for byte sizes. Unsigned types are for bits, flags and
// formats defined outside momo (files, pixels, GPU data).
//
// i64 and u64 are long long rather than int64_t, which is long on Linux and
// long long on Windows. printf's %lld and %llu then match them on every
// platform, and GCC's format check catches a %ld that would only be right on
// Linux (chapter 05).
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = long long;

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = unsigned long long;

using f32 = float;
using f64 = double;

static_assert(sizeof(i64) == 8 && sizeof(u64) == 8, "i64 and u64 must be 64 bits");
static_assert(sizeof(f32) == 4 && sizeof(f64) == 8, "f32 and f64 must be 32 and 64 bits");
static_assert(sizeof(void*) == 8, "momo assumes a 64-bit platform");

// Byte sizes: 16 * MB.
constexpr i64 KB = 1024;
constexpr i64 MB = 1024 * KB;

// ---- MOMO_API -------------------------------------------------------------

// MOMO_API marks a function that one binary calls in another. Both shared
// libraries are compiled with -fvisibility=hidden, so any function without it
// can only be called from inside its own library. It lives here because every
// engine module's header uses it (chapter 04).
//
// A Windows port would make this __declspec(dllexport) while building the
// library and __declspec(dllimport) everywhere else.
#define MOMO_API __attribute__((visibility("default")))

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

// ---- Math -----------------------------------------------------------------

// Templates, so they work on every number type and return the type they were
// given. Both arguments must have the same type: with an i64 size,
// min(size, 4096) doesn't compile, and min(size, (i64)4096) does. Chapter 03
// explains why these aren't macros.
template <typename T>
T min(T a, T b)
{
    return a < b ? a : b;
}

template <typename T>
T max(T a, T b)
{
    return a > b ? a : b;
}

// x, moved inside the range lo..hi (both ends included).
template <typename T>
T clamp(T x, T lo, T hi)
{
    ASSERT(lo <= hi);
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}
