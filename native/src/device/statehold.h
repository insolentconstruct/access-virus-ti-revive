// statehold.h - note-ons held while a project loaded in a running session goes out (research R8-1). The original's
// setChunk puts the whole project into its start-up queue, and its timer counts that queue as busy: notes are dropped
// until the upload is out (~1-2 s, E161). Ours held notes only at the first start and after a reconnect (starting_),
// so a setState while playing (REAPER undo, preset swaps) put notes between the 16 part dumps, late behind the
// Virus' backlog (E144, E148) and under patches that change. No readiness probe here: the Virus did not go away,
// and the original waits for its own queue only. setState starts the hold after it queued the state; process()
// asks once per block with DeviceOut::busy() (queued or not yet reported written, T7). Lock-free, header only.
#pragma once
#include <atomic>
#include <cstdint>

namespace vti {

class StateHold {
public:
    static constexpr int64_t LIMIT_MS = 3000; // like the reconnect's marker wait: a queue that never empties ends it

    // setState, after send_state: holds only when `running` (a connected Virus, an active plug-in; before that the
    // start-up's own hold covers the state, E591 (2)). A second setState renews it: one hold for both.
    bool start(bool running, int64_t now_ms)
    {
        if (!running)
            return false;
        until_.store(now_ms + LIMIT_MS, std::memory_order_relaxed);
        return true;
    }
    // process(), once per block: true while the state is on its way. Once it is out (or the bound passed) the hold
    // ends for good, so a later edit's SysEx in the queue does not drop notes; a setState meanwhile keeps its own
    bool holding(bool busy, int64_t now_ms)
    {
        int64_t u = until_.load(std::memory_order_relaxed);
        if (u == 0)
            return false;
        if (busy && now_ms < u)
            return true;
        until_.compare_exchange_strong(u, 0, std::memory_order_relaxed);
        return false;
    }

private:
    std::atomic<int64_t> until_{0}; // 0 = no hold
};

} // namespace vti
