// Editor edits while the host does not process (research T13, R7-4): the worker sends them itself once no block came
// for 50 ms and none since the edit, latest value per parameter at the automation rate; while the host processes only
// process() sends them, and its hand-back of a value the worker sent is an echo, so each change goes out once
#include "check.h"
#include "device/idleedits.h"
#include <utility>
#include <vector>
using namespace vti;

namespace {
struct Sent {
    std::vector<std::pair<uint32_t, int>> m;
    int pump(IdleEdits &e, int64_t now, int64_t processed, bool hold = false)
    {
        return e.pump(now, processed, hold, [&](uint32_t id, int v) {
            m.push_back({id, v});
            return true;
        });
    }
};
} // namespace

// no audio device at all (no block yet): an edit goes out from the worker, once, with its latest value
TEST(idleedits_no_block_yet_sends_latest_once)
{
    IdleEdits e;
    Sent s;
    s.pump(e, 1000, -1);
    e.edit(7, 10, 0, 1001);
    e.edit(7, 12, 0, 1002);
    for (int64_t t = 1003; t < 1020; t++)
        s.pump(e, t, -1);
    CHECK((s.m == std::vector<std::pair<uint32_t, int>>{{7, 12}}));
}

// the host stopped processing (last block 100 ms ago): the edit goes, and a later one of the same knob too
TEST(idleedits_idle_host_sends_each_edit)
{
    IdleEdits e;
    Sent s;
    s.pump(e, 1000, 900);
    e.edit(7, 10, 0, 1000);
    for (int64_t t = 1001; t < 1020; t++)
        s.pump(e, t, 900);
    e.edit(7, 20, 0, 1020);
    for (int64_t t = 1021; t < 1040; t++)
        s.pump(e, t, 900);
    CHECK((s.m == std::vector<std::pair<uint32_t, int>>{{7, 10}, {7, 20}}));
}

// the host processes (a block every 10 ms): the worker sends nothing, process() gets the edits from the host; an edit
// made before a block stays process()'s even when the host stops right after it
TEST(idleedits_running_host_only_process_sends)
{
    IdleEdits e;
    Sent s;
    int64_t block = 1000;
    for (int64_t t = 1000; t < 1200; t++) {
        if (t % 10 == 0)
            block = t;
        if (t % 7 == 0)
            e.edit(3, (int)(t % 128), 1, t);
        s.pump(e, t, block);
    }
    e.edit(4, 50, 1, 1195); // the block at 1200 comes after it, then the host stops
    for (int64_t t = 1200; t < 1400; t++)
        s.pump(e, t, 1200);
    CHECK(s.m.empty());
    CHECK(!e.from_host(4, 50, 1190)); // the host's hand-back is not an echo: process() sends it
}

// the automation rate (250/s, rule 2): 200 knobs edited at once in an idle host take 0.8 s, and the first idle
// moment has no burst (the budget starts empty, so process()'s last burst is not followed by a second one)
TEST(idleedits_rate_limited_without_burst)
{
    IdleEdits e(250);
    Sent s;
    s.pump(e, 999, 900);
    for (uint32_t id = 0; id < 200; id++)
        e.edit(id, 1, 0, 1000);
    for (int64_t t = 1000; t <= 1100; t++)
        s.pump(e, t, 900);
    CHECK(s.m.size() >= 24 && s.m.size() <= 26); // 101 ms at 250/s
    for (int64_t t = 1101; t <= 1900; t++)
        s.pump(e, t, 900);
    CHECK_EQ(s.m.size(), (size_t)200);
    // no edit lost or sent twice, oldest first
    for (uint32_t id = 0; id < 200; id++)
        CHECK(s.m[id].first == id);
}

// back to process(): the host hands back a value the worker sent, in the first block after it: an echo (not sent
// again); a newer value is not; a hand-back in a later block is no echo any more
TEST(idleedits_switching_sends_each_change_once)
{
    IdleEdits e;
    Sent s;
    s.pump(e, 1000, 900);
    e.edit(7, 10, 0, 1000);
    e.edit(8, 30, 0, 1000);
    e.edit(9, 40, 0, 1000);
    for (int64_t t = 1001; t < 1020; t++)
        s.pump(e, t, 900);
    CHECK_EQ(s.m.size(), (size_t)3);
    // the host resumes at 1030 (previous block 900): it passes the queued edits
    CHECK(e.from_host(7, 10, 900));
    CHECK(!e.from_host(8, 31, 900)); // automation with another value: process() sends it
    CHECK(!e.from_host(7, 10, 900)); // an echo counts once
    // the block after: 9's hand-back came late, it is the host's value now
    CHECK(!e.from_host(9, 40, 1030));
    for (int64_t t = 1030; t < 1100; t++)
        s.pump(e, t, t);
    CHECK_EQ(s.m.size(), (size_t)3);
}

// an edit still waiting here when the host passes that parameter (a flush call without samples): process() owns it
TEST(idleedits_host_hand_back_takes_the_waiting_edit)
{
    IdleEdits e;
    Sent s;
    s.pump(e, 1000, 900);
    e.edit(7, 10, 0, 1000);
    CHECK(!e.from_host(7, 10, 900)); // not sent here yet: no echo, process() sends it
    for (int64_t t = 1001; t < 1050; t++)
        s.pump(e, t, 900);
    CHECK(s.m.empty());
}

// a dump request is open (T9): the edits wait, the latest value goes after the answer, once
TEST(idleedits_held_during_a_request)
{
    IdleEdits e;
    Sent s;
    s.pump(e, 1000, 900);
    e.edit(7, 10, 0, 1000);
    for (int64_t t = 1001; t < 1100; t++) {
        if (t == 1050)
            e.edit(7, 11, 0, t);
        s.pump(e, t, 900, true);
    }
    CHECK(s.m.empty());
    for (int64_t t = 1100; t < 1120; t++)
        s.pump(e, t, 900);
    CHECK((s.m == std::vector<std::pair<uint32_t, int>>{{7, 11}}));
}

// a part's single dump queued (R7-3, T10): its waiting edits go, other parts' and part-less ones stay
TEST(idleedits_a_load_drops_its_parts_edits)
{
    IdleEdits e;
    Sent s;
    s.pump(e, 999, -1, true);
    e.edit(1, 10, 2, 1000);
    e.edit(2, 20, 3, 1000);
    e.edit(3, 30, -1, 1000);
    e.drop_parts(1u << 2);
    for (int64_t t = 1000; t < 1050; t++)
        s.pump(e, t, -1);
    CHECK((s.m == std::vector<std::pair<uint32_t, int>>{{2, 20}, {3, 30}}));
}

// edits made while the host ran and dropped here must not fill the queue: later idle edits still go
TEST(idleedits_dropped_edits_do_not_fill_the_queue)
{
    IdleEdits e;
    Sent s;
    for (int i = 0; i < 20000; i++) {
        e.edit((uint32_t)(i % 3000), 5, 0, 1000 + i);
        s.pump(e, 1000 + i, 1000 + i + 1); // a block after each edit
    }
    e.edit(5000, 1, 0, 30000);
    for (int64_t t = 30001; t < 30200; t++)
        s.pump(e, t, 21001);
    CHECK((s.m == std::vector<std::pair<uint32_t, int>>{{5000, 1}}));
}

// research T20 (R4-4): one knob dragged in an idle host (an edit every ms) goes at most every 20 ms like the
// original's gate, latest value wins, and the value it ends on goes out
TEST(idleedits_one_knob_at_most_every_20_ms)
{
    IdleEdits e;
    Sent s;
    std::vector<int64_t> at;
    s.pump(e, 999, 900);
    for (int64_t t = 1000; t < 1200; t++) {
        e.edit(7, (int)(t % 128), 0, t);
        size_t before = s.m.size();
        s.pump(e, t, 900);
        if (s.m.size() > before)
            at.push_back(t);
    }
    for (int64_t t = 1200; t < 1240; t++)
        s.pump(e, t, 900);
    CHECK(s.m.size() >= 9 && s.m.size() <= 11); // 200 ms / 20 ms, plus the final value
    CHECK(s.m.back().second == 1199 % 128);
    bool spaced = true;
    for (size_t i = 1; i < at.size(); i++)
        spaced = spaced && at[i] - at[i - 1] >= 20;
    CHECK(spaced);
}
