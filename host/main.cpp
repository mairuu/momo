// main.cpp: the momo executable. All it does is hand control to the engine.
// Chapter 02 explains why the engine isn't the executable itself.
#include "momo.h"

int main()
{
    return momo::run();
}

// GCC defines __SANITIZE_ADDRESS__ when it builds with AddressSanitizer,
// which only the debug build does.
#ifdef __SANITIZE_ADDRESS__

// When momo exits, LeakSanitizer reports any memory from malloc that nothing
// points to anymore. momo's own memory comes from the arenas, so whatever it
// finds was allocated by a library. The graphics driver SDL loads keeps a
// little memory that only the driver's own variables point to, and when
// SDL_Quit unloads the driver, those variables go with it. Chapter 07 shows
// the report. Lines starting with "leak:" tell LeakSanitizer to skip a leak
// whose call stack passes through a function, file or library by that name.
//
// LeakSanitizer looks for this function by name, in the executable only.
// extern "C" keeps the name unmangled.
extern "C" const char* __lsan_default_suppressions()
{
    return "leak:libSDL3.so\n";
}

#endif
