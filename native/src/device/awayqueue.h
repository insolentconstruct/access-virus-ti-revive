// awayqueue.h - the plug-in's send queue while a Virus it had is away (research R6-3). The original clears its
// start-up queue while no Virus is connected and builds everything again from its model at the connection; ours kept
// it, so leftovers (an unfinished start-up block, editor edits, sync requests) went out in front of the new start-up.
// A Virus that was never there keeps the whole queue: the start-up and the state wait for it (E591 (2)).
#pragma once
#include <algorithm>
#include <deque>

namespace vti {

// Drops what the reconnect sends again from the plug-in's state (start-up messages, the state, edits recorded in
// it); keeps the entries marked keep, which no state brings back (REMOTE template edits). Returns how many went.
template <class Q> int drop_while_away(std::deque<Q> &q)
{
    const size_t before = q.size();
    q.erase(std::remove_if(q.begin(), q.end(), [](const Q &e) { return !e.keep; }), q.end());
    return (int)(before - q.size());
}

} // namespace vti
