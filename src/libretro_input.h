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

constexpr unsigned LIBRETRO_MAX_PORTS = 4;

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
