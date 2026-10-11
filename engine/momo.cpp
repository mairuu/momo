// momo.cpp: the engine's entry point. It reserves memory, loads game.so,
// opens the window, runs the loop, and reloads game.so whenever it changes.
// It also draws the debug overlay and saves captures (chapter 13), and hands
// each tick the pad (chapter 14).
#include "momo.h"
#include "input.h"
#include "options.h"
#include "platform.h"
#include "renderer.h"
#include "stats.h"
#include "timestep.h"

#include <stddef.h>

namespace momo {

// The engine never reloads, so its state can live in a file-scope variable.
// Like every static, it starts out zeroed.
struct Engine {
    Options options; // what the command line asked for (chapter 13)
    i32 exit_code;   // what run returns: 0, or 1 if a capture couldn't be saved

    Str game_path;  // the full path to game.so, in the engine's own arena
    void* game_lib; // the handle platform::open_library returned, or null
    i64 game_time;  // game.so's modification time when it was last loaded
    GameApi game;

    // The game's state, at the start of the permanent arena, and the
    // version and size it was made with.
    GameState* state;
    i32 state_version;
    i64 state_size;

    // How many ticks each frame runs (chapter 09), and what the loop has
    // done since it started, for the log and for --capture.
    Timestep timestep;
    i64 start_ns; // when the loop started, by platform::clock_ns
    i32 frame_count;
    i32 tick_count;
    bool quit_requested;

    // The pad, as it's been since the last tick: what went down and up,
    // which the next tick takes (chapter 14).
    Pad pad;

    // The debug overlay (chapter 13): whether it's shown, and its numbers.
    bool overlay_shown;
    FrameStats stats;

    u8* memory; // one block from the OS, split into the four arenas
    i64 memory_size;
    Arena permanent; // the game's three arenas
    Arena stage;
    Arena frame;
    Arena arena; // the engine's own, which the game can't reach
};

static Engine engine;

// Fixed sizes, so a game that needs more memory than this stops at an
// ASSERT instead of quietly growing.
constexpr i64 PERMANENT_SIZE = 16 * MB;
constexpr i64 STAGE_SIZE = 32 * MB;
constexpr i64 FRAME_SIZE = 8 * MB;
constexpr i64 ENGINE_SIZE = 8 * MB;

// The size the window asks for when it opens: three times the 256x240
// screen, in the display server's units. That's points on Wayland, so 6 times
// the screen in pixels on a display with a scale of 2, and pixels on X11. A
// tiling window manager picks a size of its own, and the renderer fits the
// screen into whatever size the window has (chapter 11).
constexpr i32 WINDOW_WIDTH = 3 * SCREEN_WIDTH;
constexpr i32 WINDOW_HEIGHT = 3 * SCREEN_HEIGHT;

// Reserves one block for all four arenas. The OS hands out zeroed pages,
// and only gives a page physical memory when it's first touched, so
// reserving more than the game uses costs almost nothing.
static bool init_memory()
{
    i64 size = PERMANENT_SIZE + STAGE_SIZE + FRAME_SIZE + ENGINE_SIZE;
    void* memory = platform::reserve_memory(size);
    if (!memory)
        return false; // already logged
    engine.memory = (u8*)memory;
    engine.memory_size = size;

    // The game's memory, permanent and stage, comes first and in one piece,
    // so chapter 21 can copy all of it, and none of the engine's, at once.
    u8* next = engine.memory;
    arena::init(&engine.permanent, next, PERMANENT_SIZE);
    next += PERMANENT_SIZE;
    arena::init(&engine.stage, next, STAGE_SIZE);
    next += STAGE_SIZE;
    arena::init(&engine.frame, next, FRAME_SIZE);
    next += FRAME_SIZE;
    arena::init(&engine.arena, next, ENGINE_SIZE);

    LOG_INFO("reserved %lld MB at %p", size / MB, memory);
    return true;
}

static void log_peak(const char* name, const Arena& arena)
{
    LOG_INFO("%-9s arena: at most %lld bytes used of %lld MB", name, arena.peak, arena.size / MB);
}

static void shutdown_memory()
{
    log_peak("permanent", engine.permanent);
    log_peak("stage", engine.stage);
    log_peak("frame", engine.frame);
    log_peak("engine", engine.arena);
    platform::release_memory(engine.memory, engine.memory_size);
}

// Works out where game.so is: in the executable's folder. The path goes in
// the engine's arena, since the engine needs it for the whole run.
static bool find_game()
{
    // ".../build/debug/" -> ".../build/debug/game.so"
    const char* folder = platform::executable_folder();
    if (!folder)
        return false; // already logged
    engine.game_path = str::fmt(&engine.arena, "%sgame.so", folder);
    return true;
}

// Opens game.so and fills in engine.game.
static bool load_game()
{
    // The time is read before opening, so if the file changes again in
    // between, the next check sees a newer time and loads it once more.
    // It's recorded even if loading fails, so a broken game.so is tried
    // once, not on every frame. str::fmt put a 0 after the path, so the
    // platform functions can take it.
    engine.game_time = platform::file_time(engine.game_path.data);

    engine.game_lib = platform::open_library(engine.game_path.data);
    if (!engine.game_lib)
        return false; // already logged, like every platform:: failure

    auto* get_game_api = (GameApiFn*)platform::find_function(engine.game_lib, "momo_game_api");
    if (!get_game_api) {
        platform::close_library(engine.game_lib);
        engine.game_lib = nullptr;
        return false;
    }

    engine.game = get_game_api();
    return true;
}

// Closes game.so. Its code, its constants and its globals are gone after
// this, and a pointer into them points at whatever is loaded there next,
// which is often the new game.so.
static void unload_game()
{
    platform::close_library(engine.game_lib);
    engine.game_lib = nullptr;
    engine.game = {};
}

// Starts the game from nothing: resets the game's memory, puts a new
// GameState at the start of the permanent arena, and calls the game's init.
// Everything the game pushes after this comes out zeroed again.
static void start_game()
{
    arena::reset(&engine.permanent);
    arena::reset(&engine.stage);

    // The engine can't see alignof(GameState). max_align_t is the most any
    // ordinary type needs, which is what malloc promises too.
    engine.state = (GameState*)arena::push_bytes(&engine.permanent, engine.game.state_size,
                                                 alignof(max_align_t));
    engine.state_version = engine.game.state_version;
    engine.state_size = engine.game.state_size;
    engine.game.init(engine.state);
}

// ---- The debug overlay and captures (chapter 13) ----------------------------

constexpr Color OVERLAY_TEXT_COLOR = renderer::rgb(0xffffff);
constexpr Color OVERLAY_DIM_COLOR = renderer::rgb(0x606060); // a button that isn't held
// Black, an eighth see-through.
constexpr Color OVERLAY_BACKDROP_COLOR = { .r = 0, .g = 0, .b = 0, .a = 224 };
constexpr i32 OVERLAY_MARGIN = 4;       // pixels of backdrop around the text
constexpr i32 OVERLAY_LINE_HEIGHT = 10; // 8 pixels of letters, then 2 between lines

// The overlay's names for the pad's buttons, in the order of enum Button.
constexpr Str BUTTON_NAMES[] = {
    STR("A"), STR("B"), STR("sel"), STR("start"), STR("^"),
    STR("v"), STR("<"), STR(">"),   STR("L"),     STR("R"),
};
static_assert(ARRAY_COUNT(BUTTON_NAMES) == BUTTON_COUNT, "a name for every button");

// One line of the overlay's table of arenas: the arena's name, and the most
// it has ever held, lined up under "peak".
static Str arena_line(Arena* text, const char* name, const Arena& arena)
{
    return str::fmt(text, "%-9s %9s", name, stats::format_size(text, arena.peak).data);
}

// Draws the overlay at the screen's top left, over the game: a few lines of
// white text on a dark box that the game shows through a little.
static void draw_overlay()
{
    // The text is formatted into memory on the stack, through an arena of its
    // own: an arena doesn't own the memory it hands out (chapter 04).
    // draw_text turns each char into a rectangle right away, so the text can
    // go when this function returns. str::fmt ends each piece with a 0, so
    // format_size's .data can go to a %s.
    u8 memory[1024];
    Arena text = {};
    arena::init(&text, memory, sizeof(memory));

    const FrameStats& frames = engine.stats;
    RenderStats drawn = renderer::last_frame_stats();
    Str lines[] = {
        str::fmt(&text, "%d fps, %d ticks/s", frames.frames_per_second, frames.ticks_per_second),
        str::fmt(&text, "%.2f ms of work a frame", (f64)frames.work_ns_per_frame / 1e6),
        str::fmt(&text, "%d rects, %d draw calls", drawn.rects, drawn.draw_calls),
        str::fmt(&text, "%-9s %9s", "arena", "peak"),
        arena_line(&text, "permanent", engine.permanent),
        arena_line(&text, "stage", engine.stage),
        arena_line(&text, "frame", engine.frame),
    };
    i32 longest = 0;
    for (Str line : lines)
        longest = max(longest, line.len);

    // The last line is the pad: the name of every button, with a space after
    // each one but the last.
    i32 pad_line_len = -1;
    for (Str name : BUTTON_NAMES)
        pad_line_len += name.len + 1;
    longest = max(longest, pad_line_len);

    // The box fits around the text, with the same margin on every side. The
    // last line has no gap under it, and a glyph's last column and last row
    // are blank already, so the box ends one pixel sooner.
    i32 line_count = ARRAY_COUNT(lines) + 1;
    i32 text_height = (line_count - 1) * OVERLAY_LINE_HEIGHT + GLYPH_SIZE;
    Rect box = {
        .x = 0,
        .y = 0,
        .w = 2 * OVERLAY_MARGIN + longest * GLYPH_SIZE - 1,
        .h = 2 * OVERLAY_MARGIN + text_height - 1,
    };

    // The overlay's batch is drawn after the game's, so everything here goes
    // on top of the game whatever its layer, in drawing order: the box, then
    // the text.
    renderer::begin_overlay();
    renderer::draw_rect(box, OVERLAY_BACKDROP_COLOR, 0);
    for (i32 i = 0; i < ARRAY_COUNT(lines); i++)
        renderer::draw_text(lines[i], OVERLAY_MARGIN, OVERLAY_MARGIN + i * OVERLAY_LINE_HEIGHT,
                            OVERLAY_TEXT_COLOR, 0);

    // The pad's line: a button's name is white while the button is held, and
    // gray while it isn't. It shows the pad as it is now, which the next tick
    // will see.
    i32 x = OVERLAY_MARGIN;
    i32 y = OVERLAY_MARGIN + ARRAY_COUNT(lines) * OVERLAY_LINE_HEIGHT;
    for (i32 i = 0; i < BUTTON_COUNT; i++) {
        bool held = (engine.pad.down & input::bit((Button)i)) != 0;
        Color color = held ? OVERLAY_TEXT_COLOR : OVERLAY_DIM_COLOR;
        renderer::draw_text(BUTTON_NAMES[i], x, y, color, 0);
        x += (BUTTON_NAMES[i].len + 1) * GLYPH_SIZE;
    }
}

// Saves the screen as a PNG file where --capture said, and ends the run.
static void save_capture()
{
    // 256 x 240 pixels of 3 bytes is 180 KB, which fits on the stack: the
    // main thread's is 8 MB on Linux.
    u8 pixels[SCREEN_WIDTH * SCREEN_HEIGHT * 3];
    renderer::read_screen(pixels);
    const char* path = engine.options.capture_path;
    if (platform::write_png(path, SCREEN_WIDTH, SCREEN_HEIGHT, pixels))
        LOG_INFO("saved the screen at tick %d as %s", engine.tick_count, path);
    else
        engine.exit_code = 1; // already logged
    engine.quit_requested = true;
}

// ---- Running the game -------------------------------------------------------

// Called once per frame: if game.so has changed since it was loaded, swaps
// in the new one. The game's state stays where it is, and the new code
// carries on with it, unless its layout changed.
static void reload_game_if_changed()
{
    // No file (in the middle of make clean, say) isn't a change: the old
    // code is still in memory and keeps running.
    i64 time = platform::file_time(engine.game_path.data);
    if (time == 0 || time == engine.game_time)
        return;

    // The old game.so has to be closed first. dlopen, under open_library,
    // finds libraries it has already loaded by their path, so opening the
    // same path again would only hand back the old one.
    if (engine.game_lib)
        unload_game();
    if (!load_game())
        return; // already logged: the game waits, its state untouched, for the next build

    if (engine.game.state_version == engine.state_version &&
        engine.game.state_size == engine.state_size) {
        LOG_INFO("reloaded %.*s", engine.game_path.len, engine.game_path.data);
        return;
    }

    LOG_WARN("GameState changed from version %d, %lld bytes, to version %d, %lld bytes: "
             "starting the game over",
             engine.state_version, engine.state_size, engine.game.state_version,
             engine.game.state_size);
    start_game();
}

i32 run(i32 argc, char** argv)
{
    // On a failure here, the process exits right away, and the OS takes
    // back its memory, closes its libraries and its window.
    if (!options::parse(argc, argv, &engine.options))
        return 1;
    engine.overlay_shown = engine.options.overlay;
    if (!init_memory())
        return 1;
    if (!find_game())
        return 1;
    if (!load_game())
        return 1;
    LOG_INFO("loaded %.*s", engine.game_path.len, engine.game_path.data);

    // The window and OpenGL start before the game does. The game's init
    // loads textures, and OpenGL needs a window before it can take them.
    if (!platform::init("momo", WINDOW_WIDTH, WINDOW_HEIGHT))
        return 1;
    if (!renderer::init(&engine.arena))
        return 1;

    start_game();
    engine.start_ns = platform::clock_ns();
    timestep::init(&engine.timestep, engine.start_ns);
    stats::init(&engine.stats, engine.start_ns);
    while (!engine.quit_requested) {
        i64 frame_start_ns = platform::clock_ns();
        platform::Events events = platform::handle_events(&engine.pad);
        if (events.quit)
            break; // the window was closed, or Ctrl+C
        if (events.backquote_pressed)
            engine.overlay_shown = !engine.overlay_shown;
        if (engine.options.capture_path)
            engine.pad = {}; // a capture takes no input, so every run saves the same picture
        reload_game_if_changed();
        arena::reset(&engine.frame); // last frame's memory is free again

        // One tick for every 1/60 s since the last frame: none, one or a few,
        // however fast the frames come. A game that failed to load runs
        // none, and waits. A capture runs none past its tick.
        i32 ticks = timestep::advance(&engine.timestep, frame_start_ns);
        if (engine.timestep.dropped_ns > 0)
            LOG_WARN("skipping %lld ms of game time: a frame runs at most %d ticks",
                     engine.timestep.dropped_ns / 1'000'000, MAX_TICKS_PER_FRAME);
        if (!engine.game_lib)
            ticks = 0;
        if (engine.options.capture_path)
            ticks = min(ticks, engine.options.capture_tick - engine.tick_count);
        for (i32 i = 0; i < ticks; i++) {
            // Each tick takes what the pad did since the last tick: a press in
            // a frame that ran no ticks waits for the next tick, and a frame
            // that runs three hands it to the first of them only.
            input::begin_tick(input::take_tick(&engine.pad));
            engine.game.update(engine.state);
            input::end_tick();
        }
        engine.tick_count += ticks;

        // The game draws its picture of where the ticks left it, and the
        // overlay goes on top.
        renderer::begin_frame();
        if (engine.game_lib)
            engine.game.render(engine.state);
        if (engine.overlay_shown)
            draw_overlay();
        renderer::end_frame();
        if (engine.options.capture_path && engine.tick_count == engine.options.capture_tick)
            save_capture();

        // The frame's work ends here. With vsync, the rest of it is waiting.
        i64 work_ns = platform::clock_ns() - frame_start_ns;
        platform::present(); // with vsync, waits for the display
        engine.frame_count++;
        stats::count_frame(&engine.stats, platform::clock_ns(), ticks, work_ns);
    }
    // SDL's clock starts over after platform::shutdown, so read it now.
    f64 seconds = (f64)(platform::clock_ns() - engine.start_ns) / 1e9;

    if (engine.game_lib) {
        engine.game.shutdown(engine.state);
        unload_game();
    }
    renderer::shutdown();
    platform::shutdown();

    LOG_INFO("quit after %.2f s: %d frames, %d ticks", seconds, engine.frame_count,
             engine.tick_count);
    shutdown_memory();
    return engine.exit_code;
}

void quit()
{
    engine.quit_requested = true;
}

Arena* permanent_arena()
{
    return &engine.permanent;
}

Arena* stage_arena()
{
    return &engine.stage;
}

Arena* frame_arena()
{
    return &engine.frame;
}

} // namespace momo
