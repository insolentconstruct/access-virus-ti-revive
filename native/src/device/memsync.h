// memsync.h - the Virus' RAM banks in a local cache, kept current like the original plug-in does it (docs/protocol.md):
// per bank a bank-info request (39 -> 14: one 14-bit fingerprint per program), then single dump requests (30 -> 10)
// for the programs whose cached dump has a different fingerprint. The Virus answers one request at a time (~1 s each;
// overlapping requests make it truncate replies), so requests are strictly sequential.
//
// Cache files use the original's format, so an existing Access cache can be imported: "MIDI" 00 01 08 00, then
// 128 x (u32 big-endian length 524, single dump).
#pragma once
#include <atomic>
#include "broker.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vti {

using Dump = std::vector<uint8_t>;
using Bank = std::array<Dump, 128>; // empty dump = not cached

bool load_bank_file(const std::string &path, Bank &bank);
bool save_bank_file(const std::string &path, const Bank &bank);
// the temporary file a save writes before its rename: "<path>.tmp.<process id>.<n>", a new one per call, because the
// plug-in, the Control Center and two plug-in instances in one DAW save the same cache files (research R11-5)
std::string bank_temp_path(const std::string &path);
// the RAM bank cache of Virus `slot` (1-based): Virus 1 (or not chosen yet) in `base` itself, Virus 2.. in base/virus-N
std::string ram_cache_dir(const std::string &base, int slot);
// RAM A..D (0..3) from the "RAM X" files of `dir`; a bank whose file does not load completely is left out
std::map<int, std::vector<Dump>> load_ram_cache(const std::string &dir);
// for the log: whose RAM cache folder an instance loaded and how many of its 4 banks (no path, rule 5)
std::string ram_cache_note(int slot, int banks);

// A bank cache file another program wrote (the Control Center's restore or backup, research R11-2): changed() is true
// when the file's modification time differs from the one seen last for that path, also the first time; false while
// the file is missing. The caller compares the content with its own copy, so its own saves cost one load, no reload.
class FileWatch {
public:
    bool changed(const std::string &path);

private:
    std::map<std::string, std::filesystem::file_time_type> seen_;
};

class MemorySync {
public:
    // standalone: owns the broker's receive handler (tools/virussync)
    MemorySync(Broker &broker, int device_id = 0x10);
    // embedded (the plug-in): `send` queues a SysEx to the Virus, replies are passed in with feed()
    explicit MemorySync(std::function<bool(const std::vector<uint8_t> &)> send, int device_id = 0x10);
    ~MemorySync();
    void feed(const std::vector<uint8_t> &sysex); // a SysEx from the Virus
    size_t queued();                               // SysEx kept for a waiting request (tests)

    // One request/reply round trip: sends `request` (a SysEx), waits for a SysEx reply whose bytes 6..8 equal
    // `match` (prefix). Retries `tries` times, each waiting `timeout_ms` + 1.5 s (plus the queue, see `sending`); a
    // reply to any try of it counts, also one that comes after the next try went out (E591), but none that came before
    // its first request went out (a late answer to an earlier one, R2-5). Empty result on failure.
    Dump transact(const Dump &request, const std::vector<uint8_t> &match, int timeout_ms = 3000, int tries = 3);
    // the last transact got no reply although every try went out and waited its whole time: the Virus did not answer
    // (not a cancel, a handover, a send that failed, or a reply of the wrong size). The plug-in's cue for AnswerWatch
    // (E591): a sync that ended with -1 because of it, or the readiness probe.
    bool missed() const { return missed_; }
    // a request is out and its transact has not returned (answer, timeout over all tries, cancel): the plug-in's
    // process() holds its MIDI clock and automation meanwhile, like the original drops F8 while busy (research T9).
    // Lock-free, read on the audio thread
    bool request_open() const { return open_.load(std::memory_order_relaxed); }
    // Ends a waiting transact() at once and makes every later one return empty right away (the plug-in's
    // terminate(): it waited up to 10 s for a request in flight, review 2026-10-19 M6).
    void cancel();

    // Bank info of RAM bank 1..4: 128 fingerprints, or empty.
    std::vector<int> bank_info(int bank);
    // `asks` without an answer before it gives up (3 like the original's sync; the plug-in's read before a RAM write
    // asks once, research T2)
    Dump single(int bank, int program, int asks = 3);

    // Brings `cache` up to date for RAM bank 1..4; progress(program, fetched) is called per fetched program.
    // Returns the number of fetched programs, -1 when the bank info gets no answer or at the 3rd given-up slot.
    // `stop` (optional) cancels between two requests, cancel() also during one: returns -2, `cache` keeps what was
    // fetched so far.
    int sync_bank(int bank, Bank &cache, const std::function<void(int, bool)> &progress = nullptr,
                  const std::atomic<bool> *stop = nullptr);
    // "Download entire bank from Virus TI" like the original (E301): RAM bank 1..4, all 128 programs in order, each
    // request after the display message 74 01 7E 04 (Actions/Rise Message 4: "syncing" on the Virus), cached or not.
    // progress(program) per program read. Returns 128, -1 when a request gets no reply, -2 when cancelled (like
    // sync_bank); `cache` keeps what was read, the rest unchanged.
    int read_bank(int bank, Bank &cache, const std::function<void(int)> &progress = nullptr,
                  const std::atomic<bool> *stop = nullptr);
    // A user ROM bank (bank 5..30, E381/E382): the original has no bank info for ROM banks, it reads the programs it
    // has no data for, each after 74 01 7E 04, with 30 <bank> <program> (captured: 128 requests ~1 s apart). Reads
    // only the empty slots of `cache`; progress(program) per program read. Returns the number read, -1 at the 3rd
    // given-up slot (see given_up), -2 as read_bank; `cache` keeps what was read.
    int read_missing(int bank, Bank &cache, const std::function<void(int)> &progress = nullptr,
                     const std::atomic<bool> *stop = nullptr);
    // called before every request: may block (e.g. while the host plays - the Virus delays notes while it answers
    // dump requests); returning false cancels like `stop`
    std::function<bool()> before_request;
    // optional: one line per bank info / single dump reply of the wrong size or no reply (not after cancel()), so a
    // "bank sync failed" log tells lost packets from a Virus that does not answer (#34)
    std::function<void(const std::string &)> log;
    // optional (the plug-in): true while its out queue still holds messages. Like the original, a request goes out
    // only when nothing else waits, and its timeout starts when it is out of the queue, not when it was queued
    // behind a state upload (E591); both waits together at most queue_wait_ms per try (a queue that does not empty:
    // the Virus refusing EP 0x06 for 8.7 s, E591 (3), or a broker gone - the request is sent anyway)
    std::function<bool()> sending;
    int queue_wait_ms = 15000;
    // optional (the plug-in): false while this instance has no Virus (handed over to the played instance, #51, or
    // waiting for another one, or the Virus gone, R6-2): a request does not start and one waiting ends at once,
    // counted as a stop like cancel() (no "no reply" line, no given-up slot, sync_bank returns -2) but not for good
    // (E591 (2)); the plug-in checks the banks again when the Virus is back
    std::function<bool()> present;
    // optional (the plug-in): sends the reply-cable and link start-up messages again (AnswerWatch::routing_messages).
    // bank_info calls it before its 5th and 6th ask, like the original's event 0x22 (0x18008e220, E591, E594)
    std::function<void()> resend_routing;
    // the waits per ask (the original's 5 s and 10 s, E591) and the pause after each before the next (E145); tests
    // shorten them
    int bank_info_ms = 5000, single_ms = 10000, pause_ms = 1500;

    // Like the original (0x18008e220, E594): a slot that cannot be read (no reply to 3 asks, or only replies of the
    // wrong size, checksum or fingerprint) is given up: its cache entry is emptied (a wrong or stale dump is never
    // kept), it goes into `unconfirmed`, and sync_bank / read_missing go on with the next one. The 3rd given-up slot
    // since `given_up` was set to 0 (the plug-in does at the start of its sync) ends them with -1.
    int given_up = 0;
    // per bank (1-based, as sync_bank's `bank`): the programs given up in its last sync_bank / read_missing, plus the
    // stale ones a stop at the 3rd left unread. Written on the caller's thread; read it there after the call.
    std::map<int, std::set<int>> unconfirmed;

    // Like the original (0x18008eaf0 -> 0x18009c150 -> 0x1800a1cd0, research R1-5/R10-6, T18): a single dump of RAM
    // bank 1..4 that no transact waits for (the late answer to a given-up slot, a second answer, one nobody asked
    // for) is kept, the newest per slot, when it is 524 bytes with our checksums (E552), until file_late takes it. A
    // transact's own answer for the slot drops an older kept one. Not bank 0 (edit buffers, the probe's 10 00 00), not
    // a user ROM bank (its cache is read_missing's, once per empty slot), not a bank info (14: we keep no fingerprints
    // apart from the dumps, so there is nothing to file it in).
    bool has_late(int bank);
    // Moves the kept dumps of `bank` into `cache` (made 128 long) and out of unconfirmed[bank]: the number of slots
    // whose content changed. On the thread that runs sync_bank (unconfirmed).
    int file_late(int bank, std::vector<Dump> &cache);
    // true while a RAM write or download runs: drops what is kept and keeps nothing (it may be older than what the
    // write or the download puts in the cache)
    void hold_late(bool on);
    void drop_late(); // the Virus went away: what it sent must not be filed into the next one's cache

private:
    bool give_up(int bank, int program); // true at the 3rd: stop
    void note_reply(const char *what, int bank, int program, size_t got, size_t want, int t);
    bool was_cancelled(); // cancel(), or no Virus for this instance (present)
    bool wait_slice(const std::vector<uint8_t> &match, Dump &reply, std::chrono::steady_clock::time_point until);
    Broker *broker_ = nullptr;
    std::function<bool(const std::vector<uint8_t> &)> send_;
    int dev_;
    std::mutex mu_;
    std::mutex tx_mu_; // one request at a time: the Virus drops replies to requests that overlap (E145)
    std::condition_variable cv_;
    std::vector<Dump> inbox_;
    bool cancelled_ = false; // under mu_
    bool waiting_ = false;   // under mu_: a transact is running, feed() keeps Access SysEx
    std::vector<uint8_t> match_;                  // under mu_: what the running transact waits for
    std::map<std::pair<int, int>, Dump> late_;    // under mu_: (bank, program) -> kept dump (has_late)
    bool late_held_ = false;                      // under mu_ (hold_late)
    std::atomic<bool> missed_{false}; // set under tx_mu_, read by the plug-in after the transact
    std::atomic<bool> open_{false};   // set under tx_mu_ (request_open)
};

} // namespace vti
