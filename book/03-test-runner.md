# 03 · A Test Runner

> **Goal:** run `make test` and watch momo's first tests pass. Then break them on purpose in three ways (a wrong answer, an `ASSERT`, and undefined behavior), see each one reported with its file and line, and find a failing test in gdb.

## The problem

Some of momo's code is pure logic: numbers in, numbers out, with no window, no GPU and no clock. Chapter 04's arenas hand out memory by moving an offset forward. Chapter 15's fixed-point math multiplies numbers that count 1/256ths of a pixel. Chapter 18's collision moves an entity one pixel at a time until it touches a wall.

When code like that is wrong, the game shows you a symptom, not the bug. The player stands one pixel inside a wall. A jump comes out a little short. Something breaks only when you hit a wall's corner while falling at full speed. You can find the cause by playing until it happens and then stepping through it in gdb, but that's slow, and after every change that might have broken it again, you have to play it again.

A **test** calls the code directly, with inputs you choose, and checks what comes back. It runs in milliseconds, it tries the awkward cases on purpose instead of hoping the game stumbles into them, and it runs again after every change. [Plan §4.10](plan.md#410-debug-tools-and-tests) decides which code gets tests: pure logic, meaning math, fixed-point, containers and collision. Drawing, input and how the game feels get checked by running the game, and from chapter 13 on, by screenshots.

Tests need a **test runner**: something that runs every test, says which ones failed and where, and makes the build fail if any did. C++ has popular ones: GoogleTest, Catch2, doctest. Each is thousands of lines that lean on templates and macros, which is exactly what the dialect avoids. This chapter writes momo's own. It's about 90 lines.

## Design

### What a test looks like

A test is a function. It calls the code being tested and checks the results:

```cpp
static void clamp_moves_values_outside_to_the_nearest_end()
{
    CHECK_EQ(clamp(-3, 0, 10), 0);
    CHECK_EQ(clamp(11, 0, 10), 10);
}
```

`CHECK(cond)` fails the test if `cond` is false. `CHECK_EQ(a, b)` checks that `a == b`, and when it fails it prints both values, which `CHECK` can't do. `CHECK(x == 10) failed` doesn't tell you what `x` was. `CHECK_EQ(x, 10) failed: 11 != 10` does.

A failed check prints its file and line in the format compiler errors use, the same as `ASSERT` ([chapter 01](01-dialect.md)). Then it ends that test, because later checks in a test often only make sense if the earlier ones passed. The runner moves on to the next test, so one run tells you about every failing test, not only the first.

### How the runner finds the tests

The runner has to know every test function. There are three common ways to tell it:

| Way | Used by | The catch |
|---|---|---|
| Each test registers itself. `TEST(name) { ... }` expands to the function plus a global variable whose initializer adds the function to a list, before `main` starts. | GoogleTest, Catch2, doctest | Code runs at a declaration, where you can't see it. The dialect rules that out ([chapter 01](01-dialect.md)). |
| A script finds them. The build greps the test files for `TEST(` and writes the list into a generated file. | Some C projects | An extra build step that "parses" C++ with grep, and a generated file you have to know about |
| **Each test file lists its tests in its own `main`** | momo | You have to add every test to the list by hand. Forget one, and it never runs. |

momo uses the last one, and its catch has a fix that costs nothing. Test functions are `static`, which means nothing outside their own file can call them. A `static` function that nothing calls is almost always a mistake, so `-Wall` warns about it (`-Wunused-function`). Forget to list a test, and the build warns you.

### One program per test file

Each `tests/<name>_test.cpp` is a program of its own, with its own `main`:

```cpp
int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(min_and_max_pick_one_argument);
    RUN(clamp_moves_values_outside_to_the_nearest_end);
    return test::report();
}
```

The alternative is one program that runs the tests from every file. It would have one `main`, so it would need a way to collect tests from other files, and that brings back the registration problem above. Separate programs have more going for them:

- **A crash stops only one program.** A test that fails an `ASSERT` or makes a sanitizer report kills the program it's in. Nothing in the runner can survive that. `make test` then carries on with the next program.
- **Each program links only the engine code it tests** (next section).
- **You can run one test file directly**, or only some of its tests, or run it under gdb.

The cost is five lines of `main` per file, and one more link step per file.

### How tests reach engine code

This chapter's tests only test a header. Chapter 04's will call functions in `engine/arena.cpp`, which is compiled into `libmomo.so`. How does a test program get that code?

| Option | The catch |
|---|---|
| Link against `libmomo.so`, like the game does | The test can only call `MOMO_API` functions, and plenty of engine code worth testing isn't exported |
| Link every engine object file into every test | From chapter 07 on, the engine's platform code calls SDL, and from chapter 08, OpenGL. Every test program would need both, even the arena test. |
| `#include "../engine/arena.cpp"` in the test | Each test compiles its own copy of the engine code, and if `arena.cpp` needs `log.cpp`, the test has to include that too |
| **Link against `libmomo.a`, a static library of the engine's object files** | One more file to build |

A static library, also called an **archive** (`.a`), is a bundle of object files. Linking a program against an archive copies in only the object files that define something the program uses, plus whatever those use in turn, and leaves the rest out. So the arena test gets `arena.o` and none of the platform code. Hidden visibility doesn't get in the way either. `-fvisibility=hidden` only decides what a shared library offers to other binaries once it's loaded. Inside a single link, object files can call each other's functions regardless, so a test can call every function in the object files it gets, `MOMO_API` or not. The archive is built from the same object files as `libmomo.so`, so the tests run the exact compiled code the engine does.

Chapter 02 ruled out linking the engine statically, because the game would then use a second copy of it. A test program never loads `game.so`, so its copy is the only one.

There's one limit. A `static` function, like `load_game` in `engine/momo.cpp`, can only be called from its own file, archive or not. Test it through the functions that call it.

### Tests run under the sanitizers

`make test` builds the debug configuration unless you say otherwise, so the tests run with AddressSanitizer and UndefinedBehaviorSanitizer, the same as the game. That's a big part of what they're worth. An out-of-bounds write in arena code shows up in a test, at the line that did it, even if every check in the test passes.

There's a catch. When UBSan finds undefined behavior, say a signed integer that overflowed, it prints a message and lets the program carry on. A test whose arithmetic overflowed, wrapped around and happened to land on the right answer prints the message, then `ok`, and `make test` succeeds. The message scrolls away, and what you remember is that the tests passed.

That's not how momo treats bugs. A bug stops the program where it happens ([plan §3.3](plan.md#33-errors-and-strings)). So the debug build now adds `-fno-sanitize-recover=all`, which makes every sanitizer stop the program at its first report, like an `ASSERT` does. ASan already stopped at its first report, so in practice the flag changes UBSan. It applies to the game as well as the tests. Run it shows the difference.

### Something to test

There's almost no pure logic in momo yet. `base.h` has `ARRAY_COUNT`, and that's worked out while compiling, so `static_assert` checks it better than a test could. So this chapter adds three small functions to `base.h` that the next chapters need: `min`, `max` and `clamp`. Chapter 04 tracks an arena's peak usage with `max`, chapter 09 caps the ticks per frame with `min`, and chapter 23 keeps the camera inside a room with `clamp`.

C programs usually write `min` as a macro, which has a famous trap:

```c
#define MIN(a, b) ((a) < (b) ? (a) : (b))

MIN(i++, j)   // when i is the smaller one, i++ runs twice
```

The other C way is a function per type: `min_i32`, `min_i64`, `min_f32`, and so on. A template is one definition that works for every type and evaluates each argument once, and math is one of the two places the dialect allows templates ([plan §3.1](plan.md#31-rules)). `min(T a, T b)` has one `T` for both arguments, so they must have the same type. `min(size, 4096)` with an `i64` size doesn't compile, because `4096` is an `int`, and nothing gets converted behind your back (exercise 4).

Tests this simple almost can't fail. They're here so the runner has something real to run. The tests that earn their keep start in the next chapter.

## The code

### Writing tests: `tests/test.h`

The header has the runner's four functions and three macros. The functions:

`tests/test.h:28`

```cpp
namespace test {

void init(int argc, char** argv);
void run(const char* name, void (*fn)());
i32 report();

void fail(const char* file, i32 line, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

} // namespace test
```

`init` reads the command line, `run` runs one test, and `report` prints the totals and returns `main`'s exit code. `fail` is what a failed check calls. It takes a format string like `printf`, and the attribute tells GCC so: argument 3 is a `printf` format, and the arguments from 4 on are the values it formats. GCC then checks every call with `-Wformat`, as it does for `printf` itself. Chapter 05 does the same for `str_fmt`.

The macros:

`tests/test.h:49`

```cpp
#define RUN(fn) test::run(#fn, fn)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            test::fail(__FILE__, __LINE__, "CHECK(%s) failed", #cond);        \
            return;                                                           \
        }                                                                     \
    } while (0)
```

`RUN` passes the test function along with its name. `#fn` turns the name into a string, so `RUN(min_and_max_pick_one_argument)` becomes `test::run("min_and_max_pick_one_argument", min_and_max_pick_one_argument)`.

`CHECK` has the same shape as `ASSERT` in chapter 01: `do { } while (0)` to make it one statement, `#cond` for the condition's text, and `__FILE__` and `__LINE__` for where it is. It differs in two ways. It calls `test::fail` instead of trapping, and then it `return`s, which ends the test function. A `return` hidden inside a macro is the kind of thing the dialect is wary of. Here it's acceptable: `CHECK` only ever appears in test functions, it does one documented thing, and a test can't continue usefully once a check has failed.

The condition's text goes to `fail` as an argument for `%s`. Pasting it into the format string would break on `CHECK(x % 2 == 0)`, because `printf` would read that `%` as the start of a format.

`tests/test.h:63`

```cpp
#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        i64 check_a_ = (a);                                                   \
        i64 check_b_ = (b);                                                   \
        if (check_a_ != check_b_) {                                           \
            test::fail(__FILE__, __LINE__,                                    \
                       "CHECK_EQ(%s, %s) failed: %lld != %lld", #a, #b,       \
                       (long long)check_a_, (long long)check_b_);             \
            return;                                                           \
        }                                                                     \
    } while (0)
```

`CHECK_EQ` stores each side in an `i64` first. That evaluates each one exactly once, which avoids the `MIN(i++, j)` trap, and gives `fail` two numbers it can print. Integers, enums and bools all convert to `i64`. A pointer doesn't, so passing one is a compile error. A float does convert, quietly dropping everything after the decimal point, which is why floats use `CHECK` for now (exercise 2).

The `(long long)` casts are about portability. An `i64` is a `long` on Linux and a `long long` on Windows, and `printf` needs a format that matches. Casting to `long long` makes `%lld` right on both. Chapter 05 meets the same problem in momo's own string formatting.

### The runner: `tests/test.cpp`

`tests/test.cpp:11`

```cpp
struct Runner {
    const char* program; // "base_test": argv[0] without its folder
    const char* filter;  // run only tests whose names contain this; null runs all
    bool failed;         // the running test has failed a check
    i32 passed_count;
    i32 failed_count;
};

static Runner runner;
```

The runner's state is a file-scope variable, like the engine's in chapter 02. A test program isn't game code and never reloads, so that's safe.

`tests/test.cpp:29`

```cpp
void run(const char* name, void (*fn)())
{
    ASSERT(runner.program); // main has to call test::init first
    if (runner.filter && !strstr(name, runner.filter))
        return;

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
```

If you gave the program a word on its command line, `init` stored it as the filter, and `strstr` skips every test whose name doesn't contain it.

The test's name goes out *before* the test runs, so if the test crashes, the crash's message lands right after the name, and you can see which test it was. That only works because of `fflush`. The C library doesn't write `printf`'s text straight away. It collects it in a buffer: when stdout is a terminal, until the end of the line, and when it's a pipe or a file, until a few kilobytes have piled up. When a program crashes, whatever was still in the buffer is lost. `fflush(stdout)` writes it out now. Error messages from `ASSERT` and the sanitizers go to stderr, which has no buffer, so they appear immediately.

`run` doesn't need to know how a test failed. A failed check calls `fail`, which sets `runner.failed`:

`tests/test.cpp:67`

```cpp
void fail(const char* file, i32 line, const char* fmt, ...)
{
    if (!runner.failed)
        printf("FAIL\n");
    runner.failed = true;

    printf("%s:%d: ", file, line);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}
```

`FAIL` finishes the line `run` started, then the message goes on a line of its own, starting with `file:line:` so editors can jump to it. The `va_` lines are C's way of passing `fail`'s extra arguments on to `vprintf`, the version of `printf` that takes them as a list.

The `if` handles one case. `CHECK`'s `return` leaves the function the check is in, and that isn't always the test. If a test calls a helper function and a check fails inside the helper, the test carries on, and it can fail a second time. `FAIL` is printed once either way.

`report` (`tests/test.cpp:53`) prints `5 passed`, or `4 passed, 1 failed`, and returns 1 if anything failed. It also returns 1 if no test ran at all, which usually means a typo in the filter word.

### The first tests: `tests/base_test.cpp`

`tests/base_test.cpp:4`

```cpp
// ARRAY_COUNT is worked out while compiling, so the compiler can check it.
// Prefer static_assert for anything it can check; tests are for the rest.
constexpr i32 SEVEN_ZEROS[7] = {};
static_assert(ARRAY_COUNT(SEVEN_ZEROS) == 7);

static void min_and_max_pick_one_argument()
{
    CHECK_EQ(min(3, 5), 3);
    CHECK_EQ(min(5, 3), 3);
    CHECK_EQ(max(3, 5), 5);
    // ...
}
```

The file holds five tests. Each one checks one behavior and is named after it, so a failure's name already tells you what broke. Every test function is `static`, and the `main` at `tests/base_test.cpp:53` runs all five in order. A test of floats shows `CHECK` instead of `CHECK_EQ`:

`tests/base_test.cpp:29`

```cpp
static void min_and_max_work_on_floats()
{
    // CHECK_EQ is only for integers, so floats use CHECK. 1.5 and 2.5 are
    // exact in binary, so comparing them with == is safe.
    CHECK(min(2.5f, 1.5f) == 1.5f);
    CHECK(max(2.5f, 1.5f) == 2.5f);
}
```

### `min`, `max` and `clamp`: `engine/base.h`

`engine/base.h:58`

```cpp
template <typename T>
T min(T a, T b)
{
    return a < b ? a : b;
}

// ...max is the same with >...

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
```

`template <typename T>` makes each of these a recipe rather than a function. When the compiler sees `min(3, 5)`, it works out that `T` is `int` and writes the function `min<int>`. `min(big, (i64)1)` gets a separate `min<long>`. `clamp` with `lo` above `hi` is a mistake at the call, so it's an `ASSERT`, not a quiet guess about what the caller meant.

### The Makefile

**Sanitizers stop at the first report.** `Makefile:22`

```make
# -fno-sanitize-recover=all makes the sanitizers stop the program at the
# first problem they find. UBSan would otherwise print a message and go on.
ifeq ($(CONFIG),debug)
    CXXFLAGS += -O0 -g3 -fsanitize=address,undefined -fno-sanitize-recover=all
```

It's a compile flag, so it goes in `CXXFLAGS` only: it changes the checks the compiler inserts into each function.

**The files.** `Makefile:61`

```make
TESTS_SRC  := $(wildcard tests/*.cpp)
TESTS_OBJ  := $(TESTS_SRC:%.cpp=$(OUT)/obj/%.o)
TEST_PROGS := $(patsubst tests/%.cpp,$(OUT)/tests/%,$(wildcard tests/*_test.cpp))
```

`TESTS_SRC` is every `.cpp` in `tests/`: the runner and the test files. `TEST_PROGS` names one program per test file. `patsubst` is the function form of the `:%.cpp=...` substitution from chapter 02, needed here because that shorthand only works on a variable, not on what `wildcard` returns. It turns `tests/base_test.cpp` into `build/debug/tests/base_test`. Like the other folders, a new test file is picked up without touching the Makefile.

**The archive.** `Makefile:102`

```make
$(OUT)/obj/libmomo.a: $(ENGINE_OBJ) Makefile
	rm -f $@
	ar rcs $@ $(ENGINE_OBJ)
```

`ar` is the archive tool. `r` puts the files into the archive, `c` creates it without a warning that it didn't exist yet, and `s` writes an index of every symbol in it, which the linker uses to find which object file defines what. `ar r` never takes files out, so the archive is deleted first. Otherwise an engine file you deleted would stay in the archive.

It lives in `obj/` and not next to `libmomo.so` to keep it out of `-lmomo`'s way. Given a folder with both, the linker picks the `.so`, so it would work, but it's one less rule to have to know.

**The test programs.** `Makefile:110`

```make
$(TEST_PROGS): $(OUT)/tests/%: $(OUT)/obj/tests/%.o $(OUT)/obj/tests/test.o $(OUT)/obj/libmomo.a Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $< $(OUT)/obj/tests/test.o $(OUT)/obj/libmomo.a -o $@
```

This is a **static pattern rule**, which has three parts: the targets it applies to (`$(TEST_PROGS)`), a pattern that matches each of them (`$(OUT)/tests/%`), and the prerequisites, where `%` is whatever the pattern matched. For `build/debug/tests/base_test`, `%` is `base_test`, so it needs `base_test.o`, the runner's `test.o` and the archive.

Why not an ordinary pattern rule like the compile rules use? Make treats a file it built only because a chain of pattern rules needed it, and that the Makefile never names, as *intermediate*, and deletes it when the build is done. With an ordinary pattern rule here, every build of a test program would end with `rm build/debug/obj/tests/base_test.o`. A static pattern rule names its targets and prerequisites outright, so nothing is intermediate.

The order on the link line matters. The linker reads its inputs left to right, and when it reaches an archive it takes only the object files that define something it's still missing. So the archive goes last, after the files that call into it. Put it first, and the linker hasn't seen any calls yet, takes nothing, and later fails with `undefined reference` (exercise 1). Test programs link with plain `LDFLAGS` and no rpath, since they don't use `libmomo.so` at all.

**Running them.** `Makefile:76`

```make
test: $(TEST_PROGS) compile_commands.json
	@failed=""; \
	 for t in $(TEST_PROGS); do $$t || failed="$$failed $$t"; done; \
	 if [ -n "$$failed" ]; then echo "failed:$$failed"; exit 1; fi
```

The recipe is one shell command spread over three lines. It runs each test program, and when one exits with anything but 0, adds its name to `failed` (`$$` is Make's way of writing a single `$`). Then, if anything failed, it prints the list and exits with 1, which tells make the recipe failed. It doesn't stop at the first failing program, for the same reason the runner doesn't stop at the first failing test. The `@` hides the loop itself, so you see only what the tests print.

`test` also depends on `compile_commands.json`, so a new test file reaches your editor even if all you ran was `make test`. A test is compiled with plain `CXXFLAGS`, like the host, because it's an executable (`Makefile:134`), and the list for `compile_commands.json` now includes `tests/` (`Makefile:152`).

## Run it

### All the tests

```sh
make test
```

The flags are shortened to `...` again:

```
g++ ... -c tests/base_test.cpp -o build/debug/obj/tests/base_test.o
g++ ... -c tests/test.cpp -o build/debug/obj/tests/test.o
g++ ... -fPIC -fvisibility=hidden -MMD -MP -c engine/momo.cpp -o build/debug/obj/engine/momo.o
rm -f build/debug/obj/libmomo.a
ar rcs build/debug/obj/libmomo.a build/debug/obj/engine/momo.o
g++ -fsanitize=address,undefined build/debug/obj/tests/base_test.o build/debug/obj/tests/test.o build/debug/obj/libmomo.a -o build/debug/tests/base_test
base_test
  min_and_max_pick_one_argument ... ok
  min_and_max_keep_the_type ... ok
  min_and_max_work_on_floats ... ok
  clamp_leaves_values_inside_alone ... ok
  clamp_moves_values_outside_to_the_nearest_end ... ok
  5 passed
```

The test file and the runner compiled, the engine compiled and archived, one link, and the five tests. `compile_commands.json` was rewritten too, quietly. Run `make test` again and nothing is rebuilt, but the tests run anyway, because `test` is a phony target: it never exists as a file, so make never considers it done.

### One program, some tests

A test program is an ordinary executable. Give it a word, and only tests whose names contain that word run:

```
$ ./build/debug/tests/base_test clamp
base_test
  clamp_leaves_values_inside_alone ... ok
  clamp_moves_values_outside_to_the_nearest_end ... ok
  2 passed
$ ./build/debug/tests/base_test clmap
base_test
  no tests ran
```

The release build runs the same tests with `-O2` and no sanitizers:

```sh
make CONFIG=release test
```

`CONFIG` on the command line, as in chapter 02. The programs go in `build/release/tests/`.

### Break it: a wrong answer

Change the expected value at `tests/base_test.cpp:12` from `3` to `5`, so it reads `CHECK_EQ(min(5, 3), 5);`, and run `make test`. After the lines that rebuild `base_test`:

```
base_test
  min_and_max_pick_one_argument ... FAIL
tests/base_test.cpp:12: CHECK_EQ(min(5, 3), 5) failed: 3 != 5
  min_and_max_keep_the_type ... ok
  min_and_max_work_on_floats ... ok
  clamp_leaves_values_inside_alone ... ok
  clamp_moves_values_outside_to_the_nearest_end ... ok
  4 passed, 1 failed
failed: build/debug/tests/base_test
make: *** [Makefile:77: test] Error 1
```

The failing test stopped at line 12 and printed both values. The other four still ran. `failed:` comes from the Makefile's loop, and the last line is make reporting that the recipe failed.

Here, the values are enough to see the problem. When they aren't, gdb can stop the test at the moment it fails. `test::fail` runs for every failed check, so put a breakpoint on it. The word `min` limits the run to the tests about `min`:

```
$ gdb -q --args build/debug/tests/base_test min
(gdb) break test::fail
Breakpoint 1 at 0x4308: file tests/test.cpp, line 68.
(gdb) run
base_test
  min_and_max_pick_one_argument ... 
Breakpoint 1, test::fail (file=0x555555559140 "tests/base_test.cpp", line=12, fmt=0x5555555590e0 "CHECK_EQ(%s, %s) failed: %lld != %lld") at tests/test.cpp:68
68	{
(gdb) up
#1  0x0000555555557376 in min_and_max_pick_one_argument () at tests/base_test.cpp:12
12	    CHECK_EQ(min(5, 3), 5);
(gdb) info locals
check_a_ = 3
check_b_ = 5
(gdb) print min<int>(5, 3)
$1 = 3
(gdb) quit
```

`up` moves from `fail` to the test that called it, where every local variable is still there to inspect. To call a template function from gdb, name the version the compiler made: `min<int>`, not `min`. Your addresses will differ.

Put the line back with `git checkout tests/base_test.cpp`.

### Break it: an ASSERT

Add a new test file. The Makefile finds it on its own:

```sh
cat > tests/scratch_test.cpp <<'EOF'
// scratch_test.cpp: tests that go wrong on purpose. Delete it afterwards.
#include "test.h"

static void clamp_with_the_ends_swapped()
{
    CHECK_EQ(clamp(5, 10, 0), 5);
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(clamp_with_the_ends_swapped);
    return test::report();
}
EOF
make test
```

After the build:

```
base_test
  min_and_max_pick_one_argument ... ok
  min_and_max_keep_the_type ... ok
  min_and_max_work_on_floats ... ok
  clamp_leaves_values_inside_alone ... ok
  clamp_moves_values_outside_to_the_nearest_end ... ok
  5 passed
scratch_test
  clamp_with_the_ends_swapped ... engine/base.h:74: ASSERT(lo <= hi) failed
failed: build/debug/tests/scratch_test
```

`base_test` ran in full. `scratch_test` printed its test's name, then the `ASSERT` stopped it in the middle of the line. There's no `FAIL` and no totals: the program was killed, so the runner never got a chance. The test's name came out first because of the `fflush`.

The `ASSERT` points at `engine/base.h:74`, inside `clamp`, which every caller shares. To find the caller, use the routine from chapter 01:

```
$ gdb -q --args build/debug/tests/scratch_test
(gdb) run
scratch_test
  clamp_with_the_ends_swapped ... engine/base.h:74: ASSERT(lo <= hi) failed

Program received signal SIGILL, Illegal instruction.
0x0000555555556408 in clamp<int> (x=5, lo=10, hi=0) at engine/base.h:74
74	    ASSERT(lo <= hi);
(gdb) bt
#0  0x0000555555556408 in clamp<int> (x=5, lo=10, hi=0) at engine/base.h:74
#1  0x00005555555562a5 in clamp_with_the_ends_swapped () at tests/scratch_test.cpp:6
#2  0x000055555555673d in test::run (name=0x555555557140 "clamp_with_the_ends_swapped", fn=0x555555556289 <clamp_with_the_ends_swapped()>) at tests/test.cpp:43
#3  0x000055555555633b in main (argc=1, argv=0x7fffffffe418) at tests/scratch_test.cpp:12
(gdb) quit
```

Frame `#1` is the test, at the line that passed the swapped ends. gdb names the function `clamp<int>`, the version the compiler made for `int`.

### Break it: undefined behavior

Replace the scratch test with one whose arithmetic overflows:

```sh
cat > tests/scratch_test.cpp <<'EOF'
// scratch_test.cpp: tests that go wrong on purpose. Delete it afterwards.
#include "test.h"

static void there_and_back_again()
{
    i32 x = 2147483600; // close to the largest i32, 2147483647
    x += 100;
    x -= 100;
    CHECK_EQ(x, 2147483600);
}

int main(int argc, char** argv)
{
    test::init(argc, argv);
    RUN(there_and_back_again);
    return test::report();
}
EOF
make test
```

```
scratch_test
  there_and_back_again ... tests/scratch_test.cpp:7:7: runtime error: signed integer overflow: 2147483600 + 100 cannot be represented in type 'int'
failed: build/debug/tests/scratch_test
```

`x += 100` doesn't fit in an `i32`. Overflowing a signed integer is undefined behavior in C++, and UBSan stopped the program at that line, column 7.

Now see what this chapter's flag changed. Compile the same test by hand, with the sanitizers but without `-fno-sanitize-recover=all`, and run it:

```
$ g++ -std=c++20 -Iengine -O0 -g3 -fsanitize=address,undefined tests/scratch_test.cpp tests/test.cpp -o build/recover_test
$ ./build/recover_test
recover_test
  there_and_back_again ... tests/scratch_test.cpp:7:7: runtime error: signed integer overflow: 2147483600 + 100 cannot be represented in type 'int'
tests/scratch_test.cpp:8:7: runtime error: signed integer overflow: -2147483596 - 100 cannot be represented in type 'int'
ok
  1 passed
```

UBSan saw both overflows and let the program go on. In practice the CPU wraps around: `x` went past the largest `i32` to a large negative number, then wrapped back to exactly where it started. So the check passed, the program exited with 0, and `make test` would have reported success. Chapter 15's fixed-point multiply is just the kind of code where an overflow like this hides. Delete the extra binary with `rm build/recover_test`.

To stop gdb at a UBSan report, `break __ubsan_on_report`, a function UBSan calls for every report. `bt` then shows a few frames inside `libubsan.so` above your code, and `frame N` with the number of the first frame in your code takes you there.

### Break it: forget a test

Delete the line `RUN(there_and_back_again);` from `tests/scratch_test.cpp` and run `make test`:

```
tests/scratch_test.cpp:4:13: warning: ‘void there_and_back_again()’ defined but not used [-Wunused-function]
    4 | static void there_and_back_again()
      |             ^~~~~~~~~~~~~~~~~~~~
```

and later:

```
scratch_test
  no tests ran
failed: build/debug/tests/scratch_test
```

The compiler noticed the forgotten test, and the runner noticed a program that ran nothing. Now delete the scratch file:

```sh
rm tests/scratch_test.cpp
```

Its program is still in `build/debug/tests/`, but `make test` only runs programs that have a test file, so the old one just sits there until `make clean`.

### Look inside

`base_test` was linked against the archive, so how much of the engine is in it?

```
$ nm -C build/debug/tests/base_test | grep momo::
$
```

None. `base_test` only uses `base.h`, so the linker found nothing in `libmomo.a` that it needed. Exercise 1 makes a test call the engine and looks again.

### Your editor

`compile_commands.json` now lists the files in `tests/` too. Restart the language server (`:LspRestart`) and open `tests/base_test.cpp`: `CHECK_EQ` and `min` resolve, and hovering over `min` shows its comment from `base.h`. clangd compiles with `-Wall` from the same flags, so if you delete a `RUN` line, the unused-function warning shows up in the editor before you even build.

## Recap

momo has tests now, and every chapter from here can add more.

```
momo/
  Makefile                  + make test, -fno-sanitize-recover=all
  engine/
    base.h                  + min, max, clamp
  tests/
    test.h                  RUN, CHECK, CHECK_EQ, and the runner's functions
    test.cpp                the runner: init, run, report, fail
    base_test.cpp      →    build/<config>/tests/base_test
  build/debug/, build/release/
    tests/                  one program per test file
    obj/libmomo.a           the engine's object files, for tests to link
```

- A test is a `static` function that calls `CHECK` and `CHECK_EQ`. A failed check prints `file:line` and ends the test, and the runner moves on.
- Each test file is its own program, and its `main` lists its tests with `RUN`. A test left off the list is an unused `static` function, which `-Wall` warns about.
- A crash, whether an `ASSERT` or a sanitizer report, stops one program. The test's name is printed first, so you can tell which test it was.
- Tests link the engine as a static archive, so each gets only the engine code it uses, `MOMO_API` or not.
- `make test` runs every test program under the sanitizers, and fails if any program did. UB now stops the program, in tests and in the game.
- In gdb: `break test::fail` for a failed check, `bt` for a crash.

The plan records the test design in [§4.10](plan.md#410-debug-tools-and-tests), the new flag in [§7](plan.md#7-build-dependencies-repo), and `min`/`max`/`clamp` in [§3.1](plan.md#31-rules). The outline guessed the runner at 50 lines. It came out at about 90, mostly because of `CHECK_EQ` and the filter, and the outline says so now.

Chapter 04 builds the arenas, momo's memory, and writes their tests as it goes.

## Exercises

Undo each change before the next exercise: `git checkout -- Makefile engine tests` puts those back, and `rm` any test file you added.

1. **Call the engine from a test.** Add `#include "momo.h"` and a test that checks `CHECK_EQ(momo::frame_index(), 0)` to `base_test.cpp`, and run `make test`. Now run `nm -C build/debug/tests/base_test | grep momo::`. You called one function. What else came along, and why? Then move `$(OUT)/obj/libmomo.a` in front of `$<` on the link line (`Makefile:112`) and run `make test`. Read the error.
2. **A float in `CHECK_EQ`.** Add `CHECK_EQ(min(2.5f, 1.5f), 1.25f);` to a test. The expected value is wrong. Does it compile? Does the test fail? Work out what `check_a_` and `check_b_` hold.
3. **UB in release.** Bring back the `there_and_back_again` test and run `make CONFIG=release test`. What happens, and why do momo's tests run in the debug build by default?
4. **Mixed types.** In a test, write `i64 size = 100;` and `CHECK_EQ(min(size, 4096), 100);`. Read the error: what two types did the compiler deduce for `T`? Fix it with a cast.
5. **Let the tests decide.** Rewrite the body of `clamp` as a single line with `min` and `max` (keep the `ASSERT`). There are two ways to nest them: `max(lo, min(x, hi))` and `min(lo, max(x, hi))`. Try both. Which tests fail for the wrong one, and what do they print?
