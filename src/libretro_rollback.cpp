#include "libretro_rollback.h"

#include <cstring>

#include "host_log.h"

static uint32_t state_slot(const LibretroRollback* rollback, uint32_t frame)
{
    return frame % (rollback->config.depth + 1);
}

static uint32_t input_slot(uint32_t frame)
{
    return frame % ROLLBACK_INPUT_RING;
}

static uint32_t oldest_unconfirmed(const LibretroRollback* rollback)
{
    uint32_t oldest = rollback->confirmed_until[0];
    for (uint32_t p = 1; p < rollback->config.players; p++)
    {
        if (rollback->confirmed_until[p] < oldest)
        {
            oldest = rollback->confirmed_until[p];
        }
    }
    return oldest;
}

static bool same_input(const RollbackInput* a, const RollbackInput* b)
{
    return a->buttons == b->buttons && a->analog[0][0] == b->analog[0][0] && a->analog[0][1] == b->analog[0][1] &&
           a->analog[1][0] == b->analog[1][0] && a->analog[1][1] == b->analog[1][1];
}

uint64_t libretro_rollback_hash(const uint8_t* data, size_t size)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= data[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

bool libretro_rollback_create(LibretroRollback* rollback, LibretroHost* host, const RollbackConfig* config)
{
    if (config->players == 0 || config->players > ROLLBACK_MAX_PLAYERS || config->depth == 0 ||
        config->depth > ROLLBACK_MAX_DEPTH)
    {
        host_log(LogLevel::error, "Rollback: %u players, depth %u: out of range", config->players, config->depth);
        return false;
    }
    size_t size = libretro_host_state_size(host);
    if (size == 0)
    {
        host_log(LogLevel::error, "Rollback: the core can't save states");
        return false;
    }
    rollback->host = host;
    rollback->config = *config;
    rollback->frame = 0;
    rollback->state_size = size;
    for (uint32_t i = 0; i <= config->depth; i++)
    {
        rollback->states[i].assign(size, 0);   // once: never grown afterwards
        rollback->state_frame[i] = UINT32_MAX;
    }
    for (uint32_t p = 0; p < ROLLBACK_MAX_PLAYERS; p++)
    {
        rollback->confirmed_until[p] = 0;
    }
    rollback->wrong = false;
    rollback->wrong_from = 0;
    rollback->stats = RollbackStats{};
    libretro_host_set_savestate_context(host, RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY);
    host_log(LogLevel::info, "Rollback: %u players, depth %u, state %zu bytes%s", config->players, config->depth, size,
             config->replay_video ? ", replays drawn" : "");
    return true;
}

void libretro_rollback_destroy(LibretroRollback* rollback)
{
    if (rollback->host != nullptr)
    {
        libretro_host_set_hidden(rollback->host, false);
        libretro_host_set_savestate_context(rollback->host, RETRO_SAVESTATE_CONTEXT_NORMAL);
    }
    for (uint32_t i = 0; i <= ROLLBACK_MAX_DEPTH; i++)
    {
        rollback->states[i].clear();
        rollback->states[i].shrink_to_fit();
    }
    rollback->host = nullptr;
}

bool libretro_rollback_add_input(LibretroRollback* rollback, uint32_t player, uint32_t frame, const RollbackInput* input)
{
    if (player >= rollback->config.players)
    {
        return false;
    }
    uint32_t expected = rollback->confirmed_until[player];
    if (frame < expected)
    {
        return true;   // a repeat (each packet carries the last few frames)
    }
    if (frame > expected)
    {
        return false;   // a gap: the frames before it come first
    }
    // The ring holds the frames still needed: from the oldest one a rollback
    // may go back to.
    uint32_t keep_from = rollback->frame > rollback->config.depth ? rollback->frame - rollback->config.depth : 0;
    uint32_t oldest = oldest_unconfirmed(rollback);
    if (oldest < keep_from)
    {
        keep_from = oldest;
    }
    if (frame - keep_from >= ROLLBACK_INPUT_RING)
    {
        return false;
    }
    uint32_t slot = input_slot(frame);
    rollback->confirmed[slot][player] = *input;
    rollback->confirmed_until[player] = frame + 1;
    if (frame < rollback->frame && !same_input(&rollback->used[slot][player], input))
    {
        // That frame ran with a guess that was wrong: replay from it.
        if (!rollback->wrong || frame < rollback->wrong_from)
        {
            rollback->wrong_from = frame;
        }
        rollback->wrong = true;
    }
    return true;
}

bool libretro_rollback_can_advance(const LibretroRollback* rollback)
{
    return rollback->frame < oldest_unconfirmed(rollback) + rollback->config.depth;
}

// The inputs `frame` runs with (confirmed, or the last confirmed one again),
// kept in `used`, and handed to the core.
static void set_frame_input(LibretroRollback* rollback, uint32_t frame)
{
    uint32_t slot = input_slot(frame);
    LibretroInputState state;
    for (uint32_t p = 0; p < rollback->config.players; p++)
    {
        RollbackInput input;
        uint32_t until = rollback->confirmed_until[p];
        if (frame < until)
        {
            input = rollback->confirmed[slot][p];
        }
        else if (until > 0)
        {
            input = rollback->confirmed[input_slot(until - 1)][p];   // predicted: still holding it
        }
        rollback->used[slot][p] = input;
        state.joypad_buttons[p] = input.buttons;
        std::memcpy(state.analog[p], input.analog, sizeof(input.analog));
    }
    libretro_host_set_input(rollback->host, &state);
}

static bool save_state(LibretroRollback* rollback, uint32_t frame)
{
    uint32_t slot = state_slot(rollback, frame);
    size_t size = libretro_host_state_size(rollback->host);
    if (size > rollback->state_size ||
        !libretro_host_serialize_to(rollback->host, rollback->states[slot].data(), rollback->state_size))
    {
        host_log(LogLevel::error, "Rollback: the state of frame %u can't be kept (%zu bytes, room for %zu)", frame, size,
                 rollback->state_size);
        rollback->state_frame[slot] = UINT32_MAX;
        return false;
    }
    rollback->state_frame[slot] = frame;
    return true;
}

bool libretro_rollback_correct(LibretroRollback* rollback)
{
    if (!rollback->wrong)
    {
        return true;
    }
    rollback->wrong = false;
    uint32_t from = rollback->wrong_from;
    uint32_t slot = state_slot(rollback, from);
    if (rollback->state_frame[slot] != from ||
        !libretro_host_unserialize(rollback->host, rollback->states[slot].data(), rollback->state_size))
    {
        host_log(LogLevel::error, "Rollback: no state for frame %u: can't go back", from);
        return false;
    }
    uint32_t back = rollback->frame - from;
    rollback->stats.rollbacks++;
    rollback->stats.frames_replayed += back;
    if (back > rollback->stats.deepest)
    {
        rollback->stats.deepest = back;
    }
    libretro_host_set_hidden(rollback->host, !rollback->config.replay_video);
    bool ok = true;
    for (uint32_t frame = from; frame < rollback->frame && ok; frame++)
    {
        if (frame != from)
        {
            ok = save_state(rollback, frame);   // the replayed timeline replaces the old one
        }
        set_frame_input(rollback, frame);
        ok = ok && libretro_host_run_frame(rollback->host);
    }
    libretro_host_set_hidden(rollback->host, false);
    return ok;
}

bool libretro_rollback_advance(LibretroRollback* rollback)
{
    if (!libretro_rollback_can_advance(rollback))
    {
        rollback->stats.stalls++;
        return false;
    }
    if (!libretro_rollback_correct(rollback) || !save_state(rollback, rollback->frame))
    {
        return false;
    }
    set_frame_input(rollback, rollback->frame);
    if (!libretro_host_run_frame(rollback->host))
    {
        return false;
    }
    rollback->frame++;
    return true;
}

bool libretro_rollback_confirmed_hash(LibretroRollback* rollback, uint32_t* frame, uint64_t* hash)
{
    if (!libretro_rollback_correct(rollback) || rollback->frame == 0)
    {
        return false;
    }
    // The state at the start of frame f holds the result of every frame
    // before it: f's own input needn't be confirmed, only those before.
    uint32_t newest = oldest_unconfirmed(rollback);
    if (newest > rollback->frame - 1)
    {
        newest = rollback->frame - 1;   // the newest start state kept
    }
    uint32_t slot = state_slot(rollback, newest);
    if (rollback->state_frame[slot] != newest)
    {
        return false;
    }
    *frame = newest;
    *hash = libretro_rollback_hash(rollback->states[slot].data(), rollback->state_size);
    return true;
}
