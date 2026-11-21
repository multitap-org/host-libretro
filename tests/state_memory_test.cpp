// Checks the host's savestates in memory and hidden frames (rollback and
// run-ahead's base) with the test core, whose state is a frame counter kept
// as its system RAM: a state saved and loaded brings the RAM back, hidden
// frames draw nothing, the core's quirks are kept and GET_SAVESTATE_CONTEXT
// answers what the frontend set.
//
// Usage: host_libretro_state_memory_test <test core>
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>
#include <vector>

#include "host_log.h"
#include "libretro_host.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

// What the event listener heard.
static uint32_t          g_events      = 0;
static LibretroHostEvent g_last_event  = LibretroHostEvent::reset;
static uint8_t           g_last_data[512] = {};
static uint32_t          g_last_size   = 0;

static void heard(void* userdata, LibretroHostEvent event, const uint8_t* data, uint32_t size)
{
    (void)userdata;
    g_events++;
    g_last_event = event;
    g_last_size  = size <= sizeof(g_last_data) ? size : 0;
    if (g_last_size > 0)
    {
        std::memcpy(g_last_data, data, g_last_size);
    }
}

static uint32_t ram_frame(LibretroHost* host)
{
    const uint8_t* ram = nullptr;
    size_t size = 0;
    if (!libretro_host_memory(host, RETRO_MEMORY_SYSTEM_RAM, &ram, &size) || size < 4)
    {
        return 0xFFFFFFFFu;
    }
    // The test core keeps byte i = (frame >> 8 * (i % 4)) + i * 7.
    uint32_t frame = 0;
    for (uint32_t i = 0; i < 4; i++)
    {
        frame |= static_cast<uint32_t>(static_cast<uint8_t>(ram[i] - i * 7)) << (8 * i);
    }
    return frame;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("FAIL usage: host_libretro_state_memory_test <test core>\n");
        return 1;
    }
    host_log_set_min_level(LogLevel::warning);
    LibretroHost* host = new LibretroHost{};
    LibretroHostConfig config;
    check(libretro_host_load_core(host, argv[1], &config) && libretro_host_load_game(host, nullptr), "test core loaded");
    check(host->serialization_quirks == RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT, "the core's quirks are kept");

    for (int i = 0; i < 10; i++)
    {
        libretro_host_run_frame(host);
    }
    check(ram_frame(host) == 10, "the core's RAM is readable");
    check(libretro_host_state_size(host) == 8, "state size");
    std::vector<uint8_t> state;
    check(libretro_host_serialize(host, &state) && state.size() == 8, "state saved in memory");
    for (int i = 0; i < 5; i++)
    {
        libretro_host_run_frame(host);
    }
    check(ram_frame(host) == 15, "ran on");
    check(libretro_host_unserialize(host, state.data(), state.size()) && ram_frame(host) == 10,
          "state loaded from memory: the RAM is back");

    // Hidden frames: no picture copied.
    uint64_t pictures = host->stats.video_frames;
    libretro_host_set_hidden(host, true);
    libretro_host_set_savestate_context(host, RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY);
    for (int i = 0; i < 3; i++)
    {
        libretro_host_run_frame(host);
    }
    check(host->stats.video_frames == pictures, "hidden frames draw nothing");
    check(ram_frame(host) == 13, "but they run");
    libretro_host_set_hidden(host, false);
    libretro_host_run_frame(host);
    check(host->stats.video_frames == pictures + 1, "a shown frame draws again");

    // Into memory the caller holds, no allocation.
    uint8_t raw[16] = {};
    check(!libretro_host_serialize_to(host, raw, 4), "serialize_to: too small refused");
    check(libretro_host_serialize_to(host, raw, sizeof(raw)), "serialize_to");
    check(libretro_host_serialize(host, &state) && std::memcmp(raw, state.data(), 8) == 0, "serialize_to: same bytes");

    // Events: heard after they reach the core, and given back by replay.
    libretro_host_set_event_listener(host, heard, nullptr);
    check(libretro_host_set_option(host, "test_core_speed", "fast"), "option set");
    check(g_events == 1 && g_last_event == LibretroHostEvent::option && g_last_size == 21 &&
          std::memcmp(g_last_data, "test_core_speed\0fast\0", 21) == 0, "option heard");
    uint8_t  option_event[64] = {};
    uint32_t option_size      = g_last_size;
    std::memcpy(option_event, g_last_data, option_size);
    libretro_host_reset(host);
    check(g_events == 2 && g_last_event == LibretroHostEvent::reset && g_last_size == 0, "reset heard");
    check(libretro_host_set_option(host, "test_core_speed", "normal"), "option back");
    check(libretro_host_replay_event(host, LibretroHostEvent::option, option_event, option_size) &&
          g_last_event == LibretroHostEvent::option && std::strcmp(reinterpret_cast<char*>(g_last_data) + 16, "fast") == 0,
          "option replayed");
    check(!libretro_host_replay_event(host, LibretroHostEvent::option, option_event, 10), "malformed option refused");
    check(!libretro_host_replay_event(host, LibretroHostEvent::keyboard, option_event, 3), "malformed keyboard refused");
    uint32_t events = g_events;
    check(libretro_host_replay_event(host, LibretroHostEvent::reset, nullptr, 0) && g_events == events + 1,
          "reset replayed");
    libretro_host_set_event_listener(host, nullptr, nullptr);
    libretro_host_reset(host);
    check(g_events == events + 1, "no listener, nothing heard");

    libretro_host_unload_core(host);
    delete host;
    if (g_failures == 0)
    {
        std::printf("state memory: all checks passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
