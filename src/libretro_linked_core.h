#pragma once

// A libretro core linked into the program instead of loaded from a file (the
// browser build: one wasm per core, no dlopen). Its own small library,
// host_libretro_linked, which a frontend links together with the core's
// static archive (the archive provides the retro_* functions this file names).
//
// After libretro_linked_core_register("sameboy_libretro.so"), opening any
// core path whose file name is that one opens the linked core
// (dynamic_library_set_linked in hosts-common): libretro_host_load_core,
// peeking and the core catalog work unchanged.

// Registers the linked core under `file_name` (no folder). The string must
// outlive the program's use of it (a literal).
void libretro_linked_core_register(const char* file_name);
