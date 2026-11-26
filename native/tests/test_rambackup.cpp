// RAM backup (model/rambackup.h, TI Revive's own addition, write policy 2026-10-31): before a SAVE, a browser drop,
// swap, rename, sort or undo writes RAM slots of the Virus, the patches it would lose go into a personal library of
// the day ("RAM Backup 2026-10-31", then " 2", ... at 128 patches), so the browser can drag them back.
// Hardware-free.
#include "check.h"
#include "model/rambackup.h"

using namespace vti;
using Patch = RamBackup::Patch;

static Patch single(int bank, int program, uint8_t sound)
{
    Patch d(524, 0);
    d[0] = 0xF0, d[1] = 0x00, d[2] = 0x20, d[3] = 0x33, d[4] = 0x01, d[5] = 0x10, d[6] = 0x10;
    d[523] = 0xF7;
    d[7] = (uint8_t)bank, d[8] = (uint8_t)program, d[9] = 0x0C;
    for (int i = 11; i < 265; i++)
        d[i] = (uint8_t)((sound + i) & 0x7F);
    for (int i = 266; i < 522; i++)
        d[i] = (uint8_t)((sound * 3 + i) & 0x7F);
    BankDrop::checksums(d);
    return d;
}

static std::map<int, std::vector<Patch>> bank_a(int n)
{
    std::map<int, std::vector<Patch>> b;
    b[0].resize(128);
    for (int p = 0; p < n; p++)
        b[0][p] = single(1, p, (uint8_t)p);
    return b;
}

TEST(rambackup_same_sound_ignores_address_and_checksums)
{
    Patch a = single(1, 5, 7), b = single(3, 99, 7), c = single(1, 5, 8);
    CHECK(RamBackup::same_sound(a, b)); // another slot, the same patch
    CHECK(!RamBackup::same_sound(a, c));
    b[265] ^= 1; // a stale checksum (E378 merge leaves them) is no other sound
    CHECK(RamBackup::same_sound(a, b));
    CHECK(!RamBackup::same_sound(a, Patch(10, 0)));
}

TEST(rambackup_overwritten_patch_is_lost)
{
    auto before = bank_a(4);
    std::vector<BankDrop::Write> w = {{0, 2, single(1, 2, 100)}};
    auto lost = RamBackup::lost(w, before);
    CHECK(lost.size() == 1);
    CHECK(RamBackup::same_sound(lost[0], before[0][2]));
}

TEST(rambackup_same_patch_again_loses_nothing)
{
    auto before = bank_a(4);
    std::vector<BankDrop::Write> w = {{0, 2, single(1, 2, 2)}}; // stored again unchanged
    CHECK(RamBackup::lost(w, before).empty());
}

TEST(rambackup_swap_within_the_bank_loses_nothing)
{
    auto before = bank_a(4);
    std::vector<BankDrop::Write> w = {{0, 0, single(1, 0, 1)}, {0, 1, single(1, 1, 0)}};
    CHECK(RamBackup::lost(w, before).empty()); // both patches are still in the bank afterwards
}

TEST(rambackup_empty_or_unknown_slots_have_nothing_to_lose)
{
    auto before = bank_a(2);
    std::vector<BankDrop::Write> w = {{0, 50, single(1, 50, 9)}, {2, 0, single(3, 0, 9)}};
    CHECK(RamBackup::lost(w, before).empty()); // slot 50 empty, RAM C not in the cache
}

TEST(rambackup_each_lost_patch_once)
{
    auto before = bank_a(4);
    before[0][3] = single(1, 3, 1); // slot 3 holds the same patch as slot 1
    std::vector<BankDrop::Write> w = {{0, 1, single(1, 1, 90)}, {0, 3, single(1, 3, 91)}};
    auto lost = RamBackup::lost(w, before);
    CHECK(lost.size() == 1);
}

TEST(rambackup_library_names)
{
    CHECK(RamBackup::library_name("2026-10-31", 1) == "RAM Backup 2026-10-31");
    CHECK(RamBackup::library_name("2026-10-31", 2) == "RAM Backup 2026-10-31 2");
}

TEST(rambackup_plan_fills_the_day_library_then_the_next)
{
    std::map<std::string, int> have = {{"RAM Backup 2026-10-31", 120}};
    auto count = [&](const std::string &n) {
        auto it = have.find(n);
        return it == have.end() ? 0 : it->second;
    };
    auto plan = RamBackup::plan("2026-10-31", count, 20);
    CHECK(plan.size() == 2);
    CHECK(plan[0].name == "RAM Backup 2026-10-31" && plan[0].count == 8);
    CHECK(plan[1].name == "RAM Backup 2026-10-31 2" && plan[1].count == 12);
    have["RAM Backup 2026-10-31"] = 128;
    plan = RamBackup::plan("2026-10-31", count, 1);
    CHECK(plan.size() == 1 && plan[0].name == "RAM Backup 2026-10-31 2" && plan[0].count == 1);
    CHECK(RamBackup::plan("2026-10-31", count, 0).empty());
}

// T2 (research R2-1, R7-5): the backup of a slot about to be overwritten comes from the Virus when the cache may be
// older than the slot. refresh() reads those slots with `read` (the plug-in: MemorySync::single) into `before`.
struct Reads {
    std::map<std::pair<int, int>, Patch> virus; // what the Virus holds
    std::vector<std::pair<int, int>> asked;
    std::function<Patch(int, int)> fn()
    {
        return [this](int bank, int slot) {
            asked.push_back({bank, slot});
            auto it = virus.find({bank, slot});
            return it == virus.end() ? Patch() : it->second;
        };
    }
};

TEST(rambackup_unconfirmed_bank_backs_up_what_the_virus_has)
{
    // RAM C was not reached by a failed sync: its cache is from an earlier session, slot 5 changed since
    std::map<int, std::vector<Patch>> before;
    before[2].resize(128);
    for (int p = 0; p < 128; p++)
        before[2][p] = single(3, p, (uint8_t)p);
    Reads r;
    r.virus[{2, 5}] = single(3, 5, 77); // stored on the panel / by another computer
    std::vector<BankDrop::Write> w = {{2, 5, single(3, 5, 100)}, {2, 6, single(3, 6, 101)}};
    r.virus[{2, 6}] = single(3, 6, 6);
    CHECK(RamBackup::refresh(w, before, [](int) { return false; }, r.fn()));
    CHECK(r.asked.size() == 2); // every written slot of an unconfirmed bank
    auto lost = RamBackup::lost(w, before);
    CHECK(lost.size() == 2);
    CHECK(RamBackup::same_sound(lost[0], single(3, 5, 77))); // the real patch, not the stale cached one
    CHECK(RamBackup::same_sound(lost[1], single(3, 6, 6)));
}

TEST(rambackup_unconfirmed_bank_does_not_trust_its_other_slots)
{
    // the stale cache says slot 9 holds the patch slot 5 really has: lost() must not count it as kept
    std::map<int, std::vector<Patch>> before;
    before[1].resize(128);
    before[1][9] = single(2, 9, 77);
    Reads r;
    r.virus[{1, 5}] = single(2, 5, 77);
    std::vector<BankDrop::Write> w = {{1, 5, single(2, 5, 1)}};
    CHECK(RamBackup::refresh(w, before, [](int) { return false; }, r.fn()));
    auto lost = RamBackup::lost(w, before);
    CHECK(lost.size() == 1 && RamBackup::same_sound(lost[0], single(2, 5, 77)));
}

TEST(rambackup_slot_that_cannot_be_read_refuses_the_write)
{
    auto before = bank_a(4);
    Reads r; // the Virus answers nothing
    BankDrop::Write failed{-1, -1, {}};
    std::vector<BankDrop::Write> w = {{0, 1, single(1, 1, 50)}};
    CHECK(!RamBackup::refresh(w, before, [](int) { return true; }, r.fn(), &failed));
    CHECK(failed.bank == 0 && failed.slot == 1);
    failed = {-1, -1, {}};
    CHECK(!RamBackup::refresh(w, before, [](int) { return false; }, r.fn(), &failed));
    CHECK(failed.bank == 0 && failed.slot == 1);
}

TEST(rambackup_single_save_reads_the_slot_even_in_a_confirmed_bank)
{
    // R7-5: a patch stored on the Virus' panel after the sync is not in the cache
    auto before = bank_a(4);
    Reads r;
    r.virus[{0, 2}] = single(1, 2, 66);
    std::vector<BankDrop::Write> w = {{0, 2, single(1, 2, 100)}};
    CHECK(RamBackup::refresh(w, before, [](int) { return true; }, r.fn()));
    CHECK(r.asked.size() == 1);
    auto lost = RamBackup::lost(w, before);
    CHECK(lost.size() == 1 && RamBackup::same_sound(lost[0], single(1, 2, 66)));
}

TEST(rambackup_confirmed_multi_write_reads_only_what_is_lost)
{
    auto before = bank_a(4);
    Reads r;
    for (int p = 0; p < 128; p++)
        r.virus[{0, p}] = single(1, p, (uint8_t)p);
    // a swap keeps both patches in the bank: nothing to back up, nothing read
    std::vector<BankDrop::Write> swap = {{0, 0, single(1, 0, 1)}, {0, 1, single(1, 1, 0)}};
    CHECK(RamBackup::refresh(swap, before, [](int) { return true; }, r.fn()));
    CHECK(r.asked.empty());
    // slot 3's patch moves to slot 2 (still in the bank), slot 2's is lost: only slot 2 is read; the empty (given-up,
    // unknown) slot 60 is read too
    std::vector<BankDrop::Write> drop = {{0, 2, single(1, 2, 3)}, {0, 3, single(1, 3, 90)}, {0, 60, single(1, 60, 91)}};
    CHECK(RamBackup::refresh(drop, before, [](int) { return true; }, r.fn()));
    CHECK(r.asked.size() == 2 && r.asked[0] == std::make_pair(0, 2) && r.asked[1] == std::make_pair(0, 60));
}

// T2: a rename made from the cached patch renames what the Virus holds when the fresh read differs, so only the name
// changes on the Virus
static std::string name_of(const Patch &d)
{
    return std::string(d.begin() + 249, d.begin() + 259);
}

TEST(rambackup_rename_uses_the_fresh_patch)
{
    const Patch cached = single(1, 4, 10), fresh = single(1, 4, 55);
    BankDrop::Write w{0, 4, BankDrop::renamed(cached, "NewName")};
    BankDrop::address(w.dump, 0, 4);
    RamBackup::rename_fresh(w, cached, fresh, "NewName");
    CHECK(RamBackup::same_sound(w.dump, BankDrop::renamed(fresh, "NewName")));
    CHECK(name_of(w.dump) == "NewName   ");
    CHECK(w.dump[7] == 1 && w.dump[8] == 4); // RAM A 5
    // the Virus holds what the cache has: the rename stays as it was
    const Patch was = w.dump;
    BankDrop::Write same{0, 4, BankDrop::renamed(cached, "Other")};
    BankDrop::address(same.dump, 0, 4);
    const Patch before_same = same.dump;
    RamBackup::rename_fresh(same, cached, cached, "Other");
    CHECK(same.dump == before_same);
    RamBackup::rename_fresh(w, cached, Patch(), "NewName"); // nothing read: unchanged
    CHECK(w.dump == was);
}
