// the automation rate limit as process() uses it: coalesced per parameter, <= 250 messages/s, oldest first.
// Simulated host blocks of 512 samples at 44.1 kHz (86 blocks/s).
#include "check.h"
#include "device/automation.h"
#include <map>
#include <memory>
#include <vector>

using namespace vti;

namespace {
struct Sim {
    std::unique_ptr<AutomationQueue> q = std::make_unique<AutomationQueue>(250.0); // 16 KB: not on the stack
    std::map<uint32_t, int> pending;                                          // id -> waiting value (-1 = none)
    std::map<uint32_t, int> sent_count, last_sent;
    int total = 0;
    void change(uint32_t id, int v) // a host parameter change (process(): inputParameterChanges)
    {
        auto it = pending.find(id);
        bool waiting = it != pending.end() && it->second >= 0;
        if (waiting || q->push(id))
            pending[id] = v;
    }
    void block(int max_events = 96)
    {
        q->refill(512, 44100);
        q->drain(max_events, [&](uint32_t id) {
            int &v = pending[id];
            if (v < 0)
                return false;
            last_sent[id] = v, sent_count[id]++, total++;
            v = -1;
            return true;
        });
    }
};
} // namespace

TEST(automation_three_lfos)
{
    // the forum case: 3 Max for Live LFOs, each changing its parameter every block (~258 changes/s). Each one goes at
    // most every 20 ms (research T20, R4-4): every second block of 11.6 ms, ~43/s per LFO
    Sim s;
    int blocks = 86 * 10;
    for (int b = 0; b < blocks; b++) {
        for (uint32_t id = 1; id <= 3; id++)
            s.change(id, (b * (int)id) & 127);
        s.block();
    }
    for (uint32_t id = 1; id <= 3; id++) {
        CHECK(s.sent_count[id] >= 40 * 10);
        CHECK(s.sent_count[id] <= 50 * 10 + 1);
    }
}

// research T20 (R4-4): the original sends one parameter at most every 20 ms, latest value wins (gate 0x1800815e0,
// drain 0x18007a690); one lane changing in every block of 256 samples (172 blocks/s) goes at <= 50/s, never two sends
// closer than 20 ms, and its last value still goes out after it stops
TEST(automation_one_parameter_at_most_every_20_ms)
{
    Sim s;
    std::vector<double> at;
    double t = 0;
    for (int b = 0; b < 172 * 10; b++, t += 256.0 / 44100) {
        s.change(9, b & 127);
        int before = s.total;
        s.q->refill(256, 44100);
        s.q->drain(96, [&](uint32_t id) {
            int &v = s.pending[id];
            if (v < 0)
                return false;
            s.last_sent[id] = v, s.sent_count[id]++, s.total++;
            v = -1;
            return true;
        });
        if (s.total > before)
            at.push_back(t);
    }
    CHECK(s.sent_count[9] <= 50 * 10 + 1);
    CHECK(s.sent_count[9] >= 40 * 10); // every 4th block of 5.8 ms: 23.2 ms
    bool spaced = true;
    for (size_t i = 1; i < at.size(); i++)
        spaced = spaced && at[i] - at[i - 1] >= 0.02 - 1e-9;
    CHECK(spaced);
    // it stops: the latest value (1719 & 127) waits for its 20 ms and then goes
    for (int b = 0; b < 4; b++)
        s.block();
    CHECK_EQ(s.last_sent[9], (172 * 10 - 1) & 127);
    CHECK_EQ(s.q->waiting(), 0);
}

// a parameter waiting for its 20 ms does not hold up the others behind it, and costs no budget
TEST(automation_floor_does_not_block_the_others)
{
    Sim s;
    s.change(1, 10);
    s.block(); // 1 sent
    s.change(1, 11);
    s.change(2, 20);
    s.block(); // 11.6 ms later: 1 waits, 2 goes
    CHECK(s.sent_count[1] == 1 && s.sent_count[2] == 1);
    s.block(); // 23.2 ms after its send: 1 goes with its latest value
    CHECK(s.sent_count[1] == 2 && s.last_sent[1] == 11);
}

// pitch bend and channel aftertouch are not in the original's gate (only CC, poly pressure and parameter SysEx,
// 0x1800815e0): pushed without the floor they go at the shared budget only
TEST(automation_unfloored_ids_skip_the_20_ms)
{
    Sim s;
    for (int b = 0; b < 86; b++) {
        auto it = s.pending.find(5);
        if (!(it != s.pending.end() && it->second >= 0) && s.q->push(5, false))
            s.pending[5] = b;
        s.block();
    }
    CHECK(s.sent_count[5] >= 80); // every block of 11.6 ms
}

TEST(automation_flood_is_fair_and_limited)
{
    // 200 parameters changing every block (~17200 changes/s): still <= 250/s, and every parameter keeps moving
    Sim s;
    for (int b = 0; b < 86 * 20; b++) {
        for (uint32_t id = 1; id <= 200; id++)
            s.change(id, (b + (int)id) & 127);
        s.block();
    }
    CHECK(s.total <= 250 * 20 + 64);
    for (uint32_t id = 1; id <= 200; id++)
        CHECK(s.sent_count[id] >= 20); // each one updated about every 0.8 s (round robin, oldest first)
    CHECK(s.q->waiting() <= 200);   // each parameter waits at most once
}

TEST(automation_latest_value_wins)
{
    Sim s;
    s.q = std::make_unique<AutomationQueue>(250.0, 1.0);
    for (int i = 0; i <= 100; i++)
        s.change(7, i); // 101 changes inside one block
    s.block();
    CHECK_EQ(s.sent_count[7], 1);
    CHECK_EQ(s.last_sent[7], 100);
    // after the block nothing is left: a parameter that stops moving ends at its final value
    for (int b = 0; b < 10; b++)
        s.block();
    CHECK_EQ(s.sent_count[7], 1);
    CHECK_EQ(s.q->waiting(), 0);
}

// research T9 (R4-2): while a dump request is open process() drains nothing (max 0); the changes keep coalescing, and
// after the answer every moved parameter goes out once with its latest value, the first block at most the burst
TEST(automation_held_during_a_request_latest_value_after)
{
    Sim s;
    for (int b = 0; b < 86; b++) { // ~1 s of an answer, 40 lanes moving every block
        for (uint32_t id = 1; id <= 40; id++)
            s.change(id, (b + (int)id) & 127);
        s.block(0);
    }
    CHECK_EQ(s.total, 0);
    s.block();
    CHECK(s.total <= 64); // the burst: no flood after the hold (rule 2)
    for (int b = 0; b < 86; b++)
        s.block();
    CHECK_EQ(s.total, 40); // one message per lane, not 86
    for (uint32_t id = 1; id <= 40; id++)
        CHECK(s.last_sent[id] == ((85 + (int)id) & 127));
}

TEST(automation_final_values_arrive)
{
    // a burst of many parameters, then silence: every parameter's last value is sent eventually (no stuck values)
    Sim s;
    for (uint32_t id = 1; id <= 1000; id++)
        s.change(id, (int)(id & 127));
    for (int b = 0; b < 86 * 5; b++) // 5 s >= 1000 / 250
        s.block();
    CHECK_EQ(s.total, 1000);
    for (uint32_t id = 1; id <= 1000; id++)
        CHECK(s.last_sent[id] == (int)(id & 127));
}

TEST(automation_respects_event_room)
{
    // process() leaves room for notes: the queue never sends more than it is allowed in a block
    Sim s;
    for (uint32_t id = 1; id <= 100; id++)
        s.change(id, 1);
    s.q->refill(44100, 44100); // a full second of budget (capped to the burst of 64)
    int before = s.total;
    s.block(10);
    CHECK_EQ(s.total - before, 10);
    s.block(0);
    CHECK_EQ(s.total - before, 10);
}

// Mutation run 2026-10-20: a full queue refuses the next parameter instead of overwriting the oldest
TEST(automation_full_queue_refuses)
{
    auto q = std::make_unique<AutomationQueue>(250.0);
    bool all = true;
    for (int i = 0; i < AutomationQueue::MAX; i++)
        all = all && q->push((uint32_t)i);
    CHECK(all);
    CHECK(!q->push(99999));
    uint32_t first = 0xFFFFFFFF;
    q->refill(44100, 44100);
    q->drain(1, [&](uint32_t id) { first = id; return true; });
    CHECK_EQ(first, 0u); // the oldest is still there
}

// R7-3: values still waiting for the budget (or held while a dump request is open, T9) from before a part's single
// dump was queued would overwrite the new patch: they go; the standard controllers and other parts stay
TEST(automation_a_load_drops_the_waiting_values_of_its_parts)
{
    struct P {
        int part, pending;
        bool standard;
    };
    std::map<uint32_t, P> params = {{1, {0, 10, false}}, {2, {0, 20, true}}, {3, {1, 30, false}},
                                    {4, {2, 40, false}}, {5, {0, -1, false}}};
    CHECK(drop_pending(params, (1u << 0) | (1u << 2)) == 2);
    CHECK(params[1].pending == -1 && params[4].pending == -1);
    CHECK(params[2].pending == 20 && params[3].pending == 30 && params[5].pending == -1);
    CHECK(drop_pending(params, 0) == 0);
    // the queue still holds the dropped ids: drain skips them without spending budget, a new change queues anew
    Sim s;
    s.change(7, 1);
    s.pending[7] = -1; // dropped
    s.change(7, 2);    // not waiting any more: queued again
    for (int b = 0; b < 3; b++) // the stale id goes 20 ms after the send (T20 floor), without budget
        s.block();
    CHECK(s.total == 1 && s.last_sent[7] == 2 && s.q->waiting() == 0);
}
