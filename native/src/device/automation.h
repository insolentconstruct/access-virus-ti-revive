// automation.h - the rate limit for host automation going to the Virus. A flood of parameter messages hung the
// Virus' audio until a power cycle (stress test 2026-10-13, Linux: 1000/s together with dense notes; the firmware
// mechanism is still open - not the DSP's stamp queue, E261), so process() coalesces the changes per parameter (only
// the latest value waits) and sends at most `rate` per second, oldest parameter first. Since round 0.8.27 each
// change goes out with its own time stamp at the block time (stamprule.h) and within the byte budget (sendbudget.h).
// Realtime safe: fixed ring, no allocation. The pending values live with the caller (one slot per parameter).
#pragma once
#include <algorithm>
#include <cstdint>

namespace vti {

class AutomationQueue {
public:
    static constexpr int MAX = 8192; // > 2703 automatable parameters (E111) + 16 x 122 mapped controllers: each queued at most once
    explicit AutomationQueue(double rate = 250, double burst = 64) : rate_(rate), burst_(burst) {}

    // a parameter got a new value and was not waiting yet (the caller keeps the "waiting" flag); false if full
    bool push(uint32_t id)
    {
        if (count_ >= MAX)
            return false;
        ids_[(head_ + count_++) % MAX] = id;
        return true;
    }
    // per block: budget for `samples` at `sample_rate`, up to the burst size
    void refill(int samples, double sample_rate) { budget_ = std::min(burst_, budget_ + rate_ * samples / sample_rate); }
    // sends waiting parameters while budget and `max` last: send(id) returns true if a message went out (costs one
    // unit of budget), false for a parameter that is no longer waiting. Returns the number sent.
    template <class Send> int drain(int max, Send &&send)
    {
        int sent = 0;
        while (count_ > 0 && budget_ >= 1 && sent < max) {
            uint32_t id = ids_[head_];
            head_ = (head_ + 1) % MAX;
            count_--;
            if (send(id)) {
                budget_ -= 1;
                sent++;
            }
        }
        return sent;
    }
    int waiting() const { return count_; } // test hook: the tests check the queue's bound

private:
    double rate_, burst_, budget_ = 0;
    uint32_t ids_[MAX];
    int head_ = 0, count_ = 0;
};

// R7-3: single dumps of `parts` (bit per part 0..15) were queued: the values still waiting for the budget, or held
// while a dump request is open (T9), would go out after the new patch and overwrite it - they go, as in the
// original's slot clear (0x18007aa50); standard controllers stay. The map's values have part, pending (-1 = none)
// and standard. The ids stay in the queue: drain skips them without budget. Returns how many went.
template <class Map> int drop_pending(Map &params, uint32_t parts)
{
    int n = 0;
    for (auto &kv : params)
        if (kv.second.pending >= 0 && !kv.second.standard && kv.second.part >= 0 && kv.second.part < 16 &&
            (parts >> kv.second.part & 1)) {
            kv.second.pending = -1;
            n++;
        }
    return n;
}

} // namespace vti
