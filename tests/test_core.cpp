// A minimal libretro core used to test host-libretro. Not an emulator.
//
// It behaves like a typical real core during startup:
//   - calls the environment from inside retro_set_environment (log interface,
//     SET_SUPPORT_NO_GAME, SET_VARIABLES) like many cores do,
//   - asks for directories and a pixel format in retro_init,
//   - accepts content from memory and checks it arrived intact,
//   - reports a 320x240 @ 60 fps, 48 kHz AV info,
//   - declares its input like a console core: two ports (a joypad and a
//     six-button pad, a subclass), input descriptors that change with the
//     port's device, rumble; retro_run reports what GET_INPUT_MAX_USERS and
//     GET_INPUT_DEVICE_CAPABILITIES answered through port 0's rumble (strong =
//     max users, weak = the low 16 bits of the capabilities), so a test can see it.
//
// Build variants (CMake):
//   test_core                 the normal core
//   test_core_missing_symbol  built with TEST_CORE_OMIT_RUN: no retro_run export,
//                             so the host must refuse to load it.
//
// Global state is normal for a libretro core (the API has no instance
// pointer), so this file uses file-local statics on purpose.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libretro.h"
#include "libretro_extensions.h"

static retro_environment_t g_environment = nullptr;
static retro_log_printf_t  g_log         = nullptr;

// Input (see the header comment).
constexpr unsigned TEST_CORE_DEVICE_SIX_BUTTON = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0);
static retro_set_rumble_state_t g_rumble       = nullptr;
static unsigned                 g_max_users    = 0;
static uint64_t                 g_capabilities = 0;
static unsigned                 g_port0_device = RETRO_DEVICE_JOYPAD;

// The controls port 0 has with its current device (three for the joypad,
// five for the six-button pad), plus one on port 1.
static void send_input_descriptors()
{
    static const retro_input_descriptor joypad[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Jump" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Run" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Pause" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Player 2 Jump" },
        { 0, 0, 0, 0, nullptr },
    };
    static const retro_input_descriptor six_button[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Low Kick" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Mid Kick" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Low Punch" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Mid Punch" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Pause" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Player 2 Jump" },
        { 0, 0, 0, 0, nullptr },
    };
    const retro_input_descriptor* list = g_port0_device == TEST_CORE_DEVICE_SIX_BUTTON ? six_button : joypad;
    g_environment(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, const_cast<retro_input_descriptor*>(list));
}

static void core_log(retro_log_level level, const char* format, ...)
{
    if (g_log == nullptr)
    {
        return;
    }
    char message[512] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    g_log(level, "%s", message);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void retro_set_environment(retro_environment_t callback)
{
    g_environment = callback;

    retro_log_callback log_callback = {};
    if (g_environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_callback))
    {
        g_log = log_callback.log;
    }

    bool no_game = true;
    g_environment(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);

    // Old-style core options. The host doesn't implement options yet, so this
    // should be logged as "NOT handled" and the core must cope.
    static const retro_variable variables[] = {
        { "test_core_speed", "Speed; normal|fast" },
        { nullptr, nullptr },
    };
    g_environment(RETRO_ENVIRONMENT_SET_VARIABLES, const_cast<retro_variable*>(variables));

    static const retro_controller_description port0_types[] = {
        { "Test Joypad", RETRO_DEVICE_JOYPAD },
        { "Test Six Button Pad", TEST_CORE_DEVICE_SIX_BUTTON },
    };
    static const retro_controller_description port1_types[] = {
        { "Test Joypad", RETRO_DEVICE_JOYPAD },
    };
    static const retro_controller_info controllers[] = {
        { port0_types, 2 },
        { port1_types, 1 },
        { nullptr, 0 },
    };
    g_environment(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, const_cast<retro_controller_info*>(controllers));
}

// A tiny game for the host's savestates: a frame counter, kept as the
// "system RAM" (RETRO_MEMORY_SYSTEM_RAM), saved and loaded as the state, and
// drawn as a 4x2 picture when the frontend wants video.
static retro_video_refresh_t g_video = nullptr;
static uint32_t g_frame = 0;
static uint8_t g_ram[16] = {};
#if !defined(TEST_CORE_OMIT_RUN)
static int g_context_last = -1;   // what retro_run last logged
#endif

static void ram_from_frame(void)
{
    for (uint32_t i = 0; i < sizeof(g_ram); i++)
    {
        g_ram[i] = static_cast<uint8_t>((g_frame >> (8 * (i % 4))) + i * 7);
    }
}

void retro_set_video_refresh(retro_video_refresh_t callback)            { g_video = callback; }
void retro_set_audio_sample(retro_audio_sample_t callback)              { (void)callback; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t callback)  { (void)callback; }
void retro_set_input_poll(retro_input_poll_t callback)                  { (void)callback; }
void retro_set_input_state(retro_input_state_t callback)                { (void)callback; }

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_get_system_info(retro_system_info* info)
{
    info->library_name     = "multitap test core";
    info->library_version  = "1";
    info->valid_extensions = "bin|txt";
    info->need_fullpath    = false;
    info->block_extract    = false;
}

static bool g_run_ahead_driven = false;   // the frontend drives our "internal run-ahead" (libretro_extensions.h)
#if !defined(TEST_CORE_OMIT_RUN)
static unsigned g_run_ahead_last = 0xFFFFFFFFu;   // what retro_run last logged
#endif

void retro_init(void)
{
    // Its states are this machine's u32 as is: endian-dependent.
    uint64_t quirks = RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT;
    g_environment(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &quirks);
    g_frame = 0;
    ram_from_frame();
    // A core with its own run-ahead, up to 3 frames, offers it to the frontend.
    unsigned most = 3;
    g_run_ahead_driven = g_environment(RETRO_ENVIRONMENT_EXT_SET_RUN_AHEAD, &most);
    core_log(RETRO_LOG_INFO, "run-ahead offered: %s\n", g_run_ahead_driven ? "the frontend drives it" : "not taken");

    const char* system_directory = nullptr;
    g_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_directory);
    core_log(RETRO_LOG_INFO, "system directory: %s\n", system_directory != nullptr ? system_directory : "(none)");

    // The rate the frontend plays at, if it says (cores like SameBoy produce that rate).
    unsigned target_rate = 0;
    if (g_environment(RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE, &target_rate))
    {
        core_log(RETRO_LOG_INFO, "target sample rate: %u\n", target_rate);
    }
    else
    {
        core_log(RETRO_LOG_INFO, "target sample rate: not answered\n");
    }

    retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!g_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format))
    {
        core_log(RETRO_LOG_ERROR, "XRGB8888 refused\n");
    }

    // An unsupported pixel format value must be refused.
    retro_pixel_format bogus = static_cast<retro_pixel_format>(77);
    if (g_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &bogus))
    {
        core_log(RETRO_LOG_ERROR, "host accepted an invalid pixel format\n");
    }

    retro_variable variable = { "test_core_speed", nullptr };
    if (!g_environment(RETRO_ENVIRONMENT_GET_VARIABLE, &variable))
    {
        core_log(RETRO_LOG_INFO, "no value for test_core_speed, using default\n");
    }

    retro_rumble_interface rumble = {};
    if (g_environment(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble))
    {
        g_rumble = rumble.set_rumble_state;
    }
    g_environment(RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS, &g_max_users);
    g_environment(RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES, &g_capabilities);

    // A command number nobody defines: must come back "not handled".
    g_environment(9999, nullptr);

    // Specified with NULL data (the return value is the answer): must NOT be
    // reported as a core bug. Regression test for a host bug found 2026-11-11.
    g_environment(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, nullptr);

    // A command that needs data, sent with NULL: the host must refuse it, not crash.
    if (g_environment(RETRO_ENVIRONMENT_GET_CAN_DUPE, nullptr))
    {
        core_log(RETRO_LOG_ERROR, "host accepted GET_CAN_DUPE with NULL data\n");
    }
}

void retro_deinit(void)
{
    core_log(RETRO_LOG_INFO, "deinit\n");
    g_environment = nullptr;
    g_log = nullptr;
}

// ---------------------------------------------------------------------------
// Content
// ---------------------------------------------------------------------------

bool retro_load_game(const retro_game_info* game)
{
    g_port0_device = RETRO_DEVICE_JOYPAD;
    send_input_descriptors();
    if (game == nullptr)
    {
        core_log(RETRO_LOG_INFO, "started without content\n");
        return true;
    }

    if (game->data == nullptr)
    {
        core_log(RETRO_LOG_ERROR, "need_fullpath is false but no data was passed\n");
        return false;
    }

    // The test content starts with this marker; anything else is rejected,
    // which also exercises the host's "load failed" path.
    static const char marker[] = "MULTITAP";
    if (game->size < sizeof(marker) - 1 || std::memcmp(game->data, marker, sizeof(marker) - 1) != 0)
    {
        core_log(RETRO_LOG_ERROR, "content does not start with MULTITAP (%zu bytes)\n", game->size);
        return false;
    }

    core_log(RETRO_LOG_INFO, "content OK: %s, %zu bytes\n", game->path != nullptr ? game->path : "(no path)", game->size);

    const char* message_text = "test core: content loaded";
    retro_message message = { message_text, 60 };
    g_environment(RETRO_ENVIRONMENT_SET_MESSAGE, &message);
    return true;
}

bool retro_load_game_special(unsigned type, const retro_game_info* info, size_t count)
{
    (void)type;
    (void)info;
    (void)count;
    return false;
}

void retro_unload_game(void)
{
    core_log(RETRO_LOG_INFO, "unload game\n");
}

void retro_get_system_av_info(retro_system_av_info* info)
{
    info->geometry.base_width   = 320;
    info->geometry.base_height  = 240;
    info->geometry.max_width    = 320;
    info->geometry.max_height   = 240;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = 60.0;
    info->timing.sample_rate    = 48000.0;
}

// ---------------------------------------------------------------------------
// Everything else: required exports, not used by the tests yet
// ---------------------------------------------------------------------------

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    core_log(RETRO_LOG_INFO, "port %u device %u\n", port, device);
    if (port == 0)
    {
        g_port0_device = device;
        send_input_descriptors();
    }
}

void retro_reset(void)
{
}

#if !defined(TEST_CORE_OMIT_RUN)
void retro_run(void)
{
    if (g_run_ahead_driven)
    {
        unsigned frames = 0;
        g_environment(RETRO_ENVIRONMENT_EXT_GET_RUN_AHEAD, &frames);
        if (frames != g_run_ahead_last)
        {
            core_log(RETRO_LOG_INFO, "run-ahead frames: %u\n", frames);
            g_run_ahead_last = frames;
        }
    }
    g_frame++;
    ram_from_frame();
    int av = 3;
    g_environment(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av);
    if ((av & 1) != 0 && g_video != nullptr)
    {
        uint32_t picture[8];
        for (uint32_t i = 0; i < 8; i++)
        {
            picture[i] = g_frame * 8 + i;
        }
        g_video(picture, 4, 2, 16);
    }
    int context = 0;
    if (g_environment(RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT, &context) && context != g_context_last)
    {
        core_log(RETRO_LOG_INFO, "savestate context: %d\n", context);
        g_context_last = context;
    }
    if (g_rumble != nullptr)
    {
        g_rumble(0, RETRO_RUMBLE_STRONG, static_cast<uint16_t>(g_max_users));
        g_rumble(0, RETRO_RUMBLE_WEAK, static_cast<uint16_t>(g_capabilities & 0xFFFFu));
    }
}
#endif

size_t retro_serialize_size(void)
{
    return sizeof(g_frame);
}

bool retro_serialize(void* data, size_t size)
{
    if (size < sizeof(g_frame))
    {
        return false;
    }
    std::memcpy(data, &g_frame, sizeof(g_frame));
    return true;
}

bool retro_unserialize(const void* data, size_t size)
{
    if (size < sizeof(g_frame))
    {
        return false;
    }
    std::memcpy(&g_frame, data, sizeof(g_frame));
    ram_from_frame();
    return true;
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
    (void)index;
    (void)enabled;
    (void)code;
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

void* retro_get_memory_data(unsigned id)
{
    return id == RETRO_MEMORY_SYSTEM_RAM ? g_ram : nullptr;
}

size_t retro_get_memory_size(unsigned id)
{
    return id == RETRO_MEMORY_SYSTEM_RAM ? sizeof(g_ram) : 0;
}
