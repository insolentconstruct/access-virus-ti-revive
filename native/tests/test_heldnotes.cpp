// What still sounds when the host deactivates the plug-in (device/heldnotes.h, E373): only held keys and pressed
// sustain pedals are released, per channel - not sustain off + all notes off on all 16 channels, which REAPER's
// deactivation at every Play put in front of its first notes (a Virus sync report at every Play). Hardware-free.
#include "check.h"
#include "../src/device/heldnotes.h"

using vti::HeldNotes;

TEST(heldnotes_nothing_held_nothing_sent)
{
    HeldNotes h;
    uint8_t m[32][3];
    CHECK(h.release(m) == 0);
    h.note_on(0, 60);
    h.note_off(0, 60);
    h.sustain(1, 127);
    h.sustain(1, 0);
    CHECK(h.release(m) == 0); // all released by the host already (REAPER at Stop)
}

TEST(heldnotes_releases_held_keys_and_pedals_per_channel)
{
    HeldNotes h;
    uint8_t m[32][3];
    h.note_on(2, 60);
    h.note_on(2, 127);
    h.note_on(15, 0);
    h.sustain(5, 64); // pedal down from 64 on
    int n = h.release(m);
    CHECK(n == 3);
    // per channel in order: pedal up first, then all notes off
    CHECK(m[0][0] == 0xB2 && m[0][1] == 123 && m[0][2] == 0);
    CHECK(m[1][0] == 0xB5 && m[1][1] == 64 && m[1][2] == 0);
    CHECK(m[2][0] == 0xBF && m[2][1] == 123 && m[2][2] == 0);
    CHECK(h.release(m) == 0); // released: nothing left
}

TEST(heldnotes_pedal_and_keys_on_one_channel)
{
    HeldNotes h;
    uint8_t m[32][3];
    h.note_on(0, 1);
    h.sustain(0, 100);
    int n = h.release(m);
    CHECK(n == 2);
    CHECK(m[0][0] == 0xB0 && m[0][1] == 64 && m[0][2] == 0);
    CHECK(m[1][0] == 0xB0 && m[1][1] == 123 && m[1][2] == 0);
}

// Panic (TI Revive's own addition): sustain off + all notes off on every channel, held or not - the messages
// setActive(false) sent before E373, which the Virus took (2026-10-27); forgets what was held
TEST(heldnotes_panic_all_channels)
{
    HeldNotes h;
    uint8_t m[32][3];
    h.note_on(3, 64);
    h.sustain(9, 127);
    CHECK(HeldNotes::panic(m) == 32);
    for (int ch = 0; ch < 16; ch++) {
        CHECK(m[2 * ch][0] == 0xB0 + ch && m[2 * ch][1] == 64 && m[2 * ch][2] == 0);
        CHECK(m[2 * ch + 1][0] == 0xB0 + ch && m[2 * ch + 1][1] == 123 && m[2 * ch + 1][2] == 0);
    }
    h.forget();
    CHECK(h.release(m) == 0); // nothing left to release after a panic
}

// A short USB drop (research R6-4): a Virus that stays powered keeps a note whose note-off fell in the gap, as
// process() drops its events while the Virus is away. What was held when it went is released at its return, with
// the same messages as release() (pedal up, all notes off), once per absence.
TEST(heldnotes_released_at_the_return_after_a_gap)
{
    HeldNotes h;
    uint8_t m[32][3];
    h.note_on(4, 60);
    h.sustain(7, 127);
    h.away();          // the Virus is gone
    h.note_off(4, 60); // the host lets go during the gap: this note-off never reaches the Virus
    h.sustain(7, 0);
    int n = h.returned(m);
    CHECK(n == 2);
    CHECK(m[0][0] == 0xB4 && m[0][1] == 123 && m[0][2] == 0);
    CHECK(m[1][0] == 0xB7 && m[1][1] == 64 && m[1][2] == 0);
    CHECK(h.returned(m) == 0); // once: a second return without a new absence sends nothing
}

TEST(heldnotes_return_without_held_notes_sends_nothing)
{
    HeldNotes h;
    uint8_t m[32][3];
    h.away();
    h.note_on(1, 50); // played during the gap: never reached the Virus, its note-off later is harmless
    CHECK(h.returned(m) == 0);
    CHECK(h.release(m) == 1); // still tracked for a later deactivation
}

TEST(heldnotes_bounce_releases_at_once)
{
    // gone and back between two polls (E212): away() and returned() in one go
    HeldNotes h;
    uint8_t m[32][3];
    h.note_on(0, 60);
    h.away();
    CHECK(h.returned(m) == 1);
    CHECK(m[0][0] == 0xB0 && m[0][1] == 123);
}
