// The POSIX broker's handling of client messages (src/broker/vtibroker_posix.c, through brokerharness.c): a socketpair
// is one client, the device is a stub. Linux only (own binary brokertests); nothing here opens USB.
#include "check.h"
#include <chrono>
#include <cstring>
#include <functional>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
extern "C" {
#include "vtiproto.h"
void bh_device(int open);
void bh_serve(int fd);
int bh_clients(void);
int bh_observers(void);
int bh_writes(uint8_t *last, int *len);
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
