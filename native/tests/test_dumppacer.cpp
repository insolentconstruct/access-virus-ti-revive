// A single dump on cable 3 goes out in pieces with the notes in between (dumppacer.h, round 0.8.27 L3): the
// original writes 64 bytes every 6-12 ms and sends its notes and clock ticks on cable 5 meanwhile (DSPs only), so
// the Virus' host CPU parses the dump undisturbed and the other parts keep playing (capture, E270).
#include "check.h"
#include "device/dumppacer.h"

#include <deque>
#include <vector>

using namespace vti;

static const int64_t MS = 1000000;

TEST(dumppacer_splits_a_dump_into_paced_pieces)
{
    DumpPacer p;
    CHECK(!p.active());
    std::vector<uint8_t> pk(700, 0x34); // 175 USB-MIDI packets of a 524-byte dump
    p.start(pk, 0);
    CHECK(p.active());
    std::vector<uint8_t> out;
    CHECK(p.next(0, out) && out.size() == 64);  // the first piece at once
    CHECK(!p.next(9 * MS, out));                 // not before 10 ms
    CHECK(p.next(10 * MS, out) && out.size() == 64);
    int pieces = 2;
    for (int64_t t = 20 * MS; p.active(); t += 10 * MS)
        if (p.next(t, out))
            pieces++;
    CHECK(pieces == 11);        // 10 x 64 + 60
    CHECK(out.size() == 60);    // the last piece holds the rest
    CHECK(!p.active());
}

TEST(dumppacer_events_go_on_cable_5_while_a_dump_runs)
{
    DumpPacer p;
    CHECK(p.event_cable(2) == 2);
    std::vector<uint8_t> pk(128, 0x34);
    p.start(pk, 0);
    CHECK(p.event_cable(2) == 5); // notes, clock, Active Sensing: to the DSPs only, the host CPU is in the dump
    std::vector<uint8_t> out;
    p.next(0, out);
    CHECK(p.event_cable(2) == 5);
    p.next(10 * MS, out);
    CHECK(!p.active());
    CHECK(p.event_cable(2) == 2);
    CHECK(p.wait_ns(10 * MS) == 0);
}

TEST(dumppacer_says_how_long_to_wait)
{
    DumpPacer p;
    std::vector<uint8_t> pk(256, 0x34), out;
    p.start(pk, 0);
    p.next(0, out);
    CHECK(p.wait_ns(2 * MS) == 8 * MS);
    CHECK(p.wait_ns(11 * MS) == 0);
}

TEST(dumppacer_holds_parameters_until_the_dump_is_out)
{
    // automation (parameter SysEx from process()) that falls into a dump would go on cable 5 with the notes and
    // reach only the DSPs - the host CPU would not store the value (E274); it waits for the dump's end instead,
    // the latest value per parameter
    HeldParams h;
    const std::vector<uint8_t> cut1 = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x28, 0x10, 0xF7};
    const std::vector<uint8_t> cut2 = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x28, 0x20, 0xF7};
    const std::vector<uint8_t> reso = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x29, 0x30, 0xF7};
    const std::vector<uint8_t> other_part = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x01, 0x28, 0x40, 0xF7};
    CHECK(HeldParams::held(cut1.data(), cut1.size()));
    const uint8_t note[] = {0x90, 60, 100};
    CHECK(!HeldParams::held(note, 3)); // notes go between the pieces
    h.add(cut1);
    h.add(reso);
    h.add(cut2); // replaces cut1
    h.add(other_part);
    auto out = h.take();
    CHECK(out.size() == 3);
    CHECK(out[0] == cut2 && out[1] == reso && out[2] == other_part); // in first-arrival order, latest value
    CHECK(h.take().empty());
}

TEST(dumppacer_holds_only_parameter_pages)
{
    // the sync ping carries its own raw stamp (3072) and the marker follows it in the same block; the transport tick
    // belongs to the F2 before it - neither may wait for the dump's end and lose its stamp (review 2026-10-19)
    const uint8_t ping[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7};
    const uint8_t tick[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x73, 0x00, 0x23, 0x01, 0xF7};
    const uint8_t page_a[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x28, 0x10, 0xF7};
    const uint8_t page_e[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x72, 0x00, 0x05, 0x41, 0xF7};
    const uint8_t page_b6e[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x6E, 0x00, 0x05, 0x41, 0xF7};
    CHECK(!HeldParams::held(ping, sizeof ping));
    CHECK(!HeldParams::held(tick, sizeof tick));
    CHECK(HeldParams::held(page_a, sizeof page_a));
    CHECK(HeldParams::held(page_e, sizeof page_e));
    CHECK(HeldParams::held(page_b6e, sizeof page_b6e));
}

TEST(dumppacer_a_piece_is_the_queues_turn)
{
    // after 8 events in a row the queue side gets its turn - but only when it can go now: a due piece while a dump
    // runs, else a queued message that is not held. Otherwise the events waited for something that could not go
    // (the next piece up to 10 ms, the hold after a multi dump up to 250 ms; review 2026-10-19)
    EventRun run;
    for (int i = 0; i < EventRun::MAX; i++) {
        CHECK(run.events_first(true));
        run.event();
    }
    CHECK(!run.events_first(true)); // the queue's turn
    CHECK(run.events_first(false)); // the queue cannot go: events go on
    run.turn();                     // a piece of a dump (or a queued message) went out
    CHECK(run.events_first(true));
}

TEST(dumppacer_queue_ready_only_when_it_can_go)
{
    DumpPacer p;
    CHECK(!queue_ready(p, 0, true, false));  // nothing queued
    CHECK(queue_ready(p, 0, false, false));  // a queued message
    CHECK(!queue_ready(p, 0, false, true));  // held after a multi dump
    std::vector<uint8_t> pk(256, 0x34), out;
    p.start(pk, 0);
    CHECK(queue_ready(p, 0, false, false));  // the first piece is due
    p.next(0, out);
    CHECK(!queue_ready(p, 5 * MS, false, false)); // the next piece is not due: the queue waits for the dump anyway
    CHECK(queue_ready(p, 10 * MS, true, false));  // due, also with nothing else queued
}

TEST(dumppacer_holds_only_access_parameters)
{
    const uint8_t foreign[] = {0xF0, 0x00, 0x20, 0x29, 0x01, 0x10, 0x70, 0x00, 0x28, 0x10, 0xF7}; // not 00 20 33
    CHECK(!HeldParams::held(foreign, sizeof foreign));
}

TEST(dumppacer_only_queued_notes_go_between_pieces)
{
    // R12-8: like the original's short messages before its dump, a queued note on or off goes between the pieces (on
    // cable 5, the DAW's notes do, E270); a controller or program change on cable 5 would reach the DSPs only (E274),
    // on cable 2 it and any SysEx would land in the host CPU's parse of the dump (E252, R9-5): they wait
    const uint8_t on[] = {0x91, 0x3C, 0x64}, off[] = {0x8F, 0x3C, 0x00}, cc[] = {0xB0, 0x7B, 0x00},
                  pc[] = {0xC0, 0x05}, bend[] = {0xE0, 0x00, 0x40}, at[] = {0xA0, 0x3C, 0x10},
                  sx[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7};
    CHECK(between_pieces(on, sizeof on) && between_pieces(off, sizeof off));
    CHECK(!between_pieces(cc, sizeof cc) && !between_pieces(pc, sizeof pc) && !between_pieces(bend, sizeof bend));
    CHECK(!between_pieces(at, sizeof at) && !between_pieces(sx, sizeof sx) && !between_pieces(on, 2));
}

TEST(dumppacer_holds_controllers_and_pressure_until_the_dump_is_out)
{
    // R4-3: on cable 5 a CC or poly pressure reaches only the DSPs, the host CPU's edit buffer misses it (E274); the
    // original queues CC, poly and channel pressure behind the dump on cable 2. Notes, bend and clock go on; the
    // pedals 64-69 go at once (their timing against the notes), the mode messages 120-127 stay in order with the notes
    const uint8_t cut[] = {0xB2, 40, 10}, mod[] = {0xB0, 1, 64}, poly[] = {0xA3, 41, 7}, at[] = {0xD1, 90},
                  sus[] = {0xB0, 64, 127}, pedal69[] = {0xB0, 69, 0}, off[] = {0xB0, 123, 0}, cc119[] = {0xB0, 119, 1},
                  note[] = {0x90, 60, 100}, bend[] = {0xE0, 0, 64}, f8[] = {0xF8}, pc[] = {0xC0, 5};
    CHECK(HeldParams::held(cut, 3) && HeldParams::held(mod, 3) && HeldParams::held(poly, 3) && HeldParams::held(at, 2));
    CHECK(HeldParams::held(cc119, 3));
    CHECK(!HeldParams::held(sus, 3) && !HeldParams::held(pedal69, 3) && !HeldParams::held(off, 3));
    CHECK(!HeldParams::held(note, 3) && !HeldParams::held(bend, 3));
    CHECK(!HeldParams::held(f8, 1) && !HeldParams::held(pc, 2));
    // the latest value per channel and controller / key / channel pressure, in first-arrival order
    HeldParams h;
    h.add({0xB2, 40, 10});
    h.add({0xB3, 40, 11}); // another channel
    h.add({0xB2, 41, 12}); // another controller
    h.add({0xA2, 40, 13}); // poly pressure: not the same as CC 40
    h.add({0xD2, 20});
    h.add({0xB2, 40, 99});
    h.add({0xD2, 30});
    h.add({0xA2, 40, 14});
    auto out = h.take();
    CHECK(out.size() == 5);
    CHECK(out[0] == std::vector<uint8_t>({0xB2, 40, 99}) && out[1] == std::vector<uint8_t>({0xB3, 40, 11}));
    CHECK(out[2] == std::vector<uint8_t>({0xB2, 41, 12}) && out[3] == std::vector<uint8_t>({0xA2, 40, 14}));
    CHECK(out[4] == std::vector<uint8_t>({0xD2, 30}));
}

TEST(dumppacer_a_part_dump_drops_what_was_held_of_that_part)
{
    // R7-3: a held edit sent after its part's new patch would overwrite it; like the original's slot clear, a part's
    // queued single dump drops what is held of that part so far. Edits held afterwards survive; the standard
    // controllers (mod wheel, volume, ...; not in the patch) and channel pressure stay
    HeldParams h;
    const std::vector<uint8_t> cut0 = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x28, 0x10, 0xF7};
    const std::vector<uint8_t> cut1 = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x01, 0x28, 0x10, 0xF7};
    h.add(cut0);
    h.add(cut1);
    h.add({0xB0, 40, 1});  // a patch parameter as CC, part 1
    h.add({0xA0, 41, 2});  // as poly pressure
    h.add({0xB0, 1, 3});   // mod wheel: kept
    h.add({0xB0, 7, 4});   // volume: kept
    h.add({0xD0, 5});      // channel pressure: kept
    h.add({0xB1, 40, 6});  // part 2
    h.drop_part(0);
    h.add({0xB0, 40, 7}); // after the load: survives
    auto out = h.take();
    CHECK(out.size() == 6);
    CHECK(out[0] == cut1 && out[1] == std::vector<uint8_t>({0xB0, 1, 3}));
    CHECK(out[2] == std::vector<uint8_t>({0xB0, 7, 4}));
    CHECK(out[3] == std::vector<uint8_t>({0xD0, 5}) && out[4] == std::vector<uint8_t>({0xB1, 40, 6}));
    CHECK(out[5] == std::vector<uint8_t>({0xB0, 40, 7}));
    // the single-mode edit buffer (part 0x40): its SysEx only
    const std::vector<uint8_t> single = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x40, 0x28, 0x10, 0xF7};
    h.add(single);
    h.add({0xB0, 40, 8});
    h.drop_part(0x40);
    out = h.take();
    CHECK(out.size() == 1 && out[0] == std::vector<uint8_t>({0xB0, 40, 8}));
}

TEST(dumppacer_knows_a_part_dump)
{
    std::vector<uint8_t> d(524, 0);
    d[0] = 0xF0, d[1] = 0x00, d[2] = 0x20, d[3] = 0x33, d[4] = 0x01, d[5] = 0x10, d[6] = 0x10, d[7] = 0x00, d[8] = 5;
    d[523] = 0xF7;
    CHECK(dump_part(d) == 5);
    d[8] = 0x40;
    CHECK(dump_part(d) == 0x40); // single mode's edit buffer
    d[7] = 0x01;                 // a RAM bank: no edit buffer
    CHECK(dump_part(d) == -1);
    d[7] = 0x00, d[6] = 0x11;    // a multi dump
    CHECK(dump_part(d) == -1);
    CHECK(dump_part({0xB0, 1, 2}) == -1 && dump_part(std::vector<uint8_t>(100, 0xF0)) == -1);
}

namespace {
struct Q {
    std::vector<uint8_t> msg;
    int tag = 0;
    bool held = false; // put back by requeue_held
};
std::vector<uint8_t> dump_of(int part)
{
    std::vector<uint8_t> d(524, 0);
    d[0] = 0xF0, d[1] = 0x00, d[2] = 0x20, d[3] = 0x33, d[4] = 0x01, d[5] = 0x10, d[6] = 0x10, d[8] = (uint8_t)part;
    return d;
}
} // namespace

TEST(dumppacer_held_messages_go_first_but_behind_their_parts_queued_dump)
{
    // at the dump's end the held messages go first (cable 2), in first-arrival order - but one of a part whose single
    // dump still waits in the queue was held after that dump was queued (drop_part), so it goes behind it; in front the
    // new patch would overwrite it (R7-3)
    std::deque<Q> q;
    q.push_back({{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, 1}); // a request
    q.push_back({dump_of(2), 2});
    q.push_back({{0x92, 60, 0}, 3});
    q.push_back({dump_of(2), 4}); // the newest dump of part 3 counts
    q.push_back({{0x92, 61, 0}, 5});
    std::vector<std::vector<uint8_t>> held = {{0xB0, 40, 1}, {0xB2, 40, 2}, {0xB1, 40, 3}, {0xB2, 41, 4}};
    const int n = requeue_held(q, held, [](std::vector<uint8_t> &&m) { return Q{std::move(m), 9, true}; });
    CHECK(n == 4 && q.size() == 9);
    CHECK(q[0].msg == held[0] && q[1].msg == held[2]); // in front, in order
    CHECK(q[2].tag == 1 && q[3].tag == 2 && q[4].tag == 3 && q[5].tag == 4);
    CHECK(q[6].msg == held[1] && q[7].msg == held[3]); // behind part 3's last dump, in order
    CHECK(q[8].tag == 5);
}

TEST(dumppacer_a_newer_value_drops_the_requeued_older_one)
{
    // review of T10: after a dump the held values wait in the queue while up to 8 real-time events go first; a newer
    // value of the same parameter among them went out before the held older one, and the stale value won. The newer
    // one drops the requeued entry of its key (channel + controller, channel + key, part + parameter); other keys,
    // other channels and messages that were not held stay
    const std::vector<uint8_t> sx_old = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x28, 0x10, 0xF7};
    const std::vector<uint8_t> sx_new = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x28, 0x7F, 0xF7};
    const std::vector<uint8_t> sx_part2 = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x01, 0x28, 0x10, 0xF7};
    std::deque<Q> q;
    q.push_back({{0xB0, 40, 1}, 1, true});
    q.push_back({{0xB1, 40, 2}, 2, true});   // another channel
    q.push_back({{0xA0, 40, 3}, 3, true});   // poly pressure, not CC 40
    q.push_back({{0xB0, 40, 4}, 4, false});  // queued by the editor, not held: stays
    q.push_back({sx_old, 5, true});
    q.push_back({sx_part2, 6, true});
    q.push_back({{0xD0, 9}, 7, true});
    const std::vector<uint8_t> cc_new = {0xB0, 40, 99}, at_new = {0xD0, 10}, note = {0x90, 40, 100};
    CHECK(drop_superseded(q, cc_new) == 1);
    CHECK(drop_superseded(q, sx_new) == 1);
    CHECK(drop_superseded(q, at_new) == 1);
    CHECK(drop_superseded(q, note) == 0);
    CHECK(q.size() == 4 && q[0].tag == 2 && q[1].tag == 3 && q[2].tag == 4 && q[3].tag == 6);
}

TEST(dumppacer_a_second_requeue_replaces_the_first_of_the_same_key)
{
    // a dump that starts while the last one's held values still wait in the queue: its own held values are newer, so
    // they replace the older requeued entries of their keys instead of going in front of them
    std::deque<Q> q;
    q.push_back({{0xB0, 40, 1}, 1, true});
    q.push_back({{0xB0, 41, 2}, 2, true});
    std::vector<std::vector<uint8_t>> held = {{0xB0, 40, 50}};
    requeue_held(q, held, [](std::vector<uint8_t> &&m) { return Q{std::move(m), 9, true}; });
    CHECK(q.size() == 2 && q[0].msg == held[0] && q[1].tag == 2);
}
