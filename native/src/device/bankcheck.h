// bankcheck.h - when the plug-in checks the Virus' RAM banks again. The original mounts its library again and
// re-checks the RAM banks at every connection (0x18007ec30 connect branch, 0x18008d070 / 0x18008ded0; research R6-1,
// R10-2); ours did it once per instance, so a Virus switched on after the DAW, or one whose RAM changed while it was
// away, kept a stale browser for the session. Here every answered readiness probe asks for a check: the first start, a
// reconnect, a resume, a Virus that appears after a start without it, or one that answers again after a missed request
// (R2-3, bounded by AnswerWatch). The sync thread starts it once the Virus is ready and no RAM job runs; a check that
// begins covers every request made before it. Lock-free: the probe runs on the sync and the reconnect thread. Header
// only: the plug-in and the tests use it.
#pragma once
#include <atomic>

namespace vti {

class BankCheck {
public:
    void request() { wanted_ = true; }
    // the sync thread's poll: wanted, the Virus ready (its probe answered, `starting` over: the sync marker came or
    // its wait ended, as at the first start) and no RAM download or write running (they read the same slots and write
    // the cache back)
    bool due(bool ready, bool starting, bool ram_job) const { return wanted_ && ready && !starting && !ram_job; }
    void begin() { wanted_ = false; } // a check starts: it covers what was asked before
private:
    std::atomic<bool> wanted_{false};
};

} // namespace vti
