#pragma once

// host-libretro: our libretro implementation.
//
// A LibretroHost owns one loaded core and everything the core was told or
// asked for. Lifecycle, in this order:
//
//   libretro_host_load_core()     dlopen, symbol check, set_environment, get_system_info, retro_init
//   libretro_host_load_game()     retro_load_game, retro_get_system_av_info
//     or libretro_host_load_game_special()   a subsystem: retro_load_game_special
//   libretro_host_set_input()     (each frame) what the frontend's controls say
//   libretro_host_run_frame()     (each frame) retro_run; the frame ends up in host->video,
//                                 its audio in host->audio
//   libretro_host_unload_game()   retro_unload_game
//   libretro_host_unload_core()   retro_deinit, dlclose
//
// Multi-instancing: libretro callbacks carry no userdata pointer, so the host
// keeps ONE thread_local "current instance" pointer. It is set right before
// every call into the core and cleared right after (see libretro_host.cpp).
// That is the only hidden global state in the host, by design.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "libretro.h"
#include "libretro_audio.h"
#include "libretro_core.h"
#include "libretro_extensions.h"
#include "libretro_input.h"
#include "libretro_options.h"
#include "libretro_subsystem.h"
#include "libretro_video.h"

constexpr size_t LIBRETRO_PATH_MAX = 4096;
// Rewind / replay events (LibretroHostEvent): the longest text answer and
// option (key + value) kept.
constexpr size_t LIBRETRO_TEXT_ANSWER_MAX = 256;
constexpr size_t LIBRETRO_OPTION_EVENT_MAX = 256;

// Savestate slots: any number, one file each (<game>.state<N>). How many a
// frontend offers is its own limit (Andrés, 2026-11-25: the host takes any
// slot, the frontend decides the range).

// RETRO_DEVICE_KEYBOARD key states are indexed by RETROK_* code.
constexpr unsigned LIBRETRO_KEY_COUNT = RETROK_LAST;

// A port's cursor: what the core reads as its mouse, pointer or lightgun
// (whichever it asks for; the frontend moves one cursor with any device).
struct LibretroCursorState
{
    bool    off_screen = false;   // outside the image (or unknown): a lightgun's reload aim
    int16_t x = 0;                // -0x7fff..0x7fff across the core's whole frame
    int16_t y = 0;
    int16_t mouse_dx = 0;         // mouse motion this frame
    int16_t mouse_dy = 0;
    uint16_t buttons = 0;         // bit = LibretroCursorButton
};

// LibretroCursorState::buttons bits.
enum class LibretroCursorButton
{
    left,          // mouse left, pointer touch, lightgun trigger
    right,
    middle,
    wheel_up,
    wheel_down,
    reload,        // lightgun: reload (fired off the screen)
    gun_aux_a,     // lightgun: its other buttons
    gun_aux_b,
    gun_aux_c,
    gun_start,
    gun_select,
};

// Input as the frontend sees it this frame, in RetroPad terms. The frontend
// fills this (keyboard, gamepads, mouse... mapped however it likes) and hands it
// over with libretro_host_set_input() before libretro_host_run_frame().
// What the frontend does to the core between frames that the core's state
// doesn't carry and its input doesn't say: a rewind / replay log keeps these
// next to the input (libretro_host_set_event_listener) and gives them back
// to the core in order (libretro_host_replay_event).
enum class LibretroHostEvent : uint8_t
{
    option,         // data: key '\0' value '\0'
    reset,          // no data
    keyboard,       // data: down (1), keycode (4), character (4), modifiers (2), little-endian
    text_answer,    // data: answered (1) then the text and '\0' (cancelled: just 0)
    state_loaded,   // a savestate replaced the game's state (no data); never replayed
};

// Called after the event reached the core. `data` lives only during the call.
typedef void (*LibretroHostEventListener)(void* userdata, LibretroHostEvent event, const uint8_t* data, uint32_t size);

struct LibretroInputState
{
    uint16_t joypad_buttons[LIBRETRO_MAX_PORTS] = {};   // bit N = RETRO_DEVICE_ID_JOYPAD_N
    int16_t  analog[LIBRETRO_MAX_PORTS][2][2]   = {};   // [port][left/right stick][x/y], -0x8000..0x7fff

    // Each port's cursor (mouse, pointer, lightgun). A lightgun's d-pad is
    // the port's RetroPad's.
    LibretroCursorState cursor[LIBRETRO_MAX_PORTS] = {};

    // Keyboard (RETRO_DEVICE_KEYBOARD, any port): 1 = key held, by RETROK_* code.
    // Cores that want typed text use the keyboard callback instead
    // (libretro_host_keyboard_event), which also carries the character.
    uint8_t  keys[LIBRETRO_KEY_COUNT] = {};
};

// What a core says about itself before it is started (libretro_host_peek_core).
// The canonical source: the core's own answers, not an info file.
constexpr size_t LIBRETRO_INFO_TEXT_MAX = 256;
constexpr size_t LIBRETRO_EXTENSIONS_MAX = 1024;

struct LibretroCoreInfo
{
    bool     usable = false;                          // false: see `error`
    char     error[LIBRETRO_INFO_TEXT_MAX] = {};
    bool     already_loaded = false;                  // refused: the library is loaded in this process (running?)

    char     name[LIBRETRO_INFO_TEXT_MAX]    = {};    // retro_system_info.library_name
    char     version[LIBRETRO_INFO_TEXT_MAX] = {};    // library_version
    char     extensions[LIBRETRO_EXTENSIONS_MAX] = {}; // valid_extensions as reported ("sfc|smc|..."), may be empty
    bool     need_fullpath    = false;
    bool     block_extract    = false;
    bool     supports_no_game = false;                // SET_SUPPORT_NO_GAME during retro_set_environment
    uint32_t option_count     = 0;                    // core options declared during retro_set_environment
    uint32_t options_api      = 0;                    // 2, 1, 0 (SET_VARIABLES), if option_count > 0
    unsigned api_version      = 0;                    // retro_api_version()
    LibretroSubsystems subsystems;                    // SET_SUBSYSTEM_INFO during retro_set_environment
};

// Counters for debug UIs and tests.
struct LibretroHostStats
{
    uint64_t frames_run          = 0;   // retro_run calls
    uint64_t video_frames        = 0;   // video_refresh with a new image
    uint64_t video_duplicates    = 0;   // video_refresh with NULL
    uint64_t video_errors        = 0;   // frames we couldn't store (bad format, HW frame, no memory)
    uint64_t audio_frames        = 0;   // stereo frames received (both audio callbacks)
    uint64_t audio_errors        = 0;   // audio callbacks whose samples we couldn't store (no memory)
    uint64_t input_polls         = 0;
};

// What GET_INPUT_DEVICE_CAPABILITIES answers: the devices LibretroInputState
// carries.
constexpr uint64_t LIBRETRO_INPUT_DEVICE_CAPABILITIES =
    (1ull << RETRO_DEVICE_JOYPAD) | (1ull << RETRO_DEVICE_MOUSE) | (1ull << RETRO_DEVICE_KEYBOARD) |
    (1ull << RETRO_DEVICE_LIGHTGUN) | (1ull << RETRO_DEVICE_ANALOG) | (1ull << RETRO_DEVICE_POINTER);

// Directories we hand to the core. Empty string = "not defined".
struct LibretroHostConfig
{
    const char* system_directory = "";
    const char* save_directory   = "";

    // Where the HOST keeps per-game files: SRAM/RTC in <saves>/<core>/<game>.srm
    // (.rtc) and savestates in <states>/<core>/<game>.state<N>. Empty = not kept.
    // (save_directory above is what the core is told for its OWN files.)
    const char* saves_directory  = "";
    const char* states_directory = "";

    // Highest core options version we tell the core about (GET_CORE_OPTIONS_VERSION):
    // 2 = v2, 1 = v1, 0 = don't answer at all, like a frontend from before
    // options existed. It only changes what the core THINKS we support (so its
    // v1/v0 fallbacks can be tested); every format is accepted regardless.
    uint32_t core_options_version = 2;

    // What GET_LANGUAGE answers: a RETRO_LANGUAGE_* value (the frontend's
    // language, so cores can show their own text in it).
    uint32_t language = 0;   // RETRO_LANGUAGE_ENGLISH

    // Video frames in the core's own pixel format (rows copied as they are)
    // instead of converted to XRGB8888 (libretro_video.h). For frontends that
    // upload each format to a matching GPU texture; others leave it off.
    bool native_pixel_formats = false;

    // What GET_TARGET_SAMPLE_RATE answers: the sample rate the frontend plays
    // at (its audio device's), so cores that can produce any rate (SameBoy)
    // make exactly that and nothing is resampled. 0 = not answered (the core
    // picks its own rate; SameBoy's own is ~2 MHz, too high to play).
    uint32_t target_sample_rate = 0;

    // Core option values the frontend kept for this core and game (its
    // options file): each applied when the core declares that option, before
    // it reads it (libretro_options_set_saved). Copied at load.
    const LibretroOptionValue* saved_options      = nullptr;
    uint32_t                   saved_option_count = 0;
};

constexpr uint32_t LIBRETRO_CORE_OPTIONS_VERSION_MAX = 2;

// The keyboard a core asked for (Multitap's text input extension,
// libretro_extensions.h), copied: the core's strings may be gone after the call.
// `serial` grows with every request, so a frontend sees a new one even when
// it looks like the last.
constexpr size_t LIBRETRO_TEXT_INPUT_MAX = 256;

struct LibretroTextInput
{
    uint32_t mode = RETRO_EXT_TEXT_INPUT_OFF;        // retro_ext_text_input_mode
    uint32_t type = RETRO_EXT_TEXT_INPUT_TYPE_TEXT;  // retro_ext_text_input_type
    char     prompt[LIBRETRO_TEXT_INPUT_MAX]  = {};
    char     initial[LIBRETRO_TEXT_INPUT_MAX] = {};
    uint32_t max_length = 0;                         // characters; 0 = the frontend's limit
    retro_ext_text_input_done_t done = nullptr;      // TEXT only
    uint32_t serial = 0;
};

// A core memory region the host keeps in a file (SRAM, RTC, a subsystem
// cart's save RAM). Worked out when a game loads.
constexpr size_t LIBRETRO_MEMORY_LABEL_MAX = 160;

struct LibretroMemoryFile
{
    unsigned type = 0;                              // retro_get_memory_data(type)
    char     label[LIBRETRO_MEMORY_LABEL_MAX] = {}; // for the log: "SRAM", "Game Boy ROM .sav"
    char     path[LIBRETRO_PATH_MAX] = {};
};

// One content file of a subsystem start (libretro_host_load_game_special).
// Kept until retro_unload_game(): the core may keep pointing into it.
struct LibretroSpecialContent
{
    char     path[LIBRETRO_PATH_MAX] = {};   // empty: an optional file left out
    uint8_t* data = nullptr;                 // when the core doesn't need a path
    size_t   size = 0;
};

struct LibretroHost
{
    LibretroCore core = {};

    // Our own copies: the core may keep the pointers we give it, so these
    // must live as long as the core is loaded.
    char core_path[LIBRETRO_PATH_MAX]        = {};
    char system_directory[LIBRETRO_PATH_MAX] = {};
    char save_directory[LIBRETRO_PATH_MAX]   = {};
    char saves_directory[LIBRETRO_PATH_MAX]  = {};
    char states_directory[LIBRETRO_PATH_MAX] = {};
    char content_path[LIBRETRO_PATH_MAX]     = {};

    // Content loaded into memory (when the core doesn't need a path).
    // Kept until retro_unload_game(): the core may keep pointing into it.
    uint8_t* content_data = nullptr;
    size_t   content_size = 0;

    // A subsystem start (libretro_host_load_game_special): its ident and its
    // content files. Empty ident = a normal start. content_path above is then
    // the first file given.
    char                                subsystem_ident[LIBRETRO_SUBSYSTEM_IDENT_MAX] = {};
    std::vector<LibretroSpecialContent> special_content;

    // The name per-game files use (SRAM, RTC, savestates): the content file's
    // name without extension, the core's name without content, or for a
    // subsystem the names of its files joined with " + " ("SGB + Tetris").
    char game_name[LIBRETRO_PATH_MAX] = {};

    // The memory regions kept in files for this game (set when it loads).
    std::vector<LibretroMemoryFile> memory_files;

    bool core_initialized = false;   // retro_init() ran
    bool game_loaded      = false;   // retro_load_game() succeeded

    // Filled from the core.
    retro_system_info    system_info = {};
    retro_system_av_info av_info     = {};

    // Video, audio and input exchanged with the core (see libretro_video.h, libretro_audio.h).
    LibretroVideoFrame  video = {};
    LibretroAudioBuffer audio = {};   // this frame's audio only: cleared by every run_frame
    LibretroInputState  input = {};
    LibretroHostStats  stats = {};

    // Core options (libretro_options.h). Declared by the core in retro_set_environment,
    // kept until the core is unloaded. Frontends read this list and change values
    // with libretro_host_set_option().
    LibretroOptions options              = {};
    uint32_t        core_options_version = 2;   // copied from the config at load
    uint32_t        language = 0;               // copied from the config at load (GET_LANGUAGE)
    bool            native_pixel_formats = false;   // copied from the config at load (libretro_video.h)
    uint32_t        target_sample_rate = 0;         // copied from the config at load (GET_TARGET_SAMPLE_RATE)

    // What the core said about its input (libretro_input.h), and the device the
    // frontend plugged into each port (libretro_host_set_port_device).
    LibretroInputInfo input_info = {};

    // The core's subsystems (libretro_subsystem.h), from SET_SUBSYSTEM_INFO.
    LibretroSubsystems subsystems;
    unsigned          port_devices[LIBRETRO_MAX_PORTS] = {RETRO_DEVICE_JOYPAD, RETRO_DEVICE_JOYPAD,
                                                          RETRO_DEVICE_JOYPAD, RETRO_DEVICE_JOYPAD};

    // Rumble the core asked for (GET_RUMBLE_INTERFACE), per port: strong and
    // weak motor, 0..0xffff. The frontend plays it on the port's controller.
    uint16_t rumble[LIBRETRO_MAX_PORTS][2] = {};

    // Set by the core with SET_KEYBOARD_CALLBACK; nullptr = it doesn't want key events.
    retro_keyboard_event_t keyboard_callback = nullptr;

    // What the core asked for through the environment callback.
    retro_pixel_format pixel_format       = RETRO_PIXEL_FORMAT_0RGB1555;   // libretro's default
    bool               supports_no_game   = false;
    bool               shutdown_requested = false;
    bool               fast_forwarding    = false;   // what GET_FASTFORWARDING answers (libretro_host_set_fast_forward)
    bool               peeking            = false;   // libretro_host_peek_core: environment calls logged at debug level only
    unsigned           performance_level  = 0;

    // Multitap's run-ahead extension (libretro_extensions.h): a core with its
    // own run-ahead lets the frontend drive it instead of a core option.
    unsigned           core_run_ahead_max = 0;   // the most the core does; 0 = it didn't offer

    // Savestates in memory, for rollback (netplay) and run-ahead done by the
    // frontend: what the core says about its states, frames run without
    // output, and what kind of save the core is told this is.
    uint64_t                serialization_quirks = 0;   // RETRO_SERIALIZATION_QUIRK_* (SET_SERIALIZATION_QUIRKS)
    LibretroHostEventListener event_listener = nullptr;  // libretro_host_set_event_listener
    void*                     event_userdata = nullptr;
    bool                    hidden = false;             // libretro_host_set_hidden: no video, no audio
    retro_savestate_context savestate_context = RETRO_SAVESTATE_CONTEXT_NORMAL;
    unsigned           run_ahead_frames   = 0;   // what the frontend wants (libretro_host_set_run_ahead)

    // Multitap's text input extension: the keyboard the core wants now
    // (OFF when none). Frontends read it; a TEXT request is answered with
    // libretro_host_text_input_answer().
    LibretroTextInput  text_input = {};

    // Environment statistics (for logging and the debug UI later).
    // Indexed by the command number without the EXPERIMENTAL/PRIVATE bits.
    uint32_t environment_call_counts[256] = {};
    uint32_t extension_call_counts[256] = {};   // Multitap's private calls, by their low byte
};

bool libretro_host_load_core(LibretroHost* host, const char* core_path, const LibretroHostConfig* config);

// content_path == nullptr starts the core without content (needs SET_SUPPORT_NO_GAME).
bool libretro_host_load_game(LibretroHost* host, const char* content_path);

// Starts subsystem `ident` (one of host->subsystems) with its content files:
// `paths[i]` for the subsystem's roms[i], `path_count` = its number of
// files. nullptr or "" leaves an optional file out (the core gets an empty
// retro_game_info); a required one can't be. Each file's memory regions are
// kept as <saves>/<core>/<that file's name>.<extension>, so a cart's save
// follows the cart; savestates use game_name.
bool libretro_host_load_game_special(LibretroHost* host, const char* ident, const char* const* paths, uint32_t path_count);

// Replaces the input the core will see on its next input_state calls.
void libretro_host_set_input(LibretroHost* host, const LibretroInputState* input);

// Plugs `device` (RETRO_DEVICE_JOYPAD, or a type the core listed in
// input_info for that port) into `port`. Kept, and passed to the core with
// retro_set_controller_port_device now if a game is loaded, else right after
// the game loads. The core may send new input descriptors in reply. False
// for a port past LIBRETRO_MAX_PORTS.
bool libretro_host_set_port_device(LibretroHost* host, unsigned port, unsigned device);

// Tells the core whether the frontend is fast-forwarding (GET_FASTFORWARDING).
// The frontend runs the extra frames itself; this is only what the core is told
// (some cores skip work while fast-forwarding).
void libretro_host_set_fast_forward(LibretroHost* host, bool fast_forwarding);

// Writes the core's save RAM (and RTC, if any) to the data directory now.
// Also done automatically when the game is unloaded; call this too every so
// often so a crash loses little. Returns false if anything failed (logged).
bool libretro_host_save_memory(LibretroHost* host);

// Savestates: retro_serialize into <data>/states/<core>/<game>.state<slot>,
// and back with retro_unserialize. Any slot number. Call between frames.
// Return false if the core can't (serialize size 0), there's no data
// directory, or a file operation failed; the reason is logged.
bool libretro_host_save_state(LibretroHost* host, uint32_t slot);
bool libretro_host_load_state(LibretroHost* host, uint32_t slot);

// The file a slot uses (whether or not it exists yet). False if there's no data directory.
bool libretro_host_state_path(const LibretroHost* host, uint32_t slot, char* out, size_t out_size);

// Forwards one key event to the core's keyboard callback, if it registered
// one (SET_KEYBOARD_CALLBACK). keycode = RETROK_*, character = the UTF-32
// character typed (0 if none), modifiers = RETROKMOD_* bits. Typed text can
// come as key-down events with keycode RETROK_UNKNOWN and only a character.
// Call outside retro_run (between frames). Returns false if the core has no callback.
// --- Savestates in memory (rollback, the frontend's own run-ahead) ---------
// The size the core needs now (0: no game, or no savestates).
size_t libretro_host_state_size(LibretroHost* host);
// The game's state into `state`, resized to the size written (its memory only
// grows): use state->size(), not a size asked before. False if the core can't.
bool libretro_host_serialize(LibretroHost* host, std::vector<uint8_t>* state);
bool libretro_host_unserialize(LibretroHost* host, const uint8_t* state, size_t size);
// A savestate the frontend kept in memory loaded as the player's own load
// (UNDO LOAD STATE): like libretro_host_load_state, the listener hears
// state_loaded (the rewind history jumps there). False if the core refused it.
bool libretro_host_load_state_memory(LibretroHost* host, const uint8_t* state, size_t size);
// The same into memory the caller holds (a rewind slot): `size` must be at
// least libretro_host_state_size(). No allocation.
bool libretro_host_serialize_to(LibretroHost* host, uint8_t* data, size_t size);
// Every option change, reset, keyboard event, text answer and savestate
// load from now on is told to `listener` (nullptr: none). libretro_host_unserialize
// (the frontend's own memory states) isn't an event.
void libretro_host_set_event_listener(LibretroHost* host, LibretroHostEventListener listener, void* userdata);
// Gives a recorded event back to the core (option, reset, keyboard, text
// answer; state_loaded is ignored). The listener hears it again. False if
// the data is malformed or the core refused it.
bool libretro_host_replay_event(LibretroHost* host, LibretroHostEvent event, const uint8_t* data, uint32_t size);
// Frames run while hidden make no picture and no sound (GET_AUDIO_VIDEO_ENABLE
// answers 0; what the core sends anyway is dropped): the frames a rollback
// or run-ahead runs again before the one shown.
void libretro_host_set_hidden(LibretroHost* host, bool hidden);
// What GET_SAVESTATE_CONTEXT answers (NORMAL, RUNAHEAD_SAME_INSTANCE,
// ROLLBACK_NETPLAY).
void libretro_host_set_savestate_context(LibretroHost* host, retro_savestate_context context);
// One of the core's memory areas (RETRO_MEMORY_SYSTEM_RAM...), or false.
bool libretro_host_memory(LibretroHost* host, unsigned id, const uint8_t** data, size_t* size);

// Run-ahead frames the frontend wants (its RUN-AHEAD setting). Applied by a
// core that does its own run-ahead and offered it (libretro_extensions.h); the
// core never gets more than it said it does.
void libretro_host_set_run_ahead(LibretroHost* host, unsigned frames);
// What's applied now: 0 if the core has no run-ahead of its own.
unsigned libretro_host_run_ahead_applied(const LibretroHost* host);

bool libretro_host_keyboard_event(LibretroHost* host, bool down, unsigned keycode, uint32_t character, uint16_t modifiers);

// Answers the core's TEXT request (host->text_input) with what was typed
// (UTF-8), or nullptr if the user cancelled; the request is then OFF. Call
// between frames. Does nothing if no TEXT request is pending.
void libretro_host_text_input_answer(LibretroHost* host, const char* text);

// Changes a core option. The core sees it on its next GET_VARIABLE_UPDATE /
// GET_VARIABLE (usually during the next retro_run). Fails (logged) for an
// unknown key or a value that isn't one of the option's values.
bool libretro_host_set_option(LibretroHost* host, const char* key, const char* value);

// Runs one frame (retro_run). Needs a loaded game. Returns false if nothing
// could run (no game, or the core asked to shut down). Afterwards host->audio
// holds exactly the audio this frame produced; play or drop it before the next call.
bool libretro_host_run_frame(LibretroHost* host);

// retro_reset: the core's "reset button". Does nothing if no game is loaded.
void libretro_host_reset(LibretroHost* host);

// Both are safe to call when nothing is loaded.
void libretro_host_unload_game(LibretroHost* host);
void libretro_host_unload_core(LibretroHost* host);

// Loads the core only far enough to ask it about itself: retro_set_environment
// (catches SET_SUPPORT_NO_GAME, core options, ...) and retro_get_system_info,
// never retro_init, then unloads it. Same approach as RetroArch's system info
// probe. Returns false if the file isn't a usable core (info->error says why).
// Uses its own temporary host, so other cores can keep running. A core whose
// library is already loaded in this process (running in another host, however
// its path is spelled) is refused (info->already_loaded): peeking would run
// retro_set_environment a second time on the live core.
bool libretro_host_peek_core(const char* core_path, LibretroCoreInfo* info);

// A core's short name, as save files and settings use it: "snes9x" from
// ".../snes9x_libretro.so". False if it doesn't fit in `out`.
bool libretro_core_short_name(const char* core_path, char* out, size_t out_size);

// True if a core with this info can start `content_path`: its extension is in
// the core's list (case-insensitive; an empty list accepts anything), or, for
// no content (nullptr or ""), if the core supports running without content.
bool libretro_core_info_accepts(const LibretroCoreInfo* info, const char* content_path);

// The rumble callback handed to cores (GET_RUMBLE_INTERFACE); it writes
// into the current host's `rumble`. For libretro_environment.cpp.
retro_set_rumble_state_t libretro_host_rumble_callback();

// The instance the core is currently running in, or nullptr outside core calls.
LibretroHost* libretro_host_current();
