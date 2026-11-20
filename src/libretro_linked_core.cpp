#include "libretro_linked_core.h"

#include <libretro.h>

#include "dynamic_library.h"

// libretro.h declares every retro_* function (extern "C"); the core's archive
// defines them. Each one as a GenericFunction, the same cast a loader's
// symbol lookup returns.
static const LinkedSymbol LINKED_CORE_SYMBOLS[] = {
    { "retro_set_environment",            reinterpret_cast<GenericFunction>(&retro_set_environment) },
    { "retro_set_video_refresh",          reinterpret_cast<GenericFunction>(&retro_set_video_refresh) },
    { "retro_set_audio_sample",           reinterpret_cast<GenericFunction>(&retro_set_audio_sample) },
    { "retro_set_audio_sample_batch",     reinterpret_cast<GenericFunction>(&retro_set_audio_sample_batch) },
    { "retro_set_input_poll",             reinterpret_cast<GenericFunction>(&retro_set_input_poll) },
    { "retro_set_input_state",            reinterpret_cast<GenericFunction>(&retro_set_input_state) },
    { "retro_init",                       reinterpret_cast<GenericFunction>(&retro_init) },
    { "retro_deinit",                     reinterpret_cast<GenericFunction>(&retro_deinit) },
    { "retro_api_version",                reinterpret_cast<GenericFunction>(&retro_api_version) },
    { "retro_get_system_info",            reinterpret_cast<GenericFunction>(&retro_get_system_info) },
    { "retro_get_system_av_info",         reinterpret_cast<GenericFunction>(&retro_get_system_av_info) },
    { "retro_set_controller_port_device", reinterpret_cast<GenericFunction>(&retro_set_controller_port_device) },
    { "retro_reset",                      reinterpret_cast<GenericFunction>(&retro_reset) },
    { "retro_run",                        reinterpret_cast<GenericFunction>(&retro_run) },
    { "retro_serialize_size",             reinterpret_cast<GenericFunction>(&retro_serialize_size) },
    { "retro_serialize",                  reinterpret_cast<GenericFunction>(&retro_serialize) },
    { "retro_unserialize",                reinterpret_cast<GenericFunction>(&retro_unserialize) },
    { "retro_cheat_reset",                reinterpret_cast<GenericFunction>(&retro_cheat_reset) },
    { "retro_cheat_set",                  reinterpret_cast<GenericFunction>(&retro_cheat_set) },
    { "retro_load_game",                  reinterpret_cast<GenericFunction>(&retro_load_game) },
    { "retro_load_game_special",          reinterpret_cast<GenericFunction>(&retro_load_game_special) },
    { "retro_unload_game",                reinterpret_cast<GenericFunction>(&retro_unload_game) },
    { "retro_get_region",                 reinterpret_cast<GenericFunction>(&retro_get_region) },
    { "retro_get_memory_data",            reinterpret_cast<GenericFunction>(&retro_get_memory_data) },
    { "retro_get_memory_size",            reinterpret_cast<GenericFunction>(&retro_get_memory_size) },
};

void libretro_linked_core_register(const char* file_name)
{
    LinkedLibrary library = {};
    library.file_name = file_name;
    library.symbols = LINKED_CORE_SYMBOLS;
    library.symbol_count = static_cast<int32_t>(sizeof(LINKED_CORE_SYMBOLS) / sizeof(LINKED_CORE_SYMBOLS[0]));
    dynamic_library_set_linked(&library, 1);
}
