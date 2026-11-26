// MemorySync against a fake Virus (the embedded constructor: requests go to a callback, replies come back via feed):
// request/reply matching, bank info, which programs a bank sync fetches, cancelling, and the cache file format
#include "check.h"
#include "device/memsync.h"
#include "model/bankdrop.h"
#include "model/remotedevice.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <set>
#include <thread>
#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#endif

using namespace vti;
using Bytes = std::vector<uint8_t>;

// a single dump as the Virus sends it (524 bytes, 30 bank program -> 10 bank program); the content depends on `seed`
static Dump fake_single(int bank, int program, int seed)
{
    Dump d(524, 0);
    const uint8_t head[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, (uint8_t)bank, (uint8_t)program};
    std::copy(head, head + 9, d.begin());
    for (size_t i = 9; i < 523; i++)
        d[i] = (uint8_t)((i * 7 + seed * 13) & 0x7F);
    d[523] = 0xF7;
    BankDrop::checksums(d); // as the Virus sends them (E552)
    return d;
}

// answers bank info (39 bank -> 14 bank, 128 fingerprints) and single dump requests from `ram`, synchronously
struct FakeVirus {
    std::vector<Bank> ram = std::vector<Bank>(5);
    MemorySync *sync = nullptr;
    std::vector<Bytes> requests;
    int corrupt = -1; // this program's dump comes back with a different fingerprint than bank info says
    int corrupt_times = -1; // how many times it does; -1: always
    int short_info = 0;     // this many bank info replies come back cut short (a packet lost on the way); -1: always
    std::set<int> silent;   // programs whose single dump request gets no reply
    std::function<bool(const Bytes &)> send()
    {
        return [this](const Bytes &m) {
            requests.push_back(m);
            if (m.size() < 9)
                return true;
            int bank = m[7];
            if (m[6] == 0x39) {
                if (short_info != 0) {
                    if (short_info > 0)
                        short_info--;
                    sync->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, (uint8_t)bank, 0x00, 0x01, 0xF7});
                    return true;
                }
                Bytes r = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, (uint8_t)bank, 0x00};
                for (int p = 0; p < 128; p++) {
                    int fp = RemoteDevice::single_fingerprint(ram[bank][p]);
                    r.push_back((uint8_t)(fp & 0x7F));
                    r.push_back((uint8_t)(fp >> 7 & 0x7F));
                }
                r.push_back(0x00); // checksum position (not checked by bank_info)
                r.push_back(0xF7);
                sync->feed(r);
            } else if (m[6] == 0x30) {
                int p = m[8];
                if (silent.count(p))
                    return true;
                const bool bad = p == corrupt && corrupt_times != 0;
                if (bad && corrupt_times > 0)
                    corrupt_times--;
                sync->feed(bad ? fake_single(bank, p, 999) : ram[bank][p]);
            }
            return true;
        };
    }
};

TEST(memsync_transact_matches_reply)
{
    MemorySync *ms = nullptr;
    MemorySync sync([&](const Bytes &) {
        ms->feed({0xF0, 0x7E, 0x00, 0x06, 0x02, 0xF7});                         // not an Access SysEx: dropped
        ms->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, 0x01, 0x00, 0xF7}); // another reply: not matched
        ms->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, 0x00, 0x42, 0xF7});
        return true;
    });
    ms = &sync;
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 500, 1);
    CHECK_EQ(r.size(), 11u);
    CHECK(r.size() == 11 && r[9] == 0x42);
}

TEST(memsync_keeps_sysex_only_while_a_request_waits)
{
    // audit #12: the plug-in sees every Access SysEx of a DAW session; between requests none may pile up
    MemorySync *ms = nullptr;
    MemorySync sync([&](const Bytes &) {
        ms->feed(fake_single(0, 0, 1));
        return true;
    });
    ms = &sync;
    for (int i = 0; i < 100; i++)
        sync.feed(fake_single(0, 1, i));
    CHECK_EQ(sync.queued(), 0u);
    CHECK_EQ(sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 500, 1)
                 .size(),
             524u);
    CHECK_EQ(sync.queued(), 0u); // the reply leaves with transact, nothing stays behind
}

TEST(memsync_single_asks_again_after_a_bad_checksum)
{
    // audit #32: a 524-byte reply was taken on its size alone, a damaged one went into the bank cache on disk
    MemorySync *ms = nullptr;
    int asked = 0;
    std::vector<std::string> logged;
    MemorySync sync([&](const Bytes &) {
        Dump d = fake_single(1, 5, 3);
        if (asked++ == 0)
            d[300] ^= 0x01; // one bit lost on the way: the checksums no longer match
        ms->feed(d);
        return true;
    });
    ms = &sync;
    sync.log = [&](const std::string &l) { logged.push_back(l); };
    Dump r = sync.single(1, 5);
    CHECK_EQ(asked, 2);
    CHECK(r == fake_single(1, 5, 3));
    CHECK(!logged.empty() && logged[0].find("checksum") != std::string::npos);
}

TEST(memsync_single_takes_the_same_reply_twice_as_the_virus_means_it)
{
    // E552 covers our TI2 only: a model that counts its checksums otherwise sends the same reply every time, a damaged
    // packet does not - so a repeated identical reply is taken (and logged), the sync still works
    MemorySync *ms = nullptr;
    int asked = 0;
    MemorySync sync([&](const Bytes &) {
        Dump d = fake_single(1, 5, 3);
        d[265] ^= 0x01;
        asked++;
        ms->feed(d);
        return true;
    });
    ms = &sync;
    std::string last;
    sync.log = [&](const std::string &l) { last = l; };
    Dump r = sync.single(1, 5);
    CHECK_EQ(asked, 2);
    CHECK_EQ(r.size(), 524u);
    CHECK(last.find("twice") != std::string::npos);
}

TEST(memsync_transact_times_out_without_reply)
{
    MemorySync sync([](const Bytes &) { return true; });
    auto t0 = std::chrono::steady_clock::now();
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 100, 1);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.empty());
    CHECK(ms >= 100); // one try waits its timeout, then the drain pause for a late reply (1.5 s)
    CHECK(ms < 2500);
}

TEST(memsync_transact_failed_send)
{
    int sends = 0;
    MemorySync sync([&](const Bytes &) {
        sends++;
        return false;
    });
    CHECK(sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10}, 1000, 3).empty());
    CHECK_EQ(sends, 1); // no retries when the message cannot even be queued
}

// missed(): the last request waited its whole time and nothing came - the plug-in's cue to re-send the reply cables
// and then warn (E591). Not a stop, a send that failed, a handover, or a reply of the wrong size.
TEST(memsync_missed_only_when_the_virus_did_not_answer)
{
    const Dump req = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7};
    MemorySync quiet([](const Bytes &) { return true; });
    CHECK(!quiet.missed());
    CHECK(quiet.transact(req, {0x10, 0x00, 0x00}, 100, 1).empty());
    CHECK(quiet.missed());

    MemorySync *ms = nullptr;
    MemorySync answers([&](const Bytes &) {
        ms->feed(fake_single(0, 0, 1));
        return true;
    });
    ms = &answers;
    answers.transact(req, {0x10, 0x00, 0x00}, 100, 1);
    CHECK(!answers.missed());

    MemorySync failed([](const Bytes &) { return false; });
    CHECK(failed.transact(req, {0x10, 0x00, 0x00}, 100, 1).empty());
    CHECK(!failed.missed()); // nothing reached the Virus

    MemorySync gone([](const Bytes &) { return true; });
    gone.present = [] { return false; };
    CHECK(gone.transact(req, {0x10, 0x00, 0x00}, 100, 1).empty());
    CHECK(!gone.missed()); // handed over (E591 (2))

    MemorySync stopped([](const Bytes &) { return true; });
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stopped.cancel();
    });
    CHECK(stopped.transact(req, {0x10, 0x00, 0x00}, 1000, 1).empty());
    stopper.join();
    CHECK(!stopped.missed());

    // the same instance: a miss, then an answer
    bool answer = false;
    MemorySync *ps = nullptr;
    MemorySync once([&](const Bytes &) {
        if (answer)
            ps->feed(fake_single(0, 0, 1));
        return true;
    });
    ps = &once;
    once.transact(req, {0x10, 0x00, 0x00}, 100, 1);
    CHECK(once.missed());
    answer = true;
    once.transact(req, {0x10, 0x00, 0x00}, 100, 1);
    CHECK(!once.missed());
}

TEST(memsync_wrong_size_replies_are_not_missed)
{
    MemorySync *ms = nullptr;
    MemorySync sync([&](const Bytes &m) {
        ms->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, m[7], m[8], 0x00, 0xF7}); // cut short
        return true;
    });
    ms = &sync;
    CHECK(sync.single(1, 7).empty());
    CHECK(!sync.missed()); // lost packets, a Virus that answers
}

TEST(memsync_transact_late_reply)
{
    // the readiness probe's case: the Virus answers only after it worked through what was queued before
    MemorySync *ms = nullptr;
    std::thread late;
    MemorySync sync([&](const Bytes &) {
        late = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            ms->feed(fake_single(0, 0, 1));
        });
        return true;
    });
    ms = &sync;
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 2000, 1);
    late.join();
    CHECK_EQ(r.size(), 524u);
}

// E591 (1): answers that came after the timeout were thrown away - one in the 1.5 s pause after a try was kept but never
// looked at, the next try cleared it. Each reply below comes `after_ms` after the first request, the fake Virus answers
// only that first one.
static Dump late_answer(int after_ms, int timeout_ms, int tries, int &sends, const Dump &reply = fake_single(0, 0, 1))
{
    MemorySync *self = nullptr;
    std::thread late;
    sends = 0;
    MemorySync sync([&](const Bytes &) {
        if (sends++ == 0)
            late = std::thread([&self, after_ms, reply] {
                std::this_thread::sleep_for(std::chrono::milliseconds(after_ms));
                self->feed(reply);
            });
        return true;
    });
    self = &sync;
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, timeout_ms,
                           tries);
    late.join();
    return r;
}

TEST(memsync_transact_takes_a_reply_in_the_pause_after_a_try)
{
    int sends;
    CHECK_EQ(late_answer(700, 100, 1, sends).size(), 524u); // in the pause after the timeout
    CHECK_EQ(late_answer(700, 100, 2, sends).size(), 524u); // ... and not cleared by a second try
    CHECK_EQ(sends, 1); // the reply came before the second try was due: no second request for the Virus
}

TEST(memsync_transact_takes_the_first_tries_reply_after_the_second_try)
{
    int sends;
    CHECK_EQ(late_answer(1800, 100, 2, sends).size(), 524u); // try 2 went out at 1.6 s: the same request, it counts
    CHECK_EQ(sends, 2);
}

TEST(memsync_transact_ignores_a_late_reply_with_another_header)
{
    int sends;
    CHECK(late_answer(700, 100, 1, sends, fake_single(0, 1, 1)).empty()); // 10 00 01 is not the answer to 30 00 00
}

// E591 (1): the clock started when the request was queued; behind a state upload it may wait seconds before it is
// even written. `sending` says the plug-in's queue still holds messages: the timeout starts when it is empty.
TEST(memsync_transact_clock_starts_when_the_request_is_out)
{
    MemorySync *self = nullptr;
    std::atomic<bool> queued{false};
    std::thread out, late;
    MemorySync sync([&](const Bytes &) {
        queued = true;
        out = std::thread([&] { // the queue in front of the request takes 2 s to go out
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            queued = false;
        });
        late = std::thread([&] { // the Virus answers 0.5 s after it got the request
            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
            self->feed(fake_single(0, 0, 1));
        });
        return true;
    });
    self = &sync;
    sync.sending = [&] { return queued.load(); };
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 300, 1);
    out.join();
    late.join();
    CHECK_EQ(r.size(), 524u); // 300 ms + 1.5 s from the queueing would have ended at 1.8 s
}

// E591, like the original (it asks only when its start-up queue is empty): a request does not go out while other
// messages still wait in the plug-in's queue (bounded), so it is not stuck behind them
TEST(memsync_transact_asks_only_when_the_queue_is_empty)
{
    std::atomic<bool> busy{true};
    std::thread drain([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        busy = false;
    });
    MemorySync *self = nullptr;
    bool busy_at_send = true;
    MemorySync sync([&](const Bytes &) {
        busy_at_send = busy.load();
        self->feed(fake_single(0, 0, 1));
        return true;
    });
    self = &sync;
    sync.sending = [&] { return busy.load(); };
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 300, 1);
    drain.join();
    CHECK(!busy_at_send);
    CHECK_EQ(r.size(), 524u);
}

// research T9 (R1-3, R4-2, R9-3): the plug-in holds its MIDI clock and automation while a request is open, like the
// original that drops F8 while busy. request_open() is true from the first send until transact returns (reply,
// timeout, cancel), also over the tries; not while the request still waits for the queue (it is not out yet)
TEST(memsync_request_open_from_the_send_to_the_answer)
{
    std::atomic<bool> busy{true};
    MemorySync *self = nullptr;
    bool open_while_queued = true, open_at_send = false, open_while_waiting = false;
    std::thread answer;
    MemorySync sync([&](const Bytes &) {
        open_at_send = self->request_open();
        answer = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            open_while_waiting = self->request_open();
            self->feed(fake_single(0, 0, 1));
        });
        return true;
    });
    self = &sync;
    std::thread drain([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        open_while_queued = sync.request_open();
        busy = false;
    });
    sync.sending = [&] { return busy.load(); };
    CHECK(!sync.request_open());
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 1000, 1);
    drain.join();
    answer.join();
    CHECK_EQ(r.size(), 524u);
    CHECK(!open_while_queued); // waiting for the queue: the clock goes on, so the queue can empty
    CHECK(open_at_send);
    CHECK(open_while_waiting);
    CHECK(!sync.request_open()); // answered: clock and automation go on
}

TEST(memsync_request_open_ends_with_a_timeout_or_a_cancel)
{
    MemorySync *self = nullptr;
    bool open_at_second_try = false;
    int sends = 0;
    MemorySync sync([&](const Bytes &) {
        if (++sends == 2)
            open_at_second_try = self->request_open();
        return true;
    });
    self = &sync;
    sync.pause_ms = 0;
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 50, 2);
    CHECK(r.empty() && sends == 2);
    CHECK(open_at_second_try); // open over the tries
    CHECK(!sync.request_open()); // timed out: not open for good
    bool open_before_cancel = false;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        open_before_cancel = sync.request_open();
        sync.cancel();
    });
    r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 10000, 1);
    stopper.join();
    CHECK(r.empty() && open_before_cancel);
    CHECK(!sync.request_open());
}

// a queue that never empties (the Virus refused EP 0x06 for 8.7 s, E591 (3); a broker gone): the wait is bounded
TEST(memsync_transact_queue_wait_is_bounded)
{
    int sends = 0;
    MemorySync sync([&](const Bytes &) {
        sends++;
        return true;
    });
    sync.sending = [] { return true; };
    sync.queue_wait_ms = 200;
    auto t0 = std::chrono::steady_clock::now();
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 100, 1);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.empty());
    CHECK_EQ(sends, 1);
    CHECK(ms >= 200 + 100);
    CHECK(ms < 2500); // 200 ms for the queue (before and after the send together), 100 ms + the 1.5 s pause
}

TEST(memsync_cancel_ends_the_wait_for_the_queue_at_once)
{
    MemorySync sync([](const Bytes &) { return true; });
    sync.sending = [] { return true; };
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        sync.cancel();
    });
    auto t0 = std::chrono::steady_clock::now();
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 10000, 3);
    stopper.join();
    CHECK(r.empty());
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1000));
}

// E591 (2): an instance that handed its Virus over (#51) waited out its probe and logged "no reply". `present` false:
// a request does not start, one waiting ends at once, nothing is logged as a miss - and unlike cancel() not for good
TEST(memsync_no_virus_ends_a_request_at_once)
{
    std::atomic<bool> have{true};
    int sends = 0;
    bool answer = false;
    std::vector<std::string> lines;
    MemorySync *self = nullptr;
    MemorySync sync([&](const Bytes &) {
        sends++;
        if (answer)
            self->feed(fake_single(0, 0, 1));
        return true;
    });
    self = &sync;
    sync.present = [&] { return have.load(); };
    sync.log = [&](const std::string &s) { lines.push_back(s); };
    std::thread handover([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        have = false;
    });
    auto t0 = std::chrono::steady_clock::now();
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 10000, 3);
    handover.join();
    CHECK(r.empty());
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1000));
    CHECK_EQ(sends, 1);
    CHECK(sync.bank_info(1).empty());
    CHECK_EQ(sends, 1); // no Virus: not even sent
    Bank cache;
    CHECK_EQ(sync.sync_bank(1, cache), -2); // a stop, not "bank sync failed"
    CHECK(lines.empty());                   // and no "no reply" line
    have = true; // the Virus is back for this instance: requests work again
    answer = true;
    CHECK_EQ(sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 500, 1)
                 .size(),
             524u);
}

// research R2-5: the late answer to an earlier probe came while the next probe still waited for the queue and ended
// it before its own request was even written (the Virus not ready yet). A transact takes only answers that arrive
// after its first request went out.
TEST(memsync_transact_ignores_an_answer_from_before_its_request)
{
    std::atomic<bool> busy{true};
    MemorySync *self = nullptr;
    int sends = 0;
    MemorySync sync([&](const Bytes &) {
        sends++;
        self->feed(fake_single(0, 0, 2)); // the answer to this request
        return true;
    });
    self = &sync;
    sync.sending = [&] { return busy.load(); };
    std::thread before([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        self->feed(fake_single(0, 0, 1)); // the earlier probe's answer, while this one waits for the queue
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        busy = false;
    });
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 300, 1);
    before.join();
    CHECK_EQ(sends, 1);
    CHECK(r == fake_single(0, 0, 2));
}

// research R6-2: a Virus lost during the bank sync (the plug-in's `present` is false then) stops it like a cancel: no
// "given up" slots, no miss, nothing more sent to the gone Virus, the patches fetched so far kept. Run again when it is
// back (the plug-in does at the reconnect), only the slots still stale are fetched.
TEST(memsync_sync_bank_stops_cleanly_when_the_virus_goes_and_runs_again)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    for (int p = 0; p < 128; p++)
        v.ram[1][p] = fake_single(1, p, p);
    std::atomic<bool> here{true};
    sync.present = [&] { return here.load(); };
    size_t sent_at_loss = 0;
    Bank cache;
    int n = sync.sync_bank(1, cache, [&](int p, bool) {
        if (p == 9) { // switched off after the 10th patch
            here = false;
            sent_at_loss = v.requests.size();
        }
    });
    CHECK_EQ(n, -2);
    CHECK_EQ(v.requests.size(), sent_at_loss); // not even the display message for the next slot
    CHECK_EQ(sync.given_up, 0);
    CHECK(sync.unconfirmed[1].empty());
    CHECK(!sync.missed());
    CHECK(cache[9] == v.ram[1][9]);
    CHECK(cache[10].empty());
    here = true;
    CHECK_EQ(sync.sync_bank(1, cache), 118);
    bool same = true;
    for (int p = 0; p < 128; p++)
        same = same && cache[p] == v.ram[1][p];
    CHECK(same);
}

TEST(memsync_bank_info_and_single)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    for (int p = 0; p < 128; p++)
        v.ram[2][p] = fake_single(2, p, p);
    auto info = sync.bank_info(2);
    CHECK_EQ(info.size(), 128u);
    bool same = info.size() == 128;
    for (int p = 0; same && p < 128; p++)
        same = info[p] == RemoteDevice::single_fingerprint(v.ram[2][p]);
    CHECK(same);
    CHECK(sync.single(2, 5) == v.ram[2][5]);
    CHECK(v.requests.size() == 2 && v.requests[0][6] == 0x39 && v.requests[1][6] == 0x30 && v.requests[1][8] == 5);
}

TEST(memsync_sync_bank_fetches_only_stale)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++) {
        v.ram[1][p] = fake_single(1, p, p);
        cache[p] = v.ram[1][p];
    }
    cache[3] = fake_single(1, 3, 500); // changed on the Virus
    cache[100].clear();                // not cached yet
    int progress_calls = 0, stale_calls = 0;
    int n = sync.sync_bank(1, cache, [&](int, bool stale) {
        progress_calls++;
        stale_calls += stale;
    });
    CHECK_EQ(n, 2);
    CHECK_EQ(progress_calls, 128);
    CHECK_EQ(stale_calls, 2);
    CHECK(cache[3] == v.ram[1][3] && cache[100] == v.ram[1][100]);
    CHECK_EQ(v.requests.size(), 6u); // display message (E321) + bank info + 2 x (syncing message + single dump)
    // up to date now: only the display message and the bank info
    v.requests.clear();
    CHECK_EQ(sync.sync_bank(1, cache), 0);
    CHECK_EQ(v.requests.size(), 2u);
}

TEST(memsync_sync_bank_cancel_and_errors)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    for (int p = 0; p < 128; p++)
        v.ram[1][p] = fake_single(1, p, p);
    Bank cache; // empty: every program is stale
    std::atomic<bool> stop{true};
    CHECK_EQ(sync.sync_bank(1, cache, nullptr, &stop), -2);
    sync.before_request = [] { return false; }; // e.g. the plug-in is closing
    CHECK_EQ(sync.sync_bank(1, cache), -2);
    int calls = 0;
    sync.before_request = [&] { return ++calls < 3; }; // bank info + 1 program, then cancelled
    CHECK_EQ(sync.sync_bank(1, cache), -2);
    CHECK(cache[0] == v.ram[1][0] && cache[1].empty());
    sync.before_request = nullptr;
    v.corrupt = 2; // a reply that does not match the bank info: never cached, the slot is given up, the sync goes on
    CHECK_EQ(sync.sync_bank(1, cache), 126); // 0 was current, 2 given up
    CHECK(cache[2].empty() && cache[3] == v.ram[1][3]);
}

// One lost packet must not end a sync: seen on the simulator's bridge (S048), not reported on a real unit. single()
// already retried a dump of the wrong size; bank info did not, and a dump with the wrong fingerprint ended the sync.
TEST(memsync_bank_info_retries_a_wrong_size_reply)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    v.short_info = 1;
    CHECK_EQ(sync.bank_info(2).size(), 128u);
    CHECK_EQ(v.requests.size(), 2u);
}

TEST(memsync_bank_info_gives_up_on_wrong_sizes)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    v.short_info = -1;
    CHECK(sync.bank_info(2).empty());
    CHECK_EQ(v.requests.size(), 3u); // bounded, like single()
}

// #34: a "bank sync failed" log could not tell cut-short replies (lost packets) from no reply at all (2026-11-14)
TEST(memsync_logs_wrong_size_replies)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    std::vector<std::string> lines;
    sync.log = [&](const std::string &s) { lines.push_back(s); };
    v.short_info = 1;
    CHECK_EQ(sync.bank_info(2).size(), 128u);
    CHECK_EQ(lines.size(), 1u);
    if (!lines.empty())
        CHECK(lines[0] == std::string("bank info RAM B: reply 11 bytes, expected 267 (try 1 of 3)"));

    lines.clear();
    v.ram[2][5] = fake_single(2, 5, 5);
    CHECK_EQ(sync.single(2, 5).size(), 524u);
    CHECK(lines.empty()); // a good reply logs nothing
}

TEST(memsync_logs_no_reply_but_not_a_cancel)
{
    std::vector<std::string> lines;
    MemorySync sync([](const Bytes &) { return false; }); // empty at once, like a reply that never comes
    sync.log = [&](const std::string &s) { lines.push_back(s); };
    CHECK(sync.single(3, 7).empty());
    CHECK_EQ(lines.size(), 1u);
    if (!lines.empty())
        CHECK(lines[0] == std::string("single RAM C 7: no reply"));
    lines.clear();
    sync.cancel();
    CHECK(sync.bank_info(1).empty());
    CHECK(lines.empty()); // the plug-in closing is not a Virus that did not answer
}

TEST(memsync_sync_bank_survives_one_wrong_size_bank_info)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = v.ram[1][p] = fake_single(1, p, p);
    v.short_info = 1;
    CHECK_EQ(sync.sync_bank(1, cache), 0);
}

TEST(memsync_sync_bank_retries_a_wrong_fingerprint)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    for (int p = 0; p < 128; p++)
        v.ram[1][p] = fake_single(1, p, p);
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = v.ram[1][p];
    cache[2] = fake_single(1, 2, 500); // only this one is stale
    v.corrupt = 2;
    v.corrupt_times = 1;
    CHECK_EQ(sync.sync_bank(1, cache), 1);
    CHECK(cache[2] == v.ram[1][2]);
    CHECK_EQ(v.requests.size(), 6u); // display message + bank info + the dump twice, each after its syncing message
}

TEST(memsync_sync_bank_gives_up_on_a_wrong_fingerprint)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    for (int p = 0; p < 128; p++)
        v.ram[1][p] = fake_single(1, p, p);
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = v.ram[1][p];
    const Dump old = fake_single(1, 2, 500);
    cache[2] = old;
    v.corrupt = 2; // always: never cached
    std::vector<std::string> lines;
    sync.log = [&](const std::string &l) { lines.push_back(l); };
    // like the original (0x18008e220): the slot is given up, not the sync. The stale dump goes too: it is known wrong
    CHECK_EQ(sync.sync_bank(1, cache), 0);
    CHECK(cache[2].empty());
    CHECK(sync.unconfirmed[1] == std::set<int>{2});
    CHECK_EQ(sync.given_up, 1);
    CHECK_EQ(v.requests.size(), 8u); // display message + bank info + 3 tries (each with its syncing message), bounded
    CHECK(!lines.empty() && lines.back() == std::string("single RAM A 2: given up, not cached (1 of 3)"));
}

// T1, like the original (Virus Control 5.1.7 x64 0x18008e220, E594): a single dump request is asked 3 times, each
// waiting its whole time, before the slot is given up
TEST(memsync_single_asks_three_times_without_an_answer)
{
    int asked = 0;
    MemorySync sync([&](const Bytes &) {
        asked++;
        return true;
    });
    sync.single_ms = 1, sync.pause_ms = 5;
    CHECK(sync.single(1, 7).empty());
    CHECK_EQ(asked, 3);
    CHECK(sync.missed());
}

// T2: the read for a RAM backup asks once (the original's 10 s single wait), so a Virus without an answer ends it
// after one wait, not three
TEST(memsync_single_can_ask_once)
{
    int asked = 0;
    MemorySync sync([&](const Bytes &) {
        asked++;
        return true;
    });
    sync.single_ms = 1, sync.pause_ms = 5;
    CHECK(sync.single(1, 7, 1).empty());
    CHECK_EQ(asked, 1);
    CHECK(sync.missed());
}

// a bank info is asked 6 times; before the 5th and the 6th ask the reply cables are set again (event 0x22, E591)
TEST(memsync_bank_info_asks_six_times_and_resends_the_cables_before_the_last_two)
{
    std::vector<size_t> resent_at; // how many requests had gone out at each re-send
    int asked = 0;
    MemorySync sync([&](const Bytes &) {
        asked++;
        return true;
    });
    sync.bank_info_ms = 1, sync.pause_ms = 5;
    sync.resend_routing = [&] { resent_at.push_back((size_t)asked); };
    CHECK(sync.bank_info(2).empty());
    CHECK_EQ(asked, 6);
    CHECK((resent_at == std::vector<size_t>{4, 5}));
    CHECK(sync.missed());

    // an answer, or a reply of the wrong size (the Virus answers), re-sends nothing
    FakeVirus v;
    MemorySync answers(v.send());
    v.sync = &answers;
    int resends = 0;
    answers.resend_routing = [&] { resends++; };
    CHECK_EQ(answers.bank_info(1).size(), 128u);
    v.short_info = -1;
    CHECK(answers.bank_info(1).empty());
    CHECK_EQ(resends, 0);

    // the cables helped: the 5th ask is answered
    int n = 0;
    MemorySync *ps = nullptr;
    bool cables = false;
    MemorySync late([&](const Bytes &m) {
        n++;
        if (cables) {
            Bytes r(267, 0); // 14 bank 00, 128 fingerprints, checksum, F7
            const uint8_t head[8] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, m[7]};
            std::copy(head, head + 8, r.begin());
            r[266] = 0xF7;
            ps->feed(r);
        }
        return true;
    });
    ps = &late;
    late.bank_info_ms = 1, late.pause_ms = 5;
    late.resend_routing = [&] { cables = true; };
    CHECK_EQ(late.bank_info(3).size(), 128u);
    CHECK_EQ(n, 5);
}

// one slot without an answer no longer ends the whole sync (#51): it is given up (empty, so the next start asks again)
// and the sync goes on with the next slot
TEST(memsync_sync_bank_gives_up_a_silent_slot_and_goes_on)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    sync.single_ms = 1, sync.pause_ms = 5;
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = v.ram[1][p] = fake_single(1, p, p);
    cache[5] = fake_single(1, 5, 500); // stale, and the Virus does not answer for it
    cache[9] = fake_single(1, 9, 500); // stale, answered
    v.silent = {5};
    std::vector<std::string> lines;
    sync.log = [&](const std::string &l) { lines.push_back(l); };
    std::vector<int> fetched;
    CHECK_EQ(sync.sync_bank(1, cache, [&](int p, bool f) {
        if (f)
            fetched.push_back(p);
    }),
             1);
    CHECK(cache[5].empty());
    CHECK(cache[9] == v.ram[1][9]);
    CHECK((fetched == std::vector<int>{9}));
    CHECK(sync.unconfirmed[1] == std::set<int>{5});
    CHECK_EQ(sync.given_up, 1);
    CHECK(std::count(lines.begin(), lines.end(), std::string("single RAM A 5: given up, not cached (1 of 3)")) == 1);
    // a later sync of the bank that gets the slot confirms it
    v.silent.clear();
    CHECK_EQ(sync.sync_bank(1, cache), 1);
    CHECK(sync.unconfirmed[1].empty());
}

// the 3rd given-up slot of a sync (across banks) ends it, like the original's abort 0x18008e0d0; the stale slots it
// did not reach in that bank are known wrong: they are emptied and unconfirmed too, never asked
TEST(memsync_sync_stops_at_the_third_given_up_slot)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    sync.single_ms = 1, sync.pause_ms = 5;
    Bank a, b;
    for (int p = 0; p < 128; p++) {
        v.ram[1][p] = fake_single(1, p, p);
        v.ram[2][p] = fake_single(2, p, p);
    }
    v.silent = {3, 7};
    CHECK_EQ(sync.sync_bank(1, a), 126); // RAM A: 3 and 7 given up
    CHECK_EQ(sync.given_up, 2);
    for (int p = 0; p < 128; p++)
        b[p] = v.ram[2][p];
    b[3] = fake_single(2, 3, 500);
    b[50] = fake_single(2, 50, 500);
    v.requests.clear();
    CHECK_EQ(sync.sync_bank(2, b), -1); // RAM B: 3 is the 3rd
    CHECK_EQ(sync.given_up, 3);
    CHECK(b[3].empty() && b[50].empty());
    CHECK((sync.unconfirmed[2] == std::set<int>{3, 50}));
    for (auto &r : v.requests)
        CHECK(!(r[6] == 0x30 && r[8] == 50)); // stopped: slot 50 was never asked
}

// user ROM programs are part of the same sync (the original's scheduler reads them after the RAM banks)
TEST(memsync_read_missing_gives_up_a_silent_slot_and_goes_on)
{
    FakeVirus v;
    v.ram.resize(31);
    MemorySync sync(v.send());
    v.sync = &sync;
    sync.single_ms = 1, sync.pause_ms = 5;
    Bank cache;
    for (int p = 0; p < 128; p++)
        v.ram[30][p] = fake_single(30, p, p);
    v.silent = {4};
    CHECK_EQ(sync.read_missing(30, cache), 127);
    CHECK(cache[4].empty() && cache[5] == v.ram[30][5]);
    CHECK(sync.unconfirmed[30] == std::set<int>{4});
    v.silent = {10, 11};
    Bank other;
    CHECK_EQ(sync.read_missing(30, other), -1); // 10 and 11: the 2nd and the 3rd of this sync
    CHECK_EQ(sync.given_up, 3);
    CHECK(other[4] == v.ram[30][4] && other[11].empty() && other[12].empty());
}

TEST(memsync_bank_file_round_trip)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "vtitests-memsync";
    fs::create_directories(dir);
    const std::string path = (dir / "bank.bin").string();
    Bank out;
    for (int p = 0; p < 128; p += 3)
        out[p] = fake_single(1, p, p);
    CHECK(save_bank_file(path, out));
    CHECK(!fs::exists(path + ".tmp"));
    CHECK(save_bank_file(path, out)); // replaces an existing file
    Bank in;
    CHECK(load_bank_file(path, in));
    CHECK(in == out);
    CHECK(fs::file_size(path) == 8 + 128 * 4 + 43 * 524u);

    Bank none;
    CHECK(!load_bank_file((dir / "missing.bin").string(), none));
    auto write = [&](const std::string &name, const Bytes &b) {
        FILE *f = fopen((dir / name).string().c_str(), "wb");
        fwrite(b.data(), 1, b.size(), f);
        fclose(f);
        return (dir / name).string();
    };
    CHECK(!load_bank_file(write("header.bin", {'X', 'I', 'D', 'I', 0, 1, 8, 0}), none));
    CHECK(!load_bank_file(write("huge.bin", {'M', 'I', 'D', 'I', 0, 1, 8, 0, 0x00, 0x01, 0x00, 0x00}), none));
    CHECK(!load_bank_file(write("short.bin", {'M', 'I', 'D', 'I', 0, 1, 8, 0, 0, 0, 2, 0x0C, 0xF0}), none));
    CHECK(!load_bank_file(write("cut.bin", {'M', 'I', 'D', 'I', 0, 1, 8, 0, 0, 0, 0, 1, 0xF7}), none)); // 1 of 128
    fs::remove_all(dir);
}

// Two threads with requests at once (review 2026-10-19, hardware test of the broker reconnect): the bank sync was
// still running when the readiness probe went out, and the probe waited its full 10 s ("Virus ready after 11.5 s
// (no reply to the probe)"). The Virus does not answer requests that overlap: of 16 single dump requests sent back to
// back only the first (truncated) and the last were answered (E145). The fake Virus here answers only the latest
// open request. Requests go one at a time now, so both get their reply.
TEST(memsync_concurrent_transacts_both_get_their_reply)
{
    MemorySync *self = nullptr;
    std::mutex mu;
    std::vector<std::thread> answers;
    int latest = 0; // requests seen; a reply goes out only if no newer request came meanwhile
    MemorySync sync([&](const Bytes &req) {
        std::lock_guard<std::mutex> g(mu);
        const int n = ++latest;
        answers.emplace_back([&self, &mu, &latest, n, req] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            {
                std::lock_guard<std::mutex> g2(mu);
                if (latest != n) // overlapped by a newer request: this one is lost (E145)
                    return;
            }
            self->feed(fake_single(req[7], req[8], 1));
        });
        return true;
    });
    self = &sync;
    Dump a, b;
    std::thread probe([&] { a = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 1000, 1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30)); // the probe's request is out, its reply not yet
    std::thread bank([&] { b = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x01, 0x05, 0xF7}, {0x10, 0x01, 0x05}, 1000, 1); });
    probe.join();
    bank.join();
    std::vector<std::thread> done;
    {
        std::lock_guard<std::mutex> g(mu);
        done.swap(answers); // joined outside: they take mu themselves
    }
    for (auto &t : done)
        t.join();
    CHECK(a.size() == 524 && a[7] == 0 && a[8] == 0);
    CHECK(b.size() == 524 && b[7] == 1 && b[8] == 5);
}

// The plug-in's terminate() waited for a request in flight (review 2026-10-19 M6): the readiness probe up to 10 s, a
// bank sync request 3 x 3 s plus 1.5 s pauses. cancel() ends the waiting at once, also for later requests.
TEST(memsync_cancel_ends_a_waiting_request_at_once)
{
    MemorySync sync([](const Bytes &) { return true; }); // the Virus never answers
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        sync.cancel();
    });
    auto t0 = std::chrono::steady_clock::now();
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 10000, 3);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    stopper.join();
    CHECK(r.empty());
    CHECK(ms < 1000);
    t0 = std::chrono::steady_clock::now();
    CHECK(sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x39, 0x01, 0xF7}, {0x14, 0x01}, 10000, 3).empty());
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(100));
}

// "Download entire bank from Virus TI" like the original (E301): all 128 programs in order, each request preceded by
// the display message 74 01 7E 04 (Actions/Rise Message 4), no bank info, every program read again even if cached
TEST(memsync_sync_bank_keeps_the_configuring_message_up)
{
    // E321: the original sends 74 01 7E 07 (Rise Message 7: "Configuring Virus TI") right before every bank info
    // request, whether anything is fetched or not - the Virus shows it while the banks are checked
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = v.ram[1][p] = fake_single(1, p, p);
    CHECK_EQ(sync.sync_bank(1, cache), 0);
    const Bytes rise = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x07, 0xF7};
    CHECK_EQ(v.requests.size(), 2u);
    CHECK(v.requests[0] == rise);
    CHECK(v.requests.size() > 1 && v.requests[1][6] == 0x39);
}

// the original's start-up sync on the wire (captures/orig-rom-a-fetch-20261101b): 74 01 7E 07 only right before the
// bank info; every program it fetches gets 74 01 7E 04 (the Virus shows its syncing message) right before its
// 30 bank program, also each retry. Nothing else in between: no repeated "Configuring" while patches come in.
TEST(memsync_sync_bank_shows_syncing_for_each_fetch)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = v.ram[1][p] = fake_single(1, p, p);
    cache[3] = fake_single(1, 3, 500);
    cache[100].clear();
    v.corrupt = 100; // one wrong dump first: asked again
    v.corrupt_times = 1;
    CHECK_EQ(sync.sync_bank(1, cache), 2);
    const Bytes configuring = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x07, 0xF7};
    const Bytes syncing = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x04, 0xF7};
    CHECK_EQ(v.requests.size(), 8u); // 7E 07, bank info, 7E 04 + #3, 7E 04 + #100, 7E 04 + #100 again
    CHECK(v.requests.size() == 8 && v.requests[0] == configuring && v.requests[1][6] == 0x39);
    for (size_t i = 2; i + 1 < v.requests.size(); i += 2) {
        CHECK(v.requests[i] == syncing);
        CHECK(v.requests[i + 1][6] == 0x30);
    }
    CHECK(v.requests.size() == 8 && v.requests[3][8] == 3 && v.requests[5][8] == 100 && v.requests[7][8] == 100);
}

TEST(memsync_read_bank_reads_all)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++) {
        v.ram[2][p] = fake_single(2, p, p + 7);
        cache[p] = fake_single(2, p, p + 7); // already current: read anyway
    }
    cache[5] = fake_single(2, 5, 999);
    std::vector<int> order;
    int n = sync.read_bank(2, cache, [&](int p) { order.push_back(p); });
    CHECK_EQ(n, 128);
    CHECK_EQ(order.size(), 128u);
    CHECK(order.front() == 0 && order.back() == 127);
    CHECK(cache[5] == v.ram[2][5]);
    CHECK_EQ(v.requests.size(), 256u);
    const Bytes rise = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x04, 0xF7};
    CHECK(v.requests[0] == rise);
    CHECK((v.requests[1] == Bytes{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x02, 0x00, 0xF7}));
    CHECK(v.requests[254] == rise && v.requests[255][8] == 127);
}

// cancelled or a request without reply: stops, what was read stays, the rest of `cache` is untouched
TEST(memsync_read_bank_stops)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++) {
        v.ram[1][p] = fake_single(1, p, p);
        cache[p] = fake_single(1, p, 500);
    }
    int calls = 0;
    sync.before_request = [&] { return ++calls <= 3; }; // 3 programs, then the plug-in closes
    CHECK_EQ(sync.read_bank(1, cache), -2);
    CHECK(cache[2] == v.ram[1][2] && cache[3] == fake_single(1, 3, 500));
    sync.before_request = nullptr;
    std::atomic<bool> stop{true};
    CHECK_EQ(sync.read_bank(1, cache, nullptr, &stop), -2);
}

// E314: Live closed the plug-in 3 s into its bank sync; terminate() cancels the request in flight, which came back
// empty like a request without reply, and the log said "bank sync failed". A cancelled request is a stop (-2).
TEST(memsync_cancel_during_a_request_is_a_stop)
{
    for (int mode = 0; mode < 2; mode++) { // 0: sync_bank, 1: read_bank
        MemorySync sync([](const Bytes &) { return true; }); // the Virus never answers
        std::thread stopper([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sync.cancel();
        });
        Bank cache;
        const int r = mode == 0 ? sync.sync_bank(1, cache) : sync.read_bank(1, cache);
        stopper.join();
        CHECK_EQ(r, -2);
    }
    // a request that just gets no reply is still a failure
    MemorySync quiet([](const Bytes &) { return false; }); // the send fails: nothing reaches the Virus
    Bank cache;
    CHECK_EQ(quiet.sync_bank(1, cache), -1);
    CHECK_EQ(quiet.read_bank(1, cache), -1);
}

// a user ROM bank (E381/E382): the original reads the programs it has no data for, each after the display message
// 74 01 7E 04, with 30 <5+i> pp - one at a time, no bank info for ROM banks. Programs already cached are not read.
TEST(memsync_read_missing_reads_only_empty_slots)
{
    FakeVirus v;
    v.ram.resize(31);
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        v.ram[30][p] = fake_single(30, p, p + 3); // ROM Z = bank 30
    cache[0] = v.ram[30][0];
    cache[64] = v.ram[30][64];
    std::vector<int> order;
    int n = sync.read_missing(30, cache, [&](int p) { order.push_back(p); });
    CHECK_EQ(n, 126);
    CHECK_EQ(order.size(), 126u);
    CHECK(order.front() == 1 && order.back() == 127);
    for (int p = 0; p < 128; p++)
        CHECK(cache[p] == v.ram[30][p]);
    CHECK_EQ(v.requests.size(), 252u); // 126 x (display message + request), no bank info
    const Bytes rise = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x04, 0xF7};
    CHECK(v.requests[0] == rise);
    CHECK((v.requests[1] == Bytes{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x1E, 0x01, 0xF7}));
    for (auto &r : v.requests)
        CHECK(r[6] != 0x39);
}

TEST(memsync_read_missing_full_cache_sends_nothing)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = fake_single(5, p, p);
    CHECK_EQ(sync.read_missing(5, cache), 0);
    CHECK(v.requests.empty());
}

// stopped (closed, notes) or no reply: what was read stays, the next start continues with the rest
TEST(memsync_read_missing_stops)
{
    FakeVirus v;
    v.ram.resize(6);
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        v.ram[5][p] = fake_single(5, p, p);
    int calls = 0;
    sync.before_request = [&] { return ++calls <= 3; };
    CHECK_EQ(sync.read_missing(5, cache), -2);
    CHECK(cache[2] == v.ram[5][2] && cache[3].empty());
    sync.before_request = nullptr;
    std::atomic<bool> stop{true};
    CHECK_EQ(sync.read_missing(5, cache, nullptr, &stop), -2);
}

// Only an Access SysEx (F0 00 20 33) longer than 8 bytes is a reply: other manufacturers' SysEx and cut-short Access
// messages whose bytes 6.. happen to look like the reply must not be taken for it (the plug-in feeds every SysEx from
// the Virus in, also MIDI-port traffic)
TEST(memsync_transact_skips_lookalike_messages)
{
    MemorySync *ms = nullptr;
    const Bytes good = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, 0x01, 0x00, 0x55, 0xF7};
    MemorySync sync([&](const Bytes &) {
        ms->feed({0xF1, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, 0x01, 0x00, 0xF7}); // not a SysEx start
        ms->feed({0xF0, 0x01, 0x20, 0x33, 0x01, 0x10, 0x14, 0x01, 0x00, 0xF7}); // another manufacturer (each byte)
        ms->feed({0xF0, 0x00, 0x21, 0x33, 0x01, 0x10, 0x14, 0x01, 0x00, 0xF7});
        ms->feed({0xF0, 0x00, 0x20, 0x34, 0x01, 0x10, 0x14, 0x01, 0x00, 0xF7});
        ms->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, 0x01}); // 8 bytes: cut short
        ms->feed(good);
        return true;
    });
    ms = &sync;
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x39, 0x01, 0xF7}, {0x14, 0x01}, 500, 1);
    CHECK(r == good);
}

// the match is a prefix of bytes 6..: a reply that ends right after the matched bytes still counts
TEST(memsync_transact_reply_just_long_enough)
{
    MemorySync *ms = nullptr;
    const Bytes nine = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, 0x05};
    MemorySync sync([&](const Bytes &) {
        ms->feed(nine);
        return true;
    });
    ms = &sync;
    CHECK(sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x05, 0xF7}, {0x10, 0x00, 0x05}, 300, 1) ==
          nine);
}

// a single dump of the wrong size is asked for again, but bounded: 3 requests, then single() gives up (each extra
// request costs ~1 s on the Virus and delays its notes)
TEST(memsync_single_gives_up_after_three_wrong_sizes)
{
    MemorySync *ms = nullptr;
    int requests = 0;
    MemorySync sync([&](const Bytes &m) {
        requests++;
        ms->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, m[7], m[8], 0x00, 0xF7}); // cut short
        return true;
    });
    ms = &sync;
    CHECK(sync.single(1, 7).empty());
    CHECK_EQ(requests, 3);
}

// a ROM bank has 128 programs (0..127): read_missing must never ask for a program number 128 (0x80 is not even a
// valid data byte in a SysEx). The bank sits in front of an empty dump so that a read one past its end would find an
// empty slot and ask for it.
TEST(memsync_read_missing_never_asks_past_program_127)
{
    struct {
        Bank bank;
        Dump after;
    } cache;
    MemorySync *ms = nullptr;
    int singles = 0, bad = 0;
    MemorySync sync([&](const Bytes &m) {
        if (m.size() >= 9 && m[6] == 0x30) {
            singles++;
            bad += m[8] > 0x7F;
            ms->feed(fake_single(m[7], m[8], 1));
        }
        return true;
    });
    ms = &sync;
    CHECK_EQ(sync.read_missing(5, cache.bank), 128);
    CHECK_EQ(singles, 128);
    CHECK_EQ(bad, 0);
}

namespace {
struct TempDir {
    std::filesystem::path dir;
    explicit TempDir(const char *name) : dir(std::filesystem::temp_directory_path() / name)
    {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::string write(const std::string &name, const Bytes &b) const
    {
        FILE *f = fopen((dir / name).string().c_str(), "wb");
        fwrite(b.data(), 1, b.size(), f);
        fclose(f);
        return (dir / name).string();
    }
};
} // namespace

// a cache file holds exactly 128 programs: whatever follows them is not read (and cannot make the load fail), and a
// dump of up to 4096 bytes is accepted (only longer ones are taken as a broken file)
TEST(memsync_bank_file_limits)
{
    TempDir t("vtitests-memsync-limits");
    Bytes full = {'M', 'I', 'D', 'I', 0, 1, 8, 0};
    for (int p = 0; p < 128; p++) {
        Dump d = fake_single(1, p, p);
        full.insert(full.end(), {0x00, 0x00, 0x02, 0x0C});
        full.insert(full.end(), d.begin(), d.end());
    }
    full.insert(full.end(), {0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02}); // trailing bytes after program 128
    Bank in;
    CHECK(load_bank_file(t.write("trailing.bin", full), in));
    CHECK(in[127] == fake_single(1, 127, 127));

    Bytes big(12 + 4096, 0x11);
    const uint8_t head[12] = {'M', 'I', 'D', 'I', 0, 1, 8, 0, 0x00, 0x00, 0x10, 0x00}; // one dump of 4096 bytes
    std::copy(head, head + 12, big.begin());
    big.insert(big.end(), 127 * 4, 0); // programs 2..128 empty
    Bank b;
    CHECK(load_bank_file(t.write("4096.bin", big), b));
    CHECK_EQ(b[0].size(), 4096u);
}

// a save that cannot be done reports it, and an existing cache file at the path is never left half-written
TEST(memsync_bank_file_save_errors)
{
    TempDir t("vtitests-memsync-save");
    Bank bank;
    bank[0] = fake_single(1, 0, 0);
    CHECK(!save_bank_file((t.dir / "no-such-folder" / "bank.bin").string(), bank)); // the temp file cannot be made
    // the rename at the end fails (a non-empty folder is in the way): the folder stays as it was
    const std::filesystem::path in_way = t.dir / "bank.bin";
    std::filesystem::create_directories(in_way);
    t.write("bank.bin/keep", {1});
    CHECK(!save_bank_file(in_way.string(), bank));
    CHECK(std::filesystem::is_directory(in_way) && std::filesystem::exists(in_way / "keep"));
#ifndef _WIN32
    // the disk is full when the buffered data is written at fclose (the writes before it all succeed): simulated
    // with a file size limit of 100 bytes (SIGXFSZ ignored, so the write fails with EFBIG instead)
    {
        Bank empty; // 8 + 128 x 4 = 520 bytes: all in stdio's buffer until fclose
        const std::string path = (t.dir / "full.bin").string();
        const std::string old = t.write("full.bin", {'o', 'l', 'd'});
        struct rlimit was;
        getrlimit(RLIMIT_FSIZE, &was);
        void (*was_sig)(int) = signal(SIGXFSZ, SIG_IGN);
        struct rlimit small = was;
        small.rlim_cur = 100;
        setrlimit(RLIMIT_FSIZE, &small);
        const bool ok = save_bank_file(path, empty);
        setrlimit(RLIMIT_FSIZE, &was);
        signal(SIGXFSZ, was_sig);
        CHECK(!ok);
        CHECK_EQ(std::filesystem::file_size(old), 3u); // the old file is kept
    }
#endif
}

// #50: the RAM cache is per Virus number - Virus 1 in the cache folder itself, Virus 2.. in virus-N below it - and an
// instance on Virus 2 must load that folder's banks, not Virus 1's (it re-read all 512 programs at every start)
TEST(memsync_ram_cache_per_virus)
{
    TempDir t("vtitests-ramcache");
    const std::string base = t.dir.string();
    CHECK(ram_cache_dir(base, 0) == base);
    CHECK(ram_cache_dir(base, 1) == base);
    CHECK(ram_cache_dir(base, 2) == base + "/virus-2");
    CHECK(ram_cache_dir("", 2).empty());
    Bank one, two;
    one[0] = Dump(524, 1);
    two[0] = Dump(524, 2);
    CHECK(save_bank_file(base + "/RAM A", one));
    std::filesystem::create_directories(t.dir / "virus-2");
    CHECK(save_bank_file(base + "/virus-2/RAM B", two));
    auto v1 = load_ram_cache(ram_cache_dir(base, 1));
    CHECK_EQ(v1.size(), 1u);
    CHECK(v1.count(0) && v1[0].size() == 128 && v1[0][0] == one[0]);
    auto v2 = load_ram_cache(ram_cache_dir(base, 2));
    CHECK_EQ(v2.size(), 1u); // not Virus 1's RAM A
    CHECK(v2.count(1) && v2[1][0] == two[0]);
    CHECK(load_ram_cache("").empty());
}

// #50 (E563): the log names the RAM cache an instance loaded - whose folder, how many banks - without the path
TEST(memsync_ram_cache_note_names_the_folder)
{
    CHECK(ram_cache_note(0, 4) == "RAM cache: Virus 1's folder, 4 of 4 banks");
    CHECK(ram_cache_note(1, 0) == "RAM cache: Virus 1's folder, 0 of 4 banks");
    CHECK(ram_cache_note(2, 3) == "RAM cache: Virus 2's own folder, 3 of 4 banks");
}

// T2 (research R11-2): the plug-in notices a bank cache file the Control Center wrote (restore, backup) by its
// modification time and reloads that bank
TEST(memsync_file_watch_sees_a_rewritten_cache_file)
{
    TempDir t("vtitests-filewatch");
    const std::string path = (t.dir / "RAM A").string();
    FileWatch w;
    CHECK(!w.changed(path)); // no file: nothing to reload
    Bank b;
    b[0] = Dump(524, 1);
    CHECK(save_bank_file(path, b));
    CHECK(w.changed(path));  // seen the first time: the caller compares it with what it has
    CHECK(!w.changed(path)); // unchanged
    b[0] = Dump(524, 2);
    CHECK(save_bank_file(path, b));
    // a coarse file system clock could give the rewrite the same time: move it on explicitly
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));
    CHECK(w.changed(path));
    CHECK(!w.changed(path));
    std::filesystem::remove(path);
    CHECK(!w.changed(path)); // gone: nothing to reload
    CHECK(save_bank_file(path, b));
    CHECK(w.changed(path)); // there again
}
