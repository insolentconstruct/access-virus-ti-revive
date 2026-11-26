// idleedits.h - the editor's edits of automatable parameters while the host does not process (research T13, R7-4).
// Why: ui_set hands such an edit to the host (performEdit), and its Virus message is built when the host passes it back
// to process(). A host that stops calling process() (stopped transport with "suspend plug-ins", audio device closed,
// no audio device) left the knob move unsent while the editor showed it. The original pumps its GUI edits from a timer
// without host blocks (R4: 0x1800a8ba0, gate 0x1800815e0). So the worker sends an edit itself when no block came for
// IDLE_MS and none since the edit: latest value per parameter, at the automation rate (AutomationQueue, rule 2), with
// an empty budget at the start of each idle phase (no burst right after one of process()'s). An edit made before a
// block is the host's to pass to process(): it is dropped here. The host's hand-back of a value sent here, in the
// first block after the send, is an echo and not sent again (from_host), so each change goes out once.
// Threads: edit from the editor thread, pump and drop_parts from the worker, from_host from the audio thread (try_lock,
// no allocation; if the lock is busy the value counts as no echo: at worst the same value goes twice).
#pragma once
#include "automation.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>

namespace vti {

class IdleEdits {
public:
    static constexpr int64_t IDLE_MS = 50; // no block for this long: the host does not process (original FE: 40 ms, R4-5)
    explicit IdleEdits(double rate = 250) : q_(rate) {}

    // editor thread: `id` got `value` at `now` (ms); `part` 0..15 if that part's single dump drops it (R7-3), else -1
    void edit(uint32_t id, int value, int part, int64_t now)
    {
        std::lock_guard<std::mutex> lk(mu_);
        Slot &s = slots_[id];
        if (!s.queued && !(s.queued = q_.push(id)))
            return; // queue full: not waiting, or it would never go
        s.value = value, s.at = now, s.part = part;
    }
    // worker, every round: `processed` = the start of the host's last block (ms, -1 = none yet); `hold` = one of our
    // dump requests is open (T9: the edits wait, latest value wins). send(id, value) is true if a message went out.
    template <class Send> int pump(int64_t now, int64_t processed, bool hold, Send send)
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto &kv : slots_)
            if (kv.second.value >= 0 && processed > kv.second.at)
                kv.second.value = -1; // a block came after the edit: the host passes it to process()
        const bool idle = processed < 0 || now - processed > IDLE_MS;
        if (!idle || last_ < 0) {
            q_.clear_budget(); // process() has the rate while it runs
            last_ = now;
            return 0;
        }
        q_.refill((int)std::min<int64_t>(now - last_, 1000), 1000.0);
        last_ = now;
        return q_.drain(hold ? 0 : AutomationQueue::MAX, [&](uint32_t id) {
            Slot &s = slots_[id];
            s.queued = false;
            if (s.value < 0)
                return false; // dropped: no budget
            const int v = s.value;
            s.value = -1;
            if (!send(id, v))
                return false;
            s.mark = v, s.sent = now;
            return true;
        });
    }
    // worker: single dumps of `parts` (bit per part) were queued: their edits waiting here would overwrite the new patch
    void drop_parts(uint32_t parts)
    {
        if (!parts)
            return;
        std::lock_guard<std::mutex> lk(mu_);
        for (auto &kv : slots_)
            if (kv.second.value >= 0 && kv.second.part >= 0 && kv.second.part < 16 && (parts >> kv.second.part & 1))
                kv.second.value = -1;
    }
    // audio thread: the host passes `value` for `id` to process(); `prev` = the start of the block before (ms, -1 =
    // none). True: the echo of a value sent here (not sent again). Either way an edit waiting here is process()'s now.
    bool from_host(uint32_t id, int value, int64_t prev)
    {
        std::unique_lock<std::mutex> lk(mu_, std::try_to_lock);
        if (!lk.owns_lock())
            return false;
        auto it = slots_.find(id);
        if (it == slots_.end())
            return false;
        Slot &s = it->second;
        s.value = -1;
        const bool echo = s.mark == value && s.sent >= prev;
        s.mark = -1;
        return echo;
    }

private:
    struct Slot {
        int value = -1; // waiting to be sent here, -1 = none
        int64_t at = 0; // when the editor made it
        int part = -1;
        bool queued = false; // its id is in q_ (once at most, so dropped edits cannot fill it)
        int mark = -1;       // the last value sent here, until the host hands it back
        int64_t sent = 0;
    };
    std::mutex mu_;
    std::map<uint32_t, Slot> slots_;
    AutomationQueue q_;
    int64_t last_ = -1; // the last pump (ms)
};

} // namespace vti
