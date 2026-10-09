# Change log

Every change to [plan.md](plan.md) and [outline.md](outline.md), oldest first. The plan marks each changed decision with the chapter that changed it, and this file says why.

| Chapter | Change | Why |
|---|---|---|
| ch00 | Plan written from the design session on 2026-10-08 | Starting point. |
| ch01 | Added dialect details: `= {}` and designated initializers, C-style casts, how `ASSERT` stops, `i32` counts and `i64` sizes, `UPPER_SNAKE` constants, `-Wno-missing-field-initializers` | Writing `base.h` and its test program needed them, and the plan didn't cover them yet. |
| ch02 | Moved loading `game.so` with `dlopen` and the `GameApi` struct from chapter 06 to chapter 02 | A `game.so` that nothing loads would prove nothing. Chapter 06 keeps the reloading itself. |
| ch02 | Added the boundary and build details: `momo.h`, `-fvisibility=hidden`, `extern "C"`, `RTLD_NOW`, `$ORIGIN`, `--no-undefined`, the build folder layout, `compile_commands.json` | The Makefile and the first engine code needed them. You asked for `compile_commands.json` so clangd works. |
| ch03 | Added the test runner's design: one program per test file, `RUN`/`CHECK`/`CHECK_EQ`, linking tests against `libmomo.a`, how `make test` runs them | Plan §4.10 only said "a tiny test runner". |
| ch03 | The debug build now stops at the first sanitizer report (`-fno-sanitize-recover=all`) | Writing the tests showed that a test with signed overflow passed while UBSan only printed a warning. |
| ch03 | Added `min`, `max` and `clamp` to `base.h` | The runner needed real code to test, and chapters 04, 09 and 23 need these functions. |
| ch04 | Added the arena details: the `Arena` struct and functions, zeroing in push, aligning addresses, the sizes, who resets which arena, the accessors | Plan §4.2 only described the three arenas. |
| ch04 | The boundary became `momo.h` plus the module headers it includes, and `MOMO_API` moved to `base.h` | `arena.h` is the first engine module the game calls into. |
| ch04 | Allowed `arena::push<T>` and `push_array<T>` as templates | They're typed wrappers over one untyped function, clearer than a macro. |
| after ch04 | Moved the outline (§8) to `outline.md` and this change log (§10) to `changelog.md`. §8 stays as a pointer, so older links still work. | The plan was growing every chapter. Both are lists rather than decisions, and the plan reads faster without them. |
| after ch04 | Shortened the rows added in ch01–ch04 to the decision and its reason, and added that rule to the plan's introduction | Struct fields, function names, paths and sizes were already in the code and the chapters. Repeating them added several long rows to the plan every chapter. |
| ch05 | `str_fmt` became `str::fmt`, and the plan gained the `Str` details: an `i32` length, and a 0 after formatted text | The namespace rule in §3.4, and writing the code. |
| ch05 | Added the log: `LOG_INFO`, `LOG_WARN` and `LOG_ERROR` macros around `logger::print`, writing to stderr. The outline's `log::info` changed with it. | Only a macro can pass on the caller's `__FILE__` and `__LINE__`. `log` is a C library function, so it can't be a namespace. Output on stdout was lost when the game crashed. |
| ch05 | `i64` and `u64` are now `long long` and `unsigned long long` | Chapter 03's `(long long)` casts would otherwise have spread to every log line. |
