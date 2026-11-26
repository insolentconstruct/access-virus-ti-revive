// The broker's packetised device write with retries (src/broker/writeretry.h): a timed-out packet is sent again,
// the message stays intact, a real error stops at once, a partial acceptance (WinUSB) continues from there. T5: a
// busy Virus never cuts a message; only the bound (device failed), a real error or keep() == 0 ends it early.
#include "check.h"
#include "writeretry.h"
#include <vector>

namespace {
struct Fake {
    std::vector<int> script; // per call: 0 = accept, 1 = timeout, -1 = error, 2 = accept half then timeout
    size_t call = 0;
    std::vector<uint8_t> got; // bytes accepted, in order
    std::vector<int> sizes;   // packet sizes offered
    int keep_calls = 0, keep_until = -1; // keep() says stop from call keep_until on (-1 = never)
};
int keep(void *ctx)
{
    Fake &f = *(Fake *)ctx;
    ++f.keep_calls;
    return f.keep_until < 0 || f.keep_calls < f.keep_until;
}
int xfer(void *ctx, const uint8_t *data, int n, int *done)
{
    Fake &f = *(Fake *)ctx;
    int what = f.call < f.script.size() ? f.script[f.call] : 0;
    f.call++;
    f.sizes.push_back(n);
    *done = 0;
    if (what == 0) {
        f.got.insert(f.got.end(), data, data + n);
        *done = n;
        return 0;
    }
    if (what == 2) {
        f.got.insert(f.got.end(), data, data + n / 2);
        *done = n / 2;
        return VTI_XFER_TIMEOUT;
    }
    return what == 1 ? VTI_XFER_TIMEOUT : -5;
}
std::vector<uint8_t> msg(int n)
{
    std::vector<uint8_t> m;
    for (int i = 0; i < n; i++)
        m.push_back((uint8_t)i);
    return m;
}
} // namespace

TEST(writeretry_packets_of_max)
{
    Fake f;
    auto m = msg(70);
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, 5, nullptr, nullptr) == 0);
    CHECK(f.got == m);
    CHECK(f.sizes == std::vector<int>({32, 32, 6}));
}

TEST(writeretry_timeout_retries_same_packet)
{
    Fake f;
    f.script = {0, 1, 1, 0, 0}; // the second packet times out twice, then goes
    auto m = msg(70);
    int timeouts = 0;
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, 5, &timeouts, nullptr) == 0);
    CHECK(f.got == m); // nothing duplicated, nothing lost
    CHECK(timeouts == 2);
    CHECK(f.call == 5);
}

TEST(writeretry_gives_up_after_attempts)
{
    Fake f;
    f.script = {1, 1, 1, 1, 1, 1};
    auto m = msg(32);
    int timeouts = 0;
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, 5, &timeouts, nullptr) == VTI_WRITE_STUCK);
    CHECK(timeouts == 5 && f.call == 5);
    CHECK(f.got.empty());
}

TEST(writeretry_error_stops_at_once)
{
    Fake f;
    f.script = {0, -1, 0};
    auto m = msg(64);
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, 5, nullptr, nullptr) == -1);
    CHECK(f.call == 2 && f.got.size() == 32);
}

TEST(writeretry_partial_then_timeout_continues)
{
    Fake f;
    f.script = {2, 0}; // WinUSB: half the message accepted before the timeout, the rest in the retry
    auto m = msg(64);
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 64, 5, nullptr, nullptr) == 0);
    CHECK(f.got == m);
    CHECK(f.sizes == std::vector<int>({64, 32}));
}

TEST(writeretry_busy_virus_never_cuts_the_message)
{
    // E591 (3): the Virus refused EP 0x06 for ~8.7 s; with 1 s tries and the broker's bound that is 9 timeouts of one
    // packet, and a few more on the next: all of the message still goes, nothing twice
    Fake f;
    f.script = {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0};
    f.keep_until = -1;
    auto m = msg(100);
    int timeouts = 0;
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, VTI_WRITE_TRIES, &timeouts, keep) == 0);
    CHECK(f.got == m);
    CHECK_EQ(timeouts, 12);
    CHECK_EQ(f.keep_calls, 12); // asked once per timeout, before the retry
    CHECK(VTI_WRITE_TRIES >= 30); // well above the 8.7 s seen
}

TEST(writeretry_bound_means_failed_not_dropped)
{
    // one packet refused VTI_WRITE_TRIES times in a row: the caller treats the device as failed (STUCK), it does not
    // go on with the rest as if the message had gone
    Fake f;
    f.script.assign(VTI_WRITE_TRIES + 5, 1);
    f.script[0] = 0;
    auto m = msg(64);
    int timeouts = 0;
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, VTI_WRITE_TRIES, &timeouts, nullptr) == VTI_WRITE_STUCK);
    CHECK_EQ(timeouts, VTI_WRITE_TRIES);
    CHECK_EQ((int)f.call, VTI_WRITE_TRIES + 1);
    CHECK_EQ(f.got.size(), (size_t)32);
}

TEST(writeretry_keep_stops_a_waiting_write)
{
    // the client left, the device went or the broker exits: stop at the next retry, not after the bound
    Fake f;
    f.script.assign(10, 1);
    f.keep_until = 3; // the third check says stop
    auto m = msg(32);
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, VTI_WRITE_TRIES, nullptr, keep) == VTI_WRITE_STOPPED);
    CHECK_EQ(f.keep_calls, 3);
    CHECK_EQ((int)f.call, 3); // checked after each timeout, before the next try
    CHECK(f.got.empty());
}

TEST(writeretry_keep_not_asked_while_the_virus_takes_everything)
{
    // the check (a poll per call in the broker) costs nothing on the normal path
    Fake f;
    f.keep_until = 1; // would stop at once if asked
    auto m = msg(96);
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, VTI_WRITE_TRIES, nullptr, keep) == 0);
    CHECK_EQ(f.keep_calls, 0);
    CHECK(f.got == m);
}

TEST(writeretry_error_after_timeouts_stops)
{
    // a real error (stall, device gone) ends the write at once even while it was waiting
    Fake f;
    f.script = {1, 1, -1, 0};
    auto m = msg(32);
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, VTI_WRITE_TRIES, nullptr, keep) == VTI_WRITE_ERROR);
    CHECK_EQ((int)f.call, 3);
}
