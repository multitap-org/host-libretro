// Checks libretro_rollback.h with the test core (its state mixes both
// ports' buttons every frame, so any input applied to the wrong frame
// changes it):
//   - a reference run with every input on time;
//   - the same inputs through rollback, the second player's arriving 0..6
//     frames late (predicted meanwhile, replayed when they differ): at the
//     end the core's state is byte for byte the reference's;
//   - the confirmed state's hash matches the reference at that frame;
//   - a player more than `depth` frames behind stalls it;
//   - inputs out of order are refused, repeats ignored.
//
// Usage: host_libretro_rollback_test <test core>
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>
#include <vector>

#include "host_log.h"
#include "libretro_host.h"
#include "libretro_rollback.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

constexpr uint32_t FRAMES = 300;

// A player's buttons over time: held for a few frames, then something else
// (like hands on a pad), from a small fixed generator.
static uint16_t buttons_at(uint32_t player, uint32_t frame)
{
    uint32_t span = frame / (3 + player * 2);
    uint32_t x = span * 2654435761u + player * 40503u;
    x ^= x >> 13;
    return static_cast<uint16_t>(x & 0x0FFFu);
}

// How late player 1's input for `frame` arrives (frames), 0..6.
static uint32_t lateness(uint32_t frame)
{
    uint32_t x = frame * 2246822519u;
    x ^= x >> 15;
    return x % 7;
}

static RollbackInput input_of(uint32_t player, uint32_t frame)
{
    RollbackInput input;
    input.buttons = buttons_at(player, frame);
    return input;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("FAIL usage: host_libretro_rollback_test <test core>\n");
        return 1;
    }
    host_log_set_min_level(LogLevel::warning);
    LibretroHost* host = new LibretroHost{};
    LibretroHostConfig config;
    check(libretro_host_load_core(host, argv[1], &config) && libretro_host_load_game(host, nullptr), "test core loaded");
    std::vector<uint8_t> start;
    check(libretro_host_serialize(host, &start), "start state");

    // The reference: every input on time. The state at the start of each
    // frame is hashed (the confirmed hash is compared with these).
    std::vector<uint64_t> reference_hash(FRAMES + 1, 0);
    std::vector<uint8_t> state;
    for (uint32_t frame = 0; frame < FRAMES; frame++)
    {
        libretro_host_serialize(host, &state);
        reference_hash[frame] = libretro_rollback_hash(state.data(), state.size());
        LibretroInputState input;
        input.joypad_buttons[0] = buttons_at(0, frame);
        input.joypad_buttons[1] = buttons_at(1, frame);
        libretro_host_set_input(host, &input);
        libretro_host_run_frame(host);
    }
    std::vector<uint8_t> reference;
    check(libretro_host_serialize(host, &reference), "reference end state");
    reference_hash[FRAMES] = libretro_rollback_hash(reference.data(), reference.size());

    // The same through rollback, player 1 late.
    check(libretro_host_unserialize(host, start.data(), start.size()), "back to the start");
    LibretroRollback* rollback = new LibretroRollback{};
    RollbackConfig rollback_config;
    rollback_config.players = 2;
    rollback_config.depth = 8;
    check(libretro_rollback_create(rollback, host, &rollback_config), "rollback created");

    RollbackInput first = input_of(1, 0);
    check(!libretro_rollback_add_input(rollback, 1, 1, &first), "a gap is refused");
    check(!libretro_rollback_add_input(rollback, 2, 0, &first), "no third player");

    uint32_t delivered = 0;   // player 1's inputs handed over so far (frames 0..delivered-1)
    uint32_t hashes_checked = 0;
    bool hash_ok = true;
    for (uint32_t tick = 0; rollback->frame < FRAMES && tick < FRAMES * 4; tick++)
    {
        // Player 1's inputs whose time has come (in order: a late one holds
        // back the ones after it, as an ordered channel would).
        while (delivered < FRAMES && delivered + lateness(delivered) <= rollback->frame)
        {
            RollbackInput input = input_of(1, delivered);
            check(libretro_rollback_add_input(rollback, 1, delivered, &input), "player 1's input taken");
            delivered++;
        }
        if (delivered > 0)
        {
            RollbackInput again = input_of(1, delivered - 1);
            check(libretro_rollback_add_input(rollback, 1, delivered - 1, &again), "a repeat is ignored");
        }
        // Player 0 (local) at once.
        RollbackInput local = input_of(0, rollback->frame);
        libretro_rollback_add_input(rollback, 0, rollback->frame, &local);
        libretro_rollback_advance(rollback);
        uint32_t frame = 0;
        uint64_t hash = 0;
        if (libretro_rollback_confirmed_hash(rollback, &frame, &hash))
        {
            hashes_checked++;
            hash_ok = hash_ok && frame <= FRAMES && hash == reference_hash[frame];
        }
    }
    check(rollback->frame == FRAMES, "every frame ran");
    while (delivered < FRAMES)
    {
        RollbackInput input = input_of(1, delivered);
        libretro_rollback_add_input(rollback, 1, delivered, &input);
        delivered++;
    }
    check(libretro_rollback_correct(rollback), "the last late inputs replayed");
    std::vector<uint8_t> result;
    check(libretro_host_serialize(host, &result), "rollback end state");
    check(result == reference, "rollback ends exactly where on-time inputs do");
    check(rollback->stats.rollbacks > 0 && rollback->stats.deepest > 0 && rollback->stats.deepest <= 8,
          "it did go back (and never past its depth)");
    check(hashes_checked > 100 && hash_ok, "the confirmed state's hash matches the reference's");
    std::printf("rollbacks %llu, frames replayed %llu, deepest %u\n",
                static_cast<unsigned long long>(rollback->stats.rollbacks),
                static_cast<unsigned long long>(rollback->stats.frames_replayed), rollback->stats.deepest);

    // A player that stops sending: the engine runs `depth` frames on its
    // guess, then stalls.
    uint32_t stalled_at = rollback->frame;
    uint32_t ran = 0;
    for (uint32_t i = 0; i < 20; i++)
    {
        RollbackInput local = input_of(0, rollback->frame);
        libretro_rollback_add_input(rollback, 0, rollback->frame, &local);
        if (libretro_rollback_advance(rollback))
        {
            ran++;
        }
    }
    check(ran == 8 && rollback->frame == stalled_at + 8 && !libretro_rollback_can_advance(rollback),
          "a silent player stalls it after depth frames");
    check(rollback->stats.stalls == 12, "the refused advances counted");
    RollbackInput late = input_of(1, delivered);
    check(libretro_rollback_add_input(rollback, 1, delivered, &late) && libretro_rollback_can_advance(rollback),
          "its input arrives: it goes on");

    libretro_rollback_destroy(rollback);
    delete rollback;
    libretro_host_unload_game(host);
    libretro_host_unload_core(host);
    delete host;
    if (g_failures == 0)
    {
        std::printf("rollback: all checks passed\n");
        return 0;
    }
    return 1;
}
