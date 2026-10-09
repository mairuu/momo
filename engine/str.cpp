// str.cpp: momo's strings. Chapter 05 explains them.
#include "str.h"

#include <stdarg.h>
#include <string.h>

namespace str {

Str fmt(Arena* arena, const char* format, ...)
{
    va_list args;
    va_start(args, format);

    // With no buffer, vsnprintf writes nothing and returns the length the
    // text would have. Measuring reads the arguments out of args, and a
    // va_list can only be read once, so the measuring gets a copy.
    va_list measure_args;
    va_copy(measure_args, args);
    i32 len = vsnprintf(nullptr, 0, format, measure_args);
    va_end(measure_args);
    ASSERT(len >= 0); // only a broken format fails

    // One more byte for the 0 vsnprintf writes at the end.
    char* data = arena::push_array<char>(arena, len + 1);
    vsnprintf(data, len + 1, format, args);
    va_end(args);

    return { .data = data, .len = len };
}

Str slice(Str s, i32 start, i32 end)
{
    ASSERT(0 <= start && start <= end && end <= s.len);
    return { .data = s.data + start, .len = end - start };
}

i32 find_last(Str s, char c)
{
    for (i32 i = s.len - 1; i >= 0; i--) {
        if (s.data[i] == c)
            return i;
    }
    return -1;
}

bool eq(Str a, Str b)
{
    if (a.len != b.len)
        return false;
    // An empty Str's data can be null, and memcmp mustn't be given a null
    // pointer, even with a length of 0. UBSan stops on it.
    if (a.len == 0)
        return true;
    return memcmp(a.data, b.data, a.len) == 0;
}

} // namespace str
