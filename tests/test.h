// test.h: momo's test runner. Chapter 03 explains it.
//
// Every tests/*_test.cpp file is a program of its own. Its tests are static
// functions that call CHECK and CHECK_EQ, and its main runs them in order:
//
//     static void min_picks_the_smaller()
//     {
//         CHECK_EQ(min(3, 5), 3);
//     }
//
//     int main(int argc, char** argv)
//     {
//         test::init(argc, argv);
//         RUN(min_picks_the_smaller);
//         return test::report();
//     }
//
// A test function that main forgets to RUN is a static function nothing
// calls, so -Wunused-function (part of -Wall) warns about it.
//
// `make test` builds and runs every test program. Run one directly to see
// only its tests, or add a word to run only the tests whose names contain it:
// build/debug/tests/base_test clamp
#pragma once

#include "base.h"

namespace test {

// Reads the program's name and the optional word from the command line.
void init(int argc, char** argv);

// Runs one test and prints its name, then "ok" or "FAIL".
void run(const char* name, void (*fn)());

// Prints how many tests passed and failed. Returns the exit code for main:
// 0 if every test passed.
i32 report();

// Marks the running test as failed and prints why, starting with file:line.
// CHECK and CHECK_EQ call it. In gdb, `break test::fail` stops at every
// failed check.
void fail(const char* file, i32 line, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

} // namespace test

// Runs the test function fn, under its own name.
#define RUN(fn) test::run(#fn, fn)

// If cond is false, the test fails and returns: the rest of it is skipped,
// and the runner moves on to the next test.
#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            test::fail(__FILE__, __LINE__, "CHECK(%s) failed", #cond);        \
            return;                                                           \
        }                                                                     \
    } while (0)

// Like CHECK(a == b), but a failure also prints both values. For integers,
// enums and bools: each side is evaluated once and stored in an i64.
#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        i64 check_a_ = (a);                                                   \
        i64 check_b_ = (b);                                                   \
        if (check_a_ != check_b_) {                                           \
            test::fail(__FILE__, __LINE__,                                    \
                       "CHECK_EQ(%s, %s) failed: %lld != %lld", #a, #b,       \
                       check_a_, check_b_);                                   \
            return;                                                           \
        }                                                                     \
    } while (0)
