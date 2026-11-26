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

#include "hostmidi.h"
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
// reach only the DSPs, and the host CPU would keep the old value in its edit buffer (E274). The same for controllers,
// poly pressure and channel pressure (R4-3: page A/B parameters go as CC or poly pressure; the original queues all
// three behind its dump on cable 2, 0x18007a5a0), but not the pedals 64-69 (they go at once, their timing against the
// notes) nor the mode messages 120-127 (in order with the notes). Notes, bend and other SysEx from process() are not
// held: the sync ping needs its own raw stamp in its block (the marker follows it) and the transport tick belongs to
// its F2. Several changes of one parameter (controller, key) keep only the latest value, in first-arrival order.
class HeldParams {
public:
    static bool held(const uint8_t *m, size_t n) { return access_param(m, n) || held_channel(m, n); }
    static bool held_channel(const uint8_t *m, size_t n)
    {
        const uint8_t s = n ? m[0] & 0xF0 : 0;
        if (n == 3 && s == 0xB0)
            return m[1] < 120 && (m[1] < 64 || m[1] > 69);
        return (n == 3 && s == 0xA0) || (n == 2 && s == 0xD0);
    }
    // the part a held message is for: the SysEx' part byte, else the channel (encode_into: channel = part)
    static int part(const std::vector<uint8_t> &m) { return m[0] == 0xF0 ? m[7] : m[0] & 15; }
    void add(const std::vector<uint8_t> &m)
    {
        auto same = [&](const std::vector<uint8_t> &h) { return same_param(h, m); };
        auto it = std::find_if(list_.begin(), list_.end(), same);
        if (it != list_.end())
            *it = m;
        else
            list_.push_back(m);
    }
    // R7-3: a single dump of `part` was queued: what is held of it so far would overwrite the new patch after the dump
    // (the original clears the part's waiting slots at its dump, 0x18007aa50). Standard controllers and channel
    // pressure are not in the patch and stay; what is held from now on survives
    void drop_part(int part)
    {
        list_.erase(std::remove_if(list_.begin(), list_.end(),
                                   [&](const std::vector<uint8_t> &h) {
                                       const uint8_t s = h[0] & 0xF0;
                                       return HeldParams::part(h) == part &&
                                              (h[0] == 0xF0 || s == 0xA0 || (s == 0xB0 && !standard_controller(h[1])));
                                   }),
                    list_.end());
    }
    std::vector<std::vector<uint8_t>> take()
    {
        std::vector<std::vector<uint8_t>> out;
        out.swap(list_);
        return out;
    }
    bool empty() const { return list_.empty(); }

    // one parameter: part + parameter (SysEx), channel + controller or key, channel pressure of one channel
    static bool same_param(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
    {
        if (a.size() != b.size())
            return false;
        if (a.size() == 11)
            return a[6] == b[6] && a[7] == b[7] && a[8] == b[8];
        return a[0] == b[0] && (a.size() == 2 || a[1] == b[1]); // status + channel, controller or key
    }

private:
    std::vector<std::vector<uint8_t>> list_;
};

// the part whose edit buffer a single dump (10 00 pp, 524 bytes) is for: 0..15 multi mode, 0x40 single mode; else -1
inline int dump_part(const std::vector<uint8_t> &m)
{
    return m.size() == 524 && m[0] == 0xF0 && m[6] == 0x10 && m[7] == 0x00 ? m[8] : -1;
}

// A newer value goes out (or is requeued) while an older held one of the same parameter still waits in the queue
// (`.held`: put there by requeue_held): the older one goes, else it would follow the newer one and win - after a
// dump up to 8 real-time events go before the queue (review of T10). Returns how many went.
template <class Queue> int drop_superseded(Queue &q, const std::vector<uint8_t> &m)
{
    if (q.empty() || !HeldParams::held(m.data(), m.size()))
        return 0;
    const size_t before = q.size();
    auto stale = [&](const auto &e) { return e.held && HeldParams::same_param(e.msg, m); };
    q.erase(std::remove_if(q.begin(), q.end(), stale), q.end());
    return (int)(before - q.size());
}

// The held messages at the dump's end back into the queue, in first-arrival order: in front, so they go next on
// cable 2 - but behind a queued single dump of their part. What is still held of that part came after that dump was
// queued (drop_part), and in front of it the new patch would overwrite it (R7-3). An older requeued entry of the same
// parameter still waiting goes (drop_superseded). `make` builds a queue entry with .held set. Returns the change in
// the queue's length.
template <class Queue, class Make> int requeue_held(Queue &q, std::vector<std::vector<uint8_t>> held, Make make)
{
    int dropped = 0;
    for (auto &m : held)
        dropped += drop_superseded(q, m);
    size_t front = 0;
    int behind[128] = {}; // per part: how many already went behind its dump
    for (auto &m : held) {
        const int p = HeldParams::part(m) & 0x7F;
        size_t at = front;
        for (size_t i = q.size(); i-- > front;)
            if (dump_part(q[i].msg) == p) {
                at = i + 1 + (size_t)behind[p]++;
                break;
            }
        if (at == front)
            front++;
        q.insert(q.begin() + (long)at, make(std::move(m)));
    }
    return (int)held.size() - dropped;
}

} // namespace vti
