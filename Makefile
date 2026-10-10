# Makefile: builds momo and its tests. Chapters 02 and 03 explain every line.
#
#   make            debug build in build/debug/: no optimization, sanitizers
#   make release    optimized build in build/release/
#   make test       builds and runs the tests (chapter 03)
#   make clean      deletes both builds
#
# ASSERT stays on in both builds. Every build also writes
# compile_commands.json, which tells editors how each file is compiled.

CONFIG ?= debug
OUT    := build/$(CONFIG)

CXX := g++
CC  := gcc

# ---- Flags ------------------------------------------------------------------

# For every C++ file in every build
CXXFLAGS := -std=c++20 -Wall -Wextra -Wno-missing-field-initializers \
            -fno-exceptions -fno-rtti -Iengine

# For every C file. Only vendored libraries are written in C (chapter 08).
CFLAGS := -std=c11 -Wall -Wextra

# -fno-sanitize-recover=all makes the sanitizers stop the program at the
# first problem they find. UBSan would otherwise print a message and go on.
ifeq ($(CONFIG),debug)
    CONFIG_FLAGS := -O0 -g3 -fsanitize=address,undefined -fno-sanitize-recover=all
    LDFLAGS      += -fsanitize=address,undefined
else ifeq ($(CONFIG),release)
    CONFIG_FLAGS := -O2 -g
else
    $(error CONFIG must be debug or release, not "$(CONFIG)")
endif
CXXFLAGS += $(CONFIG_FLAGS)
CFLAGS   += $(CONFIG_FLAGS)

# For code that goes into a shared library: it must run at whatever address
# it's loaded at (-fPIC), and it exports only what MOMO_API marks.
LIB_CXXFLAGS := $(CXXFLAGS) -fPIC -fvisibility=hidden

# SDL3 is installed on the system, and pkg-config prints the flags to compile
# and link against it (chapter 07). Only the engine gets them: the game and
# the host never call SDL themselves.
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)

# glad, the OpenGL loader (chapter 08), is C code that goes into libmomo.so,
# so it's compiled like the engine's C++: -fPIC, and nothing exported. The
# engine finds its header as <glad/gl.h>.
GLAD_INCLUDE := -Ithird_party/glad/include
GLAD_CFLAGS  := $(CFLAGS) -fPIC -fvisibility=hidden $(GLAD_INCLUDE)

ENGINE_CXXFLAGS := $(LIB_CXXFLAGS) $(SDL_CFLAGS) $(GLAD_INCLUDE)

# For linking a shared library. --no-undefined makes calling a function that
# no library exports a link error, instead of an error when it's loaded.
LIB_LDFLAGS := $(LDFLAGS) -shared -Wl,--no-undefined

# For linking anything that calls the engine: link against libmomo.so, and
# at run time look for it in the folder this binary is in. The loader reads
# $ORIGIN as that folder. $$ is how Make writes a single $, and the quotes
# stop the shell from expanding $ORIGIN itself.
LINK_MOMO := -L$(OUT) -lmomo -Wl,-rpath,'$$ORIGIN'

# ---- Files ------------------------------------------------------------------

# Every .cpp in a folder belongs to that folder's binary, and its object file
# goes under build/<config>/obj/: engine/momo.cpp -> build/debug/obj/engine/momo.o
ENGINE_SRC := $(wildcard engine/*.cpp)
GAME_SRC   := $(wildcard game/*.cpp)
HOST_SRC   := $(wildcard host/*.cpp)

ENGINE_OBJ := $(ENGINE_SRC:%.cpp=$(OUT)/obj/%.o)
GAME_OBJ   := $(GAME_SRC:%.cpp=$(OUT)/obj/%.o)
HOST_OBJ   := $(HOST_SRC:%.cpp=$(OUT)/obj/%.o)

# Vendored C code that's linked into the engine. Each library in third_party/
# has its own layout, so its files are listed by name.
GLAD_SRC := third_party/glad/src/gl.c
GLAD_OBJ := $(GLAD_SRC:%.c=$(OUT)/obj/%.o)

# tests/test.cpp is the runner. Every tests/<name>_test.cpp is a test program
# of its own, built as build/<config>/tests/<name>_test.
TESTS_SRC  := $(wildcard tests/*.cpp)
TESTS_OBJ  := $(TESTS_SRC:%.cpp=$(OUT)/obj/%.o)
TEST_PROGS := $(patsubst tests/%.cpp,$(OUT)/tests/%,$(wildcard tests/*_test.cpp))

# ---- Targets ----------------------------------------------------------------

all: $(OUT)/momo $(OUT)/libmomo.so $(OUT)/game.so compile_commands.json

release:
	$(MAKE) CONFIG=release

clean:
	rm -rf build/debug build/release compile_commands.json

# Runs every test program, even after one fails, then fails if any did.
test: $(TEST_PROGS) compile_commands.json
	@failed=""; \
	 for t in $(TEST_PROGS); do $$t || failed="$$failed $$t"; done; \
	 if [ -n "$$failed" ]; then echo "failed:$$failed"; exit 1; fi

.PHONY: all release clean test

# ---- Linking ----------------------------------------------------------------

# Every rule below also depends on the Makefile, so changing a flag here
# rebuilds everything that uses it.

$(OUT)/libmomo.so: $(ENGINE_OBJ) $(GLAD_OBJ) Makefile
	$(CXX) $(LIB_LDFLAGS) $(ENGINE_OBJ) $(GLAD_OBJ) $(SDL_LIBS) -o $@

# The running engine reloads game.so as soon as it changes (chapter 06), so
# the new file has to appear all at once. The linker writes it under another
# name, and mv renames it into place, which replaces the old file in one step.
$(OUT)/game.so: $(GAME_OBJ) $(OUT)/libmomo.so Makefile
	$(CXX) $(LIB_LDFLAGS) $(GAME_OBJ) $(LINK_MOMO) -o $@.tmp
	mv $@.tmp $@

$(OUT)/momo: $(HOST_OBJ) $(OUT)/libmomo.so Makefile
	$(CXX) $(LDFLAGS) $(HOST_OBJ) $(LINK_MOMO) -o $@

# The engine again, as a static library: an archive of the same object files
# libmomo.so is linked from. Linking a program against an archive copies in
# only the object files that define something the program calls. Every
# function in them can be called, MOMO_API or not. It lives in obj/, so
# -lmomo never finds it instead of libmomo.so.
$(OUT)/obj/libmomo.a: $(ENGINE_OBJ) $(GLAD_OBJ) Makefile
	rm -f $@
	ar rcs $@ $(ENGINE_OBJ) $(GLAD_OBJ)

# One rule for every test program: its own object file, the runner, and
# whatever it uses from the engine. "targets: pattern: prerequisites" is a
# static pattern rule: % stands for the same name on both sides, but only for
# the targets listed before the first colon.
$(TEST_PROGS): $(OUT)/tests/%: $(OUT)/obj/tests/%.o $(OUT)/obj/tests/test.o $(OUT)/obj/libmomo.a Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $< $(OUT)/obj/tests/test.o $(OUT)/obj/libmomo.a -o $@

# ---- Compiling --------------------------------------------------------------

# $< is the first prerequisite (the .cpp) and $@ is the target (the .o).
#
# -MMD also writes a .d file next to each .o: a small makefile that lists the
# headers the .cpp included. -MP keeps the build working when a header is
# deleted. The -include line reads the .d files, so editing a header rebuilds
# every file that includes it.
$(OUT)/obj/engine/%.o: engine/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP -c $< -o $@

$(OUT)/obj/game/%.o: game/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(LIB_CXXFLAGS) -MMD -MP -c $< -o $@

$(OUT)/obj/host/%.o: host/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OUT)/obj/tests/%.o: tests/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OUT)/obj/third_party/glad/%.o: third_party/glad/%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(GLAD_CFLAGS) -MMD -MP -c $< -o $@

-include $(ENGINE_OBJ:.o=.d) $(GAME_OBJ:.o=.d) $(HOST_OBJ:.o=.d) $(TESTS_OBJ:.o=.d) \
         $(GLAD_OBJ:.o=.d)

# ---- compile_commands.json --------------------------------------------------

# clangd and other editor tools read this file to compile each .cpp the way
# the build does. It's rewritten, quietly, whenever this Makefile or a source
# file changes. It holds absolute paths, so it's generated and git ignores it.

# $(call compdb_entry,file,compiler and flags) is a shell command that prints
# one entry, a JSON object on one line.
compdb_entry = echo '{ "directory": "$(CURDIR)", "file": "$(1)", "command": "$(2) -c $(1)" }';

# JSON allows no comma after the last entry, so sed adds a comma to the end of
# every line except the last one ($!).
compile_commands.json: Makefile $(ENGINE_SRC) $(GAME_SRC) $(HOST_SRC) $(TESTS_SRC) $(GLAD_SRC)
	@{ echo '['; \
	   { $(foreach f,$(ENGINE_SRC),$(call compdb_entry,$(f),$(CXX) $(ENGINE_CXXFLAGS))) \
	     $(foreach f,$(GAME_SRC),$(call compdb_entry,$(f),$(CXX) $(LIB_CXXFLAGS))) \
	     $(foreach f,$(HOST_SRC) $(TESTS_SRC),$(call compdb_entry,$(f),$(CXX) $(CXXFLAGS))) \
	     $(foreach f,$(GLAD_SRC),$(call compdb_entry,$(f),$(CC) $(GLAD_CFLAGS))) } | sed '$$!s/$$/,/'; \
	   echo ']'; } > $@
