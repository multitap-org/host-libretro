#ifndef LIBRETRO_EXTENSIONS_H
#define LIBRETRO_EXTENSIONS_H

/* libretro extensions: private environment calls between the Multitap
 * project's frontends (piccolo) and the cores built for them (RETRO_ENVIRONMENT_EXT_*;
 * "Multitap" is the project, not the controller multitap: an extension
 * about ports will come too). Plain C, for cores to include next to
 * libretro.h. In libretro's RETRO_ENVIRONMENT_PRIVATE range, so other
 * frontends answer false and a core falls back to what it did before.
 *
 * Run-ahead (decided by Andrés, 2026-11-19): a core with its own internal
 * run-ahead (bsnes: "Internal Run-Ahead") lets the frontend drive it, from the
 * frontend's one RUN-AHEAD setting, instead of a core option. Only what
 * toggles it changes: on / off and how many frames.
 *
 *   in retro_init (or retro_set_environment):
 *     unsigned most = 4;
 *     frontend_drives = environ_cb(RETRO_ENVIRONMENT_EXT_SET_RUN_AHEAD, &most);
 *     if frontend_drives: hide (or drop) the core option, it isn't read
 *
 *   in retro_run:
 *     unsigned frames = 0;
 *     if (frontend_drives) environ_cb(RETRO_ENVIRONMENT_EXT_GET_RUN_AHEAD, &frames);
 *     run with `frames` of run-ahead (0 = none), as the core always did
 *
 * The core keeps its own rules (no run-ahead while fast-forwarding, ...).
 *
 * Text input (decided by Andrés, 2026-11-20): a core asks the frontend for the
 * keyboard, so a phone can open its on-screen keyboard only when the game
 * wants one. Two modes:
 *
 *   KEYS: "I need the keyboard now" (a DOS prompt, a computer's BASIC). Keys
 *     and characters arrive as always: the keyboard callback and
 *     RETRO_DEVICE_KEYBOARD. A desktop changes nothing visible; a phone opens
 *     its keyboard. The core turns it OFF when it's done.
 *   TEXT: "give me a string" (a name, a password, a code). The frontend shows
 *     its own text box and answers through `done` with the whole string. The
 *     frontend may pause the game while its box is up (piccolo does); don't
 *     rely on frames running, or not, until `done` comes.
 *
 *   struct retro_ext_text_input request = { RETRO_EXT_TEXT_INPUT_TEXT,
 *       RETRO_EXT_TEXT_INPUT_TYPE_TEXT, "NAME", "LINK", 8, name_done };
 *   if (!environ_cb(RETRO_ENVIRONMENT_EXT_SET_TEXT_INPUT, &request))
 *       fall back to what the core did before (its own on-screen letters, ...)
 *
 * A new request replaces the one before (a TEXT request that is replaced or
 * turned OFF gets no `done`). The frontend copies the strings during the call. */

#include "libretro.h"

enum
{
    /* const unsigned *: the most frames of internal run-ahead the core does.
     * Returns true when the frontend will drive it: the core then asks with
     * GET_RUN_AHEAD in every retro_run and ignores its own option. */
    RETRO_ENVIRONMENT_EXT_SET_RUN_AHEAD = RETRO_ENVIRONMENT_PRIVATE | 0x4D01,

    /* unsigned *: the frames of run-ahead to do in this retro_run, 0 = none,
     * never more than the core announced. */
    RETRO_ENVIRONMENT_EXT_GET_RUN_AHEAD = RETRO_ENVIRONMENT_PRIVATE | 0x4D02,

    /* const struct retro_ext_text_input *: the keyboard the core wants now
     * (mode OFF = none). Returns true when the frontend takes the request. */
    RETRO_ENVIRONMENT_EXT_SET_TEXT_INPUT = RETRO_ENVIRONMENT_PRIVATE | 0x4D03
};

enum retro_ext_text_input_mode
{
    RETRO_EXT_TEXT_INPUT_OFF  = 0,   /* the core doesn't need the keyboard */
    RETRO_EXT_TEXT_INPUT_KEYS = 1,   /* keys as usual; open the on-screen keyboard */
    RETRO_EXT_TEXT_INPUT_TEXT = 2    /* ask for a string; the answer comes through `done` */
};

/* What the text is, so a phone shows the right keyboard. */
enum retro_ext_text_input_type
{
    RETRO_EXT_TEXT_INPUT_TYPE_TEXT   = 0,
    RETRO_EXT_TEXT_INPUT_TYPE_NUMBER = 1
};

/* Called by the frontend between retro_run calls (never inside one) with the
 * string typed, UTF-8, valid only during the call; NULL if the user cancelled. */
typedef void (*retro_ext_text_input_done_t)(const char *text);

struct retro_ext_text_input
{
    unsigned mode;                     /* enum retro_ext_text_input_mode */
    unsigned type;                     /* enum retro_ext_text_input_type */
    const char *prompt;                /* TEXT: what is asked ("NAME"); may be NULL */
    const char *initial;               /* TEXT: the text to start from; may be NULL */
    unsigned max_length;               /* TEXT: most characters (not bytes); 0 = the frontend's limit */
    retro_ext_text_input_done_t done;  /* TEXT: required; KEYS / OFF: ignored */
};

#endif
