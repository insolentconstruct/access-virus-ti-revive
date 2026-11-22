// latencywatch.h - does the host have the plug-in's latency right? (#37, #33, E544). The latency follows the host's
// block (stamprule.h reported_frames: lead + block + margin), so a host that read it before its last setupProcessing
// keeps a value that is a block off. setActive announces a change it sees (kLatencyChanged), but inside a host's own
// set-up that announcement can be lost: Logic on an Intel Mac was "slightly out of sync" after inserting the plug-in or
// opening the project until a buffer size change (its new set-up reads the latency again). The UI timer asks this once
// per tick, outside the host's set-up: a value the host did not read is announced once more, each value only once (a
// host that never reads again gets no flood). Header only, no SDK: unit-tested.
#pragma once
#include <cstdint>

namespace vti {

class LatencyWatch {
public:
    // announce now? `host_read`: what getLatencySamples last returned (-1: never asked), `now`: what it would return
    bool announce(bool active, int64_t host_read, int64_t now)
    {
        if (!active || host_read < 0)
            return false;
        if (now == host_read) {
            announced_ = -1; // right again: a later change is announced anew
            return false;
        }
        if (now == announced_)
            return false;
        announced_ = now;
        return true;
    }
    // #41 (E546): Logic sometimes kept an old latency although the value it read looked right. Once per instance,
    // about 1 s after the latency settled, announce it anyway; only once, so a host that re-activates on the
    // announcement cannot loop. Settled = `now` unchanged while active, read by the host and the Virus ready: a
    // nudge before the Virus connects is wasted on a value that changes (Mac field log: 3085, then 2816).
    // One call per UI timer tick
    static constexpr int kNudgeTicks = 20; // the 50 ms UI timer: about 1 s
    bool nudge(bool active, bool device_ready, int64_t host_read, int64_t now)
    {
        if (nudged_)
            return false;
        if (!active || !device_ready || host_read < 0 || now != nudge_value_) {
            nudge_value_ = now;
            nudge_ticks_ = active && device_ready && host_read >= 0; // a change counts as the new value's first tick
            return false;
        }
        nudged_ = ++nudge_ticks_ >= kNudgeTicks;
        return nudged_;
    }
    // for the log: has the host read a value it had not read before?
    bool read_changed(int64_t host_read)
    {
        if (host_read < 0 || host_read == logged_)
            return false;
        logged_ = host_read;
        return true;
    }

private:
    int64_t announced_ = -1, logged_ = -1, nudge_value_ = -1;
    int nudge_ticks_ = 0;
    bool nudged_ = false;
};

} // namespace vti
