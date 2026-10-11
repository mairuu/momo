// fx.h: fixed-point numbers, which the game moves things with. Chapter 15
// explains them.
#pragma once

#include "base.h"

// An Fx is a number with 8 bits of fraction, a 24.8 fixed-point number: raw
// counts 256ths, so 1.0 is 256 and MegaMan's walking speed, 1.375 pixels a
// tick, is 352. Every value is a whole number of 256ths, so adding and
// subtracting are exact, and the same on every machine. The NES's subpixels
// were 256ths too.
//
// Everything here is constexpr and in this header, like base.h's min and max,
// so the game compiles each function into its own code, and none of it is
// MOMO_API.

constexpr i32 FX_FRACTION_BITS = 8;
constexpr i32 FX_ONE = 1 << FX_FRACTION_BITS; // 256: the raw number for 1.0

// The whole numbers an Fx holds: about 8 million pixels either way.
constexpr i32 FX_MIN_INT = INT32_MIN / FX_ONE; // -8,388,608
constexpr i32 FX_MAX_INT = INT32_MAX / FX_ONE; //  8,388,607

// A struct rather than a plain i32, so that adding a whole number of pixels
// to an Fx, `x += 1`, doesn't compile instead of adding 1/256. A zeroed Fx is
// 0.0.
struct Fx {
    i32 raw; // the number times 256
};

// A point, or a step, on the screen: x to the right and y down, in pixels.
struct Fx2 {
    Fx x;
    Fx y;
};

namespace fx {

// x, a whole number, as an Fx.
constexpr Fx from_int(i32 x)
{
    ASSERT(x >= FX_MIN_INT && x <= FX_MAX_INT);
    return { .raw = x * FX_ONE };
}

// x as an Fx, rounded to the nearest 256th: 1.375 is exact, and 0.1 becomes
// 26/256, which is 0.1015625. consteval means the compiler works it out, and
// a call it can't work out doesn't compile, so the game writes its constants
// as decimals and never computes with a double while it runs.
consteval Fx from_double(f64 x)
{
    ASSERT(x >= FX_MIN_INT && x <= FX_MAX_INT);
    f64 scaled = x * FX_ONE;
    return { .raw = (i32)(scaled < 0 ? scaled - 0.5 : scaled + 0.5) };
}

// The whole number at or below x: 1.75 is 1, and -0.25 is -1. That's the
// pixel x is drawn at. Shifting right by 8 drops the fraction's bits, and
// shifting a negative number rounds toward minus infinity too, which C++20
// guarantees. Dividing by 256 would round toward zero, and -0.25 would be 0.
constexpr i32 floor(Fx x)
{
    return x.raw >> FX_FRACTION_BITS;
}

// x as a double, exactly, for printing. Never for the game's arithmetic.
constexpr f64 to_double(Fx x)
{
    return (f64)x.raw / FX_ONE;
}

} // namespace fx

// ---- Fx's operators ---------------------------------------------------------

constexpr Fx operator+(Fx a, Fx b)
{
    return { .raw = a.raw + b.raw };
}

constexpr Fx operator-(Fx a, Fx b)
{
    return { .raw = a.raw - b.raw };
}

constexpr Fx operator-(Fx a)
{
    return { .raw = -a.raw };
}

// Each raw number is 256 times its value, so the product of two is 256 * 256
// times too big, and shifting right by 8 takes one 256 back out. The product
// is worked out in an i64: in an i32, even 1000.0 * 1000.0 would overflow.
// The shift rounds down, as floor does.
constexpr Fx operator*(Fx a, Fx b)
{
    i64 product = ((i64)a.raw * b.raw) >> FX_FRACTION_BITS;
    ASSERT(product >= INT32_MIN && product <= INT32_MAX); // the answer must fit an Fx
    return { .raw = (i32)product };
}

// += and -= return nothing, so they can't hide inside a longer expression.
constexpr void operator+=(Fx& a, Fx b)
{
    a.raw += b.raw;
}

constexpr void operator-=(Fx& a, Fx b)
{
    a.raw -= b.raw;
}

constexpr bool operator==(Fx a, Fx b)
{
    return a.raw == b.raw;
}

constexpr bool operator!=(Fx a, Fx b)
{
    return a.raw != b.raw;
}

constexpr bool operator<(Fx a, Fx b)
{
    return a.raw < b.raw;
}

constexpr bool operator<=(Fx a, Fx b)
{
    return a.raw <= b.raw;
}

constexpr bool operator>(Fx a, Fx b)
{
    return a.raw > b.raw;
}

constexpr bool operator>=(Fx a, Fx b)
{
    return a.raw >= b.raw;
}

// ---- Fx2's operators --------------------------------------------------------

constexpr Fx2 operator+(Fx2 a, Fx2 b)
{
    return { .x = a.x + b.x, .y = a.y + b.y };
}

constexpr Fx2 operator-(Fx2 a, Fx2 b)
{
    return { .x = a.x - b.x, .y = a.y - b.y };
}

constexpr void operator+=(Fx2& a, Fx2 b)
{
    a.x += b.x;
    a.y += b.y;
}

constexpr void operator-=(Fx2& a, Fx2 b)
{
    a.x -= b.x;
    a.y -= b.y;
}

constexpr bool operator==(Fx2 a, Fx2 b)
{
    return a.x == b.x && a.y == b.y;
}

constexpr bool operator!=(Fx2 a, Fx2 b)
{
    return !(a == b);
}
