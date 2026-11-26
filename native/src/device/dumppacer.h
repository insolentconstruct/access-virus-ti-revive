// dumppacer.h - a single dump on cable 3 goes out in pieces, with the DAW's events in between on cable 5.
//
// Why: the original writes a patch dump (524 bytes = 700 bytes of USB-MIDI packets) in pieces of 64 bytes, one
// every 6-12 ms (~130 ms per dump, also when the patches change fast; never two dumps at once), and sends its notes, clock ticks and time stamps between them on cable 5 (capture of a patch change
// while another part plays, E270). Cable 3 reaches only the Virus' host CPU, which parses the dump byte by byte;
// cable 2 would put a note's bytes into that parse (one parser for all cables but 5, E252), cable 5 goes to the DSPs
// only. Written in one piece, the dump held the notes until the Virus had taken all of it, and the other parts
// stopped for a moment (E269). Queued messages wait until the dump is out (they go on cable 2), queued notes excepted
// (between_pieces).
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "stamprule.h"

namespace vti {

class DumpPacer {
public:
    static constexpr size_t PIECE = 64;              // bytes per write (16 USB-MIDI packets), as the original
    static constexpr int64_t GAP_NS = 10 * 1000000LL; // between two pieces: the original's average is ~11 ms

    bool active() const { return off_ < pk_.size(); }
    // the packed USB-MIDI bytes of a dump; the first piece is due at once
    void start(const std::vector<uint8_t> &pk, int64_t now_ns)
    {
        pk_ = pk;
        off_ = 0;
        due_ = now_ns;
    }
    // the next piece if it is due
    bool next(int64_t now_ns, std::vector<uint8_t> &out)
    {
        if (!active() || now_ns < due_)
            return false;
        size_t n = pk_.size() - off_ < PIECE ? pk_.size() - off_ : PIECE;
        out.assign(pk_.begin() + (long)off_, pk_.begin() + (long)(off_ + n));
        off_ += n;
        due_ = now_ns + GAP_NS;
        if (!active())
            pk_.clear(), off_ = 0;
        return true;
    }
    // nanoseconds until the next piece is due (0: due now or no dump)
    int64_t wait_ns(int64_t now_ns) const { return active() && now_ns < due_ ? due_ - now_ns : 0; }
    // the cable for an event from the DAW: 5 while a dump runs (the host CPU is in the middle of parsing it)
    uint8_t event_cable(uint8_t cable) const { return active() ? 5 : cable; }

private:
    std::vector<uint8_t> pk_;
    size_t off_ = 0;
    int64_t due_ = 0;
};

// The worker's fairness rule: after MAX events from process() in a row the queue side gets its turn, else dense MIDI
// starves the queue - but only when it can go now (queue_ready). A dump in pieces is the queue's turn piece by
// piece. Asking only "is something queued" made the events wait for what could not go: for a dump's last piece
// (~130 ms), the next piece (10 ms) or the hold after a multi dump (250 ms) - late notes (review 2026-10-19).
class EventRun {
public:
    static constexpr int MAX = 8;
    bool events_first(bool queue_ready) const { return run_ < MAX || !queue_ready; }
    void event() { run_++; }
    void turn() { run_ = 0; } // a queued message or a piece of a dump went out

private:
    int run_ = 0;
};

// the queue side can go now: while a dump runs its next piece is due (queued messages wait for its end), else a
// queued message that is not held (after a multi dump the Virus sets up its parts)
inline bool queue_ready(const DumpPacer &dump, int64_t now_ns, bool queue_empty, bool held)
{
    return dump.active() ? dump.wait_ns(now_ns) == 0 : !queue_empty && !held;
}

// R12-8: the one kind of queued message that goes between a dump's pieces (on cable 5, before the next piece), like
// the original's short messages before its dump: a note on or off, as the DAW's own notes do (E270). The rest waits for
// the dump's end: on cable 2 or 3 a controller, program change or SysEx would land in the host CPU's parse of the dump
// (one parser for all cables but 5, E252, R9-5), and on cable 5 a controller or program change would reach the DSPs
// only, not the host CPU that must see it (E274).
inline bool between_pieces(const uint8_t *m, size_t n) { return n == 3 && m[0] >= 0x80 && m[0] < 0xA0; }

// Parameter SysEx that falls into a dump waits for its end (at most ~140 ms): on cable 5 with the notes it would
// reach only the DSPs, and the host CPU would keep the old value in its edit buffer (E274). Notes and other
// channel messages are not held, nor other SysEx from process(): the sync ping needs its own raw stamp in its block
// (the marker follows it) and the transport tick belongs to its F2. Several changes of one parameter keep only the
// latest value, in first-arrival order.
class HeldParams {
public:
    // a parameter of page A-E, the messages stamprule.h stamps for automation
    static bool held(const uint8_t *m, size_t n) { return access_param(m, n); }
    void add(const std::vector<uint8_t> &m)
    {
        auto same = [&](const std::vector<uint8_t> &h) { return same_param(h, m); };
        auto it = std::find_if(list_.begin(), list_.end(), same);
        if (it != list_.end())
            *it = m;
        else
            list_.push_back(m);
    }
    std::vector<std::vector<uint8_t>> take()
    {
        std::vector<std::vector<uint8_t>> out;
        out.swap(list_);
        return out;
    }
    bool empty() const { return list_.empty(); }

private:
    static bool same_param(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
    {
        return a.size() == 11 && b.size() == 11 && a[6] == b[6] && a[7] == b[7] && a[8] == b[8];
    }
    std::vector<std::vector<uint8_t>> list_;
};

} // namespace vti
