// ramqueue.h - RAM writes in the plug-in's send queue (research T11, R7-1). A drop, swap or sort writes up to 128 RAM
// slots one dump after the other (~0.1 s each at the dump pacer's rate); the bank cache shows the result at once. Cut
// short (the plug-in closed, the broker or the Virus gone) the Virus keeps a mix of old and new patches. So a close
// lets the queued RAM writes go out before its close sequence (bounded), and what does not go out is logged by slot.
#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace vti {

// at most this long a close waits for queued RAM writes: 128 dumps x 11 pieces x 10 ms (DumpPacer) = 14.1 s, the
// original's ~130 ms per dump 16.6 s; the rest is the margin for the write reports (T7)
constexpr int RAM_CLOSE_WAIT_MS = 20000;

// a single dump into RAM bank 1..4: bank 0..3 * 128 + program, else -1
inline int ram_slot(const std::vector<uint8_t> &m)
{
    if (m.size() != 524 || m[0] != 0xF0 || m[6] != 0x10 || m[7] < 1 || m[7] > 4 || m[8] > 127)
        return -1;
    return (m[7] - 1) * 128 + m[8];
}

// the slots of the RAM writes (entries marked ram) in `q`, in queue order
template <class Q> std::vector<int> ram_writes(const std::deque<Q> &q)
{
    std::vector<int> out;
    for (const auto &e : q)
        if (e.ram && ram_slot(e.msg) >= 0)
            out.push_back(ram_slot(e.msg));
    return out;
}

// the close: everything but the RAM writes goes, as before T11; returns how many went
template <class Q> int keep_ram_writes(std::deque<Q> &q)
{
    const size_t before = q.size();
    q.erase(std::remove_if(q.begin(), q.end(), [](const Q &e) { return !e.ram; }), q.end());
    return (int)(before - q.size());
}

// "RAM A 3-10, 12; RAM B 1" (1-based programs): short enough for one log line also for a whole bank
inline std::string ram_slot_list(std::vector<int> slots)
{
    std::sort(slots.begin(), slots.end());
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    std::string out;
    for (size_t i = 0; i < slots.size();) {
        size_t j = i;
        while (j + 1 < slots.size() && slots[j + 1] == slots[j] + 1 && slots[j + 1] / 128 == slots[i] / 128)
            j++;
        const bool new_bank = i == 0 || slots[i - 1] / 128 != slots[i] / 128;
        if (new_bank)
            out += std::string(out.empty() ? "" : "; ") + "RAM " + (char)('A' + slots[i] / 128) + " ";
        else
            out += ", ";
        out += std::to_string(slots[i] % 128 + 1);
        if (j > i)
            out += "-" + std::to_string(slots[j] % 128 + 1);
        i = j + 1;
    }
    return out;
}

} // namespace vti
