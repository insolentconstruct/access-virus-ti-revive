// answerwatch.h - what to do when the Virus does not answer a request (the readiness probe, a bank sync), like the
// original Virus Control 5.1.7 (E591): it re-sets the reply-cable globals after repeated unanswered bank-info requests
// (listener 0x180086bf0) and, when that does not help, shows "A communication error has occurred..." (0x180080ba0).
// Here: the first miss of a Virus connection re-sends the start-up messages that set those cables, a miss after that
// warns; an answer takes the warning back. Lock-free: the probe runs on the sync or the reconnect thread. Header only:
// the plug-in and the tests use it.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace vti {

class AnswerWatch {
public:
    enum Action { NONE, RESEND, WARN };

    // a request got no reply in its whole time (MemorySync::missed: not a stop, a handover or a wrong size)
    Action missed()
    {
        if (!resent_.exchange(true)) // once per connection: re-sending on every miss would only add traffic (rule 2)
            return RESEND;
        return warned_.exchange(true) ? NONE : WARN;
    }
    void answered() { warned_ = false; } // a new miss warns again, it does not re-send again
    void reset()                         // a new connection to the Virus (reconnect, resume, got it after waiting)
    {
        resent_ = false;
        warned_ = false;
    }
    bool warned() const { return warned_; }

    // the start-up messages (GlobalInit from the user's Access files) that set the cables the Virus answers on:
    // Output Sounddump, MIDI Data and Panel Data Cable Number (73 00 3A/3B/3C, E039), and USB MidiPort active = 1
    // (73 00 52 01): with 0x52 = 0 the firmware sends every answer to the 5-pin MIDI OUT and forces the cables back to
    // 1 (R9-1, E601). Unchanged; the link first, as a re-send after 0x52 was lost must not have its cables forced
    // back to 1 before the link is on again, then the cables in their start-up order. Only known traffic goes out
    // again (rule 2), nothing is built here, and never a link off.
    static std::vector<std::vector<uint8_t>> routing_messages(const std::vector<std::vector<uint8_t>> &startup)
    {
        std::vector<std::vector<uint8_t>> out;
        for (const auto &m : startup)
            if (m.size() == 11 && m[6] == 0x73 && m[7] == 0x00 &&
                ((m[8] >= 0x3A && m[8] <= 0x3C) || (m[8] == 0x52 && m[9] == 0x01)))
                out.push_back(m);
        std::stable_partition(out.begin(), out.end(), [](const std::vector<uint8_t> &m) { return m[8] == 0x52; });
        return out;
    }

private:
    std::atomic<bool> resent_{false}, warned_{false};
};

} // namespace vti
