// The POSIX broker's handling of client messages (src/broker/vtibroker_posix.c, through brokerharness.c): a socketpair
// is one client, the device is a stub. Linux only (own binary brokertests); nothing here opens USB.
#include "check.h"
#include <chrono>
#include <cstring>
#include <functional>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
extern "C" {
#include "owner.h"
#include "vtiproto.h"
void bh_device(int open);
void bh_serve(int fd);
int bh_clients(void);
int bh_observers(void);
int bh_writes(uint8_t *last, int *len);
void bh_busy(int on);
int bh_inside(void);
int bh_stopped(void);
}

namespace {
int client() // the test's end of a new broker client
{
    int s[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, s))
        return -1;
    bh_serve(s[1]);
    return s[0];
}

void msg(int fd, uint16_t type, const std::vector<uint8_t> &payload)
{
    vtimsg h{type, (uint16_t)payload.size()};
    std::vector<uint8_t> b((uint8_t *)&h, (uint8_t *)&h + sizeof h);
    b.insert(b.end(), payload.begin(), payload.end());
    CHECK_EQ(::write(fd, b.data(), b.size()), (ssize_t)b.size());
}

bool until(const std::function<bool()> &f, int ms = 2000)
{
    for (int i = 0; i < ms; i++) {
        if (f())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return f();
}

void hello(int fd, const std::string &name)
{
    std::vector<uint8_t> p = {VTI_PROTO_VERSION, 0, 0, 0};
    p.insert(p.end(), name.begin(), name.end());
    msg(fd, VTIMSG_HELLO, p);
}

// the next VTIMSG_STATE the client gets (other messages are skipped), -1 when none comes within 2 s
int next_state(int fd)
{
    for (;;) {
        pollfd p{fd, POLLIN, 0};
        vtimsg h;
        uint8_t b[VTIMSG_MAX];
        if (poll(&p, 1, 2000) != 1 || ::read(fd, &h, sizeof h) != (ssize_t)sizeof h)
            return -1;
        for (size_t got = 0; got < h.len;) {
            const ssize_t r = ::read(fd, b + got, h.len - got);
            if (r <= 0)
                return -1;
            got += (size_t)r;
        }
        if (h.type == VTIMSG_STATE && h.len >= 4)
            return b[0];
    }
}

int writes()
{
    uint8_t b[VTIMSG_MAX];
    int n;
    return bh_writes(b, &n);
}
} // namespace

TEST(broker_second_observe_keeps_one_observer)
{
    // B1: a second OBSERVE attached a second queue and writer; the first stayed in the observer list after the client
    // was freed (its writer then wrote to freed memory)
    int fd = client();
    msg(fd, VTIMSG_OBSERVE, {0});
    msg(fd, VTIMSG_OBSERVE, {0});
    CHECK(until([] { return bh_observers() >= 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // the second one is handled (no reply to wait for)
    CHECK_EQ(bh_observers(), 1);
    close(fd);
    CHECK(until([] { return bh_clients() == 0; }));
    CHECK_EQ(bh_observers(), 0);
}

TEST(broker_write_never_passes_original_patch)
{
    // B3 (rule 2): the plug-in's sendguard.h rule for every client - a write carrying Multi "Original Patch" (72 00 03)
    // or status F4 is refused whole; other writes reach the Virus
    bh_device(1);
    int fd = client();
    const std::vector<uint8_t> note = {0x06, 0x29, 0x90, 0x3C, 0x64};
    const std::vector<uint8_t> orig = {0x06, 0x24, 0xF0, 0x00, 0x20, 0x24, 0x33, 0x01, 0x10,
                                       0x24, 0x72, 0x00, 0x03, 0x26, 0x05, 0xF7, 0x00};
    msg(fd, VTIMSG_WRITE, note);
    CHECK(until([] { return writes() == 1; }));
    msg(fd, VTIMSG_WRITE, orig);
    msg(fd, VTIMSG_WRITE, {0x06, 0x2F, 0xF4, 0x00, 0x00});
    msg(fd, VTIMSG_WRITE, note);
    CHECK(until([] { return writes() == 2; }));
    uint8_t last[VTIMSG_MAX];
    int n = 0;
    CHECK_EQ(bh_writes(last, &n), 2);
    CHECK_EQ(n, 4);
    CHECK_EQ(last[0], 0x29);
    close(fd);
    CHECK(until([] { return bh_clients() == 0; }));
    bh_device(0);
}

TEST(broker_owner_hands_over_on_observe_and_close)
{
    // B11 (the POSIX side of B6): the owning plug-in becomes an observer -> the next waiting plug-in owns the Virus;
    // when that one leaves, the next one
    bh_device(1);
    int a = client(), b = client(), c = client();
    hello(a, VTI_PLUGIN_NAME);
    CHECK_EQ(next_state(a), 1);
    hello(b, VTI_PLUGIN_NAME);
    CHECK_EQ(next_state(b), 2);
    hello(c, VTI_PLUGIN_NAME);
    CHECK_EQ(next_state(c), 2);
    msg(a, VTIMSG_OBSERVE, {0});
    CHECK_EQ(next_state(b), 1);
    close(b);
    CHECK_EQ(next_state(c), 1);
    close(a);
    close(c);
    CHECK(until([] { return bh_clients() == 0; }));
    bh_device(0);
}

TEST(broker_second_hello_changes_nothing)
{
    // B11 (the POSIX side of B6): the name and the owner are decided with the first HELLO; a client that says it is
    // the plug-in later does not take the Virus from the plug-in that comes next
    bh_device(1);
    int cc = client(), p = client();
    hello(cc, "tirevive-cc");
    CHECK_EQ(next_state(cc), 1);
    hello(cc, VTI_PLUGIN_NAME);
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // handled (no reply to wait for)
    hello(p, VTI_PLUGIN_NAME);
    CHECK_EQ(next_state(p), 1); // the owner, not waiting
    close(cc);
    close(p);
    CHECK(until([] { return bh_clients() == 0; }));
    bh_device(0);
}

namespace {
// the reply to one control request (seq), -2 when none comes within `ms`
int control_reply(int fd, uint32_t seq, int ms)
{
    for (;;) {
        pollfd p{fd, POLLIN, 0};
        vtimsg h;
        uint8_t b[VTIMSG_MAX];
        if (poll(&p, 1, ms) != 1 || ::read(fd, &h, sizeof h) != (ssize_t)sizeof h)
            return -2;
        for (size_t got = 0; got < h.len;) {
            const ssize_t r = ::read(fd, b + got, h.len - got);
            if (r <= 0)
                return -2;
            got += (size_t)r;
        }
        uint32_t s;
        memcpy(&s, b, 4);
        if (h.type == VTIMSG_CONTROL_REPLY && h.len >= 8 && s == seq)
            return 0;
    }
}
} // namespace

TEST(broker_busy_write_does_not_hold_control_requests)
{
    // T5 (R12-5): a write waiting on a busy Virus held the global device lock, so another client's control request
    // (and audio, close, the dead-device check) waited for it. Now only the TX path waits; one message stays whole
    // on the wire, so a second client's write waits for the first message to end.
    bh_device(1);
    bh_busy(1);
    int a = client(), b = client();
    msg(a, VTIMSG_WRITE, {0x06, 0x29, 0x90, 0x3C, 0x64});
    CHECK(until([] { return bh_inside() == 1; }));
    // GET_DESCRIPTOR (device), a standard read
    msg(b, VTIMSG_CONTROL, {7, 0, 0, 0, 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00});
    CHECK_EQ(control_reply(b, 7, 1000), 0);
    msg(b, VTIMSG_WRITE, {0x06, 0x29, 0x80, 0x3C, 0x00});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_EQ(writes(), 1); // b's message waits: never inside a's
    bh_busy(0);
    CHECK(until([] { return writes() == 2 && bh_inside() == 0; }));
    close(a);
    close(b);
    CHECK(until([] { return bh_clients() == 0; }));
    bh_device(0);
}

TEST(broker_busy_write_stops_when_its_client_goes)
{
    // T5: the retry loop keeps a message whole while the client is there; a client that left does not hold the TX
    // path for the whole bound
    bh_device(1);
    bh_busy(1);
    int a = client();
    msg(a, VTIMSG_WRITE, {0x06, 0x29, 0x90, 0x3C, 0x64});
    CHECK(until([] { return bh_inside() == 1; }));
    close(a);
    CHECK(until([] { return bh_stopped() == 1; }));
    bh_busy(0);
    CHECK(until([] { return bh_clients() == 0; }));
    bh_device(0);
}
