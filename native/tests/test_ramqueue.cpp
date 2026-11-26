// RAM writes in the plug-in's send queue (device/ramqueue.h, research T11): a close keeps them and lets them go out
// before its close sequence, within a bound that covers a whole bank; what is cut is named by slot. Hardware-free:
// the queue below has the fields of DeviceOut's Queued.
#include "check.h"
#include "../src/device/dumppacer.h"
#include "../src/device/ramqueue.h"
#include "../src/device/usbmidi.h"

#include <cstdint>
#include <deque>
#include <vector>

using namespace vti;

namespace {
struct Q {
    std::vector<uint8_t> msg;
    bool state = false;
    uint8_t cable = 2;
    bool keep = false;
    bool held = false;
    bool ram = false;
};
std::vector<uint8_t> dump(int bank, int program) // bank 1..4 = RAM, 0 = edit buffer
{
    std::vector<uint8_t> d(524, 0);
    d[0] = 0xF0, d[1] = 0x00, d[2] = 0x20, d[3] = 0x33, d[4] = 0x01, d[5] = 0x10, d[6] = 0x10;
    d[7] = (uint8_t)bank, d[8] = (uint8_t)program, d[523] = 0xF7;
    return d;
}
Q ram(int bank, int program)
{
    Q q{dump(bank, program)};
    q.ram = true;
    return q;
}
const std::vector<uint8_t> STARTUP = {0xF0, 0x01, 0xF7}, EDIT = {0xB0, 0x10, 0x20};
} // namespace

TEST(ramqueue_ram_slot_only_for_ram_banks)
{
    CHECK_EQ(ram_slot(dump(1, 0)), 0);
    CHECK_EQ(ram_slot(dump(4, 127)), 3 * 128 + 127);
    CHECK_EQ(ram_slot(dump(0, 5)), -1); // an edit buffer
    CHECK_EQ(ram_slot(dump(5, 5)), -1); // a ROM bank
    CHECK_EQ(ram_slot(EDIT), -1);
}

TEST(ramqueue_close_keeps_the_ram_writes_in_order)
{
    // a sort cut short by the close: the state, an edit and a start-up block queued around its writes
    std::deque<Q> q = {{STARTUP}, ram(1, 0), {EDIT, true}, ram(1, 1), {STARTUP, false, 3, true}, ram(1, 2)};
    CHECK_EQ(keep_ram_writes(q), 3); // everything else goes, the kept REMOTE record too, as before
    CHECK_EQ(q.size(), 3u);
    CHECK((ram_writes(q) == std::vector<int>{0, 1, 2}));
}

TEST(ramqueue_close_without_ram_writes_drops_all)
{
    std::deque<Q> q = {{STARTUP}, {EDIT, true}, {dump(0, 0)}}; // a part's dump is no RAM write
    CHECK_EQ(keep_ram_writes(q), 3);
    CHECK(q.empty() && ram_writes(q).empty());
}

TEST(ramqueue_only_marked_entries_count)
{
    std::deque<Q> q = {{dump(2, 7)}}; // a RAM dump not sent by write_ram (unmarked) is not kept
    CHECK(ram_writes(q).empty());
    CHECK_EQ(keep_ram_writes(q), 1);
}

TEST(ramqueue_bound_covers_a_whole_bank)
{
    // 128 dumps at the pacer's rate, the original's ~130 ms per dump (E270) too
    const size_t pieces = (usbmidi_pack(dump(1, 0), 3).size() + DumpPacer::PIECE - 1) / DumpPacer::PIECE;
    CHECK_EQ(pieces, 11u);
    const int64_t paced_ms = 128 * (int64_t)pieces * DumpPacer::GAP_NS / 1000000;
    CHECK(paced_ms < RAM_CLOSE_WAIT_MS);
    CHECK(128 * 130 < RAM_CLOSE_WAIT_MS);
    CHECK(RAM_CLOSE_WAIT_MS <= 20000); // a host waits for terminate(): not much longer than that
}

TEST(ramqueue_slot_list_is_short)
{
    CHECK(ram_slot_list({}) == std::string());
    CHECK(ram_slot_list({2}) == std::string("RAM A 3"));
    CHECK(ram_slot_list({5, 2, 3, 4, 9, 128, 129, 300}) == std::string("RAM A 3-6, 10; RAM B 1-2; RAM C 45"));
    CHECK(ram_slot_list({127, 128}) == std::string("RAM A 128; RAM B 1")); // no range across banks
    std::vector<int> bank;
    for (int p = 0; p < 128; p++)
        bank.push_back(p);
    CHECK(ram_slot_list(bank) == std::string("RAM A 1-128"));
}
