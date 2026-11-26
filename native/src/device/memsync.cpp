// memsync.cpp - see memsync.h.
#include "memsync.h"
#include "model/utf8.h"
#include "../model/bankdrop.h"
#include "../model/remotedevice.h"
#include "usbmidi.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <memory>
#include <filesystem>

namespace vti {

static const uint8_t EP_PLUGIN_OUT = 0x06, CABLE_PLUGIN = 2;
static const size_t SINGLE_SIZE = 524, BANKINFO_SIZE = 267;

// the Virus' display message 74 01 7E n (Actions/Rise Message n: 4 "syncing", 7 "Configuring Virus TI")
static std::vector<uint8_t> rise_message(int dev, uint8_t n)
{
    return {0xF0, 0x00, 0x20, 0x33, 0x01, (uint8_t)dev, 0x74, 0x01, 0x7E, n, 0xF7};
}

bool load_bank_file(const std::string &path, Bank &bank)
{
    FILE *f = u8fopen(path, "rb");
    if (!f)
        return false;
    uint8_t hdr[8];
    bool ok = fread(hdr, 1, 8, f) == 8 && !memcmp(hdr, "MIDI", 4);
    for (int p = 0; ok && p < 128; p++) {
        uint8_t l[4];
        if (fread(l, 1, 4, f) != 4) {
            ok = false; // cut short: a saved bank has all 128 entries
            break;
        }
        size_t n = (size_t)l[0] << 24 | l[1] << 16 | l[2] << 8 | l[3];
        if (n > 4096) {
            ok = false;
            break;
        }
        bank[p].resize(n);
        if (fread(bank[p].data(), 1, n, f) != n)
            ok = false;
    }
    fclose(f);
    return ok;
}

std::string ram_cache_dir(const std::string &base, int slot)
{
    return slot > 1 && !base.empty() ? base + "/virus-" + std::to_string(slot) : base;
}

std::map<int, std::vector<Dump>> load_ram_cache(const std::string &dir)
{
    std::map<int, std::vector<Dump>> out;
    for (int b = 0; b < 4 && !dir.empty(); b++) {
        Bank bank;
        if (load_bank_file(dir + "/RAM " + (char)('A' + b), bank))
            out[b].assign(bank.begin(), bank.end());
    }
    return out;
}

bool FileWatch::changed(const std::string &path)
{
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(u8path(path), ec);
    if (ec) {
        seen_.erase(path);
        return false;
    }
    auto it = seen_.find(path);
    if (it != seen_.end() && it->second == t)
        return false;
    seen_[path] = t;
    return true;
}

std::string ram_cache_note(int slot, int banks)
{
    const std::string whose = slot > 1 ? "Virus " + std::to_string(slot) + "'s own" : "Virus 1's";
    return "RAM cache: " + whose + " folder, " + std::to_string(banks) + " of 4 banks";
}

bool save_bank_file(const std::string &path, const Bank &bank)
{
    std::string tmp = path + ".tmp";
    FILE *f = u8fopen(tmp, "wb");
    if (!f)
        return false;
    static const uint8_t hdr[8] = {'M', 'I', 'D', 'I', 0x00, 0x01, 0x08, 0x00};
    bool ok = fwrite(hdr, 1, 8, f) == 8;
    for (auto &d : bank) {
        uint8_t l[4] = {(uint8_t)(d.size() >> 24), (uint8_t)(d.size() >> 16), (uint8_t)(d.size() >> 8), (uint8_t)d.size()};
        ok = ok && fwrite(l, 1, 4, f) == 4 && fwrite(d.data(), 1, d.size(), f) == d.size();
    }
    ok = fclose(f) == 0 && ok;
    // std::filesystem::rename replaces an existing file on Windows too (C rename() fails there)
    std::error_code ec;
    if (ok)
        std::filesystem::rename(u8path(tmp), u8path(path), ec);
    return ok && !ec;
}

MemorySync::MemorySync(Broker &broker, int device_id) : broker_(&broker), dev_(device_id)
{
    send_ = [this](const std::vector<uint8_t> &m) {
        auto pk = usbmidi_pack(m, CABLE_PLUGIN);
        return broker_->write(EP_PLUGIN_OUT, pk.data(), pk.size());
    };
    // replies come on EP 0x86 cable 3 in a plug-in session, otherwise on EP 0x85 cable 1 (MIDI port)
    auto parser = std::make_shared<UsbMidiParser>([this](uint8_t, int, const std::vector<uint8_t> &m) { feed(m); });
    broker_->on_rx([parser](uint8_t ep, const uint8_t *d, size_t len) { parser->feed(ep, d, len); });
}

MemorySync::MemorySync(std::function<bool(const std::vector<uint8_t> &)> send, int device_id)
    : send_(std::move(send)), dev_(device_id)
{
}

MemorySync::~MemorySync()
{
    if (broker_)
        broker_->on_rx(nullptr);
}

void MemorySync::feed(const std::vector<uint8_t> &m)
{
    if (m.size() > 8 && m[0] == 0xF0 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x33) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!waiting_) // the plug-in sees every Access SysEx of a session: keep only what a request waits for
            return;
        inbox_.push_back(m);
        cv_.notify_all();
    }
}

size_t MemorySync::queued()
{
    std::lock_guard<std::mutex> lk(mu_);
    return inbox_.size();
}

// one slice (<= 50 ms, so sending() and present() are looked at) of the wait until `until`: false when it is over -
// a matching reply in `reply` (also one to an earlier try: the same request, E591 (1)), cancel(), or no Virus
bool MemorySync::wait_slice(const std::vector<uint8_t> &match, Dump &reply, std::chrono::steady_clock::time_point until)
{
    if (present && !present())
        return false;
    std::unique_lock<std::mutex> lk(mu_);
    auto over = [&] {
        for (auto &m : inbox_)
            if (m.size() >= 6 + match.size() && std::equal(match.begin(), match.end(), m.begin() + 6)) {
                reply = m;
                return true;
            }
        return cancelled_;
    };
    return !cv_.wait_until(lk, std::min(until, std::chrono::steady_clock::now() + std::chrono::milliseconds(50)), over);
}

Dump MemorySync::transact(const Dump &request, const std::vector<uint8_t> &match, int timeout_ms, int tries)
{
    // one request at a time, also across threads: the readiness probe after a reconnect met a bank sync still
    // running, the Virus answered only the newer request (E145) and the probe waited 10 s (review 2026-10-19)
    std::lock_guard<std::mutex> one(tx_mu_);
    missed_ = false;
    struct Done { // on every return, after the reply was copied out: stop keeping SysEx, free what is left
        MemorySync *s;
        ~Done()
        {
            std::lock_guard<std::mutex> lk(s->mu_);
            s->waiting_ = false;
            s->inbox_.clear();
        }
    } done{this};
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (cancelled_)
            return {};
        inbox_.clear();
        waiting_ = true; // before the first send: a fast reply must not be lost; kept over all tries (E591 (1))
    }
    using clock = std::chrono::steady_clock;
    Dump reply;
    const auto queued = [this] { return sending && sending(); };
    for (int t = 0; t < tries; t++) {
        const auto cap = clock::now() + std::chrono::milliseconds(queue_wait_ms);
        // like the original: ask only when nothing else waits to go out (E591), else the request is stuck behind it
        while (queued() && clock::now() < cap)
            if (!wait_slice(match, reply, cap))
                return reply;
        if (!wait_slice(match, reply, clock::now())) // cancelled, no Virus, or an earlier try's answer came meanwhile
            return reply;
        if (!send_(request))
            return {};
        // the timeout starts when the request is out of the queue, not when it was queued (E591 (1))
        while (queued() && clock::now() < cap)
            if (!wait_slice(match, reply, cap))
                return reply;
        // the timeout, then 1.5 s more before the next try (overlapping requests confuse the Virus, E145): a reply
        // in that pause counts too, it used to be dropped (E591 (1))
        const auto until = clock::now() + std::chrono::milliseconds(timeout_ms + pause_ms);
        while (clock::now() < until)
            if (!wait_slice(match, reply, until))
                return reply;
    }
    missed_ = true; // every try waited its whole time: the Virus did not answer (E591)
    return {};
}

void MemorySync::cancel()
{
    std::lock_guard<std::mutex> lk(mu_);
    cancelled_ = true;
    cv_.notify_all();
}

// an empty reply after cancel() is the plug-in closing, not a Virus that did not answer (E314: "bank sync failed"
// when Live closed the plug-in during the sync)
bool MemorySync::was_cancelled()
{
    if (present && !present()) // no Virus for this instance (handed over, E591 (2)): a stop, not a miss
        return true;
    std::lock_guard<std::mutex> lk(mu_);
    return cancelled_;
}

// the slot's name in the log, as the Virus shows it
static std::string slot_name(int bank, int program)
{
    std::string where = bank >= 1 && bank <= 4 ? std::string("RAM ") + (char)('A' + bank - 1) : "bank " + std::to_string(bank);
    return program >= 0 ? where + " " + std::to_string(program) : where;
}

bool MemorySync::give_up(int bank, int program)
{
    unconfirmed[bank].insert(program);
    if (log)
        log("single " + slot_name(bank, program) + ": given up, not cached (" + std::to_string(given_up + 1) + " of 3)");
    return ++given_up >= 3; // the original's abort 0x18008e0d0 at its 3rd given-up slot
}

void MemorySync::note_reply(const char *what, int bank, int program, size_t got, size_t want, int t)
{
    if (!log || got == want || (got == 0 && was_cancelled()))
        return;
    log(std::string(what) + " " + slot_name(bank, program) + ": " +
        (got == 0 ? std::string("no reply")
                  : "reply " + std::to_string(got) + " bytes, expected " + std::to_string(want) + " (try " +
                        std::to_string(t + 1) + " of 3)"));
}

std::vector<int> MemorySync::bank_info(int bank)
{
    // a reply of the wrong size is a packet lost on the way: ask again as single() does, one lost packet must not end
    // the whole sync
    const Dump req = {0xF0, 0x00, 0x20, 0x33, 0x01, (uint8_t)dev_, 0x39, (uint8_t)bank, 0xF7};
    const std::vector<uint8_t> match = {0x14, (uint8_t)bank};
    for (int t = 0; t < 3; t++) {
        // 6 asks 5 s apart like the original (0x18008e220, E594): before the 5th and the 6th it sets the reply cables
        // again (event 0x22, E591). Its 7th request goes out with the abort, unwaited: not sent here (E145)
        Dump r = transact(req, match, bank_info_ms, 4);
        for (int k = 0; k < 2 && r.empty() && missed_; k++) {
            if (resend_routing)
                resend_routing();
            r = transact(req, match, bank_info_ms, 1);
        }
        note_reply("bank info", bank, -1, r.size(), BANKINFO_SIZE, t);
        if (r.size() == BANKINFO_SIZE) {
            std::vector<int> v(128);
            for (int p = 0; p < 128; p++)
                v[p] = r[9 + 2 * p] | r[10 + 2 * p] << 7;
            return v;
        }
        if (r.empty())
            break;
    }
    return {};
}

Dump MemorySync::single(int bank, int program, int asks)
{
    Dump bad; // the last reply with checksums other than ours
    for (int t = 0; t < 3; t++) {
        // 3 asks 10 s apart like the original (0x18008e220, E594; a late reply to an earlier ask counts, E591): since a
        // slot without an answer is given up instead of ending the sync, the third ask is worth its 11.5 s. The
        // original's 4th request goes out with the give-up, unwaited: not sent here (E145)
        Dump r = transact({0xF0, 0x00, 0x20, 0x33, 0x01, (uint8_t)dev_, 0x30, (uint8_t)bank, (uint8_t)program, 0xF7},
                          {0x10, (uint8_t)bank, (uint8_t)program}, single_ms, asks);
        note_reply("single", bank, program, r.size(), SINGLE_SIZE, t);
        if (r.size() == SINGLE_SIZE) {
            // the Virus sends both checksums as BankDrop computes them (E552): a mismatch is a reply damaged on the
            // way, asked again like a wrong size - it must not go into the bank cache on disk (audit #32)
            Dump c = r;
            BankDrop::checksums(c);
            if (c == r)
                return r;
            const std::string where = "single " + std::to_string(bank) + " " + std::to_string(program);
            if (r == bad) { // a damaged packet differs from try to try; a model that counts otherwise does not (E552)
                if (log)
                    log(where + ": the same reply twice with other checksums: taken as the Virus sends it");
                return r;
            }
            bad = r;
            if (log)
                log(where + ": reply with a wrong checksum (try " + std::to_string(t + 1) + " of 3)");
            continue;
        }
        if (r.empty())
            break;
    }
    return {};
}

int MemorySync::sync_bank(int bank, Bank &cache, const std::function<void(int, bool)> &progress,
                          const std::atomic<bool> *stop)
{
    if ((before_request && !before_request()) || was_cancelled()) // no display message for a Virus we do not have
        return -2;
    // the original keeps "Configuring Virus TI" (Rise Message 7) up while it checks the banks: before every bank info
    // request, fetched or not (E321)
    send_(rise_message(dev_, 7));
    unconfirmed[bank].clear();
    auto info = bank_info(bank);
    if (info.empty())
        return was_cancelled() ? -2 : -1;
    int fetched = 0;
    for (int p = 0; p < 128; p++) {
        if (stop && *stop)
            return -2;
        bool stale = RemoteDevice::single_fingerprint(cache[p]) != info[p];
        if (stale) {
            if (before_request && !before_request())
                return -2;
            // a dump of the right size that does not match the bank info: ask again before giving up, bounded. A wrong
            // dump is never cached. Each request after Rise Message 4 like the original's start-up sync (its capture:
            // 74 01 7E 04 + 30 bank program per try), so the Virus shows its syncing message, not "Configuring"
            Dump d;
            for (int t = 0; t < 3; t++) {
                if (t > 0 && before_request && !before_request())
                    return -2;
                send_(rise_message(dev_, 4));
                d = single(bank, p);
                if (d.empty() || RemoteDevice::single_fingerprint(d) == info[p])
                    break;
                d.clear();
            }
            if (d.empty()) {
                if (was_cancelled())
                    return -2;
                cache[p].clear(); // its fingerprint differs from the Virus' one: known wrong
                if (give_up(bank, p)) {
                    for (int q = p + 1; q < 128; q++) // the original marks every slot left unread (0x18008e0d0)
                        if (RemoteDevice::single_fingerprint(cache[q]) != info[q]) {
                            cache[q].clear();
                            unconfirmed[bank].insert(q);
                        }
                    return -1;
                }
                stale = false; // not fetched
            } else {
                cache[p] = std::move(d);
                fetched++;
            }
        }
        if (progress)
            progress(p, stale);
    }
    return fetched;
}

int MemorySync::read_bank(int bank, Bank &cache, const std::function<void(int)> &progress, const std::atomic<bool> *stop)
{
    const std::vector<uint8_t> rise = rise_message(dev_, 4);
    for (int p = 0; p < 128; p++) {
        if ((stop && *stop) || (before_request && !before_request()) || was_cancelled())
            return -2;
        send_(rise);
        Dump d = single(bank, p);
        if (d.empty())
            return was_cancelled() ? -2 : -1;
        cache[p] = std::move(d);
        if (progress)
            progress(p);
    }
    return 128;
}

int MemorySync::read_missing(int bank, Bank &cache, const std::function<void(int)> &progress,
                             const std::atomic<bool> *stop)
{
    const std::vector<uint8_t> rise = rise_message(dev_, 4);
    int read = 0;
    unconfirmed[bank].clear();
    for (int p = 0; p < 128; p++) {
        if (!cache[p].empty())
            continue;
        if ((stop && *stop) || (before_request && !before_request()) || was_cancelled())
            return -2;
        send_(rise);
        Dump d = single(bank, p);
        if (d.empty()) {
            if (was_cancelled())
                return -2;
            if (give_up(bank, p)) // stays empty: the next start reads it again
                return -1;
            continue;
        }
        cache[p] = std::move(d);
        read++;
        if (progress)
            progress(p);
    }
    return read;
}

} // namespace vti
