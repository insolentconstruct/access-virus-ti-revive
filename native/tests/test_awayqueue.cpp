// The plug-in's send queue while a Virus it had is away (device/awayqueue.h, research R6-3). The original clears its
// start-up queue while no Virus is connected and builds everything again from its model at the connection; ours kept
// the queue, so an unfinished start-up block and edits went out again in front of the new start-up. Hardware-free:
// the reconnect below plays the plug-in's part (resend_to_virus: start-up messages, then the state).
#include "check.h"
#include "../src/device/awayqueue.h"

#include <cstdint>
#include <deque>
#include <vector>

using vti::drop_while_away;

namespace {
struct Q {
    std::vector<uint8_t> msg;
    bool state = false;
    uint8_t cable = 2;
    bool keep = false;
};
const std::vector<uint8_t> STARTUP = {0xF0, 0x01, 0xF7}, EDIT_OLD = {0xB0, 0x10, 0x20}, EDIT_NEW = {0xB0, 0x10, 0x40},
                           REMOTE = {0xF0, 0x02, 0xF7};

// resend_to_virus: the start-up messages, then the state (the editor's edits are in it)
void reconnect(std::deque<Q> &q, const std::vector<uint8_t> &state_edit)
{
    q.push_back({STARTUP});
    q.push_back({state_edit, true});
}
int count(const std::deque<Q> &q, const std::vector<uint8_t> &m)
{
    int n = 0;
    for (auto &e : q)
        n += e.msg == m;
    return n;
}
} // namespace

TEST(awayqueue_lost_during_startup_no_duplicate_block)
{
    // the Virus went away while the first start-up block was still queued
    std::deque<Q> q = {{STARTUP}, {EDIT_OLD, true}};
    CHECK(drop_while_away(q) == 2);
    CHECK(q.empty());
    reconnect(q, EDIT_OLD);
    CHECK(count(q, STARTUP) == 1); // one start-up block, not the old one in front of the new
    CHECK(q.front().msg == STARTUP);
}

TEST(awayqueue_edit_while_away_reaches_the_virus_from_the_state)
{
    std::deque<Q> q;
    q.push_back({EDIT_NEW}); // an editor edit made while away (ui_set: sent, and recorded in the state)
    CHECK(drop_while_away(q) == 1); // the next tick of the absence drops it again
    reconnect(q, EDIT_NEW);         // the state holds the edit
    CHECK(count(q, EDIT_NEW) == 1);
    CHECK(count(q, EDIT_OLD) == 0);
    CHECK(q.front().msg == STARTUP); // after the start-up, not in front of it
}

TEST(awayqueue_keeps_what_no_state_brings_back)
{
    // REMOTE template edits live in the template file, not in the state: a reconnect does not send them again
    std::deque<Q> q = {{STARTUP}, {REMOTE, false, 3, true}, {EDIT_OLD, true}};
    CHECK(drop_while_away(q) == 2);
    CHECK(q.size() == 1 && q.front().msg == REMOTE && q.front().cable == 3);
}

TEST(awayqueue_empty_queue)
{
    std::deque<Q> q;
    CHECK(drop_while_away(q) == 0);
}
