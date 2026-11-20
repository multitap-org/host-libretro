// A core linked into the program (libretro_linked_core.h): the test core is
// compiled into this executable, registered under a file name, and then
// loaded, peeked and run through the normal host API by a path that doesn't
// exist on disk (only the file name matters).
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>

#include "dynamic_library.h"
#include "libretro_host.h"
#include "libretro_linked_core.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

int main()
{
    const char* path = "/no/such/folder/test_core_libretro.so";
    libretro_linked_core_register("test_core_libretro.so");

    check(!dynamic_library_is_loaded(path), "not loaded before opening");

    LibretroCoreInfo info = {};
    check(libretro_host_peek_core(path, &info), "peek the linked core");
    check(info.supports_no_game, "peek: it says it runs without a game");

    // ~20 KB of path buffers: on the heap.
    LibretroHost* host = new LibretroHost{};
    LibretroHostConfig config = {};
    bool loaded = libretro_host_load_core(host, path, &config) && libretro_host_load_game(host, nullptr);
    check(loaded, "load the linked core");
    check(dynamic_library_is_loaded(path), "loaded while open");
    LibretroCoreInfo again = {};
    check(!libretro_host_peek_core(path, &again) && again.already_loaded, "peek refused while it runs");
    for (int i = 0; loaded && i < 3; i++)
    {
        check(libretro_host_run_frame(host), "run a frame");
    }
    libretro_host_unload_game(host);
    libretro_host_unload_core(host);
    delete host;

    check(!dynamic_library_is_loaded(path), "not loaded after unloading");
    DynamicLibrary other = {};
    check(!dynamic_library_open(&other, "/no/such/folder/other_libretro.so"), "another file name isn't linked");

    if (g_failures == 0)
    {
        std::printf("linked core: all checks passed\n");
        return 0;
    }
    return 1;
}
