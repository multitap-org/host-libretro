#pragma once

// What a core says about its input, kept as data the frontend can read:
//   - SET_CONTROLLER_INFO: per port, the device types the core can emulate
//     ("SNES Joypad", "SNES Mouse", "Multitap"...), each with its id.
//   - SET_INPUT_DESCRIPTORS: the controls the core uses and what they do in
//     this game ("Jump", "Run"...), per port / device / index / id. Cores send
//     them again when a port's device changes, so each call replaces the list.
// Copied, never kept as the core's pointers (the core may free them).

#include <cstddef>
#include <cstdint>

#include "libretro.h"
#include "libretro_extensions.h"

// 8: two multitaps of 4 (SNES, PlayStation, Jaguar); the most players a
// core is told it can have (GET_INPUT_MAX_USERS).
constexpr unsigned LIBRETRO_MAX_PORTS = 8;

constexpr size_t   LIBRETRO_INPUT_TEXT_MAX          = 128;
constexpr uint32_t LIBRETRO_CONTROLLER_TYPES_MAX    = 32;    // per port
constexpr uint32_t LIBRETRO_INPUT_DESCRIPTORS_MAX   = 256;

struct LibretroControllerType
{
    unsigned id = 0;                                // RETRO_DEVICE_* or a subclass (RETRO_DEVICE_SUBCLASS)
    char     description[LIBRETRO_INPUT_TEXT_MAX] = {};
};

struct LibretroPortControllers
{
    uint32_t               count = 0;
    LibretroControllerType types[LIBRETRO_CONTROLLER_TYPES_MAX] = {};
};

struct LibretroInputDescriptor
{
    unsigned port   = 0;
    unsigned device = 0;
    unsigned index  = 0;
    unsigned id     = 0;
    char     description[LIBRETRO_INPUT_TEXT_MAX] = {};
};

struct LibretroInputInfo
{
    // SET_CONTROLLER_INFO. port_count = how many ports the core described
    // (0 = it sent nothing); ports past LIBRETRO_MAX_PORTS are dropped (logged).
    LibretroPortControllers ports[LIBRETRO_MAX_PORTS] = {};
    uint32_t                port_count         = 0;
    uint32_t                controller_serial  = 0;   // +1 on every SET_CONTROLLER_INFO

    // SET_INPUT_DESCRIPTORS, the latest list.
    LibretroInputDescriptor descriptors[LIBRETRO_INPUT_DESCRIPTORS_MAX] = {};
    uint32_t                descriptor_count  = 0;
    uint32_t                descriptor_serial = 0;   // +1 on every SET_INPUT_DESCRIPTORS
};

// Copies a SET_CONTROLLER_INFO array (ends with an entry whose `types` is
// NULL). Replaces what was there. Returns the number of ports the core
// described, all of them (also those dropped past LIBRETRO_MAX_PORTS).
uint32_t libretro_input_store_controllers(LibretroInputInfo* info, const retro_controller_info* controllers);

// Copies a SET_INPUT_DESCRIPTORS array (ends with an entry whose
// `description` is NULL). Replaces the list. Returns the number of
// descriptors the core sent (also those dropped past the maximum).
uint32_t libretro_input_store_descriptors(LibretroInputInfo* info, const retro_input_descriptor* descriptors);

// The description of a port's device type, or nullptr if the core didn't
// list it for that port.
const char* libretro_input_controller_name(const LibretroInputInfo* info, unsigned port, unsigned device);

// ---------------------------------------------------------------------------
// Port adapters (Multitap's extension, libretro_extensions.h): the core's
// physical ports, the adapters each takes, which one the frontend wants on
// each, and the players (libretro ports) that gives.
// ---------------------------------------------------------------------------

constexpr uint32_t LIBRETRO_PHYSICAL_PORTS_MAX = 4;
constexpr uint32_t LIBRETRO_ADAPTERS_MAX       = 4;             // per physical port
constexpr unsigned LIBRETRO_ADAPTER_AUTO       = 0xFFFFFFFFu;   // wanted: the one the core suggests for this game

struct LibretroAdapter
{
    unsigned                id = 0;
    char                    description[LIBRETRO_INPUT_TEXT_MAX] = {};
    uint32_t                sub_ports = 0;
    bool                    takes_next_port = false;   // a Four Score: this port and the next
    LibretroPortControllers types = {};                // what each sub-port takes
};

struct LibretroPhysicalPort
{
    char                    description[LIBRETRO_INPUT_TEXT_MAX] = {};
    LibretroPortControllers types = {};                // plugged straight in; none = a built-in pad
    LibretroAdapter         adapters[LIBRETRO_ADAPTERS_MAX] = {};
    uint32_t                adapter_count = 0;
};

struct LibretroPortLayout
{
    bool                     offered = false;          // the core described its ports (EXT_SET_PORT_ADAPTERS)
    LibretroPhysicalPort     ports[LIBRETRO_PHYSICAL_PORTS_MAX] = {};
    uint32_t                 port_count = 0;
    retro_ext_set_adapters_t set_adapters = nullptr;
    unsigned                 suggested[LIBRETRO_PHYSICAL_PORTS_MAX] = {};   // this game's (EXT_SUGGEST_PORT_ADAPTERS), 0 = none

    // The frontend's choice per physical port: 0 = none (the default),
    // LIBRETRO_ADAPTER_AUTO or an adapter's id.
    unsigned                 wanted[LIBRETRO_PHYSICAL_PORTS_MAX] = {};

    // What that gives (libretro_ports_resolve): the adapter in use per
    // physical port (0 = none), its first player (RETRO_EXT_PORT_UNUSED: the
    // adapter before took the port) and the players in all.
    unsigned                 applied[LIBRETRO_PHYSICAL_PORTS_MAX] = {};
    uint32_t                 first_port[LIBRETRO_PHYSICAL_PORTS_MAX] = {};
    uint32_t                 player_count = 0;
    uint32_t                 serial = 0;               // +1 every time adapters are applied
};

// Copies the core's description. False (and `error` says why) for one that
// can't be used: no set_adapters, no ports, too many, an adapter id 0...
bool libretro_ports_store(LibretroPortLayout* layout, const retro_ext_port_adapters* description, char* error,
                          size_t error_size);

// The adapter `id` on a physical port, or nullptr (0, unknown, no such port).
const LibretroAdapter* libretro_ports_adapter(const LibretroPortLayout* layout, uint32_t physical, unsigned id);

// From wanted (and suggested, for AUTO) to applied / first_port /
// player_count: players numbered in order, physical port by physical port; an
// adapter that doesn't fit (past LIBRETRO_MAX_PORTS players, a two-port one
// on the last port) isn't connected.
void libretro_ports_resolve(LibretroPortLayout* layout);

// Where a player is: its physical port and sub-port (0 without an adapter).
// False for a player past player_count.
bool libretro_ports_player(const LibretroPortLayout* layout, uint32_t player, uint32_t* physical, uint32_t* sub_port);

// The device types a player can have (the adapter's sub-port types, or the
// physical port's own). Empty for a built-in pad.
const LibretroPortControllers* libretro_ports_player_types(const LibretroPortLayout* layout, uint32_t player);
