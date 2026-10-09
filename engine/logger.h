// logger.h: momo's log, one line per message on stderr. Chapter 05 explains
// it.
#pragma once

#include "base.h"

// Each line starts with the file and line that logged it, like a compiler
// message, and a warning or an error says so:
//
//     game/game.cpp:9: init
//     engine/momo.cpp:101: error: .../game.so: cannot open shared object file
//
// The log adds the newline. Log an expected failure, such as a missing file,
// with LOG_ERROR and carry on. A bug is an ASSERT.
#define LOG_INFO(...)  logger::print(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  logger::print(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) logger::print(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

enum LogLevel : u8 {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
};

// Not "log": that's the C library's logarithm, and a namespace can't share a
// name with a function.
namespace logger {

// What the LOG_ macros call. format and what follows it work like printf's,
// and GCC checks them the same way.
MOMO_API void print(LogLevel level, const char* file, i32 line, const char* format, ...)
    __attribute__((format(printf, 4, 5)));

} // namespace logger
