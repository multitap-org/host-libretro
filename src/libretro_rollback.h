#pragma once

// Rollback netplay's engine, on one LibretroHost (Andrés, 2026-11-21: rollback
// lives in host-libretro; docs/design-notes.md "Network and netplay"). No
// sockets: the frontend carries the inputs between machines and feeds them
// here; this decides what the core runs.
//
// Each frame runs with every player's input for that frame. A player whose
// input hasn't arrived yet is predicted to hold what they last held (the
// last confirmed input). When the real input arrives and differs from what
// was predicted, the state saved at the start of that frame is loaded and
// the frames since are run again, hidden (no picture, no sound; or drawn
// and dropped for cores whose logic needs video, libretro_rollback_config's
// replay_video: snes9x on Donkey Kong Country), before the next frame is
// shown.
//
// The engine never runs more than `depth` frames past the oldest frame some
// player hasn't confirmed: libretro_rollback_can_advance says when the
// frontend must wait (a stall) instead.
//
// Inputs arrive per player in frame order (the network repeats the last few
// frames in each packet, so a lost one costs nothing): a frame already
// confirmed is ignored, one past the next expected is refused. The local
// player adds its own inputs the same way, usually `input_delay` frames
// ahead of the frame about to run.
//
// Desync check: libretro_rollback_confirmed_hash gives a hash of the newest
// state whose frame every player has confirmed; both sides compare theirs
// for the same frame (the frontend sends them).
//
// The states are allocated once, at create (the core's state size then): a
// core whose state grows later fails the next save (logged), and the
// frontend falls back (delay or stream mode). Nothing is allocated per frame.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "libretro_host.h"

constexpr uint32_t ROLLBACK_MAX_PLAYERS = 4;
constexpr uint32_t ROLLBACK_MAX_DEPTH   = 16;   // frames a rollback may go back
constexpr uint32_t ROLLBACK_INPUT_RING  = 64;   // frames of input kept (> depth + delay + what arrives early)

// One player's input for one frame: a RetroPad (Andrés: "phone maps, sends
// RetroPad"; the same for netplay).
struct RollbackInput
{
    uint16_t buttons = 0;          // bit N = RETRO_DEVICE_ID_JOYPAD_N
    int16_t  analog[2][2] = {};    // [left/right stick][x/y]
};

struct RollbackConfig
{
    uint32_t players = 2;          // ports 0..players-1, one player each
    uint32_t depth = 8;            // 1..ROLLBACK_MAX_DEPTH
    bool     replay_video = false; // replays drawn (and dropped) instead of hidden
};

struct RollbackStats
{
    uint64_t rollbacks = 0;        // times a misprediction sent it back
    uint64_t frames_replayed = 0;
    uint32_t deepest = 0;          // frames gone back at most
    uint64_t stalls = 0;           // advances refused: a player too far behind
};

struct LibretroRollback
{
    LibretroHost*  host = nullptr;
    RollbackConfig config;
    uint32_t       frame = 0;      // the next frame to run (frames count from 0 at create)

    // The state at the start of frame f lives in states[f % (depth + 1)].
    std::vector<uint8_t> states[ROLLBACK_MAX_DEPTH + 1];
    uint32_t       state_frame[ROLLBACK_MAX_DEPTH + 1] = {};
    size_t         state_size = 0;

    // Inputs by frame (slot f % ROLLBACK_INPUT_RING): what each player
    // confirmed, and what the frame ran with (predicted or confirmed).
    RollbackInput  confirmed[ROLLBACK_INPUT_RING][ROLLBACK_MAX_PLAYERS];
    RollbackInput  used[ROLLBACK_INPUT_RING][ROLLBACK_MAX_PLAYERS];
    uint32_t       confirmed_until[ROLLBACK_MAX_PLAYERS] = {};   // every frame below is confirmed for that player
    bool           wrong = false;  // a prediction turned out wrong: replay from `wrong_from`
    uint32_t       wrong_from = 0;

    RollbackStats  stats;
};

// Starts at the core's current state, as frame 0. False (logged) if the core
// can't save states, the config is out of range, or no game runs.
bool libretro_rollback_create(LibretroRollback* rollback, LibretroHost* host, const RollbackConfig* config);
void libretro_rollback_destroy(LibretroRollback* rollback);

// A player's input for `frame` (local or from the network). Returns false
// for a frame past the next one expected from that player, or too far ahead
// of what runs; true (and ignored) for one already confirmed.
bool libretro_rollback_add_input(LibretroRollback* rollback, uint32_t player, uint32_t frame, const RollbackInput* input);

// Whether the next frame may run: no player is `depth` frames or more behind.
bool libretro_rollback_can_advance(const LibretroRollback* rollback);

// Replays what a late input changed (if anything), then runs the next frame
// shown. False if it may not advance (counted as a stall) or the core failed.
bool libretro_rollback_advance(LibretroRollback* rollback);

// Only the replay: brings the frames already run in line with every input
// confirmed so far, without running a new one (advance does it first).
bool libretro_rollback_correct(LibretroRollback* rollback);

// The newest frame every player has confirmed whose start state is still
// kept, and a hash of that state (FNV-1a 64 over the core's state). False if
// none is.
bool libretro_rollback_confirmed_hash(LibretroRollback* rollback, uint32_t* frame, uint64_t* hash);

// FNV-1a 64 of `size` bytes (the desync check's hash; also for tests).
uint64_t libretro_rollback_hash(const uint8_t* data, size_t size);
