// logger.cpp: momo's log. Chapter 05 explains it.
#include "logger.h"

#include <stdarg.h>

namespace logger {

// What goes between "file:line: " and the message, indexed by LogLevel.
// These are GCC's words, so editors that read compiler output read the log
// the same way.
constexpr const char* LEVEL_LABELS[] = {
    "",          // LOG_LEVEL_INFO
    "warning: ", // LOG_LEVEL_WARN
    "error: ",   // LOG_LEVEL_ERROR
};

void print(LogLevel level, const char* file, i32 line, const char* format, ...)
{
    ASSERT(level < ARRAY_COUNT(LEVEL_LABELS));

    // stderr isn't buffered: each line is out before print returns, so a
    // crash right after it can't lose it. ASSERT writes to stderr too, so its
    // message lands in order with the log's.
    fprintf(stderr, "%s:%d: %s", file, line, LEVEL_LABELS[level]);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

} // namespace logger
