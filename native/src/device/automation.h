// automation.h - the rate limit for host automation going to the Virus. A flood of parameter messages hung the
// Virus' audio until a power cycle (stress test 2026-10-13, Linux: 1000/s together with dense notes; the firmware
// mechanism is still open - not the DSP's stamp queue, E261), so process() coalesces the changes per parameter (only
// the latest value waits) and sends at most `rate` per second, oldest parameter first. Since round 0.8.27 each
// change goes out with its own time stamp at the block time (stamprule.h) and within the byte budget (sendbudget.h).
// Since research T20 (R4-4) one parameter also goes at most every 20 ms like the original's gate (0x1800815e0, drain
// 0x18007a690: at most 50/s each, latest value wins): one automated lane no longer takes the whole 250/s.
// Realtime safe: fixed ring, no allocation. The pending values live with the caller (one slot per parameter).
#pragma once
#include <algorithm>
#include <cstdint>

namespace vti {

class AutomationQueue {
public:
    static constexpr int MAX = 8192; // > 2703 automatable parameters (E111) + 16 x 122 mapped controllers: each queued at most once
    static constexpr double FLOOR = 0.020; // seconds between two sends of one parameter (R4-4)
    explicit AutomationQueue(double rate = 250, double burst = 64) : rate_(rate), burst_(burst) {}

    // a parameter got a new value and was not waiting yet (the caller keeps the "waiting" flag); false if full.
    // floor = false for messages outside the original's gate (pitch bend, channel aftertouch): only the budget
    bool push(uint32_t id, bool floor = true)
    {
        if (count_ >= MAX)
            return false;
        const int i = (head_ + count_++) % MAX;
        ids_[i] = id, floor_[i] = floor;
        return true;
    }
    // per block: budget for `samples` at `sample_rate`, up to the burst size; also the clock of the 20 ms floor
    void refill(int samples, double sample_rate)
    {
        budget_ = std::min(burst_, budget_ + rate_ * samples / sample_rate);
        now_ += samples / sample_rate;
    }
    void clear_budget() { budget_ = 0; } // another sender has the rate meanwhile (idleedits.h): no burst after it
    // sends waiting parameters while budget and `max` last: send(id) returns true if a message went out (costs one
    // unit of budget), false for a parameter that is no longer waiting. One sent less than FLOOR ago goes back to the
    // end without send() (its latest value stays waiting with the caller, no budget). Returns the number sent.
    template <class Send> int drain(int max, Send &&send)
    {
        int sent = 0;
        for (int n = count_; n > 0 && budget_ >= 1 && sent < max; n--) { // each waiting id looked at once
            const uint32_t id = ids_[head_];
            const bool floor = floor_[head_];
            head_ = (head_ + 1) % MAX;
            count_--;
            if (floor && too_soon(id)) {
                push(id, true); // room: it was just taken out
                continue;
            }
            if (send(id)) {
                budget_ -= 1;
                sent++;
                if (floor)
                    recent_[recent_head_++ % RECENT] = {id, now_};
            }
        }
        return sent;
    }
    int waiting() const { return count_; } // test hook: the tests check the queue's bound

private:
    // the sends of the last 20 ms: at most burst + rate * FLOOR (69 at the defaults), so 128 keep them all and no
    // per-parameter table is needed (ids are sparse)
    static constexpr int RECENT = 128;
    struct Sent {
        uint32_t id;
        double at = -1;
    };
    bool too_soon(uint32_t id) const
    {
        for (const Sent &r : recent_)
            if (r.at >= 0 && r.id == id && now_ - r.at < FLOOR - 1e-9) // epsilon: 882 samples at 44.1 kHz are 20 ms
                return true;
        return false;
    }
    double rate_, burst_, budget_ = 0, now_ = 0;
    uint32_t ids_[MAX];
    bool floor_[MAX];
    int head_ = 0, count_ = 0;
    Sent recent_[RECENT] = {};
    unsigned recent_head_ = 0;
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
