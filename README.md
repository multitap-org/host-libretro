# host-libretro

multitap's libretro host: loads a libretro core and implements the libretro
API (environment, video, audio, input, options, save RAM, savestates) as a
static library, with no SDL and no GUI. Plus `libretro_core_info` (load a core
and report what it does) and the host's tests.

A library: it doesn't build on its own. A frontend (piccolo,
libretro-test-frontend, ...) adds it as a submodule, after hosts-common, and
defines `libretro_api` (libretro.h from libretro-common). With the frontend's
tests on (`MULTITAP_BUILD_TESTS`), adding test-libretro first adds the Test
libretro checks.

MIT License (see LICENSE).
