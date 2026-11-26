#include "libretro_input.h"

#include <cstdio>

// A core that forgets the terminating entry would make us read forever:
// stop at a number no real core comes near.
constexpr uint32_t CONTROLLER_PORTS_LIMIT = 64;
constexpr uint32_t DESCRIPTORS_LIMIT      = 4096;

static void copy_text(char* out, const char* text)
{
    std::snprintf(out, LIBRETRO_INPUT_TEXT_MAX, "%s", text != nullptr ? text : "");
}

uint32_t libretro_input_store_controllers(LibretroInputInfo* info, const retro_controller_info* controllers)
{
    for (uint32_t p = 0; p < LIBRETRO_MAX_PORTS; p++)
    {
        info->ports[p] = LibretroPortControllers{};
    }
    uint32_t described = 0;
    while (described < CONTROLLER_PORTS_LIMIT && controllers[described].types != nullptr)
    {
        if (described < LIBRETRO_MAX_PORTS)
        {
            LibretroPortControllers* port = &info->ports[described];
            unsigned count = controllers[described].num_types;
            for (unsigned t = 0; t < count && port->count < LIBRETRO_CONTROLLER_TYPES_MAX; t++)
            {
                LibretroControllerType* type = &port->types[port->count];
                type->id = controllers[described].types[t].id;
                copy_text(type->description, controllers[described].types[t].desc);
                port->count++;
            }
        }
        described++;
    }
    info->port_count = described < LIBRETRO_MAX_PORTS ? described : LIBRETRO_MAX_PORTS;
    info->controller_serial++;
    return described;
}

uint32_t libretro_input_store_descriptors(LibretroInputInfo* info, const retro_input_descriptor* descriptors)
{
    info->descriptor_count = 0;
    uint32_t sent = 0;
    while (sent < DESCRIPTORS_LIMIT && descriptors[sent].description != nullptr)
    {
        if (info->descriptor_count < LIBRETRO_INPUT_DESCRIPTORS_MAX)
        {
            LibretroInputDescriptor* out = &info->descriptors[info->descriptor_count];
            out->port = descriptors[sent].port;
            out->device = descriptors[sent].device;
            out->index = descriptors[sent].index;
            out->id = descriptors[sent].id;
            copy_text(out->description, descriptors[sent].description);
            info->descriptor_count++;
        }
        sent++;
    }
    info->descriptor_serial++;
    return sent;
}

const char* libretro_input_controller_name(const LibretroInputInfo* info, unsigned port, unsigned device)
{
    if (port >= info->port_count)
    {
        return nullptr;
    }
    const LibretroPortControllers* controllers = &info->ports[port];
    for (uint32_t t = 0; t < controllers->count; t++)
    {
        if (controllers->types[t].id == device)
        {
            return controllers->types[t].description;
        }
    }
    return nullptr;
}
