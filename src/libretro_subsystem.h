#pragma once

// Subsystems (SET_SUBSYSTEM_INFO): the other ways a core can start, each with
// several content files at once. Super Game Boy (a SNES BIOS + a Game Boy
// game), Sufami Turbo (BIOS + two carts), two linked Game Boys, ... Each
// subsystem lists its content files ("ROMs" in libretro): what they are, the
// extensions they take, whether one is required, and the core memory each
// one keeps (a cart's save RAM, saved next to that cart's name).
//
// Cores send this from retro_set_environment, so peeking catches it too.
// Copied, never kept as the core's pointers. Started with
// libretro_host_load_game_special (libretro_host.h).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "libretro.h"

constexpr size_t   LIBRETRO_SUBSYSTEM_TEXT_MAX       = 128;
constexpr size_t   LIBRETRO_SUBSYSTEM_IDENT_MAX      = 32;
constexpr size_t   LIBRETRO_SUBSYSTEM_EXTENSIONS_MAX = 256;
constexpr size_t   LIBRETRO_SUBSYSTEM_MEMORY_EXTENSION_MAX = 16;
constexpr uint32_t LIBRETRO_SUBSYSTEM_MEMORY_MAX     = 4;    // per content file
constexpr uint32_t LIBRETRO_SUBSYSTEMS_MAX           = 32;   // FinalBurn Neo lists ~20
constexpr uint32_t LIBRETRO_SUBSYSTEM_ROMS_MAX       = 8;    // Sufami Turbo has 3

// One core memory region a content file keeps: retro_get_memory_data(type),
// saved as "<content name>.<extension>".
struct LibretroSubsystemMemory
{
    unsigned type = 0;
    char     extension[LIBRETRO_SUBSYSTEM_MEMORY_EXTENSION_MAX] = {};   // "srm", without the dot
};

// One content file of a subsystem.
struct LibretroSubsystemRom
{
    char     description[LIBRETRO_SUBSYSTEM_TEXT_MAX] = {};       // "Game Boy ROM"
    char     extensions[LIBRETRO_SUBSYSTEM_EXTENSIONS_MAX] = {};  // "gb|gbc", may be empty (anything)
    bool     need_fullpath = false;
    bool     block_extract = false;
    bool     required      = false;   // false: the core starts without it (empty retro_game_info)
    uint32_t memory_count  = 0;
    LibretroSubsystemMemory memory[LIBRETRO_SUBSYSTEM_MEMORY_MAX] = {};
};

struct LibretroSubsystem
{
    char     description[LIBRETRO_SUBSYSTEM_TEXT_MAX] = {};   // "Super Game Boy"
    char     ident[LIBRETRO_SUBSYSTEM_IDENT_MAX] = {};        // "sgb": stable, for settings and recent games
    unsigned id = 0;                                          // what retro_load_game_special gets
    std::vector<LibretroSubsystemRom> roms;                   // in the order retro_load_game_special wants them
};

// The latest SET_SUBSYSTEM_INFO (each call replaces the list). Empty = the
// core has none.
struct LibretroSubsystems
{
    std::vector<LibretroSubsystem> list;
};

// Copies a SET_SUBSYSTEM_INFO array (ends with an entry whose `desc` is
// NULL). Replaces what was there. Entries past the maximums, without an
// ident or without content files are dropped (logged). Returns how many
// subsystems the core sent, all of them.
uint32_t libretro_subsystem_store(LibretroSubsystems* subsystems, const retro_subsystem_info* info);

// The subsystem with this ident, or nullptr.
const LibretroSubsystem* libretro_subsystem_find(const LibretroSubsystems* subsystems, const char* ident);

// True if `path` has one of the extensions in `extensions` ("gb|gbc",
// case-insensitive; an empty list accepts anything). The extension of the
// file name, not of a directory ("dir.v2/game").
bool libretro_extensions_accept(const char* extensions, const char* path);
