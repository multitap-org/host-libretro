#include "libretro_subsystem.h"

#include <cstdio>
#include <cstring>

#include "host_log.h"

// A core that forgets the terminating entry would make us read forever:
// stop at a number no real core comes near.
constexpr uint32_t SUBSYSTEMS_LIMIT = 256;

static void copy_text(char* out, size_t out_size, const char* text)
{
    std::snprintf(out, out_size, "%s", text != nullptr ? text : "");
}

// A memory extension becomes part of a file name: "srm" or ".srm" are fine,
// anything with a path separator or nothing at all is not.
static bool copy_memory_extension(char* out, const char* extension)
{
    if (extension == nullptr)
    {
        return false;
    }
    if (extension[0] == '.')
    {
        extension++;
    }
    size_t length = std::strlen(extension);
    if (length == 0 || length >= LIBRETRO_SUBSYSTEM_MEMORY_EXTENSION_MAX || std::strchr(extension, '/') != nullptr ||
        std::strchr(extension, '\\') != nullptr)
    {
        return false;
    }
    std::memcpy(out, extension, length + 1);
    return true;
}

static void store_rom(LibretroSubsystemRom* out, const retro_subsystem_rom_info* rom, const char* ident)
{
    copy_text(out->description, sizeof(out->description), rom->desc);
    copy_text(out->extensions, sizeof(out->extensions), rom->valid_extensions);
    out->need_fullpath = rom->need_fullpath;
    out->block_extract = rom->block_extract;
    out->required      = rom->required;

    unsigned count = rom->memory != nullptr ? rom->num_memory : 0;
    for (unsigned m = 0; m < count; m++)
    {
        if (out->memory_count >= LIBRETRO_SUBSYSTEM_MEMORY_MAX)
        {
            host_log(LogLevel::warning, "Subsystem %s: memory regions past %u dropped", ident,
                     LIBRETRO_SUBSYSTEM_MEMORY_MAX);
            break;
        }
        LibretroSubsystemMemory* memory = &out->memory[out->memory_count];
        if (!copy_memory_extension(memory->extension, rom->memory[m].extension))
        {
            host_log(LogLevel::warning, "Subsystem %s: memory type 0x%x has no usable extension, not saved", ident,
                     rom->memory[m].type);
            continue;
        }
        memory->type = rom->memory[m].type;
        out->memory_count++;
    }
}

uint32_t libretro_subsystem_store(LibretroSubsystems* subsystems, const retro_subsystem_info* info)
{
    subsystems->list.clear();
    uint32_t sent = 0;
    while (sent < SUBSYSTEMS_LIMIT && info[sent].desc != nullptr)
    {
        const retro_subsystem_info* entry = &info[sent];
        sent++;

        // The ident names the subsystem in settings and recent games: without
        // one there is nothing stable to keep.
        if (entry->ident == nullptr || entry->ident[0] == '\0' || std::strlen(entry->ident) >= LIBRETRO_SUBSYSTEM_IDENT_MAX)
        {
            host_log(LogLevel::warning, "Subsystem \"%s\": no usable ident, dropped", entry->desc);
            continue;
        }
        if (entry->roms == nullptr || entry->num_roms == 0 || entry->num_roms > LIBRETRO_SUBSYSTEM_ROMS_MAX)
        {
            host_log(LogLevel::warning, "Subsystem %s: %u content files (1 to %u), dropped", entry->ident, entry->num_roms,
                     LIBRETRO_SUBSYSTEM_ROMS_MAX);
            continue;
        }
        if (subsystems->list.size() >= LIBRETRO_SUBSYSTEMS_MAX)
        {
            host_log(LogLevel::warning, "Subsystem %s: more than %u subsystems, dropped", entry->ident,
                     LIBRETRO_SUBSYSTEMS_MAX);
            continue;
        }

        LibretroSubsystem subsystem = {};
        copy_text(subsystem.description, sizeof(subsystem.description), entry->desc);
        copy_text(subsystem.ident, sizeof(subsystem.ident), entry->ident);
        subsystem.id = entry->id;
        subsystem.roms.resize(entry->num_roms);
        for (unsigned r = 0; r < entry->num_roms; r++)
        {
            store_rom(&subsystem.roms[r], &entry->roms[r], subsystem.ident);
        }
        subsystems->list.push_back(subsystem);
    }
    return sent;
}

const LibretroSubsystem* libretro_subsystem_find(const LibretroSubsystems* subsystems, const char* ident)
{
    if (ident == nullptr)
    {
        return nullptr;
    }
    for (size_t i = 0; i < subsystems->list.size(); i++)
    {
        if (std::strcmp(subsystems->list[i].ident, ident) == 0)
        {
            return &subsystems->list[i];
        }
    }
    return nullptr;
}

// Case-insensitive ASCII comparison of `length` characters.
static bool same_text_ignoring_case(const char* a, const char* b, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        char x = a[i];
        char y = b[i];
        x = (x >= 'A' && x <= 'Z') ? static_cast<char>(x - 'A' + 'a') : x;
        y = (y >= 'A' && y <= 'Z') ? static_cast<char>(y - 'A' + 'a') : y;
        if (x != y)
        {
            return false;
        }
    }
    return true;
}

bool libretro_extensions_accept(const char* extensions, const char* path)
{
    if (extensions == nullptr || extensions[0] == '\0')
    {
        return true;   // the core didn't say: let it try
    }

    // The extension of the file name, not of a directory ("dir.v2/game").
    const char* dot = std::strrchr(path, '.');
    const char* slash = std::strrchr(path, '/');
    const char* backslash = std::strrchr(path, '\\');
    if (backslash != nullptr && (slash == nullptr || backslash > slash))
    {
        slash = backslash;
    }
    if (dot == nullptr || (slash != nullptr && dot < slash))
    {
        return false;
    }
    const char* extension = dot + 1;
    size_t extension_length = std::strlen(extension);

    // Walk "sfc|smc|swc": compare each entry with the content's extension.
    const char* entry = extensions;
    while (*entry != '\0')
    {
        const char* bar = std::strchr(entry, '|');
        size_t entry_length = bar != nullptr ? static_cast<size_t>(bar - entry) : std::strlen(entry);
        if (entry_length == extension_length && same_text_ignoring_case(entry, extension, entry_length))
        {
            return true;
        }
        if (bar == nullptr)
        {
            break;
        }
        entry = bar + 1;
    }
    return false;
}
