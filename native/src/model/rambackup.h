// rambackup.h - TI Revive's own addition (write policy of 2026-10-31: a feature that overwrites stored data backs
// it up first). Before a SAVE or a browser operation (drop, swap, rename, sort, undo) writes RAM slots of the Virus,
// the patches the writes would lose go into a personal library of the day, "RAM Backup 2026-10-31" (" 2", " 3", ...
// once one holds 128 patches, the most a library file addresses), so the browser can drag them back. A patch still
// in the bank after one write, or written again unchanged, is not lost; several writes back up every slot they
// overwrite (to_back_up, T11). What is lost comes from the bank cache: a slot the cache does not know has nothing to
// back up. Header only, no files.
#pragma once
#include "model/bankdrop.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace vti {

class RamBackup {
public:
    using Patch = std::vector<uint8_t>;
    static constexpr int kPerLibrary = 128;

    // the same patch, wherever it is stored: everything but the address (bytes 7, 8), the flags (byte 10: 0x20 after
    // a section lock merge, E378) and the two checksums (265, 522; a merge leaves them stale)
    static bool same_sound(const Patch &a, const Patch &b)
    {
        if (!BankDrop::is_single(a) || !BankDrop::is_single(b))
            return false;
        for (int i = 11; i < 522; i++)
            if (i != 265 && a[i] != b[i])
                return false;
        return a[9] == b[9];
    }

    // the patches `writes` would lose: a slot's patch in `before` (bank -> its 128 slots, the cache) that is not
    // in its bank any more once the writes are done; each patch once, in the order of the writes
    static std::vector<Patch> lost(const std::vector<BankDrop::Write> &writes, const std::map<int, std::vector<Patch>> &before)
    {
        std::map<int, std::vector<Patch>> after = before;
        for (const auto &w : writes)
            if (after.count(w.bank) && w.slot >= 0 && w.slot < (int)after[w.bank].size())
                after[w.bank][w.slot] = w.dump;
        std::vector<Patch> out;
        for (const auto &w : writes) {
            auto it = before.find(w.bank);
            if (it == before.end() || w.slot < 0 || w.slot >= (int)it->second.size())
                continue;
            const Patch &old = it->second[w.slot];
            if (!BankDrop::is_single(old) || contains(after[w.bank], old) || contains(out, old))
                continue;
            out.push_back(old);
        }
        return out;
    }

    // what the backup before `writes` holds (research T11, R7-1): one write (a SAVE, a rename) backs up what lost()
    // names; several (drop, swap, sort, undo) back up the old patch of every slot they overwrite with another sound,
    // one still in the bank afterwards included - cut short (plug-in closed, broker gone), a swap or sort leaves a
    // moved patch nowhere. Each patch once; a slot the cache does not know has nothing to back up
    static std::vector<Patch> to_back_up(const std::vector<BankDrop::Write> &writes,
                                         const std::map<int, std::vector<Patch>> &before)
    {
        if (writes.size() <= 1)
            return lost(writes, before);
        std::vector<Patch> out;
        for (const auto &w : writes) {
            auto it = before.find(w.bank);
            if (it == before.end() || w.slot < 0 || w.slot >= (int)it->second.size())
                continue;
            const Patch &old = it->second[w.slot];
            if (!BankDrop::is_single(old) || same_sound(old, w.dump) || contains(out, old))
                continue;
            out.push_back(old);
        }
        return out;
    }

    // Before lost(): the written slots whose cached patch may be older than the slot are read from the Virus with
    // `read` (bank 0..3, slot; empty = no answer) into `before` (research T2: R2-1, R7-5). Which ones, the smallest
    // rule that never backs up a patch the Virus does not hold:
    //  - a bank this session's sync did not confirm (`confirmed` false): its cache may be from an earlier session,
    //    so every written slot is read, and its other slots are emptied - lost() must not count a patch as still in
    //    the bank because a stale entry says so
    //  - one write (a SAVE, a rename): always read, a patch stored on the Virus' panel since the sync is not in the
    //    cache (one ~1 s request)
    //  - several writes (drop, swap, sort, undo): only the slots whose cached patch lost() names, plus those the cache
    //    does not know (given up, E594). A slot changed on the panel since the sync whose cached patch stays in the
    //    bank is not read: rare, and a sort would otherwise read the whole bank (two minutes); to_back_up() takes
    //    those slots from the confirmed cache (T11)
    // false: a needed slot could not be read (`failed` = that write), the caller must not write.
    static bool refresh(const std::vector<BankDrop::Write> &writes, std::map<int, std::vector<Patch>> &before,
                        const std::function<bool(int)> &confirmed, const std::function<Patch(int, int)> &read,
                        BankDrop::Write *failed = nullptr)
    {
        const std::vector<Patch> cached_lost = lost(writes, before);
        std::map<int, bool> trusted;
        for (const auto &w : writes)
            if (w.slot >= 0 && w.slot < BankDrop::SLOTS && !trusted.count(w.bank)) {
                trusted[w.bank] = confirmed(w.bank);
                auto &v = before[w.bank];
                v.resize(BankDrop::SLOTS);
                if (!trusted[w.bank])
                    for (auto &p : v)
                        p.clear();
            }
        std::map<int, std::vector<bool>> done;
        for (const auto &w : writes) {
            if (w.slot < 0 || w.slot >= BankDrop::SLOTS)
                continue;
            auto &v = before[w.bank];
            auto &d = done[w.bank];
            d.resize(BankDrop::SLOTS);
            const bool needed = !trusted[w.bank] || writes.size() == 1 || !BankDrop::is_single(v[w.slot]) ||
                                contains(cached_lost, v[w.slot]);
            if (!needed || d[w.slot])
                continue;
            Patch fresh = read(w.bank, w.slot);
            if (!BankDrop::is_single(fresh)) {
                if (failed)
                    *failed = w;
                return false;
            }
            v[w.slot] = std::move(fresh);
            d[w.slot] = true;
        }
        return true;
    }

    // a rename made from the cached patch: when refresh() read another patch from that slot (`fresh`), the rename is
    // applied to that one, so only the name changes on the Virus (research T2)
    static void rename_fresh(BankDrop::Write &w, const Patch &cached, const Patch &fresh, const std::string &name)
    {
        if (!BankDrop::is_single(fresh) || same_sound(fresh, cached))
            return;
        Patch d = BankDrop::renamed(fresh, name);
        BankDrop::address(d, w.bank, w.slot);
        w.dump = std::move(d);
    }

    // the day's library: "RAM Backup <date>", the n-th one (n >= 2) with " n" after it
    static std::string library_name(const std::string &date, int n)
    {
        return "RAM Backup " + date + (n >= 2 ? " " + std::to_string(n) : std::string());
    }

    struct Target {
        std::string name; // library name (file name without ".mid")
        int count;        // patches to append to it
    };
    // where `n` patches go: the day's libraries in order, each filled up to kPerLibrary; `count_of` tells how many a
    // library of that name holds already (0 = none yet)
    static std::vector<Target> plan(const std::string &date, const std::function<int(const std::string &)> &count_of, int n)
    {
        std::vector<Target> out;
        for (int k = 1; n > 0 && k < 1000; k++) {
            const std::string name = library_name(date, k);
            const int room = kPerLibrary - count_of(name);
            if (room <= 0)
                continue;
            const int take = room < n ? room : n;
            out.push_back({name, take});
            n -= take;
        }
        return out;
    }

private:
    static bool contains(const std::vector<Patch> &v, const Patch &p)
    {
        for (const auto &x : v)
            if (same_sound(x, p))
                return true;
        return false;
    }
};

} // namespace vti
