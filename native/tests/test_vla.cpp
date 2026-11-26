// The libusb audio stream (src/broker/vtiusbaudio_libusb.c, Linux and the macOS libusb package) against a stub libusb:
// transfers are kept in a list, the event thread hands back what the test completes or the code cancels.
// Own binary (vlatests): the stubs replace libusb's functions, nothing else may link the real library.
#include "check.h"
#include <libusb.h>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>
extern "C" {
#include "vtiaudioshm.h"
#include "vtiusbaudio_libusb.h"
}

namespace {
std::mutex mu;
std::vector<libusb_transfer *> submitted, pending; // pending: back to the code at the next event round
std::vector<libusb_transfer *> allocs;             // mu: every transfer in allocation order
std::vector<uint32_t> deliveries;                  // mu: frames per call of record_in
std::atomic<bool> fail_submit{false};
std::atomic<int> claims{0};
std::atomic<int> fail_claim{-1};          // this interface's claim fails
std::atomic<int> fail_alloc{-1};          // this allocation (counted down) gives NULL
std::vector<libusb_device_handle *> touched; // mu: handles given to release / set_alt

void quiet(const char *, ...) {}
void on_in(const int16_t *, uint32_t, void *) {}
void on_out(int16_t *, uint32_t, void *) {}
void record_in(const int16_t *, uint32_t frames, void *)
{
    std::lock_guard<std::mutex> lk(mu);
    deliveries.push_back(frames);
}

void hand_back(libusb_transfer *t, libusb_transfer_status st) // mu held
{
    for (size_t i = 0; i < submitted.size(); i++)
        if (submitted[i] == t) {
            submitted.erase(submitted.begin() + (long)i);
            t->status = st;
            pending.push_back(t);
            return;
        }
}

// every queued IN transfer comes back complete (no audio in it); returns once the code has seen them all
void complete_ins()
{
    {
        std::lock_guard<std::mutex> lk(mu);
        for (auto *t : std::vector<libusb_transfer *>(submitted))
            if (t->endpoint == 0x82)
                hand_back(t, LIBUSB_TRANSFER_COMPLETED);
    }
    for (int i = 0; i < 200; i++) {
        {
            std::lock_guard<std::mutex> lk(mu);
            if (pending.empty())
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); // the last callback returns
}
} // namespace

extern "C" {
void (*vti_log)(const char *fmt, ...) = quiet;

struct libusb_transfer *LIBUSB_CALL libusb_alloc_transfer(int iso_packets)
{
    if (fail_alloc >= 0 && fail_alloc-- == 0)
        return nullptr;
    auto *t = (libusb_transfer *)calloc(1, sizeof(libusb_transfer) + (size_t)iso_packets * sizeof(libusb_iso_packet_descriptor));
    std::lock_guard<std::mutex> lk(mu);
    allocs.push_back(t);
    return t;
}
void LIBUSB_CALL libusb_free_transfer(struct libusb_transfer *t) { free(t); }
int LIBUSB_CALL libusb_submit_transfer(struct libusb_transfer *t)
{
    if (fail_submit)
        return LIBUSB_ERROR_IO;
    std::lock_guard<std::mutex> lk(mu);
    submitted.push_back(t);
    return 0;
}
int LIBUSB_CALL libusb_cancel_transfer(struct libusb_transfer *t)
{
    std::lock_guard<std::mutex> lk(mu);
    size_t n = pending.size();
    hand_back(t, LIBUSB_TRANSFER_CANCELLED);
    return pending.size() > n ? 0 : LIBUSB_ERROR_NOT_FOUND;
}
int LIBUSB_CALL libusb_handle_events_timeout_completed(libusb_context *, struct timeval *, int *)
{
    std::vector<libusb_transfer *> now;
    {
        std::lock_guard<std::mutex> lk(mu);
        now.swap(pending);
    }
    for (auto *t : now)
        t->callback(t);
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    return 0;
}
int LIBUSB_CALL libusb_control_transfer(libusb_device_handle *, uint8_t, uint8_t, uint16_t, uint16_t, unsigned char *,
                                        uint16_t len, unsigned int)
{
    return len;
}
int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *, int iface)
{
    if (iface == fail_claim)
        return LIBUSB_ERROR_BUSY;
    claims++;
    return 0;
}
int LIBUSB_CALL libusb_release_interface(libusb_device_handle *h, int)
{
    std::lock_guard<std::mutex> lk(mu);
    touched.push_back(h);
    return 0;
}
int LIBUSB_CALL libusb_set_interface_alt_setting(libusb_device_handle *h, int, int)
{
    std::lock_guard<std::mutex> lk(mu);
    touched.push_back(h);
    return 0;
}
const char *LIBUSB_CALL libusb_error_name(int) { return "stub"; }
}

TEST(vla_stream_without_in_transfers_is_not_running_and_restarts)
{
    // audit #14: every IN resubmit fails without NO_DEVICE (a USB error burst): no audio comes any more, so the broker
    // must see a stopped stream, and the next start must stop the old one (event thread, OUT transfers) first
    auto *h = reinterpret_cast<libusb_device_handle *>(0x1);
    CHECK_EQ(vla_start(nullptr, h, 44100, on_in, on_out, nullptr), 0);
    CHECK(vla_running());
    CHECK_EQ(claims.load(), 3);
    fail_submit = true;
    complete_ins();
    CHECK(!vla_running());
    fail_submit = false;
    CHECK_EQ(vla_start(nullptr, h, 44100, on_in, on_out, nullptr), 0);
    CHECK_EQ(claims.load(), 6); // started again, not "already running"
    CHECK(vla_running());
    vla_stop();
    CHECK(!vla_running());
    std::lock_guard<std::mutex> lk(mu);
    CHECK(submitted.empty()); // every transfer came back before it was freed
}

TEST(vla_claim_failure_forgets_the_handle)
{
    // B2: a failed claim (another driver holds the interface) must not leave the handle behind: after a replug the
    // next start would stop the old stream on the freed handle first
    auto *gone = reinterpret_cast<libusb_device_handle *>(0x2), *fresh = reinterpret_cast<libusb_device_handle *>(0x3);
    fail_claim = 1;
    CHECK_EQ(vla_start(nullptr, gone, 44100, on_in, on_out, nullptr), -1);
    fail_claim = -1;
    {
        std::lock_guard<std::mutex> lk(mu);
        touched.clear(); // interface 0 was given back on `gone` before the error: fine, the handle was still open
    }
    CHECK_EQ(vla_start(nullptr, fresh, 44100, on_in, on_out, nullptr), 0);
    vla_stop();
    std::lock_guard<std::mutex> lk(mu);
    for (auto *h : touched)
        CHECK(h == fresh);
}

TEST(vla_transfer_alloc_failure_is_an_error)
{
    // B10: a transfer libusb cannot allocate was filled through NULL; now the start fails and gives everything back
    auto *h = reinterpret_cast<libusb_device_handle *>(0x4);
    int first_in = -1; // the start allocates every OUT transfer first, then the IN ones
    {
        std::lock_guard<std::mutex> lk(mu);
        allocs.clear();
    }
    CHECK_EQ(vla_start(nullptr, h, 44100, on_in, on_out, nullptr), 0);
    {
        std::lock_guard<std::mutex> lk(mu);
        for (size_t i = 0; i < allocs.size() && first_in < 0; i++)
            if (allocs[i]->endpoint == 0x82)
                first_in = (int)i;
    }
    vla_stop();
    CHECK(first_in > 0);
    for (int k : {0, first_in}) { // the first OUT and the first IN transfer
        fail_alloc = k;
        CHECK_EQ(vla_start(nullptr, h, 44100, on_in, on_out, nullptr), -1);
        CHECK(!vla_running());
        fail_alloc = -1;
    }
    CHECK_EQ(vla_start(nullptr, h, 44100, on_in, on_out, nullptr), 0);
    vla_stop();
    std::lock_guard<std::mutex> lk(mu);
    CHECK(submitted.empty());
}

TEST(vla_delivers_4_ms_pieces_with_80_ms_in_flight)
{
    // R5-1 (research 2026-11-26): the original gets the Virus' audio in 128-frame blocks (E402) and reads 512 frames
    // behind the newest; whole 10 ms transfers (441 frames) took about half of that margin as a sawtooth. Now pieces of
    // VTI_AUDIO_NP packets (4 ms = 176/177 frames), with as much queued as before (80 ms)
    auto *h = reinterpret_cast<libusb_device_handle *>(0x5);
    {
        std::lock_guard<std::mutex> lk(mu);
        deliveries.clear();
    }
    CHECK_EQ(VTI_AUDIO_NP, 4);
    CHECK_EQ(VTI_AUDIO_NP * VTI_AUDIO_NR, 80);
    CHECK_EQ(vla_start(nullptr, h, 44100, record_in, on_out, nullptr), 0);
    int ins = 0, outs = 0;
    {
        std::lock_guard<std::mutex> lk(mu);
        for (auto *t : submitted)
            if (t->endpoint == 0x82) {
                ins++;
                CHECK_EQ(t->num_iso_packets, VTI_AUDIO_NP);
            } else
                outs++;
    }
    CHECK_EQ(ins, VTI_AUDIO_NR);
    CHECK(outs > 0); // the OUT lead-in
    // a held event thread: every queued IN transfer completes at once, and each one's OUT mirror is queued too (none
    // lost for want of a free OUT transfer while the earlier ones are still on the wire)
    complete_ins();
    {
        std::lock_guard<std::mutex> lk(mu);
        int outs_now = 0;
        for (auto *t : submitted)
            outs_now += t->endpoint != 0x82;
        CHECK_EQ(outs_now, outs + VTI_AUDIO_NR);
        CHECK_EQ((int)deliveries.size(), VTI_AUDIO_NR);
        for (uint32_t f : deliveries) // lost packets are repaired with the nominal pattern: 4 x 44, one 45 in ten
            CHECK(f == 176 || f == 177);
    }
    vla_stop();
}
