// options.cpp: momo's command line. Chapter 13 explains it.
#include "options.h"
#include "logger.h"

#include <string.h>

namespace options {

constexpr const char* USAGE = "usage: momo [--overlay] [--capture TICK FILE]";

// Reads text as a tick: one or more of the digits 0 to 9, and nothing else,
// so "-1", "1e3", "12x" and "" are all refused. A number bigger than an i32
// holds is refused too.
static bool parse_tick(const char* text, i32* tick)
{
    if (text[0] == 0)
        return false;
    i64 value = 0;
    for (const char* c = text; *c != 0; c++) {
        if (*c < '0' || *c > '9')
            return false;
        value = value * 10 + (*c - '0');
        if (value > INT32_MAX)
            return false;
    }
    *tick = (i32)value;
    return true;
}

bool parse(i32 argc, const char* const* argv, Options* options)
{
    *options = {};
    for (i32 i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (strcmp(arg, "--overlay") == 0) {
            options->overlay = true;
        } else if (strcmp(arg, "--capture") == 0) {
            // Two more arguments: argv[i + 1] and argv[i + 2].
            if (i + 2 >= argc) {
                LOG_ERROR("--capture needs a tick and a file\n%s", USAGE);
                return false;
            }
            if (!parse_tick(argv[i + 1], &options->capture_tick)) {
                LOG_ERROR("--capture's tick must be a whole number, 0 or more, not \"%s\"\n%s",
                          argv[i + 1], USAGE);
                return false;
            }
            options->capture_path = argv[i + 2];
            i += 2;
        } else {
            LOG_ERROR("unknown argument \"%s\"\n%s", arg, USAGE);
            return false;
        }
    }
    return true;
}

} // namespace options
