#include "libretro_input.h"

#include <cstdio>
#include <cstring>

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

// ---------------------------------------------------------------------------
// Port adapters
// ---------------------------------------------------------------------------

static void copy_types(LibretroPortControllers* out, const retro_controller_description* types, unsigned count)
{
    *out = LibretroPortControllers{};
    if (types == nullptr)
    {
        return;
    }
    for (unsigned t = 0; t < count && out->count < LIBRETRO_CONTROLLER_TYPES_MAX; t++)
    {
        out->types[out->count].id = types[t].id;
        copy_text(out->types[out->count].description, types[t].desc);
        out->count++;
    }
}

bool libretro_ports_store(LibretroPortLayout* layout, const retro_ext_port_adapters* description, char* error,
                          size_t error_size)
{
    if (description->set_adapters == nullptr)
    {
        std::snprintf(error, error_size, "no set_adapters callback");
        return false;
    }
    if (description->ports == nullptr || description->num_ports == 0 ||
        description->num_ports > LIBRETRO_PHYSICAL_PORTS_MAX)
    {
        std::snprintf(error, error_size, "%u physical ports (1..%u)", description->num_ports, LIBRETRO_PHYSICAL_PORTS_MAX);
        return false;
    }
    for (unsigned p = 0; p < description->num_ports; p++)
    {
        const retro_ext_physical_port* port = &description->ports[p];
        if (port->num_adapters > LIBRETRO_ADAPTERS_MAX || (port->num_adapters > 0 && port->adapters == nullptr))
        {
            std::snprintf(error, error_size, "port %u: %u adapters (at most %u)", p + 1, port->num_adapters,
                          LIBRETRO_ADAPTERS_MAX);
            return false;
        }
        for (unsigned a = 0; a < port->num_adapters; a++)
        {
            const retro_ext_adapter* adapter = &port->adapters[a];
            if (adapter->id == 0 || adapter->id == LIBRETRO_ADAPTER_AUTO || adapter->sub_ports < 1 ||
                adapter->sub_ports > LIBRETRO_MAX_PORTS)
            {
                std::snprintf(error, error_size, "port %u: adapter %u has id %u and %u sub-ports", p + 1, a + 1,
                              adapter->id, adapter->sub_ports);
                return false;
            }
        }
    }

    // Keeps what the frontend wants: the core may describe its ports again.
    unsigned wanted[LIBRETRO_PHYSICAL_PORTS_MAX] = {};
    std::memcpy(wanted, layout->wanted, sizeof(wanted));
    uint32_t serial = layout->serial;
    *layout = LibretroPortLayout{};
    std::memcpy(layout->wanted, wanted, sizeof(wanted));
    layout->serial = serial;

    layout->offered = true;
    layout->port_count = description->num_ports;
    layout->set_adapters = description->set_adapters;
    for (unsigned p = 0; p < description->num_ports; p++)
    {
        const retro_ext_physical_port* in = &description->ports[p];
        LibretroPhysicalPort* out = &layout->ports[p];
        copy_text(out->description, in->desc);
        copy_types(&out->types, in->types, in->num_types);
        for (unsigned a = 0; a < in->num_adapters; a++)
        {
            const retro_ext_adapter* adapter = &in->adapters[a];
            LibretroAdapter* kept = &out->adapters[out->adapter_count];
            kept->id = adapter->id;
            copy_text(kept->description, adapter->desc);
            kept->sub_ports = adapter->sub_ports;
            kept->takes_next_port = (adapter->flags & RETRO_EXT_ADAPTER_TAKES_NEXT_PORT) != 0;
            copy_types(&kept->types, adapter->types, adapter->num_types);
            out->adapter_count++;
        }
    }
    libretro_ports_resolve(layout);
    return true;
}

const LibretroAdapter* libretro_ports_adapter(const LibretroPortLayout* layout, uint32_t physical, unsigned id)
{
    if (physical >= layout->port_count || id == 0)
    {
        return nullptr;
    }
    const LibretroPhysicalPort* port = &layout->ports[physical];
    for (uint32_t a = 0; a < port->adapter_count; a++)
    {
        if (port->adapters[a].id == id)
        {
            return &port->adapters[a];
        }
    }
    return nullptr;
}

void libretro_ports_resolve(LibretroPortLayout* layout)
{
    uint32_t players = 0;
    bool taken = false;   // the adapter on the port before took this one
    for (uint32_t p = 0; p < layout->port_count; p++)
    {
        if (taken)
        {
            layout->applied[p] = 0;
            layout->first_port[p] = RETRO_EXT_PORT_UNUSED;
            taken = false;
            continue;
        }
        unsigned id = layout->wanted[p] == LIBRETRO_ADAPTER_AUTO ? layout->suggested[p] : layout->wanted[p];
        const LibretroAdapter* adapter = libretro_ports_adapter(layout, p, id);
        if (adapter != nullptr && adapter->takes_next_port && p + 1 >= layout->port_count)
        {
            adapter = nullptr;   // a two-port adapter needs a next port
        }
        // The ports after this one still need a player each.
        uint32_t later = layout->port_count - p - 1 - (adapter != nullptr && adapter->takes_next_port ? 1u : 0u);
        if (adapter != nullptr && players + adapter->sub_ports + later > LIBRETRO_MAX_PORTS)
        {
            adapter = nullptr;   // past the most players: not connected
        }
        layout->applied[p] = adapter != nullptr ? adapter->id : 0;
        layout->first_port[p] = players;
        players += adapter != nullptr ? adapter->sub_ports : 1;
        taken = adapter != nullptr && adapter->takes_next_port;
    }
    layout->player_count = players;
}

bool libretro_ports_player(const LibretroPortLayout* layout, uint32_t player, uint32_t* physical, uint32_t* sub_port)
{
    if (player >= layout->player_count)
    {
        return false;
    }
    for (uint32_t p = 0; p < layout->port_count; p++)
    {
        uint32_t first = layout->first_port[p];
        if (first == RETRO_EXT_PORT_UNUSED)
        {
            continue;
        }
        const LibretroAdapter* adapter = libretro_ports_adapter(layout, p, layout->applied[p]);
        uint32_t count = adapter != nullptr ? adapter->sub_ports : 1;
        if (player >= first && player < first + count)
        {
            *physical = p;
            *sub_port = player - first;
            return true;
        }
    }
    return false;
}

const LibretroPortControllers* libretro_ports_player_types(const LibretroPortLayout* layout, uint32_t player)
{
    uint32_t physical = 0;
    uint32_t sub_port = 0;
    if (!libretro_ports_player(layout, player, &physical, &sub_port))
    {
        return nullptr;
    }
    const LibretroAdapter* adapter = libretro_ports_adapter(layout, physical, layout->applied[physical]);
    return adapter != nullptr ? &adapter->types : &layout->ports[physical].types;
}
