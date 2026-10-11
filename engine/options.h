// options.h: what momo was asked to do on its command line. Chapter 13
// explains it.
#pragma once

#include "base.h"

// momo [--overlay] [--capture TICK FILE]
//
//   --overlay             start with the debug overlay shown. The ` key, left
//                         of 1, shows and hides it while momo runs.
//   --capture TICK FILE   run the game until it has run TICK ticks, save the
//                         screen as a PNG file at FILE, and quit. 0 saves the
//                         first picture, before any tick.
//
// All zeros is a run with neither.
struct Options {
    bool overlay;
    const char* capture_path; // null for no capture. Points into argv, which lasts the whole run.
    i32 capture_tick;
};

namespace options {

// Reads the arguments after the program's name, argv[1] to argv[argc - 1],
// into options. Returns false, after logging why and how momo is run, if
// they make no sense.
bool parse(i32 argc, const char* const* argv, Options* options);

} // namespace options
