// latencywatch.h (#37, #33, E544): Logic was "slightly out of sync" after inserting the plug-in, reopening the project or
// restarting Logic, and in sync after a buffer size change. The plug-in's latency follows the host's block (stamprule.h
// reported_frames), and a host that read it before its last setupProcessing keeps a wrong value. The plug-in then
// announces the change once more from its UI timer, after the host's own set-up, and logs what the host read.
#include "check.h"
#include "device/latencywatch.h"

using namespace vti;

TEST(latencywatch_announces_a_latency_the_host_did_not_read)
{
    LatencyWatch w;
    CHECK(!w.announce(false, 4096, 5632)); // not active: the host reads it at the activation
    CHECK(!w.announce(true, -1, 5632));    // the host never read one: nothing it could have wrong
    CHECK(!w.announce(true, 5632, 5632));  // read and still right
    CHECK(w.announce(true, 4096, 5632));   // read at block 512, the block is now 2048: once
    CHECK(!w.announce(true, 4096, 5632));  // not again while nothing changes (a host that ignores it: no flood)
    CHECK(!w.announce(true, 5632, 5632));  // the host read the new one
}

TEST(latencywatch_a_new_mismatch_is_announced_again)
{
    LatencyWatch w;
    CHECK(w.announce(true, 4096, 5632));
    CHECK(w.announce(true, 4096, 4608)); // another value: announced
    CHECK(!w.announce(true, 4096, 4096)); // back to what the host has: nothing
    CHECK(w.announce(true, 4096, 4608));  // and away again: announced again
}

TEST(latencywatch_logs_each_new_value_the_host_read)
{
    LatencyWatch w;
    CHECK(!w.read_changed(-1));
    CHECK(w.read_changed(4096));
    CHECK(!w.read_changed(4096));
    CHECK(w.read_changed(5632));
}

// #41 (E546): Logic kept an old latency after opening the project although the value it read matched the current one
// (32 ms early at I/O buffer 32); a buffer change fixed it. About 1 s after the latency settled (unchanged, active,
// read by the host, the Virus ready) it is announced once more, even when the host's value looks right - once per
// instance, so a host that re-activates on the announcement cannot loop
TEST(latencywatch_nudges_once_about_a_second_after_the_latency_settled)
{
    LatencyWatch w;
    for (int i = 1; i < LatencyWatch::kNudgeTicks; i++)
        CHECK(!w.nudge(true, true, 5632, 5632)); // not before ~1 s of stable ticks
    CHECK(w.nudge(true, true, 5632, 5632));      // then once, although the host read the current value
    for (int i = 0; i < 100; i++)
        CHECK(!w.nudge(true, true, 5632, 5632)); // never again
    for (int i = 0; i < 10; i++)
        CHECK(!w.nudge(false, true, 5632, 5632)); // deactivated ...
    for (int i = 0; i < 100; i++)
        CHECK(!w.nudge(true, true, 5632, 4096)); // ... and activated again, even with a new value: still not
}

TEST(latencywatch_nudge_waits_for_activation_and_a_host_read)
{
    LatencyWatch w;
    for (int i = 0; i < 3 * LatencyWatch::kNudgeTicks; i++) {
        CHECK(!w.nudge(false, true, 5632, 5632)); // inactive: no count
        CHECK(!w.nudge(true, true, -1, 5632));    // the host never read a value: no count
    }
    for (int i = 1; i < LatencyWatch::kNudgeTicks; i++)
        CHECK(!w.nudge(true, true, 5632, 5632));
    CHECK(w.nudge(true, true, 5632, 5632));
}

// field log (Mac, Logic): the project opened before the Virus was connected, the nudge came at 3085, then the Virus
// connected and the latency became 2816 - the nudge was wasted on a value that had not settled
TEST(latencywatch_nudge_restarts_its_count_when_the_latency_changes)
{
    LatencyWatch w;
    for (int i = 1; i <= 10; i++)
        CHECK(!w.nudge(true, true, 3085, 3085));
    for (int i = 11; i <= LatencyWatch::kNudgeTicks; i++)
        CHECK(!w.nudge(true, true, 3085, 2816)); // changed at tick 11: no nudge at tick 20
    for (int i = 1; i < LatencyWatch::kNudgeTicks - 10; i++)
        CHECK(!w.nudge(true, true, 2816, 2816));
    CHECK(w.nudge(true, true, 2816, 2816)); // 20 ticks after the last change
    for (int i = 0; i < 100; i++)
        CHECK(!w.nudge(true, true, 2816, 2816)); // once only
}

TEST(latencywatch_nudge_waits_for_the_virus)
{
    LatencyWatch w;
    for (int i = 0; i < 5 * LatencyWatch::kNudgeTicks; i++)
        CHECK(!w.nudge(true, false, 3085, 3085)); // no Virus ready: never
    for (int i = 1; i < LatencyWatch::kNudgeTicks; i++)
        CHECK(!w.nudge(true, true, 3085, 3085)); // ~1 s after it became ready
    CHECK(w.nudge(true, true, 3085, 3085));
    CHECK(!w.nudge(true, true, 3085, 3085));
}
