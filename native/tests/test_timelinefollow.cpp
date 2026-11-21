// The Virus timeline after its anchor (E337): an uneven host cadence must not make it jump back and forth (each jump
// is a click), while a real lasting offset (REAPER after a render, E279) still jumps soon. The tests run a small model:
// the ring grows in 441-frame USB chunks (10 ms), the host calls 256-frame blocks at the given times, the timeline
// moves by the block plus what TimelineFollow says - the same arithmetic as process().
#include "check.h"
#include "device/timelinefollow.h"
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace vti;

namespace {

constexpr int RATE = 44100, BLOCK = 256;

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
    int jumps = 0, steps = 0;
    std::vector<int64_t> jump_at, jump_by;
    std::vector<int64_t> t, err;
    std::vector<int64_t> shift_at, shift_by; // pre-shifts at Play (from a stop, a burst learned before)
};

// `plays`: host times of Play starts; as in process(), the block that sees Play tells the follower after its update.
// `activated`: each Play comes right after setActive(1) (REAPER), seen by process() before the Play block's update
Result run(const std::vector<int64_t> &calls, const Ring &ring, bool marker = true,
           const std::vector<int64_t> &plays = {}, bool activated = false, bool from_stop = false)
{
    TimelineFollow f;
    f.set_window(RATE / 2);
    Result r;
    int64_t tl = ring.newest(calls[0]); // anchored at the first block
    size_t next_play = 0;
    for (int64_t c : calls) {
        const int64_t err = ring.newest(c) - tl;
        r.t.push_back(c), r.err.push_back(err);
        if (activated && next_play < plays.size() && c >= plays[next_play])
            f.activated();
        const TimelineFollow::Move m = f.update(err, BLOCK, marker);
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
    // the Virus' clock against the sound card's: one-frame steps keep the error inside the read margin (pos is
    // block + 640 frames behind; below err -640 a block would read frames that are not there yet)
    for (double ppm : {-200.0, 200.0}) {
        Ring ring;
        ring.ppm = ppm;
        const Result r = run(steady(60), ring);
        CHECK(r.jumps == 0);
        CHECK(r.steps > 100); // 200 ppm = 529 frames a minute, part of it inside the band
        for (size_t i = 3000; i < r.err.size(); i++)
            CHECK(r.err[i] > -600 && r.err[i] < 600);
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
        if (const TimelineFollow::Move m = f.update(-600 - i * 50, BLOCK, true); m.jump)
            r.by = m.delta, r.at = i;
    CHECK(r.at == 16 - 1); // 16 blocks of 256 > SETTLE (2048)
    CHECK(r.by == -600 - r.at * 50);
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
int jumps_between(const Result &r, int64_t a, int64_t b)
{
    int n = 0;
    for (int64_t t : r.jump_at)
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
    // the next burst is smaller (12 blocks instead of 22): the timeline is behind by the rest, a jump forward fixes
    // it, and the third Play shifts by the new size
    const std::vector<int64_t> plays = {5 * RATE, 12 * RATE, 19 * RATE};
    const Result r = run(steady_with_plays(25, plays, 0, {22, 12, 12}), Ring{}, true, plays, false, true);
    CHECK(r.shift_by.size() == 2);
    if (r.shift_by.size() == 2)
        CHECK(r.shift_by[1] <= -12 * BLOCK + 441 && r.shift_by[1] >= -12 * BLOCK - 441);
    CHECK(in_sync_after(r, plays[1]));
    CHECK(jumps_between(r, plays[2], plays[2] + 3 * RATE) == 0);
}

TEST(timeline_learned_burst_too_small_jumps_the_rest_and_learns_more)
{
    const std::vector<int64_t> plays = {5 * RATE, 12 * RATE, 19 * RATE};
    const Result r = run(steady_with_plays(25, plays, 0, {10, 22, 22}), Ring{}, true, plays, false, true);
    CHECK(r.shift_by.size() == 2);
    if (r.shift_by.size() == 2)
        CHECK(r.shift_by[1] <= -22 * BLOCK + 441 && r.shift_by[1] >= -22 * BLOCK - 441);
    CHECK(in_sync_after(r, plays[1]));
    CHECK(jumps_between(r, plays[2], plays[2] + 3 * RATE) == 0);
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
