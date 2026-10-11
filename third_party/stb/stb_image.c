/* stb_image.c: momo's one copy of stb_image's code. stb_image.h is a
 * header-only library: every file that includes it gets the declarations,
 * and the one file that defines STB_IMAGE_IMPLEMENTATION first gets the code
 * too. Chapter 12 explains the options.
 *
 * STBI_ONLY_PNG    decode PNG files and nothing else
 * STBI_NO_STDIO    no fopen: momo reads files through platform::read_file and
 *                  hands stb_image the bytes
 *
 * With only PNG, two helpers the other formats use are never called, and
 * -Wall warns about unused static functions. The warning is about code momo
 * doesn't own, so it's turned off for this file only. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO

#pragma GCC diagnostic ignored "-Wunused-function"

#include "stb_image.h"
