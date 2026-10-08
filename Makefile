# Makefile: builds momo's three binaries. Chapter 02 explains every line.
#
#   make            debug build in build/debug/: no optimization, sanitizers
#   make release    optimized build in build/release/
#   make clean      deletes both builds
#
# ASSERT stays on in both builds. Every build also writes
# compile_commands.json, which tells editors how each file is compiled.

CONFIG ?= debug
OUT    := build/$(CONFIG)

CXX := g++

# ---- Flags ------------------------------------------------------------------

# For every file in every build
CXXFLAGS := -std=c++20 -Wall -Wextra -Wno-missing-field-initializers \
            -fno-exceptions -fno-rtti -Iengine

ifeq ($(CONFIG),debug)
    CXXFLAGS += -O0 -g3 -fsanitize=address,undefined
    LDFLAGS  += -fsanitize=address,undefined
else ifeq ($(CONFIG),release)
    CXXFLAGS += -O2 -g
else
    $(error CONFIG must be debug or release, not "$(CONFIG)")
endif

# For code that goes into a shared library: it must run at whatever address
# it's loaded at (-fPIC), and it exports only what MOMO_API marks.
LIB_CXXFLAGS := $(CXXFLAGS) -fPIC -fvisibility=hidden

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

# ---- Targets ----------------------------------------------------------------

all: $(OUT)/momo $(OUT)/libmomo.so $(OUT)/game.so compile_commands.json

release:
	$(MAKE) CONFIG=release

clean:
	rm -rf build/debug build/release compile_commands.json

.PHONY: all release clean

# ---- Linking ----------------------------------------------------------------

# Every rule below also depends on the Makefile, so changing a flag here
# rebuilds everything that uses it.

$(OUT)/libmomo.so: $(ENGINE_OBJ) Makefile
	$(CXX) $(LIB_LDFLAGS) $(ENGINE_OBJ) -o $@

$(OUT)/game.so: $(GAME_OBJ) $(OUT)/libmomo.so Makefile
	$(CXX) $(LIB_LDFLAGS) $(GAME_OBJ) $(LINK_MOMO) -o $@

$(OUT)/momo: $(HOST_OBJ) $(OUT)/libmomo.so Makefile
	$(CXX) $(LDFLAGS) $(HOST_OBJ) $(LINK_MOMO) -o $@

# ---- Compiling --------------------------------------------------------------

# $< is the first prerequisite (the .cpp) and $@ is the target (the .o).
#
# -MMD also writes a .d file next to each .o: a small makefile that lists the
# headers the .cpp included. -MP keeps the build working when a header is
# deleted. The -include line reads the .d files, so editing a header rebuilds
# every file that includes it.
$(OUT)/obj/engine/%.o: engine/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(LIB_CXXFLAGS) -MMD -MP -c $< -o $@

$(OUT)/obj/game/%.o: game/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(LIB_CXXFLAGS) -MMD -MP -c $< -o $@

$(OUT)/obj/host/%.o: host/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(ENGINE_OBJ:.o=.d) $(GAME_OBJ:.o=.d) $(HOST_OBJ:.o=.d)

# ---- compile_commands.json --------------------------------------------------

# clangd and other editor tools read this file to compile each .cpp the way
# the build does. It's rewritten, quietly, whenever this Makefile or a source
# file changes. It holds absolute paths, so it's generated and git ignores it.

# $(call compdb_entry,file,flags) is a shell command that prints one entry, a
# JSON object on one line.
compdb_entry = echo '{ "directory": "$(CURDIR)", "file": "$(1)", "command": "$(CXX) $(2) -c $(1)" }';

# JSON allows no comma after the last entry, so sed adds a comma to the end of
# every line except the last one ($!).
compile_commands.json: Makefile $(ENGINE_SRC) $(GAME_SRC) $(HOST_SRC)
	@{ echo '['; \
	   { $(foreach f,$(ENGINE_SRC) $(GAME_SRC),$(call compdb_entry,$(f),$(LIB_CXXFLAGS))) \
	     $(foreach f,$(HOST_SRC),$(call compdb_entry,$(f),$(CXXFLAGS))) } | sed '$$!s/$$/,/'; \
	   echo ']'; } > $@
