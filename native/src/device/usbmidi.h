// usbmidi.h - MIDI bytes <-> USB-MIDI event packets (4 bytes: cable << 4 | code index, 3 data bytes), as the Virus
// TI's plug-in channel (EP 0x06 / 0x86) carries them: cable 2 = MIDI/SysEx of the plug-in, cable 5 = F5 time stamps.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace vti {

// One complete MIDI message (channel message, system common or a whole SysEx F0..F7) as packets for `cable`.
inline std::vector<uint8_t> usbmidi_pack(const std::vector<uint8_t> &msg, uint8_t cable)
{
    std::vector<uint8_t> out;
    if (msg.empty())
        return out;
    auto put = [&](uint8_t cin, uint8_t a, uint8_t b, uint8_t c) {
        out.insert(out.end(), {(uint8_t)(cable << 4 | cin), a, b, c});
    };
    uint8_t s = msg[0];
    if (s == 0xF0) {
        for (size_t i = 0; i < msg.size(); i += 3) {
            size_t rest = msg.size() - i;
            uint8_t cin = rest > 3 ? 0x4 : rest == 3 ? 0x7 : rest == 2 ? 0x6 : 0x5;
            put(cin, msg[i], rest > 1 ? msg[i + 1] : 0, rest > 2 ? msg[i + 2] : 0);
        }
    } else if (s >= 0x80 && s < 0xF0) {
        uint8_t cin = s >> 4;
        put(cin, s, msg.size() > 1 ? msg[1] : 0, msg.size() > 2 ? msg[2] : 0);
    } else { // system common / real time: 1..3 bytes
        uint8_t cin = msg.size() == 1 ? 0xF : msg.size() == 2 ? 0x2 : 0x3;
        put(cin, s, msg.size() > 1 ? msg[1] : 0, msg.size() > 2 ? msg[2] : 0);
    }
    return out;
}

// USB-MIDI packets -> complete messages per (endpoint, cable); SysEx is reassembled across packets.
class UsbMidiParser {
public:
    using Handler = std::function<void(uint8_t ep, int cable, const std::vector<uint8_t> &msg)>;
    explicit UsbMidiParser(Handler h) : h_(std::move(h)) {}
    static constexpr size_t MAX_SYSEX = 65536; // the Virus' largest message is a 524-byte single dump

    // bytes of SysEx waiting for their end (all cables); test hook: the tests check the buffer's bound
    size_t pending_bytes() const
    {
        size_t n = 0;
        for (auto &kv : sysex_)
            n += kv.second.size();
        return n;
    }

    void feed(uint8_t ep, const uint8_t *d, size_t len)
    {
        static const int CIN_LEN[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
        for (size_t i = 0; i + 4 <= len; i += 4) {
            if (!d[i])
                continue;
            int cable = d[i] >> 4, cin = d[i] & 15;
            auto &buf = sysex_[ep << 4 | cable];
            const uint8_t *b = d + i + 1;
            if (cin == 4 || (cin >= 5 && cin <= 7 && (!buf.empty() || b[0] == 0xF0))) {
                // F0 starts a new message: a SysEx cut off before it (broker gone, Virus off) is dropped, not glued
                // in front (the readiness probe's reply after a broker reconnect, review 2026-10-19); a part without
                // its start, or one past MAX_SYSEX, is skipped until the next F0
                if (b[0] == 0xF0)
                    buf.clear();
                else if (buf.empty() || buf.size() + 3 > MAX_SYSEX) {
                    buf.clear();
                    continue;
                }
                buf.insert(buf.end(), b, b + CIN_LEN[cin]);
                if (cin != 4) {
                    h_(ep, cable, buf);
                    buf.clear();
                }
            } else if (CIN_LEN[cin]) {
                h_(ep, cable, std::vector<uint8_t>(b, b + CIN_LEN[cin]));
            }
        }
    }

private:
    Handler h_;
    std::map<int, std::vector<uint8_t>> sysex_;
};

} // namespace vti
