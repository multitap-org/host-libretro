#ifndef LIBRETRO_MULTITAP_H
#define LIBRETRO_MULTITAP_H

/* Multitap's private environment calls: extensions between Multitap's
 * frontends (piccolo) and the cores built for them. Plain C, for cores to
 * include next to libretro.h. In libretro's RETRO_ENVIRONMENT_PRIVATE range,
 * so other frontends answer false and a core falls back to what it did
 * before.
 *
 * Run-ahead (decided by Andrés, 2026-11-19): a core with its own internal
 * run-ahead (bsnes: "Internal Run-Ahead") lets the frontend drive it, from the
 * frontend's one RUN-AHEAD setting, instead of a core option. Only what
 * toggles it changes: on / off and how many frames.
 *
 *   in retro_init (or retro_set_environment):
 *     unsigned most = 4;
 *     frontend_drives = environ_cb(RETRO_ENVIRONMENT_MULTITAP_SET_RUN_AHEAD, &most);
 *     if frontend_drives: hide (or drop) the core option, it isn't read
 *
 *   in retro_run:
 *     unsigned frames = 0;
 *     if (frontend_drives) environ_cb(RETRO_ENVIRONMENT_MULTITAP_GET_RUN_AHEAD, &frames);
 *     run with `frames` of run-ahead (0 = none), as the core always did
 *
 * The core keeps its own rules (no run-ahead while fast-forwarding, ...). */

#include "libretro.h"

enum
{
    /* const unsigned *: the most frames of internal run-ahead the core does.
     * Returns true when the frontend will drive it: the core then asks with
     * GET_RUN_AHEAD in every retro_run and ignores its own option. */
    RETRO_ENVIRONMENT_MULTITAP_SET_RUN_AHEAD = RETRO_ENVIRONMENT_PRIVATE | 0x4D01,

    /* unsigned *: the frames of run-ahead to do in this retro_run, 0 = none,
     * never more than the core announced. */
    RETRO_ENVIRONMENT_MULTITAP_GET_RUN_AHEAD = RETRO_ENVIRONMENT_PRIVATE | 0x4D02
};

#endif
