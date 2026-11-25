// The brokers' control request filter (AGENTS.md rule 2): a client may only read standard information (descriptors,
// status, configuration); vendor and class requests, and every host-to-device request, never reach the Virus.
// Hardware-free: the decision only.
#include "check.h"
#include <algorithm>
#include <vector>
extern "C" {
#include "ctrlguard.h"
}

static int allowed(uint8_t rt, uint8_t req)
{
    const uint8_t setup[8] = {rt, req, 0, 1, 0, 0, 18, 0};
    return vti_control_allowed(setup);
}

TEST(ctrlguard_standard_reads)
{
    CHECK(allowed(0x80, 6) == 1);  // GET_DESCRIPTOR (device): plug-in and Control Center ask for the model this way
    CHECK(allowed(0x80, 0) == 1);  // GET_STATUS
    CHECK(allowed(0x80, 8) == 1);  // GET_CONFIGURATION
    CHECK(allowed(0x81, 6) == 1);  // GET_DESCRIPTOR (interface)
    CHECK(allowed(0x81, 10) == 1); // GET_INTERFACE
    CHECK(allowed(0x82, 0) == 1);  // GET_STATUS (endpoint)
}

TEST(ctrlguard_blocks_vendor_and_writes)
{
    CHECK(allowed(0x40, 1) == 0);    // vendor OUT to the device (flash protocol address write, E026/E040)
    CHECK(allowed(0x41, 1) == 0);    // vendor OUT to the interface
    CHECK(allowed(0xC0, 0x81) == 0); // vendor IN (flash status/data, E040): not needed by any client
    CHECK(allowed(0xC1, 0x81) == 0);
    CHECK(allowed(0x21, 1) == 0);    // class OUT (e.g. audio SET_CUR)
    CHECK(allowed(0xA1, 0x81) == 0); // class IN
    CHECK(allowed(0x00, 9) == 0);    // SET_CONFIGURATION
    CHECK(allowed(0x01, 11) == 0);   // SET_INTERFACE
    CHECK(allowed(0x00, 7) == 0);    // SET_DESCRIPTOR
    CHECK(allowed(0x02, 1) == 0);    // CLEAR_FEATURE (endpoint)
    CHECK(allowed(0x80, 7) == 0);    // a standard IN type with a host-to-device request number
    CHECK(allowed(0x83, 6) == 0);    // recipient "other"
    CHECK(allowed(0x80, 12) == 0);   // SYNCH_FRAME is not needed either
}

// audit 2026-11-17 #2: a client's WRITE names its endpoint; every client writes the plug-in channel EP 0x06 only.
// EP 0x05 (MI03 OUT) is the USB chip's command endpoint (one known command, the driver's 4E 73 52 01 at device start, E030; the broker sends it itself); an IN endpoint on libusb
// would run an IN transfer into the client's buffer and steal the reader's data
TEST(ctrlguard_write_only_to_the_plugin_channel)
{
    CHECK(vti_write_allowed(0x06) == 1);
    CHECK(vti_write_allowed(0x05) == 0);
    CHECK(vti_write_allowed(0x85) == 0);
    CHECK(vti_write_allowed(0x86) == 0);
    CHECK(vti_write_allowed(0x00) == 0);
    CHECK(vti_write_allowed(0x01) == 0);
    CHECK(vti_write_allowed(0x02) == 0); // audio OUT is the broker's own (shared memory), never a client write
    CHECK(vti_write_allowed(0xFF) == 0);
}

TEST(ctrlguard_length_never_past_what_the_client_sent)
{
    // audit #20: an OUT request whose wLength is larger than the data in the message logged uninitialised bytes
    const uint8_t out[8] = {0x40, 0x01, 0, 0, 0, 0, 100, 0}, in[8] = {0x80, 0x06, 0, 1, 0, 0, 0xff, 0xff};
    CHECK_EQ(vti_control_len(out, 4), 4u);      // 4 data bytes sent, 100 claimed
    CHECK_EQ(vti_control_len(out, 200), 100u);  // more sent than claimed: wLength counts
    CHECK_EQ(vti_control_len(in, 0), 4096u - 16); // a read: wLength, capped to the reply buffer
}

// B3 (rule 2, the brokers' copy of the plug-in's sendguard.h): no client may send the Multi parameters 0-3 of page
// 0x72 (Flags, Original Bank, Original Patch: issue #7, E255/E258) or status 0xF4 (the DSP's private commands,
// E236/E248), whatever the client (Control Center, virusctl, a buggy one). The data are USB-MIDI packets.
static std::vector<uint8_t> pack(uint8_t cable, std::vector<uint8_t> sx) // one SysEx as CIN 4..7 packets
{
    std::vector<uint8_t> o;
    for (size_t i = 0; i < sx.size(); i += 3) {
        const size_t n = std::min<size_t>(3, sx.size() - i);
        o.push_back((uint8_t)(cable << 4 | (i + 3 >= sx.size() ? 4 + n : 4)));
        for (size_t k = 0; k < 3; k++)
            o.push_back(k < n ? sx[i + k] : 0);
    }
    return o;
}
static std::vector<uint8_t> param72(uint8_t param) { return {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x72, 0x00, param, 5, 0xF7}; }
static int blocked(vti_sxwatch &w, const std::vector<uint8_t> &d) { return vti_write_blocked(&w, d.data(), (int)d.size()); }

TEST(ctrlguard_original_patch_never_reaches_the_virus)
{
    vti_sxwatch w{};
    for (uint8_t p = 0; p <= 3; p++)
        CHECK(blocked(w, pack(2, param72(p))) == 1);
    CHECK(blocked(w, pack(3, param72(3))) == 1); // on any cable
    CHECK(blocked(w, pack(2, param72(4))) == 0); // 4..13 are the multi name: allowed
    CHECK(blocked(w, {0x2F, 0xF4, 0x00, 0x00}) == 1);
    CHECK(blocked(w, {0x22, 0xF4, 0x01, 0x00}) == 1);
}

TEST(ctrlguard_original_patch_split_over_two_writes)
{
    // a client cuts its writes at 1024 bytes: the message may straddle two; the part from the parameter byte on is held
    vti_sxwatch w{};
    auto pk = pack(2, param72(3)); // 4 packets: F0 00 20 | 33 01 10 | 72 00 03 | 05 F7
    CHECK(blocked(w, std::vector<uint8_t>(pk.begin(), pk.begin() + 8)) == 0);
    CHECK(blocked(w, std::vector<uint8_t>(pk.begin() + 8, pk.end())) == 1);
    CHECK(blocked(w, pack(2, param72(4))) == 0); // and the watch is reset after it
}

TEST(ctrlguard_plugin_traffic_passes)
{
    vti_sxwatch w{};
    CHECK(blocked(w, {0x5F, 0xF5, 0x12, 0x34, 0x29, 0x90, 0x3C, 0x64}) == 0); // stamp on cable 5 + note-on
    std::vector<uint8_t> dump = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x01, 0x00};
    dump.resize(523, 0x72); // a single dump whose data bytes look like the page
    dump.push_back(0xF7);
    CHECK(blocked(w, pack(3, dump)) == 0);
    CHECK(blocked(w, pack(2, {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x03, 5, 0xF7})) == 0); // page A param 3
    CHECK(blocked(w, {0x2B, 0xB0, 0x07, 0x64}) == 0);
}
