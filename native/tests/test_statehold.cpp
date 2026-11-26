// StateHold (statehold.h): a setState in a running session holds note-ons until the uploaded state is out of the
// queue, at most 3 s, like the original's start-up queue at setChunk (research R8-1). The sequences follow the
// plug-in: setState queues the state, then starts the hold; process() asks once per block with the queue's busy().
#include "check.h"
#include "device/statehold.h"

using namespace vti;

TEST(statehold_holds_until_the_state_is_out)
{
    StateHold h;
    CHECK(h.start(true, 1000));
    CHECK(h.holding(true, 1000));
    CHECK(h.holding(true, 2500)); // 16 dumps at ~110 ms each: still going
    CHECK(!h.holding(false, 2600)); // out and reported by the broker: normal play
    CHECK(!h.holding(true, 2700));  // a later edit's SysEx does not hold notes again
}

TEST(statehold_ends_at_the_bound)
{
    StateHold h;
    CHECK(h.start(true, 1000));
    CHECK(h.holding(true, 1000 + StateHold::LIMIT_MS - 1));
    CHECK(!h.holding(true, 1000 + StateHold::LIMIT_MS)); // a queue that never empties: notes play after 3 s
    CHECK(!h.holding(true, 1000 + StateHold::LIMIT_MS + 1)); // and stay playing
}

TEST(statehold_two_quick_setstates_one_hold)
{
    StateHold h;
    CHECK(h.start(true, 1000));
    CHECK(h.holding(true, 1500));
    CHECK(h.start(true, 1600)); // the second replaces the queued rest of the first (send_state)
    CHECK(h.holding(true, 1000 + StateHold::LIMIT_MS + 100)); // the bound counts from the second
    CHECK(!h.holding(false, 1000 + StateHold::LIMIT_MS + 200));
}

TEST(statehold_a_setstate_after_the_first_ended_holds_again)
{
    StateHold h;
    CHECK(h.start(true, 1000));
    CHECK(!h.holding(false, 2000));
    CHECK(h.start(true, 5000));
    CHECK(h.holding(true, 5001));
}

TEST(statehold_none_without_a_connected_running_virus)
{
    StateHold h;
    // not connected (the queue waits for the Virus, its start-up holds, E591 (2)) or not active (no notes come, the
    // first setActive holds with starting_)
    CHECK(!h.start(false, 1000));
    CHECK(!h.holding(true, 1001));
}

TEST(statehold_nothing_before_a_start)
{
    StateHold h;
    CHECK(!h.holding(true, 0));
    CHECK(!h.holding(true, 1000000));
}
