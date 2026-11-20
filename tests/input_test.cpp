// Checks what the host keeps of a core's input (libretro_input.h) and the
// input calls it answers:
//   1. libretro_input_store_*: copies, replaces, limits, missing terminators
//      capped, controller names by port and id;
//   2. with the test core (argv[1]): its controller info and descriptors
//      arrive, the port device the frontend picks reaches the core (before
//      and after the game loads) and brings new descriptors, rumble works,
//      GET_INPUT_MAX_USERS / GET_INPUT_DEVICE_CAPABILITIES answer what the
//      header says (the test core reports them through port 0's rumble).
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>

#include "libretro_host.h"
#include "libretro_input.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static bool text_is(const char* text, const char* expected)
{
    return text != nullptr && std::strcmp(text, expected) == 0;
}

// ---------------------------------------------------------------------------
// 1. Storing
// ---------------------------------------------------------------------------

static void test_store_controllers()
{
    LibretroInputInfo* info = new LibretroInputInfo{};
    retro_controller_description pad_types[] = {
        { "Pad", RETRO_DEVICE_JOYPAD },
        { "Mouse", RETRO_DEVICE_MOUSE },
    };
    retro_controller_description one_type[] = {
        { nullptr, RETRO_DEVICE_JOYPAD },   // no description: stored as ""
    };
    retro_controller_info controllers[] = {
        { pad_types, 2 },
        { one_type, 1 },
        { one_type, 1 },
        { one_type, 1 },
        { one_type, 1 },
        { one_type, 1 },
        { one_type, 1 },
        { one_type, 1 },
        { pad_types, 2 },   // a ninth port: counted, not kept
        { nullptr, 0 },
    };
    uint32_t described = libretro_input_store_controllers(info, controllers);
    check(described == 9, "controllers: all nine ports counted");
    check(info->port_count == LIBRETRO_MAX_PORTS, "controllers: eight ports kept");
    check(info->ports[0].count == 2 && info->ports[0].types[1].id == RETRO_DEVICE_MOUSE, "controllers: port 0 types");
    check(text_is(info->ports[0].types[1].description, "Mouse"), "controllers: description copied");
    check(text_is(info->ports[1].types[0].description, ""), "controllers: no description = empty");
    check(text_is(libretro_input_controller_name(info, 0, RETRO_DEVICE_MOUSE), "Mouse"), "controllers: name by id");
    check(libretro_input_controller_name(info, 1, RETRO_DEVICE_MOUSE) == nullptr, "controllers: not listed on that port");
    check(libretro_input_controller_name(info, LIBRETRO_MAX_PORTS, RETRO_DEVICE_JOYPAD) == nullptr, "controllers: port past the maximum");
    check(info->controller_serial == 1, "controllers: serial counts calls");

    // A second call replaces everything.
    retro_controller_info fewer[] = {
        { one_type, 1 },
        { nullptr, 0 },
    };
    libretro_input_store_controllers(info, fewer);
    check(info->port_count == 1 && info->ports[0].count == 1, "controllers: replaced");
    check(info->ports[1].count == 0, "controllers: old ports cleared");
    check(info->controller_serial == 2, "controllers: serial again");
    delete info;
}

static void test_store_descriptors()
{
    LibretroInputInfo* info = new LibretroInputInfo{};
    retro_input_descriptor descriptors[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Jump" },
        { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Steer" },
        { 0, 0, 0, 0, nullptr },
    };
    uint32_t sent = libretro_input_store_descriptors(info, descriptors);
    check(sent == 2 && info->descriptor_count == 2, "descriptors: two stored");
    check(info->descriptors[1].port == 1 && info->descriptors[1].device == RETRO_DEVICE_ANALOG &&
              info->descriptors[1].index == RETRO_DEVICE_INDEX_ANALOG_LEFT && info->descriptors[1].id == RETRO_DEVICE_ID_ANALOG_X,
          "descriptors: fields copied");
    check(text_is(info->descriptors[0].description, "Jump"), "descriptors: description copied");

    // More than the maximum: counted, the rest dropped.
    static retro_input_descriptor many[LIBRETRO_INPUT_DESCRIPTORS_MAX + 11] = {};
    for (uint32_t i = 0; i < LIBRETRO_INPUT_DESCRIPTORS_MAX + 10; i++)
    {
        many[i] = { 0, RETRO_DEVICE_JOYPAD, 0, 0, "x" };
    }
    sent = libretro_input_store_descriptors(info, many);
    check(sent == LIBRETRO_INPUT_DESCRIPTORS_MAX + 10, "descriptors: all counted");
    check(info->descriptor_count == LIBRETRO_INPUT_DESCRIPTORS_MAX, "descriptors: capped");
    check(info->descriptor_serial == 2, "descriptors: serial counts calls");
    delete info;
}

// ---------------------------------------------------------------------------
// 2. With the test core
// ---------------------------------------------------------------------------

constexpr unsigned SIX_BUTTON = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0);

static bool has_descriptor(const LibretroInputInfo* info, unsigned port, unsigned id, const char* description)
{
    for (uint32_t i = 0; i < info->descriptor_count; i++)
    {
        const LibretroInputDescriptor* d = &info->descriptors[i];
        if (d->port == port && d->id == id && text_is(d->description, description))
        {
            return true;
        }
    }
    return false;
}

static void test_with_core(const char* core_path)
{
    LibretroHost* host = new LibretroHost{};
    LibretroHostConfig config = {};
    check(libretro_host_load_core(host, core_path, &config), "core: loads");

    // Declared in retro_set_environment.
    check(host->input_info.port_count == 2, "core: two ports described");
    check(text_is(libretro_input_controller_name(&host->input_info, 0, SIX_BUTTON), "Test Six Button Pad"),
          "core: six-button pad listed on port 0");
    check(libretro_input_controller_name(&host->input_info, 1, SIX_BUTTON) == nullptr, "core: not on port 1");

    // Picked before the game loads: kept, then applied after the load.
    check(libretro_host_set_port_device(host, 0, SIX_BUTTON), "core: port device set before the game");
    check(!libretro_host_set_port_device(host, LIBRETRO_MAX_PORTS, RETRO_DEVICE_JOYPAD), "core: port past the maximum refused");
    check(libretro_host_load_game(host, nullptr), "core: game loads (no content)");
    check(has_descriptor(&host->input_info, 0, RETRO_DEVICE_ID_JOYPAD_A, "Mid Kick"),
          "core: six-button descriptors after the load");
    check(has_descriptor(&host->input_info, 1, RETRO_DEVICE_ID_JOYPAD_B, "Player 2 Jump"), "core: port 1 descriptor");

    // Changed while the game runs: new descriptors at once.
    uint32_t serial = host->input_info.descriptor_serial;
    check(libretro_host_set_port_device(host, 0, RETRO_DEVICE_JOYPAD), "core: back to the joypad");
    check(host->input_info.descriptor_serial > serial, "core: descriptors sent again");
    check(has_descriptor(&host->input_info, 0, RETRO_DEVICE_ID_JOYPAD_B, "Jump"), "core: joypad descriptors");
    check(!has_descriptor(&host->input_info, 0, RETRO_DEVICE_ID_JOYPAD_A, "Mid Kick"), "core: old ones gone");

    // Rumble, carrying the input answers (see test_core.cpp).
    check(libretro_host_run_frame(host), "core: runs a frame");
    check(host->rumble[0][0] == LIBRETRO_MAX_PORTS, "core: GET_INPUT_MAX_USERS = LIBRETRO_MAX_PORTS (strong rumble)");
    check(host->rumble[0][1] == (LIBRETRO_INPUT_DEVICE_CAPABILITIES & 0xFFFFu),
          "core: GET_INPUT_DEVICE_CAPABILITIES (weak rumble)");

    libretro_host_unload_game(host);
    libretro_host_unload_core(host);
    delete host;
}

int main(int argc, char** argv)
{
    test_store_controllers();
    test_store_descriptors();
    if (argc >= 2)
    {
        test_with_core(argv[1]);
    }
    else
    {
        std::printf("FAIL no test core given\n");
        g_failures++;
    }
    if (g_failures == 0)
    {
        std::printf("input: all checks passed\n");
        return 0;
    }
    std::printf("input: %d check(s) failed\n", g_failures);
    return 1;
}
