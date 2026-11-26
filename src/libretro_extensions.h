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
 * turned OFF gets no `done`). The frontend copies the strings during the call.
 * Asking for OFF changes nothing, so a core can ask it at init to know whether
 * the frontend has the extension at all (Test libretro does).
 *
 * Port adapters (decided by Andrés, 2026-11-20): multitaps and the like as
 * adapters on the console's physical ports, not as one more device type that
 * takes a port's place. The core describes its physical ports (PORT 1, PORT 2,
 * the Famicom's EXPANSION...), the devices each takes straight in, and the
 * adapters each takes (a Super Multitap: 4 sub-ports; a Four Score: 4
 * sub-ports over both ports). The frontend picks one adapter (or none) per
 * physical port and tells the core where each one's players are.
 *
 * Players (libretro ports) are numbered by the frontend, in order: physical
 * ports in order, and inside one with an adapter its sub-ports in order; a
 * port without an adapter is one player; a port taken by the adapter before it
 * (RETRO_EXT_ADAPTER_TAKES_NEXT_PORT) has none. A SNES with a multitap on port
 * 2: player 1 on port 1, players 2-5 on the multitap. At most 8 players: an
 * adapter that would go past that isn't connected.
 *
 *   in retro_init (or retro_set_environment):
 *     frontend_drives = environ_cb(RETRO_ENVIRONMENT_EXT_SET_PORT_ADAPTERS, &ports);
 *     if frontend_drives: hide the core's own multitap device types and options
 *
 *   in retro_load_game, optional (a game database, a header field):
 *     unsigned wanted[2] = { 0, SNES_MULTITAP };   (one per physical port, 0 = none)
 *     environ_cb(RETRO_ENVIRONMENT_EXT_SUGGEST_PORT_ADAPTERS, wanted);
 *     the frontend uses it when its user leaves the adapter on AUTO
 *
 *   ports.set_adapters(choices, count) is called by the frontend after
 *   retro_load_game and whenever its user changes an adapter, between
 *   retro_run calls; then retro_set_controller_port_device for every player
 *   of the new layout. The core connects its adapters without reloading the game. */

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
    RETRO_ENVIRONMENT_EXT_SET_TEXT_INPUT = RETRO_ENVIRONMENT_PRIVATE | 0x4D03,

    /* const struct retro_ext_port_adapters *: the core's physical ports and
     * the adapters they take. The arrays must stay valid while the core is
     * loaded. Returns true when the frontend drives the adapters. */
    RETRO_ENVIRONMENT_EXT_SET_PORT_ADAPTERS = RETRO_ENVIRONMENT_PRIVATE | 0x4D04,

    /* const unsigned *: one adapter id per physical port (0 = none), the ones
     * this game wants. Call from retro_load_game. */
    RETRO_ENVIRONMENT_EXT_SUGGEST_PORT_ADAPTERS = RETRO_ENVIRONMENT_PRIVATE | 0x4D05
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


enum
{
    /* The adapter takes this physical port and the next one (a Four Score,
     * an EA 4-Way Play). Only on a port that has a next one. */
    RETRO_EXT_ADAPTER_TAKES_NEXT_PORT = 1u << 0,

    /* In a retro_ext_adapter_choice: this physical port has no player (the
     * adapter on the port before it took it). */
    RETRO_EXT_PORT_UNUSED = 0x7FFFFFFF   /* (an int: C enums before C23) */
};

struct retro_ext_adapter
{
    unsigned id;                                       /* the core's own, never 0 (0 = no adapter) */
    const char *desc;                                  /* "Super Multitap" */
    unsigned sub_ports;                                /* its players: 2..8 */
    unsigned flags;                                    /* RETRO_EXT_ADAPTER_* */
    const struct retro_controller_description *types;  /* what each sub-port takes */
    unsigned num_types;
};

struct retro_ext_physical_port
{
    const char *desc;                                  /* "Port 1", "Expansion" */
    const struct retro_controller_description *types;  /* what plugs straight in; NULL / 0 = a built-in pad */
    unsigned num_types;
    const struct retro_ext_adapter *adapters;          /* NULL / 0 = none */
    unsigned num_adapters;
};

/* What the frontend connected to one physical port. */
struct retro_ext_adapter_choice
{
    unsigned adapter;      /* the adapter's id, 0 = none */
    unsigned first_port;   /* the libretro port of its first sub-port (or of the
                              device plugged straight in); RETRO_EXT_PORT_UNUSED */
};

/* Called by the frontend, between retro_run calls: one choice per physical port. */
typedef void (*retro_ext_set_adapters_t)(const struct retro_ext_adapter_choice *choices, unsigned count);

struct retro_ext_port_adapters
{
    const struct retro_ext_physical_port *ports;
    unsigned num_ports;                                /* 1..4 */
    retro_ext_set_adapters_t set_adapters;             /* required */
};

#endif
