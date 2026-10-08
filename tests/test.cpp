// test.cpp: momo's test runner. Chapter 03 explains it.
#include "test.h"

#include <stdarg.h>
#include <string.h>

namespace test {

// A test program isn't game code and never reloads, so the runner keeps its
// state in a file-scope variable, like the engine does.
struct Runner {
    const char* program; // "base_test": argv[0] without its folder
    const char* filter;  // run only tests whose names contain this; null runs all
    bool failed;         // the running test has failed a check
    i32 passed_count;
    i32 failed_count;
};

static Runner runner;

void init(int argc, char** argv)
{
    const char* slash = strrchr(argv[0], '/');
    runner.program = slash ? slash + 1 : argv[0];
    runner.filter = argc > 1 ? argv[1] : nullptr;
    printf("%s\n", runner.program);
}

void run(const char* name, void (*fn)())
{
    ASSERT(runner.program); // main has to call test::init first
    if (runner.filter && !strstr(name, runner.filter))
        return;

    // The name goes out before the test runs. If the test crashes, the
    // crash's message ends up right after it, so you can tell which test it
    // was. fflush matters: text still waiting in stdout's buffer is lost
    // when a program crashes.
    printf("  %s ... ", name);
    fflush(stdout);

    runner.failed = false;
    fn();

    if (runner.failed) {
        runner.failed_count++;
    } else {
        runner.passed_count++;
        printf("ok\n");
    }
}

i32 report()
{
    if (runner.passed_count + runner.failed_count == 0) {
        printf("  no tests ran\n"); // most likely a typo in the filter
        return 1;
    }
    if (runner.failed_count > 0) {
        printf("  %d passed, %d failed\n", runner.passed_count, runner.failed_count);
        return 1;
    }
    printf("  %d passed\n", runner.passed_count);
    return 0;
}

void fail(const char* file, i32 line, const char* fmt, ...)
{
    // A test stops at its first failed check, but a check in a helper
    // function only returns from the helper, so a test can fail twice.
    if (!runner.failed)
        printf("FAIL\n");
    runner.failed = true;

    // file:line: comes first, in the same format as a compiler error.
    printf("%s:%d: ", file, line);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

} // namespace test
