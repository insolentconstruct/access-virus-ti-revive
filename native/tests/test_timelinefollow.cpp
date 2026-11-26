// The Virus timeline after its anchor (E337): an uneven host cadence must not make it jump back and forth (each jump
// is a click), while a real lasting offset (REAPER after a render, E279) still jumps soon. The tests run a small model:
// the ring grows in 441-frame USB chunks (10 ms), the host calls 256-frame blocks at the given times, the timeline
// moves by the block plus what TimelineFollow says - the same arithmetic as process().
#include "check.h"
#include "device/crossfade.h"
#include "device/stamprule.h"
#include "device/timelinefollow.h"
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace vti;

namespace {

constexpr int RATE = 44100, BLOCK = 256;
constexpr int64_t LEAD_A = 3072, FLOOR_MIN = 1024; // stamprule.h timing_lead, virusplugin.h floor_min
constexpr int64_t ROOM = LEAD_A - FLOOR_MIN;       // what process() hands to set_room (#37)

struct Lcg { // deterministic jitter
    uint64_t s;
    int64_t next(int64_t n)
    {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return (int64_t)((s >> 33) % (uint64_t)n);
    }
};

struct Ring {
    double ppm = 0;          // the Virus' clock against the host's
    int64_t offset_at = -1;  // from this host time on the ring is `offset` frames further (a real offset)
    int64_t offset = 0;
    int64_t newest(int64_t t) const
    {
        int64_t v = (int64_t)((double)t * (1.0 + ppm * 1e-6));
        v = v / 441 * 441;
        return offset_at >= 0 && t >= offset_at ? v + offset : v;
    }
};

struct Result {
    int jumps = 0, steps = 0; // jumps: hard and crossfaded ones
    std::vector<int64_t> jump_at, jump_by, hard_at; // hard_at: the jumps without a crossfade
    std::vector<int64_t> t, err;
    std::vector<int64_t> shift_at, shift_by; // pre-shifts at Play (from a stop, a burst learned before)
};

// `plays`: host times of Play starts; as in process(), the block that sees Play tells the follower after its update.
// `activated`: each Play comes right after setActive(1) (REAPER), seen by process() before the Play block's update
// `room` > 0: set_room, as the plug-in does at the default lead (#37)
Result run(const std::vector<int64_t> &calls, const Ring &ring, bool marker = true,
           const std::vector<int64_t> &plays = {}, bool activated = false, bool from_stop = false, int64_t room = 0)
{
    TimelineFollow f;
    f.set_window(RATE / 2);
    if (room > 0)
        f.set_room(room);
    Result r;
    int64_t tl = ring.newest(calls[0]); // anchored at the first block
    size_t next_play = 0;
    for (int64_t c : calls) {
        const int64_t err = ring.newest(c) - tl;
        r.t.push_back(c), r.err.push_back(err);
        if (activated && next_play < plays.size() && c >= plays[next_play])
            f.activated();
        const TimelineFollow::Move m = f.update(err, BLOCK, marker);
        if (m.jump && !m.fade)
            r.hard_at.push_back(c);
        if (m.jump)
            r.jumps++, r.jump_at.push_back(c), r.jump_by.push_back(m.delta);
        else if (m.delta != 0)
            r.steps++;
        if (next_play < plays.size() && c >= plays[next_play]) {
            const int64_t s = f.play_started(from_stop);
            if (s != 0)
                r.shift_at.push_back(c), r.shift_by.push_back(s);
            tl += s;
            next_play++;
        }
        tl += m.delta + BLOCK;
    }
    return r;
}

// steady, and at each Play REAPER's burst (diag.log 2026-10-24 21:08-21:18: 1792 host frames more than the Virus made
// in the 10 s with each Play, block 256): `extra` blocks at once, after which the host stays that far ahead
std::vector<int64_t> steady_with_plays(int seconds, const std::vector<int64_t> &plays, int extra = 7,
                                       const std::vector<int> &extras = {})
{
    std::vector<int64_t> c;
    size_t p = 0;
    for (int64_t i = 0; i < (int64_t)seconds * RATE / BLOCK; i++) {
        const int64_t t = i * BLOCK + 50;
        c.push_back(t);
        if (p < plays.size() && t >= plays[p]) {
            for (int k = 0; k < (p < extras.size() ? extras[p] : extra); k++)
                c.push_back(t);
            p++;
        }
    }
    return c;
}

// even calls, `seconds` long
std::vector<int64_t> steady(int seconds)
{
    std::vector<int64_t> c;
    for (int64_t i = 0; i < (int64_t)seconds * RATE / BLOCK; i++)
        c.push_back(i * BLOCK + 50);
    return c;
}

// steady, then `rough` seconds of bursts of 8 blocks around the burst's middle with +-jit frames of jitter and a burst
// 40 ms late every ~5 s, then steady again (the cadence of E337's episodes, exaggerated)
std::vector<int64_t> uneven(uint64_t seed, int calm1, int rough, int calm2, int64_t jit)
{
    Lcg rng{seed};
    const int64_t n1 = (int64_t)calm1 * RATE / BLOCK / 8 * 8, n2 = (int64_t)rough * RATE / BLOCK / 8 * 8,
                  n3 = (int64_t)calm2 * RATE / BLOCK;
    std::vector<int64_t> c;
    int64_t last = 0, burst = 0;
    for (int64_t i = 0; i < n1 + n2 + n3; i++) {
        int64_t t = i * BLOCK;
        if (i >= n1 && i < n1 + n2) {
            const int64_t b = (i - n1) / 8, k = (i - n1) % 8;
            if (k == 0)
                burst = b % 108 == 50 ? (i + 4) * BLOCK + 1764 : (i + 4) * BLOCK - BLOCK / 2 + rng.next(2 * jit + 1) - jit;
            t = burst + k * 10;
        }
        last = t > last ? t : last;
        c.push_back(last);
    }
    return c;
}

// REAPER's fast restarts (user, 2026-10-24 21:39: Play clicked again and again): from `from` every `period` a Play
// with its burst of 7 blocks, and a Stop `stop_after` later where the host leaves 3 blocks out
std::vector<int64_t> fast_restarts(int seconds, int64_t from, int64_t period, int64_t stop_after, int count,
                                   std::vector<int64_t> &plays)
{
    std::vector<int64_t> c;
    for (int k = 0; k < count; k++)
        plays.push_back(from + k * period);
    size_t p = 0, s = 0;
    for (int64_t i = 0; i < (int64_t)seconds * RATE / BLOCK; i++) {
        const int64_t t = i * BLOCK + 50;
        if (s < plays.size() && t >= plays[s] + stop_after && t < plays[s] + stop_after + 3 * BLOCK)
            continue; // the Stop: these blocks are not called
        if (s < plays.size() && t >= plays[s] + stop_after + 3 * BLOCK)
            s++;
        c.push_back(t);
        if (p < plays.size() && t >= plays[p]) {
            for (int k = 0; k < 7; k++)
                c.push_back(t);
            p++;
        }
    }
    return c;
}

} // namespace

TEST(timeline_rough_host_cadence_does_not_jump)
{
    // E337: err swung -767..852 within a second; the old rule (jump at abs(err) > 512 per block) jumped ~3700 times
    // and stepped ~3000 times in this minute of the model - here not a single jump and few drift steps
    for (uint64_t seed = 1; seed <= 4; seed++) {
        const Result r = run(uneven(seed, 10, 60, 10, 882), Ring{});
        CHECK(r.jumps == 0);
        CHECK(r.steps < 400);
    }
}

TEST(timeline_lasting_offset_jumps_after_half_a_second)
{
    // REAPER after a render (E279): the timeline is ~1000 frames off for good - one jump, half a second later
    Ring ring;
    ring.offset_at = 10 * RATE, ring.offset = 1000;
    const Result r = run(steady(20), ring);
    CHECK(r.jumps == 1);
    if (r.jumps == 1) {
        CHECK(r.jump_at[0] - ring.offset_at >= RATE * 4 / 10 && r.jump_at[0] - ring.offset_at <= RATE * 6 / 10);
        CHECK(r.jump_by[0] >= 700 && r.jump_by[0] <= 1100);
    }
    for (size_t i = 0; i < r.t.size(); i++)
        if (r.t[i] > 12 * RATE)
            CHECK(std::llabs(r.err[i]) < 450); // back at real time
}

TEST(timeline_far_offset_jumps_at_once)
{
    // REAPER processing ahead after Play (status.md: up to -8548): far beyond any jitter, no half-second wait
    Ring ring;
    ring.offset_at = 10 * RATE, ring.offset = 5000;
    const Result r = run(steady(12), ring);
    CHECK(r.jumps == 1);
    if (r.jumps == 1)
        CHECK(r.jump_at[0] - ring.offset_at < BLOCK);
}

TEST(timeline_offset_in_rough_cadence_still_jumps)
{
    // the same offset while the host's cadence is uneven: the 2 s mean finds it
    Ring ring;
    ring.offset_at = 20 * RATE, ring.offset = 1000;
    const Result r = run(uneven(1, 10, 30, 0, 400), ring);
    CHECK(r.jumps == 1);
    if (r.jumps == 1)
        CHECK(r.jump_at[0] > ring.offset_at && r.jump_at[0] - ring.offset_at <= RATE * 5 / 2);
}

TEST(timeline_without_marker_only_steps)
{
    // without the marker the Virus' stamp clock follows our anchor: no jump, the offset is stepped out one frame per block
    Ring ring;
    ring.offset_at = 5 * RATE, ring.offset = 1000;
    const Result r = run(steady(20), ring, false);
    CHECK(r.jumps == 0);
    CHECK(r.steps > 500);
    CHECK(std::llabs(r.err.back()) < 450);
}

TEST(timeline_follows_clock_drift)
{
    // the Virus' clock against the sound card's: one-frame steps keep the error inside the read margin (the read ends
    // 512 frames behind the newest, timing_m(); below err -512 a block would read frames that are not there yet, #51)
    for (double ppm : {-200.0, 200.0}) {
        Ring ring;
        ring.ppm = ppm;
        const Result r = run(steady(60), ring);
        CHECK(r.jumps == 0);
        CHECK(r.steps > 100); // 200 ppm = 529 frames a minute, part of it inside the band
        for (size_t i = 3000; i < r.err.size(); i++)
            CHECK(r.err[i] > -512 && r.err[i] < 600);
    }
}

TEST(timeline_play_burst_jumps_before_the_first_note_sounds)
{
    // REAPER at Play (Linux, TI2, 2026-10-24): 7 blocks ahead at once, then ahead for good (err about -2000). With the
    // half-second wait the reads sat at the ring's newest frame for 0.5 s - crackle on the notes right at Play. Now one
    // jump soon after the burst (before a note of the Play block sounds, ~40 ms later), and no read ahead afterwards
    const int64_t play = 10 * RATE;
    const Result r = run(steady_with_plays(14, {play}), Ring{}, true, {play});
    CHECK(r.jumps == 1);
    if (r.jumps == 1) {
        CHECK(r.jump_at[0] - play < RATE * 30 / 1000);
        CHECK(r.jump_by[0] <= -1792 && r.jump_by[0] >= -1792 - 441);
    }
    for (size_t i = 0; i < r.t.size(); i++)
        if (r.t[i] > play + RATE * 30 / 1000)
            CHECK(r.err[i] > -300); // inside the read margin (~370 frames ahead of a block are there)
}

TEST(timeline_play_burst_every_play)
{
    // each Play brings its burst, each gets its own jump
    const std::vector<int64_t> plays = {5 * RATE, 9 * RATE, 13 * RATE};
    const Result r = run(steady_with_plays(16, plays), Ring{}, true, plays);
    CHECK(r.jumps == 3);
    for (size_t j = 0; j < r.jump_at.size() && j < plays.size(); j++)
        CHECK(r.jump_at[j] - plays[j] < RATE * 30 / 1000);
}

TEST(timeline_play_without_burst_does_not_jump)
{
    // Ableton at Play (E337 log: err -367..73): nothing to correct
    const std::vector<int64_t> plays = {5 * RATE, 9 * RATE};
    const Result r = run(steady_with_plays(12, plays, 0), Ring{}, true, plays);
    CHECK(r.jumps == 0);
}

TEST(timeline_play_behind_waits_as_before)
{
    // the timeline behind real time at Play only adds latency (the frames are there): the half-second rule as before
    Ring ring;
    ring.offset_at = 10 * RATE, ring.offset = 1000;
    const Result r = run(steady(14), ring, true, {10 * RATE});
    CHECK(r.jumps == 1);
    if (r.jumps == 1)
        CHECK(r.jump_at[0] - ring.offset_at >= RATE * 4 / 10);
}

TEST(timeline_play_in_rough_cadence_no_back_and_forth)
{
    // the Windows crackle of E337 must not come back: Plays in the middle of a rough cadence give at most one jump
    // each, and none away from them
    for (uint64_t seed = 1; seed <= 4; seed++) {
        std::vector<int64_t> plays;
        for (int s = 12; s < 70; s += 5)
            plays.push_back((int64_t)s * RATE);
        const Result r = run(uneven(seed, 10, 60, 10, 882), Ring{}, true, plays);
        CHECK(r.jumps <= (int)plays.size());
        for (int64_t at : r.jump_at) {
            bool near_play = false;
            for (int64_t p : plays)
                near_play |= at >= p && at - p <= RATE;
            CHECK(near_play);
        }
    }
}

TEST(timeline_play_in_harsh_rough_cadence_changes_nothing)
{
    // the device run of 2026-10-24 (nativehost --rough 10:50 --transport 25, block 512, err -2900..3600): a Play jump by
    // the most-ahead error moved the whole band past AT_ONCE and the timeline jumped back and forth (12 jumps, none
    // with 0.8.33). A Play in a rough cadence must leave everything as without it
    for (uint64_t seed = 1; seed <= 4; seed++) {
        std::vector<int64_t> plays;
        for (int s = 12; s < 70; s += 5)
            plays.push_back((int64_t)s * RATE);
        const std::vector<int64_t> calls = uneven(seed, 10, 60, 10, 3000);
        const Result without = run(calls, Ring{});
        const Result with = run(calls, Ring{}, true, plays);
        CHECK(with.jumps == without.jumps);
        CHECK(with.jump_at == without.jump_at);
    }
}

TEST(timeline_fast_restarts_each_play_jumps)
{
    // the calm gate alone failed here (user, REAPER): right after a Play's jump the last second never looks calm, the
    // next Play waited the half second again and the bursts piled up (err -4320, 16 late events). After setActive(1)
    // a Play is a fresh start: each one jumps after its burst, and between the jumps no read is ahead
    std::vector<int64_t> plays;
    const std::vector<int64_t> calls = fast_restarts(12, 5 * RATE, RATE * 4 / 10, RATE * 3 / 10, 12, plays);
    const Result r = run(calls, Ring{}, true, plays, true);
    for (int64_t p : plays) { // one jump right after each Play's burst (the Stops' positive offsets may jump later)
        int n = 0;
        for (int64_t at : r.jump_at)
            n += at >= p && at - p < RATE * 30 / 1000;
        CHECK(n == 1);
    }
    for (size_t i = 0; i < r.t.size(); i++)
        for (int64_t p : plays)
            if (r.t[i] > p + RATE * 30 / 1000 && r.t[i] < p + RATE * 3 / 10)
                CHECK(r.err[i] > -300);
}

TEST(timeline_fast_restarts_without_activation_keep_the_gate)
{
    // a host that does not reactivate at Play keeps the calm gate (E337 hosts: nothing changes there)
    for (uint64_t seed = 1; seed <= 4; seed++) {
        std::vector<int64_t> plays;
        for (int s = 12; s < 70; s += 5)
            plays.push_back((int64_t)s * RATE);
        const std::vector<int64_t> calls = uneven(seed, 10, 60, 10, 3000);
        const Result without = run(calls, Ring{});
        const Result with = run(calls, Ring{}, true, plays, false);
        CHECK(with.jump_at == without.jump_at);
    }
}

namespace {

// `n` blocks of BLOCK frames with the same err; returns the first jump's delta (0: none) and where it came (block index)
struct Fed {
    int64_t by = 0;
    int at = -1;
};
Fed feed(TimelineFollow &f, int64_t err, int n, bool marker = true)
{
    Fed r;
    for (int i = 0; i < n && r.at < 0; i++)
        if (const TimelineFollow::Move m = f.update(err, BLOCK, marker); m.jump)
            r.by = m.delta, r.at = i;
    return r;
}

// REAPER's burst as the follower sees it: err falls by a block per call `blocks` times from `from`, then stays
Fed burst(TimelineFollow &f, int64_t from, int blocks)
{
    Fed r;
    for (int i = 0; i <= blocks + 2 && r.at < 0; i++)
        if (const TimelineFollow::Move m = f.update(from - (int64_t)(i < blocks ? i : blocks) * BLOCK, BLOCK, true); m.jump)
            r.by = m.delta, r.at = i;
    return r;
}

// a rough cadence: err swings +-800 around 0 (E337), 1 s of it
void rough_second(TimelineFollow &f)
{
    for (int i = 0; i < RATE / BLOCK; i++)
        f.update(i % 2 ? 800 : -800, BLOCK, false);
}

} // namespace

TEST(timeline_play_before_any_block_keeps_the_gate)
{
    // the first playing block can be the anchor block: no err seen yet, so the cadence is unknown - not calm (the
    // empty range read INT64_MIN - INT64_MAX, signed overflow)
    TimelineFollow f;
    f.set_window(RATE / 2);
    f.play_started();
    CHECK(burst(f, -600, 4).at < 0);
}

TEST(timeline_play_before_any_block_after_activation_jumps)
{
    TimelineFollow f;
    f.set_window(RATE / 2);
    f.activated();
    f.play_started();
    const Fed r = burst(f, -600, 4);
    CHECK(r.at >= 0 && r.at <= 5);
    CHECK(r.by == -600 - 4 * BLOCK);
}

TEST(timeline_anchor_forgets_the_old_cadence)
{
    // the range before a new anchor was measured against the old timeline: after the anchor only the new one counts
    TimelineFollow f;
    f.set_window(RATE / 2);
    rough_second(f);
    f.anchor();
    feed(f, 0, RATE / 4 / BLOCK); // a calm quarter second
    f.play_started();
    CHECK(burst(f, -600, 4).at >= 0);
}

TEST(timeline_jump_keeps_the_cadence)
{
    // after a jump (not an anchor) the range stays: a rough cadence before it still closes the gate
    TimelineFollow f;
    f.set_window(RATE / 2);
    rough_second(f);
    CHECK(feed(f, -5000, 1).at == 0); // far off: jumps at once
    f.play_started();
    CHECK(burst(f, -600, 4).at < 0);
}

TEST(timeline_activation_survives_the_anchor)
{
    // REAPER: setActive(1), then the Play block is an anchor block - the Play is still a fresh start (E351)
    TimelineFollow f;
    f.set_window(RATE / 2);
    rough_second(f);
    f.activated();
    f.anchor();
    f.play_started();
    CHECK(burst(f, -600, 4).at >= 0);
}

TEST(timeline_activation_counts_for_a_second)
{
    for (int ms : {200, 900, 1500}) {
        TimelineFollow f;
        f.set_window(RATE / 2);
        rough_second(f);
        f.activated();
        for (int i = 0; i < RATE * ms / 1000 / BLOCK; i++) // still rough until the Play
            f.update(i % 2 ? 800 : -800, BLOCK, false);
        f.play_started();
        CHECK((burst(f, -600, 4).at >= 0) == (ms < 1000));
    }
}

TEST(timeline_new_window_starts_the_windows_afresh)
{
    // a new Virus rate: frames counted at the old one do not complete the new window
    TimelineFollow f;
    f.set_window(RATE / 2);
    feed(f, 1000, RATE * 4 / 10 / BLOCK); // 0.4 s beyond JUMP
    f.set_window(48000 / 2);
    const Fed r = feed(f, 1000, RATE / BLOCK);
    CHECK(r.at >= 48000 / 2 / BLOCK - 1 && r.at <= 48000 / 2 / BLOCK + 1);
}

TEST(timeline_thresholds_are_exclusive)
{
    {
        TimelineFollow f;
        f.set_window(RATE / 2);
        CHECK(feed(f, -TimelineFollow::AT_ONCE, 1).at < 0);
        CHECK(feed(f, -TimelineFollow::AT_ONCE - 1, 1).at == 0);
    }
    {
        TimelineFollow f;
        f.set_window(RATE / 2);
        CHECK(feed(f, -TimelineFollow::JUMP, RATE / BLOCK).at < 0); // on the band's edge: steps, no jump
        CHECK(feed(f, TimelineFollow::JUMP, RATE / BLOCK).at < 0);
    }
}

TEST(timeline_play_burst_without_marker_waits_for_it)
{
    // no marker in the burst: no Play jump; the marker back later in the window: the burst is measured from there
    TimelineFollow f;
    f.set_window(RATE / 2);
    feed(f, 0, RATE / BLOCK);
    f.play_started();
    for (int i = 0; i < 4; i++)
        CHECK(!f.update(-600 - i * BLOCK, BLOCK, false).jump);
    const Fed r = burst(f, -600, 3);
    CHECK(r.at >= 0 && r.at <= 4);
    CHECK(r.by == -600 - 3 * BLOCK);
}

TEST(timeline_play_burst_that_never_ends_jumps_after_settle)
{
    // err falls a little every block, never stops: SETTLE frames (and 16 blocks) ahead at the latest, by the most-ahead
    // error
    TimelineFollow f;
    f.set_window(RATE / 2);
    feed(f, 0, RATE / BLOCK);
    f.play_started();
    Fed r;
    for (int i = 0; i < 20 && r.at < 0; i++)
        if (const TimelineFollow::Move m = f.update(-600 - i * 50, 2 * BLOCK, true); m.jump)
            r.by = m.delta, r.at = i;
    CHECK(r.at == 16 - 1); // 16 blocks of 512 > SETTLE (4096)
    CHECK(r.by == -600 - r.at * 50);
}

TEST(timeline_first_play_burst_at_block_128_jumps_once)
{
    // REAPER at block 128 (Linux, pw-jack, 48 kHz; user diag.log 2026-11-22, TI2 repro): the burst is the reported
    // latency (3712 = 29 blocks of 128) and nothing is learned yet. 16 blocks (2048 frames) into it the "never ends"
    // limit jumped by what it had seen; the rest of the burst waited for the half-second rule: 202 blocks read pulled
    // back (stutter on the first note) and a second jump. One jump when the burst is over, nothing ahead after it
    for (const int64_t b : {32, 64, 128}) {
        const int64_t burst = 3072 + b + 512; // the latency: A + block + margin (stamprule.h)
        TimelineFollow f;
        f.set_window(RATE / 2);
        int64_t ahead = 0, moved = 0, worst = 0;
        int jumps = 0;
        auto call = [&](int64_t grow) { // one block; in a burst the ring stands still, so each call is `grow` ahead
            ahead += grow;
            const TimelineFollow::Move m = f.update(-ahead - moved, b, true);
            moved += m.delta, jumps += m.jump;
            return -ahead - moved; // err after the move
        };
        for (int i = 0; i < RATE / b; i++)
            call(0);
        f.play_started(true);
        for (int64_t done = 0; done < burst; done += b)
            call(b);
        for (int i = 0; i < RATE / b; i++) {
            const int64_t e = call(0);
            worst = e < worst ? e : worst;
        }
        CHECK(jumps == 1);
        CHECK(worst > -TimelineFollow::JUMP); // no read past the newest frame after the burst (margin 512)
    }
}

// #37 (E571-E573): the burst at Play is at least the latency the plug-in reports (delay compensation rendered at once:
// REAPER ~1 x, Logic ~2 x), so after it the host stays that far ahead and a jump back made the burst's notes late
// (user, REAPER; rc2). A Play from a stop learns the burst; the next one moves the timeline back by it before the
// burst, so the burst's reads hit frames that are there and nothing jumps afterwards

namespace {
// the error from the Play's first second on, for 5 s (before the next Play): in sync (no read ahead, not behind)
bool in_sync_after(const Result &r, int64_t play)
{
    for (size_t i = 0; i < r.t.size(); i++)
        if (r.t[i] > play + RATE + 4 * BLOCK && r.t[i] < play + 6 * RATE && (r.err[i] < -400 || r.err[i] > TimelineFollow::JUMP))
            return false;
    return true;
}
int jumps_between(const Result &r, int64_t a, int64_t b, bool hard_only = false)
{
    int n = 0;
    for (int64_t t : hard_only ? r.hard_at : r.jump_at)
        n += t >= a && t < b;
    return n;
}
} // namespace

TEST(timeline_learned_burst_preshifts_the_next_play)
{
    const std::vector<int64_t> plays = {5 * RATE, 12 * RATE};
    const Result r = run(steady_with_plays(18, plays, 22), Ring{}, true, plays, false, true); // 22 x 256 = 5632
    CHECK(jumps_between(r, plays[0], plays[0] + RATE) >= 1); // the first Play: nothing learned yet, it jumps
    CHECK(r.shift_at.size() == 1);
    if (r.shift_at.size() == 1) {
        CHECK(r.shift_at[0] >= plays[1] && r.shift_at[0] < plays[1] + BLOCK);
        CHECK(r.shift_by[0] <= -22 * BLOCK + 441 && r.shift_by[0] >= -22 * BLOCK - 441);
    }
    CHECK(jumps_between(r, plays[1], plays[1] + 3 * RATE) == 0); // nothing jumps after the burst
    for (size_t i = 0; i < r.t.size(); i++)
        if (r.t[i] >= plays[1] && r.t[i] < plays[1] + RATE)
            CHECK(r.err[i] > -400); // no read past the newest frame in the burst
    CHECK(in_sync_after(r, plays[1]));
}

TEST(timeline_learned_burst_too_large_jumps_forward_and_learns_less)
{
    // the next burst is smaller (12 blocks instead of 22): the timeline is behind by the rest, a move forward fixes
    // it, and the third Play shifts by the new size plus the room for a burst that varies (at most a quarter)
    const std::vector<int64_t> plays = {5 * RATE, 12 * RATE, 19 * RATE};
    const Result r = run(steady_with_plays(25, plays, 0, {22, 12, 12}), Ring{}, true, plays, false, true);
    CHECK(r.shift_by.size() == 2);
    if (r.shift_by.size() == 2)
        CHECK(r.shift_by[1] <= -12 * BLOCK + 441 && r.shift_by[1] >= -12 * BLOCK * 5 / 4 - 441);
    CHECK(in_sync_after(r, plays[1]));
    CHECK(jumps_between(r, plays[2], plays[2] + 3 * RATE, true) == 0); // the room's rest is one crossfade
    CHECK(jumps_between(r, plays[2], plays[2] + 3 * RATE) <= 1);
}

TEST(timeline_learned_burst_too_small_jumps_the_rest_and_learns_more)
{
    const std::vector<int64_t> plays = {5 * RATE, 12 * RATE, 19 * RATE};
    const Result r = run(steady_with_plays(25, plays, 0, {10, 22, 22}), Ring{}, true, plays, false, true);
    CHECK(r.shift_by.size() == 2);
    if (r.shift_by.size() == 2)
        CHECK(r.shift_by[1] <= -22 * BLOCK + 441 && r.shift_by[1] >= -22 * BLOCK * 5 / 4 - 441);
    CHECK(in_sync_after(r, plays[1]));
    CHECK(jumps_between(r, plays[2], plays[2] + 3 * RATE, true) == 0); // the room's rest is one crossfade
    CHECK(jumps_between(r, plays[2], plays[2] + 3 * RATE) <= 1);
}

TEST(timeline_loop_restart_never_preshifts)
{
    // a loop restart or a jump while playing has no burst (the host keeps processing): no pre-shift
    const std::vector<int64_t> plays = {5 * RATE, 12 * RATE};
    const Result r = run(steady_with_plays(18, plays, 22), Ring{}, true, plays, false, false);
    CHECK(r.shift_at.empty());
}

namespace {
// the follower with its moves applied: the host `ahead` frames past real time, err = -ahead - what the timeline moved
struct Model {
    TimelineFollow f;
    int64_t ahead = 0, moved = 0;
    Model() { f.set_window(RATE / 2); }
    void blocks(int n, int64_t grow = 0) // n calls; in a burst the ring stands still, so each call is `grow` ahead
    {
        for (int i = 0; i < n; i++) {
            ahead += grow;
            moved += f.update(err(), BLOCK, true).delta;
        }
    }
    int64_t play()
    {
        const int64_t s = f.play_started(true);
        moved += s;
        return s;
    }
    int64_t err() const { return -ahead - moved; }
};
} // namespace

TEST(timeline_preshift_window_expects_the_offset)
{
    // process() re-anchors when abs(err) > 8192; while the burst eats a pre-shift (Logic: 11608) that offset is expected
    Model m;
    m.blocks(RATE / BLOCK);
    m.play();
    m.blocks(46, BLOCK); // 46 x 256 = 11776: the burst
    m.blocks(2 * RATE / BLOCK);
    CHECK(!m.f.expects(12000));
    CHECK(m.play() <= -11776 + 441);
    CHECK(m.f.expects(m.err()));
    CHECK(m.f.expects(-300));
    CHECK(!m.f.expects(11776 + 2 * TimelineFollow::JUMP));
    m.blocks(46, BLOCK);
    CHECK(m.err() > -400 && m.err() < TimelineFollow::JUMP); // the burst ate the shift
    TimelineFollow g;
    g.set_window(RATE / 2);
    CHECK(!g.expects(9000)); // nothing pre-shifted
}

TEST(timeline_loop_restart_after_a_learned_burst_never_preshifts)
{
    // a burst is learned, then the host restarts its loop (no burst): the learned shift is for a Play from a stop only
    Model m;
    m.blocks(RATE / BLOCK);
    m.play();
    m.blocks(22, BLOCK);
    m.blocks(2 * RATE / BLOCK);
    CHECK_EQ(m.f.play_started(false), 0);
    CHECK_EQ(m.f.play_started(), 0); // the default: not from a stop
}

TEST(timeline_anchor_keeps_the_learned_burst)
{
    Model m;
    m.blocks(RATE / BLOCK);
    m.play();
    m.blocks(22, BLOCK);
    m.blocks(2 * RATE / BLOCK);
    m.f.anchor(); // a re-anchor (resync ping) between two Plays
    m.blocks(RATE / BLOCK);
    CHECK(m.play() < -20 * BLOCK);
}

// #51 (E564): two clocks (48 kHz, 1 ms USB chunks, delivery jitter per block). process() reads the block ending at
// newest - margin - err with the timeline AFTER this block's move (virusplugin.cpp stamp-aligned read), so a block with
// err - delta < -margin is pulled back to avail - n: the click of #51.
namespace {

constexpr int64_t MARGIN = 512; // virusplugin.h timing_m()

struct Jitter { // deterministic, uniform +-amp
    uint64_t s;
    int64_t amp;
    int64_t next()
    {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return amp > 0 ? (int64_t)((s >> 33) % (uint64_t)(2 * amp + 1)) - amp : 0;
    }
};

struct Clocks {
    int rate = 48000, block = 256;
    double ppm = 0;          // < 0: the Virus slower than the sound card (the err falls: the side the margin guards)
    int64_t jitter = 300;    // the ring seen up to this many frames early or late per block (#51: err spread ~630)
    int64_t anchor_late = 0; // the anchor block sees the ring this much further than its average (an unlucky anchor)
    int64_t margin = 0;      // set_margin() if > 0
    int64_t room = 0;        // set_room() if > 0 (#37)
    uint64_t seed = 1;
};

struct Drift {
    int jumps = 0, steps = 0, pulls = 0; // pulls: blocks past -MARGIN after `from` (each one a click)
    int64_t err_min = INT64_MAX, err_max = INT64_MIN;
};

Drift two_clocks(const Clocks &c, int seconds, int from_seconds)
{
    TimelineFollow f;
    f.set_window(c.rate / 2);
    if (c.margin > 0)
        f.set_margin(c.margin);
    if (c.room > 0)
        f.set_room(c.room);
    Jitter j{c.seed, c.jitter};
    const int chunk = c.rate / 1000;
    auto newest = [&](int64_t t) { return ((int64_t)((double)t * (1.0 + c.ppm * 1e-6)) + j.next()) / chunk * chunk; };
    Drift d;
    int64_t tl = c.anchor_late ? c.anchor_late : newest(0); // anchored at the first block
    const int64_t m_limit = c.margin > 0 ? c.margin : MARGIN;
    for (int64_t k = 0; k < (int64_t)seconds * c.rate / c.block; k++) {
        const int64_t t = k * c.block, err = newest(t) - tl;
        const TimelineFollow::Move m = f.update(err, c.block, true);
        if (m.jump)
            d.jumps++;
        else if (m.delta != 0)
            d.steps++;
        tl += m.delta;
        const int64_t read = err - m.delta;
        if (t >= (int64_t)from_seconds * c.rate) {
            d.err_min = read < d.err_min ? read : d.err_min, d.err_max = read > d.err_max ? read : d.err_max;
            if (read < -m_limit)
                d.pulls++;
        }
        tl += c.block;
    }
    return d;
}

} // namespace

TEST(timeline_two_clocks_stay_inside_the_read_margin)
{
    // #51: Apollo ASIO 48 kHz / 256 and the Virus ~90 ppm apart, Windows delivery jitter. Before: the 2 s mean parked
    // at the band's edge (-256..-265) and the jitter took err to ~-590: thousands of pulled-back blocks in 30 min
    for (double ppm : {-90.0, -200.0, 90.0})
        for (uint64_t seed = 1; seed <= 3; seed++) {
            Clocks c;
            c.ppm = ppm, c.seed = seed;
            const Drift d = two_clocks(c, 30 * 60, 4);
            CHECK(d.jumps == 0);
            CHECK_EQ(d.pulls, 0);
            CHECK(d.err_min > -MARGIN);
            CHECK(d.err_max < 2 * MARGIN); // the other side only costs stamp lead (A = 3072): bounded, no jump
            // the steps are what the drift needs (|ppm| * 48 frames a second), not hunting on the jitter
            const int64_t need = (int64_t)(std::abs(ppm) * 1e-6 * c.rate * 30 * 60);
            CHECK(d.steps < need + 1000);
        }
}

TEST(timeline_two_clocks_large_blocks_stay_inside_the_read_margin)
{
    // the same at block 2048 (one step per block = 23 frames a second: the first lift takes longer) and at 44.1 kHz
    for (int block : {32, 2048})
        for (int rate : {44100, 48000}) {
            Clocks c;
            c.block = block, c.rate = rate, c.ppm = -90;
            const Drift d = two_clocks(c, 30 * 60, block >= 2048 ? 30 : 4);
            CHECK(d.jumps == 0);
            CHECK_EQ(d.pulls, 0);
        }
}

TEST(timeline_late_anchor_block_leaves_room_below)
{
    // one clock (no drift), but the anchor block saw the ring 200 frames further than its average: the mean sits at
    // about -224, inside the +-256 band, so before nothing moved it and the jitter's low end (~-548) was pulled back for good
    Clocks c;
    c.anchor_late = 200;
    const Drift d = two_clocks(c, 10 * 60, 4);
    CHECK(d.jumps == 0);
    CHECK_EQ(d.pulls, 0);
    CHECK(d.steps < 600); // one lift, then quiet
    // a wider margin (VTI_NATIVE_MARGIN=1024, the #51 workaround) has room for it: nothing moves
    c.margin = 1024;
    const Drift w = two_clocks(c, 10 * 60, 4);
    CHECK_EQ(w.pulls, 0);
    CHECK_EQ(w.steps, 0);
}

namespace {

// REAPER on PipeWire at 48 kHz (user diag.log 2026-11-22; our TI2 under nativehost the same): the ring seen in 512-frame
// steps, so err cycles -96..352 at blocks of 64 and 128 (mean 128 = HOLD) and nothing moves
struct Hiccup {
    int block = 64;
    int64_t dip = 0, dip_blocks = 0; // at 10 s the ring is seen `dip` frames late for N blocks (a late USB delivery)
    int64_t xrun = 0;                // at 10 s the host drops this many blocks (REAPER xrun): the ring runs on, we do not
    int64_t at_ms = 10000;           // when the dip or the xrun comes (10 s: where a 2 s mean window starts)
    int64_t lost = 0;                // an xrun of any size: from then on the ring is this many frames further
    int block_after = 0;             // from at_ms on this block size (0: as before; Logic's playback -> live mode)
};

struct Moves {
    int jumps = 0, fwd = 0, back = 0, fades = 0; // jumps: hard ones only
    int64_t faded = 0;                           // the crossfaded catch-ups' frames
};

Moves pipewire(const Hiccup &c, int seconds = 60)
{
    constexpr int64_t rate = 48000, chunk = 512;
    const int64_t at = c.at_ms * rate / 1000;
    TimelineFollow f;
    f.set_window(rate / 2);
    f.set_margin(MARGIN);
    Moves r;
    // err = newest - tl: -96..352; a block of whole chunks sees a steady 128 (HOLD)
    int64_t tl = c.block < chunk ? 96 - chunk + c.block : -128;
    int block = c.block;
    for (int64_t t = 0; t < seconds * rate; t += block) {
        if (c.block_after && t >= at)
            block = c.block_after;
        if (t >= at && t < at + c.xrun * c.block)
            continue; // a dropped host cycle: process() is not called
        const int64_t newest = t / chunk * chunk - (t >= at && t < at + c.dip_blocks * c.block ? c.dip : 0) +
                               (t >= at ? c.lost : 0);
        const TimelineFollow::Move m = f.update(newest - tl, block, true);
        if (m.fade)
            r.fades++, r.faded += m.delta;
        else if (m.jump)
            r.jumps++;
        else if (m.delta > 0)
            r.fwd++;
        else if (m.delta < 0)
            r.back++;
        tl += m.delta + block;
    }
    return r;
}

} // namespace

TEST(timeline_one_late_delivery_does_not_lift_and_step_back)
{
    // one late USB delivery under a steady cadence: the window's low end passed 3/4 of the margin, the #51 guard lifted
    // the band (one-frame steps back) - so far that the lifted mean was past STEP, and the next window stepped it
    // down again: out and back, 280-384 one-frame skips for one hiccup that needed nothing
    for (const Hiccup &c : {Hiccup{64, 300, 20}, Hiccup{256, 400, 4}, Hiccup{256, 300, 1}}) {
        const Moves m = pipewire(c);
        CHECK_EQ(m.jumps, 0);
        CHECK_EQ(m.fwd + m.back, 0);
    }
}

TEST(timeline_skewed_cadence_does_not_hunt)
{
    // err mostly at +200 and every 10th block at -400 (a periodic late delivery): the low end is real (10 % of the
    // blocks) but no position fits both it and the band - lifted to -256 the mean was 284, past STEP, so each window
    // lifted it and the next stepped it down: ~4500 one-frame skips a minute (fd33fc4). The low end needs nothing: -400
    // is inside the margin
    TimelineFollow f;
    f.set_window(48000 / 2);
    f.set_margin(MARGIN);
    int steps = 0, jumps = 0;
    int64_t off = 0;
    for (int64_t k = 0; k < 60LL * 48000 / 128; k++) {
        const TimelineFollow::Move m = f.update((k % 10 ? 200 : -400) + off, 128, true);
        jumps += m.jump, steps += !m.jump && m.delta != 0;
        off -= m.delta;
    }
    CHECK_EQ(jumps, 0);
    CHECK_EQ(steps, 0);
}

TEST(timeline_xrun_catches_up_in_one_crossfade)
{
    // REAPER on Linux drops host cycles (an xrun): the ring runs on, the timeline does not - 256-384 frames behind for
    // good. Paid back in one-frame steps that was one skip per block for a second (~375 a second at block 128: a short
    // metallic sound); now one crossfaded jump by the offset, no steps
    // anywhere in the 2 s mean window: the catch-up is the offset of the window's last half second, not its diluted
    // mean (on the TI2 a fade of 380 for a 513-frame xrun left 173 one-frame steps for the next window)
    for (int64_t xrun : {2, 3})
        for (int block : {64, 128})
            for (int64_t at_ms : {10000, 10600, 11000, 11400}) {
                Hiccup c;
                c.block = block, c.xrun = xrun * 128 / block, c.at_ms = at_ms;
                const Moves m = pipewire(c);
                CHECK_EQ(m.jumps, 0);
                CHECK_EQ(m.fades, 1);
                CHECK(m.faded >= xrun * 128 - 64 && m.faded <= xrun * 128 + 64);
                CHECK_EQ(m.fwd + m.back, 0);
            }
}

TEST(timeline_large_xrun_catches_up_in_one_crossfade)
{
    // the user's case is a loss of 4 blocks of 128 = 512 frames: on the TI2 one of 513 passed JUMP and took the hard
    // jump of a lasting offset (a click). Those offset jumps (half a second beyond JUMP, the 2 s mean beyond it) are
    // crossfaded too; the read path falls back to the hard jump only where the old frames are not in the ring
    for (int64_t lost : {512, 513, 700, 1024, 2048})
        for (int64_t at_ms : {10000, 10600, 11000, 11400}) {
            Hiccup c;
            c.block = 128, c.lost = lost, c.at_ms = at_ms;
            const Moves m = pipewire(c);
            CHECK_EQ(m.jumps, 0);
            CHECK_EQ(m.fades, 1);
            CHECK(m.faded >= lost - 64 && m.faded <= lost + 192);
            CHECK_EQ(m.fwd + m.back, 0);
        }
}

TEST(timeline_logic_lost_frames_one_crossfade_at_any_phase)
{
    // #37 rc6 (Logic on Catalina, 44.1 kHz, diag.log 2026-11-22 22:28): crossfaded catch-ups while playing. At a steady
    // block of 512 (18:54:52-18:56:10 UTC) they pay frames Logic did not process (host frames 436736 / Virus frames
    // 441441, 439808 / 441000 in their 10 s windows; the plug-in sees no xrun report), so a catch-up is needed - but
    // "caught up by 1461 frames (crossfaded), err 2373" and 1108 a second later fit one loss of ~2300 paid twice: a 2 s
    // mean window that ends shortly after the loss fades by its last half second's mean, diluted by the blocks before
    // the loss; the rest is a second fade (beyond JUMP) or one-frame steps. Logic's switch from its playback mode
    // (block 2048) to the live one (128) likely drops its read-ahead the same way (a fade of 1339 right after it in
    // the log). One loss, one crossfade: at every phase of the mean window
    struct Case {
        int block, block_after;
        int64_t lost;
    };
    for (const Case &k : {Case{512, 0, 2300}, Case{512, 0, 1024}, Case{128, 0, 1400}, Case{2048, 128, 1400},
                          Case{2048, 512, 1400}})
        for (int64_t at_ms = 10000; at_ms < 12000; at_ms += 50) {
            Hiccup c;
            c.block = k.block, c.block_after = k.block_after, c.lost = k.lost, c.at_ms = at_ms;
            const Moves m = pipewire(c);
            CHECK_EQ(m.jumps, 0);
            CHECK_EQ(m.fades, 1);
            CHECK(m.faded >= k.lost - 192 && m.faded <= k.lost + 192);
            CHECK_EQ(m.fwd + m.back, 0);
        }
}

TEST(timeline_drift_never_crossfades)
{
    // clock drift up to 500 ppm passes STEP by at most ~48 frames a 2 s window: its owed amount stays below the fade
    // threshold, so it keeps its sparse one-frame steps (each rendered as a one-frame fade since E589: Move::fade
    // without Move::jump) and never becomes a crossfaded jump
    for (double ppm : {-500.0, -200.0, 200.0, 500.0}) {
        Clocks c;
        c.ppm = ppm, c.jitter = 0;
        TimelineFollow f;
        f.set_window(c.rate / 2);
        int64_t tl = 0;
        int fades = 0;
        for (int64_t k = 0; k < 120LL * c.rate / c.block; k++) {
            const int64_t t = k * c.block, newest = (int64_t)((double)t * (1.0 + ppm * 1e-6)) / 48 * 48;
            const TimelineFollow::Move m = f.update(newest - tl, c.block, true);
            fades += m.jump && m.fade;
            tl += m.delta + c.block;
        }
        CHECK_EQ(fades, 0);
    }
}

// REAPER's Play, start and block-size switch (user diag.log 2026-11-22, rc5): each comes with setActive(1) and a burst
// of about the latency (the ring stands still while the host renders it, then the host stays that far ahead). The E579
// catch-up fade is for a host xrun in steady playback only: inside or after a burst every move stays as in fd33fc4
// (hard jumps, no catch-up jump), and a burst seen before a Play is not taken for the cadence's low when it learns.
namespace {

struct Burst {
    double at;          // s
    int64_t frames;     // the burst (0: none)
    bool play;          // a Play from a stop in the burst's first block (else only setActive(1) and the burst)
    int block = 0;      // from here on this block size (0: as before)
};

struct Reaper {
    struct Jump {
        double at;
        int64_t by;
        bool fade;
    };
    std::vector<Jump> jumps; // hard and faded
    std::vector<int64_t> shifts; // the pre-shift of each Play (0: nothing learned)
    int steps = 0, pulled = 0;   // pulled: blocks read past the newest frame (err after the move < -margin)
    double last_pulled = -1;     // s
    std::vector<double> step_at, pulled_at; // s
    int fades() const
    {
        int n = 0;
        for (const Jump &j : jumps)
            n += j.fade;
        return n;
    }
    std::vector<int64_t> hard_before(double t) const
    {
        std::vector<int64_t> v;
        for (const Jump &j : jumps)
            if (j.at < t && !j.fade)
                v.push_back(j.by);
        return v;
    }
};

// 48 kHz, the ring seen in 512-frame steps (PipeWire), margin 512; a jump of `lost` frames forward at `lost_at` s
Reaper reaper(const std::vector<Burst> &bursts, int block, double seconds, int64_t lost = 0, double lost_at = -1)
{
    constexpr int64_t rate = 48000, chunk = 512;
    TimelineFollow f;
    f.set_window(rate / 2);
    f.set_margin(MARGIN);
    Reaper r;
    int64_t tl = 96 - chunk + block, gone = 0;
    size_t p = 0;
    for (int64_t t = 0; t < (int64_t)(seconds * rate); t += block) {
        int64_t burst = 0;
        bool play = false;
        if (p < bursts.size() && t >= (int64_t)(bursts[p].at * rate)) {
            f.activated();
            burst = bursts[p].frames, play = bursts[p].play;
            block = bursts[p].block ? bursts[p].block : block;
            p++;
        }
        if (lost_at >= 0 && t >= (int64_t)(lost_at * rate))
            gone = lost; // a host xrun: the ring ran on
        for (int64_t k = 0, calls = 1 + (burst + block - 1) / block; k < calls; k++) {
            const int64_t err = t / chunk * chunk + gone - tl;
            const TimelineFollow::Move m = f.update(err, block, true);
            if (m.jump)
                r.jumps.push_back({(double)t / rate, m.delta, m.fade});
            else if (m.delta != 0)
                r.steps++, r.step_at.push_back((double)t / rate);
            if (err - m.delta < -MARGIN)
                r.pulled++, r.last_pulled = (double)t / rate, r.pulled_at.push_back((double)t / rate);
            tl += m.delta;
            if (k == 0 && play) {
                const int64_t s = f.play_started(true);
                r.shifts.push_back(s);
                tl += s;
            }
            tl += block;
        }
    }
    return r;
}

int64_t latency(int block) { return 3072 + block + 512; } // stamprule.h: A + block + margin

} // namespace

TEST(timeline_reaper_plays_never_crossfade)
{
    // Plays from a stop, each after setActive(1) with its burst, at every phase of the 2 s mean window: as in fd33fc4
    // the first one jumps once inside the burst, the next ones are pre-shifted; no fade
    for (const int block : {128, 256})
        for (double ph = 0; ph < 2; ph += 0.25) {
            const int64_t L = latency(block);
            const Reaper r = reaper({{10 + ph, L, true}, {17 + ph, L, true}, {24 + ph, L, true}, {31 + ph, L, true}}, block, 40);
            CHECK_EQ(r.fades(), 0);
            CHECK_EQ((int)r.jumps.size(), 1);
            CHECK_EQ(r.pulled, block == 128 ? 26 : 14); // fd33fc4: the first Play's burst only
            CHECK_EQ(r.steps, 0);
        }
}

TEST(timeline_reaper_start_burst_before_play_jumps_hard)
{
    // REAPER starting the stream at Play (rc5 log: a setActive cycle with a burst, then the Play's own one): the first
    // burst has no Play window and waits for the half-second rule, the Play's jumps inside its own. The same hard
    // jumps as fd33fc4 (rc5 crossfaded the first one, cut to a hard jump in the read path), no fade
    struct Case {
        int block;
        double gap;
        std::vector<int64_t> moves; // fd33fc4's jumps before the second Play
    };
    for (const Case &c : {Case{128, 0.3, {-4192, -2999}}, Case{256, 0.3, {-4192, -3292}}, Case{128, 1.5, {-3419, -4101}},
                          Case{256, 1.5, {-3579, -4197}}, Case{128, 0.6, {-3419, -4101}}, Case{256, 0.6, {-3579, -4197}}}) {
        const int64_t L = latency(c.block);
        const Reaper r = reaper({{10, L, false}, {10 + c.gap, L, true}, {20, L, true}, {30, L, true}}, c.block, 40);
        CHECK_EQ(r.fades(), 0);
        CHECK(r.hard_before(20) == c.moves);
    }
}

TEST(timeline_burst_before_play_does_not_spoil_the_learned_burst)
{
    // rc5 log: a block switch while playing (setActive cycles with bursts), then a Play: it learned 765 frames, not the
    // ~3700 of the latency, and the next Play's burst read 395 blocks past the newest frame. The Play took the error's
    // low of the last second as the cadence's normal low - that was the burst before it. Only a calm cadence's low is
    // the base, else the low since the last jump; the next Play is pre-shifted by the burst and nothing moves after it
    for (const int block : {128, 256})
        for (double gap = 0.3; gap < 1.0; gap += 0.15) {
            const int64_t L = latency(block);
            const Reaper r = reaper({{10, L, true}, {20, L, false}, {20 + gap, L, true}, {30, L, true}}, block, 36);
            CHECK_EQ((int)r.shifts.size(), 3);
            if (r.shifts.size() == 3)
                CHECK(r.shifts[2] <= -L + 128 && r.shifts[2] >= -L - 128);
            CHECK(r.last_pulled < 30); // the last Play's burst reads frames that are there
            CHECK_EQ(r.steps, 0);      // and no rest is left behind to step off (a shift too large)
            CHECK_EQ(r.fades(), 0);
        }
}

TEST(timeline_preshift_rest_after_play_is_one_crossfade)
{
    // rc5 log: a Play pre-shifted by 3616, its burst ~290 smaller: the timeline then sat ~290 behind and 4 s later a
    // window end crossfaded the rest (287); E581 made it one-frame steps again (167 in the user's REAPER test, can
    // sound metallic; at Logic's ~1200 a hard jump, #37). Without a room (set_room not called: a lead that leaves
    // none, e.g. the experimental low lead) the rest is one crossfade when the learn second ends, at every phase of
    // the mean window: no steps, no hard jump after the first Play's. With the room the rest stays instead
    // (timeline_logic_rest_inside_the_lead_room_is_kept)
    for (double ph = 0; ph < 2; ph += 0.25) {
        const int64_t L = latency(128);
        const Reaper r = reaper({{10, L, true}, {17 + ph, L - 300, true}}, 128, 30);
        CHECK_EQ(r.fades(), 1);
        CHECK_EQ(r.steps, 0);
        CHECK_EQ((int)r.hard_before(30).size(), 1);
    }
}

TEST(timeline_xrun_after_a_play_still_crossfades)
{
    // the steady playback after a Play: a host xrun (512 lost) more than 4 s after it is crossfaded as before (E579)
    for (const int block : {128, 256}) {
        const int64_t L = latency(block);
        const Reaper r = reaper({{10, L, true}, {17, L, true}}, block, 30, 512, 23);
        CHECK_EQ(r.fades(), 1);
        CHECK_EQ(r.steps, 0);
    }
}

TEST(timeline_xrun_inside_the_quiet_time_is_crossfaded)
{
    // an xrun within QUIET after a Play (4 s): E581 made it a hard jump, because rc5's fades there were flagged burst
    // jumps the read path cut anyway. But a move forward skips audio that is in the ring, so it can always be
    // crossfaded, and a hard one is a click for nothing (#37 rc7: one after every Play). QUIET now only keeps the
    // window-end catch-up from firing on a burst's rest. 1500 lost: beyond JUMP for half a second, inside QUIET
    const int64_t L = latency(128);
    const Reaper r = reaper({{10, L, true}, {17, L, true}}, 128, 30, 1500, 18.2);
    CHECK_EQ(r.fades(), 1);
    int late = 0, hard_forward = 0;
    for (const Reaper::Jump &j : r.jumps) {
        late += j.at >= 18.2 && j.at < 20 && j.fade && j.by > 1300;
        hard_forward += j.by > 0 && !j.fade;
    }
    CHECK_EQ(late, 1);
    CHECK_EQ(hard_forward, 0);
}

TEST(timeline_logic_varying_bursts_no_click_after_play)
{
    // #37 (rc6, Logic on Catalina, diag.log 2026-11-22 18:56-18:58 UTC, block 2048): Logic's burst differs by up to
    // ~1200 frames from Play to Play (learned 10779, 11847, 11847, 10642). A burst larger than the pre-shift read past
    // the newest frame (1 block pulled back by 343-1097) and jumped back hard; a smaller one jumped forward hard or
    // was stepped off frame by frame (17-45 steps per Play) - right on the first notes, the reported crackle. Once
    // two Plays showed the spread, the pre-shift keeps that much room, and the rest after the burst is one crossfade.
    // This is the path without set_room (a lead that leaves no room). At the default lead the rest stays and no
    // crossfade is needed: rc7 still crackled with this rule (timeline_logic_rest_inside_the_lead_room_is_kept)
    for (const int block : {512, 2048}) {
        const std::vector<int64_t> b = {11847, 10779, 11847, 11847, 10642, 11847, 10779}; // the log's Plays
        std::vector<Burst> bursts;
        for (size_t i = 0; i < b.size(); i++)
            bursts.push_back({10 + 7.0 * (double)i, b[i], true});
        const Reaper r = reaper(bursts, block, 10 + 7.0 * (double)b.size());
        const double from = 10 + 7.0 * 2; // from the third Play on the spread is known
        int late_pulled = 0, late_steps = 0, late_hard = 0, late_fades = 0;
        for (double t : r.pulled_at)
            late_pulled += t >= from;
        for (double t : r.step_at)
            late_steps += t >= from;
        for (const Reaper::Jump &j : r.jumps)
            if (j.at >= from)
                (j.fade ? late_fades : late_hard)++;
        CHECK_EQ(late_pulled, 0);
        CHECK_EQ(late_hard, 0);
        CHECK_EQ(late_steps, 0);
        CHECK(late_fades <= 5); // at most one per Play
    }
}

// #37 rc7 (Logic on Catalina, 44.1 kHz, diag.log 2026-11-24, build c7e07331): every pre-shifted Play was followed by a
// crossfade about a second later ("caught up by 374..2067 frames"), and err at that block was still 250-1960 above the
// fade, so a second move followed: a hard jump forward ("anchors 1" in the windows with one Play) or 138-174 one-frame
// steps - all on the first notes. With the marker none of it is needed: an event at timeline t sounds at ring frame
// t + lead and is read latency later, so a timeline that is BEHIND the ring by a rest plays every stamped event
// exactly. The rest only uses stamp lead, and a stamp is floored only beyond A - floor_min (stamprule.h StampFloor).
// So a rest inside that room stays (set_room), and what has to move forward is always crossfaded.
namespace {

struct LogicPlay {
    int64_t burst; // rendered at once at Play: the lowest err of the learn second
    int64_t idle;  // then the host waits this long and stays burst - idle ahead (the log: err at the fade - the fade)
};

struct Logic : Reaper {
    std::vector<double> play_at;  // s
    std::vector<int64_t> rest_hi; // per Play: the highest err of its last two seconds (the rest that stays)
    int late(double from, bool fades, bool hard) const // jumps from `from` on
    {
        int n = 0;
        for (const Jump &j : jumps)
            n += j.at >= from && (j.fade ? fades : hard);
        return n;
    }
    int steps_from(double from) const
    {
        int n = 0;
        for (double t : step_at)
            n += t >= from;
        return n;
    }
    int pulled_from(double from) const
    {
        int n = 0;
        for (double t : pulled_at)
            n += t >= from;
        return n;
    }
};

// Logic's Plays from a stop, one every 9 s from 10 s on: 44.1 kHz, the ring seen in 441-frame steps (err swings about
// +-220, the log's band at block 512). `room` 0: set_room is not called. `lost` frames at `lost_at` s: a host xrun
Logic logic(const std::vector<LogicPlay> &plays, int block, int64_t room, int64_t lost = 0, double lost_at = -1)
{
    constexpr int64_t chunk = 441;
    TimelineFollow f;
    f.set_window(RATE / 2);
    f.set_margin(MARGIN);
    if (room > 0)
        f.set_room(room);
    Logic r;
    int64_t tl = -220, t = 0;
    auto call = [&](bool starts) {
        const int64_t newest = t / chunk * chunk + (lost_at >= 0 && t >= (int64_t)(lost_at * RATE) ? lost : 0);
        const TimelineFollow::Move m = f.update(newest - tl, block, true);
        if (m.jump)
            r.jumps.push_back({(double)t / RATE, m.delta, m.fade});
        else if (m.delta != 0)
            r.steps++, r.step_at.push_back((double)t / RATE);
        tl += m.delta;
        if (starts) {
            const int64_t s = f.play_started(true);
            r.shifts.push_back(s);
            tl += s;
        }
        if (newest - tl < -MARGIN) // this block reads past the newest frame
            r.pulled++, r.pulled_at.push_back((double)t / RATE);
        if (!r.rest_hi.empty() && t >= (int64_t)((r.play_at.back() + 7) * RATE) && newest - tl > r.rest_hi.back())
            r.rest_hi.back() = newest - tl;
        tl += block;
    };
    for (size_t k = 0; k <= plays.size(); k++) {
        for (const int64_t until = (int64_t)(10 + 9 * k) * RATE; t < until; t += block)
            call(false);
        if (k == plays.size())
            break;
        r.play_at.push_back((double)t / RATE), r.rest_hi.push_back(INT64_MIN);
        const int64_t t0 = t, n = plays[k].burst / block + 1; // whole blocks at once: the ring does not move
        for (int64_t j = 0; j < n; j++)
            call(j == 0);
        t = t0 + n * block - plays[k].burst; // the burst's last block: the host is `burst` ahead here
        call(false);
        t = t0 + (n + 1) * block - (plays[k].burst - plays[k].idle); // then it idles and stays burst - idle ahead
    }
    return r;
}

// The rc7 log's Plays. Block 512 (session from 1795473943): the bursts follow from the logged shifts and fades (each
// shift is the burst before it plus its room: 5441 + 886 = 6327, 5867 + 1466 = 7333), the idle is err at the fade
// block minus the fade where both are logged (633, 1606, 920, 252, 920, 698, 723, 1222); 600, 362 and 400 are
// assumed (Plays without a fade line)
const std::vector<LogicPlay> RC7_512 = {{5755, 600}, {5815, 362}, {5441, 633}, {4260, 1606}, {5680, 400}, {5924, 920},
                                        {5516, 252}, {5512, 920}, {5781, 698}, {5867, 723}, {5912, 1222}};
// Block 2048 (session from 1795473710): 11443 / 236, 10524 / 1961 and 11346 / 520 the same way; 11787 and 11817 are the
// shifts of the two Plays without a fade, and the last four are rc6's learned bursts (E582); their idles are assumed
const std::vector<LogicPlay> RC7_2048 = {{11787, 500}, {11817, 500}, {11443, 236}, {10524, 1961}, {11346, 520},
                                         {10779, 600}, {11847, 600}, {11847, 600}, {10642, 600}};

} // namespace

TEST(timeline_logic_rest_inside_the_lead_room_is_kept)
{
    // from the third Play on (the first learns, the second has one burst to go by): no hard jump, no step, no read
    // past the newest frame. At 512 one Play may fade (the one whose burst was 4260 after 5441-5815 and whose idle of
    // 1606 left more than the room), at 2048 three (bursts 10524-11847: the spread plus the idle passes the room)
    for (const int block : {512, 2048}) {
        const Logic r = logic(block == 512 ? RC7_512 : RC7_2048, block, ROOM);
        const double from = r.play_at[2] - 0.5;
        CHECK_EQ(r.late(from, false, true), 0);
        CHECK_EQ(r.steps_from(from), 0);
        CHECK_EQ(r.pulled_from(from), 0);
        CHECK(r.late(from, true, false) <= (block == 512 ? 1 : 3));
        for (size_t k = 2; k < r.rest_hi.size(); k++)
            CHECK(r.rest_hi[k] <= ROOM); // and what stays floors no stamp
    }
}

TEST(timeline_kept_rest_does_not_add_up)
{
    // ten equal Plays: the pre-shift counts from where the timeline already is (the rest kept from the Play before),
    // else each Play put its ~700 on top and the fourth passed the room. Nothing moves at all, and the tenth Play's
    // rest is the third's (a ring step of slack)
    for (const int block : {512, 2048}) {
        const Logic r = logic(std::vector<LogicPlay>(10, {block == 512 ? 5600 : 11400, 600}), block, ROOM);
        const double from = r.play_at[2] - 0.5;
        CHECK_EQ(r.late(from, true, true), 0);
        CHECK_EQ(r.steps_from(from), 0);
        CHECK_EQ(r.pulled_from(from), 0);
        for (size_t k = 2; k < r.rest_hi.size(); k++)
            CHECK(r.rest_hi[k] <= ROOM);
        CHECK(r.rest_hi[9] <= r.rest_hi[2] + 441);
    }
}

TEST(timeline_xrun_on_a_kept_rest_pays_once_crossfaded)
{
    // a host xrun while a rest is kept (Logic lost ~2290 frames, E584): rest + loss pass the room, one crossfade pays
    // it - 5.5 s after the Play and 2 s after it (inside QUIET) alike: no hard jump, no steps, nothing floored after
    for (const double after : {5.5, 2.0}) {
        const Logic r = logic(std::vector<LogicPlay>(4, {5600, 600}), 512, ROOM, 2290, 28 + after);
        const double from = 28 + after;
        CHECK_EQ(r.late(from, true, false), 1);
        CHECK_EQ(r.late(from, false, true), 0);
        CHECK_EQ(r.steps_from(from), 0);
        CHECK_EQ(r.pulled_from(from), 0);
        CHECK(r.rest_hi[2] <= ROOM && r.rest_hi[3] <= ROOM);
    }
    // a loss that still fits the room with the rest (600 on ~700): nothing moves, the lead covers it
    const Logic r = logic(std::vector<LogicPlay>(4, {5600, 600}), 512, ROOM, 600, 33.5);
    CHECK_EQ(r.late(r.play_at[2] - 0.5, true, true), 0);
    CHECK_EQ(r.steps_from(r.play_at[2] - 0.5), 0);
    CHECK(r.rest_hi[2] <= ROOM && r.rest_hi[3] <= ROOM);
}

TEST(timeline_rest_inside_the_room_floors_no_stamp)
{
    // what the room rests on (stamprule.h StampFloor, virusplugin.cpp pack): a block at timeline tl has
    // err = newest - S - tl, and its event is due at ring frame tl + S + A. Up to err = A - floor_min the safety net
    // leaves the stamp alone (the note sounds on its stamp), one frame more and it moves
    const int64_t S = 5000, tl = 1000000;
    for (const int64_t err : {(int64_t)0, (int64_t)700, ROOM}) {
        StampFloor net;
        CHECK_EQ(net.apply(tl + S + LEAD_A, tl + S + err, FLOOR_MIN), tl + S + LEAD_A);
        CHECK_EQ(net.moved, 0);
    }
    StampFloor net;
    CHECK_EQ(net.apply(tl + S + LEAD_A, tl + S + ROOM + 1, FLOOR_MIN), tl + S + LEAD_A + 1);
    CHECK_EQ(net.moved, 1);
}

TEST(timeline_forward_move_is_always_faded)
{
    // a move forward skips audio that is in the ring, so the read path can always crossfade it (crossfade.h); a hard
    // one is a click for nothing. Without a room too. rc7 jumped hard in three places (the third, an xrun inside
    // QUIET, is timeline_xrun_inside_the_quiet_time_is_crossfaded):
    // (1) after a Play's fade, the rest the fade left (the log's "anchors 1" per Play; 4 and 3 times in this model)
    for (const int block : {512, 2048}) {
        const Logic r = logic(block == 512 ? RC7_512 : RC7_2048, block, 0);
        int hard_forward = 0;
        for (const Reaper::Jump &j : r.jumps)
            hard_forward += j.at >= r.play_at[2] - 0.5 && j.by > 0 && !j.fade;
        CHECK_EQ(hard_forward, 0);
    }
    // (2) a stall beyond AT_ONCE in steady playback (6000 frames): at once as before, but crossfaded
    {
        const Logic r = logic({}, 512, 0, 6000, 5);
        CHECK_EQ((int)r.jumps.size(), 1);
        CHECK(r.jumps.size() == 1 && r.jumps[0].fade && r.jumps[0].by > 5000);
    }
    // a move back stays hard: its old position is past the newest frame, there is nothing to fade from
    {
        const Logic r = logic({{5600, 600}}, 512, 0);
        int back_faded = 0;
        for (const Reaper::Jump &j : r.jumps)
            back_faded += j.by < 0 && j.fade;
        CHECK_EQ(back_faded, 0);
    }
}

TEST(timeline_lasting_offset_inside_the_room_is_absorbed)
{
    // REAPER after a render (E279), ~1000 frames behind for good: with the room nothing moves - the stamped notes
    // are exact either way, the offset only uses lead. Beyond the room it is paid in one crossfade, all of it (the
    // whole lead is back), and a host AHEAD of the ring jumps back as before: those frames are not there yet
    Ring ring;
    ring.offset_at = 10 * RATE, ring.offset = 1000;
    const Result kept = run(steady(20), ring, true, {}, false, false, ROOM);
    CHECK_EQ(kept.jumps, 0);
    CHECK_EQ(kept.steps, 0);
    for (size_t i = 0; i < kept.t.size(); i++)
        if (kept.t[i] > 12 * RATE)
            CHECK(kept.err[i] > 550 && kept.err[i] < 1100); // the offset on the model's band (-390..50)
    ring.offset = 2500;
    const Result paid = run(steady(20), ring, true, {}, false, false, ROOM);
    CHECK_EQ(paid.jumps, 1);
    CHECK_EQ((int)paid.hard_at.size(), 0);
    if (paid.jumps == 1)
        CHECK(paid.jump_by[0] >= 2200 && paid.jump_by[0] <= 2600);
    CHECK(std::llabs(paid.err.back()) < 450);
    ring.offset = -1000;
    const Result back = run(steady(20), ring, true, {}, false, false, ROOM);
    CHECK_EQ(back.jumps, 1);
    CHECK_EQ((int)back.hard_at.size(), 1);
    CHECK(std::llabs(back.err.back()) < 450);
}

// Guards: the room is on in the plug-in at the default lead, so the older rules have to hold with it. Where no jump
// is asked for, the room changes nothing at all: the same steps at the same blocks
namespace {
bool same(const Result &a, const Result &b) { return a.jumps == b.jumps && a.steps == b.steps && a.err == b.err; }
} // namespace

TEST(timeline_room_changes_nothing_in_a_rough_cadence)
{
    // E337 (Ableton at 2048, err -767..852 within a second): no jump, few steps, exactly as without the room
    for (uint64_t seed = 1; seed <= 4; seed++) {
        const std::vector<int64_t> calls = uneven(seed, 10, 60, 10, 882);
        const Result r = run(calls, Ring{}, true, {}, false, false, ROOM);
        CHECK(r.jumps == 0);
        CHECK(r.steps < 400);
        CHECK(same(r, run(calls, Ring{})));
    }
}

TEST(timeline_room_keeps_the_drift_steps)
{
    // two clocks drifting: one-frame steps as before, on both sides. A drift is not taken into the room: it would
    // fill it in minutes (200 ppm = 529 frames a minute) and then cost a crossfade of the whole room
    for (double ppm : {-200.0, 200.0}) {
        Ring ring;
        ring.ppm = ppm;
        const Result r = run(steady(60), ring, true, {}, false, false, ROOM);
        CHECK(r.jumps == 0);
        CHECK(r.steps > 100);
        for (size_t i = 3000; i < r.err.size(); i++)
            CHECK(r.err[i] > -512 && r.err[i] < 600);
        CHECK(same(r, run(steady(60), ring)));
    }
}

TEST(timeline_room_two_clocks_stay_inside_the_read_margin)
{
    // #51 (E564, E578) with the room, the cases of the two tests above: no pulled-back block, no jump, the same steps
    struct Case {
        int block;
        double ppm;
    };
    for (const Case k : {Case{256, -90}, Case{256, -200}, Case{256, 90}, Case{32, -90}, Case{2048, -90}}) {
        Clocks c;
        c.block = k.block, c.ppm = k.ppm;
        const Drift off = two_clocks(c, 10 * 60, k.block >= 2048 ? 30 : 4);
        c.room = ROOM;
        const Drift d = two_clocks(c, 10 * 60, k.block >= 2048 ? 30 : 4);
        CHECK(d.jumps == 0);
        CHECK_EQ(d.pulls, 0);
        CHECK_EQ(d.steps, off.steps);
        CHECK_EQ(d.err_min, off.err_min);
        CHECK_EQ(d.err_max, off.err_max);
    }
}

TEST(timeline_room_needs_the_marker)
{
    // without the marker the stamps are not tied to the ring: a timeline behind it is real latency, so there is no
    // room - the offset is stepped out as before
    Ring ring;
    ring.offset_at = 5 * RATE, ring.offset = 1000;
    const Result r = run(steady(20), ring, false, {}, false, false, ROOM);
    CHECK(r.jumps == 0);
    CHECK(r.steps > 500);
    CHECK(std::llabs(r.err.back()) < 450);
    CHECK(same(r, run(steady(20), ring, false)));
    // and a rest kept with the marker is given up when the marker goes: stepped out, no jump
    TimelineFollow f;
    f.set_window(RATE / 2);
    f.set_room(ROOM);
    int64_t err = 1000, jumps = 0, steps = 0;
    for (int i = 0; i < 20 * RATE / BLOCK; i++) {
        const TimelineFollow::Move m = f.update(err, BLOCK, i < 5 * RATE / BLOCK);
        jumps += m.jump, steps += !m.jump && m.delta != 0;
        err -= m.delta;
    }
    CHECK_EQ(jumps, 0);
    CHECK(steps > 500);
    CHECK(err < 450);
}

namespace {
struct Steps {
    int n = 0, unfaded = 0;
    int64_t gap_min = INT64_MAX; // frames between two steps
};
// a mean offset of 300 (beyond STEP: 172 frames owed), stepped off in a closed loop at this block size
Steps step_off(int block)
{
    TimelineFollow f;
    f.set_window(RATE / 2);
    Steps s;
    int64_t err = 300, since = -1;
    for (int i = 0; i < 12 * RATE / block; i++) {
        const TimelineFollow::Move m = f.update(err, block, true);
        if (since >= 0)
            since += block;
        if (!m.jump && m.delta != 0) {
            s.n++, s.unfaded += !m.fade;
            if (since >= 0)
                s.gap_min = since < s.gap_min ? since : s.gap_min;
            since = 0;
            err -= m.delta;
        }
    }
    return s;
}
} // namespace

TEST(timeline_step_is_reported_as_faded)
{
    // a one-frame step skips or repeats a frame: hard, that is a click per block (86 a second at block 512, the
    // "metallic" sound of #37 and E579). The follower reports it with Move::fade, and the read path renders it as a
    // 128-frame crossfade from the old position (crossfade.h, delta +-1) like a catch-up
    for (const int block : {64, 128, 512, 2048}) {
        const Steps s = step_off(block);
        // 300 - HOLD = 172 owed; at 2048 a window has 43 blocks and the mean is back inside STEP after the first
        CHECK(block == 2048 ? s.n >= 40 : s.n == 172);
        CHECK_EQ(s.unfaded, 0);
    }
}

TEST(timeline_steps_leave_room_for_their_fade)
{
    // below block 128 a step per block would restart the 128-frame fade mid-way (a half step: a small click again).
    // So two steps are at least Crossfade::LEN frames apart: every 4th block at 32, every 2nd at 64 - 0.8 % at
    // most, far more than any clock drift needs. From 128 on one per block, as before
    for (const int block : {32, 64, 128, 256}) {
        const Steps s = step_off(block);
        CHECK_EQ(s.n, 172); // all of them still come
        CHECK_EQ(s.gap_min, block < Crossfade::LEN ? (int64_t)Crossfade::LEN : (int64_t)block);
    }
}

// The mutation pass on the older code (2026-11-23, findings/testing.md): the limits and the windows' exact ends
namespace {
struct Tally {
    int steps = 0, jumps = 0, fades = 0, at = -1; // at: the first jump's block
    int64_t by = 0;
};
// `n` blocks of `frames` with err(i), open loop (the moves are counted, not applied)
template <class E> Tally tally(TimelineFollow &f, int n, int64_t frames, E err, bool marker = true)
{
    Tally t;
    for (int i = 0; i < n; i++) {
        const TimelineFollow::Move m = f.update(err(i), frames, marker);
        if (m.jump && t.at < 0)
            t.at = i, t.by = m.delta;
        if (m.jump)
            t.jumps++, t.fades += m.fade;
        else
            t.steps += m.delta != 0;
    }
    return t;
}
auto constant(int64_t e)
{
    return [e](int) { return e; };
}
} // namespace

TEST(timeline_zero_window_and_margin_fall_back)
{
    // no Virus rate yet (0): the window stays 1, so an activation then still counts for a second at the real rate
    // (a window of 0 lost it, and the next set_window divided by it)
    TimelineFollow f;
    f.set_window(0);
    f.activated();
    f.set_window(RATE / 2);
    f.play_started();
    CHECK(burst(f, -600, 4).at >= 0);
    // VTI_NATIVE_MARGIN=0 (atoi of garbage): the default margin, so #51's lift still works
    auto lifted = [](int64_t m) {
        TimelineFollow g;
        g.set_window(RATE / 2);
        g.set_margin(m);
        return tally(g, 3 * RATE / BLOCK, BLOCK, [](int i) { return i % 2 ? 76 : -524; }).steps;
    };
    CHECK(lifted(512) > 0);
    CHECK_EQ(lifted(0), lifted(512));
}

TEST(timeline_calm_is_below_calm)
{
    // a cadence spread of exactly CALM is not calm: no Play window
    for (const int64_t spread : {TimelineFollow::CALM - 1, TimelineFollow::CALM}) {
        TimelineFollow f;
        f.set_window(RATE / 2);
        tally(f, RATE / BLOCK, BLOCK, [&](int i) { return i % 2 ? 0 : -spread; }, false);
        f.play_started();
        CHECK((burst(f, -600, 4).at >= 0) == (spread < TimelineFollow::CALM));
    }
}

TEST(timeline_blocks_before_any_play_learn_nothing)
{
    // a far-ahead first block is no burst: only a Play's learn second learns (else it pre-shifted the next Play)
    TimelineFollow f;
    f.set_window(RATE / 2);
    f.update(-3000, BLOCK, false);
    feed(f, 0, 2 * RATE / BLOCK);
    CHECK_EQ(f.play_started(true), 0);
}

TEST(timeline_rough_cadence_play_neither_learns_nor_preshifts)
{
    // a Play in a rough cadence has no Play window, so no learn second either: everything stays as without Play
    Model m;
    m.blocks(RATE / BLOCK);
    m.play();
    m.blocks(22, BLOCK);
    m.blocks(2 * RATE / BLOCK);
    rough_second(m.f);
    CHECK_EQ(m.f.play_started(true), 0);
}

TEST(timeline_learn_second_without_preshift_moves_as_without_play)
{
    // nothing learned yet: far off still jumps at once in the learn second, and its end is no reason to jump on one
    // block behind (that waits for the half second as always)
    TimelineFollow f;
    f.set_window(RATE / 2);
    feed(f, 0, RATE / BLOCK);
    f.play_started(true);
    CHECK_EQ(feed(f, 5000, 1).at, 0);
    TimelineFollow g;
    g.set_window(RATE / 2);
    feed(g, 0, RATE / BLOCK);
    g.play_started(true);
    feed(g, 0, RATE / BLOCK); // 172 blocks: the learn second's last block comes next
    CHECK(feed(g, 600, 1).at < 0);
}

TEST(timeline_burst_of_exactly_jump_is_not_learned)
{
    // a Play "burst" of exactly JUMP is none: learned as 512 it made the next real burst look like a host that
    // varies by ~2500 (spread_), and the Play after that got a quarter more room than the burst
    Model m;
    m.blocks(RATE / BLOCK);
    m.play();
    m.blocks(2, BLOCK); // 512 ahead
    m.blocks(3 * RATE / BLOCK);
    m.play();
    m.blocks(12, BLOCK); // 3072
    m.blocks(3 * RATE / BLOCK);
    const int64_t s = m.play();
    CHECK(s <= -12 * BLOCK + 256 && s >= -12 * BLOCK - 256);
}

TEST(timeline_preshift_learn_second_counts_minus_jump_as_inside)
{
    // pre-shifted: blocks at exactly -JUMP are the shift being eaten (no move, no window counted), so a burst at the
    // learn second's end still has its Play window (counted down by them, it had run out and the jump came late)
    Model m;
    m.blocks(RATE / BLOCK);
    m.play();
    m.blocks(22, BLOCK);
    m.blocks(2 * RATE / BLOCK);
    CHECK(m.f.play_started(true) < 0);
    const Tally t = tally(m.f, 175, BLOCK, [](int i) {
        return i < 170 ? -TimelineFollow::JUMP : i > 172 ? -1280 : -768 - (int64_t)(i - 170) * 256;
    });
    CHECK_EQ(t.at, 173);
    CHECK_EQ(t.by, -1280);
}

TEST(timeline_preshift_expects_up_to_its_edges)
{
    // process() re-anchors beyond 8192 unless expected: the range is -JUMP..shift + JUMP, both ends included
    Model m;
    m.blocks(RATE / BLOCK);
    m.play();
    m.blocks(46, BLOCK);
    m.blocks(2 * RATE / BLOCK);
    const int64_t s = m.play();
    CHECK(m.f.expects(-s + TimelineFollow::JUMP));
    CHECK(m.f.expects(-TimelineFollow::JUMP));
    TimelineFollow g;
    g.set_window(RATE / 2);
    CHECK(!g.expects(0)); // nothing pre-shifted: nothing expected
}

TEST(timeline_window_thresholds_are_exclusive)
{
    // every limit is "beyond": exactly AT_ONCE waits, exactly -JUMP is no burst and no mean jump, exactly STEP no step
    {
        TimelineFollow f;
        f.set_window(RATE / 2);
        CHECK(feed(f, TimelineFollow::AT_ONCE, 1).at < 0);
    }
    {
        TimelineFollow f;
        f.set_window(RATE / 2);
        feed(f, 0, RATE / BLOCK);
        f.play_started();
        CHECK(feed(f, -TimelineFollow::JUMP, RATE / BLOCK).at < 0);
    }
    {
        TimelineFollow f;
        f.set_window(RATE / 2);
        CHECK(feed(f, -TimelineFollow::JUMP, 3 * RATE / BLOCK).at < 0);
    }
    for (const int64_t e : {TimelineFollow::STEP, -TimelineFollow::STEP}) {
        TimelineFollow f;
        f.set_window(RATE / 2);
        CHECK_EQ(tally(f, 3 * RATE / BLOCK, BLOCK, constant(e)).steps, 0);
    }
}

TEST(timeline_far_side_flip_restarts_the_half_second)
{
    // behind for 0.4 s, then ahead: the half second counts from the flip (a mixed mean jumped by 337 at 0.5 s)
    TimelineFollow f;
    f.set_window(RATE / 2);
    const int flip = RATE * 4 / 10 / BLOCK;
    const Tally t = tally(f, RATE / BLOCK, BLOCK, [&](int i) { return i < flip ? 600 : -600; });
    CHECK_EQ(t.at, flip + RATE / 2 / BLOCK);
    CHECK_EQ(t.by, -600);
}

TEST(timeline_offset_only_the_mean_shows_jumps_after_two_seconds)
{
    // behind beyond JUMP on average, every other block back inside: the half-second rule never completes, the 2 s
    // mean jumps by it - on the first block inside JUMP once it is due (block 344 is at 1500: it waits one, E584)
    TimelineFollow f;
    f.set_window(RATE / 2);
    const Tally t = tally(f, 3 * RATE / BLOCK, BLOCK, [](int i) { return i % 2 ? -100 : 1500; });
    CHECK_EQ(t.at, 345);
    CHECK(t.by > TimelineFollow::JUMP);
}

TEST(timeline_windows_end_on_exact_block_counts)
{
    // 48 kHz at block 64 or 128: the windows are whole blocks, each ends on the block that fills it
    {
        TimelineFollow f; // the half second: 375 blocks of 64
        f.set_window(24000);
        CHECK_EQ(tally(f, 400, 64, constant(1000)).at, 374);
    }
    {
        TimelineFollow f; // the Play window: 375 blocks of 128, the next one is outside it
        f.set_window(24000);
        tally(f, 375, 128, constant(0));
        f.play_started();
        CHECK(tally(f, 376, 128, [](int i) { return i < 374 ? 0 : -600; }).at < 0);
    }
    {
        TimelineFollow f; // the spread buckets: 375 blocks of 64; block 376 is in the second, which still counts
        f.set_window(24000);
        tally(f, 752, 64, [](int i) { return i == 375 ? -2000 : 0; }, false);
        f.play_started();
        CHECK(burst(f, -600, 4).at < 0);
    }
    {
        // the 2 s window and its last half second (default window 22050, blocks of 150: 588 and 147 blocks); block 441
        // (66150 frames) is not in the last half second
        TimelineFollow f;
        const Tally t = tally(f, 588, 150, [](int i) { return i == 440 ? -400 : 400; });
        CHECK_EQ(t.at, 587);
        CHECK_EQ(t.by, 272);
        CHECK_EQ(t.fades, 1);
    }
}

TEST(timeline_catchup_thresholds)
{
    // a catch-up fade needs the whole 2 s window behind (owed > 0, a late half second alone waits for the next
    // window) and a catch-up beyond FADE (exactly FADE is stepped). Default window: no quiet time
    {
        TimelineFollow f;
        CHECK(tally(f, 345, BLOCK, [](int i) { return i < 258 ? 0 : 400; }).at < 0);
    }
    {
        TimelineFollow f;
        CHECK_EQ(tally(f, 345, BLOCK, constant(TimelineFollow::FADE + TimelineFollow::HOLD)).jumps, 0);
    }
}

TEST(timeline_lift_thresholds_are_exact)
{
    // #51's lift (margin 512): a low end below -384 in more than 1/16 of the blocks, a range below 768, and a lifted
    // mean up to STEP. Exactly -384, exactly 1/16 and exactly 768 do not lift; a lifted mean of exactly STEP does
    auto steps = [](int64_t frames, int n, auto err) {
        TimelineFollow f;
        f.set_window(RATE / 2);
        f.set_margin(512);
        return tally(f, n, frames, err).steps;
    };
    CHECK_EQ(steps(BLOCK, 3 * RATE / BLOCK, [](int i) { return i % 2 ? 0 : -384; }), 0);
    CHECK_EQ(steps(251, 500, [](int i) { return i % 16 ? 0 : -400; }), 0); // 352 blocks a window, 22 low
    CHECK_EQ(steps(BLOCK, 3 * RATE / BLOCK, [](int i) { return i % 2 ? 368 : -400; }), 0);
    CHECK(steps(BLOCK, 3 * RATE / BLOCK, [](int i) { return i % 5 ? 128 : -512; }) > 0); // mean 0, lifted to -256
}
