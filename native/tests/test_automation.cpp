// the automation rate limit as process() uses it: coalesced per parameter, <= 250 messages/s, oldest first.
// Simulated host blocks of 512 samples at 44.1 kHz (86 blocks/s).
#include "check.h"
#include "device/automation.h"
#include <map>
#include <memory>

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
    // the forum case: 3 Max for Live LFOs, each changing its parameter every block (~258 changes/s)
    Sim s;
    int blocks = 86 * 10;
    for (int b = 0; b < blocks; b++) {
        for (uint32_t id = 1; id <= 3; id++)
            s.change(id, (b * (int)id) & 127);
        s.block();
    }
    CHECK(s.total <= 250 * 10 + 64); // never above the budget (+ the initial burst)
    CHECK(s.total >= 240 * 10);      // and the budget is used
    for (uint32_t id = 1; id <= 3; id++) {
        CHECK(s.sent_count[id] >= 75 * 10); // each LFO still ~80 updates per second
        CHECK(s.sent_count[id] <= 90 * 10);
    }
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
